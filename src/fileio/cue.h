/****************************************************************************
* NeoCDRX - CUE/BIN virtual CD backend
****************************************************************************/
#ifndef __NEO_CUE_BIN__
#define __NEO_CUE_BIN__

#include <gccore.h>

#define CUE_VIRTUAL_HANDLE_MASK 0x40000000u

int cue_is_mounted(void);
int cue_mount_directory(const char *directory);
void cue_unmount(void);

int cue_is_virtual_handle(u32 fp);
u32 cue_vfopen(const char *filename, const char *mode);
u32 cue_vfread(char *buffer, int block, int length, u32 fp);
int cue_vfseek(u32 fp, int where, int whence);
int cue_vftell(u32 fp);
int cue_vfclose(u32 fp);
void cue_vfcloseall(void);

int cue_audio_first_track(void);
int cue_audio_last_track(void);
int cue_audio_track_exists(int track);
unsigned long cue_audio_track_frames_44100(int track);
int cue_audio_start(int track);
void cue_audio_stop(void);
int cue_audio_render_48k(char *outbuffer, int frames);
unsigned int cue_audio_debug_underruns(void);
unsigned int cue_audio_debug_min_ring_bytes(void);
unsigned int cue_audio_debug_ring_bytes(void);
unsigned int cue_audio_debug_last_underruns(void);
unsigned int cue_audio_debug_last_min_ring_bytes(void);
unsigned int cue_audio_debug_last_retry_events(void);
unsigned int cue_audio_debug_last_retry_attempts(void);
unsigned int cue_audio_debug_last_retry_recovered(void);
unsigned int cue_audio_debug_last_retry_failed(void);
unsigned int cue_audio_debug_last_hold_callbacks(void);
int cue_audio_debug_last_valid(void);
int cue_audio_ended(void);

#endif
