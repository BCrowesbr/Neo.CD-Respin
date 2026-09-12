/****************************************************************************
* NeoCDRX - CUE/BIN virtual CD backend
*
* Supports:
*   - CUE + single BIN
*   - CUE + multiple BIN files
*   - TRACK AUDIO
*   - MODE1/2048
*   - MODE1/2352
*   - MODE2/2352 (2048-byte Form 1 payload)
*   - INDEX 00 / INDEX 01
*   - PREGAP
*   - quoted FILE names and relative paths
*
* The data track is exposed as a tiny read-only ISO9660 virtual filesystem.
* This lets the original NeoCDRX cdrom.c keep loading IPL.TXT / PRG / SPR /
* PCM files without knowing that they are inside a BIN image.
****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <unistd.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "cue.h"

#define CUE_MAX_TRACKS       99
#define CUE_MAX_FILES        99
#define CUE_MAX_VFILES       16
#define CUE_PATH_MAX         1024
#define ISO_SECTOR_SIZE      2048
#define AUDIO_SECTOR_SIZE    2352
#define AUDIO_RING_SIZE        (512 * 1024)
#define AUDIO_THREAD_CHUNK     (128 * 1024)
#define AUDIO_CONSUME_CACHE    (32 * 1024)
#define AUDIO_INITIAL_PRELOAD  (256 * 1024)
#define AUDIO_THREAD_STACKSIZE (16 * 1024)
#define AUDIO_THREAD_PRIO      40

/* CDDA source rate. Keep the normal path exact; do not derive clock from I/O fill. */
#define AUDIO_RATE_NOMINAL       44100

/*
 * A ring underrun can be shorter than one mixer callback. Give the producer a
 * few bounded scheduling opportunities before concealing the rest of the block.
 * This path runs only when the shared ring is actually empty.
 */
#define AUDIO_REFILL_RETRIES     4
#define AUDIO_REFILL_RETRY_US    100

enum
{
  CUE_TRACK_UNKNOWN = 0,
  CUE_TRACK_AUDIO,
  CUE_TRACK_MODE1_2048,
  CUE_TRACK_MODE1_2352,
  CUE_TRACK_MODE2_2352
};

typedef struct
{
  char path[CUE_PATH_MAX];
  long size;
} CUEFILE;

typedef struct
{
  int number;
  int type;
  int file_index;
  int index00;
  int index01;
  int pregap;
  int sector_size;
} CUETRACK;

typedef struct
{
  int used;
  unsigned int extent;
  unsigned int size;
  unsigned int pos;
} CUEVFILE;

typedef struct
{
  int mounted;
  char base_dir[CUE_PATH_MAX];
  char cue_path[CUE_PATH_MAX];

  CUEFILE files[CUE_MAX_FILES];
  int file_count;

  CUETRACK tracks[CUE_MAX_TRACKS];
  int track_count;
  int data_track;

  unsigned int root_extent;
  unsigned int root_size;

  FILE *data_fp;
  CUEVFILE vfiles[CUE_MAX_VFILES];

  FILE *audio_fp;
  int audio_track;
  long audio_start;
  long audio_end;
  long audio_pos;

  unsigned char audio_ring[AUDIO_RING_SIZE];
  unsigned int audio_ring_read;
  unsigned int audio_ring_write;
  unsigned int audio_ring_count;

  /* Diagnostic counters: RAM only, never written to storage during playback. */
  unsigned int audio_underruns;
  unsigned int audio_ring_min_count;
  unsigned int audio_retry_events;
  unsigned int audio_retry_attempts;
  unsigned int audio_retry_recovered;
  unsigned int audio_retry_failed;
  unsigned int audio_hold_callbacks;

  unsigned char audio_consume[AUDIO_CONSUME_CACHE];
  int audio_consume_pos;
  int audio_consume_len;

  unsigned char audio_thread_buffer[AUDIO_THREAD_CHUNK];
  lwp_t audio_thread;
  mutex_t audio_mutex;
  int audio_mutex_ready;
  int audio_thread_created;
  volatile int audio_thread_quit;
  volatile int audio_source_eof;
  int audio_eof;

  int audio_have_samples;
  int audio_l0, audio_r0;
  int audio_l1, audio_r1;
  unsigned int audio_phase;
} CUESTATE;

static CUESTATE cue;

/* Snapshot of the last CDDA session. Kept outside CUESTATE so cue_unmount()
 * can clear the mounted-disc state without erasing the diagnostics. */
static unsigned int last_audio_underruns;
static unsigned int last_audio_ring_min_count;
static unsigned int last_audio_retry_events;
static unsigned int last_audio_retry_attempts;
static unsigned int last_audio_retry_recovered;
static unsigned int last_audio_retry_failed;
static unsigned int last_audio_hold_callbacks;
static int last_audio_diag_valid;

static unsigned int read_le32(const unsigned char *p)
{
  return ((unsigned int)p[0]) |
         ((unsigned int)p[1] << 8) |
         ((unsigned int)p[2] << 16) |
         ((unsigned int)p[3] << 24);
}

static int msf_to_frames(const char *s)
{
  int m = 0, sec = 0, f = 0;
  if (!s)
    return 0;
  if (sscanf(s, "%d:%d:%d", &m, &sec, &f) != 3)
    return 0;
  return ((m * 60) + sec) * 75 + f;
}

static void normalize_slashes(char *s)
{
  while (*s)
  {
    if (*s == '\\')
      *s = '/';
    s++;
  }
}

static void strip_trailing_slash(char *s)
{
  int n = strlen(s);
  while (n > 0 && s[n - 1] == '/')
  {
    s[n - 1] = 0;
    n--;
  }
}

static void path_dirname(const char *path, char *out, int outsize)
{
  int i;
  int last = -1;

  strncpy(out, path, outsize - 1);
  out[outsize - 1] = 0;
  normalize_slashes(out);

  for (i = 0; out[i]; i++)
    if (out[i] == '/')
      last = i;

  if (last < 0)
  {
    strcpy(out, ".");
    return;
  }

  if (last == 0)
  {
    out[1] = 0;
    return;
  }

  out[last] = 0;
}

static void join_path(char *out, int outsize, const char *dir, const char *name)
{
  char temp[CUE_PATH_MAX];

  strncpy(temp, name, sizeof(temp) - 1);
  temp[sizeof(temp) - 1] = 0;
  normalize_slashes(temp);

  if (strstr(temp, ":/") || temp[0] == '/')
  {
    strncpy(out, temp, outsize - 1);
    out[outsize - 1] = 0;
    return;
  }

  snprintf(out, outsize, "%s/%s", dir, temp);
}

static int has_extension(const char *name, const char *ext)
{
  int ln = strlen(name);
  int le = strlen(ext);

  if (ln < le)
    return 0;

  return strcasecmp(name + ln - le, ext) == 0;
}

static int cue_find_file_in_directory(const char *directory, char *out, int outsize)
{
  DIR *d;
  struct dirent *entry;
  char dir[CUE_PATH_MAX];

  strncpy(dir, directory, sizeof(dir) - 1);
  dir[sizeof(dir) - 1] = 0;
  normalize_slashes(dir);
  strip_trailing_slash(dir);

  d = opendir(dir);
  if (!d)
    return 0;

  while ((entry = readdir(d)) != NULL)
  {
    if (entry->d_name[0] == '.')
      continue;

    if (has_extension(entry->d_name, ".cue"))
    {
      snprintf(out, outsize, "%s/%s", dir, entry->d_name);
      closedir(d);
      return 1;
    }
  }

  closedir(d);
  return 0;
}

static int parse_file_name(const char *line, char *out, int outsize)
{
  const char *p = line;
  const char *start;
  const char *end;
  int len;

  while (*p && isspace((unsigned char)*p))
    p++;

  if (strncasecmp(p, "FILE", 4) != 0)
    return 0;

  p += 4;
  while (*p && isspace((unsigned char)*p))
    p++;

  if (*p == '"')
  {
    start = ++p;
    end = strchr(start, '"');
    if (!end)
      return 0;
  }
  else
  {
    start = p;
    end = p;
    while (*end && !isspace((unsigned char)*end))
      end++;
  }

  len = end - start;
  if (len <= 0)
    return 0;
  if (len >= outsize)
    len = outsize - 1;

  memcpy(out, start, len);
  out[len] = 0;
  normalize_slashes(out);
  return 1;
}

static int parse_track_type(const char *type)
{
  if (!strcasecmp(type, "AUDIO"))
    return CUE_TRACK_AUDIO;
  if (!strcasecmp(type, "MODE1/2048"))
    return CUE_TRACK_MODE1_2048;
  if (!strcasecmp(type, "MODE1/2352"))
    return CUE_TRACK_MODE1_2352;
  if (!strcasecmp(type, "MODE2/2352"))
    return CUE_TRACK_MODE2_2352;
  return CUE_TRACK_UNKNOWN;
}

static int sector_size_for_type(int type)
{
  if (type == CUE_TRACK_MODE1_2048)
    return 2048;
  return 2352;
}

static int cue_parse(const char *cuepath)
{
  FILE *fp;
  char line[2048];
  char filename[CUE_PATH_MAX];
  char dir[CUE_PATH_MAX];
  int current_file = -1;
  int current_track = -1;

  fp = fopen(cuepath, "rb");
  if (!fp)
    return 0;

  path_dirname(cuepath, dir, sizeof(dir));

  cue.file_count = 0;
  cue.track_count = 0;
  cue.data_track = -1;

  while (fgets(line, sizeof(line), fp))
  {
    char *p = line;

    while (*p && isspace((unsigned char)*p))
      p++;

    if (!strncasecmp(p, "FILE", 4))
    {
      FILE *test;

      if (cue.file_count >= CUE_MAX_FILES)
        continue;

      if (!parse_file_name(p, filename, sizeof(filename)))
        continue;

      current_file = cue.file_count++;
      join_path(cue.files[current_file].path,
                sizeof(cue.files[current_file].path),
                dir, filename);

      test = fopen(cue.files[current_file].path, "rb");
      if (!test)
      {
        fclose(fp);
        return 0;
      }
      fseek(test, 0, SEEK_END);
      cue.files[current_file].size = ftell(test);
      fclose(test);
    }
    else if (!strncasecmp(p, "TRACK", 5))
    {
      int number;
      char type[64];

      if (current_file < 0 || cue.track_count >= CUE_MAX_TRACKS)
        continue;

      if (sscanf(p + 5, "%d %63s", &number, type) != 2)
        continue;

      current_track = cue.track_count++;
      memset(&cue.tracks[current_track], 0, sizeof(CUETRACK));
      cue.tracks[current_track].number = number;
      cue.tracks[current_track].type = parse_track_type(type);
      cue.tracks[current_track].file_index = current_file;
      cue.tracks[current_track].index00 = -1;
      cue.tracks[current_track].index01 = 0;
      cue.tracks[current_track].pregap = 0;
      cue.tracks[current_track].sector_size =
        sector_size_for_type(cue.tracks[current_track].type);

      if (cue.data_track < 0 &&
          cue.tracks[current_track].type != CUE_TRACK_AUDIO &&
          cue.tracks[current_track].type != CUE_TRACK_UNKNOWN)
        cue.data_track = current_track;
    }
    else if (!strncasecmp(p, "INDEX", 5) && current_track >= 0)
    {
      int index;
      char msf[32];

      if (sscanf(p + 5, "%d %31s", &index, msf) == 2)
      {
        if (index == 0)
          cue.tracks[current_track].index00 = msf_to_frames(msf);
        else if (index == 1)
          cue.tracks[current_track].index01 = msf_to_frames(msf);
      }
    }
    else if (!strncasecmp(p, "PREGAP", 6) && current_track >= 0)
    {
      char msf[32];
      if (sscanf(p + 6, "%31s", msf) == 1)
        cue.tracks[current_track].pregap = msf_to_frames(msf);
    }
  }

  fclose(fp);

  if (cue.track_count <= 0 || cue.data_track < 0)
    return 0;

  return 1;
}

static int data_payload_offset(int type)
{
  if (type == CUE_TRACK_MODE1_2352)
    return 16;

  /*
   * MODE2/2352 Form 1:
   * sync(12) + header(4) + subheader(8) = 24 bytes.
   */
  if (type == CUE_TRACK_MODE2_2352)
    return 24;

  return 0;
}

static int cue_read_data_sector(unsigned int lba, unsigned char *out)
{
  CUETRACK *t;
  long offset;
  int payload;

  if (!cue.mounted || cue.data_track < 0 || !cue.data_fp)
    return 0;

  t = &cue.tracks[cue.data_track];
  payload = data_payload_offset(t->type);

  offset = (long)t->index01 * t->sector_size;
  offset += (long)lba * t->sector_size;
  offset += payload;

  if (fseek(cue.data_fp, offset, SEEK_SET) != 0)
    return 0;

  return fread(out, 1, ISO_SECTOR_SIZE, cue.data_fp) == ISO_SECTOR_SIZE;
}

static void iso_clean_name(const unsigned char *src, int len,
                           char *out, int outsize)
{
  int i;
  int n = 0;

  if (len == 1 && src[0] == 0)
  {
    strcpy(out, ".");
    return;
  }

  if (len == 1 && src[0] == 1)
  {
    strcpy(out, "..");
    return;
  }

  for (i = 0; i < len && n < outsize - 1; i++)
  {
    if (src[i] == ';')
      break;
    out[n++] = (char)src[i];
  }

  while (n > 0 && out[n - 1] == '.')
    n--;

  out[n] = 0;
}

static int iso_find_in_directory(unsigned int dir_extent,
                                 unsigned int dir_size,
                                 const char *wanted,
                                 unsigned int *found_extent,
                                 unsigned int *found_size,
                                 int *found_is_dir)
{
  unsigned char sector[ISO_SECTOR_SIZE];
  unsigned int pos = 0;

  while (pos < dir_size)
  {
    unsigned int sector_index = pos / ISO_SECTOR_SIZE;
    unsigned int sector_off = pos % ISO_SECTOR_SIZE;
    unsigned int sector_base = sector_index * ISO_SECTOR_SIZE;

    if (!cue_read_data_sector(dir_extent + sector_index, sector))
      return 0;

    while (sector_off < ISO_SECTOR_SIZE &&
           sector_base + sector_off < dir_size)
    {
      unsigned char *rec = sector + sector_off;
      int reclen = rec[0];

      if (reclen == 0)
        break;

      if (reclen >= 34)
      {
        int namelen = rec[32];
        char name[256];

        if (33 + namelen <= reclen)
        {
          iso_clean_name(rec + 33, namelen, name, sizeof(name));

          if (!strcasecmp(name, wanted))
          {
            *found_extent = read_le32(rec + 2);
            *found_size = read_le32(rec + 10);
            *found_is_dir = (rec[25] & 2) ? 1 : 0;
            return 1;
          }
        }
      }

      sector_off += reclen;
      pos = sector_base + sector_off;
    }

    pos = sector_base + ISO_SECTOR_SIZE;
  }

  return 0;
}

static int iso_resolve(const char *path,
                       unsigned int *extent,
                       unsigned int *size,
                       int *is_dir)
{
  char temp[CUE_PATH_MAX];
  char *p;
  unsigned int current_extent;
  unsigned int current_size;
  int current_is_dir = 1;

  strncpy(temp, path, sizeof(temp) - 1);
  temp[sizeof(temp) - 1] = 0;
  normalize_slashes(temp);

  p = temp;
  while (*p == '/')
    p++;

  current_extent = cue.root_extent;
  current_size = cue.root_size;

  if (*p == 0)
  {
    *extent = current_extent;
    *size = current_size;
    *is_dir = 1;
    return 1;
  }

  while (*p)
  {
    char component[256];
    int n = 0;
    unsigned int next_extent;
    unsigned int next_size;
    int next_is_dir;

    while (*p && *p != '/' && n < (int)sizeof(component) - 1)
      component[n++] = *p++;
    component[n] = 0;

    while (*p == '/')
      p++;

    if (!iso_find_in_directory(current_extent, current_size, component,
                               &next_extent, &next_size, &next_is_dir))
      return 0;

    current_extent = next_extent;
    current_size = next_size;
    current_is_dir = next_is_dir;

    if (*p && !current_is_dir)
      return 0;
  }

  *extent = current_extent;
  *size = current_size;
  *is_dir = current_is_dir;
  return 1;
}

static int cue_read_iso_bytes(unsigned int extent,
                              unsigned int file_size,
                              unsigned int pos,
                              unsigned char *dst,
                              unsigned int bytes)
{
  unsigned char sector[ISO_SECTOR_SIZE];
  unsigned int done = 0;

  if (pos >= file_size)
    return 0;

  if (bytes > file_size - pos)
    bytes = file_size - pos;

  while (done < bytes)
  {
    unsigned int absolute = pos + done;
    unsigned int sec = absolute / ISO_SECTOR_SIZE;
    unsigned int off = absolute % ISO_SECTOR_SIZE;
    unsigned int copy = ISO_SECTOR_SIZE - off;

    if (copy > bytes - done)
      copy = bytes - done;

    if (!cue_read_data_sector(extent + sec, sector))
      break;

    memcpy(dst + done, sector + off, copy);
    done += copy;
  }

  return done;
}

static int cue_path_to_iso_name(const char *filename, char *out, int outsize)
{
  char temp[CUE_PATH_MAX];
  char base[CUE_PATH_MAX];
  int base_len;

  if (!cue.mounted)
    return 0;

  strncpy(temp, filename, sizeof(temp) - 1);
  temp[sizeof(temp) - 1] = 0;
  normalize_slashes(temp);

  if (!strncasecmp(temp, "cue:/", 5))
  {
    strncpy(out, temp + 5, outsize - 1);
    out[outsize - 1] = 0;
    return 1;
  }

  strncpy(base, cue.base_dir, sizeof(base) - 1);
  base[sizeof(base) - 1] = 0;
  normalize_slashes(base);
  strip_trailing_slash(base);

  base_len = strlen(base);

  if (strncasecmp(temp, base, base_len) != 0)
    return 0;

  if (temp[base_len] != 0 && temp[base_len] != '/')
    return 0;

  strncpy(out, temp + base_len, outsize - 1);
  out[outsize - 1] = 0;

  while (out[0] == '/')
    memmove(out, out + 1, strlen(out));

  return 1;
}

int cue_is_mounted(void)
{
  return cue.mounted;
}

void cue_vfcloseall(void)
{
  memset(cue.vfiles, 0, sizeof(cue.vfiles));
}

void cue_audio_stop(void)
{
  /* Preserve a completed/current session for the selector diagnostic line. */
  if (cue.audio_track > 0 || cue.audio_have_samples)
  {
    last_audio_underruns = cue.audio_underruns;
    last_audio_ring_min_count = cue.audio_ring_min_count;
    last_audio_retry_events = cue.audio_retry_events;
    last_audio_retry_attempts = cue.audio_retry_attempts;
    last_audio_retry_recovered = cue.audio_retry_recovered;
    last_audio_retry_failed = cue.audio_retry_failed;
    last_audio_hold_callbacks = cue.audio_hold_callbacks;
    last_audio_diag_valid = 1;
  }

  /* Stop the producer before touching its FILE or shared ring buffer. */
  if (cue.audio_thread_created)
  {
    cue.audio_thread_quit = 1;
    LWP_JoinThread(cue.audio_thread, NULL);
    cue.audio_thread_created = 0;
  }

  if (cue.audio_fp)
    fclose(cue.audio_fp);

  cue.audio_fp = NULL;

  if (cue.audio_mutex_ready)
  {
    LWP_MutexDestroy(cue.audio_mutex);
    cue.audio_mutex_ready = 0;
  }

  cue.audio_track = 0;
  cue.audio_start = 0;
  cue.audio_end = 0;
  cue.audio_pos = 0;
  cue.audio_ring_read = 0;
  cue.audio_ring_write = 0;
  cue.audio_ring_count = 0;
  cue.audio_consume_pos = 0;
  cue.audio_consume_len = 0;
  cue.audio_thread_quit = 0;
  cue.audio_source_eof = 0;
  cue.audio_eof = 0;
  cue.audio_have_samples = 0;
  cue.audio_phase = 0;
}

void cue_unmount(void)
{
  cue_audio_stop();

  if (cue.data_fp)
    fclose(cue.data_fp);

  cue.data_fp = NULL;
  cue_vfcloseall();
  memset(&cue, 0, sizeof(cue));
  cue.data_track = -1;
}

int cue_mount_directory(const char *directory)
{
  char cuepath[CUE_PATH_MAX];
  unsigned char pvd[ISO_SECTOR_SIZE];
  CUETRACK *data;
  int fi;

  cue_unmount();

  if (!cue_find_file_in_directory(directory, cuepath, sizeof(cuepath)))
    return 0;

  strncpy(cue.base_dir, directory, sizeof(cue.base_dir) - 1);
  cue.base_dir[sizeof(cue.base_dir) - 1] = 0;
  normalize_slashes(cue.base_dir);
  strip_trailing_slash(cue.base_dir);

  strncpy(cue.cue_path, cuepath, sizeof(cue.cue_path) - 1);
  cue.cue_path[sizeof(cue.cue_path) - 1] = 0;

  if (!cue_parse(cuepath))
  {
    cue_unmount();
    return 0;
  }

  data = &cue.tracks[cue.data_track];
  fi = data->file_index;

  cue.data_fp = fopen(cue.files[fi].path, "rb");
  if (!cue.data_fp)
  {
    cue_unmount();
    return 0;
  }

  cue.mounted = 1;

  if (!cue_read_data_sector(16, pvd))
  {
    cue_unmount();
    return 0;
  }

  if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0)
  {
    cue_unmount();
    return 0;
  }

  cue.root_extent = read_le32(pvd + 156 + 2);
  cue.root_size = read_le32(pvd + 156 + 10);

  if (!cue.root_extent || !cue.root_size)
  {
    cue_unmount();
    return 0;
  }

  return 1;
}

int cue_is_virtual_handle(u32 fp)
{
  return (fp & CUE_VIRTUAL_HANDLE_MASK) != 0;
}

static int virtual_index(u32 fp)
{
  int index;

  if (!cue_is_virtual_handle(fp))
    return -1;

  index = (int)(fp & 0xff) - 1;
  if (index < 0 || index >= CUE_MAX_VFILES)
    return -1;

  if (!cue.vfiles[index].used)
    return -1;

  return index;
}

u32 cue_vfopen(const char *filename, const char *mode)
{
  char iso_name[CUE_PATH_MAX];
  unsigned int extent, size;
  int is_dir;
  int i;

  if (!cue.mounted || !filename || !mode)
    return 0;

  if (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'))
    return 0;

  if (!cue_path_to_iso_name(filename, iso_name, sizeof(iso_name)))
    return 0;

  if (!iso_resolve(iso_name, &extent, &size, &is_dir) || is_dir)
    return 0;

  for (i = 0; i < CUE_MAX_VFILES; i++)
  {
    if (!cue.vfiles[i].used)
    {
      cue.vfiles[i].used = 1;
      cue.vfiles[i].extent = extent;
      cue.vfiles[i].size = size;
      cue.vfiles[i].pos = 0;
      return CUE_VIRTUAL_HANDLE_MASK | (u32)(i + 1);
    }
  }

  return 0;
}

u32 cue_vfread(char *buffer, int block, int length, u32 fp)
{
  int index;
  unsigned int bytes;
  unsigned int done;

  index = virtual_index(fp);
  if (index < 0 || block <= 0 || length <= 0)
    return 0;

  bytes = (unsigned int)block * (unsigned int)length;

  done = cue_read_iso_bytes(cue.vfiles[index].extent,
                            cue.vfiles[index].size,
                            cue.vfiles[index].pos,
                            (unsigned char *)buffer,
                            bytes);

  cue.vfiles[index].pos += done;
  return done / block;
}

int cue_vfseek(u32 fp, int where, int whence)
{
  int index;
  long pos;

  index = virtual_index(fp);
  if (index < 0)
    return -1;

  if (whence == SEEK_SET)
    pos = where;
  else if (whence == SEEK_CUR)
    pos = (long)cue.vfiles[index].pos + where;
  else if (whence == SEEK_END)
    pos = (long)cue.vfiles[index].size + where;
  else
    return -1;

  if (pos < 0)
    pos = 0;
  if ((unsigned long)pos > cue.vfiles[index].size)
    pos = cue.vfiles[index].size;

  cue.vfiles[index].pos = (unsigned int)pos;
  return 0;
}

int cue_vftell(u32 fp)
{
  int index = virtual_index(fp);
  if (index < 0)
    return -1;
  return cue.vfiles[index].pos;
}

int cue_vfclose(u32 fp)
{
  int index = virtual_index(fp);
  if (index < 0)
    return 0;

  memset(&cue.vfiles[index], 0, sizeof(CUEVFILE));
  return 1;
}

int cue_audio_first_track(void)
{
  int i;
  int first = 0;

  for (i = 0; i < cue.track_count; i++)
  {
    if (cue.tracks[i].type == CUE_TRACK_AUDIO)
    {
      if (!first || cue.tracks[i].number < first)
        first = cue.tracks[i].number;
    }
  }

  return first;
}

int cue_audio_last_track(void)
{
  int i;
  int last = 0;

  for (i = 0; i < cue.track_count; i++)
  {
    if (cue.tracks[i].type == CUE_TRACK_AUDIO &&
        cue.tracks[i].number > last)
      last = cue.tracks[i].number;
  }

  return last;
}

int cue_audio_track_exists(int track)
{
  int i;

  for (i = 0; i < cue.track_count; i++)
  {
    if (cue.tracks[i].number == track &&
        cue.tracks[i].type == CUE_TRACK_AUDIO)
      return 1;
  }

  return 0;
}

static int find_track_index(int number);
static long track_start_byte(int ti);
static long track_end_byte(int ti);

unsigned long cue_audio_track_frames_44100(int track)
{
  int ti;
  long start;
  long end;

  ti = find_track_index(track);
  if (ti < 0)
    return 0;

  if (cue.tracks[ti].type != CUE_TRACK_AUDIO)
    return 0;

  start = track_start_byte(ti);
  end = track_end_byte(ti);

  if (end <= start)
    return 0;

  /* CDDA stereo 16-bit PCM = 4 bytes per native 44.1 kHz frame. */
  return (unsigned long)((end - start) / 4);
}

static int find_track_index(int number)
{
  int i;
  for (i = 0; i < cue.track_count; i++)
    if (cue.tracks[i].number == number)
      return i;
  return -1;
}

static long track_start_byte(int ti)
{
  CUETRACK *t = &cue.tracks[ti];
  return (long)t->index01 * t->sector_size;
}

static long track_end_byte(int ti)
{
  CUETRACK *t = &cue.tracks[ti];
  long end = cue.files[t->file_index].size;

  if (ti + 1 < cue.track_count &&
      cue.tracks[ti + 1].file_index == t->file_index)
  {
    CUETRACK *next = &cue.tracks[ti + 1];
    int frame = (next->index00 >= 0) ? next->index00 : next->index01;
    long next_start = (long)frame * next->sector_size;

    if (next_start > 0 && next_start < end)
      end = next_start;
  }

  return end;
}

/*
 * CDDA producer/consumer pipeline
 * -------------------------------
 *
 * The storage thread is the ONLY code that calls fread() while a track is
 * playing.  The emulation/audio thread consumes bytes already resident in
 * RAM.  A 512 KiB ring buffer holds almost three seconds of raw CDDA, which
 * absorbs normal FAT/SD/USB latency spikes without stalling the emulator.
 */
static unsigned int audio_ring_free_locked(void)
{
  return AUDIO_RING_SIZE - cue.audio_ring_count;
}

static unsigned int audio_ring_write_bytes(const unsigned char *src,
                                           unsigned int bytes)
{
  unsigned int first;
  unsigned int room;

  if (!cue.audio_mutex_ready || bytes == 0)
    return 0;

  LWP_MutexLock(cue.audio_mutex);

  room = audio_ring_free_locked();
  if (bytes > room)
    bytes = room;

  first = AUDIO_RING_SIZE - cue.audio_ring_write;
  if (first > bytes)
    first = bytes;

  if (first)
    memcpy(cue.audio_ring + cue.audio_ring_write, src, first);

  if (bytes > first)
    memcpy(cue.audio_ring, src + first, bytes - first);

  cue.audio_ring_write = (cue.audio_ring_write + bytes) % AUDIO_RING_SIZE;
  cue.audio_ring_count += bytes;

  LWP_MutexUnlock(cue.audio_mutex);
  return bytes;
}

static unsigned int audio_ring_read_bytes(unsigned char *dst,
                                          unsigned int bytes)
{
  unsigned int first;

  if (!cue.audio_mutex_ready || bytes == 0)
    return 0;

  LWP_MutexLock(cue.audio_mutex);

  if (bytes > cue.audio_ring_count)
    bytes = cue.audio_ring_count;

  first = AUDIO_RING_SIZE - cue.audio_ring_read;
  if (first > bytes)
    first = bytes;

  if (first)
    memcpy(dst, cue.audio_ring + cue.audio_ring_read, first);

  if (bytes > first)
    memcpy(dst + first, cue.audio_ring, bytes - first);

  cue.audio_ring_read = (cue.audio_ring_read + bytes) % AUDIO_RING_SIZE;
  cue.audio_ring_count -= bytes;

  /* Ignore the normal drain after the producer has reached true track EOF. */
  if (!cue.audio_source_eof && cue.audio_ring_count < cue.audio_ring_min_count)
    cue.audio_ring_min_count = cue.audio_ring_count;

  LWP_MutexUnlock(cue.audio_mutex);
  return bytes;
}

static unsigned int audio_ring_free(void)
{
  unsigned int free_bytes;

  if (!cue.audio_mutex_ready)
    return 0;

  LWP_MutexLock(cue.audio_mutex);
  free_bytes = audio_ring_free_locked();
  LWP_MutexUnlock(cue.audio_mutex);

  return free_bytes;
}

/*
 * Initial synchronous reserve.  This happens only when a track starts, never
 * in cue_audio_render_48k().  Once playback begins, all file I/O is done by
 * audio_reader_thread().
 */
static int audio_initial_preload(void)
{
  unsigned int total = 0;

  while (total < AUDIO_INITIAL_PRELOAD && cue.audio_pos < cue.audio_end)
  {
    long remain = cue.audio_end - cue.audio_pos;
    unsigned int want = AUDIO_THREAD_CHUNK;
    size_t got;

    if (want > AUDIO_INITIAL_PRELOAD - total)
      want = AUDIO_INITIAL_PRELOAD - total;
    if ((long)want > remain)
      want = (unsigned int)remain;

    if (want == 0)
      break;

    got = fread(cue.audio_thread_buffer, 1, want, cue.audio_fp);
    if (got == 0)
      break;

    audio_ring_write_bytes(cue.audio_thread_buffer, (unsigned int)got);
    cue.audio_pos += (long)got;
    total += (unsigned int)got;

    if (got < want)
      break;
  }

  if (cue.audio_pos >= cue.audio_end)
    cue.audio_source_eof = 1;

  return total > 0;
}

static void *audio_reader_thread(void *arg)
{
  (void)arg;

  while (!cue.audio_thread_quit)
  {
    unsigned int room;
    unsigned int want;
    long remain;
    size_t got;

    if (!cue.audio_fp)
      break;

    remain = cue.audio_end - cue.audio_pos;
    if (remain <= 0)
    {
      cue.audio_source_eof = 1;
      break;
    }

    room = audio_ring_free();

    /* Do not start a storage transaction unless a full chunk fits. */
    if (room < AUDIO_THREAD_CHUNK)
    {
      usleep(2000);
      continue;
    }

    want = AUDIO_THREAD_CHUNK;
    if ((long)want > remain)
      want = (unsigned int)remain;

    got = fread(cue.audio_thread_buffer, 1, want, cue.audio_fp);
    if (got == 0)
    {
      cue.audio_source_eof = 1;
      break;
    }

    /* fread() happens without the mutex.  Only this short RAM copy is locked. */
    audio_ring_write_bytes(cue.audio_thread_buffer, (unsigned int)got);
    cue.audio_pos += (long)got;

    if (got < want || cue.audio_pos >= cue.audio_end)
    {
      cue.audio_source_eof = 1;
      break;
    }
  }

  return NULL;
}

/*
 * Pull up to 32 KiB from the shared ring into a consumer-only cache.  The
 * resampler then reads thousands of samples without taking the mutex again.
 */
static int audio_refill_consumer(void)
{
  unsigned int got;
  int retry;

  got = audio_ring_read_bytes(cue.audio_consume, AUDIO_CONSUME_CACHE);

  if (got == 0 && !cue.audio_source_eof)
    cue.audio_retry_events++;

  /*
   * A producer fread() can finish just after the consumer observes an empty
   * ring.  The old code immediately converted that sub-millisecond starvation
   * into a hold for the remainder of the whole 800-frame mixer callback.
   *
   * Retry only on a real temporary starvation and keep the wait strictly
   * bounded. usleep() yields the Broadway CPU, allowing the LWP producer to
   * finish its pending I/O. Normal playback never enters this loop.
   */
  for (retry = 0;
       got == 0 && !cue.audio_source_eof && retry < AUDIO_REFILL_RETRIES;
       retry++)
  {
    cue.audio_retry_attempts++;
    usleep(AUDIO_REFILL_RETRY_US);
    got = audio_ring_read_bytes(cue.audio_consume, AUDIO_CONSUME_CACHE);
  }

  if (retry > 0 && got > 0)
    cue.audio_retry_recovered++;

  cue.audio_consume_pos = 0;
  cue.audio_consume_len = (int)got;

  /* One diagnostic event only after all bounded recovery attempts failed. */
  if (got == 0 && !cue.audio_source_eof)
  {
    cue.audio_retry_failed++;
    cue.audio_underruns++;
  }

  return got > 0;
}

static int audio_get_byte(unsigned char *v)
{
  if (cue.audio_consume_pos >= cue.audio_consume_len)
  {
    if (!audio_refill_consumer())
      return 0;
  }

  *v = cue.audio_consume[cue.audio_consume_pos++];
  return 1;
}

static int audio_get_frame(int *left, int *right)
{
  unsigned char b0, b1, b2, b3;
  short l, r;

  if (!audio_get_byte(&b0) ||
      !audio_get_byte(&b1) ||
      !audio_get_byte(&b2) ||
      !audio_get_byte(&b3))
    return 0;

  /* Raw CDDA in the tested CUE/BIN set is little-endian signed 16-bit PCM. */
  l = (short)((unsigned int)b0 | ((unsigned int)b1 << 8));
  r = (short)((unsigned int)b2 | ((unsigned int)b3 << 8));

  *left = l;
  *right = r;
  return 1;
}

int cue_audio_start(int track)
{
  int ti;
  CUETRACK *t;
  long start;
  long end;

  cue_audio_stop();

  if (!cue.mounted)
    return 0;

  ti = find_track_index(track);
  if (ti < 0)
    return 0;

  t = &cue.tracks[ti];
  if (t->type != CUE_TRACK_AUDIO)
    return 0;

  cue.audio_fp = fopen(cue.files[t->file_index].path, "rb");
  if (!cue.audio_fp)
    return 0;

  start = track_start_byte(ti);
  end = track_end_byte(ti);

  if (end <= start || fseek(cue.audio_fp, start, SEEK_SET) != 0)
  {
    cue_audio_stop();
    return 0;
  }

  cue.audio_track = track;
  cue.audio_start = start;
  cue.audio_end = end;
  cue.audio_pos = start;
  cue.audio_ring_read = 0;
  cue.audio_ring_write = 0;
  cue.audio_ring_count = 0;
  cue.audio_underruns = 0;
  cue.audio_ring_min_count = AUDIO_RING_SIZE;
  cue.audio_retry_events = 0;
  cue.audio_retry_attempts = 0;
  cue.audio_retry_recovered = 0;
  cue.audio_retry_failed = 0;
  cue.audio_hold_callbacks = 0;
  cue.audio_consume_pos = 0;
  cue.audio_consume_len = 0;
  cue.audio_thread_quit = 0;
  cue.audio_source_eof = 0;
  cue.audio_eof = 0;
  cue.audio_phase = 0;

  if (LWP_MutexInit(&cue.audio_mutex, 0) < 0)
  {
    cue_audio_stop();
    return 0;
  }
  cue.audio_mutex_ready = 1;

  /* Establish a RAM reserve before playback begins. */
  if (!audio_initial_preload())
  {
    cue_audio_stop();
    return 0;
  }

  /* Baseline for the minimum-buffer diagnostic after the initial reserve. */
  cue.audio_ring_min_count = cue.audio_ring_count;

  if (!audio_get_frame(&cue.audio_l0, &cue.audio_r0) ||
      !audio_get_frame(&cue.audio_l1, &cue.audio_r1))
  {
    cue_audio_stop();
    return 0;
  }

  cue.audio_have_samples = 1;

  /*
   * The producer has a modest priority and performs only file I/O + RAM copy.
   * No fread() occurs on the emulation/audio consumer path after this point.
   */
  if (!cue.audio_source_eof)
  {
    if (LWP_CreateThread(&cue.audio_thread,
                         audio_reader_thread,
                         NULL,
                         NULL,
                         AUDIO_THREAD_STACKSIZE,
                         AUDIO_THREAD_PRIO) < 0)
    {
      cue_audio_stop();
      return 0;
    }
    cue.audio_thread_created = 1;
  }

  return 1;
}

int cue_audio_render_48k(char *outbuffer, int frames)
{
  short *out = (short *)outbuffer;
  int i;

  if (!cue.audio_fp || !cue.audio_have_samples || cue.audio_eof)
  {
    memset(outbuffer, 0, frames * 4);
    return 0;
  }

  /*
   * Exact 44.1 kHz -> 48 kHz rational sample-and-hold resampling.
   * Storage-buffer occupancy is deliberately NOT used as a clock source.
   */

  for (i = 0; i < frames; i++)
  {
    *out++ = (short)cue.audio_l0;
    *out++ = (short)cue.audio_r0;

    cue.audio_phase += AUDIO_RATE_NOMINAL;

    while (cue.audio_phase >= 48000)
    {
      cue.audio_phase -= 48000;
      cue.audio_l0 = cue.audio_l1;
      cue.audio_r0 = cue.audio_r1;

      if (!audio_get_frame(&cue.audio_l1, &cue.audio_r1))
      {
        /*
         * A temporary producer underrun must NOT be interpreted as track end.
         * Repeat the last valid sample for the remainder of this callback;
         * with a 512 KiB ring this should only happen on an extreme I/O stall.
         */
        if (!cue.audio_source_eof)
        {
          int j;
          cue.audio_hold_callbacks++;
          for (j = i + 1; j < frames; j++)
          {
            *out++ = (short)cue.audio_l0;
            *out++ = (short)cue.audio_r0;
          }
          return frames;
        }

        cue.audio_eof = 1;
        i++;

        if (i < frames)
          memset(out, 0, (frames - i) * 4);

        return i;
      }
    }
  }

  return frames;
}

unsigned int cue_audio_debug_underruns(void)
{
  return cue.audio_underruns;
}

unsigned int cue_audio_debug_min_ring_bytes(void)
{
  return cue.audio_ring_min_count;
}

unsigned int cue_audio_debug_ring_bytes(void)
{
  unsigned int count = 0;

  if (!cue.audio_mutex_ready)
    return 0;

  LWP_MutexLock(cue.audio_mutex);
  count = cue.audio_ring_count;
  LWP_MutexUnlock(cue.audio_mutex);

  return count;
}

unsigned int cue_audio_debug_last_underruns(void)
{
  return last_audio_underruns;
}

unsigned int cue_audio_debug_last_min_ring_bytes(void)
{
  return last_audio_ring_min_count;
}

unsigned int cue_audio_debug_last_retry_events(void)
{
  return last_audio_retry_events;
}

unsigned int cue_audio_debug_last_retry_attempts(void)
{
  return last_audio_retry_attempts;
}

unsigned int cue_audio_debug_last_retry_recovered(void)
{
  return last_audio_retry_recovered;
}

unsigned int cue_audio_debug_last_retry_failed(void)
{
  return last_audio_retry_failed;
}

unsigned int cue_audio_debug_last_hold_callbacks(void)
{
  return last_audio_hold_callbacks;
}

int cue_audio_debug_last_valid(void)
{
  return last_audio_diag_valid;
}

int cue_audio_ended(void)
{
  return cue.audio_eof;
}
