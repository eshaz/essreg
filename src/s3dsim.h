/*
 * A 3-D effect made after the measurement of one card, for the tests of
 * the measurement (s3dmeas.c) and for ess3d measure /sim: what record
 * source 7 would return. It follows the card where it was measured and is
 * made up in between, so it shows what a report looks like, and the tests
 * check that the measurement finds each part of it again.
 *
 * Notes:
 *
 * The right channel comes in 0.2 dB lower than the left, as on the card,
 * which puts a little of the M into the S. With 50h bits 3 and 2 set, the
 * S of the input, (L - R) / 2, gets a boost B through a first-order
 * high-pass at 200 Hz and a first-order low-pass at 1 kHz, 16.7 dB at its
 * peak with 52h at 3Fh and 0.75 dB less for each step below. 50h bit 1,
 * the model, makes the S from the M through the same boost, 3 dB lower,
 * and keeps only a 32nd of the input's own S.
 *
 * 50h bit 0, the limit, holds the S out under the M by T dB, where T goes
 * from -5.6 dB with 54h at 00h through +5.5 at 8Fh to +9.8 at FFh, in
 * straight lines between them. The boost's gain falls at 260 x 2^(-58h /
 * 65) dB a second while the S is over that, 53 with ESS's 94h, and rises
 * at the same rate while it's under, or at a 16th of it with 58h bit 7 set.
 * Writing the registers puts the boost back, as each run on the card
 * started with it back. 56h and 5Ah do nothing. The path has a gain of
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
  u8 reg[6];                // 50h, 52h, 54h, 56h, 58h, 5Ah
  double hb[3], lb[3];      // high-pass and low-pass: b0, b1, a1
  double g;                 // the boost's gain, from 52h
  double hx, hy, ly;        // the filters' states
  double env_m, env_s;      // the limit's envelopes of the M in and S out
  double gain;              // the boost's gain the limit leaves
  double lim, f_down, f_up; // its level against the M, its steps a frame
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

// what the limit does with these registers: the level it holds the S out
// at against the M in, in dB, and its fall and rise in dB a second
void s3dsim_limit(const u8 *reg, double *level, double *fall, double *rise);

#endif /* S3DSIM_H */
