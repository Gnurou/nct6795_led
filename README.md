Linux Kernel Module for NCT6795 LEDs
====================================

This is a quick kernel module for controlling the LEDs found on motherboards
with the NCT6795 chip (and possibly others, not supported yet).

Just build with `make && sudo make install`, then run

    # sudo modprobe nct6795_led

This will create led devices under `/sys/class/leds/nct6795:*`, one per red,
green, and blue component.

The module also takes parameters to set the LEDs initial value, e.g.

    # sudo modprobe nct6795_led r=128, g=128, b=128

will set the LED intensity to half of maximum brightness.

Credit
------
The LED programming patterns have been reproduced from the
[msi-rgb project](https://github.com/nagisa/msi-rgb).
