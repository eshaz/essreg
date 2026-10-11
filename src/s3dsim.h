/*
 * A 3-D effect made after the measurements of one card, for the tests of
 * the measurement (s3dmeas.c) and for ess3d measure /sim: what record
 * source 7 would return. It follows the card where it was measured and is
 * made up in between, so it shows what a report looks like, and the tests
 * check that the measurement finds each part of it again.
 *
 * Notes:
 *
 * The right channel comes in 0.22 dB higher than the left, as on the card,
 * which puts a little of the M into the S, and reaches the ADC 1.7 us after
 * the left through an all-pass, which ess3d reads as 2.1 us over 1 kHz as on
 * the card, and which puts more of the M there at high frequencies. 50h bit
 * 3 alone adds 0.047 of the S through a first-order low-pass at 550 Hz to
 * the S, 0.4 dB of bass, as on the card. With 50h bits 3 and 2 set, the S of
 * the input, (L - R) / 2, gets a boost B through a first-order high-pass at
 * 380 Hz and a first-order low-pass at 540 Hz, the poles that the card's
 * chirp runs gave, 16.8 dB at its peak at 453 Hz with 52h at 3Fh and 0.75 dB
 * less for each step below, and lagging by 4.2 us, through a first-order
 * all-pass, as the card's does. 50h bit 1, the model, makes the S from the M
 * through the same boost, 3 dB lower, and keeps only a 32nd of the input's
 * own S.
 *
 * 50h bit 0, the limit, compares the S out with a level from 54h and one
 * from 56h. Each is the register's value, 00h to FFh, over 91.2 times the M
 * in, plus a fixed part that 5Ah sets: 0.00188 of the M that the card was
 * measured with, -24 dBFS, for each step, or -36.5 dBFS at 80h. The boost's
 * gain falls while the S out is over both levels and rises while it is under
 * 54h's level made 0.5 dB higher, both at once where both hold: with ESS's
 * values and that M, the S out settles at 56h's level, +5.5 dB re M. The
 * limit follows the mean of the S out through a high-pass at 420 Hz, so it
 * hardly acts on bass, against the mean of the M through a high-pass at
 * 460 Hz with its gain at 400 Hz brought to 0 dB, both through two 2 ms
 * stages, about 4 ms late as on the card, and leaves the boost at least
 * -47 dB. The gain falls at 257.5 / (n + 1) dB a second, where n is 58h bits
 * 3:0, and rises (h + 1) times slower, where h is 58h bits 7:4: 51.5 and
 * 5.15 dB a second with ESS's 94h. Writing the registers puts the boost
 * back, as each run on the card started with it back. The path has a gain of
 * 0.5, a delay of 37 frames and noise of about 2 LSB.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef S3DSIM_H
#define S3DSIM_H

#include "esstypes.h"

#define S3DSIM_DELAY 37

struct s3dsim {
  u32 rate;
  u8 reg[6];                 // 50h, 52h, 54h, 56h, 58h, 5Ah
  double hb[3], lb[3];       // high-pass and low-pass: b0, b1, a1
  double g;                  // the boost's gain, from 52h
  double hx, hy, ly;         // the filters' states
  double env_m[2], env_s[2]; // the limit's envelopes of the M in and S out
  double db[3], dx, dy;      // its high-pass on the S out, and its state
  double gain;               // the boost's gain the limit leaves
  double k[2], d[2];         // its levels from 54h and 56h: K, and D as a mean
  double f_down, f_up;       // its steps a frame
  double lag[3];             // the boost's lag, an all-pass: a, x and y
  double mb[3], mx, my;      // the limit's high-pass on the M, its state
  double m_400;              // and its gain at 400 Hz brought to 0 dB
  double rb[3], rx, ry;      // 50h at 08h: the S's low-pass, its state
  double skew[3];            // the right channel's lag in the ADC, the same
  s16 dl[2][S3DSIM_DELAY];
  int dpos;
  u32 seed;
};

void s3dsim_reset(struct s3dsim *s, u32 rate);

// the registers 50h, 52h, 54h, 56h, 58h and 5Ah
void s3dsim_regs(struct s3dsim *s, const u8 *reg);

// n frames of 16-bit stereo through the effect, in to out
void s3dsim_run(struct s3dsim *s, const s16 *in, s16 *out, u16 n);

// the response at hz of M>M, S>S, M>S and S>M with these registers,
// relative to the effect off and without the limit: re[4] and im[4]
void s3dsim_response(const u8 *reg, u32 rate, double hz, double *re,
                     double *im);

// the gain of the limit's high-pass on the M at hz, against 400 Hz
double s3dsim_detect_m(u32 rate, double hz);

// how fast the limit moves the boost's gain with these registers, in dB a
// second: down and up
void s3dsim_speed(const u8 *reg, double *fall, double *rise);

// the limit's levels of the S out with these registers, in dB re an M in
// of amplitude m (full scale 1): under rise the boost rises, and over fall
// it falls, so where rise is the higher one it settles at fall
void s3dsim_levels(const u8 *reg, double m, double *rise, double *fall);

#endif /* S3DSIM_H */
