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

Installation
____________
It isn't required, but in order to make it easier for udev rules use `sudo make install`

Usage
-----
Simply run the program.

Seperate virtual controllers are created for each one plugged into the adapter
and hotplugging (both controllers and adapters) is supported.

Quirks
------
* In order to run the app without root rigths add following udev rule:
```bash
cat /etc/udev/rules.d/51-gcadapter.rules 
SUBSYSTEM=="usb", ATTRS{idVendor}=="057e", ATTRS{idProduct}=="0337", MODE="0666"
```
* To run on startup create a systemd service:
```bash
cat /etc/systemd/system/wii-u-gc-adapter.service
[Unit]
Description=Wii U GameCube adapter
StartLimitIntervalSec=0

[Service]
Type=forking
ExecStart=bash -c 'wii-u-gc-adapter -R & disown'

[Install]
WantedBy=multi-user.target
```
and then enable in on the startup
```bash
systemctl enable wii-u-gc-adapter
```
* It's new, so there might be bugs! Please report them!
* The uinput kernel module is required. If it's not autoloaded, you should do
  so with `modprobe uinput`
* Input ranges on the sticks/analog triggers are scaled to try to match the
  physical ranges of the controls. To remove this scaling run the program with
  the `--raw` flag.
* If needed to make sticks work in the range from -128 to 128 use the `--relative` flag. In can be handy if a contoller used in rpcs3 emulator
* If all your controllers start messing with the mouse cursor, you can fix
  them with this xorg.conf rule. (You can place it in a file in xorg.conf.d)

````
Section "InputClass"
        Identifier "Wii U GameCube Adapter Blacklist"
        MatchProduct "Wii U GameCube Adapter Port "
        Option "Ignore" "on"
EndSection
````
