// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2021 Alexandre Courbot <gnurou@gmail.com>
/*
 * NCT6795D/NCT6797D LED driver
 *
 * Driver to control the RGB interfaces found on some MSI motherboards.
 * This is for the most part a port of the MSI-RGB user-space program
 * by Simonas Kazlauskas (https://github.com/nagisa/msi-rgb.git) to the Linux
 * kernel LED interface.
 *
 * It is more limited than the original program due to limitations in the LED
 * interface. For now, only static colors are possible.
 *
 * Supported motherboards (a per MSI-RGB's README):
 * B350 MORTAR ARCTIC
 * B350 PC MATE
 * B350 TOMAHAWK
 * B360M GAMING PLUS
 * B450 GAMING PLUS AC
 * B450 MORTAR
 * B450 TOMAHAWK
 * B450M GAMING PLUS
 * H270 MORTAR ARCTIC
 * H270 TOMAHAWK ARCTIC
 * X470 GAMING PLUS
 * X470 GAMING PRO
 * Z270 GAMING M7
 * Z270 SLI PLUS
 * Z370 MORTAR
 * Z370 PC PRO
 *
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/led-class-multicolor.h>
#include <linux/module.h>
#include <linux/platform_device.h>

/* Copied from drivers/hwmon/nct6775.c */

#define SIO_REG_LDSEL 0x07 /* Logical device select */
#define SIO_REG_DEVID 0x20 /* Device ID (2 bytes) */

static inline void superio_outb(int ioreg, int reg, int val)
{
	outb(reg, ioreg);
	outb(val, ioreg + 1);
}

static inline int superio_inb(int ioreg, int reg)
{
	outb(reg, ioreg);
	return inb(ioreg + 1);
}

static inline void superio_select(int ioreg, int ld)
{
	outb(SIO_REG_LDSEL, ioreg);
	outb(ld, ioreg + 1);
}

static inline int superio_enter(int ioreg)
{
	if (!request_muxed_region(ioreg, 2, "NCT6795D LED"))
		return -EBUSY;

	outb(0x87, ioreg);
	outb(0x87, ioreg);

	return 0;
}

static inline void superio_exit(int ioreg)
{
	outb(0xaa, ioreg);
	outb(0x02, ioreg);
	outb(0x02, ioreg + 1);
	release_region(ioreg, 2);
}

/* End copy from drivers/hwmon/nct6775.c */

#define NCT6795D_DEVICE_NAME "nct6795d"
#define DEFAULT_STEP_DURATION 25

#define NCT6795D_RGB_BANK 0x12

/* Color registers */
#define NCT6795D_RED_CELL 0xf0
#define NCT6795D_GREEN_CELL 0xf4
#define NCT6795D_BLUE_CELL 0xf8

#define NCT6795D_PARAMS_0 0xe4
/* Enable/disable LED overall */
#define PARAMS_0_LED_ENABLE(e) ((e) ? 0x0 : 0x1)
/* Enable/disable smooth pulsing */
#define PARAMS_0_LED_PULSE_ENABLE(e) ((e) ? 0x08 : 0x0)
/* Duration between blinks (0 means always on) */
#define PARAMS_0_BLINK_DURATION(x) ((x) & 0x07)

#define NCT6795D_PARAMS_1 0xfe
/* Lower part of step duration (8 out of 9 bits) */
#define PARAMS_1_STEP_DURATION_LOW(s) ((s) & 0xff)

#define NCT6795D_PARAMS_2 0xff
/* Enable fade-in effect for specified primitive */
#define PARAMS_2_FADE_COLOR(r, g, b)                                           \
	(0xe0 ^ (((r) ? 0x80 : 0x0) | ((g) ? 0x40 : 0x0) | ((b) ? 0x20 : 0x0)))
/* Whether the specified colors should be inverted */
#define PARAMS_2_INVERT_COLOR(r, g, b)                                         \
	(((r) ? 0x10 : 0x0) | ((g) ? 0x08 : 0x0) | ((b) ? 0x04 : 0x0))
/* Disable board leds if the LED_DISABLE bit is set */
#define PARAMS_2_DISABLE_BOARD_LED 0x02
/* MSB (9th bit) of step duration */
#define PARAMS_2_STEP_DURATION_HIGH(s) (((s) >> 8) & 0x01)

enum { RED = 0, GREEN, BLUE, NUM_COLORS };

static u8 init_vals[NUM_COLORS];
module_param_named(r, init_vals[RED], byte, 0);
MODULE_PARM_DESC(r, "Initial red intensity (default 0)");
module_param_named(g, init_vals[GREEN], byte, 0);
MODULE_PARM_DESC(g, "Initial green intensity (default 0)");
module_param_named(b, init_vals[BLUE], byte, 0);
MODULE_PARM_DESC(b, "Initial blue intensity (default 0)");

struct nct6795d_led {
	struct device *dev;
	u16 base_port;
	struct led_classdev_mc mc_cdev;
	struct mc_subled subled[NUM_COLORS];
};

enum nct679x_chip {
	NCT6795D = 0,
	NCT6797D,
};

const char *chip_names[] = {
	"NCT6795D",
	"NCT6797D",
};

static enum nct679x_chip nct6795d_led_detect(u16 base_port)
{
	int ret;
	u16 val;

	ret = superio_enter(base_port);
	if (ret)
		return ret;

	val = (superio_inb(base_port, SIO_REG_DEVID) << 8) |
	      superio_inb(base_port, SIO_REG_DEVID + 1);

	switch (val & 0xfff0) {
	case 0xd350:
		ret = NCT6795D;
		break;
	case 0xd450:
		ret = NCT6797D;
		break;
	default:
		ret = -ENXIO;
		break;
	}

	superio_exit(base_port);
	return ret;
}

static int nct6795d_led_setup(const struct nct6795d_led *led)
{
	int ret;
	u16 val;

	ret = superio_enter(led->base_port);
	if (ret)
		return ret;

	/* Without this pulsing does not work? */
	superio_select(led->base_port, 0x09);
	val = superio_inb(led->base_port, 0x2c);
	if ((val & 0x10) != 0x10)
		superio_outb(led->base_port, 0x2c, val | 0x10);

	superio_select(led->base_port, NCT6795D_RGB_BANK);

	/* Check if RGB control enabled */
	val = superio_inb(led->base_port, 0xe0);
	if ((val & 0xe0) != 0xe0)
		superio_outb(led->base_port, 0xe0, val | 0xe0);

	/*
	 * Set some static parameters: led enabled, no pulse, no blink,
	 * default step duration, no fading, no inversion. These fancy features
	 * are not supported by the LED API at the moment.
	 */
	superio_outb(led->base_port, NCT6795D_PARAMS_0,
		     PARAMS_0_LED_ENABLE(true) |
			     PARAMS_0_LED_PULSE_ENABLE(false) |
			     PARAMS_0_BLINK_DURATION(0));

	superio_outb(led->base_port, NCT6795D_PARAMS_1,
		     PARAMS_1_STEP_DURATION_LOW(DEFAULT_STEP_DURATION));

	superio_outb(led->base_port, NCT6795D_PARAMS_2,
		     PARAMS_2_FADE_COLOR(false, false, false) |
			PARAMS_2_INVERT_COLOR(false, false, false) |
			PARAMS_2_DISABLE_BOARD_LED |
			PARAMS_2_STEP_DURATION_HIGH(DEFAULT_STEP_DURATION));

	superio_exit(led->base_port);
	return 0;
}

static void nct6795d_led_commit_color(const struct nct6795d_led *led,
				      size_t color_cell,
				      enum led_brightness brightness)
{
	int i;
	/*
	 * These 8 4-bit nibbles represent brightness intensity for each time
	 * frame. We set them all to the same value to get a constant color.
	 */
	const u8 b = (brightness << 4) | brightness;

	for (i = 0; i < 4; i++)
		superio_outb(led->base_port, color_cell + i, b);
}

static int nct6795d_led_commit(const struct nct6795d_led *led)
{
	const struct mc_subled *subled = led->subled;
	int ret;

	dev_dbg(led->dev, "setting values: R=%d G=%d B=%d\n",
		subled[RED].brightness, subled[GREEN].brightness,
		subled[BLUE].brightness);

	ret = superio_enter(led->base_port);
	if (ret)
		return ret;

	superio_select(led->base_port, NCT6795D_RGB_BANK);

	nct6795d_led_commit_color(led, NCT6795D_RED_CELL,
				  subled[RED].brightness);
	nct6795d_led_commit_color(led, NCT6795D_GREEN_CELL,
				  subled[GREEN].brightness);
	nct6795d_led_commit_color(led, NCT6795D_BLUE_CELL,
				  subled[BLUE].brightness);

	superio_exit(led->base_port);
	return 0;
}

static void nct6795d_led_brightness_set(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct nct6795d_led *led =
		container_of(mc_cdev, struct nct6795d_led, mc_cdev);

	led_mc_calc_color_components(mc_cdev, brightness);

	nct6795d_led_commit(led);
}

static int nct6795d_led_probe(struct platform_device *pdev)
{
	struct nct6795d_led *led;
	const struct resource *res;
	int ret;

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->dev = &pdev->dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_REG, "io_base");
	if (IS_ERR(res))
		return PTR_ERR(res);

	led->base_port = res->start;

	led->subled[RED].color_index = LED_COLOR_ID_RED;
	led->subled[RED].channel = 0;
	led->subled[RED].intensity = init_vals[RED];
	led->subled[GREEN].color_index = LED_COLOR_ID_GREEN;
	led->subled[GREEN].channel = 1;
	led->subled[GREEN].intensity = init_vals[GREEN];
	led->subled[BLUE].color_index = LED_COLOR_ID_BLUE;
	led->subled[BLUE].channel = 2;
	led->subled[BLUE].intensity = init_vals[BLUE];

	led->mc_cdev.subled_info = led->subled;
	led->mc_cdev.num_colors = NUM_COLORS;
	led->mc_cdev.led_cdev.name = NCT6795D_DEVICE_NAME;
	led->mc_cdev.led_cdev.max_brightness = 0xf;
	led->mc_cdev.led_cdev.brightness = led->mc_cdev.led_cdev.max_brightness;
	led->mc_cdev.led_cdev.brightness_set = nct6795d_led_brightness_set;

	ret = devm_led_classdev_multicolor_register_ext(&pdev->dev,
							&led->mc_cdev, NULL);
	if (ret)
		return ret;

	dev_set_drvdata(&pdev->dev, led);

	ret = nct6795d_led_setup(led);
	if (ret)
		return ret;

	nct6795d_led_brightness_set(&led->mc_cdev.led_cdev,
				    led->mc_cdev.led_cdev.brightness);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int nct6795d_led_suspend(struct device *dev)
{
	return 0;
}

static int nct6795d_led_resume(struct device *dev)
{
	struct nct6795d_led *led = dev_get_drvdata(dev);
	int ret;

	ret = nct6795d_led_setup(led);
	if (ret)
		return ret;

	return nct6795d_led_commit(led);
}
#endif

static SIMPLE_DEV_PM_OPS(nct_6795d_led_pm_ops, nct6795d_led_suspend,
			 nct6795d_led_resume);

static struct platform_driver nct6795d_led_driver = {
	.driver = {
		.name = "nct6795d_led",
		.pm = &nct_6795d_led_pm_ops,
	},
	.probe = nct6795d_led_probe,
};

static struct platform_device *nct6795d_led_pdev;

static int __init nct6795d_led_init(void)
{
	static const u16 io_bases[] = { 0x4e, 0x2e };
	struct resource io_res = {
		.name = "io_base",
		.flags = IORESOURCE_REG,
	};
	enum nct679x_chip detected_chip;
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(io_bases); i++) {
		detected_chip = nct6795d_led_detect(io_bases[i]);
		if (detected_chip >= 0)
			break;
	}
	if (i == ARRAY_SIZE(io_bases)) {
		pr_err("failed to detect nct6795d chip\n");
		return -ENXIO;
	}

	pr_info("%s: found %s chip at address 0x%x\n", KBUILD_MODNAME,
		chip_names[detected_chip], io_bases[i]);

	ret = platform_driver_register(&nct6795d_led_driver);
	if (ret)
		return ret;

	nct6795d_led_pdev =
		platform_device_alloc(NCT6795D_DEVICE_NAME "_led", 0);
	if (!nct6795d_led_pdev) {
		ret = -ENOMEM;
		goto error_pdev_alloc;
	}

	io_res.start = io_bases[i];
	io_res.end = io_res.start;
	ret = platform_device_add_resources(nct6795d_led_pdev, &io_res, 1);
	if (ret)
		goto error_pdev_resource;

	ret = platform_device_add(nct6795d_led_pdev);
	if (ret)
		goto error_pdev_resource;

	return 0;

error_pdev_resource:
	platform_device_del(nct6795d_led_pdev);
error_pdev_alloc:
	platform_driver_unregister(&nct6795d_led_driver);
	return ret;
}

static void __exit nct6795d_led_exit(void)
{
	platform_device_unregister(nct6795d_led_pdev);
	platform_driver_unregister(&nct6795d_led_driver);
}

module_init(nct6795d_led_init);
module_exit(nct6795d_led_exit);

MODULE_AUTHOR("Alexandre Courbot <gnurou@gmail.com>");
MODULE_DESCRIPTION("LED driver for NCT6795D");
MODULE_LICENSE("GPL");
