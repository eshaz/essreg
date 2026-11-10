/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * a1lin.h: what Audio 1's record path does to a sample's size, against what
 * should have come back, for es1869scope -E, and the tables of the driver's
 * linearity stage (capfilt.h) that take it back. The curve: what the path
 * adds to each expected value, about a centre, between the stage's knots.
 * The profile: the complex gain left, from the analytic signals of what came
 * back and what should have, by the band of the instantaneous frequency and
 * the bin of the envelope that the stage reads from the recording itself.
 * Both are measured on what the stage gave, so that the next tables take back
 * what the ones in use left.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef A1LIN_H
#define A1LIN_H

#include <complex.h>
#include <stdint.h>

/* as capfilt.h has them */
#define A1L_KNOTS 17 /* the curve's knots from its centre out: 0, 2 .. 512 */
#define A1L_VALS (2 * A1L_KNOTS - 1)
#define A1L_ENV 10 /* the envelope's bins, 4 LSB to 2048 */
#define A1L_BANDS 4 /* the instantaneous frequency's */
#define A1L_PER (A1L_BANDS * A1L_ENV)
#define A1L_ONE 16384 /* a gain of 1, a turn of a radian */

/* the stage's tables, as its controls hold them */
struct a1lin_tables {
	long centre[2]; /* a recorded value */
	long curve[2][A1L_VALS]; /* what each knot adds, 1/256 LSB */
	long gain[2][A1L_PER]; /* by band, then envelope bin */
	long turn[2][A1L_PER]; /* the phase added, ahead when positive */
};

struct a1lin_side {
	/*
	 * the curves' normal equations, the knots' hats and then a line,
	 * against what should have come: of what came back raw, which is the
	 * path's own, and of what the stage gave, which is what it left
	 */
	double a[A1L_VALS + 1][A1L_VALS + 1], b[A1L_VALS + 1];
	double la[A1L_VALS + 1][A1L_VALS + 1], lb[A1L_VALS + 1];
	/* the profile's, by band and bin: each bin's own and with the next,
	 * the recording against the expected, the stage's own Hilbert
	 * transform of the recording against it, and the frames */
	double pp[A1L_BANDS][A1L_ENV][2];
	double complex yp[A1L_BANDS][A1L_ENV], hp[A1L_BANDS][A1L_ENV];
	double yy[A1L_BANDS][A1L_ENV], frames[A1L_BANDS][A1L_ENV];
	/* the stage's envelope, followed over the recording, and its DC */
	int64_t pow, adv, dc;
	int32_t re, im;
	double n;
};

struct a1lin {
	struct a1lin_side s[2];
	double centre[2]; /* what the curve's sums are about */
};

/*
 * what is left after the tables, fitted: the curve, and by band and bin the
 * complex gain and how it moves with the stage's turn, a radian of it
 */
struct a1lin_fit {
	double coh[2][A1L_PER]; /* how much of what came is what should have */
	double path[2][A1L_VALS]; /* what the path adds at each knot, LSB */
	double table[2][A1L_VALS]; /* what the stage should add, LSB */
	double weight[2][A1L_VALS]; /* the frames behind each knot */
	double curve[2][A1L_VALS]; /* what the stage in use left, LSB */
	double complex g[2][A1L_PER], dg[2][A1L_PER];
	double frames[2][A1L_PER];
	double n[2];
};

/* the knot k's offset from the centre: -512 at 0, +512 at A1L_VALS - 1 */
int a1lin_knot(int k);
/* tables that change nothing, about the centres given */
void a1lin_identity(struct a1lin_tables *t, const long *centre);
/* the sums cleared, about the centres given */
void a1lin_reset(struct a1lin *a, const double *centre);
/*
 * the envelope followed from silence again, as at a recording's start, with
 * the DC under it from each side's given, LSB
 */
void a1lin_restart(struct a1lin *a, const double *dc);
/* n raw frames of side c, with 15 more either side, that only lead the
 * stage's envelope in, as the frames before those a1lin_add() counts */
void a1lin_lead(struct a1lin *a, int c, const int16_t *r16, unsigned int n);
/*
 * n frames of side c, lined up: what should have come back (p, with no DC),
 * the record path's DC, what came back raw, before the stage, as 16-bit
 * samples with 15 more either side (r16), what the stage gave (y), and the
 * Hilbert transforms of p and y (hp, hy)
 */
void a1lin_add(struct a1lin *a, int c, const double *p, double dc,
	       const int16_t *r16, const double *y, const double *hp,
	       const double *hy, unsigned int n);
/* 0, or -1 with too little to stand on */
int a1lin_fit(const struct a1lin *a, struct a1lin_fit *f);
/*
 * the next tables, from those in use and what they left, a share step of it
 * taken back: the number of values that moved
 */
int a1lin_next(const struct a1lin_fit *f, const struct a1lin_tables *in,
	       double step, struct a1lin_tables *out);
/* what the path itself adds at what should have come at knot k, LSB */
double a1lin_path(const struct a1lin_fit *f, int c, int k);

#endif /* A1LIN_H */
