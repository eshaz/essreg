/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tie.h: a clock's time error against another's, and what is in it. A phase
 * x is in seconds, one sample every tau0 seconds, and an averaging time is m
 * samples of it. The deviations are the overlapping estimators (NIST SP 1065).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef TIE_H
#define TIE_H

#include <stddef.h>

/* x = a + b t by least squares */
struct tie_line {
	double a, b;
};

void tie_fit(const double *t, const double *x, size_t n, struct tie_line *l);

/* the deviations at m samples, NaN where the record is too short */
double tie_adev(const double *x, size_t n, double tau0, size_t m);
double tie_mdev(const double *x, size_t n, double tau0, size_t m);
double tie_tdev(const double *x, size_t n, double tau0, size_t m);

/*
 * The Allan covariance of two phases measured together, and the
 * three-cornered hat: d1 = a - b and d2 = b - c, measured together, give the
 * Allan variances of a, b and c in var[0], var[1] and var[2]. A variance can
 * come out below 0 where the record is too short to tell.
 */
double tie_acov(const double *x1, const double *x2, size_t n, double tau0,
		size_t m);
void tie_hat(const double *d1, const double *d2, size_t n, double tau0,
	     size_t m, double var[3]);

/*
 * Pulses: the samples of r further from its median than k times its median
 * absolute deviation, up to max of them in idx; returns how many there are.
 */
size_t tie_pulses(const double *r, size_t n, double k, size_t *idx, size_t max,
		  double *median, double *mad);

/*
 * The spectrum of x by Welch: Hann windows of 2^log2len samples, each half
 * over the last and with its own line taken out. psd gets 2^(log2len - 1) + 1
 * values in s^2/Hz, one-sided, at i / (2^log2len * tau0) Hz. Returns the
 * number of windows, 0 if x is shorter than one, or -1 without memory.
 */
int tie_psd(const double *x, size_t n, double tau0, unsigned int log2len,
	    double *psd);

/*
 * The period that pulses at times t, in order, keep, from pmin to pmax
 * seconds: the first tall bin of a histogram of the time between every two
 * pulses, in bins of 0.5 ms, refined from its neighbours. strength gets how many
 * standard deviations that bin stands above the mean, of which chance makes
 * about 4; returns the period, or 0 when nothing stands out or there are fewer
 * than 8 pulses.
 */
double tie_period(const double *t, size_t n, double pmin, double pmax,
		  double *strength);

/* an FFT in place, of 2^log2len complex values, re and im interleaved */
void tie_fft(double *z, unsigned int log2len);

#endif /* TIE_H */
