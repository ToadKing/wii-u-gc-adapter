#!/usr/bin/env python

# Original Author: Michael Lelli <toadking@toadking.com>

import usb.core
import usb.util
import uinput
import sys
import getopt

controllers = [None, None, None, None]
controllers_state = [None, None, None, None]

DIGITAL_BUTTONS = {
  uinput.BTN_DPAD_UP:    0x8000,
  uinput.BTN_DPAD_DOWN:  0x4000,
  uinput.BTN_DPAD_LEFT:  0x1000,
  uinput.BTN_DPAD_RIGHT: 0x2000,
  uinput.BTN_NORTH:      0x0800,
  uinput.BTN_SOUTH:      0x0100,
  uinput.BTN_EAST:       0x0400,
  uinput.BTN_WEST:       0x0200,
  uinput.BTN_START:      0x0001,
  uinput.BTN_TL:         0x0008,
  uinput.BTN_TR:         0x0004,
  uinput.BTN_TR2:        0x0002
}

AXIS_BYTES = {
    uinput.ABS_X:  3,
    uinput.ABS_Y:  4,
    uinput.ABS_RX: 5,
    uinput.ABS_RY: 6,
    uinput.ABS_Z:  7,
    uinput.ABS_RZ: 8
}

def create_device(index, raw):
  if raw:
    axis_cal = (0, 255, 0, 0)
    cstick_cal = (0, 255, 0, 0)
    trigger_cal = (-255, 255, 0, 0)
  else:
    axis_cal = (20, 235, 0, 0)
    cstick_cal = (30, 225, 0, 0)
    trigger_cal = (-200, 225, 0, 0)
  events = (
    uinput.BTN_NORTH,
    uinput.BTN_SOUTH,
    uinput.BTN_EAST,
    uinput.BTN_WEST,
    uinput.BTN_START,
    uinput.BTN_DPAD_UP,
    uinput.BTN_DPAD_DOWN,
    uinput.BTN_DPAD_LEFT,
    uinput.BTN_DPAD_RIGHT,
    uinput.ABS_X + axis_cal,
    uinput.ABS_Y + axis_cal,
    uinput.ABS_RX + cstick_cal,
    uinput.ABS_RY + cstick_cal,
    uinput.BTN_TL,
    uinput.ABS_Z + trigger_cal,
    uinput.BTN_TR,
    uinput.ABS_RZ + trigger_cal,
    uinput.BTN_TR2
  )
  controllers[index] = uinput.Device(events, name="Wii U GameCube Adapter Port {}".format(index+1))
  controllers_state[index] = (
    0,
    {
      uinput.ABS_X:  -1,
      uinput.ABS_Y:  -1,
      uinput.ABS_RX: -1,
      uinput.ABS_RY: -1,
      uinput.ABS_Z:  -1,
      uinput.ABS_RZ: -1
    }
  )

STATE_NORMAL   = 0x10
STATE_WAVEBIRD = 0x20

def is_connected(state):
  return state & (STATE_NORMAL | STATE_WAVEBIRD) != 0

def help():
  print "usage: " + sys.argv[0] + " [-h/--help] [-r/--raw]"
  print
  print "    -h/--help: display this message"
  print "    -r/--raw:  do not do scaling on axis"
  sys.exit(-1)

def main():
  dev = usb.core.find(idVendor=0x057e, idProduct=0x0337)
  raw_mode = False

  try:
    opts, args = getopt.getopt(sys.argv[1:], "hr", ["help", "raw"])
  except getopt.GetoptError:
    help()

  for opt, arg in opts:
    if opt in ("-h", "--help"):
      help()
    elif opt in ("-r", "--raw"):
      print "raw mode"
      raw_mode = True

  if dev is None:
    raise ValueError('GC adapter not found')

  if dev.is_kernel_driver_active(0):
    reattach = True
    dev.detach_kernel_driver(0)

  dev.set_configuration()
  cfg = dev.get_active_configuration()
  intf = cfg[(0,0)]

  out_ep = usb.util.find_descriptor(
    intf,
    custom_match = \
    lambda e: \
      usb.util.endpoint_direction(e.bEndpointAddress) == \
      usb.util.ENDPOINT_OUT)

  in_ep = usb.util.find_descriptor(
    intf,
    custom_match = \
    lambda e: \
      usb.util.endpoint_direction(e.bEndpointAddress) == \
      usb.util.ENDPOINT_IN)

  # might not be necessary, but doesn't hurt
  dev.ctrl_transfer(0x21, 11, 0x0001, 0, [])

  out_ep.write([0x13])

  try:
    while 1:
      try:
        data = in_ep.read(37)
      except (KeyboardInterrupt, SystemExit):
        raise
      except:
        print "read error"
        continue
      if data[0] != 0x21:
        print "unknown message {:02x}}".format(data[0])
        continue

      payloads = [data[1:10], data[10:19], data[19:28], data[28:37]]

      index = 0

      for i, d in enumerate(payloads):
        status = d[0]
        # check for connected
        if is_connected(status) and controllers[i] is None:
          create_device(i, raw_mode)
        elif not is_connected(status):
          controllers[i] = None

        if controllers[i] is None:
          continue

        # if status & 0x04 != 0:
        #   do something about having both USB plugs connected

        btns = d[1] << 8 | d[2]
        newmask = 0
        for btn, mask in DIGITAL_BUTTONS.iteritems():
          pressed = btns & mask
          newmask |= pressed

          # state change
          if controllers_state[i][0] & mask != pressed:
            controllers[i].emit(btn, 1 if pressed != 0 else 0, syn=False)

        newaxis = {}
        for axis, offset in AXIS_BYTES.iteritems():
          value = d[offset]
          newaxis[axis] = value
          if axis == uinput.ABS_Y or axis == uinput.ABS_RY:
            # flip from 0 - 255 to 255 - 0
            value ^= 0xFF
          #elif axis == uinput.ABS_RZ or axis == uinput.ABS_Z:
            # scale from 0 - 255 to 127 - 255
            #value = (value >> 1) + 127

          if controllers_state[i][1][axis] != value:
            controllers[i].emit(axis, value, syn=False)

        controllers[i].syn()
        controllers_state[i] = (
          newmask,
          newaxis
        )

  except:
    raise

if __name__ == "__main__":
  main()
