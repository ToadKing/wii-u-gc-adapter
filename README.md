wii-u-gc-adapter
================

Tool for using the Wii U GameCube Adapter on Linux

Prerequisites
-------------
* Python 2
* PyUSB
* Python-uinput

Usage
-----
Simply run the program. You'll probably have to run it as root in order to
grab the USB device from the kernel and use the uinput interface. To stop the
program just kill it in any way you want.

Seperate virtual controllers are created for each one plugged into the adapter
and hotplugging is supported.

Quirks
------
* It's new, so there might be bugs! Please report them!
* The uinput kernel module is required. If it's not autoloaded, you should do
  so with `modprobe uinput`
* If all your controllers start messing with the mouse cursor, you can fix
  them with this xorg.conf rule. (You can place it in a file in xorg.conf.d)

````
Section "InputClass"
        Identifier "Wii U GameCube Adapter Blacklist"
        MatchProduct "Wii U GameCube Adapter Port "
        Option "Ignore" "on"
EndSection
````
