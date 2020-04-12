Linux Kernel Module for NCT6795D LEDs
====================================

This is a basic kernel module for controlling the LEDs found on motherboards
with the NCT6795D chip (and possibly others, not supported yet).


How to build
------------

Just do a `make && sudo make install && sudo depmod -a`.

Usage and parameters
--------------------

Run

    # sudo modprobe leds_nct6795d

To insert the module. This should create led devices under
`/sys/class/leds/nct6795d:*`, one per red, green, and blue component. Each
device supports intensity in the [0-15] range.

The module can also take parameters to set the LEDs initial value, e.g.

    # sudo modprobe leds_nct6795d r=7, g=7, b=7

will set the LED intensity to half of maximum brightness.

Credit
------
The LED programming patterns have been reproduced from the
[msi-rgb project](https://github.com/nagisa/msi-rgb).
