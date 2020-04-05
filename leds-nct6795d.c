// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2020 Alexandre Courbot <gnurou@gmail.com>
/*
 * NCT6795D LED driver
 *
 * Driver to control the RGB interfaces found on some MSI motherboards.
 * This is for the most part a port of the MSI-RGB user-space program
 * (https://github.com/nagisa/msi-rgb.git) to the Linux kernel LED interface.
 * It is more limited than the original program due to limitations in the LED
 * interface. For now, only static displays of colors are possible.
 *
 * Supported motherboards (a per MSI-RGB's README):
 * B350 MORTAR ARCTIC
 * B350 PC MATE
 * B350 TOMAHAWK
 * B360M GAMING PLUS
 * B450 GAMING PLUS AC
 * B450 MORTAR
 * B450 TOMAHAWK
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

#include <asm/io.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define NCT6795D_NAME "nct6795d"

/* Copied from drivers/hwmon/nct6775.c */

#define NCT6775_LD_12 0x12

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

enum { RED = 0, GREEN, BLUE, NUM_COLORS };

static u8 init_vals[NUM_COLORS];
module_param_named(r, init_vals[RED], byte, 0);
MODULE_PARM_DESC(r, "Initial red intensity (default 0)");
module_param_named(g, init_vals[GREEN], byte, 0);
MODULE_PARM_DESC(g, "Initial green intensity (default 0)");
module_param_named(b, init_vals[BLUE], byte, 0);
MODULE_PARM_DESC(b, "Initial blue intensity (default 0)");

#define DEFAULT_BASE_PORT 0x4e
static u16 base_port = DEFAULT_BASE_PORT;
module_param_named(base_port, base_port, ushort, 0444);
MODULE_PARM_DESC(base_port, "Base port to probe (default 0x4e)");

static const char *led_names[NUM_COLORS] = {
	"red:",
	"green:",
	"blue:",
};

struct nct6795d_led {
	struct device *dev;
	u16 base_port;
	struct led_classdev cdev[NUM_COLORS];
};

static int nct6795d_led_detect(struct device *dev, u16 base_port)
{
	int ret;
	u16 val;

	ret = superio_enter(base_port);
	if (ret)
		return ret;

	val = (superio_inb(base_port, SIO_REG_DEVID) << 8) |
	      superio_inb(base_port, SIO_REG_DEVID + 1);

	if ((val & 0xfff8) != 0xd350) {
		dev_err(dev, "nct6795d not found!\n");
		ret = -ENXIO;
		goto err_not_found;
	}

err_not_found:
	superio_exit(base_port);
	return ret;
}

static void nct6795d_led_commit_color(const struct nct6795d_led *led,
				      size_t index,
				      enum led_brightness brightness)
{
	int i;

	/*
	 * The 8 4-bit nibbles represent brightness intensity for each time
	 * frame. We set them all to the same value.
	 */
	brightness = (brightness << 4) | brightness;
	for (i = 0; i <= NUM_COLORS; i++) {
		superio_outb(led->base_port, index + i, brightness);
	}
}

static int nct6795d_led_commit(const struct nct6795d_led *led, u8 color_mask)
{
	int ret;
	u16 val;
	const struct led_classdev *cdev = led->cdev;

	ret = superio_enter(led->base_port);
	if (ret)
		return ret;

	/* Check if RGB control enabled */
	val = superio_inb(led->base_port, 0xe0);
	if ((val & 0xe0) != 0xe0) {
		superio_outb(led->base_port, 0xe0, 0xe0 | (val & !0xe0));
	}

	/* Without this pulsing does not work? */
	/*
	superio_outb(led->base_port, 0x07, 0x09);
	val = superio_inb(led->base_port, 0x2c);
	superio_outb(led->base_port, 0x2c, val | 0x10);
	*/

	/* Select the 0x12th bank (RGB) */
	superio_select(led->base_port, NCT6775_LD_12);

	dev_dbg(led->dev, "setting values: R=%d G=%d B=%d\n",
		cdev[RED].brightness, cdev[GREEN].brightness,
		cdev[BLUE].brightness);

	if (color_mask & BIT(RED))
		nct6795d_led_commit_color(led, 0xf0, cdev[RED].brightness);
	if (color_mask & BIT(GREEN))
		nct6795d_led_commit_color(led, 0xf4, cdev[GREEN].brightness);
	if (color_mask & BIT(BLUE))
		nct6795d_led_commit_color(led, 0xf8, cdev[BLUE].brightness);

	superio_exit(led->base_port);

	return 0;
}

static void nct6795d_led_brightness_set_red(struct led_classdev *cdev,
					    enum led_brightness value)
{
	const struct nct6795d_led *led =
		container_of(cdev, struct nct6795d_led, cdev[RED]);
	nct6795d_led_commit(led, BIT(RED));
}

static void nct6795d_led_brightness_set_green(struct led_classdev *cdev,
					      enum led_brightness value)
{
	const struct nct6795d_led *led =
		container_of(cdev, struct nct6795d_led, cdev[GREEN]);
	nct6795d_led_commit(led, BIT(GREEN));
}

static void nct6795d_led_brightness_set_blue(struct led_classdev *cdev,
					     enum led_brightness value)
{
	const struct nct6795d_led *led =
		container_of(cdev, struct nct6795d_led, cdev[BLUE]);
	nct6795d_led_commit(led, BIT(BLUE));
}

static void (*brightness_set[NUM_COLORS])(struct led_classdev *,
					  enum led_brightness) = {
	&nct6795d_led_brightness_set_red,
	&nct6795d_led_brightness_set_green,
	&nct6795d_led_brightness_set_blue,
};

static struct nct6795d_led *nct6795d_led_create(struct platform_device *pdev,
						u16 base_port)
{
	struct nct6795d_led *led;
	int ret;
	int i;

	ret = nct6795d_led_detect(&pdev->dev, base_port);
	if (ret)
		return ERR_PTR(ret);

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return ERR_PTR(-ENOMEM);

	led->dev = &pdev->dev;
	led->base_port = base_port;

	for (i = 0; i < NUM_COLORS; i++) {
		struct led_classdev *cdev = &led->cdev[i];
		struct led_init_data init_data = {};

		init_data.devicename = NCT6795D_NAME;
		init_data.default_label = led_names[i];

		cdev->brightness = init_vals[i];
		cdev->max_brightness = 0xf;
		cdev->brightness_set = brightness_set[i];
		ret = devm_led_classdev_register_ext(&pdev->dev, cdev,
						     &init_data);
		if (ret)
			return ERR_PTR(ret);
	}

	dev_set_drvdata(&pdev->dev, led);

	nct6795d_led_commit(led, BIT(RED) | BIT(GREEN) | BIT(BLUE));

	return led;
}

static int nct6795d_led_probe(struct platform_device *pdev)
{
	struct nct6795d_led *led;
	int ret;

	ret = nct6795d_led_detect(&pdev->dev, base_port);
	if (ret)
		return ret;

	led = nct6795d_led_create(pdev, base_port);
	if (IS_ERR(led))
		return PTR_ERR(led);

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
	const u8 color_mask = BIT(RED) | BIT(GREEN) | BIT(BLUE);
	int ret;

	/* For some reason this needs to be done twice?? */
	ret = nct6795d_led_commit(led, color_mask);
	if (ret)
		return ret;
	return nct6795d_led_commit(led, color_mask);
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
	int ret;

	ret = platform_driver_register(&nct6795d_led_driver);
	if (ret)
		return ret;

	nct6795d_led_pdev = platform_device_alloc(NCT6795D_NAME "_led", 0);
	if (!nct6795d_led_pdev) {
		ret = -ENOMEM;
		goto error_pdev_alloc;
	}

	ret = platform_device_add(nct6795d_led_pdev);
	if (ret)
		goto error_pdev_alloc;

	return 0;

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
