/*
 * The tones and markers of sb16test and essprobe: sines from a table, a
 *
 * frequency with whole cycles in a buffer, and the DTMF markers that a
 *
 * recording of the card's output holds, which tools/recan.py finds.
 *
 *
 * Notes:
 *
 * The samples go to the DMA area through the program's poke.
 * Nothing here
 * runs at interrupt time. Keep the code C89, with 16-bit ints;
 * on the host
 * a u32 is wider, so the phases are cut to 32 bits.
 *
 * (c)
 * 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL
 * Version 3.0
 */

#ifndef SBTSOUND_H
#define SBTSOUND_H

#include "esstypes.h"

// the samples' format
#define SND_F16 0x01 // 16-bit
#define SND_FST 0x02 // stereo
#define SND_FSG 0x04 // signed

// the pitch of every tone: a cycle is a whole number of frames at 11025,
// 22050 and 44100 Hz (25, 50 and 100)
#define SND_HZ 441.0

// a recorded sound's line, which tools/recan.py reads: its marker, when the
// marker began, the parts of the sound (" on-off" each), what it expects
#define SND_RECORDED "recorded: marker %s at %lu us, sound%s us, %s expected"

struct snd_tone {
  u32 inc[2], ph[2];
  long amp;
};

typedef void (*snd_poke)(u32 addr, const u8 *src, u16 n);

#define SND_TAB 1024 // the sine table's length

// the sine table
void snd_init(void);

// its entry i (mod SND_TAB), full scale 32767
s16 snd_sine(u16 i);

// a frequency near hz with a whole number of cycles in frames at rate
double snd_fit_hz(double hz, u16 frames, double rate);

// hz_l on the left (or mono), hz_r on the right, 0 for none, at amp of
// full scale (0-32767)
void snd_tone(struct snd_tone *t, double hz_l, double hz_r, double rate,
              long amp);

// the next sample of channel c
s16 snd_next(struct snd_tone *t, int c);

// frames of the tone at addr, in the format fmt
void snd_fill(snd_poke poke, u32 addr, u16 frames, u8 fmt, struct snd_tone *t);

// n bytes of v at addr
void snd_fill_byte(snd_poke poke, u32 addr, u16 n, u8 v);

// frames near most, a multiple of unit (the blocks), that hold whole
// cycles of hz at rate: exactly when rate is a multiple of hz
u16 snd_whole_cycles(double hz, double rate, u16 most, u16 unit);

// a marker's symbols at addr, 8-bit unsigned mono at rate: each symbol on
// frames of its two tones and then 2/3 as long of silence; its bytes
u16 snd_dtmf(snd_poke poke, u32 addr, const char *syms, double rate, u16 on);

#endif /* SBTSOUND_H */
