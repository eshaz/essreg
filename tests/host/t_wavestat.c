/*
 * t_wavestat tests what essctl reads from ES1869.DRV's data segment: the
 * rebuilt driver's settings and who has each audio channel, against the
 * real data segments of ESS's driver and the rebuilt one.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "wavestat.h"

#define DEV 0x2000
#define A1 (DEV + 0x132)

static u8 dg[0x3000];

// the data segment (segment 7) of an NE file, into dg: its length
static long load_dgroup(const char *path) {
  static u8 file[0x20000];
  FILE *f = fopen(path, "rb");
  long n, ne, seg, off, len;
  int shift;

  if (!f)
    return -1;
  n = (long)fread(file, 1, sizeof(file), f);
  fclose(f);
  ne = file[0x3C] | (file[0x3D] << 8);
  seg = ne + (file[ne + 0x22] | (file[ne + 0x23] << 8)) + 6 * 8;
  shift = file[ne + 0x32] | (file[ne + 0x33] << 8);
  off = (long)(file[seg] | (file[seg + 1] << 8)) << shift;
  len = file[seg + 2] | (file[seg + 3] << 8);
  if (off + len > n || len > (long)sizeof(dg))
    return -1;
  memset(dg, 0, sizeof(dg));
  memcpy(dg, file + off, len);
  return len;
}

static void device(u8 a1_user, u8 a2_user, u8 state, u8 fmt) {
  dg[0xBD2] = DEV & 0xFF;
  dg[0xBD3] = DEV >> 8;
  dg[DEV + 0x5D] = a1_user;
  dg[DEV + 0x100] = a2_user;
  dg[A1 + 0x12] = state;
  dg[A1 + 0x13] = fmt;
}

static u16 *opts(void) {
  u16 i;

  for (i = 0xBD8; i < 0xC20; i++)
    if (!memcmp(dg + i, "ESDRVFIX", 9))
      return (u16 *)(dg + i + 12);
  return 0;
}

int main(int argc, char **argv) {
  struct wavestat ws;
  static char text[600];

  if (argc < 3) {
    printf("usage: t_wavestat ESS.DRV REBUILT.DRV\n");
    return 2;
  }

  // ESS's driver: its layout, no settings, the channels still shown
  CHECK(load_dgroup(argv[1]) >= 0xBD8);
  device(2, 1, 0, 0);
  CHECK_EQ(wavestat_parse(dg, sizeof(dg), &ws), 0);
  CHECK(ws.known && !ws.rebuilt && ws.device);
  text[0] = 0;
  wavestat_text(&ws, text);
  CHECK(strstr(text, "Wave driver:\tESS's ES1869.DRV\r\n") != 0);
  CHECK(strstr(text, "Audio 1:\trecords\r\n") != 0);
  CHECK(strstr(text, "Audio 2:\tplays the first device\r\n") != 0);

  // the rebuilt one: its version and default settings, not enabled yet
  CHECK(load_dgroup(argv[2]) > 0xBD8);
  CHECK(opts() != 0);
  CHECK_EQ(wavestat_parse(dg, sizeof(dg), &ws), 0);
  CHECK(ws.rebuilt);
  CHECK_EQ(ws.version, 1);
  CHECK_EQ(ws.opts, WAVE_OPT_A1_DEVICE | WAVE_OPT_A1_SHARED | WAVE_OPT_DUAL);
  CHECK(!ws.device);
  text[0] = 0;
  wavestat_text(&ws, text);
  CHECK(strstr(text, "the rebuilt ES1869.DRV, not enabled yet\r\n") != 0);
  CHECK(strstr(text, "\nAudio 1:") == 0);

  // read from SYSTEM.INI, with keys off, the player in each of its states
  *opts() = WAVE_OPT_READ | WAVE_OPT_A1_SHARED | WAVE_OPT_A2_4X;
  device(1, 0, WAVE_A1_OPEN, 0x03);
  wavestat_parse(dg, sizeof(dg), &ws);
  text[0] = 0;
  wavestat_text(&ws, text);
  CHECK(strstr(text, "settings from SYSTEM.INI, off: Audio1Device, "
                     "DualPlayback\r\n") != 0);
  CHECK(strstr(text, "filter bypassed; Audio 2: 4x oversampling, filter "
                     "bypassed (4x)") != 0);
  CHECK(strstr(text, "Audio 1:\tplays the Audio 1 device\r\n") != 0);
  CHECK(strstr(text, "Audio 2:\tfree\r\n") != 0);
  device(1, 1, WAVE_A1_OPEN | WAVE_A1_SHARED, 0x03);
  wavestat_parse(dg, sizeof(dg), &ws);
  text[0] = 0;
  wavestat_text(&ws, text);
  CHECK(strstr(text, "plays a second program of the first device") != 0);
  device(1, 2, WAVE_A1_OPEN, 0x83);
  wavestat_parse(dg, sizeof(dg), &ws);
  text[0] = 0;
  wavestat_text(&ws, text);
  CHECK(strstr(text, "Audio 1:\tdual playback\r\nAudio 2:\tdual playback") !=
        0);

  // the VxD gave the chip away
  dg[DEV + 0x127] = 1;
  wavestat_parse(dg, sizeof(dg), &ws);
  text[0] = 0;
  wavestat_text(&ws, text);
  CHECK(strstr(text, "DirectSound or a DOS box has the chip") != 0);

  // not ESS's layout, or too short
  dg[0xD2] = 'X';
  CHECK_EQ(wavestat_parse(dg, sizeof(dg), &ws), -1);
  text[0] = 0;
  wavestat_text(&ws, text);
  CHECK(strstr(text, "not ESS's ES1869.DRV 4.04.00.1319") != 0);
  CHECK_EQ(wavestat_parse(dg, 0x100, &ws), -1);

  return CHECK_DONE("t_wavestat");
}
