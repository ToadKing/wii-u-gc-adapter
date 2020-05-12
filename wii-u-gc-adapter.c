// See LICENSE for license

#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libudev.h>
#include <libusb.h>
#include <pthread.h>

#if (!defined(LIBUSBX_API_VERSION) || LIBUSBX_API_VERSION < 0x01000102) && (!defined(LIBUSB_API_VERSION) || LIBUSB_API_VERSION < 0x01000102)
#error libusb(x) 1.0.16 or higher is required
#endif

//Adapter's USB Info
const int VENDOR_ID  = 0x057e;
const int PRODUCT_ID = 0x0337;

//USB endpoints. Find with, e.g., `lsusb -v -d 057e:0337`
const unsigned char EP_IN  = 0x81;
const unsigned char EP_OUT = 0x02;
const int COMM_OUT_PAYLOAD_SIZE = 37;

//USB configuration
const uint32_t COMM_TIMEOUT = 100; //milliseconds

#define STATE_NORMAL   0x10
#define STATE_WAVEBIRD 0x20

#define MAX_FF_EVENTS 4

//Communication codes
const unsigned char COMM_ENABLE_ADAPTER = 0x13;

enum COMM_RECEIVE_TYPES {
   RX_FATAL,
   RX_SKIP,
   RX_GOOD
};

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

struct ff_event
{
   bool in_use;
   bool forever;
   int duration;
   int delay;
   int repetitions;
   struct timespec start_time;
   struct timespec end_time;
};

struct ports
{
   bool connected;
   bool extra_power;
   int uinput;
   unsigned char type;
   uint16_t buttons;
   uint8_t axis[6];
   struct ff_event ff_events[MAX_FF_EVENTS];
};

struct adapter
{
   volatile bool quitting;
   struct libusb_device *device;
   struct libusb_device_handle *handle;
   pthread_t thread;
   unsigned char rumble[5];
   struct ports controllers[4];
   struct adapter *next;
};

static bool raw_mode;

static volatile int quitting;

static struct adapter adapters;

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
   ioctl(port->uinput, UI_SET_FFBIT, FF_PERIODIC);
   ioctl(port->uinput, UI_SET_FFBIT, FF_SQUARE);
   ioctl(port->uinput, UI_SET_FFBIT, FF_TRIANGLE);
   ioctl(port->uinput, UI_SET_FFBIT, FF_SINE);
   ioctl(port->uinput, UI_SET_FFBIT, FF_RUMBLE);
   uinput_dev.ff_effects_max = MAX_FF_EVENTS;

   snprintf(uinput_dev.name, sizeof(uinput_dev.name), "Wii U GameCube Adapter Port %d", i+1);
   uinput_dev.name[sizeof(uinput_dev.name)-1] = 0;
   uinput_dev.id.bustype = BUS_USB;
   if (write(port->uinput, &uinput_dev, sizeof(uinput_dev)) != sizeof(uinput_dev))
   {
      fprintf(stderr, "error writing uinput device settings");
      close(port->uinput);
      return false;
   }

   if (ioctl(port->uinput, UI_DEV_CREATE) != 0)
   {
      fprintf(stderr, "error creating uinput device");
      close(port->uinput);
      return false;
   }
   port->type = type;
   port->connected = true;
   return true;
}

static void uinput_destroy(int i, struct ports *port)
{
   fprintf(stderr, "disconnecting on port %d\n", i);
   ioctl(port->uinput, UI_DEV_DESTROY);
   close(port->uinput);
   port->connected = false;
}

static struct timespec ts_add(struct timespec *start, int milliseconds)
{
   struct timespec ret = *start;
   int s = milliseconds / 1000;
   int ns = (milliseconds % 1000) * 1000000;
   ret.tv_sec += s ;
   ret.tv_nsec += ns ;
   if (ret.tv_nsec >= 1000000000L)
   {
      ret.tv_sec++;
      ret.tv_nsec -= 1000000000L;
   }
   return ret;
}

static bool ts_greaterthan(struct timespec *first, struct timespec *second)
{
   return (first->tv_sec >= second->tv_sec || (first->tv_sec == second->tv_sec && first->tv_nsec >= second->tv_nsec));
}

static bool ts_lessthan(struct timespec *first, struct timespec *second)
{
   return (first->tv_sec <= second->tv_sec || (first->tv_sec == second->tv_sec && first->tv_nsec <= second->tv_nsec));
}

static void update_ff_start_stop(struct ff_event *e, struct timespec *current_time)
{
   e->repetitions--;

   if (e->repetitions < 0)
   {
      e->repetitions = 0;
      e->start_time.tv_sec = 0;
      e->start_time.tv_nsec = 0;
      e->end_time.tv_sec = 0;
      e->end_time.tv_nsec = 0;
   }
   else
   {
      e->start_time = ts_add(current_time, e->delay);
      if (e->forever)
      {
         e->end_time.tv_sec = INT_MAX;
         e->end_time.tv_nsec = 999999999L;
      }
      else
      {
         e->end_time = ts_add(&e->start_time, e->duration);
      }
   }
}

static int create_ff_event(struct ports *port, struct uinput_ff_upload *upload)
{
   if (upload->old.type != 0)
   {
      // events with length 0 last forever
      port->ff_events[upload->old.id].forever = (upload->effect.replay.length == 0);
      port->ff_events[upload->old.id].duration = upload->effect.replay.length;
      port->ff_events[upload->old.id].delay = upload->effect.replay.delay;
      port->ff_events[upload->old.id].repetitions = 0;
      return upload->old.id;
   }
   for (int i = 0; i < MAX_FF_EVENTS; i++)
   {
      if (!port->ff_events[i].in_use)
      {
         port->ff_events[i].in_use = true;
         port->ff_events[i].forever = (upload->effect.replay.length == 0);
         port->ff_events[i].duration = upload->effect.replay.length;
         port->ff_events[i].delay = upload->effect.replay.delay;
         port->ff_events[i].repetitions = 0;
         return i;
      }
   }

   return -1;
}

static void handle_payload(int i, struct ports *port, unsigned char *payload, struct timespec *current_time)
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

   struct input_event events[12+6+1] = {0}; // buttons + axis + syn event
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
      size_t to_write = sizeof(events[0]) * e_count;
      size_t written = 0;
      while (written < to_write)
      {
         ssize_t write_ret = write(port->uinput, (const char*)events + written, to_write - written);
         if (write_ret < 0)
         {
            char msg[128];
            strerror_r(errno, msg, sizeof(msg));
            fprintf(stderr, "Warning: writing input events failed: %s\n", msg);
            break;
         }
         written += write_ret;
      }
   }

   // check for rumble events
   struct input_event e;
   ssize_t ret = read(port->uinput, &e, sizeof(e));
   if (ret == sizeof(e))
   {
      if (e.type == EV_UINPUT)
      {
         switch (e.code)
         {
            case UI_FF_UPLOAD:
            {
               struct uinput_ff_upload upload = { 0 };
               upload.request_id = e.value;
               ioctl(port->uinput, UI_BEGIN_FF_UPLOAD, &upload);
               int id = create_ff_event(port, &upload);
               if (id < 0)
               {
                  // TODO: what's the proper error code for this?
                  upload.retval = -1;
               }
               else
               {
                  upload.retval = 0;
                  upload.effect.id = id;
               }
               ioctl(port->uinput, UI_END_FF_UPLOAD, &upload);
               break;
            }
            case UI_FF_ERASE:
            {
               struct uinput_ff_erase erase = { 0 };
               erase.request_id = e.value;
               ioctl(port->uinput, UI_BEGIN_FF_ERASE, &erase);
               if (erase.effect_id < MAX_FF_EVENTS)
                  port->ff_events[erase.effect_id].in_use = false;
               ioctl(port->uinput, UI_END_FF_ERASE, &erase);
            }
         }
      }
      else if (e.type == EV_FF)
      {
         if (e.code < MAX_FF_EVENTS && port->ff_events[e.code].in_use)
         {
            port->ff_events[e.code].repetitions = e.value;
            update_ff_start_stop(&port->ff_events[e.code], current_time);
         }
      }
   }
}



bool send_to_adapter(const struct adapter *a, unsigned char payload[5]){
   int bytes_transferred;
   const int transfer_ret = libusb_interrupt_transfer(a->handle, EP_OUT, payload, 5, &bytes_transferred, 0);

   if (transfer_ret != LIBUSB_SUCCESS ) {
      fprintf(stderr, "libusb_interrupt_transfer (%d): %s\n", __LINE__, libusb_error_name(transfer_ret));
      return false;
   }
   if (bytes_transferred != 5) {
      fprintf(stderr, "libusb_interrupt_transfer (%d) %d/%d bytes transferred.\n", __LINE__, bytes_transferred, 5);
      return false;
   }

   return true;
}



int receive_from_adapter(struct adapter *a, unsigned char payload[COMM_OUT_PAYLOAD_SIZE]){
   int bytes_received = 0;
   
   int transfer_ret = libusb_interrupt_transfer(
      a->handle, EP_IN, payload, COMM_OUT_PAYLOAD_SIZE, &bytes_received, COMM_TIMEOUT
   );
   
   if (transfer_ret == LIBUSB_ERROR_TIMEOUT ) {
      fprintf(stderr, "libusb_interrupt_transfer error (%d): LIBUSB_ERROR_TIMEOUT\n", __LINE__);         
      return RX_FATAL;
   }
   
   if (transfer_ret != LIBUSB_SUCCESS ) {
      fprintf(stderr, "libusb_interrupt_transfer error (%d) %d\n", __LINE__, transfer_ret);
      a->quitting = true;
      return RX_FATAL;
   }
   
   if (bytes_received != COMM_OUT_PAYLOAD_SIZE || payload[0] != 0x21)
      return RX_SKIP;

   return RX_GOOD;
}



static void *adapter_thread(void *data)
{
   struct adapter *a = (struct adapter *)data;

   //Enable the adapter
   {
      unsigned char payload[5] = {COMM_ENABLE_ADAPTER};
      if(!send_to_adapter(a, payload)){
         return NULL;
      }
   }

   while (!a->quitting)
   {
      unsigned char payload[37];
      const int receive_status = receive_from_adapter(a, payload);
      if(receive_status==RX_FATAL)
         break;
      else if(receive_status==RX_SKIP)
         continue;
      else if(receive_status==RX_GOOD){
         //Keep going
      }

      unsigned char *controller = &payload[1];

      unsigned char rumble[5] = { 0x11, 0, 0, 0, 0 };
      struct timespec current_time = { 0 };
      clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
      for (int i = 0; i < 4; i++, controller += 9)
      {
         handle_payload(i, &a->controllers[i], controller, &current_time);
         rumble[i+1] = 0;
         if (a->controllers[i].extra_power && a->controllers[i].type == STATE_NORMAL)
         {
            for (int j = 0; j < MAX_FF_EVENTS; j++)
            {
               struct ff_event *e = &a->controllers[i].ff_events[j];
               if (e->in_use)
               {
                  bool after_start = ts_lessthan(&e->start_time, &current_time);
                  bool before_end = ts_greaterthan(&e->end_time, &current_time);

                  if (after_start && before_end)
                     rumble[i+1] = 1;
                  else if (after_start && !before_end)
                     update_ff_start_stop(e, &current_time);
               }
            }
         }
      }

      if (memcmp(rumble, a->rumble, sizeof(rumble)) != 0)
      {
         memcpy(a->rumble, rumble, sizeof(rumble));
         int bytes_received;
         int transfer_ret = libusb_interrupt_transfer(a->handle, EP_OUT, a->rumble, sizeof(a->rumble), &bytes_received, 0);
         if (transfer_ret != LIBUSB_SUCCESS ) {
            fprintf(stderr, "libusb_interrupt_transfer error %d\n", transfer_ret);
            a->quitting = true;
            break;
         }
      }
   }

   for (int i = 0; i < 4; i++)
   {
      if (a->controllers[i].connected)
         uinput_destroy(i, &a->controllers[i]);
   }

   return NULL;
}

static void add_adapter(struct libusb_device *const dev)
{
   fprintf(stderr, "Found an adapter! Attempting to connect...\n");

   struct adapter *const a = calloc(1, sizeof(struct adapter));
   if (a == NULL)
   {
      fprintf(stderr, "FATAL (%d): Failed to allocate memory for an adapter!", __LINE__);
      exit(-1);
   }
   a->device = dev;

   if (libusb_open(a->device, &a->handle) != LIBUSB_SUCCESS)
   {
      fprintf(stderr, "Error (%d): Problem opening device 0x%p\n", __LINE__, a->device);
      return;
   }

   const int is_kernel_driver_active = libusb_kernel_driver_active(a->handle, 0);
   if (is_kernel_driver_active == 1) {
       fprintf(stderr, "Detaching kernel driver\n");
       if (libusb_detach_kernel_driver(a->handle, 0)) {
           fprintf(stderr, "Error (%d): Problem detaching handle 0x%p from kernel\n", __LINE__, a->handle);
           return;
       }
   } else if (is_kernel_driver_active==0){
      fprintf(stderr, "No kernel driver was active for this adapter - good.\n");      
   } else {
      fprintf(stderr, "Could not determine if a kernel driver was already active for the adapter! (Error: %d)\n", is_kernel_driver_active);
      return;
   }

   //Add adapter to linked list of adapters
   struct adapter *const old_head = adapters.next;
   adapters.next = a;
   a->next = old_head;

   //Pretty print some info about the adapter we've just got
   struct libusb_device_descriptor desc;
   libusb_get_device_descriptor(a->device, &desc);

   unsigned char manufacturer_name[200] = {0};
   unsigned char product_name[200] = {0};
   int retcode;
   if((retcode=libusb_get_string_descriptor_ascii(a->handle, desc.iManufacturer, manufacturer_name, sizeof(manufacturer_name)))<0){
      fprintf(stderr, "Failed to get manufacturer string: %s!\n", libusb_strerror(retcode));
   }
   if((retcode=libusb_get_string_descriptor_ascii(a->handle, desc.idProduct, product_name, sizeof(product_name)))<0){
      fprintf(stderr, "Failed to get product string: %s!\n", libusb_strerror(retcode));      
   }

   fprintf(stderr, "adapter 0x%p connected! Vendor: %04x, Product: %04x (%s), Manufacturer: %x (%s) \n", 
      a->device, 
      desc.idVendor,
      desc.idProduct,
      product_name,
      desc.iManufacturer,
      manufacturer_name);

   //Start a thread to manage the adapter
   pthread_create(&a->thread, NULL, adapter_thread, a);
}

static void remove_adapter(struct libusb_device *dev)
{
   struct adapter *a = &adapters;
   while (a->next != NULL)
   {
      if (a->next->device == dev)
      {
         a->next->quitting = true;
         pthread_join(a->next->thread, NULL);
         fprintf(stderr, "adapter 0x%p disconnected\n", a->next->device);
         libusb_close(a->next->handle);
         struct adapter *new_next = a->next->next;
         free(a->next);
         a->next = new_next;
         return;
      }

      a = a->next;
   }
}

static int LIBUSB_CALL hotplug_callback(struct libusb_context *ctx, struct libusb_device *dev, libusb_hotplug_event event, void *user_data)
{
   (void)ctx;
   (void)user_data;
   if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED)
   {
      add_adapter(dev);
   }
   else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT)
   {
      remove_adapter(dev);
   }

   return 0;
}

static void quitting_signal(int sig)
{
   (void)sig;
   quitting = 1;
}

void add_adapters_that_are_already_plugged_in(){
   fprintf(stderr, "Checking through devices that are already plugged in\n");

   struct libusb_device **devices;

   //Get a list of devices
   const int count = libusb_get_device_list(NULL, &devices);

   //No devices found
   if(count==0)
      return;

   for (int i = 0; i < count; i++)
   {
      struct libusb_device_descriptor desc;
      libusb_get_device_descriptor(devices[i], &desc);
      if (desc.idVendor == VENDOR_ID && desc.idProduct == PRODUCT_ID){
         //Adapter was found, let's get a reference to it and add it to our list
         //of adapters
         add_adapter(devices[i]);
      }
   }

   //Free list of devices, decrementing their references
   libusb_free_device_list(devices, 1);
}



bool setup_hotplugging(libusb_hotplug_callback_handle *callback){
   fprintf(stderr, "Preparing hotplugging\n");

   int hotplug_capability = libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG);
   if (hotplug_capability) {
      const int hotplug_ret = libusb_hotplug_register_callback(NULL,
            LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
            0, VENDOR_ID, PRODUCT_ID,
            LIBUSB_HOTPLUG_MATCH_ANY, hotplug_callback, NULL, callback);

      if (hotplug_ret != LIBUSB_SUCCESS) {
         fprintf(stderr, "cannot register hotplug callback, hotplugging not enabled\n");
         return false;
      }

      return true;
   }

   return false;
}



void setup_signal_catching(){
   struct sigaction sa;
   memset(&sa, 0, sizeof(sa));

   sa.sa_handler = quitting_signal;
   sa.sa_flags = SA_RESTART | SA_RESETHAND;
   sigemptyset(&sa.sa_mask);
   
   sigaction(SIGINT, &sa, NULL);
   sigaction(SIGTERM, &sa, NULL);
}



int main(int argc, char *argv[])
{
   struct udev *udev;
   struct udev_device *uinput;

   if (argc > 1 && (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "--raw") == 0))
   {
      fprintf(stderr, "raw mode enabled\n");
      raw_mode = true;
   }

   //Set up signal catching so we can exit gracefully
   setup_signal_catching();

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

   add_adapters_that_are_already_plugged_in();

   libusb_hotplug_callback_handle callback;
   bool hotplug_capability = setup_hotplugging(&callback);

   // pump events until shutdown & all helper threads finish cleaning up
   while (!quitting)
      libusb_handle_events_completed(NULL, (int *)&quitting);

   while (adapters.next)
      remove_adapter(adapters.next->device);

   if (hotplug_capability)
      libusb_hotplug_deregister_callback(NULL, callback);

   libusb_exit(NULL);
   udev_device_unref(uinput);
   udev_unref(udev);

   return 0;
}
