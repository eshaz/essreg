/*
 * What ES1869.DRV, ESS's wave driver, is doing, read from its data
 * segment: the SYSTEM.INI settings of the rebuilt driver and who has each
 * audio channel. essctl shows it on the Device information page.
 *
 * Notes:
 *
 * The data segment is ESS's 4.04.00.1319 layout: "CPQB023" at 00D2h, the
 * device list at 0BD2h. The rebuilt driver adds "ESDRVFIX" after ESS's
 * data, then its version and settings (src/es1869/fixdata.asm), and its
 * Audio 1 player's state after ESS's 132h bytes of the device structure
 * (src/es1869/fix.inc). See docs/AUDIO1.md.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef WAVESTAT_H
#define WAVESTAT_H

#include "esstypes.h"

// es_opts of the rebuilt driver
#define WAVE_OPT_A1_DEVICE 0x0001 // Audio1Device
#define WAVE_OPT_A1_SHARED 0x0002 // SharedWaveOut
#define WAVE_OPT_A1_FILTER 0x0004 // Audio1Filter
#define WAVE_OPT_DUAL 0x0008      // DualPlayback
#define WAVE_OPT_A2_4X 0x0100     // Audio2Oversampling
#define WAVE_OPT_A2_FILTER 0x0200 // Audio2Filter
#define WAVE_OPT_READ 0x8000      // SYSTEM.INI was read

// the Audio 1 player's A1_STATE and A1_FMT
#define WAVE_A1_OPEN 0x01
#define WAVE_A1_SHARED 0x08
#define WAVE_A1_DUAL 0x80

struct wavestat {
  int known;   // ESS's data layout was found
  int rebuilt; // the rebuilt driver: version and opts are set
  u16 version;
  u16 opts;
  int device;  // a device structure was found
  u8 a1_user;  // 0 free, 1 the player, 2 wave-in
  u8 a2_user;  // 0 free, 1 wave-out, 2 the player
  u8 busy;     // the VxD gave the chip to DirectSound or a DOS box
  u8 a1_state; // the player's, WAVE_A1_*
  u8 a1_fmt;
};

// ES1869.DRV's data segment, size bytes at dg: 0, or -1 if it isn't ESS's
// layout
int wavestat_parse(const u8 ESS_FAR *dg, u32 size, struct wavestat *ws);

// the Device information lines about it, each ending in "\r\n"
void wavestat_text(const struct wavestat *ws, char *out);

#ifndef ESS_HOST
// the running driver's: 0, 1 if it isn't loaded, or -1 if it isn't ESS's
int wave_live(struct wavestat *ws);
#endif

#endif /* WAVESTAT_H */
