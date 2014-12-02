// See LICENSE for license

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <libudev.h>
#include <libusb.h>
#include <pthread.h>

#if (!defined(LIBUSBX_API_VERSION) || LIBUSBX_API_VERSION < 0x01000102) && (!defined(LIBUSB_API_VERSION) || LIBUSB_API_VERSION < 0x01000102)
#error libusb(x) 1.0.16 or higher is required
#endif

#define EP_IN  0x81
#define EP_OUT 0x02

#define STATE_NORMAL   0x10
#define STATE_WAVEBIRD 0x20

const int BUTTON_OFFSET_VALUES[16] = {
   BTN_START,
   BTN_TR2,
   BTN_TR,
   BTN_TL,
   -1,
   -1,
   -1,
   -1,
   BTN_SOUTH,
   BTN_WEST,
   BTN_EAST,
   BTN_NORTH,
   BTN_DPAD_LEFT,
   BTN_DPAD_RIGHT,
   BTN_DPAD_DOWN,
   BTN_DPAD_UP,
};

const int AXIS_OFFSET_VALUES[6] = {
   ABS_X,
   ABS_Y,
   ABS_RX,
   ABS_RY,
   ABS_Z,
   ABS_RZ
};

struct ports
{
   bool connected;
   bool extra_power;
   bool rumbling;
   int uinput;
   unsigned char type;
   uint16_t buttons;
   uint8_t axis[6];
};

struct adapter
{
   struct libusb_device *device;
   struct libusb_device_handle *handle;
   pthread_t thread;
   unsigned char rumble[5];
   struct ports controllers[4];
};

static bool raw_mode;

static volatile int quitting;

static pthread_mutex_t adapter_mutex;

static struct adapter *adapters;
static volatile int adapters_size;

static const char *uinput_path;

static unsigned char connected_type(unsigned char status)
{
   unsigned char type = status & (STATE_NORMAL | STATE_WAVEBIRD);
   switch (type)
   {
      case STATE_NORMAL:
      case STATE_WAVEBIRD:
         return type;
      default:
         return 0;
   }
}

static bool uinput_create(int i, struct ports *port, unsigned char type)
{
   fprintf(stderr, "connecting on port %d\n", i);
   struct uinput_user_dev uinput_dev;
   memset(&uinput_dev, 0, sizeof(uinput_dev));
   port->uinput = open(uinput_path, O_RDWR | O_NONBLOCK);

   // buttons
   ioctl(port->uinput, UI_SET_EVBIT, EV_KEY);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_NORTH);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_SOUTH);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_EAST);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_WEST);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_START);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_DPAD_UP);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_DPAD_DOWN);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_DPAD_LEFT);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_DPAD_RIGHT);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_TL);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_TR);
   ioctl(port->uinput, UI_SET_KEYBIT, BTN_TR2);

   // axis
   ioctl(port->uinput, UI_SET_EVBIT, EV_ABS);
   ioctl(port->uinput, UI_SET_ABSBIT, ABS_X);
   ioctl(port->uinput, UI_SET_ABSBIT, ABS_Y);
   ioctl(port->uinput, UI_SET_ABSBIT, ABS_RX);
   ioctl(port->uinput, UI_SET_ABSBIT, ABS_RY);
   ioctl(port->uinput, UI_SET_ABSBIT, ABS_Z);
   ioctl(port->uinput, UI_SET_ABSBIT, ABS_RZ);

   if (raw_mode)
   {
      uinput_dev.absmin[ABS_X]  = 0;  uinput_dev.absmax[ABS_X]  = 255;
      uinput_dev.absmin[ABS_Y]  = 0;  uinput_dev.absmax[ABS_Y]  = 255;
      uinput_dev.absmin[ABS_RX] = 0;  uinput_dev.absmax[ABS_RX] = 255;
      uinput_dev.absmin[ABS_RY] = 0;  uinput_dev.absmax[ABS_RY] = 255;
      uinput_dev.absmin[ABS_Z]  = 0;  uinput_dev.absmax[ABS_Z]  = 255;
      uinput_dev.absmin[ABS_RZ] = 0;  uinput_dev.absmax[ABS_RZ] = 255;
   }
   else
   {
      uinput_dev.absmin[ABS_X]  = 20; uinput_dev.absmax[ABS_X]  = 235;
      uinput_dev.absmin[ABS_Y]  = 20; uinput_dev.absmax[ABS_Y]  = 235;
      uinput_dev.absmin[ABS_RX] = 30; uinput_dev.absmax[ABS_RX] = 225;
      uinput_dev.absmin[ABS_RY] = 30; uinput_dev.absmax[ABS_RY] = 225;
      uinput_dev.absmin[ABS_Z]  = 25; uinput_dev.absmax[ABS_Z]  = 225;
      uinput_dev.absmin[ABS_RZ] = 25; uinput_dev.absmax[ABS_RZ] = 225;
   }

   // rumble
   ioctl(port->uinput, UI_SET_EVBIT, EV_FF);
   //ioctl(port->uinput, UI_SET_FFBIT, FF_PERIODIC);
   ioctl(port->uinput, UI_SET_FFBIT, FF_RUMBLE);
   uinput_dev.ff_effects_max = 1;

   snprintf(uinput_dev.name, sizeof(uinput_dev.name), "Wii U GameCube Adapter Port %d", i+1);
   uinput_dev.name[sizeof(uinput_dev.name)-1] = 0;
   uinput_dev.id.bustype = BUS_USB;
   write(port->uinput, &uinput_dev, sizeof(uinput_dev));
   if (ioctl(port->uinput, UI_DEV_CREATE) != 0)
   {
      fprintf(stderr, "error creating uinput device");
      return false;
   }
   port->type = type;
   port->connected = true;
   return true;
}

void uinput_destroy(int i, struct ports *port)
{
   fprintf(stderr, "disconnecting on port %d\n", i);
   ioctl(port->uinput, UI_DEV_DESTROY);
   close(port->uinput);
   port->connected = false;
}

// NOTE: only call the following functions when the adapter mutex is owned, they are not thread safe otherwise
static void realloc_adapters(void)
{
   if (adapters_size > 0)
   {
      adapters = realloc(adapters, sizeof(struct adapter) * adapters_size);

      if (adapters == NULL)
      {
         fprintf(stderr, "FATAL: realloc() failed");
         exit(-1);
      }
   }
}

static int find_adapter(struct libusb_device *device, int guess)
{
   if (guess >= 0 && guess < adapters_size && adapters[guess].device == device)
      return guess;

   for (int i = 0; i < adapters_size; i++)
   {
      if (adapters[i].device == device)
         return i;
   }

   return -1;
}

static void handle_payload(int i, struct ports *port, unsigned char *payload)
{
   unsigned char status = payload[0];
   unsigned char type = connected_type(status);

   if (type != 0 && !port->connected)
   {
      uinput_create(i, port, type);
   }
   else if (type == 0 && port->connected)
   {
      uinput_destroy(i, port);
   }

   if (!port->connected)
      return;

   port->extra_power = ((status & 0x04) != 0);

   if (type != port->type)
   {
      fprintf(stderr, "controller on port %d changed controller type???", i+1);
      port->type = type;
   }

   struct input_event events[12+6+1]; // buttons + axis + syn event
   memset(&events, 0, sizeof(events));
   int e_count = 0;

   uint16_t btns = (uint16_t) payload[1] << 8 | (uint16_t) payload[2];

   for (int j = 0; j < 16; j++)
   {
      if (BUTTON_OFFSET_VALUES[j] == -1)
         continue;

      uint16_t mask = (1 << j);
      uint16_t pressed = btns & mask;

      if ((port->buttons & mask) != pressed)
      {
         events[e_count].type = EV_KEY;
         events[e_count].code = BUTTON_OFFSET_VALUES[j];
         events[e_count].value = (pressed == 0) ? 0 : 1;
         e_count++;
         port->buttons &= ~mask;
         port->buttons |= pressed;
      }
   }

   for (int j = 0; j < 6; j++)
   {
      unsigned char value = payload[j+3];

      if (AXIS_OFFSET_VALUES[j] == ABS_Y || AXIS_OFFSET_VALUES[j] == ABS_RY)
         value ^= 0xFF; // flip from 0 - 255 to 255 - 0

      if (port->axis[j] != value)
      {
         events[e_count].type = EV_ABS;
         events[e_count].code = AXIS_OFFSET_VALUES[j];
         events[e_count].value = value;
         e_count++;
         port->axis[j] = value;
      }
   }

   if (e_count > 0)
   {
      events[e_count].type = EV_SYN;
      events[e_count].code = SYN_REPORT;
      e_count++;
      write(port->uinput, events, sizeof(events[0]) * e_count);
   }

   // check for rumble events
   struct input_event e;
   ssize_t ret = read(port->uinput, &e, sizeof(e));
   if (ret == sizeof(e) && e.type == EV_UINPUT)
   {
      switch (e.code)
      {
         case UI_FF_UPLOAD:
         {
            printf("rumble start\n");
            struct uinput_ff_upload upload = { 0 };
            upload.request_id = e.value;
            ioctl(port->uinput, UI_BEGIN_FF_UPLOAD, &upload);
            port->rumbling = true;
            upload.retval = 0;
            ioctl(port->uinput, UI_END_FF_UPLOAD, &upload);
            break;
         }
         case UI_FF_ERASE:
         {
            printf("rumble erase\n");
            struct uinput_ff_erase erase = { 0 };
            erase.request_id = e.value;
            ioctl(port->uinput, UI_BEGIN_FF_ERASE, &erase);
            port->rumbling = false;
            erase.retval = 0;
            ioctl(port->uinput, UI_END_FF_ERASE, &erase);
         }
      }
   }
}

static void *adapter_thread(void *data)
{
   struct libusb_device *device = (struct libusb_device *)data;
   int i = -1;

   while (!quitting)
   {
      pthread_mutex_lock(&adapter_mutex);
      i = find_adapter(device, i);
      // we were removed, abort
      if (i < 0)
      {
         pthread_mutex_unlock(&adapter_mutex);
         break;
      }

      struct adapter a = adapters[i];

      pthread_mutex_unlock(&adapter_mutex);
      unsigned char payload[37];
      int size = 0;
      libusb_interrupt_transfer(a.handle, EP_IN, payload, sizeof(payload), &size, 0);
      if (size != 37 || payload[0] != 0x21)
         continue;
      
      data = &payload[1];

      unsigned char rumble[5] = { 0x11 };
      for (int i = 0; i < 4; i++, data += 9)
      {
         handle_payload(i, &a.controllers[i], data);
         rumble[i+1] = (a.controllers[i].rumbling && a.controllers[i].extra_power && a.controllers[i].type == STATE_NORMAL);
      }

      if (memcmp(rumble, a.rumble, sizeof(rumble)) != 0)
      {
         memcpy(a.rumble, rumble, sizeof(rumble));
         libusb_interrupt_transfer(a.handle, EP_OUT, a.rumble, sizeof(a.rumble), &size, 0);
      }

      pthread_mutex_lock(&adapter_mutex);
      i = find_adapter(device, i);
      // we were removed, abort
      if (i < 0)
      {
         pthread_mutex_unlock(&adapter_mutex);
         break;
      }
      adapters[i] = a;
      pthread_mutex_unlock(&adapter_mutex);
   }

   return NULL;
}

static int hotplug_callback(struct libusb_context *ctx, struct libusb_device *dev, libusb_hotplug_event event, void *user_data)
{
   (void)ctx;
   (void)user_data;
   if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED)
   {
      struct adapter a = { 0 };
      a.device = dev;

      if (libusb_open(a.device, &a.handle) != 0)
      {
         fprintf(stderr, "Error opening device 0x%p\n", a.device);
         return 0;
      }

      if (libusb_kernel_driver_active(a.handle, 0) == 1 && libusb_detach_kernel_driver(a.handle, 0))
      {
         fprintf(stderr, "Error detaching handle 0x%p from kernel\n", a.handle);
         return 0;
      }

      int tmp;
      unsigned char payload[1] = { 0x13 };
      libusb_interrupt_transfer(a.handle, EP_OUT, payload, sizeof(payload), &tmp, 0);
      pthread_mutex_lock(&adapter_mutex);
      adapters_size++;
      realloc_adapters();
      adapters[adapters_size-1] = a;
      pthread_mutex_unlock(&adapter_mutex);

      pthread_create(&a.thread, NULL, adapter_thread, a.device);

      fprintf(stderr, "adapter 0x%p connected\n", a.device);
   }
   else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT)
   {
      pthread_mutex_lock(&adapter_mutex);
      int i = find_adapter(dev, -1);
      struct adapter a;
      if (i >= 0)
      {
         a = adapters[i];
         memmove(&adapters[i], &adapters[i+1], (adapters_size-i-1)*sizeof(struct adapter));
         adapters_size--;
         realloc_adapters();
      }
      pthread_mutex_unlock(&adapter_mutex);
      if (i >= 0)
      {
         fprintf(stderr, "adapter 0x%p disconnected\n", a.device);
         for (int i = 0; i < 4; i++)
         {
            if (a.controllers[i].connected)
               uinput_destroy(i, &a.controllers[i]);
         }
         libusb_close(a.handle);
      }
   }

   return 0;
}

int main(void)
{
   struct udev *udev;
   struct udev_device *uinput;

   if (pthread_mutex_init(&adapter_mutex, NULL) < 0)
   {
      fprintf(stderr, "mutex/cond init errors\n");
      return -1;
   }

   udev = udev_new();
   if (udev == NULL) {
      fprintf(stderr, "udev init errors\n");
      return -1;
   }

   uinput = udev_device_new_from_subsystem_sysname(udev, "misc", "uinput");
   if (uinput == NULL)
   {
      fprintf(stderr, "uinput creation failed\n");
      return -1;
   }

   uinput_path = udev_device_get_devnode(uinput);
   if (uinput_path == NULL)
   {
      fprintf(stderr, "cannot find path to uinput\n");
      return -1;
   }

   libusb_init(NULL);

   struct libusb_device **devices;

   int count = libusb_get_device_list(NULL, &devices);

   for (int i = 0; i < count; i++)
   {
      struct libusb_device_descriptor desc;
      libusb_get_device_descriptor(devices[i], &desc);
      if (desc.idVendor == 0x057e && desc.idProduct == 0x0337)
         hotplug_callback(NULL, devices[i], LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, NULL);
   }

   if (count > 0)
      libusb_free_device_list(devices, 1);

   libusb_hotplug_callback_handle callback;
   int hotplug_ret = libusb_hotplug_register_callback(NULL, LIBUSB_HOTPLUG_MATCH_ANY, 0, 0x057e, 0x0337, LIBUSB_HOTPLUG_MATCH_ANY, hotplug_callback, NULL, &callback);

   if (hotplug_ret != 0)
      fprintf(stderr, "cannot register hotplug callback, hotplugging not enabled\n");

   // pump events until shutdown & all helper threads finish cleaning up
   while (!quitting || adapters_size > 0)
      libusb_handle_events_completed(NULL, (int *)&quitting);

   if (hotplug_ret == 0)
      libusb_hotplug_deregister_callback(NULL, callback);

   libusb_exit(NULL);
   udev_device_unref(uinput);
   udev_unref(udev);
   return 0;
}
