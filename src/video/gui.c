/****************************************************************************
* NeoCDRX
*
* GUI File Selector
****************************************************************************/

#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
#include "neocdrx.h"
#include "backdrop.h"
#include "banner.h"
#include "cue.h"

#define MENU_HILITE 0x8960899B
#define CDPLAYER_STATUS_HILITE MENU_HILITE

#ifdef HW_RVL
#include <wiiuse/wpad.h>
#include <di/di.h>
#endif

/*** GC 2D Video ***/
extern unsigned int *xfb[2];
extern int whichfb;
extern GXRModeObj *vmode;

/*** GUI Video: follows the selected TV Mode (240p / 480i). ***/
static GXRModeObj *guivmode = &TVNtsc240Ds;
static int gui_tv_mode = GAME_TVMODE_240P;

static inline int
gui_is_240p (void)
{
  return (gui_tv_mode == GAME_TVMODE_240P);
}

static inline int
gui_y (int y)
{
  return gui_is_240p() ? (y >> 1) : y;
}

static void
gui_select_video_mode (int mode)
{
  if (mode == GAME_TVMODE_480I)
  {
    gui_tv_mode = GAME_TVMODE_480I;
    guivmode = &TVNtsc480IntDf;
  }
  else
  {
    gui_tv_mode = GAME_TVMODE_240P;
    guivmode = &TVNtsc240Ds;
  }
}

static void
gui_apply_video_mode (int mode)
{
  gui_select_video_mode(mode);

  /*
   * Complete VI transition so the GUI can change mode immediately,
   * without restarting the emulator.
   */
  VIDEO_SetBlack(TRUE);
  VIDEO_Flush();
  VIDEO_WaitVSync();

  VIDEO_Configure(guivmode);

  VIDEO_ClearFrameBuffer(guivmode, xfb[0], COLOR_BLACK);
  VIDEO_ClearFrameBuffer(guivmode, xfb[1], COLOR_BLACK);
  VIDEO_SetNextFramebuffer(xfb[whichfb]);

  VIDEO_Flush();
  VIDEO_WaitVSync();
  if (guivmode->viTVMode & VI_NON_INTERLACE)
    VIDEO_WaitVSync();

  VIDEO_SetBlack(FALSE);
  VIDEO_Flush();
}

/*** libOGC Default Font ***/
extern u8 console_font_8x16[];

static u32 fgcolour = COLOR_WHITE;
static u32 bgcolour = COLOR_BLACK;
static unsigned char background[1280 * 480] ATTRIBUTE_ALIGN (32);
static unsigned char bannerunc[banner_WIDTH * banner_HEIGHT * 2] ATTRIBUTE_ALIGN (32);
static void unpack (void);

unsigned short SaveDevice = 1;           // default save location to SD card
int use_SD  = 0;
int use_USB = 0;
int use_IDE = 0;
int use_WKF = 0;
int use_DVD = 0;

int mega = 0;

/*** Persistent user settings ***/
static float audio_opts[8] = {
  1.0f, 1.0f,
  1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f
};

#define SETTINGS_MAGIC   "NEOCDRECFG"
#define SETTINGS_VERSION 4


/****************************************************************************
* plotpixel
****************************************************************************/
static void
plotpixel (int x, int y)
{
  u32 pixel;

  y = gui_y (y);

  pixel = xfb[whichfb][(y * 320) + (x >> 1)];

  if (x & 1)
    xfb[whichfb][(y * 320) + (x >> 1)] =
      (pixel & 0xffff00ff) | (COLOR_WHITE & 0xff00);
  else
    xfb[whichfb][(y * 320) + (x >> 1)] =
      (COLOR_WHITE & 0xffff00ff) | (pixel & 0xff00);
}

/****************************************************************************
* roughcircle
****************************************************************************/
static void
roughcircle (int cx, int cy, int x, int y)
{
  if (x == 0)
    {
      plotpixel (cx, cy + y);			/*** Anti ***/
      plotpixel (cx, cy - y);
      plotpixel (cx + y, cy);
      plotpixel (cx - y, cy);
    }
  else
    {
      if (x == y)
	{
	  plotpixel (cx + x, cy + y);		/*** Anti ***/
	  plotpixel (cx - x, cy + y);
	  plotpixel (cx + x, cy - y);
	  plotpixel (cx - x, cy - y);
	}
  else
	{
	  if (x < y)
		{
		  plotpixel (cx + x, cy + y);	/*** Anti ***/
		  plotpixel (cx - x, cy + y);
		  plotpixel (cx + x, cy - y);
		  plotpixel (cx - x, cy - y);
		  plotpixel (cx + y, cy + x);
		  plotpixel (cx - y, cy + x);
		  plotpixel (cx + y, cy - x);
		  plotpixel (cx - y, cy - x);
		}

	}
    }
}

/****************************************************************************
* circle
****************************************************************************/
void
circle (int cx, int cy, int radius)
{
  int x = 0;
  int y = radius;
  int p = (5 - radius * 4) / 4;

  roughcircle (cx, cy, x, y);

  while (x < y)
    {
      x++;
      if (p < 0)
	p += (x << 1) + 1;
      else
	{
	  y--;
	  p += ((x - y) << 1) + 1;
	}
      roughcircle (cx, cy, x, y);
    }
}

/****************************************************************************
* drawchar
****************************************************************************/
static void
drawchar (int x, int y, char c)
{
  int yy, xx;
  u32 colour[2];
  int offset;
  u8 bits;
  int rows;

  offset = (gui_y(y) * 320) + (x >> 1);
  rows = gui_is_240p() ? 8 : 16;

  for (yy = 0; yy < rows; yy++)
  {
    if (gui_is_240p())
    {
      bits = console_font_8x16[((c << 4) + (yy << 1))] |
             console_font_8x16[((c << 4) + (yy << 1)) + 1];
    }
    else
    {
      bits = console_font_8x16[(c << 4) + yy];
    }

    for (xx = 0; xx < 4; xx++)
    {
      colour[0] = (bits & 0x80) ? fgcolour : bgcolour;
      colour[1] = (bits & 0x40) ? fgcolour : bgcolour;

      xfb[whichfb][offset + xx] =
        (colour[0] & 0xffff00ff) | (colour[1] & 0xff00);

      bits <<= 2;
    }

    offset += 320;
  }
}

/****************************************************************************
* drawcharcredits
*
* Readable 8x10 Credits font for 240p.
*
* The original 8x16 font is compressed vertically to 10 rows using contiguous
* source-row groups. Horizontal width remains the native 8 pixels.
****************************************************************************/
static void
drawcharcredits (int x, int y, char c)
{
  static const u8 firstrow[10] =
    { 0, 1, 3, 4, 6, 8, 9, 11, 12, 14 };
  static const u8 lastrow[10] =
    { 0, 2, 3, 5, 7, 8, 10, 11, 13, 15 };

  int yy, xx, sy;
  u32 colour[2];
  int offset;
  u8 bits;

  offset = (gui_y (y) * 320) + (x >> 1);

  for (yy = 0; yy < 10; yy++)
  {
    bits = 0;

    for (sy = firstrow[yy]; sy <= lastrow[yy]; sy++)
      bits |= console_font_8x16[(c << 4) + sy];

    for (xx = 0; xx < 4; xx++)
    {
      colour[0] = (bits & 0x80) ? fgcolour : bgcolour;
      colour[1] = (bits & 0x40) ? fgcolour : bgcolour;

      xfb[whichfb][offset + xx] =
        (colour[0] & 0xffff00ff) | (colour[1] & 0xff00);

      bits <<= 2;
    }

    offset += 320;
  }
}

/****************************************************************************
* drawcharw
****************************************************************************/
static void
drawcharw (int x, int y, char c)
{
  int sy, rep, xx;
  int offset;
  int bits;
  int vertical_scale;

  offset = (gui_y(y) * 320) + (x >> 1);
  vertical_scale = gui_is_240p() ? 1 : 2;

  for (sy = 0; sy < 16; sy++)
  {
    bits = console_font_8x16[(c << 4) + sy];

    for (rep = 0; rep < vertical_scale; rep++)
    {
      int rowbits = bits;

      for (xx = 0; xx < 8; xx++)
      {
        if (rowbits & 0x80)
          xfb[whichfb][offset + xx] = fgcolour;
        else
          xfb[whichfb][offset + xx] = bgcolour;

        rowbits <<= 1;
      }

      offset += 320;
    }
  }
}

/****************************************************************************
* drawcharw_transparent
*
* Same 16x16 240p menu font as TXT_DOUBLE, but background pixels are not
* written. This lets the backdrop remain visible around and through the text.
****************************************************************************/
static void
drawcharw_transparent (int x, int y, char c)
{
  int sy, rep, xx;
  int offset;
  int bits;
  int vertical_scale;

  offset = (gui_y(y) * 320) + (x >> 1);
  vertical_scale = gui_is_240p() ? 1 : 2;

  for (sy = 0; sy < 16; sy++)
  {
    bits = console_font_8x16[(c << 4) + sy];

    for (rep = 0; rep < vertical_scale; rep++)
    {
      int rowbits = bits;

      for (xx = 0; xx < 8; xx++)
      {
        if (rowbits & 0x80)
          xfb[whichfb][offset + xx] = fgcolour;

        rowbits <<= 1;
      }

      offset += 320;
    }
  }
}


static void
drawchar_medium_transparent (int x, int y, char c)
{
  int sy, rep, xx;
  int offset;
  int bits;
  int vertical_scale;

  /*
   * Intermediate-size transparent text:
   * same 8-pixel width as normal text, but taller.
   */
  vertical_scale = gui_is_240p() ? 2 : 4;
  offset = (gui_y(y) * 320) + (x >> 1);

  for (sy = 0; sy < 8; sy++)
  {
    bits = console_font_8x16[((c << 4) + (sy << 1))] |
           console_font_8x16[((c << 4) + (sy << 1)) + 1];

    for (rep = 0; rep < vertical_scale; rep++)
    {
      int rowbits = bits;

      for (xx = 0; xx < 4; xx++)
      {
        int pos = offset + xx;

        if (rowbits & 0x80)
        {
          u32 old = xfb[whichfb][pos];
          xfb[whichfb][pos] =
            (fgcolour & 0xffff00ff) | (old & 0x0000ff00);
        }

        if (rowbits & 0x40)
        {
          u32 old = xfb[whichfb][pos];
          xfb[whichfb][pos] =
            (old & 0xffff00ff) | (fgcolour & 0x0000ff00);
        }

        rowbits <<= 2;
      }

      offset += 320;
    }
  }
}


static int
gui_text_width(const char *text, int mode)
{
  int n = strlen(text);

  if (mode == TXT_DOUBLE || mode == TXT_DOUBLE_TRANSPARENT)
    return n * 16;

  /* mode 0 and medium-transparent mode 4 both advance 8 pixels per char */
  return n * 8;
}

static int
gui_center_x(const char *text, int mode)
{
  return (640 - gui_text_width(text, mode)) >> 1;
}

/****************************************************************************
* gprint
****************************************************************************/
void
gprint (int x, int y, char *text, int mode)
{
  int n;
  int i;

  n = strlen (text);
  if (!n)
    return;

  if (mode == TXT_DOUBLE)
    {
      for (i = 0; i < n; i++, x += 16)
	  drawcharw (x, y, text[i]);
    }
  else if (mode == TXT_DOUBLE_TRANSPARENT)
    {
      for (i = 0; i < n; i++, x += 16)
	  drawcharw_transparent (x, y, text[i]);
    }
  else if (mode == 4)
    {
      for (i = 0; i < n; i++, x += 8)
        drawchar_medium_transparent (x, y, text[i]);
    }
  else if (mode == TXT_CREDITS_TALL)
    {
      for (i = 0; i < n; i++, x += 10)
	  drawcharcredits (x, y, text[i]);
    }
  else
    {
      for (i = 0; i < n; i++, x += 8)
	  drawchar (x, y, text[i]);
    }
}

/****************************************************************************
* DrawScreen
****************************************************************************/
void
DrawScreen (void)
{
  static int inited = 0;

  if (!inited)
    {
      unpack ();
      inited = 1;
    }

  int y;

  VIDEO_WaitVSync ();

  whichfb ^= 1;

  /*
   * Backdrop is stored as 640x480 YUY2.
   * 240p keeps the accepted every-second-line path.
   * 480i copies the complete 480-line artwork.
   */
  if (gui_is_240p())
  {
    for (y = 0; y < 240; y++)
      memcpy(((u8 *)xfb[whichfb]) + (y * 1280),
             background + ((y << 1) * 1280),
             1280);
  }
  else
  {
    memcpy((u8 *)xfb[whichfb], background, 1280 * 480);
  }
}

/****************************************************************************
* ShowScreen
****************************************************************************/
void
ShowScreen (void)
{
  VIDEO_SetNextFramebuffer (xfb[whichfb]);
  VIDEO_Flush ();
  VIDEO_WaitVSync ();
}

/****************************************************************************
* setfgcolour
****************************************************************************/
void
setfgcolour (u32 colour)
{
  fgcolour = colour;
}

/****************************************************************************
* setbgcolour
****************************************************************************/
void
setbgcolour (u32 colour)
{
  bgcolour = colour;
}

/****************************************************************************
* WaitButtonA
****************************************************************************/
void
WaitButtonA (void)
{
  short joy;
  joy = getMenuButtons();

  while (!(joy & PAD_BUTTON_A)) joy = getMenuButtons();
    VIDEO_WaitVSync ();

  while (joy & PAD_BUTTON_A) joy = getMenuButtons();
    VIDEO_WaitVSync ();
}

/****************************************************************************
* ActionScreen
****************************************************************************/
void
ActionScreen (char *msg)
{
  int n;
  char pressa[] = "Press A to continue";

  DrawScreen ();

  n = strlen (msg);
  fgcolour = COLOR_WHITE;
  bgcolour = BMPANE;

  gprint ((640 - (n * 16)) >> 1, 248, msg, TXT_DOUBLE);

  gprint (168, 288, pressa, TXT_DOUBLE);

  ShowScreen ();

  WaitButtonA ();
}

/****************************************************************************
* InfoScreen
****************************************************************************/
void
InfoScreen (char *msg)
{
  int n;

  DrawScreen ();

  n = strlen (msg);
  fgcolour = COLOR_WHITE;
  bgcolour = BMPANE;

  gprint ((640 - (n * 16)) >> 1, 264, msg, TXT_DOUBLE);

  ShowScreen ();
}

/****************************************************************************
* Credits
****************************************************************************/
int credits()
{
int quit = 0;
int ret = 0;
short joy;

char Title[]   = "CREDITS";
char Intro1[]  = "This fork would not have been possible without the";
char Intro2[]  = "developers who dedicated their time and effort to:";
char Softdev[] = "Softdev and his Neo-CD Redux (GCN) (2007)";
char Coders1[] = "Wiimpathy / Jacobeian for NeoCD-Wii (2011)";
char Coders2[] = "infact for Neo-CD Redux (2011)";
char Coders3[] = "megalomaniac - Neo-CD Redux Unofficial (2013-2016)";
char Niuus[]   = "NiuuS - NeoCD-RX (2023)";
char Fun[]     = "Let's keep it going. Wii still lives!";
char iosVersion[20];
char appVersion[24]= "Neo.CD Respin 1.2.2";

#ifdef HW_RVL
	sprintf(iosVersion, "IOS : %d", IOS_GetVersion());
#endif

  DrawScreen ();
  
  fgcolour = COLOR_BLACK;
  bgcolour = BMPANE;

  /* Keep all credit text inside the central backdrop frame. */
  gprint (250, 160, Title, TXT_DOUBLE_TRANSPARENT);
  gprint (60, 198, Intro1, TXT_CREDITS_TALL);
  gprint (60, 218, Intro2, TXT_CREDITS_TALL);
  gprint (60, 252, Softdev, TXT_CREDITS_TALL);
  gprint (60, 274, Coders1, TXT_CREDITS_TALL);
  gprint (60, 296, Coders2, TXT_CREDITS_TALL);
  gprint (60, 318, Coders3, TXT_CREDITS_TALL);
  gprint (60, 340, Niuus, TXT_CREDITS_TALL);
  gprint (60, 366, Fun, TXT_CREDITS_TALL);
  gprint (510, 398, iosVersion, 0);
  gprint (60, 398, appVersion, 0);

  ShowScreen ();

  while (quit == 0)
  {
    joy = getMenuButtons();

    if (joy & PAD_BUTTON_A)
    { 
      quit = 1;
      ret = -1;
    }

    if (joy & PAD_BUTTON_B)
    {
      quit = 1;
      ret = -1;
    }

    if (have_ROM && input_menu_button_down())
    {
      input_arm_menu_release_latch();
      quit = 1;
      ret = 1;
    }
  }
  return ret;
}

/****************************************************************************
* unpack
****************************************************************************/
static void
unpack (void)
{
  unsigned long inbytes, outbytes;

  inbytes = backdrop_COMPRESSED;
  outbytes = backdrop_RAW;

  uncompress (background, &outbytes, backdrop, inbytes);

  inbytes = banner_COMPRESSED;
  outbytes = banner_RAW;

  uncompress (bannerunc, &outbytes, banner, inbytes);
}

/****************************************************************************
* LoadingScreen
****************************************************************************/
void
LoadingScreen (char *msg)
{
  int n;

  VIDEO_WaitVSync ();

  whichfb ^= 1;

  n = strlen (msg);
  fgcolour = COLOR_WHITE;
  bgcolour = COLOR_BLACK;

  gprint ((640 - (n * 16)) >> 1, 410, msg, TXT_DOUBLE);

  ShowScreen ();
}

/****************************************************************************
* draw_menu
****************************************************************************/
char menutitle[60] = { "" };
int menu = 0;
static int menu_y_start = 205;

static void draw_menu(char items[][22], int maxitems, int selected)//(  int currsel )
{
   int i;
   int j;

   /*
    * Original NeoCDRX menu typography and spacing.
    * Only the vertical origin is shifted slightly upward.
    */
   j = menu_y_start;

   int n;
   char msg[] = "";
   n = strlen (msg);


   DrawScreen ();
   
   for( i = 0; i < maxitems; i++ )
   {
      if ( i == selected )
      {
         /* Selected item is indicated by text colour only; no background bar. */
         setfgcolour (MENU_HILITE);
         gprint( ( 640 - ( strlen(items[i]) << 4 )) >> 1, j, items[i], TXT_DOUBLE_TRANSPARENT);
      }
      else
      {
         setfgcolour (COLOR_BLACK);
         gprint( ( 640 - ( strlen(items[i]) << 4 )) >> 1, j, items[i], TXT_DOUBLE_TRANSPARENT);
      }
      j += 32;
   }
   
   if (mega == 0) {
      setfgcolour (COLOR_WHITE);
      setbgcolour (BMPANE);//COLOR_BLACK);
      gprint ((640 - (n * 16)) >> 1, 162/*432*/, msg, TXT_DOUBLE);
   }

   ShowScreen();
}

/****************************************************************************
* do_menu
****************************************************************************/
int DoMenu (char items[][22], int maxitems)
{
  int redraw = 1;
  int quit = 0;
  int ret = 0;
  short joy;

  while (quit == 0)
  {
    if (redraw)
    {
      draw_menu (&items[0], maxitems, menu);
      redraw = 0;
    }


    joy = getMenuButtons();

    if (joy & PAD_BUTTON_UP)
    {
      redraw = 1;
      menu--;
      if (menu < 0) menu = maxitems - 1;
    }

    if (joy & PAD_BUTTON_DOWN)
    {
      redraw = 1;
      menu++;
      if (menu == maxitems) menu = 0;
    }

    if (joy & PAD_BUTTON_A)
    {
      quit = 1;
      ret = menu;
    }

    if (joy & PAD_BUTTON_B)
    {
      quit = 1;
      ret = -1;
    }

    if (have_ROM && input_menu_button_down())
    {
      input_arm_menu_release_latch();
      quit = 1;
      ret = -2;
    }
  }
  return ret;
}

/****************************************************************************
* Save_menu - a friendly way to allow user to select location for RXsave.bin
****************************************************************************/
int ChooseMemCard (void)
{
  char titles[5][25] = { {"RXsave.bin not found\0"}, {"choose a save location\0"}, {"\0"}, {"A - SD Gecko   \0"}, {"B - Memory Card\0"} };
  int i;
  int quit = 0;
  short joy;
  
   DrawScreen ();

   fgcolour = COLOR_WHITE;
   bgcolour = BMPANE;

   for (i = 0; i < 5; i++) gprint ((640 - (strlen (titles[i]) * 16)) >> 1, 192 + (i * 32), titles[i], TXT_DOUBLE);

   ShowScreen ();

   while (!quit)
   {
      joy = getMenuButtons();
      if (joy & PAD_BUTTON_A)
        SaveDevice = 1;
        quit = 1;

      if (joy & PAD_BUTTON_B)
        SaveDevice = 0;
        quit = 1;
   }

  return 1;
}

/****************************************************************************
* Audio menu
****************************************************************************/

static void
audio_make_bar(char *bar, float value)
{
  int step;
  int i;
  int p = 0;

  step = (int)((value - 0.5f) * 10.0f + 0.5f);
  if (step < 0) step = 0;
  if (step > 10) step = 10;

  bar[p++] = '|';

  for (i = 0; i < 6; i++)
    bar[p++] = (i <= step) ? '#' : '.';

  bar[p++] = '|';

  for (i = 6; i < 11; i++)
    bar[p++] = (i <= step) ? '#' : '.';

  bar[p++] = '|';
  bar[p] = 0;
}

static void
draw_audio_menu(int selected, int first)
{
  static const char *labels[9] =
  {
    "SFX Volume",
    "Music Volume",
    "SFX Low",
    "SFX Mid",
    "SFX High",
    "Music Low",
    "Music Mid",
    "Music High",
    "Go Back"
  };

  int row;
  int index;
  int y;
  char bar[16];
  char value[8];

  DrawScreen();

  y = 205;

  for (row = 0; row < 5; row++)
  {
    index = first + row;
    if (index >= 9)
      break;

    if (index == selected)
      setfgcolour(MENU_HILITE);
    else
      setfgcolour(COLOR_BLACK);

    if (index == 8)
    {
      gprint((640 - (strlen(labels[index]) << 4)) >> 1,
             y, (char *)labels[index], TXT_DOUBLE_TRANSPARENT);
    }
    else
    {
      audio_make_bar(bar, audio_opts[index]);
      sprintf(value, "%1.1f", audio_opts[index]);

      gprint(72,  y, (char *)labels[index], TXT_DOUBLE_TRANSPARENT);
      gprint(288, y, bar,                  TXT_DOUBLE_TRANSPARENT);
      gprint(528, y, value,                TXT_DOUBLE_TRANSPARENT);
    }

    y += 32;
  }

  ShowScreen();
}

int audiomenu()
{
  int prevmenu = menu;
  int selected = 0;
  int first = 0;
  int redraw = 1;
  int quit = 0;
  int resume_game = 0;
  short joy;

  while (!quit)
  {
    if (redraw)
    {
      draw_audio_menu(selected, first);
      redraw = 0;
    }

    joy = getMenuButtons();

    if (have_ROM && input_menu_button_down())
    {
      input_arm_menu_release_latch();
      resume_game = 1;
      quit = 1;
      continue;
    }

    if (joy & PAD_BUTTON_UP)
    {
      selected--;
      if (selected < 0)
        selected = 8;

      if (selected < first)
        first = selected;
      else if (selected >= first + 5)
        first = selected - 4;

      if (first < 0) first = 0;
      if (first > 4) first = 4;

      redraw = 1;
    }

    if (joy & PAD_BUTTON_DOWN)
    {
      selected++;
      if (selected > 8)
        selected = 0;

      if (selected >= first + 5)
        first = selected - 4;
      else if (selected < first)
        first = selected;

      if (first < 0) first = 0;
      if (first > 4) first = 4;

      redraw = 1;
    }

    if ((joy & PAD_BUTTON_LEFT) && selected < 8)
    {
      audio_opts[selected] -= 0.1f;
      if (audio_opts[selected] < 0.5f)
        audio_opts[selected] = 0.5f;
      redraw = 1;
    }

    if ((joy & PAD_BUTTON_RIGHT) && selected < 8)
    {
      audio_opts[selected] += 0.1f;
      if (audio_opts[selected] > 1.5f)
        audio_opts[selected] = 1.5f;
      redraw = 1;
    }

    if (joy & PAD_BUTTON_A)
    {
      if (selected == 8)
      {
        quit = 1;
      }
      else
      {
        audio_opts[selected] += 0.1f;
        if (audio_opts[selected] > 1.5f)
          audio_opts[selected] = 0.5f;
        redraw = 1;
      }
    }

    if (joy & PAD_BUTTON_B)
      quit = 1;
  }

  mixer_set(audio_opts[0], audio_opts[1],
            audio_opts[2], audio_opts[3], audio_opts[4],
            audio_opts[5], audio_opts[6], audio_opts[7]);

  menu = prevmenu;
  return resume_game;
}

/****************************************************************************
* Persistent settings
****************************************************************************/

static FILE *
settings_open_read (void)
{
  FILE *fp;

  fp = fopen("sd:/NeoCDRE/neocdre.cfg", "rb");
  if (fp) return fp;

  fp = fopen("sd:/neocdre.cfg", "rb");
  if (fp) return fp;

#ifdef HW_RVL
  fp = fopen("usb:/NeoCDRE/neocdre.cfg", "rb");
  if (fp) return fp;

  fp = fopen("usb:/neocdre.cfg", "rb");
  if (fp) return fp;
#endif

  return NULL;
}

static FILE *
settings_open_write (void)
{
  FILE *fp;

  /*
   * Prefer the NeoCDRE directory on SD.  If it does not exist or SD is
   * unavailable, fall back to the root, then USB.
   */
  fp = fopen("sd:/NeoCDRE/neocdre.cfg", "wb");
  if (fp) return fp;

  fp = fopen("sd:/neocdre.cfg", "wb");
  if (fp) return fp;

#ifdef HW_RVL
  fp = fopen("usb:/NeoCDRE/neocdre.cfg", "wb");
  if (fp) return fp;

  fp = fopen("usb:/neocdre.cfg", "wb");
  if (fp) return fp;
#endif

  return NULL;
}

void
settings_apply_audio (void)
{
  mixer_set(audio_opts[0], audio_opts[1],
            audio_opts[2], audio_opts[3], audio_opts[4],
            audio_opts[5], audio_opts[6], audio_opts[7]);
}

void
settings_load (void)
{
  FILE *fp;
  char magic[16];
  int version;
  int region;
  int save_device;
  int tvmode = GAME_TVMODE_240P;
  int d, a;
  int map_value;

  fp = settings_open_read();
  if (!fp)
    return;

  if (fscanf(fp, "%15s %d", magic, &version) != 2 ||
      strcmp(magic, SETTINGS_MAGIC) != 0 ||
      (version != 1 && version != 2 && version != 3 && version != SETTINGS_VERSION))
  {
    fclose(fp);
    return;
  }

  if (version >= 3)
  {
    if (fscanf(fp, "%d %d %d", &region, &save_device, &tvmode) != 3)
    {
      fclose(fp);
      return;
    }
  }
  else
  {
    if (fscanf(fp, "%d %d", &region, &save_device) != 2)
    {
      fclose(fp);
      return;
    }
  }

  if (region >= 0 && region <= 2)
    neogeo_region = region;

  if (save_device == 0 || save_device == 1)
    SaveDevice = save_device;

  if (tvmode == GAME_TVMODE_240P || tvmode == GAME_TVMODE_480I)
    SetGameTVMode(tvmode);
  else
    SetGameTVMode(GAME_TVMODE_240P);

  gui_select_video_mode(GetGameTVMode());

  if (version >= 4)
  {
    int hsize, hpos, vsize, vpos;
    if (fscanf(fp, "%d %d %d %d", &hsize, &hpos, &vsize, &vpos) != 4)
    {
      fclose(fp);
      return;
    }
    SetScreenGeometry(hsize, hpos, vsize, vpos);
  }
  else
  {
    ResetScreenGeometry();
  }

  if (version == 1)
  {
    float a0, a1, a2, a3, a4;

    if (fscanf(fp, "%f %f %f %f %f",
               &a0, &a1, &a2, &a3, &a4) == 5)
    {
      if (a0 >= 0.0f && a0 <= 2.0f) audio_opts[0] = a0;
      if (a1 >= 0.0f && a1 <= 2.0f) audio_opts[1] = a1;

      if (a2 >= 0.0f && a2 <= 2.0f)
        audio_opts[2] = audio_opts[5] = a2;
      if (a3 >= 0.0f && a3 <= 2.0f)
        audio_opts[3] = audio_opts[6] = a3;
      if (a4 >= 0.0f && a4 <= 2.0f)
        audio_opts[4] = audio_opts[7] = a4;
    }
  }
  else
  {
    float loaded[8];
    int i;

    if (fscanf(fp, "%f %f %f %f %f %f %f %f",
               &loaded[0], &loaded[1], &loaded[2], &loaded[3],
               &loaded[4], &loaded[5], &loaded[6], &loaded[7]) == 8)
    {
      for (i = 0; i < 8; i++)
      {
        if (loaded[i] >= 0.0f && loaded[i] <= 2.0f)
          audio_opts[i] = loaded[i];
      }
    }
  }

  for (d = 0; d < INPUT_DEV_COUNT; d++)
  {
    for (a = 0; a < INPUT_MAP_COUNT; a++)
    {
      if (fscanf(fp, "%d", &map_value) != 1)
      {
        fclose(fp);
        return;
      }

      input_set_mapping_index(d, a, map_value);
    }
  }

  fclose(fp);
}

void
settings_save (void)
{
  FILE *fp;
  int d, a;
  int map_value;

  fp = settings_open_write();
  if (!fp)
    return;

  fprintf(fp, "%s %d\n", SETTINGS_MAGIC, SETTINGS_VERSION);
  fprintf(fp, "%d %d %d\n",
          (int)neogeo_region, (int)SaveDevice, GetGameTVMode());

  {
    int hsize, hpos, vsize, vpos;
    GetScreenGeometry(&hsize, &hpos, &vsize, &vpos);
    fprintf(fp, "%d %d %d %d\n", hsize, hpos, vsize, vpos);
  }

  fprintf(fp, "%.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f\n",
          audio_opts[0], audio_opts[1],
          audio_opts[2], audio_opts[3], audio_opts[4],
          audio_opts[5], audio_opts[6], audio_opts[7]);

  for (d = 0; d < INPUT_DEV_COUNT; d++)
  {
    for (a = 0; a < INPUT_MAP_COUNT; a++)
    {
      map_value = input_get_mapping_index(d, a);

      /*
       * A device not present in this build is stored as -1.
       * Wii builds have all four profiles available.
       */
      fprintf(fp, "%d", map_value);
      if (a < (INPUT_MAP_COUNT - 1))
        fputc(' ', fp);
    }
    fputc('\n', fp);
  }

  fflush(fp);
  fclose(fp);
}

/****************************************************************************
* Controller Mapping
****************************************************************************/

static const char *mapping_action_names[INPUT_MAP_COUNT] =
{
  "Neo A",
  "Neo B",
  "Neo C",
  "Neo D",
  "Start",
  "Select",
  "Emu Menu",
  "Mem Save"
};

static void
controller_mapping_capture_all (int device)
{
  int action;
  char prompt[64];

  for (action = 0; action < INPUT_MAP_COUNT; action++)
  {
    DrawScreen();

    setfgcolour(COLOR_BLACK);
    setbgcolour(BMPANE);

    sprintf(prompt, "Press button for %s", mapping_action_names[action]);
    gprint((640 - (strlen(prompt) * 16)) >> 1, 221,
           prompt, TXT_DOUBLE);

    setfgcolour(COLOR_WHITE);
    setbgcolour(BMPANE);
    gprint(176, 285, "Waiting for input...", TXT_DOUBLE);

    ShowScreen();

    input_capture_mapping(device, action);
  }
}

static void
controller_device_mapping (int device)
{
  int prevmenu = menu;
  int prevmega = mega;
  int quit = 0;
  int ret;
  static char items[3][22] =
  {
    { "Remapping" },
    { "Reset Defaults" },
    { "Go Back" }
  };

  mega = 1;

  /*
   * Entering a controller profile starts a complete mapping pass.
   * Only one action is shown at a time, avoiding the old 10-line list.
   */
  controller_mapping_capture_all(device);

  menu = 0;

  while (!quit)
  {
    ret = DoMenu (&items[0], 3);

    switch (ret)
    {
      case 0:
        controller_mapping_capture_all(device);
        menu = 0;
        break;

      case 1:
        input_reset_mapping(device);
        menu = 0;
        break;

      case -1:
      case 2:
        quit = 1;
        break;
    }
  }

  mega = prevmega;
  menu = prevmenu;
}

static int
controller_mapping_menu (void)
{
  int prevmenu = menu;
  int quit = 0;
  int ret;
  int previous_menu_y_start = menu_y_start;

#ifdef HW_RVL
  int count = 7;
  char items[7][22] =
  {
    { "GameCube Controller" },
    { "Wiimote" },
    { "Wiimote + Nunchuk" },
    { "Classic Controller" },
    { "USB XInput" },
    { "Reset All Defaults" },
    { "Go Back" }
  };
#else
  int count = 3;
  char items[3][22] =
  {
    { "GameCube Controller" },
    { "Reset All Defaults" },
    { "Go Back" }
  };
#endif

  menu = 0;
  menu_y_start = 189;

  while (!quit)
  {
    ret = DoMenu (&items[0], count);

#ifdef HW_RVL
    switch (ret)
    {
      case 0:
        controller_device_mapping(INPUT_DEV_GC);
        break;

      case 1:
        controller_device_mapping(INPUT_DEV_WIIMOTE);
        break;

      case 2:
        controller_device_mapping(INPUT_DEV_NUNCHUK);
        break;

      case 3:
        controller_device_mapping(INPUT_DEV_CLASSIC);
        break;

      case 4:
        controller_device_mapping(INPUT_DEV_USB_XINPUT);
        break;

      case 5:
        input_reset_all_mappings();
        break;

      case -2:
        menu_y_start = previous_menu_y_start;
        menu = prevmenu;
        return 1;

      case -1:
      case 6:
        quit = 1;
        break;
    }
#else
    switch (ret)
    {
      case 0:
        controller_device_mapping(INPUT_DEV_GC);
        break;

      case 1:
        input_reset_all_mappings();
        break;

      case -2:
        menu = prevmenu;
        return 1;

      case -1:
      case 2:
        quit = 1;
        break;
    }
#endif
  }

  menu_y_start = previous_menu_y_start;
  menu = prevmenu;
  return 0;
}

/****************************************************************************
* Screen geometry menu
****************************************************************************/

static int screenmenu(void)
{
  int prevmenu = menu;
  int quit = 0;
  int hsize, hpos, vsize, vpos;
  int count = 6;
  int redraw = 1;
  short joy;
  static char items[6][22] =
  {
    { "Horizontal Size" },
    { "Horizontal Pos" },
    { "Vertical Size" },
    { "Vertical Pos" },
    { "Reset to Default" },
    { "Go Back" }
  };

  menu = 0;

  while (!quit)
  {
    GetScreenGeometry(&hsize, &hpos, &vsize, &vpos);
    sprintf(items[0], "Horizontal Size:%4d", hsize);
    sprintf(items[1], "Horizontal Pos:%+4d", hpos);
    sprintf(items[2], "Vertical Size:  %3d", vsize);
    sprintf(items[3], "Vertical Pos:  %+3d", vpos);

    if (redraw)
    {
      draw_menu(&items[0], count, menu);
      redraw = 0;
    }

    joy = getMenuButtons();

    if (joy & PAD_BUTTON_UP)
    {
      menu--;
      if (menu < 0) menu = count - 1;
      redraw = 1;
    }

    if (joy & PAD_BUTTON_DOWN)
    {
      menu++;
      if (menu == count) menu = 0;
      redraw = 1;
    }

    if (joy & PAD_BUTTON_LEFT)
    {
      switch (menu)
      {
        case 0: hsize -= 2; break;
        case 1: hpos  -= 2; break;
        case 2: vsize -= 1; break;
        case 3: vpos  -= 1; break;
      }
      SetScreenGeometry(hsize, hpos, vsize, vpos);
      redraw = 1;
    }

    if (joy & PAD_BUTTON_RIGHT)
    {
      switch (menu)
      {
        case 0: hsize += 2; break;
        case 1: hpos  += 2; break;
        case 2: vsize += 1; break;
        case 3: vpos  += 1; break;
      }
      SetScreenGeometry(hsize, hpos, vsize, vpos);
      redraw = 1;
    }

    if (joy & PAD_BUTTON_A)
    {
      if (menu == 4)
      {
        ResetScreenGeometry();
        redraw = 1;
      }
      else if (menu == 5)
      {
        quit = 1;
      }
    }

    if (joy & PAD_BUTTON_B)
      quit = 1;

    if (have_ROM && input_menu_button_down())
    {
      input_arm_menu_release_latch();
      settings_save();
      menu = prevmenu;
      return 1;
    }
  }

  settings_save();
  menu = prevmenu;
  return 0;
}

/****************************************************************************
* Hidden debug settings menu
****************************************************************************/

static int debugsettingsmenu(void)
{
  int prevmenu = menu;
  int ret;
  static char items[2][22] =
  {
    { "Screen" },
    { "Go Back" }
  };

  menu = 0;

  for (;;)
  {
    ret = DoMenu(&items[0], 2);

    if (ret == 0)
    {
      if (screenmenu())
      {
        menu = prevmenu;
        return 1;
      }
      menu = 0;
    }
    else
    {
      menu = prevmenu;
      return 0;
    }
  }
}

/****************************************************************************
* Options menu
****************************************************************************/

int optionmenu()
{
  int prevmenu = menu;
  int quit = 0;
  int ret;
  char buf[22];
  int count = 6;
  static char items[6][22] =
  {
    { "Region:           USA" },
    { "Save Device:   SD/USB" },
    { "TV Mode:        240p" },
    { "SFX / Music" },
    { "Controller Mapping" },
    { "Go Back" }
  };

  menu = 0;

  while (quit == 0)
  {
    if (neogeo_region == 0) sprintf(items[0], "Region:         JAPAN");
      else if (neogeo_region == 1) sprintf(items[0], "Region:           USA");
      else sprintf(items[0], "Region:        EUROPE");

    if (SaveDevice == 1) sprintf(items[1], "Save Device:   SD/USB");
    else sprintf(items[1], "Save Device: MEM Card");

    sprintf(items[2], "TV Mode:        %s", GetGameTVModeName());

    ret = DoMenu (&items[0], count);
    switch (ret)
    {
      case 0:
         neogeo_region++;
         if (neogeo_region > 2) neogeo_region = 0;
         break;

      case 1:
         SaveDevice ^= 1;
         break;

      case 2:
      {
         int tvmode;

         if (GetGameTVMode() == GAME_TVMODE_240P)
           tvmode = GAME_TVMODE_480I;
         else
           tvmode = GAME_TVMODE_240P;

         SetGameTVMode(tvmode);
         gui_apply_video_mode(tvmode);
         break;
      }

      case 3:
         if (audiomenu())
         {
           settings_save();
           menu = prevmenu;
           return 1;
         }
         break;

      case 4:
         if (controller_mapping_menu())
         {
           settings_save();
           menu = prevmenu;
           return 1;
         }
         break;

      case -2:
         settings_save();
         menu = prevmenu;
         return 1;

      case -1:
      case 5:
         quit = 1;
         break;
    }
  }

  settings_save();

  menu = prevmenu;
  return 0;
}

/****************************************************************************
 * Load menu
 *
 ****************************************************************************/
static u8 load_menu = 0;

int loadmenu ()
{
  int prevmenu = menu;
  int ret,count;
  int quit = 0;

#ifdef HW_RVL
  count = 6;
  char item[6][22] = {
    {"Load from SD"},
    {"Load from USB"},
    {"Load from IDE-EXI"},
    {"Load from DVD"},
    {"Stop DVD Motor"},
    {"Go Back"}
  };
#else
  count = 6;
  char item[6][22] = {
    {"Load from SD"},
    {"Load from IDE-EXI"},
    {"Load from WKF"},
    {"Load from DVD"},
    {"Stop DVD Motor"},
    {"Go Back"}
  };
#endif

  menu = load_menu;
  
  while (quit == 0)
  {
     use_SD  = 0;
     use_USB = 0;
     use_IDE = 0;
     use_WKF = 0;
     use_DVD = 0;
     ret = DoMenu (&item[0], count);
     switch (ret)
     {
        case -2:               // Emulator-menu button - same action as Play Game
           menu = prevmenu;
           return 2;

        case -1:               // Button B - Exit
        case 5:
           quit = 1;
           break;

#ifdef HW_RVL
        case 1:                // Load from USB
           use_USB = 1;
           InfoScreen((char *) "Mounting media");
           SD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;// quit = 1;
           break;

        case 2:                // Load from IDE-EXI
#else
        case 2:                // Load from WKF
           use_WKF = 1;
           InfoScreen((char *) "Mounting media");
           SD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;// quit = 1;
           break;

        case 1:                // Load from IDE-EXI
#endif
           use_IDE = 1;
           InfoScreen((char *) "Mounting media");
           SD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;// quit = 1;
           break;

        case 3:                // Load from DVD
           use_DVD = 1;
           InfoScreen((char *) "Mounting media");
           DVD_SetHandler();
           GEN_mount();  
           if (have_ROM == 1) return 1;// quit = 1;
           break;

        case 4:                // Stop DVD
           InfoScreen((char *) "Stopping DVD drive...");
           dvd_motor_off();
           break;

        default:               // Load from FAT device
           use_SD  = 1;
           InfoScreen((char *) "Mounting media");
           SD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;
        break;

    }
  }

  menu = prevmenu;
  return 0;
}

/****************************************************************************
 * GUI CD Player - first functional implementation
 *
 * The currently mounted CUE/BIN remains loaded. Gameplay CPUs stay paused.
 * Only CDDA is rendered and fed to the Wii audio DMA.
 ****************************************************************************/
static int
cdplayer_next_audio_track(int current, int direction)
{
  int first = cue_audio_first_track();
  int last = cue_audio_last_track();
  int t;

  if (!first || !last)
    return 0;

  t = current;

  do
  {
    t += direction;

    if (t > last)
      t = first;
    else if (t < first)
      t = last;

    if (cue_audio_track_exists(t))
      return t;
  }
  while (t != current);

  return current;
}

/*
 * Small filled rectangle used only for the selected transport control.
 * No animation and no continuous redraw: this is drawn only when the player
 * screen itself is refreshed after input/state changes.
 */
/*
 * Warm dark beige / muted orange highlight used only for selected text.
 * Packed YUY2 pair; no filled selection rectangles.
 */
#define CDPLAYER_HILITE MENU_HILITE

static int
cdplayer_next_audio_track_no_wrap(int current)
{
  int last = cue_audio_last_track();
  int t;

  for (t = current + 1; t <= last; t++)
  {
    if (cue_audio_track_exists(t))
      return t;
  }

  return 0;
}

static int
cdplayer_audio_track_count(void)
{
  int first = cue_audio_first_track();
  int last = cue_audio_last_track();
  int t;
  int count = 0;

  for (t = first; t <= last; t++)
  {
    if (cue_audio_track_exists(t))
      count++;
  }

  return count;
}

static int
cdplayer_audio_track_ordinal(int track)
{
  int first = cue_audio_first_track();
  int last = cue_audio_last_track();
  int t;
  int ordinal = 0;

  for (t = first; t <= last; t++)
  {
    if (cue_audio_track_exists(t))
    {
      ordinal++;
      if (t == track)
        return ordinal;
    }
  }

  return 0;
}

static void
cdplayer_format_time(char *out, unsigned long seconds)
{
  unsigned long minutes = seconds / 60;
  seconds %= 60;
  sprintf(out, "%02lu:%02lu", minutes, seconds);
}

static void
draw_cdplayer_symbol(int x, int y, int symbol, int selected, int playing)
{
  const char *text;

  switch (symbol)
  {
    case 0:
      text = "<<";
      break;

    case 1:
      /*
       * Central control is PAUSE while playing and PLAY while paused/stopped.
       * It resumes the SAME CUE position.
       */
      text = playing ? "||" : ">";
      break;

    default:
      text = ">>";
      break;
  }

  if (selected)
    setfgcolour(CDPLAYER_HILITE);
  else
    setfgcolour(COLOR_BLACK);

  gprint(x, y, (char *)text, TXT_DOUBLE_TRANSPARENT);
}

static void
draw_cdplayer(int selected,
              int track,
              int playing,
              int paused,
              unsigned long elapsed_frames_48k)
{
  char line[48];
  char elapsed[16];
  char total[16];
  int count;
  int ordinal;
  unsigned long total_frames_44100;
  unsigned long elapsed_seconds;
  unsigned long total_seconds;

  DrawScreen();

  setfgcolour(COLOR_BLACK);

  sprintf(line, "CD PLAYER");
  gprint(gui_center_x(line, TXT_DOUBLE_TRANSPARENT),
         172, line, TXT_DOUBLE_TRANSPARENT);

  count = cdplayer_audio_track_count();
  ordinal = cdplayer_audio_track_ordinal(track);

  sprintf(line, "TRACK %02d / %02d", ordinal, count);
  gprint(gui_center_x(line, TXT_DOUBLE_TRANSPARENT),
         214, line, TXT_DOUBLE_TRANSPARENT);

  if (playing)
    sprintf(line, "PLAYING");
  else if (paused)
    sprintf(line, "PAUSED");
  else
    sprintf(line, "STOPPED");

  setfgcolour(CDPLAYER_STATUS_HILITE);
  gprint(gui_center_x(line, 4),
         248, line, 4);

  elapsed_seconds = elapsed_frames_48k / 48000UL;

  total_frames_44100 = cue_audio_track_frames_44100(track);
  total_seconds = total_frames_44100 / 44100UL;

  cdplayer_format_time(elapsed, elapsed_seconds);
  cdplayer_format_time(total, total_seconds);

  sprintf(line, "%s / %s", elapsed, total);
  setfgcolour(COLOR_BLACK);
  gprint(gui_center_x(line, 4),
         278, line, 4);

  draw_cdplayer_symbol(224, 320, 0, selected == 0, playing);
  draw_cdplayer_symbol(312, 320, 1, selected == 1, playing);
  draw_cdplayer_symbol(400, 320, 2, selected == 2, playing);

  if (selected == 3)
    setfgcolour(CDPLAYER_HILITE);
  else
    setfgcolour(COLOR_BLACK);

  sprintf(line, "GO BACK");
  gprint(gui_center_x(line, TXT_DOUBLE_TRANSPARENT),
         364, line, TXT_DOUBLE_TRANSPARENT);

  ShowScreen();
}

/*
 * Keep roughly 50 ms of CDDA queued before any redraw.
 * elapsed_frames_48k counts decoded player audio and therefore preserves
 * position across PAUSE/RESUME.
 */
static int
cdplayer_fill_audio(int *track, unsigned long *elapsed_frames_48k)
{
  int changed = 0;

  while (cdda_playing && audio_player_queued_frames() < 2400)
  {
    int was_playing = cdda_playing;
    int bytes;

    cdda_loop_check();

    if (was_playing && !cdda_playing)
    {
      int next = cdplayer_next_audio_track_no_wrap(*track);

      if (next)
      {
        *track = next;
        *elapsed_frames_48k = 0;
        cdda_play(*track);
      }

      changed = 1;
    }

    if (!cdda_playing)
      break;

    bytes = mp3_decoder(3200, (char *)mp3buffer);
    if (bytes > 0)
      *elapsed_frames_48k += (unsigned long)(bytes / 4);

    audio_player_update();
  }

  return changed;
}

static int
cdplayer_menu(void)
{
  int first;
  int track;
  int selected = 1;
  int redraw = 1;
  int quit = 0;
  int paused = 0;
  int saved_track;
  int saved_playing;
  int saved_autoloop;
  unsigned long elapsed_frames_48k = 0;
  unsigned long last_display_second = ~0UL;
  short joy;

  if (!have_ROM || !cue_is_mounted())
  {
    InfoScreen((char *)"CD Player requires a loaded CUE/BIN game");
    return 0;
  }

  first = cue_audio_first_track();
  if (!first)
  {
    InfoScreen((char *)"No audio tracks found");
    return 0;
  }

  saved_track = cdda_current_track;
  saved_playing = cdda_playing;
  saved_autoloop = cdda_autoloop;

  cdda_autoloop = 0;
  cdda_stop();
  track = first;
  audio_player_begin();

  while (!quit)
  {
    unsigned long display_second;

    joy = getMenuButtons();

    if (cdplayer_fill_audio(&track, &elapsed_frames_48k))
    {
      paused = 0;
      redraw = 1;
    }

    if (joy & PAD_BUTTON_LEFT)
    {
      if (selected < 3)
      {
        selected--;
        if (selected < 0) selected = 2;
        redraw = 1;
      }
    }

    if (joy & PAD_BUTTON_RIGHT)
    {
      if (selected < 3)
      {
        selected++;
        if (selected > 2) selected = 0;
        redraw = 1;
      }
    }

    if (joy & PAD_BUTTON_UP)
    {
      if (selected == 3)
      {
        selected = 1;
        redraw = 1;
      }
    }

    if (joy & PAD_BUTTON_DOWN)
    {
      if (selected < 3)
      {
        selected = 3;
        redraw = 1;
      }
    }

    if (joy & PAD_BUTTON_A)
    {
      switch (selected)
      {
        case 0: /* PREVIOUS */
          track = cdplayer_next_audio_track(track, -1);
          audio_player_end();
          audio_player_begin();
          elapsed_frames_48k = 0;
          paused = 0;
          cdda_play(track);
          cdplayer_fill_audio(&track, &elapsed_frames_48k);
          redraw = 1;
          break;

        case 1: /* PLAY / PAUSE / RESUME */
          if (cdda_playing)
          {
            /*
             * True pause: consumption stops but cue_audio state/position stays.
             * Do NOT call audio_player_end() here because that would discard
             * the player's queued position.
             */
            cdda_pause();
            paused = 1;
          }
          else if (paused)
          {
            cdda_resume();
            paused = 0;
            cdplayer_fill_audio(&track, &elapsed_frames_48k);
          }
          else
          {
            audio_player_begin();
            elapsed_frames_48k = 0;
            cdda_play(track);
            paused = 0;
            cdplayer_fill_audio(&track, &elapsed_frames_48k);
          }

          redraw = 1;
          break;

        case 2: /* NEXT */
          track = cdplayer_next_audio_track(track, 1);
          audio_player_end();
          audio_player_begin();
          elapsed_frames_48k = 0;
          paused = 0;
          cdda_play(track);
          cdplayer_fill_audio(&track, &elapsed_frames_48k);
          redraw = 1;
          break;

        case 3:
          quit = 1;
          break;
      }
    }

    if (joy & PAD_BUTTON_B)
      quit = 1;

    /*
     * Refresh the timer only when the displayed second changes.
     * Audio reserve has already been replenished before this redraw.
     */
    display_second = elapsed_frames_48k / 48000UL;
    if (display_second != last_display_second)
    {
      last_display_second = display_second;
      redraw = 1;
    }

    if (redraw)
    {
      draw_cdplayer(selected, track, cdda_playing, paused,
                    elapsed_frames_48k);
      redraw = 0;
    }
  }

  cdda_stop();
  audio_player_end();
  cdda_autoloop = saved_autoloop;

  if (saved_playing && saved_track > 1)
    cdda_play(saved_track);

  return 0;
}

/* Hidden main-menu entry: directional Konami sequence opens Debug Settings. */
static int DoMainMenu(char items[][22], int maxitems)
{
  static const u16 debug_code[] =
  {
    PAD_BUTTON_UP, PAD_BUTTON_UP,
    PAD_BUTTON_DOWN, PAD_BUTTON_DOWN,
    PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT,
    PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT
  };
  int debug_code_pos = 0;
  u64 debug_last_input = 0;
  int redraw = 1;
  int ret = 0;
  short joy;

  for (;;)
  {
    if (redraw)
    {
      draw_menu(&items[0], maxitems, menu);
      redraw = 0;
    }

    joy = getMenuButtons();

    /*
     * Deliberately hidden access to Debug Settings:
     * Up, Up, Down, Down, Left, Right, Left, Right.
     *
     * Only cursor directions participate.  Each next direction must be
     * entered within 1.5 seconds of the previous one.  A wrong direction
     * resets the sequence silently; if it is Up, it can immediately become
     * the first step of a new attempt.
     */
    if (joy & (PAD_BUTTON_UP | PAD_BUTTON_DOWN |
               PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT))
    {
      u64 now = gettime();

      if (debug_code_pos > 0 &&
          (now - debug_last_input) > 91125000ULL)
        debug_code_pos = 0;

      if (joy & debug_code[debug_code_pos])
      {
        debug_code_pos++;
        debug_last_input = now;

        if (debug_code_pos == (int)(sizeof(debug_code) / sizeof(debug_code[0])))
          return -3;
      }
      else
      {
        debug_code_pos = (joy & debug_code[0]) ? 1 : 0;
        debug_last_input = (debug_code_pos != 0) ? now : 0;
      }
    }

    if (joy & PAD_BUTTON_UP)
    {
      redraw = 1;
      menu--;
      if (menu < 0) menu = maxitems - 1;
    }

    if (joy & PAD_BUTTON_DOWN)
    {
      redraw = 1;
      menu++;
      if (menu == maxitems) menu = 0;
    }

    if (joy & PAD_BUTTON_A)
      return menu;

    if (joy & PAD_BUTTON_B)
      return -1;

    if (have_ROM && input_menu_button_down())
    {
      input_arm_menu_release_latch();
      ret = -2;
      return ret;
    }
  }
}

/****************************************************************************
 * Main Menu
 *
 ****************************************************************************/

int load_mainmenu()
{
  s8 ret;
  u8 quit = 0;
  menu = 0;
#ifdef HW_RVL
  u8 count;
  char items[7][22];
#else
  u8 count;
  char items[7][22];
#endif



  // GUI follows the persisted/current gameplay TV Mode.
  gui_select_video_mode(GetGameTVMode());

  VIDEO_SetBlack (1);
  VIDEO_Flush ();
  VIDEO_WaitVSync ();

  VIDEO_Configure (guivmode);
  VIDEO_ClearFrameBuffer(guivmode, xfb[whichfb], COLOR_BLACK);
  VIDEO_Flush();
  VIDEO_WaitVSync();
  VIDEO_WaitVSync();

  VIDEO_SetBlack (0);
  VIDEO_Flush ();

	while (quit == 0)
	{
      strcpy(items[0], "Play Game");
      strcpy(items[1], "Reset Game");
      strcpy(items[2], "Load New Game");

      if (have_ROM)
      {
        strcpy(items[3], "CD Player");
        strcpy(items[4], "Settings");
        strcpy(items[5], "Exit");
        strcpy(items[6], "Credits");
        count = 7;
        menu_y_start = 181;
      }
      else
      {
        strcpy(items[3], "Settings");
        strcpy(items[4], "Exit");
        strcpy(items[5], "Credits");
        count = 6;
        menu_y_start = 205;
      }

      if (menu >= count)
        menu = count - 1;

		ret = DoMainMenu (&items[0], count);

      if (!have_ROM && ret >= 3)
        ret++;

	switch (ret)
	{
      case -3: /*** Hidden Debug Settings (directional code on main menu) ***/
        if (debugsettingsmenu())
        {
          ret = 0;
          quit = 1;
        }
        break;

	  case -2: /*** Emulator-menu button: same action as Play Game ***/
	  case -1:
      case  0: /*** Return to game ***/
        ret = 0;
        quit = 1;
        break;

      case 1:  /*** Reset game ***/
        neogeo_reset();
        YM2610_sh_reset();
        ret = 0;
        quit = 1;
        break;

      case 2:  /*** Load device menu ***/
      {
        int loadret = loadmenu();

        if (loadret == 2)
        {
          ret = 0;
          quit = 1;
        }
        else
        {
          quit = loadret;
        }
        break;
      }

      case 3:  /*** CD Player ***/
        cdplayer_menu();
        break;

      case 4:  /*** Settings ***/
        if (optionmenu())
        {
          ret = 0;
          quit = 1;
        }
        break;

      case 5:  /*** Exit ***/
        VIDEO_ClearFrameBuffer(guivmode, xfb[whichfb], COLOR_BLACK);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        neogeocd_exit();
        break;

      case 6:  /*** Credits ***/
        if (credits() == 1)
        {
          ret = 0;
          quit = 1;
        }
        break;
	}
  }

  // Remove any still held buttons 
  while(PAD_ButtonsHeld(0)) PAD_ScanPads();
#ifdef HW_RVL
  while(WPAD_ButtonsHeld(0)) WPAD_ScanPads();
#endif

  menu_y_start = 205;
  return ret;
}

/****************************************************************************
* bannerscreen
****************************************************************************/
void
bannerscreen (void)
{
  int y, x;
  int offset;
  int srcrow;
  int *bb = (int *) bannerunc;

  whichfb ^= 1;

  VIDEO_ClearFrameBuffer(guivmode, xfb[whichfb], COLOR_BLACK);

  if (gui_is_240p())
  {
    for (y = 0; y < (banner_HEIGHT >> 1); y++)
    {
      offset = ((gui_y(200) + y) * 320) + 40;
      srcrow = (y << 1) * (banner_WIDTH >> 1);

      for (x = 0; x < (banner_WIDTH >> 1); x++)
        xfb[whichfb][offset + x] = bb[srcrow + x];
    }
  }
  else
  {
    for (y = 0; y < banner_HEIGHT; y++)
    {
      offset = ((200 + y) * 320) + 40;
      srcrow = y * (banner_WIDTH >> 1);

      for (x = 0; x < (banner_WIDTH >> 1); x++)
        xfb[whichfb][offset + x] = bb[srcrow + x];
    }
  }

  VIDEO_SetNextFramebuffer(xfb[whichfb]);
  VIDEO_Flush();
  VIDEO_WaitVSync();
}

