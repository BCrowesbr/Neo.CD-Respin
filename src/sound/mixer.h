#ifndef __NGCMIXER__
#define __NGCMIXER__

typedef struct
{
  int head;
  int tail;
} MIXER;

void mixer_update_audio (void);
void mixer_update_cdda_only (void);
void mixer_init (void);
//void ngcMixAudio (Uint8 * dst, Uint8 * src, int len, int volume);
void ngcMixAudio (u8 * dst, u8 * src, int len, int volume);
int mixer_getaudio (u8 * outbuffer, int length);
unsigned int mixer_queued_frames(void);
void mixer_set(float sound_vol, float music_vol,
               float sound_l, float sound_m, float sound_h,
               float music_l, float music_m, float music_h);

extern char mp3buffer[8192];
#endif
