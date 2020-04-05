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

/* Copied from drivers/hwmon/nct6775.c */

#define NCT6775_LD_12		0x12

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */

static inline void
superio_outb(int ioreg, int reg, int val)
{
	outb(reg, ioreg);
	outb(val, ioreg + 1);
}

static inline int
superio_inb(int ioreg, int reg)
{
	outb(reg, ioreg);
	return inb(ioreg + 1);
}

static inline void
superio_select(int ioreg, int ld)
{
	outb(SIO_REG_LDSEL, ioreg);
	outb(ld, ioreg + 1);
}

static inline int
superio_enter(int ioreg)
{
	if (!request_muxed_region(ioreg, 2, "NCT6795D LED"))
		return -EBUSY;

	outb(0x87, ioreg);
	outb(0x87, ioreg);

	return 0;
}

static inline void
superio_exit(int ioreg)
{
	outb(0xaa, ioreg);
	outb(0x02, ioreg);
	outb(0x02, ioreg + 1);
	release_region(ioreg, 2);
}

/* End copy from drivers/hwmon/nct6775.c */


enum { RED = 0, GREEN, BLUE };

static u8 init_vals[3];
module_param_named(r, init_vals[RED], byte, 0);
MODULE_PARM_DESC(r, "Initial red intensity");
module_param_named(g, init_vals[GREEN], byte, 0);
MODULE_PARM_DESC(g, "Initial green intensity");
module_param_named(b, init_vals[BLUE], byte, 0);
MODULE_PARM_DESC(b, "Initial blue intensity");

static const char *led_names[3] = {
	"nct6795d:red:0",
	"nct6795d:green:0",
	"nct6795d:blue:0",
};

struct nct6795d_led {
	struct led_classdev cdev[3];
};

static const int base = 0x4e;

static int nct6795d_led_detect(struct device *dev)
{
	int err;
	u16 val;

	err = superio_enter(base);
	if (err)
		return err;

	val = (superio_inb(base, SIO_REG_DEVID) << 8) |
	       superio_inb(base, SIO_REG_DEVID + 1);

	if ((val & 0xfff8) != 0xd350) {
		dev_err(dev, "nct6795d not found!\n");
		err = -ENXIO;
		goto err;
	}

err:
	superio_exit(base);
	return err;
}

static int nct6795d_led_program(struct nct6795d_led *led)
{
	int err;
	int i;
	u16 val;

	err = superio_enter(base);
	if (err)
		return err;

	/* Check if RGB control enabled */
	val = superio_inb(base, 0xe0);
	if ((val & 0xe0) != 0xe0) {
		superio_outb(base, 0xe0, 0xe0 | (val & !0xe0));
	}

	/* Without this pulsing does not work? */
	/*
	superio_outb(base, 0x07, 0x09);
	val = superio_inb(base, 0x2c);
	superio_outb(base, 0x2c, val | 0x10);
	*/

	/* Select the 0x12th bank (RGB) */
	superio_select(base, NCT6775_LD_12);

	dev_info(led->cdev->dev, "programming values: %d %d %d\n",
		 led->cdev[RED].brightness, led->cdev[GREEN].brightness,
		 led->cdev[BLUE].brightness);

	for (i = 0; i < 4; i++)
		superio_outb(base, 0xf0 + i, led->cdev[RED].brightness);
	for (i = 0; i < 4; i++)
		superio_outb(base, 0xf4 + i, led->cdev[GREEN].brightness);
	for (i = 0; i < 4; i++)
		superio_outb(base, 0xf8 + i, led->cdev[BLUE].brightness);

	superio_exit(base);

	return 0;
}

static void nct6795d_led_brightness_set_color(struct led_classdev *cdev,
					     int color,
					     enum led_brightness value)
{
	struct nct6795d_led *led;
	led = container_of(cdev, struct nct6795d_led, cdev[color]);

	nct6795d_led_program(led);
}

static void nct6795d_led_brightness_set_red(struct led_classdev *cdev,
					   enum led_brightness value)
{
	nct6795d_led_brightness_set_color(cdev, RED, value);
}

static void nct6795d_led_brightness_set_green(struct led_classdev *cdev,
					     enum led_brightness value)
{
	nct6795d_led_brightness_set_color(cdev, GREEN, value);
}

static void nct6795d_led_brightness_set_blue(struct led_classdev *cdev,
					    enum led_brightness value)
{
	nct6795d_led_brightness_set_color(cdev, BLUE, value);
}

static void (*brightness_set[3])(struct led_classdev *, enum led_brightness) = {
	&nct6795d_led_brightness_set_red,
	&nct6795d_led_brightness_set_green,
	&nct6795d_led_brightness_set_blue,
};

static int nct6795d_led_probe(struct platform_device *pdev)
{
	struct nct6795d_led *led;
	int err;
	int i;

	err = nct6795d_led_detect(&pdev->dev);
	if (err)
		return err;

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	for (i = 0; i < 3; i++) {
		struct led_classdev *cdev = &led->cdev[i];

		cdev->name = led_names[i];
		cdev->brightness = init_vals[i];
		cdev->max_brightness = LED_FULL;
		cdev->brightness_set = brightness_set[i];
		err = devm_led_classdev_register(&pdev->dev, cdev);
		if (err)
			return err;
	}

	dev_set_drvdata(&pdev->dev, led);

	nct6795d_led_program(led);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int nct6795d_led_suspend(struct device *dev)
{
	return 0;
}

static int nct6795d_led_resume(struct device *dev)
{
	struct nct6795d_led *led =dev_get_drvdata(dev);
	int ret;

	/* For some reason this needs to be done twice?? */
	ret = nct6795d_led_program(led);
	if (ret)
		return ret;
	return nct6795d_led_program(led);
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

static struct platform_device* nct6795d_led_pdev;

static int __init nct6795d_led_init(void)
{
	int err;

	err = platform_driver_register(&nct6795d_led_driver);
	if (err)
		return err;

	nct6795d_led_pdev = platform_device_alloc("nct6795d_led", 0);
	if (!nct6795d_led_pdev) {
		err = -ENOMEM;
		goto error_pdev_alloc;
	}

	err = platform_device_add(nct6795d_led_pdev);
	if (err)
		goto error_pdev_alloc;

	return 0;

error_pdev_alloc:
	platform_driver_unregister(&nct6795d_led_driver);
	return err;
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
