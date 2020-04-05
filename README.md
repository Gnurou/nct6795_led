Linux Kernel Module for NCT6795D LEDs
====================================

This is a quick kernel module for controlling the LEDs found on motherboards
with the NCT6795D chip (and possibly others, not supported yet).

Just build with `make && sudo make install`, then run

    # sudo modprobe leds_nct6795d

This will create led devices under `/sys/class/leds/nct6795d:*`, one per red,
green, and blue component. The led devices support intensity in the [0-15]
range.

The module also takes parameters to set the LEDs initial value, e.g.

    # sudo modprobe leds_nct6795d r=7, g=7, b=7

will set the LED intensity to half of maximum brightness.

Credit
------
The LED programming patterns have been reproduced from the
[msi-rgb project](https://github.com/nagisa/msi-rgb).
