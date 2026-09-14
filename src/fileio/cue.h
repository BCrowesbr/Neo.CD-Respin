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

/* Disc/TOC helpers for the experimental hardware-level CD controller. */
int cue_disc_first_track(void);
int cue_disc_last_track(void);
int cue_disc_track_is_data(int track);
unsigned int cue_disc_track_lba(int track);
unsigned int cue_disc_leadout_lba(void);
int cue_disc_track_from_lba(unsigned int lba);

int cue_audio_first_track(void);
int cue_audio_last_track(void);
int cue_audio_track_exists(int track);
unsigned long cue_audio_track_frames_44100(int track);
int cue_audio_start(int track);
void cue_audio_stop(void);
int cue_audio_render_48k(char *outbuffer, int frames);
int cue_audio_ended(void);

#endif
