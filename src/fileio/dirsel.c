/****************************************************************************
* NeoGeo Directory Selector
*
* As there is no 'ROM' to speak of, use the directory as the starting point
****************************************************************************/

#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neocdrx.h"

#ifdef HW_RVL
#include <wiiuse/wpad.h>
#endif

#define PAGE_SIZE 8

char basedir[1024];
char scratchdir[1024];
char megadir[1024];
char dirbuffer[0x10000] ATTRIBUTE_ALIGN (32);

char root_dir[10];
//int have_ROM;
/****************************************************************************
* DrawDirSelector
****************************************************************************/
static void
DrawDirSelector (int maxfile, int menupos, int currsel)
{
  int i;
  int j = 158;
  int *p = (int *) dirbuffer;
  char *m;
  char display[40];

  DrawScreen ();

  for (i = menupos; i < (menupos + PAGE_SIZE) && (i < maxfile); i++)
    {
      m = (char *) p[i + 1];
      memset (display, 0, 40);
      memcpy (display, m, 32);

      if (i == currsel)
        {
          /* Same selected text colour and size as the main menu; no background bar. */
          setfgcolour (INVTEXT);
          gprint (64, j, display, TXT_DOUBLE_TRANSPARENT);
        }
      else
        {
          /* Same normal text colour and size as the main menu; no background fill. */
          setfgcolour (COLOR_BLACK);
          gprint (64, j, display, TXT_DOUBLE_TRANSPARENT);
        }

      j += 32;
    }

  ShowScreen ();
}

/****************************************************************************
* DirSelector
*
* A == Enter directory / launch game
* B == Parent directory
* X == Legacy direct directory launch
****************************************************************************/
void
DirSelector (void)
{
  int *p = (int *) dirbuffer;
  char *m;
  int maxfile, i;
  int currsel = 0;
  int menupos = 0;
  int redraw = 1;
  short joy;
  int quit = 0;
  have_ROM = 0;
  GENFILE fp;

  maxfile = p[0];

  while (!quit)
  {
     if (redraw)
     {
        DrawDirSelector (maxfile, menupos, currsel);
        redraw = 0;
     }

     joy = getMenuButtons();

     // Scroll displayed directories
     if (joy & PAD_BUTTON_DOWN)
     {
        currsel++;
        if (currsel == maxfile)
           currsel = menupos = 0;
        /*
         * Keep the cursor movement one item at a time.
         * When it passes the last visible row, scroll the window by ONE item
         * instead of jumping an entire PAGE_SIZE block.
         */
        if ((currsel - menupos) >= PAGE_SIZE)
           menupos++;

        redraw = 1;
     }

     if (joy & PAD_BUTTON_UP)
     {
        currsel--;
        if (currsel < 0)
        {
           currsel = maxfile - 1;
           menupos = currsel - PAGE_SIZE + 1;
        }

        /*
         * Same behavior in the opposite direction: move the visible window
         * up by one entry when the cursor crosses its first row.
         */
        if (currsel < menupos)
           menupos--;

        if (menupos < 0)
           menupos = 0;

        redraw = 1;
     }

     // Previous page of displayed directories
     if (joy & (PAD_TRIGGER_L | PAD_BUTTON_LEFT))
     {
        menupos -= PAGE_SIZE;
        currsel = menupos;
        if (currsel < 0)
        {
           currsel = maxfile - 1;
           menupos = currsel - PAGE_SIZE + 1;
        }

        if (menupos < 0)
           menupos = 0;

        redraw = 1;
     }

     // Next page of displayed directories
     if (joy & (PAD_TRIGGER_R | PAD_BUTTON_RIGHT))
     {
        menupos += PAGE_SIZE;
        currsel = menupos;
        if (currsel >= maxfile)
           currsel = menupos = 0;

        redraw = 1;
     }

     // Go to Next Directory / launch selected game with the standard A button
     if (joy & PAD_BUTTON_A)
     {
        strcpy (scratchdir, basedir);

        if (scratchdir[strlen (scratchdir) - 1] != '/')
           strcat (scratchdir, "/");

        m = (char *) p[currsel + 1];
        strcat (scratchdir, m);

        /*
         * Test the selected directory itself before asking GEN_getdir().
         *
         * SDgetdir() returns the NUMBER OF SUBDIRECTORIES, so a perfectly valid
         * CUE/BIN game directory containing only files returns zero.  That made
         * the old A-button path reject it.
         *
         * GEN_fopen() already contains the CUE integration: if a physical
         * IPL.TXT is absent, it attempts cue_mount_directory() and then opens
         * the virtual IPL.TXT from the data track.
         */
        sprintf(megadir,"%s/IPL.TXT",scratchdir);
        fp = GEN_fopen(megadir, "rb");

        if (fp)
        {
           GEN_fclose(fp);
           strcpy (basedir, scratchdir);
           have_ROM = 1;
           quit = 1;
        }
        else if (GEN_getdir (scratchdir))
        {
           maxfile = p[0];
           currsel = menupos = 0;
           strcpy (basedir, scratchdir);
        }
        else
        {
           /*
            * Leaf-directory fallback.
            *
            * Some valid CUE/BIN game folders do not expose IPL.TXT during
            * this early probe even though the normal game loader can mount
            * them correctly.  The old X-button direct-launch path already
            * handled that case by passing the selected directory to the
            * loader.  Make A, the standard confirm button, do the same.
            *
            * A directory with subdirectories is still entered above; only
            * a leaf directory reaches this fallback.
            */
           strcpy (basedir, scratchdir);
           have_ROM = 1;
           quit = 1;
        }

        redraw = 1;
     }

     // Go to Previous Directory
     if (joy & PAD_BUTTON_B)
     {
        if (strcmp (basedir, "/"))
        {
           strcpy (scratchdir, basedir);
           for (i = strlen (scratchdir) - 1; i >= 0; i--)
           {
              if (scratchdir[i] == '/')
              {
                 if (i == 0) strcpy (scratchdir, "/");
                 else scratchdir[i] = 0;

                 if (strcmp (scratchdir, root_dir) == 0) 
                    sprintf(scratchdir,"%s/",root_dir);

                 if (GEN_getdir (scratchdir))
                 {
                    maxfile = p[0];
                    currsel = menupos = 0;
                    strcpy (basedir, scratchdir);
                  }
                  break;
              }
           }
        }
        redraw = 1;
     }

     // Quit browser, return to device menu
     if (joy & PAD_TRIGGER_Z) {
        have_ROM = 0;
        quit = 1;
     }

     // LOAD Selected Directory
     if (joy & PAD_BUTTON_X)
     {
        /*** Set basedir to mount point ***/
        if (basedir[strlen (basedir) - 1] != '/')
           strcat (basedir, "/");

        m = (char *) p[currsel + 1];
        strcat (basedir, m);
        have_ROM = 1;
        quit = 1;
     }
  }

  /*** Remove any still held buttons ***/
  while (PAD_ButtonsHeld (0)) PAD_ScanPads();
#ifdef HW_RVL
  while (WPAD_ButtonsHeld(0)) WPAD_ScanPads();
#endif
}
