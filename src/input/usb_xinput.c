/****************************************************************************
* Neo.CD Respin - Wii USB XInput support
*
* Initial implementation for the 8BitDo Ultimate Wired Controller
* (VID 0x2DC8, PID 0x3106), which uses the Xbox 360/XInput USB protocol.
*
* This is intentionally conservative:
* - Player 1 only
* - input only
* - no rumble / LED output
****************************************************************************/

#ifdef HW_RVL

#include <gccore.h>
#include <ogc/usb.h>
#include <ogc/lwp.h>
#include <ogc/semaphore.h>
#include <string.h>
#include <malloc.h>

#include "usb_xinput.h"

#define USBX_VID_8BITDO        0x2DC8
#define USBX_PID_ULTIMATE      0x3106

#ifndef USB_CLASS_VENDOR_SPEC
#define USB_CLASS_VENDOR_SPEC  0xFF
#endif

#define USBX_SUBCLASS_XBOX360  0x5D
#define USBX_PROTOCOL_XBOX360  0x01

#define USBX_DEVLIST_MAX       16
#define USBX_MAX_PACKET        32
#define USBX_HEAP_SIZE         4096
#define USBX_THREAD_STACKSIZE  (8 * 1024)
#define USBX_THREAD_PRIO       68
#define USBX_TRIGGER_THRESH    32

typedef struct
{
  int connected;
  s32 fd;
  u8 configuration;
  u32 interface;
  u32 alt_interface;
  u8 ep_in;
  u8 ep_out;
  u32 ep_size;

  volatile u32 held;
  volatile u32 down;
  volatile s16 lx;
  volatile s16 ly;
} USBXDevice;

static USBXDevice usbpad;
static s32 usb_heap = -1;
static lwp_t usb_thread = LWP_THREAD_NULL;
static sem_t usb_sema = LWP_SEM_NULL;
static int usb_thread_started = 0;
static int usb_thread_quit = 0;
static u8 usb_stack[USBX_THREAD_STACKSIZE] ATTRIBUTE_ALIGN(8);
static u8 *usb_packet = NULL;

static s16
usb_le16(const u8 *p)
{
  return (s16)((u16)p[0] | ((u16)p[1] << 8));
}

static void
usb_xinput_parse_report(const u8 *data, int len)
{
  u32 now = 0;
  u8 b2, b3;

  /*
   * Xbox 360 wired input packet:
   * 00 14 [buttons0] [buttons1] LT RT LXlo LXhi LYlo LYhi ...
   */
  if (len < 14)
    return;

  if (data[0] != 0x00)
    return;

  b2 = data[2];
  b3 = data[3];

  if (b2 & 0x01) now |= USBX_DPAD_UP;
  if (b2 & 0x02) now |= USBX_DPAD_DOWN;
  if (b2 & 0x04) now |= USBX_DPAD_LEFT;
  if (b2 & 0x08) now |= USBX_DPAD_RIGHT;

  if (b3 & 0x10) now |= USBX_A;
  if (b3 & 0x20) now |= USBX_B;
  if (b3 & 0x40) now |= USBX_X;
  if (b3 & 0x80) now |= USBX_Y;

  if (b3 & 0x01) now |= USBX_LB;
  if (b3 & 0x02) now |= USBX_RB;
  if (b3 & 0x04) now |= USBX_GUIDE;

  if (b2 & 0x20) now |= USBX_BACK;
  if (b2 & 0x10) now |= USBX_START;
  if (b2 & 0x40) now |= USBX_L3;
  if (b2 & 0x80) now |= USBX_R3;

  if (data[4] > USBX_TRIGGER_THRESH) now |= USBX_LT;
  if (data[5] > USBX_TRIGGER_THRESH) now |= USBX_RT;

  /*
   * Keep button-down edges latched until the emulation/UI thread consumes
   * them. USB input reports arrive much faster than the 60 Hz frontend.
   * Overwriting 'down' on every report loses short presses because the next
   * report (while the button is still held) contains no new edge.
   */
  usbpad.down |= now & ~usbpad.held;
  usbpad.held = now;
  usbpad.lx = usb_le16(&data[6]);
  usbpad.ly = usb_le16(&data[8]);
}

static void
usb_xinput_close(void)
{
  if (usbpad.fd >= 0)
  {
    if (usbpad.ep_in)
      USB_ClearHalt(usbpad.fd, usbpad.ep_in);
    USB_CloseDevice(&usbpad.fd);
  }

  usbpad.fd = -1;
  usbpad.connected = 0;
  usbpad.ep_in = 0;
  usbpad.ep_out = 0;
  usbpad.ep_size = 0;
  usbpad.held = 0;
  usbpad.down = 0;
  usbpad.lx = 0;
  usbpad.ly = 0;
}

static s32
usb_xinput_disconnect_cb(s32 result, void *usrdata)
{
  (void)result;
  (void)usrdata;

  usbpad.connected = 0;
  usbpad.held = 0;
  usbpad.down = 0;
  usbpad.lx = 0;
  usbpad.ly = 0;

  if (usb_sema != LWP_SEM_NULL)
    LWP_SemPost(usb_sema);

  return 1;
}

static s32
usb_xinput_device_change_cb(s32 result, void *usrdata)
{
  (void)result;
  (void)usrdata;

  if (usb_sema != LWP_SEM_NULL)
    LWP_SemPost(usb_sema);

  return 1;
}

static s32
usb_xinput_read_cb(s32 result, void *usrdata)
{
  (void)usrdata;

  if (result > 0)
    usb_xinput_parse_report(usb_packet, result);
  else
    usbpad.connected = 0;

  if (usb_sema != LWP_SEM_NULL)
    LWP_SemPost(usb_sema);

  return 0;
}

static int
usb_xinput_open(void)
{
  usb_device_entry *list;
  u8 count = 0;
  int i;
  int found = 0;

  list = (usb_device_entry *)iosAlloc(
      usb_heap, USBX_DEVLIST_MAX * sizeof(usb_device_entry));
  if (!list)
    return -1;

  memset(list, 0, USBX_DEVLIST_MAX * sizeof(usb_device_entry));

  if (USB_GetDeviceList(list, USBX_DEVLIST_MAX,
                        USB_CLASS_VENDOR_SPEC, &count) < 0)
  {
    iosFree(usb_heap, list);
    return -2;
  }

  usb_xinput_close();

  for (i = 0; i < count && !found; i++)
  {
    s32 fd = -1;
    usb_devdesc desc;
    u32 c, f, e;

    if (list[i].vid != USBX_VID_8BITDO ||
        list[i].pid != USBX_PID_ULTIMATE)
      continue;

    if (USB_OpenDevice(list[i].device_id, list[i].vid, list[i].pid, &fd) < 0)
      continue;

    if (USB_GetDescriptors(fd, &desc) < 0)
    {
      USB_CloseDevice(&fd);
      continue;
    }

    for (c = 0; c < desc.bNumConfigurations && !found; c++)
    {
      usb_configurationdesc *conf = &desc.configurations[c];

      for (f = 0; f < conf->bNumInterfaces && !found; f++)
      {
        usb_interfacedesc *iface = &conf->interfaces[f];

        /*
         * Standard Xbox 360 wired interface is vendor-specific,
         * subclass 0x5D, protocol 0x01. Some compatible pads leave
         * protocol details less strict, so VID/PID remains authoritative.
         */
        if (iface->bInterfaceClass != USB_CLASS_VENDOR_SPEC)
          continue;

        if (iface->bInterfaceSubClass != USBX_SUBCLASS_XBOX360 &&
            iface->bInterfaceSubClass != 0x00)
          continue;

        {
          u8 ep_in = 0;
          u8 ep_out = 0;
          u32 ep_in_size = 0;

          for (e = 0; e < iface->bNumEndpoints; e++)
          {
            usb_endpointdesc *ep = &iface->endpoints[e];

            if (ep->bmAttributes != USB_ENDPOINT_INTERRUPT)
              continue;

            if (ep->bEndpointAddress & USB_ENDPOINT_IN)
            {
              if (ep->wMaxPacketSize > 0 &&
                  ep->wMaxPacketSize <= USBX_MAX_PACKET)
              {
                ep_in = ep->bEndpointAddress;
                ep_in_size = ep->wMaxPacketSize;
              }
            }
            else
            {
              ep_out = ep->bEndpointAddress;
            }
          }

          if (ep_in)
          {
            usbpad.fd = fd;
            usbpad.configuration = conf->bConfigurationValue;
            usbpad.interface = iface->bInterfaceNumber;
            usbpad.alt_interface = iface->bAlternateSetting;
            usbpad.ep_in = ep_in;
            usbpad.ep_out = ep_out;
            usbpad.ep_size = ep_in_size;
            found = 1;
          }
        }
      }
    }

    USB_FreeDescriptors(&desc);

    if (!found)
      USB_CloseDevice(&fd);
  }

  iosFree(usb_heap, list);

  if (!found)
    return -3;

  {
    u8 current_conf = 0;

    if (USB_GetConfiguration(usbpad.fd, &current_conf) < 0)
    {
      usb_xinput_close();
      return -4;
    }

    if (current_conf != usbpad.configuration &&
        USB_SetConfiguration(usbpad.fd, usbpad.configuration) < 0)
    {
      usb_xinput_close();
      return -5;
    }

    if (usbpad.alt_interface != 0 &&
        USB_SetAlternativeInterface(usbpad.fd,
                                    usbpad.interface,
                                    usbpad.alt_interface) < 0)
    {
      usb_xinput_close();
      return -6;
    }
  }

  /*
   * Xbox 360/XInput devices normally need an enable command on the
   * interrupt OUT endpoint before they begin sending input reports.
   * The 8BitDo 2DC8:3106 is handled by Linux as an XTYPE_XBOX360 device.
   *
   * Packet: 01 03 0E
   */
  if (usbpad.ep_out)
  {
    u8 *enable = (u8 *)iosAlloc(usb_heap, 32);
    if (!enable)
    {
      usb_xinput_close();
      return -7;
    }

    memset(enable, 0, 32);
    enable[0] = 0x01;
    enable[1] = 0x03;
    enable[2] = 0x0E;

    if (USB_WriteIntrMsg(usbpad.fd, usbpad.ep_out, 3, enable) < 0)
    {
      iosFree(usb_heap, enable);
      usb_xinput_close();
      return -8;
    }

    iosFree(usb_heap, enable);
  }

  if (USB_DeviceRemovalNotifyAsync(usbpad.fd,
                                   usb_xinput_disconnect_cb,
                                   NULL) < 0)
  {
    usb_xinput_close();
    return -9;
  }

  usbpad.connected = 1;
  return 1;
}

static void *
usb_xinput_thread(void *arg)
{
  (void)arg;

  while (!usb_thread_quit)
  {
    if (!usbpad.connected)
    {
      usb_xinput_close();

      if (usb_xinput_open() < 0)
      {
        USB_DeviceChangeNotifyAsync(USB_CLASS_VENDOR_SPEC,
                                    usb_xinput_device_change_cb,
                                    NULL);
        LWP_SemWait(usb_sema);
        continue;
      }
    }

    if (USB_ReadIntrMsgAsync(usbpad.fd,
                             usbpad.ep_in,
                             usbpad.ep_size,
                             usb_packet,
                             (usbcallback)usb_xinput_read_cb,
                             NULL) < 0)
    {
      usbpad.connected = 0;
      continue;
    }

    LWP_SemWait(usb_sema);
  }

  usb_xinput_close();
  return NULL;
}

int
usb_xinput_init(void)
{
  if (usb_thread_started)
    return 0;

  memset(&usbpad, 0, sizeof(usbpad));
  usbpad.fd = -1;

  if (USB_Initialize() != IPC_OK)
    return -1;

  usb_heap = iosCreateHeap(USBX_HEAP_SIZE);
  if (usb_heap < 0)
    return -2;

  usb_packet = (u8 *)iosAlloc(usb_heap, USBX_MAX_PACKET);
  if (!usb_packet)
    return -3;

  memset(usb_packet, 0, USBX_MAX_PACKET);

  if (LWP_SemInit(&usb_sema, 0, 1) != 0)
    return -4;

  usb_thread_quit = 0;
  memset(usb_stack, 0, sizeof(usb_stack));

  if (LWP_CreateThread(&usb_thread,
                       usb_xinput_thread,
                       NULL,
                       usb_stack,
                       sizeof(usb_stack),
                       USBX_THREAD_PRIO) != 0)
    return -5;

  usb_thread_started = 1;
  return 0;
}

int
usb_xinput_connected(void)
{
  return usbpad.connected;
}

u32
usb_xinput_buttons_held(void)
{
  return usbpad.held;
}

u32
usb_xinput_buttons_down(void)
{
  u32 down = usbpad.down;
  usbpad.down = 0;
  return down;
}

s16
usb_xinput_lx(void)
{
  return usbpad.lx;
}

s16
usb_xinput_ly(void)
{
  return usbpad.ly;
}

#endif /* HW_RVL */
