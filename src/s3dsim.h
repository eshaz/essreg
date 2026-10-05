/*
 * A made-up 3-D effect for the tests of the measurement (s3dmeas.c) and
 * for ess3d measure /sim: what record source 7 would return if the
 * Spatializer worked this way. It shows what a report looks like, and
 * lets the tests check that the measurement finds each part again. It is
 * not a model of the real chip.
 *
 * Notes:
 *
 * With 50h bits 3 and 2 set, the S of the input, (L - R) / 2, gets a
 * boost through a high-pass at 150 + 10 x (54h bits 6:0) Hz, followed by a
 * 4 kHz low-pass while 56h bit 4 is set, scaled by 2 x 52h / 63. 50h bit 1
 * adds the M through an all-pass at 1 kHz to the S (width made from
 * mono), 50h bit 0 holds the boost under 0.7 times the M (a limit, with 5
 * ms attack and 200 ms release), and 5Ah bit 0 raises the M by 1 dB. 58h
 * does nothing. The path has a gain of 0.5, a delay of 37 frames and
 * noise of about 2 LSB.
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
  u8 reg[6];                  // 50h, 52h, 54h, 56h, 58h, 5Ah
  double hb[5], lb[3], ab[3]; // high-pass, low-pass, all-pass coefficients
  double hx[2], hy[2];        // their states
  double lx, ly, ax, ay;
  double env_m, env_b; // the limit's envelopes
  s16 dl[2][S3DSIM_DELAY];
  int dpos;
  u32 seed;
};

void s3dsim_reset(struct s3dsim *s, u32 rate);

// the registers 50h, 52h, 54h, 56h, 58h and 5Ah
void s3dsim_regs(struct s3dsim *s, const u8 *reg);

// n frames of 16-bit stereo through the effect, in to out
void s3dsim_run(struct s3dsim *s, const s16 *in, s16 *out, u16 n);

// the gains at hz of M>M, S>S, M>S and S>M with these registers, relative
// to the effect off, without the limit
void s3dsim_response(const u8 *reg, u32 rate, double hz, double *gain);

#endif /* S3DSIM_H */
