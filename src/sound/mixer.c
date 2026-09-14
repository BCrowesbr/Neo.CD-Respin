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

#define MIXBUFFER 65536
#define MIXMASK ((MIXBUFFER >> 2) -1)

static u8 mixbuffer[MIXBUFFER];	/*** 16k mixing buffer ***/
char mp3buffer[8192];		/*** Filled on each call by streamupdate ***/

static double mp3volume = 1.0f;
static double fxvolume = 1.0f;
static EQSTATE eqs;
static MIXER mixer;
static int mixer_last_frame = 0;


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
  double lsample;
  double rsample;

  s = (s16 *) src;
  d = (s16 *) dst;

  for (i = 0; i < len >> 1; i += 2)
    {
      lsample = ((int) ((double) d[i] * mp3volume) + (int)((double) s[i] * fxvolume));
      rsample = ((int) ((double) d[i + 1] * mp3volume) + (int)((double) s[i + 1] * fxvolume));

//     lsample = (int)((double) s[i] * fxvolume);
//      rsample = (int)((double) s[i + 1] * fxvolume);

      lsample = do_3band (&eqs, lsample);
      rsample = do_3band (&eqs, rsample);

      if (lsample < -32768)
	lsample = -32768;
      else
	{
	  if (lsample > 32767)
	    lsample = 32767;
	}

      if (rsample < -32768)
	rsample = -32768;
      else
	{
	  if (rsample > 32767)
	    rsample = 32767;
	}

      d[i] = (s16) lsample;
      d[i + 1] = (s16) rsample;

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
  int *src = (int *) mp3buffer;
  int *dst = (int *) mixbuffer;

  /*** Update from sound core ***/
  streamupdate (3200);
  MP3MixAudio (mp3buffer, (u8 *) play_buffer, 3200);

  /*** Update the mixbuffer ***/
  for (i = 0; i < 800; i++)
    {
      dst[mixer.head] = *src++;
      mixer.head++;
      mixer.head &= MIXMASK;
    }
}

/****************************************************************************
* mixer_update_cdda_only
*
* Current GUI CD Player compatibility.
* mp3buffer is already filled by mp3_decoder() in cdplayer_fill_audio().
* Just enqueue those 800 stereo frames into the original RX ring buffer.
****************************************************************************/
void
mixer_update_cdda_only (void)
{
  int i;
  int *src = (int *) mp3buffer;
  int *dst = (int *) mixbuffer;

  for (i = 0; i < 800; i++)
    {
      dst[mixer.head] = *src++;
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
  memset (mp3buffer, 0, 8192);
  memset (mixbuffer, 0, MIXBUFFER);
  mixer_last_frame = 0;
  init_3band_state (&eqs, 880, 5000, 48000);
}

/****************************************************************************
* mixer_getaudio
****************************************************************************/
int
mixer_getaudio (u8 * outbuffer, int length)
{
  int *dst = (int *) outbuffer;
  int *src = (int *) mixbuffer;
  int frames = length >> 2;
  unsigned int queued = mixer_queued_frames();
  int consume_frames = frames;
  int i;

  /*
   * Keep the Wii AI DMA cadence fixed at exactly 800 output frames, but
   * recover final-ring safety margin when scheduling jitter has drained it.
   *
   * The old 792/800/808 experiment changed the DMA block itself and therefore
   * changed callback cadence.  This does NOT do that: the hardware still gets
   * the same 800-frame block every callback.  Only the number of real ring
   * frames consumed is reduced slightly while the queue is low.
   *
   *  queue >= 2400 : 800 -> 800 (bit-for-bit normal path)
   *  queue 1600..2399: 796 -> 800 (0.5% temporary stretch)
   *  queue  792..1599: 792 -> 800 (1.0% temporary stretch)
   *
   * The small repeats are spread across the complete DMA block instead of
   * being concentrated at its tail.  This lets the queue rebuild gradually
   * after a stall and avoids the repeated starvation state seen in the
   * runtime diagnostic.
   */
  if (frames == 800)
    {
      if (queued < 1600 && queued >= 792)
        consume_frames = 792;
      else if (queued < 2400 && queued >= 796)
        consume_frames = 796;
    }

  if (queued >= (unsigned int)consume_frames)
    {
      unsigned int base_tail = (unsigned int)mixer.tail;

      for (i = 0; i < frames; i++)
        {
          unsigned int src_pos;
          int frame;

          /* Nearest-neighbour time stretch, evenly distributed. */
          src_pos = ((unsigned int)i * (unsigned int)consume_frames) /
                    (unsigned int)frames;
          frame = src[(base_tail + src_pos) & MIXMASK];
          *dst++ = frame;
          mixer_last_frame = frame;
        }

      mixer.tail = (mixer.tail + consume_frames) & MIXMASK;
      return length;
    }

  /*
   * True starvation: consume what is actually present, then conceal only the
   * missing tail.  This remains the last-resort path.
   */
  i = 0;
  while (i < frames && mixer.tail != mixer.head)
    {
      int frame = src[mixer.tail];

      *dst++ = frame;
      mixer_last_frame = frame;
      mixer.tail++;
      mixer.tail &= MIXMASK;
      i++;
    }

  if (i < frames)
    {
      while (i < frames)
        {
          *dst++ = mixer_last_frame;
          i++;
        }
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
	(void)music_l;
	(void)music_m;
	(void)music_h;

	mp3volume = (double)music_vol;
	fxvolume = (double)sound_vol;
	eqs.lg = (double)sound_l;
	eqs.mg = (double)sound_m;
	eqs.hg = (double)sound_h;
}
