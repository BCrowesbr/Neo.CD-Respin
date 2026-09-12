/****************************************************************************
*   NeoCDRX
*   NeoGeo CD Emulator
*   NeoCD Redux - Copyright (C) 2007 softdev
****************************************************************************/
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "neocdrx.h"
#include "streams.h"
#include "eq.h"

#define MIXBUFFER 16384
#define MIXMASK ((MIXBUFFER >> 2) -1)

/* One natural NeoCDRX audio frame at 48 kHz / 60 Hz. */
#define MIXER_FRAME_SAMPLES 800

static u8 mixbuffer[MIXBUFFER];	/*** 16k mixing buffer ***/
char mp3buffer[8192];		/*** Filled on each call by streamupdate ***/

/*
 * NeoCDRE dual-bus mixer:
 * music/CDDA and Neo Geo sound have independent volume and EQ.
 * Each stereo channel owns its own EQ history.
 */
static double musicvolume = 1.0f;
static double soundvolume = 1.0f;

static EQSTATE music_eq_l;
static EQSTATE music_eq_r;
static EQSTATE sound_eq_l;
static EQSTATE sound_eq_r;

static MIXER mixer;

/*
 * Near-full-scale output protection.
 * Samples below 30000 pass unchanged. Peaks above it are progressively
 * compressed instead of hitting the old abrupt +/-32767 hard clip.
 */
#define MIX_HEADROOM           0.90
#define OUTPUT_LIMIT_THRESHOLD 30000.0
#define OUTPUT_LIMIT_CEILING   32767.0

/*
 * Neo Geo sound-bus peak protection.
 *
 * This acts ONLY on synthesized game audio (FM/ADPCM/PSG) before it is
 * added to CDDA.  It is intentionally gentler than a compressor and leaves
 * ordinary samples untouched.  The threshold is lower than the final master
 * protection so very hot voices / impact samples cannot slam directly into
 * the CDDA sum.
 */
#define SOUND_LIMIT_THRESHOLD 27000.0
#define SOUND_LIMIT_CEILING   31000.0

static double
sound_soft_limit(double sample)
{
  double sign = 1.0;
  double room;
  double over;

  if (sample < 0.0)
  {
    sign = -1.0;
    sample = -sample;
  }

  if (sample <= SOUND_LIMIT_THRESHOLD)
    return sign * sample;

  room = SOUND_LIMIT_CEILING - SOUND_LIMIT_THRESHOLD;
  over = sample - SOUND_LIMIT_THRESHOLD;

  sample = SOUND_LIMIT_THRESHOLD +
           (room * over) / (room + over);

  return sign * sample;
}

static double
mixer_soft_limit(double sample)
{
  double sign = 1.0;
  double room;
  double over;

  if (sample < 0.0)
  {
    sign = -1.0;
    sample = -sample;
  }

  if (sample <= OUTPUT_LIMIT_THRESHOLD)
    return sign * sample;

  room = OUTPUT_LIMIT_CEILING - OUTPUT_LIMIT_THRESHOLD;
  over = sample - OUTPUT_LIMIT_THRESHOLD;

  sample = OUTPUT_LIMIT_THRESHOLD +
           (room * over) / (room + over);

  return sign * sample;
}

/* v7.1: last frame delivered to AI, used only to pad a rare short DMA block. */
static int dma_last_frame = 0;

unsigned int mixer_queued_frames(void)
{
  return (unsigned int)((mixer.head - mixer.tail) & MIXMASK);
}

/****************************************************************************
 * Nintendo GameCube SDL_MixAudio replacement function.
 *
 * This is adapted from the source of libsdl-1.2.9
 * As such it only contains the mixing function for 16BITMSB samples.
 ****************************************************************************/
void
ngcMixAudio (u8 * dst, u8 * src, int len, int volume)
//ngcMixAudio (Uint8 * dst, Uint8 * src, int len, int volume)
{
	
  s16 src1, src2;
  s16 *dsts = (s16 *) dst;
  s16 *srcs = (s16 *) src;

  int dst_sample;
  const int max_audioval = 32767;
  const int min_audioval = -32768;

  //len >>= 1;

  while (len--)
    {
      src1 = srcs[0];
      src2 = dsts[0];
      srcs++;

      dst_sample = (src1 + src2);

      if (dst_sample > max_audioval)
	{
	  dst_sample = max_audioval;
	}
      else if (dst_sample < min_audioval)
	{
	  dst_sample = min_audioval;
	}

      *dsts++ = (s16) dst_sample;
    }
}

static void
MP3MixAudio (char * dst, u8 * src, int len)
{
  s16 *s, *d;
  int i;
  double raw_music_l, raw_music_r;
  double raw_sound_l, raw_sound_r;
  double music_l, music_r;
  double sound_l, sound_r;
  double mix_l, mix_r;
  double out_l, out_r;

  s = (s16 *) src;
  d = (s16 *) dst;

  for (i = 0; i < len >> 1; i += 2)
  {
    /* Source level before user volume and before EQ. */
    raw_music_l = (double)d[i];
    raw_music_r = (double)d[i + 1];
    raw_sound_l = (double)s[i];
    raw_sound_r = (double)s[i + 1];

    music_l = raw_music_l * musicvolume;
    music_r = raw_music_r * musicvolume;
    sound_l = raw_sound_l * soundvolume;
    sound_r = raw_sound_r * soundvolume;

    music_l = do_3band(&music_eq_l, (int)music_l);
    music_r = do_3band(&music_eq_r, (int)music_r);
    sound_l = do_3band(&sound_eq_l, (int)sound_l);
    sound_r = do_3band(&sound_eq_r, (int)sound_r);

    /* Existing v3 Sound-only protection. */
    sound_l = sound_soft_limit(sound_l);
    sound_r = sound_soft_limit(sound_r);

    mix_l = (music_l + sound_l) * MIX_HEADROOM;
    mix_r = (music_r + sound_r) * MIX_HEADROOM;

    /* Existing v3 final protection. */
    out_l = mixer_soft_limit(mix_l);
    out_r = mixer_soft_limit(mix_r);

    if (out_l < -32768.0) out_l = -32768.0;
    if (out_l >  32767.0) out_l =  32767.0;
    if (out_r < -32768.0) out_r = -32768.0;
    if (out_r >  32767.0) out_r =  32767.0;

    d[i]     = (s16)out_l;
    d[i + 1] = (s16)out_r;
  }
}

/****************************************************************************
* audio_update
*
* Called from NeoCD every frame.
****************************************************************************/
void
mixer_update_audio (void)
{
  int i;
  int *dst = (int *) mixbuffer;

  /*** Update from sound core ***/
  streamupdate (3200);
  MP3MixAudio (mp3buffer, (u8 *) play_buffer, 3200);

  /*** Update the mixbuffer.
   *
   * v8: keep the mixed PCM untouched.  Clock correction is performed by
   * the Wii DMA scheduler in gcaudio.c, following the Genesis Plus GX
   * strategy of measuring AUDIO_GetDMABytesLeft() at VSYNC.
   */
  for (i = 0; i < MIXER_FRAME_SAMPLES; i++)
  {
    dst[mixer.head] = ((int *)mp3buffer)[i];
    mixer.head++;
    mixer.head &= MIXMASK;
  }

}

/****************************************************************************
* mixer_update_cdda_only
*
* GUI CD Player producer. mp3buffer already contains one 800-frame CDDA block.
* Mix with a zero Sound bus, preserving Music volume/EQ/headroom, but do not
* execute streamupdate() or any emulated sound-core work while gameplay is paused.
****************************************************************************/
void
mixer_update_cdda_only (void)
{
  int i;
  int *dst = (int *) mixbuffer;
  static u8 silent_sound[3200] ATTRIBUTE_ALIGN(32);

  memset(silent_sound, 0, sizeof(silent_sound));
  MP3MixAudio(mp3buffer, silent_sound, 3200);

  for (i = 0; i < MIXER_FRAME_SAMPLES; i++)
  {
    dst[mixer.head] = ((int *)mp3buffer)[i];
    mixer.head++;
    mixer.head &= MIXMASK;
  }
}

/****************************************************************************
* mixer_init
****************************************************************************/
void
mixer_init (void)
{
  memset (&mixer, 0, sizeof (MIXER));
  dma_last_frame = 0;
  memset (mp3buffer, 0, 8192);
  memset (mixbuffer, 0, MIXBUFFER);
  init_3band_state(&music_eq_l, 880, 5000, 48000);
  init_3band_state(&music_eq_r, 880, 5000, 48000);
  init_3band_state(&sound_eq_l, 880, 5000, 48000);
  init_3band_state(&sound_eq_r, 880, 5000, 48000);
}

/****************************************************************************
* mixer_getaudio
****************************************************************************/
int
mixer_getaudio (u8 * outbuffer, int length)
{
  int *dst = (int *) outbuffer;
  int *src = (int *) mixbuffer;
  int requested_frames = length >> 2;
  int copied_frames = 0;
  /* Keep the Wii AI block length fixed; short FIFO reads are padded below. */

  while (copied_frames < requested_frames && mixer.tail != mixer.head)
  {
    dma_last_frame = src[mixer.tail];
    *dst++ = dma_last_frame;
    mixer.tail++;
    mixer.tail &= MIXMASK;
    copied_frames++;
  }

  while (copied_frames < requested_frames)
  {
    *dst++ = dma_last_frame;
    copied_frames++;
  }

  return length;
}


/****************************************************************************
* mixer_set
****************************************************************************/
void mixer_set(float sound_vol, float music_vol,
               float sound_l, float sound_m, float sound_h,
               float music_l, float music_m, float music_h)
{
  soundvolume = (double)sound_vol;
  musicvolume = (double)music_vol;

  sound_eq_l.lg = sound_eq_r.lg = (double)sound_l;
  sound_eq_l.mg = sound_eq_r.mg = (double)sound_m;
  sound_eq_l.hg = sound_eq_r.hg = (double)sound_h;

  music_eq_l.lg = music_eq_r.lg = (double)music_l;
  music_eq_l.mg = music_eq_r.mg = (double)music_m;
  music_eq_l.hg = music_eq_r.hg = (double)music_h;
}
