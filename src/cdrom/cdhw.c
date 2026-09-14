/****************************************************************************
* Neo.CD Respin 1.2 - experimental native BIOS CD hardware path
*
* v0.2 fixes the mechanism packet protocol to match current NeoCD-Libretro:
* - 5-byte command and response packets
* - 10 nibble positions
* - FF0165 values 0/1/2/3 control pointer/strobe
* - response pointer starts at nibble 9 with strobe high
****************************************************************************/

#include <gccore.h>
#include <string.h>

#include "neocdrx.h"
#include "cue.h"
#include "cdaudio.h"
#include "cdhw.h"

#define CD_IDLE      0x00
#define CD_PLAYING   0x10
#define CD_SEEKING   0x20
#define CD_SCANNING  0x30
#define CD_PAUSED    0x40
#define CD_STOPPED   0x90
#define CD_END       0xC0

#define LC_DECI      0x20
#define LC_DTEI      0x40
#define LC_DTBSY     0x08

typedef struct
{
  u8 regptr;
  u8 r[16];
  u8 w[16];

  u8 command[5];
  u8 response[5];
  int command_ptr;
  int response_ptr;
  int strobe;

  u8 status;
  unsigned int position_lba;
  int current_track;
} CDHWState;

static CDHWState cd;

static u8 to_bcd(unsigned int v)
{
  return (u8)(((v / 10) << 4) | (v % 10));
}

static unsigned int from_bcd(u8 v)
{
  return ((v >> 4) * 10) + (v & 0x0F);
}

static void lba_to_msf(unsigned int lba, u8 *m, u8 *s, u8 *f)
{
  unsigned int frame = lba + 150;

  *m = to_bcd(frame / (60 * 75));
  *s = to_bcd((frame % (60 * 75)) / 75);
  *f = to_bcd(frame % 75);
}

static unsigned int msf_to_lba(u8 m, u8 s, u8 f)
{
  unsigned int frame =
    (from_bcd(m) * 60 * 75) +
    (from_bcd(s) * 75) +
    from_bcd(f);

  return frame >= 150 ? frame - 150 : 0;
}

static u8 packet_checksum(const u8 *packet)
{
  int i;
  int sum = 0;

  for (i = 0; i < 4; i++)
  {
    sum += (packet[i] >> 4);
    sum += (packet[i] & 0x0F);
  }

  sum += (packet[4] >> 4);
  sum += 5;

  return (u8)(~sum) & 0x0F;
}

static void set_packet_checksum(u8 *packet)
{
  packet[4] = (packet[4] & 0xF0) | packet_checksum(packet);
}

static void query_info(u8 sub,
                       u8 *r0, u8 *r1, u8 *r2, u8 *r3, u8 *r4)
{
  u8 m, s, f;
  int track;

  track = cue_disc_track_from_lba(cd.position_lba);
  if (!track)
    track = cue_disc_first_track();

  switch (sub)
  {
    case 0x00:
      lba_to_msf(cd.position_lba, &m, &s, &f);
      *r0 = cd.status;
      *r1 = m;
      *r2 = s;
      *r3 = f;
      *r4 = cue_disc_track_is_data(track) ? 0x40 : 0x00;
      break;

    case 0x01:
    {
      unsigned int start = cue_disc_track_lba(track);
      unsigned int rel = cd.position_lba >= start ?
                         cd.position_lba - start : 0;

      lba_to_msf(rel, &m, &s, &f);
      *r0 = cd.status | 0x01;
      *r1 = m;
      *r2 = s;
      *r3 = f;
      *r4 = cue_disc_track_is_data(track) ? 0x40 : 0x00;
      break;
    }

    case 0x02:
      *r0 = cd.status | 0x02;
      *r1 = to_bcd(track);
      *r2 = 0x01;
      *r3 = 0x00;
      *r4 = cue_disc_track_is_data(track) ? 0x40 : 0x00;
      break;

    case 0x03:
      lba_to_msf(cue_disc_leadout_lba(), &m, &s, &f);
      *r0 = cd.status | 0x03;
      *r1 = m;
      *r2 = s;
      *r3 = f;
      *r4 = 0x00;
      break;

    case 0x04:
      *r0 = cd.status | 0x04;
      *r1 = to_bcd(cue_disc_first_track());
      *r2 = to_bcd(cue_disc_last_track());
      *r3 = 0x00;
      *r4 = 0x00;
      break;

    case 0x05:
    {
      int requested = from_bcd(cd.command[2]);

      lba_to_msf(cue_disc_track_lba(requested), &m, &s, &f);

      *r0 = cd.status | 0x05;
      *r1 = m;
      *r2 = s;
      *r3 = f | (cue_disc_track_is_data(requested) ? 0x80 : 0x00);
      *r4 = (u8)(cd.command[2] << 4);
      break;
    }

    case 0x06:
      if (cd.position_lba >= cue_disc_leadout_lba())
        cd.status = CD_END;

      *r0 = cd.status | 0x06;
      *r1 = 0x00;
      *r2 = 0x00;
      *r3 = 0x00;
      *r4 = cue_disc_track_is_data(track) ? 0x40 : 0x00;
      break;

    case 0x07:
      *r0 = cd.status | 0x07;
      *r1 = 0x02;
      *r2 = 0x00;
      *r3 = 0x00;
      *r4 = 0x00;
      break;

    default:
      *r0 = cd.status;
      *r1 = 0x00;
      *r2 = 0x00;
      *r3 = 0x00;
      *r4 = 0x00;
      break;
  }
}

static void process_command(void)
{
  u8 r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0;
  int track;

  if ((cd.command[4] & 0x0F) != packet_checksum(cd.command))
  {
    cd.response[0] = cd.status;
    cd.response[1] = 0x00;
    cd.response[2] = 0x00;
    cd.response[3] = 0x00;
    cd.response[4] = 0x00;
    set_packet_checksum(cd.response);
    return;
  }

  if ((cd.status == CD_IDLE) && cue_is_mounted())
    cd.status = CD_STOPPED;

  switch (cd.command[0])
  {
    case 0x00:
      cd.response[0] = (cd.response[0] & 0x0F) | cd.status;
      break;

    case 0x10:
      cdda_stop();
      cd.status = CD_IDLE;
      cd.response[0] = cd.status;
      cd.response[1] = 0x00;
      cd.response[2] = 0x00;
      cd.response[3] = 0x00;
      cd.response[4] = 0x00;
      break;

    case 0x20:
      query_info(cd.command[1] & 0x0F, &r0, &r1, &r2, &r3, &r4);
      cd.response[0] = r0;
      cd.response[1] = r1;
      cd.response[2] = r2;
      cd.response[3] = r3;
      cd.response[4] = r4;
      break;

    case 0x30:
      cd.position_lba =
        msf_to_lba(cd.command[1], cd.command[2], cd.command[3]);

      track = cue_disc_track_from_lba(cd.position_lba);
      if (!track)
        track = cue_audio_first_track();

      cd.current_track = track;

      if (cue_audio_track_exists(track))
        cdda_play(track);

      cd.status = CD_PLAYING;
      cd.response[0] = cd.status | 0x02;
      cd.response[1] = to_bcd(track);
      cd.response[2] = 0x00;
      cd.response[3] = 0x00;
      cd.response[4] = 0x00;
      break;

    case 0x40:
      cdda_stop();
      cd.status = CD_PAUSED;
      cd.response[0] = CD_SEEKING;
      cd.response[1] = 0x00;
      cd.response[2] = 0x00;
      cd.response[3] = 0x00;
      cd.response[4] = 0x00;
      break;

    case 0x50:
      cd.response[0] = cd.status;
      break;

    case 0x60:
      cdda_pause();
      cd.status = CD_PAUSED;
      cd.response[0] = cd.status;
      break;

    case 0x70:
      cdda_resume();
      cd.status = CD_PLAYING;
      cd.response[0] = cd.status;
      break;

    case 0x80:
      cd.position_lba += 30;
      if (cue_disc_leadout_lba() &&
          cd.position_lba >= cue_disc_leadout_lba())
        cd.position_lba = cue_disc_leadout_lba() - 1;

      cd.status = CD_PLAYING;
      cd.response[0] = CD_SCANNING;
      break;

    case 0x90:
      cd.position_lba = cd.position_lba > 30 ?
                        cd.position_lba - 30 : 0;

      cd.status = CD_PLAYING;
      cd.response[0] = CD_SCANNING;
      break;

    case 0xB0:
      track = from_bcd(cd.command[1]);
      cd.current_track = track;
      cd.position_lba = cue_disc_track_lba(track);

      if (cue_audio_track_exists(track))
        cdda_play(track);

      cd.status = CD_PLAYING;
      cd.response[0] = cd.status | 0x02;
      cd.response[1] = to_bcd(track);
      cd.response[2] = 0x00;
      cd.response[3] = 0x00;
      cd.response[4] = 0x00;
      break;

    case 0x02:
    case 0x13:
    case 0x23:
    case 0x33:
    case 0x43:
    case 0x53:
    case 0x63:
    case 0xE2:
      cd.response[0] = cd.status;
      cd.response[1] = 0x00;
      cd.response[2] = 0x00;
      cd.response[3] = 0x00;
      cd.response[4] = 0x00;
      break;

    default:
      cd.response[0] = cd.status;
      cd.response[1] = 0x00;
      cd.response[2] = 0x00;
      cd.response[3] = 0x00;
      cd.response[4] = 0x00;
      break;
  }

  set_packet_checksum(cd.response);
}

void cdhw_reset(void)
{
  memset(&cd, 0, sizeof(cd));

  cd.r[1] = 0xFF;
  cd.w[8] = 0x30;
  cd.w[9] = 0x09;

  cd.command_ptr = 0;
  cd.response_ptr = 9;
  cd.strobe = 1;

  cd.status = cue_is_mounted() ? CD_STOPPED : CD_IDLE;
  cd.current_track = cue_disc_first_track();
  cd.position_lba = cd.current_track ?
                    cue_disc_track_lba(cd.current_track) : 0;

  memset(cd.command, 0, sizeof(cd.command));
  memset(cd.response, 0, sizeof(cd.response));
  set_packet_checksum(cd.response);
}

int cdhw_handles_byte(unsigned int address)
{
  switch (address & 0xFFFF)
  {
    case 0x0101:
    case 0x0103:
    case 0x0161:
    case 0x0163:
    case 0x0165:
    case 0x0181:
      return 1;

    default:
      return 0;
  }
}

u8 cdhw_read8(unsigned int address)
{
  u8 value = 0xFF;

  switch (address & 0xFFFF)
  {
    case 0x0101:
      return cd.regptr;

    case 0x0103:
      switch (cd.regptr & 0x0F)
      {
        case 0x00: value = 0x00; break;
        case 0x01: value = cd.r[1]; break;
        case 0x02: value = cd.r[2]; break;

        case 0x03:
          value = cd.r[3] & 0x0F;
          value |= (cd.r[1] & LC_DTEI) ? 0x00 : 0xF0;
          break;

        case 0x04: value = cd.r[4]; break;
        case 0x05: value = cd.r[5]; break;
        case 0x06: value = cd.r[6]; break;
        case 0x07: value = cd.r[7]; break;
        case 0x08: value = cd.w[12]; break;
        case 0x09: value = cd.w[13]; break;
        case 0x0A: value = cd.w[8]; break;
        case 0x0B: value = cd.w[9]; break;
        case 0x0C: value = cd.r[12]; break;
        case 0x0D: value = cd.r[13]; break;
        case 0x0E: value = cd.r[14]; break;

        case 0x0F:
          value = cd.r[15];
          cd.r[1] &= ~LC_DECI;
          break;

        default:
          value = 0;
          break;
      }

      if (cd.regptr)
        cd.regptr = (cd.regptr & 0xF0) | ((cd.regptr + 1) & 0x0F);

      return value;

    case 0x0161:
      if (cd.response_ptr & 1)
        value = (cd.response[cd.response_ptr >> 1] & 0x0F);
      else
        value = (cd.response[cd.response_ptr >> 1] >> 4);

      value |= (cd.strobe << 4);
      return value;

    default:
      return 0xFF;
  }
}

void cdhw_write8(unsigned int address, u8 value)
{
  switch (address & 0xFFFF)
  {
    case 0x0101:
      cd.regptr = value;
      break;

    case 0x0103:
      switch (cd.regptr & 0x0F)
      {
        case 0x00: cd.w[0] = value; break;
        case 0x01: cd.w[1] = value; break;
        case 0x02: cd.w[2] = value; break;
        case 0x03: cd.w[3] = value; break;
        case 0x04: cd.w[4] = value; break;
        case 0x05: cd.w[5] = value; break;

        case 0x06:
          cd.w[6] = value;
          if (cd.w[1] & 0x02)
            cd.r[1] &= ~LC_DTBSY;
          break;

        case 0x07:
          cd.w[7] = value;
          cd.r[1] |= LC_DTEI;
          break;

        case 0x08: cd.w[8] = value; break;
        case 0x09: cd.w[9] = value; break;
        case 0x0A: cd.w[10] = value; break;
        case 0x0B: cd.w[11] = value; break;
        case 0x0C: cd.w[12] = value; break;
        case 0x0D: cd.w[13] = value; break;
        case 0x0E: cd.w[14] = value; break;
        case 0x0F: cd.w[15] = value; break;

        default:
          break;
      }

      if (cd.regptr)
        cd.regptr = (cd.regptr & 0xF0) | ((cd.regptr + 1) & 0x0F);
      break;

    case 0x0163:
      if (cd.command_ptr & 1)
        cd.command[cd.command_ptr >> 1] =
          (cd.command[cd.command_ptr >> 1] & 0xF0) | (value & 0x0F);
      else
        cd.command[cd.command_ptr >> 1] =
          (cd.command[cd.command_ptr >> 1] & 0x0F) | ((value & 0x0F) << 4);
      break;

    case 0x0165:
      switch (value & 0x03)
      {
        case 0x00:
          break;

        case 0x01:
          cd.command_ptr = (cd.command_ptr + 1) % 10;

          if (!cd.command_ptr)
            process_command();
          break;

        case 0x02:
          cd.strobe = 0;
          cd.response_ptr = (cd.response_ptr + 1) % 10;
          break;

        case 0x03:
          cd.strobe = 1;
          break;
      }
      break;

    case 0x0181:
      cd.command_ptr = 0;
      cd.response_ptr = 9;
      cd.strobe = 1;
      break;

    default:
      break;
  }
}
