/****************************************************************************
*   NeoCDRX
*   NeoGeo CD Emulator
*   NeoCD Redux - Copyright (C) 2007 softdev
****************************************************************************/

/****************************************************************************
* Generic File I/O
*
* This module attempts to provide a single interface for file I/O.
****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include "fileio.h"
#include "cue.h"

static GENHANDLER genhandler;

static void GEN_parent_directory(const char *filename, char *out, int outsize)
{
  int i;
  if (!filename || !out || outsize <= 0) return;
  strncpy(out, filename, outsize - 1);
  out[outsize - 1] = 0;
  for (i = (int)strlen(out) - 1; i >= 0; --i)
  {
    if (out[i] == '/' || out[i] == '\\')
    {
      out[i] = 0;
      return;
    }
  }
  out[0] = 0;
}


/****************************************************************************
* GEN_SetHandler
*
* Call BEFORE using any of the other functions
****************************************************************************/
void
GEN_SetHandler (GENHANDLER * g)
{
  memcpy (&genhandler, g, sizeof (GENHANDLER));
}

/****************************************************************************
* GEN_fopen
*
* Passthrough fopen
****************************************************************************/
u32
GEN_fopen (const char *filename, const char *mode)
{
  u32 fp;
  char dir[1024];

  if (genhandler.gen_fopen == NULL)
    return 0;			/*** NULL - no file or handler ***/

  /* Keep ordinary extracted-file / MP3 behavior as the first choice. */
  fp = (genhandler.gen_fopen) (filename, mode);
  if (fp)
    return fp;

  /* A mounted CUE exposes its data track as a read-only ISO9660 filesystem. */
  if (cue_is_mounted())
  {
    /* Never remount on a miss while a disc is already active. */
    return cue_vfopen(filename, mode);
  }

  /* First miss in a game directory: look for a .cue there and mount it. */
  GEN_parent_directory(filename, dir, sizeof(dir));
  if (dir[0] && cue_mount_directory(dir))
    return cue_vfopen(filename, mode);

  return 0;
}

/****************************************************************************
* GEN_fread
*
* Passthrough fread
****************************************************************************/
u32
GEN_fread (char *buffer, int block, int length, u32 fp)
{
  if (cue_is_virtual_handle(fp))
    return cue_vfread(buffer, block, length, fp);

  if (genhandler.gen_fread == NULL)
    return 0;

  return (genhandler.gen_fread) (buffer, block, length, fp);
}

/****************************************************************************
* GEN_fwrite
*
* Passthrough fwrite
****************************************************************************/
u32
GEN_fwrite (char *buffer, int block, int length, u32 fp)
{
  if (genhandler.gen_fwrite == NULL)
    return 0;

  return (genhandler.gen_fwrite) (buffer, block, length, fp);
}

/****************************************************************************
* GEN_fclose
*
* Passthrough fclose
****************************************************************************/
int
GEN_fclose (u32 fp)
{
  if (cue_is_virtual_handle(fp))
    return cue_vfclose(fp);

  if (genhandler.gen_fclose == NULL)
    return 0;

  return (genhandler.gen_fclose) (fp);
}

/****************************************************************************
* GEN_fseek
*
* Passthrough fseek
****************************************************************************/
int
GEN_fseek (u32 fp, int where, int whence)
{
  if (cue_is_virtual_handle(fp))
    return cue_vfseek(fp, where, whence);

  if (genhandler.gen_fseek == NULL)
    return 0;

  return (genhandler.gen_fseek) (fp, where, whence);
}

/****************************************************************************
* GEN_ftell
*
* Passthrough ftell
****************************************************************************/
int
GEN_ftell (u32 fp)
{
  if (cue_is_virtual_handle(fp))
    return cue_vftell(fp);

  if (genhandler.gen_ftell == NULL)
    return -1;

  return (genhandler.gen_ftell) (fp);
}

/****************************************************************************
* GEN_fcloseall
***************************************************************************/
void
GEN_fcloseall (void)
{
  /* A new game is being selected: fully release the previous CUE/BIN disc.
     This stops CDDA, joins the producer thread, closes BIN files and clears
     all virtual handles so the next GEN_fopen() can mount the new game. */
  cue_unmount();

  if (genhandler.gen_fcloseall == NULL)
    return;

  (genhandler.gen_fcloseall) ();
}


/****************************************************************************
* GEN_fcloseall
***************************************************************************/
int
GEN_getdir (char *dir)
{
  if (genhandler.gen_getdir == NULL)
    return 0;

  return (genhandler.gen_getdir) (dir);
}

/****************************************************************************
* GEN_mount
***************************************************************************/
void
GEN_mount (void)
{
  if (genhandler.gen_mount == NULL)
    return;

  (genhandler.gen_mount) ();
}

