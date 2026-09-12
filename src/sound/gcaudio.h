/****************************************************************************
*   NeoCDRX
*   NeoGeo CD Emulator
*   NeoCD Redux - Copyright (C) 2007 softdev
****************************************************************************/
#ifndef __GCAUDIO__
#define __GCAUDIO__

#include "mixer.h"

void InitGCAudio (void);
void update_audio(void);
void audio_player_begin (void);
void audio_player_update (void);
int audio_player_queued_frames (void);
void audio_player_end (void);
void audio_vsync_sync(void);

#endif
