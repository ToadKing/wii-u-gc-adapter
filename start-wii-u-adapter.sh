#!/bin/bash
# This is just in case the user plugs in the Wii U Gamecube Adapter with wii-u-gc-adapter already running.
if [ ! "$(pidof wii-u-gc-adapter)" ]; then
   /usr/local/bin/wii-u-gc-adapter &
fi
