/*
 * The tones and markers of sb16test and essprobe, see sbtsound.h.
 *
 * (c)
 * 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL
 * Version 3.0
 */

#include <math.h>
#include <string.h>

#include "sbtsound.h"

#define PI 3.14159265358979
#define TAB SND_TAB

static s16 sine_tab[TAB];

static const char dtmf_syms[] = "123A456B789C*0#D";
static const u16 dtmf_lo[4] = {697, 770, 852, 941};
static const u16 dtmf_hi[4] = {1209, 1336, 1477, 1633};

void snd_init(void) {
  int i;

  for (i = 0; i < TAB; i++)
    sine_tab[i] = (s16)floor(32767.0 * sin(2 * PI * i / TAB) + 0.5);
}

s16 snd_sine(u16 i) { return sine_tab[i & (TAB - 1)]; }

double snd_fit_hz(double hz, u16 frames, double rate) {
  double cycles = floor(hz * frames / rate + 0.5);

  if (cycles < 1)
    cycles = 1;
  return cycles * rate / frames;
}

void snd_tone(struct snd_tone *t, double hz_l, double hz_r, double rate,
              long amp) {
  t->inc[0] = (u32)(hz_l / rate * 4294967296.0);
  t->inc[1] = (u32)(hz_r / rate * 4294967296.0);
  t->ph[0] = t->ph[1] = 0;
  t->amp = amp;
}

s16 snd_next(struct snd_tone *t, int c) {
  s16 v;

  if (t->inc[c] == 0)
    return 0;
  v = (s16)(((long)sine_tab[(u16)(t->ph[c] >> 22) & (TAB - 1)] * t->amp) >> 15);
  t->ph[c] = (t->ph[c] + t->inc[c]) & 0xFFFFFFFFUL;
  return v;
}

void snd_fill(snd_poke poke, u32 addr, u16 frames, u8 fmt, struct snd_tone *t) {
  static u8 buf[256];
  u16 i = 0, f, w;
  int c, chans = (fmt & SND_FST) ? 2 : 1;
  s16 s;

  for (f = 0; f < frames; f++) {
    for (c = 0; c < chans; c++) {
      s = snd_next(t, c);
      w = (u16)s;
      if (fmt & SND_F16) {
        if (!(fmt & SND_FSG))
          w ^= 0x8000;
        buf[i++] = (u8)w;
        buf[i++] = (u8)(w >> 8);
      } else {
        w >>= 8;
        if (!(fmt & SND_FSG))
          w ^= 0x80;
        buf[i++] = (u8)w;
      }
    }
    if (i >= sizeof(buf) - 4) {
      poke(addr, buf, i);
      addr += i;
      i = 0;
    }
  }
  if (i)
    poke(addr, buf, i);
}

void snd_fill_byte(snd_poke poke, u32 addr, u16 n, u8 v) {
  static u8 buf[128];
  u16 k;

  memset(buf, v, sizeof(buf));
  while (n) {
    k = n > sizeof(buf) ? (u16)sizeof(buf) : n;
    poke(addr, buf, k);
    addr += k;
    n -= k;
  }
}

u16 snd_whole_cycles(double hz, double rate, u16 most, u16 unit) {
  double len = rate / hz, cycles;
  u16 step = 1, n = (u16)len;

  if (len == n) // the cycles that make whole blocks
    while ((u32)step * n % unit)
      step++;
  cycles = floor(most / len / step) * step;
  if (cycles < step)
    cycles = step;
  n = (u16)floor(cycles * len + 0.5);
  return (u16)(n - n % unit);
}

u16 snd_dtmf(snd_poke poke, u32 addr, const char *syms, double rate, u16 on) {
  static u8 buf[128];
  struct snd_tone t;
  u16 sym = (u16)(on * 5U / 3U), n = 0, i, j, k;
  const char *s, *p;

  for (s = syms; *s; s++) {
    p = strchr(dtmf_syms, *s);
    k = p != NULL ? (u16)(p - dtmf_syms) : 0;
    snd_tone(&t, dtmf_lo[k / 4], dtmf_hi[k % 4], rate, 6000);
    for (i = 0; i < sym; i = (u16)(i + j)) {
      for (j = 0; j < sizeof(buf) && i + j < sym; j++)
        buf[j] =
            (u8)(i + j < on ? 128 + ((snd_next(&t, 0) + snd_next(&t, 1)) >> 8)
                            : 128);
      poke(addr + n, buf, j);
      n = (u16)(n + j);
    }
  }
  return n;
}
