/****************************************************************************
*   NeoCDRX
*   NeoGeo CD Emulator
*   NeoCD Redux - Copyright (C) 2007 softdev
****************************************************************************/

#include "neocdrx.h"
#include <math.h>

#ifdef HW_RVL
#include <wiiuse/wpad.h>
#include "usb_xinput.h"
#ifndef USBX_MAX_PADS
#define USBX_MAX_PADS 1
#endif

static int
usb_xinput_connected_slot_compat(int slot)
{
  return (slot == 0) ? usb_xinput_connected() : 0;
}

static u32
usb_xinput_buttons_held_slot_compat(int slot)
{
  return (slot == 0) ? usb_xinput_buttons_held() : 0;
}

static u32
usb_xinput_buttons_down_slot_compat(int slot)
{
  return (slot == 0) ? usb_xinput_buttons_down() : 0;
}

static s16
usb_xinput_lx_slot_compat(int slot)
{
  return (slot == 0) ? usb_xinput_lx() : 0;
}

static s16
usb_xinput_ly_slot_compat(int slot)
{
  return (slot == 0) ? usb_xinput_ly() : 0;
}
#endif


#define P1UP    0x00000001
#define P1DOWN  0x00000002
#define P1LEFT  0x00000004
#define P1RIGHT 0x00000008
#define P1A     0x00000010
#define P1B     0x00000020
#define P1C     0x00000040
#define P1D     0x00000080

#define P2UP    0x00000100
#define P2DOWN  0x00000200
#define P2LEFT  0x00000400
#define P2RIGHT 0x00000800
#define P2A     0x00001000
#define P2B     0x00002000
#define P2C     0x00004000
#define P2D     0x00008000

#define P1START 0x00010000
#define P1SEL   0x00020000
#define P2START 0x00040000
#define P2SEL   0x00080000

#define SPECIAL 0x01000000

/* Virtual GameCube C-stick directions used by runtime remapping. */
#define GC_C_UP     0x10000000
#define GC_C_DOWN   0x20000000
#define GC_C_LEFT   0x40000000
#define GC_C_RIGHT  0x80000000
#define GC_C_THRESH 60

static u32 keys = 0;
static int padcal = 80;
int accept_input = 0;

/* GUI-visible NEW-PRESS state for the configured Emulator Menu control. */
static int gui_menu_button_down = 0;
static int menu_release_latch = 0;

void
input_arm_menu_release_latch (void)
{
  menu_release_latch = 1;
}

/*
 * Runtime controller remapping.
 *
 * Directions and menu navigation remain fixed.
 * Remappable actions:
 * A, B, C, D, Start, Select, Emulator Menu, Memory Card Save.
 */
typedef struct
{
  u32 mask;
  const char *name;
} InputPhysicalButton;

/*** GameCube Controller ***/
static const InputPhysicalButton gc_choices[] =
{
  { PAD_BUTTON_A,     "A" },
  { PAD_BUTTON_B,     "B" },
  { PAD_BUTTON_X,     "X" },
  { PAD_BUTTON_Y,     "Y" },
  { PAD_TRIGGER_L,    "L" },
  { PAD_TRIGGER_R,    "R" },
  { PAD_TRIGGER_Z,    "Z" },
  { PAD_BUTTON_START, "START" },
  { GC_C_UP,          "C-UP" },
  { GC_C_DOWN,        "C-DOWN" },
  { GC_C_LEFT,        "C-LEFT" },
  { GC_C_RIGHT,       "C-RIGHT" }
};

/*
 * action order:
 * Neo A, Neo B, Neo C, Neo D, Start, Select, Menu, Save.
 *
 * These defaults reproduce the original NeoCDRX mapping.
 */
static int gc_map[INPUT_MAP_COUNT] =
{
  1, 0, 3, 2, 7, 6, 4, 5
};

#ifdef HW_RVL

#define MAX_HELD_CNT 15
static u32 held_cnt = 0;

/*** Bare Wiimote ***/
static const InputPhysicalButton wiimote_choices[] =
{
  { WPAD_BUTTON_1,                         "1" },
  { WPAD_BUTTON_2,                         "2" },
  { WPAD_BUTTON_A,                         "A" },
  { WPAD_BUTTON_B,                         "B" },
  { WPAD_BUTTON_PLUS,                      "+" },
  { WPAD_BUTTON_MINUS,                     "-" },
  { WPAD_BUTTON_HOME,                      "HOME" },
  { WPAD_BUTTON_PLUS | WPAD_BUTTON_MINUS,  "+ + -" }
};

/*** Wiimote + Nunchuk ***/
static const InputPhysicalButton nunchuk_choices[] =
{
  { WPAD_BUTTON_1,                         "1" },
  { WPAD_BUTTON_2,                         "2" },
  { WPAD_BUTTON_A,                         "A" },
  { WPAD_BUTTON_B,                         "B" },
  { WPAD_NUNCHUK_BUTTON_C,                 "C" },
  { WPAD_NUNCHUK_BUTTON_Z,                 "Z" },
  { WPAD_BUTTON_PLUS,                      "+" },
  { WPAD_BUTTON_MINUS,                     "-" },
  { WPAD_BUTTON_HOME,                      "HOME" },
  { WPAD_BUTTON_PLUS | WPAD_BUTTON_MINUS,  "+ + -" }
};

/*** Classic Controller ***/
static const InputPhysicalButton classic_choices[] =
{
  { WPAD_CLASSIC_BUTTON_A,      "A" },
  { WPAD_CLASSIC_BUTTON_B,      "B" },
  { WPAD_CLASSIC_BUTTON_X,      "X" },
  { WPAD_CLASSIC_BUTTON_Y,      "Y" },
  { WPAD_CLASSIC_BUTTON_FULL_L, "L" },
  { WPAD_CLASSIC_BUTTON_FULL_R, "R" },
  { WPAD_CLASSIC_BUTTON_PLUS,   "+" },
  { WPAD_CLASSIC_BUTTON_MINUS,  "-" },
  { WPAD_CLASSIC_BUTTON_HOME,   "HOME" }
};

/*** USB XInput (8BitDo Ultimate Wired / Xbox 360 protocol) ***/
static const InputPhysicalButton usb_xinput_choices[] =
{
  { USBX_A,     "A" },
  { USBX_B,     "B" },
  { USBX_X,     "X" },
  { USBX_Y,     "Y" },
  { USBX_LB,    "LB" },
  { USBX_RB,    "RB" },
  { USBX_BACK,  "BACK" },
  { USBX_START, "START" },
  { USBX_GUIDE, "GUIDE" },
  { USBX_L3,    "L3" },
  { USBX_R3,    "R3" },
  { USBX_LT,    "LT" },
  { USBX_RT,    "RT" }
};

/*** Defaults reproduce the original wpadmap/startsel/shortcut behavior. ***/
static int wiimote_map[INPUT_MAP_COUNT] =
{
  0, 1, 3, 2, 4, 5, 6, 7
};

static int nunchuk_map[INPUT_MAP_COUNT] =
{
  2, 3, 6, 0, 6, 7, 8, 9
};

static int classic_map[INPUT_MAP_COUNT] =
{
  1, 0, 3, 2, 6, 7, 8, 5
};

static int usb_xinput_map[INPUT_MAP_COUNT] =
{
  1, 0, 3, 2, 7, 6, 8, 5
};

#endif

static u32
gc_add_cstick (int chan, u32 buttons)
{
  s8 cx = PAD_SubStickX(chan);
  s8 cy = PAD_SubStickY(chan);

  if (cy > GC_C_THRESH)
    buttons |= GC_C_UP;
  else if (cy < -GC_C_THRESH)
    buttons |= GC_C_DOWN;

  if (cx < -GC_C_THRESH)
    buttons |= GC_C_LEFT;
  else if (cx > GC_C_THRESH)
    buttons |= GC_C_RIGHT;

  return buttons;
}

static int
button_match (u32 held, u32 mask)
{
  return (mask != 0) && ((held & mask) == mask);
}

static const InputPhysicalButton *
get_choices (int device, int *count)
{
  switch (device)
  {
    case INPUT_DEV_GC:
      *count = sizeof(gc_choices) / sizeof(gc_choices[0]);
      return gc_choices;

#ifdef HW_RVL
    case INPUT_DEV_WIIMOTE:
      *count = sizeof(wiimote_choices) / sizeof(wiimote_choices[0]);
      return wiimote_choices;

    case INPUT_DEV_NUNCHUK:
      *count = sizeof(nunchuk_choices) / sizeof(nunchuk_choices[0]);
      return nunchuk_choices;

    case INPUT_DEV_CLASSIC:
      *count = sizeof(classic_choices) / sizeof(classic_choices[0]);
      return classic_choices;

    case INPUT_DEV_USB_XINPUT:
      *count = sizeof(usb_xinput_choices) / sizeof(usb_xinput_choices[0]);
      return usb_xinput_choices;
#endif

    default:
      *count = 0;
      return NULL;
  }
}

static int *
get_map (int device)
{
  switch (device)
  {
    case INPUT_DEV_GC:
      return gc_map;

#ifdef HW_RVL
    case INPUT_DEV_WIIMOTE:
      return wiimote_map;

    case INPUT_DEV_NUNCHUK:
      return nunchuk_map;

    case INPUT_DEV_CLASSIC:
      return classic_map;

    case INPUT_DEV_USB_XINPUT:
      return usb_xinput_map;
#endif

    default:
      return NULL;
  }
}

const char *
input_device_name (int device)
{
  switch (device)
  {
    case INPUT_DEV_GC:      return "GameCube";
    case INPUT_DEV_WIIMOTE: return "Wiimote";
    case INPUT_DEV_NUNCHUK: return "Wiimote+Nunchuk";
    case INPUT_DEV_CLASSIC:   return "Classic";
    case INPUT_DEV_USB_XINPUT: return "USB XInput";
    default:                  return "?";
  }
}

const char *
input_mapping_name (int device, int action)
{
  int count;
  int *map = get_map(device);
  const InputPhysicalButton *choices = get_choices(device, &count);

  if (!map || !choices || action < 0 || action >= INPUT_MAP_COUNT)
    return "?";

  if (map[action] < 0 || map[action] >= count)
    return "?";

  return choices[map[action]].name;
}

void
input_cycle_mapping (int device, int action, int direction)
{
  int count;
  int *map = get_map(device);
  const InputPhysicalButton *choices = get_choices(device, &count);

  (void) choices;

  if (!map || count <= 0 || action < 0 || action >= INPUT_MAP_COUNT)
    return;

  if (direction < 0)
  {
    map[action]--;
    if (map[action] < 0)
      map[action] = count - 1;
  }
  else
  {
    map[action]++;
    if (map[action] >= count)
      map[action] = 0;
  }
}

int
input_capture_mapping (int device, int action)
{
  int count;
  int i;
  int released = 0;
  int settle = 0;
  int *map = get_map(device);
  const InputPhysicalButton *choices = get_choices(device, &count);

  if (!map || !choices || action < 0 || action >= INPUT_MAP_COUNT)
    return 0;

  /*
   * First wait until the button used to enter the capture screen
   * has been completely released. A few empty scans are required
   * before accepting the next press.
   */
  while (1)
  {
    u32 held = 0;

    VIDEO_WaitVSync();

    if (device == INPUT_DEV_GC)
    {
      PAD_ScanPads();
      held = gc_add_cstick(0, PAD_ButtonsHeld(0));
    }
#ifdef HW_RVL
    else if (device == INPUT_DEV_USB_XINPUT)
    {
      usb_xinput_init();
      held = usb_xinput_buttons_held();
    }
    else
    {
      WPAD_ScanPads();
      held = WPAD_ButtonsHeld(0);
    }
#endif

    if (held == 0)
    {
      settle++;
      if (settle >= 3)
      {
        released = 1;
        break;
      }
    }
    else
      settle = 0;
  }

  if (!released)
    return 0;

  /*
   * Capture a NEW physical press.  Exact multi-button entries in the
   * choice table (such as Wiimote + and -) are checked first.
   */
  while (1)
  {
    u32 down = 0;

    VIDEO_WaitVSync();

    if (device == INPUT_DEV_GC)
    {
      static u32 last_cstick = 0;
      u32 now;

      PAD_ScanPads();
      down = PAD_ButtonsDown(0);
      now = gc_add_cstick(0, 0) & (GC_C_UP | GC_C_DOWN | GC_C_LEFT | GC_C_RIGHT);
      down |= (now & ~last_cstick);
      last_cstick = now;
    }
#ifdef HW_RVL
    else if (device == INPUT_DEV_USB_XINPUT)
    {
      usb_xinput_init();
      down = usb_xinput_buttons_down();
    }
    else
    {
      WPAD_ScanPads();
      down = WPAD_ButtonsDown(0);
    }
#endif

    if (!down)
      continue;

    /* Prefer exact combinations over individual members. */
    for (i = count - 1; i >= 0; i--)
    {
      if (choices[i].mask == down)
      {
        map[action] = i;
        return 1;
      }
    }

    /* Otherwise use the first supported physical button pressed. */
    for (i = 0; i < count; i++)
    {
      if ((down & choices[i].mask) == choices[i].mask)
      {
        map[action] = i;
        return 1;
      }
    }
  }
}

void
input_reset_mapping (int device)
{
  static const int gc_default[INPUT_MAP_COUNT] =
    { 1, 0, 3, 2, 7, 6, 4, 5 };

#ifdef HW_RVL
  static const int wiimote_default[INPUT_MAP_COUNT] =
    { 0, 1, 3, 2, 4, 5, 6, 7 };
  static const int nunchuk_default[INPUT_MAP_COUNT] =
    { 2, 3, 6, 0, 6, 7, 8, 9 };
  static const int classic_default[INPUT_MAP_COUNT] =
    { 1, 0, 3, 2, 6, 7, 8, 5 };
  static const int usb_xinput_default[INPUT_MAP_COUNT] =
    { 1, 0, 3, 2, 7, 6, 8, 5 };
#endif

  int *map = get_map(device);

  if (!map)
    return;

  switch (device)
  {
    case INPUT_DEV_GC:
      memcpy(map, gc_default, sizeof(gc_default));
      break;

#ifdef HW_RVL
    case INPUT_DEV_WIIMOTE:
      memcpy(map, wiimote_default, sizeof(wiimote_default));
      break;

    case INPUT_DEV_NUNCHUK:
      memcpy(map, nunchuk_default, sizeof(nunchuk_default));
      break;

    case INPUT_DEV_CLASSIC:
      memcpy(map, classic_default, sizeof(classic_default));
      break;

    case INPUT_DEV_USB_XINPUT:
      memcpy(map, usb_xinput_default, sizeof(usb_xinput_default));
      break;
#endif
  }
}

void
input_reset_all_mappings (void)
{
  int i;

  for (i = 0; i < INPUT_DEV_COUNT; i++)
    input_reset_mapping(i);
}

int
input_mapping_choice_count (int device)
{
  int count = 0;
  const InputPhysicalButton *choices = get_choices(device, &count);

  if (!choices)
    return 0;

  return count;
}

int
input_get_mapping_index (int device, int action)
{
  int count;
  int *map = get_map(device);
  const InputPhysicalButton *choices = get_choices(device, &count);

  if (!map || !choices || action < 0 || action >= INPUT_MAP_COUNT)
    return -1;

  if (map[action] < 0 || map[action] >= count)
    return -1;

  return map[action];
}

int
input_set_mapping_index (int device, int action, int index)
{
  int count;
  int *map = get_map(device);
  const InputPhysicalButton *choices = get_choices(device, &count);

  if (!map || !choices || action < 0 || action >= INPUT_MAP_COUNT)
    return 0;

  if (index < 0 || index >= count)
    return 0;

  map[action] = index;
  return 1;
}

static u32
mapped_mask (int device, int action)
{
  int count;
  int *map = get_map(device);
  const InputPhysicalButton *choices = get_choices(device, &count);

  if (!map || !choices || action < 0 || action >= INPUT_MAP_COUNT)
    return 0;

  if (map[action] < 0 || map[action] >= count)
    return 0;

  return choices[map[action]].mask;
}

/*
 * True while ANY physical control mapped to Emulator Menu is still held.
 *
 * This is used only to re-arm Menu after GUI -> gameplay.  It deliberately
 * checks both players so one inactive/second controller cannot clear the gate
 * while the original Menu button is still down.
 */
static int
input_any_menu_held (void)
{
  u32 p;
  int chan;

  for (chan = 0; chan < 2; chan++)
  {
    p = gc_add_cstick(chan, PAD_ButtonsHeld(chan));

    if (button_match(p, mapped_mask(INPUT_DEV_GC, INPUT_MAP_MENU)))
      return 1;

#ifdef HW_RVL
    {
      u32 exp;
      int device;

      if (WPAD_Probe(chan, &exp) == WPAD_ERR_NONE)
      {
        p = WPAD_ButtonsHeld(chan);

        if (exp == WPAD_EXP_NUNCHUK)
          device = INPUT_DEV_NUNCHUK;
        else if (exp == WPAD_EXP_CLASSIC)
          device = INPUT_DEV_CLASSIC;
        else
          device = INPUT_DEV_WIIMOTE;

        if (button_match(p, mapped_mask(device, INPUT_MAP_MENU)))
          return 1;
      }
    }
#endif
  }

#ifdef HW_RVL
  usb_xinput_init();
  for (chan = 0; chan < USBX_MAX_PADS; chan++)
  {
    if (usb_xinput_connected_slot_compat(chan) &&
        button_match(usb_xinput_buttons_held_slot_compat(chan),
                     mapped_mask(INPUT_DEV_USB_XINPUT, INPUT_MAP_MENU)))
      return 1;
  }
#endif

  return 0;
}

#ifdef HW_RVL

#define PI 3.14159265f

static s8 WPAD_StickX(int chan, int right)
{
  float mag = 0.0;
  float ang = 0.0;

  WPADData *data = WPAD_Data(chan);
  switch (data->exp.type)
  {
    case WPAD_EXP_NUNCHUK:
      if (right == 0)
      {
        mag = data->exp.nunchuk.js.mag;
        ang = data->exp.nunchuk.js.ang;
      }
      break;

    case WPAD_EXP_CLASSIC:
      if (right == 0)
      {
        mag = data->exp.classic.ljs.mag;
        ang = data->exp.classic.ljs.ang;
      }
      else
      {
        mag = data->exp.classic.rjs.mag;
        ang = data->exp.classic.rjs.ang;
      }
      break;

    default:
      break;
  }

  /* Calculate X value (angle need to be converted into radian) */
  if (mag > 1.0) mag = 1.0;
  else if (mag < -1.0) mag = -1.0;
  double val = mag * sin(M_PI * ang/180.0f);
 
  return (s8)(val * 128.0f);
}


static s8 WPAD_StickY(int chan, int right)
{
  float mag = 0.0;
  float ang = 0.0;

  WPADData *data = WPAD_Data(chan);

  switch (data->exp.type)
  {
    case WPAD_EXP_NUNCHUK:
      if (right == 0)
      {
        mag = data->exp.nunchuk.js.mag;
        ang = data->exp.nunchuk.js.ang;
      }
      break;

    case WPAD_EXP_CLASSIC:
      if (right == 0)
      {
        mag = data->exp.classic.ljs.mag;
        ang = data->exp.classic.ljs.ang;
      }
      else
      {
        mag = data->exp.classic.rjs.mag;
        ang = data->exp.classic.rjs.ang;
      }
      break;

    default:
      break;
  }

  /* Calculate X value (angle need to be converted into radian) */
  if (mag > 1.0) mag = 1.0;
  else if (mag < -1.0) mag = -1.0;
  double val = mag * cos(M_PI * ang/180.0f);
 
  return (s8)(val * 128.0f);
}
#endif

int
input_menu_button_down (void)
{
  return gui_menu_button_down;
}

u16 getMenuButtons(void)
{
  
  /* Slowdown input updates */
  VIDEO_WaitVSync();
  
  /* Get gamepad inputs */
  PAD_ScanPads();
  u16 p = PAD_ButtonsDown(0);
  {
    static u32 gui_last_cstick = 0;
    u32 gc_cstick_now;
    u32 gc_map_down = (u32)p;

    /*
     * Runtime mapping supports C-stick virtual directions too, so create
     * a NEW-PRESS edge for those in addition to PAD_ButtonsDown().
     */
    gc_cstick_now = gc_add_cstick(0, 0) &
                    (GC_C_UP | GC_C_DOWN | GC_C_LEFT | GC_C_RIGHT);
    gc_map_down |= (gc_cstick_now & ~gui_last_cstick);
    gui_last_cstick = gc_cstick_now;

    gui_menu_button_down =
      button_match(gc_map_down, mapped_mask(INPUT_DEV_GC, INPUT_MAP_MENU));
  }
  s8 x  = PAD_StickX(0);
  s8 y  = PAD_StickY(0);
  if (x > 70) p |= PAD_BUTTON_RIGHT;
  else if (x < -70) p |= PAD_BUTTON_LEFT;
  if (y > 60) p |= PAD_BUTTON_UP;
  else if (y < -60) p |= PAD_BUTTON_DOWN;

#ifdef HW_RVL
  /*
   * USB XInput is independent from WPAD and can navigate the GUI directly.
   * Its mapped Emulator Menu button also participates in the menu-release logic.
   */
  usb_xinput_init();
  if (usb_xinput_connected())
  {
    static int usb_nav_dir = 0;
    static int usb_nav_repeat = 0;
    u32 uh = usb_xinput_buttons_held();
    u32 ud = usb_xinput_buttons_down();
    s16 ux = usb_xinput_lx();
    s16 uy = usb_xinput_ly();
    int nav = 0;

    /*
     * USB menu navigation:
     * - D-Pad always acts as cursor/navigation.
     * - Left analog can also navigate, but with a deliberately large
     *   dead zone so small movements do not race through the menu.
     * - Holding either input repeats only after a short delay.
     *
     * D-Pad takes priority over the analog stick.
     */
    if (uh & USBX_DPAD_UP) nav = PAD_BUTTON_UP;
    else if (uh & USBX_DPAD_DOWN) nav = PAD_BUTTON_DOWN;
    else if (uh & USBX_DPAD_LEFT) nav = PAD_BUTTON_LEFT;
    else if (uh & USBX_DPAD_RIGHT) nav = PAD_BUTTON_RIGHT;
    else
    {
      if (ux > 24500) nav = PAD_BUTTON_RIGHT;
      else if (ux < -24500) nav = PAD_BUTTON_LEFT;
      else if (uy > 24500) nav = PAD_BUTTON_UP;
      else if (uy < -24500) nav = PAD_BUTTON_DOWN;
    }

    if (nav == 0)
    {
      usb_nav_dir = 0;
      usb_nav_repeat = 0;
    }
    else if (nav != usb_nav_dir)
    {
      /* New direction: move exactly once immediately. */
      p |= nav;
      usb_nav_dir = nav;
      usb_nav_repeat = 0;
    }
    else
    {
      /*
       * Held direction: wait ~12 frames, then repeat every 4 frames.
       * At 60 Hz this feels deliberate rather than overly sensitive.
       */
      usb_nav_repeat++;
      if (usb_nav_repeat >= 12)
      {
        if (((usb_nav_repeat - 12) & 3) == 0)
          p |= nav;
      }
    }

    /*
     * GUI confirm/back use the familiar Xbox-style A/B physical buttons.
     * Button edges are latched by usb_xinput.c until consumed.
     */
    if (ud & USBX_A) p |= PAD_BUTTON_A;
    if (ud & USBX_B) p |= PAD_BUTTON_B;

    if (button_match(ud, mapped_mask(INPUT_DEV_USB_XINPUT, INPUT_MAP_MENU)))
      gui_menu_button_down = 1;
  }

  /* Update WPAD status */
  WPAD_ScanPads();
  u32 q = WPAD_ButtonsDown(0);
  u32 h = WPAD_ButtonsHeld(0);
  {
    u32 exp;
    int gui_device;

    /*
     * When WPAD channel 0 exists, gameplay uses its mapping instead of GC.
     * Mirror that choice here.
     */
    if (WPAD_Probe(0, &exp) == WPAD_ERR_NONE)
    {
      if (exp == WPAD_EXP_NUNCHUK)
        gui_device = INPUT_DEV_NUNCHUK;
      else if (exp == WPAD_EXP_CLASSIC)
        gui_device = INPUT_DEV_CLASSIC;
      else
        gui_device = INPUT_DEV_WIIMOTE;

      gui_menu_button_down =
        button_match(q, mapped_mask(gui_device, INPUT_MAP_MENU));
    }
  }
  x = WPAD_StickX(0, 0);
  y = WPAD_StickY(0, 0);
  
     /* Is Wiimote directed toward screen? (horizontal/vertical orientation) */
     struct ir_t ir;
     WPAD_IR(0, &ir);

     /* Wiimote directions */
     if (q & WPAD_BUTTON_UP)         p |= ir.valid ? PAD_BUTTON_UP : PAD_BUTTON_LEFT;
     else if (q & WPAD_BUTTON_DOWN)  p |= ir.valid ? PAD_BUTTON_DOWN : PAD_BUTTON_RIGHT;
     else if (q & WPAD_BUTTON_LEFT)  p |= ir.valid ? PAD_BUTTON_LEFT : PAD_BUTTON_DOWN;
     else if (q & WPAD_BUTTON_RIGHT) p |= ir.valid ? PAD_BUTTON_RIGHT : PAD_BUTTON_UP;


     if (h & WPAD_BUTTON_UP)
     {
        held_cnt ++;
        if (held_cnt == MAX_HELD_CNT)
        {
           held_cnt = MAX_HELD_CNT - 2;
           p |= ir.valid ? PAD_BUTTON_UP : PAD_BUTTON_LEFT;
        }
     }
     else if (h & WPAD_BUTTON_DOWN)
     {
        held_cnt ++;
        if (held_cnt == MAX_HELD_CNT)
        {
           held_cnt = MAX_HELD_CNT - 2;
           p |= ir.valid ? PAD_BUTTON_DOWN : PAD_BUTTON_RIGHT;
        }
     }
     else if (h & WPAD_BUTTON_LEFT)
     {
        held_cnt ++;
        if (held_cnt == MAX_HELD_CNT)
        {
           held_cnt = MAX_HELD_CNT - 2;
           p |= ir.valid ? PAD_BUTTON_LEFT : PAD_BUTTON_DOWN;
        }
     }
     else if (h & WPAD_BUTTON_RIGHT)
     {
        held_cnt ++;
        if (held_cnt == MAX_HELD_CNT)
        {
           held_cnt = MAX_HELD_CNT - 2;
           p |= ir.valid ? PAD_BUTTON_RIGHT : PAD_BUTTON_UP;
        }
     }
     else held_cnt = 0;


     /* Analog sticks */
     if (y > 70)       p |= PAD_BUTTON_UP;
     else if (y < -70) p |= PAD_BUTTON_DOWN;
     if (x < -60)      p |= PAD_BUTTON_LEFT;
     else if (x > 60)  p |= PAD_BUTTON_RIGHT;

     /* Wii Classic Controller directions */
     if (q & WPAD_CLASSIC_BUTTON_UP)         p |= PAD_BUTTON_UP;
     else if (q & WPAD_CLASSIC_BUTTON_DOWN)  p |= PAD_BUTTON_DOWN;
     if (q & WPAD_CLASSIC_BUTTON_LEFT)       p |= PAD_BUTTON_LEFT;
     else if (q & WPAD_CLASSIC_BUTTON_RIGHT) p |= PAD_BUTTON_RIGHT;

     /* Wiimote keys */
     if (q & WPAD_BUTTON_MINUS)  p |= PAD_TRIGGER_L;
     if (q & WPAD_BUTTON_PLUS)   p |= PAD_TRIGGER_R;
     if (q & WPAD_BUTTON_A)      p |= PAD_BUTTON_X;
     if (q & WPAD_BUTTON_2)      p |= PAD_BUTTON_A;
     if (q & WPAD_BUTTON_1)      p |= PAD_BUTTON_B;
     if (q & WPAD_BUTTON_HOME)   p |= PAD_TRIGGER_Z;
  
     /* Wii Classic Controller keys */
     if (q & WPAD_CLASSIC_BUTTON_FULL_L) p |= PAD_TRIGGER_L;
     if (q & WPAD_CLASSIC_BUTTON_FULL_R) p |= PAD_TRIGGER_R;
     if (q & WPAD_CLASSIC_BUTTON_X)      p |= PAD_BUTTON_X;
     if (q & WPAD_CLASSIC_BUTTON_A)      p |= PAD_BUTTON_A;
     if (q & WPAD_CLASSIC_BUTTON_B)      p |= PAD_BUTTON_B;
     if (q & WPAD_CLASSIC_BUTTON_HOME)   p |= PAD_TRIGGER_Z;
#endif
  return p;
}


#ifdef HW_RVL
static unsigned int
DecodeJoyUSBXInput (u32 p)
{
  unsigned int J = 0;
  s16 x = usb_xinput_lx();
  s16 y = usb_xinput_ly();

  /* Fixed directions: D-pad and left analog stick. */
  if (p & USBX_DPAD_UP) J |= P1UP;
  if (p & USBX_DPAD_DOWN) J |= P1DOWN;
  if (p & USBX_DPAD_LEFT) J |= P1LEFT;
  if (p & USBX_DPAD_RIGHT) J |= P1RIGHT;

  /*
   * Larger dead zone than the first USB build.  The Ultimate Wired
   * exposes a full 16-bit stick range, so 22000 still leaves ample travel
   * while avoiding accidental digital direction changes near center.
   */
  if (x > 20000) J |= P1RIGHT;
  else if (x < -20000) J |= P1LEFT;
  if (y > 20000) J |= P1UP;
  else if (y < -20000) J |= P1DOWN;

  if (button_match(p, mapped_mask(INPUT_DEV_USB_XINPUT, INPUT_MAP_A))) J |= P1A;
  if (button_match(p, mapped_mask(INPUT_DEV_USB_XINPUT, INPUT_MAP_B))) J |= P1B;
  if (button_match(p, mapped_mask(INPUT_DEV_USB_XINPUT, INPUT_MAP_C))) J |= P1C;
  if (button_match(p, mapped_mask(INPUT_DEV_USB_XINPUT, INPUT_MAP_D))) J |= P1D;

  return J;
}

static unsigned int
startsel_usbxinput (u32 p)
{
  int J = 0;

  if (button_match(p, mapped_mask(INPUT_DEV_USB_XINPUT, INPUT_MAP_START)))
    J |= 1;

  if (button_match(p, mapped_mask(INPUT_DEV_USB_XINPUT, INPUT_MAP_SELECT)))
    J |= 2;

  return J;
}

/****************************************************************************
 * DecodeJoy WII
 ****************************************************************************/
unsigned int
DecodeJoyWii (int chan, u32 p, int device)
{
  unsigned int J = 0;

  if (device == INPUT_DEV_CLASSIC)
  {
    if (p & WPAD_CLASSIC_BUTTON_UP)    J |= P1UP;
    if (p & WPAD_CLASSIC_BUTTON_DOWN)  J |= P1DOWN;
    if (p & WPAD_CLASSIC_BUTTON_LEFT)  J |= P1LEFT;
    if (p & WPAD_CLASSIC_BUTTON_RIGHT) J |= P1RIGHT;
  }
  else if (device == INPUT_DEV_WIIMOTE)
  {
    /* Bare Wiimote is held sideways in the original NeoCDRX mapping. */
    if (p & WPAD_BUTTON_RIGHT) J |= P1UP;
    if (p & WPAD_BUTTON_LEFT)  J |= P1DOWN;
    if (p & WPAD_BUTTON_UP)    J |= P1LEFT;
    if (p & WPAD_BUTTON_DOWN)  J |= P1RIGHT;
  }
  else
  {
    if (p & WPAD_BUTTON_UP)    J |= P1UP;
    if (p & WPAD_BUTTON_DOWN)  J |= P1DOWN;
    if (p & WPAD_BUTTON_LEFT)  J |= P1LEFT;
    if (p & WPAD_BUTTON_RIGHT) J |= P1RIGHT;
  }

  if (button_match(p, mapped_mask(device, INPUT_MAP_A))) J |= P1A;
  if (button_match(p, mapped_mask(device, INPUT_MAP_B))) J |= P1B;
  if (button_match(p, mapped_mask(device, INPUT_MAP_C))) J |= P1C;
  if (button_match(p, mapped_mask(device, INPUT_MAP_D))) J |= P1D;

  (void) chan;
  return J;
}
#endif

/****************************************************************************
 * DecodeJoy
 ****************************************************************************/
static unsigned int
DecodeJoy (u32 p)
{
  unsigned int J = 0;

  if (p & PAD_BUTTON_UP)    J |= P1UP;
  if (p & PAD_BUTTON_DOWN)  J |= P1DOWN;
  if (p & PAD_BUTTON_LEFT)  J |= P1LEFT;
  if (p & PAD_BUTTON_RIGHT) J |= P1RIGHT;

  if (button_match(p, mapped_mask(INPUT_DEV_GC, INPUT_MAP_A))) J |= P1A;
  if (button_match(p, mapped_mask(INPUT_DEV_GC, INPUT_MAP_B))) J |= P1B;
  if (button_match(p, mapped_mask(INPUT_DEV_GC, INPUT_MAP_C))) J |= P1C;
  if (button_match(p, mapped_mask(INPUT_DEV_GC, INPUT_MAP_D))) J |= P1D;

  return J;
}

/****************************************************************************
 * GetAnalog
 ****************************************************************************/
static unsigned int
GetAnalog (int Joy)
{
  float t;
  unsigned int i = 0;
  s8 x = PAD_StickX (Joy);
  s8 y = PAD_StickY (Joy);

#ifdef HW_RVL
  x +=WPAD_StickX(Joy,0);
  y +=WPAD_StickY(Joy,0);
#endif

  if ((x * x + y * y) > (padcal * padcal))
    {
      if (x > 0 && y == 0)
	i |= P1RIGHT;
      if (x < 0 && y == 0)
	i |= P1LEFT;
      if (x == 0 && y > 0)
	i |= P1UP;
      if (x == 0 && y < 0)
	i |= P1DOWN;

      if (x != 0 && y != 0)
	{

      /*** Recalc left / right ***/
	t = (float) y / x;
	if (t >= -2.41421356237 && t < 2.41421356237)
	  {
		if (x >= 0)
	i |= P1RIGHT;
		else
	i |= P1LEFT;
	  }

	/*** Recalc up / down ***/
	t = (float) x / y;
	if (t >= -2.41421356237 && t < 2.41421356237)
	  {
		if (y >= 0)
	i |= P1UP;
		else
	i |= P1DOWN;
	  }
	}
	}

  return i;
}

/****************************************************************************
 * StartSel
 ****************************************************************************/
static unsigned int
startsel_gc (u32 p)
{
  int J = 0;

  if (button_match(p, mapped_mask(INPUT_DEV_GC, INPUT_MAP_START)))
    J |= 1;

  if (button_match(p, mapped_mask(INPUT_DEV_GC, INPUT_MAP_SELECT)))
    J |= 2;

  return J;
}

#ifdef HW_RVL
static unsigned int
startsel_wii (u32 p, int device)
{
  int J = 0;

  if (button_match(p, mapped_mask(device, INPUT_MAP_START)))
    J |= 1;

  if (button_match(p, mapped_mask(device, INPUT_MAP_SELECT)))
    J |= 2;

  return J;
}
#endif

/****************************************************************************
 * Player claim system
 *
 * No device owns P1/P2 at startup.  The first physical controller that
 * generates a meaningful button/D-Pad input claims P1; the next different
 * controller claims P2.  Only one USB XInput device is supported at a time;\n * P2 may use GC/WPAD/Classic. Once claimed, ownership remains stable through
 * Game Over / Continue.  A disconnected device releases only its own slot.
 ****************************************************************************/

#define CLAIM_NONE      (-1)
#define CLAIM_GC_BASE    0
#define CLAIM_WPAD_BASE  4
#define CLAIM_USB_BASE   8

static int player_claim[2] = { CLAIM_NONE, CLAIM_NONE };

static int
claim_is_usb(int claim)
{
  return (claim >= CLAIM_USB_BASE &&
          claim < (CLAIM_USB_BASE + USBX_MAX_PADS));
}

static int
claim_is_wpad(int claim)
{
  return (claim >= CLAIM_WPAD_BASE && claim < CLAIM_WPAD_BASE + 4);
}

static int
claim_is_gc(int claim)
{
  return (claim >= CLAIM_GC_BASE && claim < CLAIM_GC_BASE + 4);
}

static int
claim_channel(int claim)
{
  if (claim_is_usb(claim)) return claim - CLAIM_USB_BASE;
  if (claim_is_wpad(claim)) return claim - CLAIM_WPAD_BASE;
  if (claim_is_gc(claim)) return claim - CLAIM_GC_BASE;
  return -1;
}

static int
wpad_device_type(int chan)
{
  u32 exp;

  if (WPAD_Probe(chan, &exp) != WPAD_ERR_NONE)
    return -1;

  if (exp == WPAD_EXP_NUNCHUK)
    return INPUT_DEV_NUNCHUK;
  if (exp == WPAD_EXP_CLASSIC)
    return INPUT_DEV_CLASSIC;

  return INPUT_DEV_WIIMOTE;
}

static int
claim_connected(int claim)
{
  int chan;

  if (claim == CLAIM_NONE)
    return 0;

  chan = claim_channel(claim);

  if (claim_is_usb(claim))
    return usb_xinput_connected_slot_compat(chan);

  if (claim_is_wpad(claim))
    return wpad_device_type(chan) >= 0;

  if (claim_is_gc(claim))
  {
    /*
     * Older libogc used by this project does not export the GameCube PAD probe helper.
     * Once a GameCube controller claims a player slot, ownership is kept
     * for the current game session.
     */
    return 1;
  }

  return 0;
}

static void
claim_release_disconnected(void)
{
  int p;

  for (p = 0; p < 2; p++)
    if (player_claim[p] != CLAIM_NONE && !claim_connected(player_claim[p]))
      player_claim[p] = CLAIM_NONE;
}

static int
claim_already_used(int claim)
{
  return player_claim[0] == claim || player_claim[1] == claim;
}

static void
claim_try_device(int claim, int start_down)
{
  if (!start_down || claim_already_used(claim))
    return;

  if (player_claim[0] == CLAIM_NONE)
    player_claim[0] = claim;
  else if (player_claim[1] == CLAIM_NONE)
    player_claim[1] = claim;
}

static void
claim_scan_activity(u32 gc_down[4], u32 wpad_down[4],
                    int wpad_type[4], u32 usb_down[USBX_MAX_PADS])
{
  int i;

  /*
   * First meaningful input claims a free player slot.
   * This includes D-Pad and any button, so the user is never forced to
   * press Start before being able to navigate or play.
   *
   * Once a device has claimed P1/P2, later inputs do not change ownership.
   */
  for (i = 0; i < USBX_MAX_PADS; i++)
  {
    if (usb_xinput_connected_slot_compat(i) && usb_down[i])
      claim_try_device(CLAIM_USB_BASE + i, 1);
  }

  for (i = 0; i < 4; i++)
  {
    if (wpad_type[i] >= 0 && wpad_down[i])
      claim_try_device(CLAIM_WPAD_BASE + i, 1);
  }

  for (i = 0; i < 4; i++)
  {
    if (gc_down[i])
      claim_try_device(CLAIM_GC_BASE + i, 1);
  }
}

static void
input_global_menu_check(u32 gc_held[4], u32 wpad_held[4],
                        int wpad_type[4], u32 usb_held[USBX_MAX_PADS])
{
  int i;

  if (menu_release_latch)
    return;

  for (i = 0; i < USBX_MAX_PADS; i++)
    if (usb_xinput_connected_slot_compat(i) &&
        button_match(usb_held[i],
                     mapped_mask(INPUT_DEV_USB_XINPUT, INPUT_MAP_MENU)))
    {
      neogeo_new_game();
      return;
    }

  for (i = 0; i < 4; i++)
    if (wpad_type[i] >= 0 &&
        button_match(wpad_held[i],
                     mapped_mask(wpad_type[i], INPUT_MAP_MENU)))
    {
      neogeo_new_game();
      return;
    }

  for (i = 0; i < 4; i++)
    if (button_match(gc_held[i],
                     mapped_mask(INPUT_DEV_GC, INPUT_MAP_MENU)))
    {
      neogeo_new_game();
      return;
    }
}

static unsigned int
decode_claimed_player(int player, int claim,
                      u32 gc_held[4], u32 wpad_held[4],
                      int wpad_type[4], u32 usb_held[USBX_MAX_PADS])
{
  int chan;
  u32 p;
  unsigned int joy = 0;
  unsigned int ss = 0;

  if (claim == CLAIM_NONE)
    return 0;

  chan = claim_channel(claim);

  if (claim_is_usb(claim))
  {
    p = usb_held[chan];
    joy = DecodeJoyUSBXInput(p);
    ss = startsel_usbxinput(p);

    if (button_match(p, mapped_mask(INPUT_DEV_USB_XINPUT, INPUT_MAP_SAVE)) &&
        mcard_written)
    {
      if (neogeo_set_memorycard())
        mcard_written = 0;
    }
  }
  else if (claim_is_wpad(claim))
  {
    p = wpad_held[chan];
    joy = DecodeJoyWii(chan, p, wpad_type[chan]);
    ss = startsel_wii(p, wpad_type[chan]);

    if (button_match(p, mapped_mask(wpad_type[chan], INPUT_MAP_SAVE)) &&
        mcard_written)
    {
      if (neogeo_set_memorycard())
        mcard_written = 0;
    }
  }
  else if (claim_is_gc(claim))
  {
    p = gc_held[chan];
    joy = DecodeJoy(p);
    joy |= GetAnalog(chan);
    ss = startsel_gc(p);

    if (button_match(p, mapped_mask(INPUT_DEV_GC, INPUT_MAP_SAVE)) &&
        mcard_written)
    {
      if (neogeo_set_memorycard())
        mcard_written = 0;
    }
  }

  if (player == 0)
  {
    joy |= (ss << 16);
    return joy;
  }

  joy <<= 8;
  joy |= (ss << 18);
  return joy;
}

/****************************************************************************
 * update_input
 ****************************************************************************/
void
update_input (void)
{
  u32 gc_held[4] = { 0, 0, 0, 0 };
  u32 gc_down[4] = { 0, 0, 0, 0 };
#ifdef HW_RVL
  u32 wpad_held[4] = { 0, 0, 0, 0 };
  u32 wpad_down[4] = { 0, 0, 0, 0 };
  int wpad_type[4] = { -1, -1, -1, -1 };
  u32 usb_held[USBX_MAX_PADS] = { 0, 0 };
  u32 usb_down[USBX_MAX_PADS] = { 0, 0 };
#else
  u32 wpad_held[4] = { 0, 0, 0, 0 };
  int wpad_type[4] = { -1, -1, -1, -1 };
  u32 usb_held[2] = { 0, 0 };
#endif
  int i;

  if (!accept_input)
    return;

  PAD_ScanPads();

  for (i = 0; i < 4; i++)
  {
    gc_held[i] = gc_add_cstick(i, PAD_ButtonsHeld(i));
    gc_down[i] = PAD_ButtonsDown(i);
  }

#ifdef HW_RVL
  usb_xinput_init();
  WPAD_ScanPads();

  for (i = 0; i < 4; i++)
  {
    wpad_type[i] = wpad_device_type(i);
    if (wpad_type[i] >= 0)
    {
      wpad_held[i] = WPAD_ButtonsHeld(i);
      wpad_down[i] = WPAD_ButtonsDown(i);
    }
  }

  for (i = 0; i < USBX_MAX_PADS; i++)
  {
    if (usb_xinput_connected_slot_compat(i))
    {
      usb_held[i] = usb_xinput_buttons_held_slot_compat(i);
      usb_down[i] = usb_xinput_buttons_down_slot_compat(i);
    }
  }

  claim_release_disconnected();
  claim_scan_activity(gc_down, wpad_down, wpad_type, usb_down);

  /*
   * Emulator Menu remains global: any connected controller may open it,
   * even if that controller has not claimed P1 or P2.
   */
  input_global_menu_check(gc_held, wpad_held, wpad_type, usb_held);

  keys = 0;
  keys |= decode_claimed_player(0, player_claim[0],
                                gc_held, wpad_held, wpad_type, usb_held);
  keys |= decode_claimed_player(1, player_claim[1],
                                gc_held, wpad_held, wpad_type, usb_held);
#else
  /*
   * GameCube build keeps the traditional fixed channels.
   * The dynamic claim layer is a Wii-side feature.
   */
  keys = DecodeJoy(gc_held[0]) | GetAnalog(0);
  keys |= (startsel_gc(gc_held[0]) << 16);
  keys |= ((DecodeJoy(gc_held[1]) | GetAnalog(1)) << 8);
  keys |= (startsel_gc(gc_held[1]) << 18);
#endif

  /*
   * Re-arm Menu only after a complete physical release.
   */
  if (menu_release_latch && !input_any_menu_held())
    menu_release_latch = 0;
}

/*--------------------------------------------------------------------------*/
unsigned char
read_player1 (void)
{
  return ~keys & 0xff;
}

/*--------------------------------------------------------------------------*/
unsigned char
read_player2 (void)
{
  return ~(keys >> 8) & 0xff;
}

/*--------------------------------------------------------------------------*/
unsigned char
read_pl12_startsel (void)
{
  return ~(keys >> 16) & 0x0f;
}
