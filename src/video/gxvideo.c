/****************************************************************************
*   NeoCDRX
*   NeoGeo CD Emulator
*   NeoCD Redux - Copyright (C) 2007 softdev
****************************************************************************/
/****************************************************************************
* NeoCDRX
*
* GX Video - Wii 240p backend
*
* Gameplay:
*   Neo Geo framebuffer: 320x224
*   Wii video mode:      NTSC 240p (TVNtsc240Ds)
*   Presentation:        orthographic 2D
*
* The 320x224 image is presented as a centered 598x224 rectangle inside
* the 640x240 EFB. 598/640 ~= 224/240, so the active image occupies the
* same fraction of the 4:3 screen in both axes.
****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include "neocdrx.h"

/*** External video state owned by neocdrx.c ***/
extern int whichfb;
extern u32 *xfb[2];
extern GXRModeObj *vmode;

static int vwidth = 0;
static int vheight = 0;
static int oldvwidth = 0;
static int oldvheight = 0;

/*** Gameplay TV mode. GUI video remains independent in gui.c. ***/
static int game_tv_mode = GAME_TVMODE_240P;
static GXRModeObj *gamevmode = &TVNtsc240Ds;
static int game_logical_height = 240;
static int game_video_mode_dirty = 0;
static int gx_started = 0;

/*
 * TV Mode is intentionally applied only to gameplay in this first version.
 * The existing GUI stays in its proven 240p path.
 */
void
SetGameTVMode(int mode)
{
  if (mode != GAME_TVMODE_240P && mode != GAME_TVMODE_480I)
    mode = GAME_TVMODE_240P;

  game_tv_mode = mode;

  switch (game_tv_mode)
  {
    case GAME_TVMODE_480I:
      gamevmode = &TVNtsc480IntDf;
      game_logical_height = 480;
      break;


    case GAME_TVMODE_240P:
    default:
      gamevmode = &TVNtsc240Ds;
      game_logical_height = 240;
      break;
  }

  /*
   * Defer the actual VI/GX transition until gameplay resumes.  This avoids
   * mixing GUI state with a half-applied gameplay mode.
   */
  game_video_mode_dirty = 1;
}

int
GetGameTVMode(void)
{
  return game_tv_mode;
}

const char *
GetGameTVModeName(void)
{
  switch (game_tv_mode)
  {
    case GAME_TVMODE_480I:
      return "480i";
    default:
      return "240p";
  }
}

/*** GX ***/
#define DEFAULT_FIFO_SIZE (256 * 1024)
#define TEXSIZE ((NEOSCR_WIDTH * NEOSCR_HEIGHT) * 2)

static u8 gp_fifo[DEFAULT_FIFO_SIZE] ATTRIBUTE_ALIGN(32);
static u8 texturemem[TEXSIZE] ATTRIBUTE_ALIGN(32);

static GXTexObj texobj;
static Mtx view;
static s16 square[12] ATTRIBUTE_ALIGN(32);

/* User-adjustable CRT screen geometry. Defaults reproduce the current output. */
static int screen_h_size = 670;
static int screen_h_pos  = 0;
static int screen_v_size = 224;
static int screen_v_pos  = 4;

static void configure_game_geometry(void);

static int clamp_int(int v, int lo, int hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void SetScreenGeometry(int hsize, int hpos, int vsize, int vpos)
{
  screen_h_size = clamp_int(hsize, 560, 720);
  screen_h_pos  = clamp_int(hpos, -40, 40);
  screen_v_size = clamp_int(vsize, 200, 224);
  screen_v_pos  = clamp_int(vpos, -20, 20);
  configure_game_geometry();
}

void GetScreenGeometry(int *hsize, int *hpos, int *vsize, int *vpos)
{
  if (hsize) *hsize = screen_h_size;
  if (hpos)  *hpos  = screen_h_pos;
  if (vsize) *vsize = screen_v_size;
  if (vpos)  *vpos  = screen_v_pos;
}

void ResetScreenGeometry(void)
{
  SetScreenGeometry(670, 0, 224, 4);
}

void ApplyGameScreenGeometry(unsigned int ngh, const char *gamename)
{
  int vpos = 4;

  /* User-tested Neo Geo CD vertical-position exceptions. */
  switch (ngh)
  {
    /* V -2 */
    case 0x0201: /* Metal Slug */
    case 0x0241: /* Metal Slug 2 */
    case 0x0010: /* Cyber-Lip */
    case 0x0001: /* NAM-1975 */
    case 0x0200: /* Neo Turf Masters */
    case 0x0089: /* Pulstar */
    case 0x0022: /* Raguy / Blue's Journey */
      vpos = -2;
      break;

    /* V 0 */
    case 0x0083: /* Bust-A-Move / Puzzle Bobble */
    case 0x0005: /* Magician Lord */
    case 0x0009: /* Ninja Combat */
    case 0x0050: /* Ninja Commando */
    case 0x0229: /* Samurai Shodown RPG */
      vpos = 0;
      break;

    default:
      break;
  }

  /* Xeno Crisis is an aftermarket NGCD release (BB01), not a classic numeric NGH entry. */
  if (gamename && stricmp(gamename, "XENO CRISIS") == 0)
    vpos = -2;

  SetScreenGeometry(670, 0, 224, vpos);
}

/*
 * 320x224 Neo Geo active image centered in a 640x240 EFB.
 *
 * Vertical:
 *   224 active lines -> 8 blank lines top + 8 bottom.
 *
 * Horizontal:
 *   Same 224/240 occupancy ratio applied to 640:
 *   640 * 224 / 240 = 597.33 -> 598 pixels.
 *   -> 21 pixels left + 21 pixels right.
 *
 * This gives a stable 4:3 presentation without the old 3D perspective
 * scaler.
 */
#define GAME_LEFT   0
#define GAME_RIGHT  640

static void
configure_game_geometry(void)
{
  int logical_h = game_logical_height;
  int hsize = screen_h_size;
  int vsize = screen_v_size;
  int hpos = screen_h_pos;
  int vpos = screen_v_pos;
  int left, right, top, bottom;

  /* 480i doubles vertical geometry; horizontal EFB geometry remains 640. */
  if (logical_h != 240)
  {
    vsize *= 2;
    vpos *= 2;
  }

  left = ((640 - hsize) / 2) + hpos;
  right = left + hsize;
  top = ((logical_h - vsize) / 2) + vpos;
  bottom = top + vsize;

  square[0] = left;   square[1] = top;    square[2] = 0;
  square[3] = right;  square[4] = top;    square[5] = 0;
  square[6] = right;  square[7] = bottom; square[8] = 0;
  square[9] = left;   square[10] = bottom; square[11] = 0;
}

static void
draw_init(void)
{
  configure_game_geometry();

  GX_ClearVtxDesc();

  GX_SetVtxDesc(GX_VA_POS, GX_INDEX8);
  GX_SetVtxDesc(GX_VA_CLR0, GX_INDEX8);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

  GX_SetArray(GX_VA_POS, square, 3 * sizeof(s16));

  GX_SetNumTexGens(1);
  GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

  GX_InvalidateTexAll();

  GX_InitTexObj(&texobj, texturemem, vwidth, vheight, GX_TF_RGB565,
                GX_CLAMP, GX_CLAMP, GX_FALSE);
}

static void
draw_vert(u8 pos, u8 c, f32 s, f32 t)
{
  GX_Position1x8(pos);
  GX_Color1x8(c);
  GX_TexCoord2f32(s, t);
}

static void
draw_square(Mtx v)
{
  Mtx m;
  Mtx mv;

  /*
   * Pure 2D model matrix. The original source translated the quad to Z=-100
   * and viewed it through guPerspective(). That is intentionally removed.
   */
  guMtxIdentity(m);
  guMtxConcat(v, m, mv);

  GX_LoadPosMtxImm(mv, GX_PNMTX0);

  GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
  draw_vert(0, 0, 0.0f, 0.0f);
  draw_vert(1, 0, 1.0f, 0.0f);
  draw_vert(2, 0, 1.0f, 1.0f);
  draw_vert(3, 0, 0.0f, 1.0f);
  GX_End();
}

/****************************************************************************
 * Resume gameplay in the selected TV Mode after leaving the GUI.
 ****************************************************************************/
void
ResumeGX(void)
{
  Mtx44 p;
  f32 yscale;
  u32 xfbHeight;

  /*
   * A TV Mode change is applied as one complete gameplay-video transition.
   * This is deliberately more conservative than changing only VIDEO_Configure:
   * stale 240-line viewport/copy/projection state must not leak into 480i.
   */
  GX_DrawDone();
  VIDEO_SetBlack(TRUE);
  VIDEO_Flush();
  VIDEO_WaitVSync();

  VIDEO_Configure(gamevmode);

  VIDEO_SetNextFramebuffer(xfb[whichfb]);
  VIDEO_SetBlack(TRUE);
  VIDEO_Flush();
  VIDEO_WaitVSync();

  GX_SetViewport(0.0f, 0.0f,
                 (f32)gamevmode->fbWidth,
                 (f32)gamevmode->efbHeight,
                 0.0f, 1.0f);
  GX_SetScissor(0, 0, gamevmode->fbWidth, gamevmode->efbHeight);

  yscale = GX_GetYScaleFactor(gamevmode->efbHeight, gamevmode->xfbHeight);
  xfbHeight = GX_SetDispCopyYScale(yscale);

  GX_SetDispCopySrc(0, 0, gamevmode->fbWidth, gamevmode->efbHeight);
  GX_SetDispCopyDst(gamevmode->fbWidth, xfbHeight);
  GX_SetCopyFilter(gamevmode->aa, gamevmode->sample_pattern,
                   GX_TRUE, gamevmode->vfilter);
  GX_SetFieldMode(gamevmode->field_rendering,
                  ((gamevmode->viHeight == 2 * gamevmode->xfbHeight) ?
                   GX_ENABLE : GX_DISABLE));

  guOrtho(p, 0.0f, (f32)game_logical_height,
          0.0f, 640.0f, 0.0f, 1.0f);
  GX_LoadProjectionMtx(p, GX_ORTHOGRAPHIC);

  configure_game_geometry();

  /*
   * Force update_video() to rebuild the draw state against the new geometry
   * even if the Neo Geo source width/height did not change.
   */
  oldvwidth = 0;
  oldvheight = 0;

  GX_CopyDisp(xfb[whichfb], GX_TRUE);
  GX_Flush();

  VIDEO_SetNextFramebuffer(xfb[whichfb]);
  VIDEO_SetBlack(FALSE);
  VIDEO_Flush();
  VIDEO_WaitVSync();

  game_video_mode_dirty = 0;
}

void
StartGX(void)
{
  configure_game_geometry();
  Mtx44 p;
  GXColor gxbackground = {0, 0, 0, 0xff};

  /*
   * GUI remains independent. Gameplay uses the TV Mode selected in Settings.
   * Default is the existing real NTSC 240p path.
   */
  VIDEO_SetBlack(1);
  VIDEO_Flush();
  VIDEO_WaitVSync();

  VIDEO_Configure(gamevmode);
  VIDEO_ClearFrameBuffer(gamevmode, xfb[0], COLOR_BLACK);
  VIDEO_ClearFrameBuffer(gamevmode, xfb[1], COLOR_BLACK);
  VIDEO_SetNextFramebuffer(xfb[whichfb]);

  VIDEO_Flush();
  VIDEO_WaitVSync();

  VIDEO_SetBlack(0);
  VIDEO_Flush();

  memset(&gp_fifo, 0, DEFAULT_FIFO_SIZE);

  GX_Init(&gp_fifo, DEFAULT_FIFO_SIZE);
  GX_SetCopyClear(gxbackground, 0x00ffffff);

  GX_SetViewport(0, 0, gamevmode->fbWidth, gamevmode->efbHeight, 0, 1);

  GX_SetDispCopyYScale((f32)gamevmode->xfbHeight /
                       (f32)gamevmode->efbHeight);

  GX_SetScissor(0, 0, gamevmode->fbWidth, gamevmode->efbHeight);

  GX_SetDispCopySrc(0, 0, gamevmode->fbWidth, gamevmode->efbHeight);
  GX_SetDispCopyDst(gamevmode->fbWidth, gamevmode->xfbHeight);

  GX_SetCopyFilter(gamevmode->aa,
                   gamevmode->sample_pattern,
                   GX_TRUE,
                   gamevmode->vfilter);

  GX_SetFieldMode(gamevmode->field_rendering,
                  ((gamevmode->viHeight == 2 * gamevmode->xfbHeight) ?
                   GX_ENABLE : GX_DISABLE));

  GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
  GX_SetCullMode(GX_CULL_NONE);

  GX_CopyDisp(xfb[whichfb ^ 1], GX_TRUE);
  GX_SetDispCopyGamma(GX_GM_1_0);

  /*
   * Exact 2D projection. 240p keeps the original 0..240 space;
   * 480i use 0..480 and double only the presentation geometry.
   */
  guOrtho(p, 0.0f, (f32)game_logical_height,
          0.0f, 640.0f, 0.0f, 1.0f);
  GX_LoadProjectionMtx(p, GX_ORTHOGRAPHIC);

  guMtxIdentity(view);

  memset(texturemem, 0, TEXSIZE);

  vwidth = 100;
  vheight = 100;

  gx_started = 1;
  game_video_mode_dirty = 0;
}

/****************************************************************************
 * Update Video
 ****************************************************************************/
void
update_video(int width, int height, char *vbuffer)
{
  int h, w;

  long long int *dst  = (long long int *)texturemem;
  long long int *src1 = (long long int *)vbuffer;
  long long int *src2 = (long long int *)(vbuffer + 640);
  long long int *src3 = (long long int *)(vbuffer + 1280);
  long long int *src4 = (long long int *)(vbuffer + 1920);

  /*
   * Neo Geo renderer remains native 320x224.
   */
  vwidth = 320;
  vheight = 224;


  whichfb ^= 1;

  if ((oldvheight != vheight) || (oldvwidth != vwidth))
  {
    oldvwidth = vwidth;
    oldvheight = vheight;

    draw_init();

    /*
     * Keep the modelview matrix strictly 2D.
     */
    guMtxIdentity(view);

    GX_SetViewport(0, 0,
                   gamevmode->fbWidth,
                   gamevmode->efbHeight,
                   0, 1);
  }

  GX_InvVtxCache();
  GX_InvalidateTexAll();

  GX_SetTevOp(GX_TEVSTAGE0, GX_DECAL);
  GX_SetTevOrder(GX_TEVSTAGE0,
                 GX_TEXCOORD0,
                 GX_TEXMAP0,
                 GX_COLOR0A0);

  /*
   * Original 320x224 RGB565 -> GX tiled texture conversion.
   */
  for (h = 0; h < vheight; h += 4)
  {
    for (w = 0; w < 80; w++)
    {
      *dst++ = *src1++;
      *dst++ = *src2++;
      *dst++ = *src3++;
      *dst++ = *src4++;
    }

    src1 += 240;
    src2 += 240;
    src3 += 240;
    src4 += 240;
  }


  DCFlushRange(texturemem, TEXSIZE);

  GX_SetNumChans(1);
  GX_LoadTexObj(&texobj, GX_TEXMAP0);

  draw_square(view);

  GX_DrawDone();

  GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  GX_SetColorUpdate(GX_TRUE);

  GX_CopyDisp(xfb[whichfb], GX_TRUE);
  GX_Flush();

  VIDEO_SetNextFramebuffer(xfb[whichfb]);
  VIDEO_Flush();

  /*
   * Keep original synchronization behaviour for now.
   */
  VIDEO_WaitVSync();
}
