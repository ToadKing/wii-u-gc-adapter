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

To properly run wii-u-gc-adapter when the adapter is plugged in, the following files have to be placed in certain places.

The file `88-wii-u-gamecube-adapter.rules` should be placed in `/etc/udev/rulles.d/`.  This file starts the script, `run-wii-u-setup.sh`, when the adapter is connected.  This script, in turn, runs the script `start-wii-u-adapter.sh`, and starts `wii-u-gc-adapter` in the background.  Both of these scripts should be made executable (`chmod +x run-wii-u-setup.sh start-wii-u-adapter.sh`) and placed in `/usr/local/bin/`.  With these files in place, along with the above xorg.conf rule, the Wii U Gamecube adapter should work.
