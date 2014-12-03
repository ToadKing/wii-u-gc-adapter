wii-u-gc-adapter
================

Tool for using the Wii U GameCube Adapter on Linux

Prerequisites
-------------
* libudev
* libusb(x) >= 1.0.16

Building
--------
Just run `make`. That's all there is to it!

Usage
-----
Simply run the program. You'll probably have to run it as root in order to
grab the USB device from the kernel and use the uinput interface. Both of
these can be worked around with udev rules, which I'm currently too lazy to
add at the moment. To stop the program just kill it in any way you want.

Seperate virtual controllers are created for each one plugged into the adapter
and hotplugging (both controllers and adapters) is supported.

Quirks
------
* It's new, so there might be bugs! Please report them!
* The uinput kernel module is required. If it's not autoloaded, you should do
  so with `modprobe uinput`
* Input ranges on the sticks/analog triggers are scaled to try to match the
  physical ranges of the controls. To remove this scaling run the program with
  the `--raw` flag.
* If all your controllers start messing with the mouse cursor, you can fix
  them with this xorg.conf rule. (You can place it in a file in xorg.conf.d)

````
Section "InputClass"
        Identifier "Wii U GameCube Adapter Blacklist"
        MatchProduct "Wii U GameCube Adapter Port "
        Option "Ignore" "on"
EndSection
````
