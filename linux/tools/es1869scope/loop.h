/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * loop.h: a path's response measured from whatever plays through it, by
 * Welch: what was played (x) and what came back (y), in Hann windows of 2^log2n
 * samples, half over the last. The response is Sxy / Sxx, the coherence
 * |Sxy|^2 / (Sxx Syy) says how far each bin can be trusted, and the lag
 * between the two captures comes from their cross-correlation, once, since
 * both run on one crystal.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef LOOP_H
#define LOOP_H

struct loop {
	unsigned int log2n, n;
	double *win;
	double *sxx, *syy, *sxy; /* sxy: re and im interleaved */
	double *zx, *zy;
	unsigned long segments;
};

/* 0, or -1 without memory or for a length out of 2^6 to 2^16 */
int loop_init(struct loop *l, unsigned int log2n);
void loop_free(struct loop *l);
void loop_reset(struct loop *l);

/* a segment of n samples of each, already lined up */
void loop_add(struct loop *l, const double *x, const double *y);

/* bin k, 0 to n/2: the response's magnitude and phase, and the coherence */
void loop_bin(const struct loop *l, unsigned int k, double *mag, double *phase,
	      double *coh);

/*
 * how many samples y lags x, from -n/2 to n/2 - 1, by their cross-correlation
 * over 2^log2n samples of each, with the parabola through the peak in frac
 */
long loop_lag(const double *x, const double *y, unsigned int log2n,
	      double *frac);

#endif /* LOOP_H */
