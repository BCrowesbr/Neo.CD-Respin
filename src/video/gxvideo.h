#ifndef _GXVIDEO_H_
#define _GXVIDEO_H_

#define GAME_TVMODE_240P 0
#define GAME_TVMODE_480I 1

void SetGameTVMode(int mode);
int  GetGameTVMode(void);
const char *GetGameTVModeName(void);

void StartGX(void);
void ResumeGX(void);
void update_video(int width, int height, char *vbuffer);

#endif
