/*
 * The WAV header, peak levels and test tone of esfmrec (see fmrec.h).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <string.h>

#include "fmrec.h"

// --- WAV header -------------------------------------------------------------

static void put16(u8 *p, u16 v) {
  p[0] = (u8)v;
  p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v) {
  put16(p, (u16)v);
  put16(p + 2, (u16)(v >> 16));
}

void fmrec_wav_header(u8 *hdr, u32 rate, u32 data_bytes) {
  memcpy(hdr, "RIFF", 4);
  put32(hdr + 4, 36 + data_bytes);
  memcpy(hdr + 8, "WAVEfmt ", 8);
  put32(hdr + 16, 16);
  put16(hdr + 20, 1); // PCM
  put16(hdr + 22, 2); // stereo
  put32(hdr + 24, rate);
  put32(hdr + 28, rate * 4);
  put16(hdr + 32, 4);  // bytes per frame
  put16(hdr + 34, 16); // bits per sample
  memcpy(hdr + 36, "data", 4);
  put32(hdr + 40, data_bytes);
}

// --- levels -----------------------------------------------------------------

void fmrec_peaks(const s16 *pcm, u16 frames, u16 *left, u16 *right) {
  u16 i, l = 0, r = 0, a;

  for (i = 0; i < frames; i++) {
    a = (u16)(pcm[2 * i] < 0 ? -(s32)pcm[2 * i] : pcm[2 * i]);
    if (a > l)
      l = a;
    a = (u16)(pcm[2 * i + 1] < 0 ? -(s32)pcm[2 * i + 1] : pcm[2 * i + 1]);
    if (a > r)
      r = a;
  }
  *left = l;
  *right = r;
}

// log2(1 + k/16) in 1/4096
static const u16 log2_tab[17] = {0,    358,  696,  1016, 1319, 1607,
                                 1882, 2145, 2396, 2637, 2869, 3092,
                                 3307, 3514, 3715, 3908, 4096};

int fmrec_db10(u16 peak) {
  s32 l2;
  u16 x, k;
  int n = 15;

  if (!peak)
    return -999;
  if (peak >= 32768)
    return 0;
  while (!(peak >> n))
    n--;
  // the mantissa, with its leading one at bit 15: table and interpolation
  x = (u16)(peak << (15 - n));
  k = (x >> 11) & 15;
  l2 = log2_tab[k] + ((s32)(log2_tab[k + 1] - log2_tab[k]) * (x & 2047) >> 11);
  // 20 log10(2) = 6.0206 dB per octave below full scale
  l2 = (s32)(15 - n) * 4096 - l2;
  return -(int)((l2 * 602 + 20480) / 40960);
}

// --- test tone --------------------------------------------------------------

// a quarter of a sine in 64 steps
static const s16 sine_tab[65] = {
    0,     804,   1608,  2410,  3212,  4011,  4808,  5602,  6393,  7179,  7962,
    8739,  9512,  10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151,
    16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170,
    23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510,
    28898, 29268, 29621, 29956, 30273, 30571, 30852, 31113, 31356, 31580, 31785,
    31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757, 32767};

// sine of the top 8 bits of a 32-bit phase
static s16 sine(u32 phase) {
  u16 i = (u16)((phase >> 24) & 255), k = i & 63;

  switch (i >> 6) {
  case 0:
    return sine_tab[k];
  case 1:
    return sine_tab[64 - k];
  case 2:
    return (s16)-sine_tab[k];
  }
  return (s16)-sine_tab[64 - k];
}

void fmrec_tone(s16 *pcm, u16 frames, u32 *phase) {
  u16 i;

  for (i = 0; i < frames; i++) {
    pcm[2 * i] = (s16)(sine(phase[0]) / 2);
    pcm[2 * i + 1] = (s16)(sine(phase[1]) / 2);
    // 2^32 * 1000 / 49716 for 1 kHz; u32 is wider than 32 bits on a host
    phase[0] = (phase[0] + 86390041UL) & 0xFFFFFFFFUL;
    phase[1] = (phase[1] + 43195021UL) & 0xFFFFFFFFUL;
  }
}
