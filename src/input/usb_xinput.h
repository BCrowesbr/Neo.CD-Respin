/****************************************************************************
* Neo.CD Respin - Wii USB XInput support
*
* Initial target:
*   8BitDo Ultimate Wired Controller
*   VID 0x2DC8 / PID 0x3106
*
* Player 1 only. No rumble/LED in this first implementation.
****************************************************************************/

#ifndef __USB_XINPUT_H__
#define __USB_XINPUT_H__

#ifdef HW_RVL

#include <gccore.h>

/* Virtual button masks used by the Respin runtime mapper. */
#define USBX_A       0x00000001
#define USBX_B       0x00000002
#define USBX_X       0x00000004
#define USBX_Y       0x00000008
#define USBX_LB      0x00000010
#define USBX_RB      0x00000020
#define USBX_BACK    0x00000040
#define USBX_START   0x00000080
#define USBX_GUIDE   0x00000100
#define USBX_L3      0x00000200
#define USBX_R3      0x00000400
#define USBX_LT      0x00000800
#define USBX_RT      0x00001000
#define USBX_DPAD_UP    0x00010000
#define USBX_DPAD_DOWN  0x00020000
#define USBX_DPAD_LEFT  0x00040000
#define USBX_DPAD_RIGHT 0x00080000

int  usb_xinput_init(void);
int  usb_xinput_connected(void);
u32  usb_xinput_buttons_held(void);
u32  usb_xinput_buttons_down(void);
s16  usb_xinput_lx(void);
s16  usb_xinput_ly(void);

#endif /* HW_RVL */
#endif /* __USB_XINPUT_H__ */
