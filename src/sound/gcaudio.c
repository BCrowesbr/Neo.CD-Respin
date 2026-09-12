/****************************************************************************
*   NeoCDRX
*   NeoGeo CD Emulator
*   NeoCD Redux - Copyright (C) 2007 softdev
****************************************************************************/

#include "neocdrx.h"
#include "streams.h"
#include "eq.h"

/**
 * Nintendo Gamecube Audio Interface 
 */

u8 soundbuffer[2][8192] ATTRIBUTE_ALIGN(32);
u32 mixbuffer;
u32 audioStarted;
static int whichab = 0;
static int IsPlaying = 0;

/*
 * v10 VBL/AI phase servo.
 *
 * v9 established that producing the 800-frame mixer block immediately after
 * VBL reduces the residual CDDA flicks.  Keep that producer timing and use the
 * AI DMA block length only to steer the DMA completion phase toward the same
 * region of the following VBL.  PCM is never interpolated, dropped or
 * duplicated here: all frames consumed from the mixer remain consecutive.
 *
 * Wii AI DMA lengths must be 32-byte aligned.  One stereo frame is 4 bytes,
 * therefore the smallest correction quantum is 8 stereo frames.
 */
#define AUDIO_DMA_NOMINAL_FRAMES 800
#define AUDIO_DMA_TRIM_FRAMES       8
#define AUDIO_DMA_MIN_FRAMES      (AUDIO_DMA_NOMINAL_FRAMES - AUDIO_DMA_TRIM_FRAMES)
#define AUDIO_DMA_MAX_FRAMES      (AUDIO_DMA_NOMINAL_FRAMES + AUDIO_DMA_TRIM_FRAMES)
#define AUDIO_PREBUFFER_FRAMES    2400

/* Desired amount of the current DMA still pending when VBL occurs.
 * 192 bytes = 48 stereo frames = about 1 ms at 48 kHz.  This places the DMA
 * completion shortly after VBL, after which v9 immediately adds the next
 * 800-frame producer block to the mixer. */
#define AUDIO_PHASE_TARGET_BYTES  192
#define AUDIO_PHASE_DEADBAND_BYTES 64

static volatile int audio_dma_next_frames = AUDIO_DMA_NOMINAL_FRAMES;

/****************************************************************************
 * AudioSwitchBuffers
 *
 * Genesis Plus only provides sound data on completion of each frame.
 * To try to make the audio less choppy, this function is called from both the
 * DMA completion and update_audio.
 *
 * Testing for data in the buffer ensures that there are no clashes.
 ****************************************************************************/
static void AudioSwitchBuffers(void)
{
    int frames = audio_dma_next_frames;
    int len = frames * 4;

    whichab ^= 1;
    mixer_getaudio(soundbuffer[whichab], len);

    IsPlaying = 1;
    DCFlushRange(soundbuffer[whichab], len);
    AUDIO_InitDMA((u32) soundbuffer[whichab], len);
    AUDIO_StartDMA();
}

/****************************************************************************
 * audio_vsync_sync
 *
 * Direct VBL/AI phase servo.  v9 produces the next 800 mixed frames just
 * after VBL; this function nudges the next aligned DMA transfer so its
 * completion stays close to that producer point instead of drifting through
 * the frame.
 ****************************************************************************/
void audio_vsync_sync(void)
{
    u16 remain;
    int low = AUDIO_PHASE_TARGET_BYTES - AUDIO_PHASE_DEADBAND_BYTES;
    int high = AUDIO_PHASE_TARGET_BYTES + AUDIO_PHASE_DEADBAND_BYTES;

    if (!IsPlaying)
        return;

    remain = AUDIO_GetDMABytesLeft();
    if (!remain)
        return;

    /* Phase correction for the next DMA block.
     *
     * Too little data left at VBL means DMA completion is arriving too soon.
     * Lengthen the next transfer by 8 frames to move the next completion later.
     * Too much data left means completion is late; shorten the next transfer by
     * 8 frames to pull it earlier.  Inside the deadband, use the natural 800. */
    if ((int)remain < low)
        audio_dma_next_frames = AUDIO_DMA_MAX_FRAMES;
    else if ((int)remain > high)
        audio_dma_next_frames = AUDIO_DMA_MIN_FRAMES;
    else
        audio_dma_next_frames = AUDIO_DMA_NOMINAL_FRAMES;
}

/****************************************************************************
 * InitGCAudio
 *
 * Stock code to set the DSP at 48Khz
 ****************************************************************************/
void InitGCAudio(void)
{
    AUDIO_Init(NULL);
    AUDIO_SetDSPSampleRate(AI_SAMPLERATE_48KHZ);
    AUDIO_RegisterDMACallback(AudioSwitchBuffers);
    memset(soundbuffer, 0, 8192);
    audio_dma_next_frames = AUDIO_DMA_NOMINAL_FRAMES;
    mixer_init();
}

/****************************************************************************
 * GUI CD Player Audio
 *****************************************************************************/
void audio_player_begin (void)
{
    AUDIO_StopDMA();
    IsPlaying = 0;
    audio_dma_next_frames = AUDIO_DMA_NOMINAL_FRAMES;
    mixer_init();
}

void audio_player_update (void)
{
    mixer_update_cdda_only();

    if (IsPlaying == 0 && mixer_queued_frames() >= AUDIO_PREBUFFER_FRAMES) {
       AUDIO_StopDMA();
       AudioSwitchBuffers();
    }
}

int audio_player_queued_frames (void)
{
    return mixer_queued_frames();
}

void audio_player_end (void)
{
    AUDIO_StopDMA();
    IsPlaying = 0;
    mixer_init();
}

/****************************************************************************
 * NeoCD Audio Update
 *
 * This is called on each VBL to get the next frame of audio.
 *****************************************************************************/
void update_audio(void)
{
    mixer_update_audio();

    if (IsPlaying == 0 && mixer_queued_frames() >= AUDIO_PREBUFFER_FRAMES) {
       AUDIO_StopDMA();
       AudioSwitchBuffers();
    }
}
