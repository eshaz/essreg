/*
 * What ES1869.DRV, ESS's wave driver, is doing, read from its data
 * segment. See wavestat.h.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <string.h>

#include "wavestat.h"

#define DG_FINGERPRINT 0x00D2 // "CPQB023"
#define DG_DEVICES 0x0BD2     // the first device structure
#define DG_ESS_END 0x0BD8     // ESS's data ends here
#define DEV_A1_USER 0x5D
#define DEV_A2_USER 0x100
#define DEV_BUSY 0x127
#define DEV_SIZE 0x132 // ESS's; the player's block follows
#define A1_STATE (DEV_SIZE + 0x12)
#define A1_FMT (DEV_SIZE + 0x13)
#define A1_SIZEOF 0x54

static u16 rd16(const u8 ESS_FAR *p) { return p[0] | (p[1] << 8); }

static int same(const u8 ESS_FAR *p, const char *s, unsigned n) {
  while (n--)
    if (*p++ != (u8)*s++)
      return 0;
  return 1;
}

int wavestat_parse(const u8 ESS_FAR *dg, u32 size, struct wavestat *ws) {
  u16 i, dev;

  memset(ws, 0, sizeof(*ws));
  if (size < DG_ESS_END + 16 || !same(dg + DG_FINGERPRINT, "CPQB023", 7))
    return -1;
  ws->known = 1;
  // the rebuilt driver's block right after ESS's data
  for (i = DG_ESS_END; i + 14 <= DG_ESS_END + 64 && i + 14UL <= size; i++)
    if (same(dg + i, "ESDRVFIX", 9)) {
      ws->rebuilt = 1;
      ws->version = rd16(dg + i + 10);
      ws->opts = rd16(dg + i + 12);
      break;
    }
  dev = rd16(dg + DG_DEVICES);
  if (dev && (u32)dev + DEV_SIZE + (ws->rebuilt ? A1_SIZEOF : 0) <= size) {
    ws->device = 1;
    ws->a1_user = dg[dev + DEV_A1_USER];
    ws->a2_user = dg[dev + DEV_A2_USER];
    ws->busy = dg[dev + DEV_BUSY] & 1;
    if (ws->rebuilt) {
      ws->a1_state = dg[dev + A1_STATE];
      ws->a1_fmt = dg[dev + A1_FMT];
    }
  }
  return 0;
}

static const struct {
  u16 bit;
  const char *key;
} keys[] = {
    {WAVE_OPT_A1_DEVICE, "Audio1Device"},
    {WAVE_OPT_A1_SHARED, "SharedWaveOut"},
    {WAVE_OPT_DUAL, "DualPlayback"},
};

void wavestat_text(const struct wavestat *ws, char *out) {
  int i, off = 0;

  out += strlen(out);
  if (!ws->known) {
    strcpy(out, "Wave driver:\tnot ESS's ES1869.DRV 4.04.00.1319\r\n");
    return;
  }
  if (!ws->rebuilt) {
    strcpy(out, "Wave driver:\tESS's ES1869.DRV\r\n");
  } else {
    strcpy(out, ws->opts & WAVE_OPT_READ
                    ? "Wave driver:\tthe rebuilt ES1869.DRV, settings from "
                      "SYSTEM.INI"
                    : "Wave driver:\tthe rebuilt ES1869.DRV, not enabled "
                      "yet");
    for (i = 0; i < (int)ESS_ARRAY_SIZE(keys); i++)
      if (!(ws->opts & keys[i].bit)) {
        strcat(out, off++ ? ", " : ", off: ");
        strcat(out, keys[i].key);
      }
    sprintf(out + strlen(out),
            "\r\nWave Audio 1:\tfilter %s; Audio 2: %s, filter %s\r\n",
            ws->opts & WAVE_OPT_A1_FILTER ? "in use" : "bypassed",
            ws->opts & WAVE_OPT_A2_4X ? "4x oversampling" : "not oversampled",
            ws->opts & WAVE_OPT_A2_4X       ? "bypassed (4x)"
            : ws->opts & WAVE_OPT_A2_FILTER ? "in use"
                                            : "bypassed");
  }
  if (!ws->device)
    return;
  out += strlen(out);
  if (ws->busy) {
    strcpy(out, "Channels:\tDirectSound or a DOS box has the chip\r\n");
    return;
  }
  strcpy(out, "Audio 1:\t");
  switch (ws->a1_user) {
  case 0:
    strcat(out, "free");
    break;
  case 1:
    strcat(out, ws->a1_fmt & WAVE_A1_DUAL       ? "dual playback"
                : ws->a1_state & WAVE_A1_SHARED ? "plays a second program "
                                                  "of the first device"
                                                : "plays the Audio 1 device");
    break;
  case 2:
    strcat(out, "records");
    break;
  default:
    strcat(out, "in use");
  }
  strcat(out, "\r\nAudio 2:\t");
  switch (ws->a2_user) {
  case 0:
    strcat(out, "free");
    break;
  case 1:
    strcat(out, "plays the first device");
    break;
  case 2:
    strcat(out, "dual playback");
    break;
  default:
    strcat(out, "in use");
  }
  strcat(out, "\r\n");
}
