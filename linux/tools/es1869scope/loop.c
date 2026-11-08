// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * loop.c: a path's response measured from whatever plays through it
 * (loop.h), on tie.c's FFT.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "loop.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tie.h"

int loop_init(struct loop *l, unsigned int log2n)
{
	unsigned int n = 1u << log2n, i;

	memset(l, 0, sizeof(*l));
	if (log2n < 6 || log2n > 16)
		return -1;
	l->log2n = log2n;
	l->n = n;
	l->win = malloc(n * sizeof(double));
	l->sxx = calloc(n / 2 + 1, sizeof(double));
	l->syy = calloc(n / 2 + 1, sizeof(double));
	l->sxy = calloc(n + 2, sizeof(double));
	l->zx = malloc((n + 2) * sizeof(double));
	l->zy = malloc((n + 2) * sizeof(double));
	l->zw = malloc(2 * n * sizeof(double));
	if (!l->win || !l->sxx || !l->syy || !l->sxy || !l->zx || !l->zy ||
	    !l->zw) {
		loop_free(l);
		return -1;
	}
	for (i = 0; i < n; i++)
		l->win[i] = 0.5 - 0.5 * cos(2 * M_PI * i / n);
	return 0;
}

void loop_free(struct loop *l)
{
	free(l->win);
	free(l->sxx);
	free(l->syy);
	free(l->sxy);
	free(l->zx);
	free(l->zy);
	free(l->zw);
	memset(l, 0, sizeof(*l));
}

void loop_reset(struct loop *l)
{
	unsigned int h = l->n / 2 + 1;

	memset(l->sxx, 0, h * sizeof(double));
	memset(l->syy, 0, h * sizeof(double));
	memset(l->sxy, 0, 2 * h * sizeof(double));
	l->segments = 0;
}

// two real signals as one complex one, a + jb: the transform's bin k and the
// conjugate of bin n - k give each one's own
void loop_spectra(struct loop *l, const double *a, const double *b, double *za,
		  double *zb)
{
	unsigned int n = l->n, k;
	double *z = l->zw;

	for (k = 0; k < n; k++) {
		z[2 * k] = a[k] * l->win[k];
		z[2 * k + 1] = b[k] * l->win[k];
	}
	tie_fft(z, l->log2n);
	for (k = 0; k <= n / 2; k++) {
		unsigned int c = (n - k) % n;
		double zr = z[2 * k], zi = z[2 * k + 1];
		double cr = z[2 * c], ci = -z[2 * c + 1];

		za[2 * k] = (zr + cr) / 2;
		za[2 * k + 1] = (zi + ci) / 2;
		zb[2 * k] = (zi - ci) / 2;
		zb[2 * k + 1] = -(zr - cr) / 2;
	}
}

void loop_add_spectra(struct loop *l, const double *zx, const double *zy)
{
	unsigned int k;

	for (k = 0; k <= l->n / 2; k++) {
		double xr = zx[2 * k], xi = zx[2 * k + 1];
		double yr = zy[2 * k], yi = zy[2 * k + 1];

		l->sxx[k] += xr * xr + xi * xi;
		l->syy[k] += yr * yr + yi * yi;
		// y times the conjugate of x
		l->sxy[2 * k] += yr * xr + yi * xi;
		l->sxy[2 * k + 1] += yi * xr - yr * xi;
	}
	l->segments++;
}

void loop_add(struct loop *l, const double *x, const double *y)
{
	loop_spectra(l, x, y, l->zx, l->zy);
	loop_add_spectra(l, l->zx, l->zy);
}

void loop_bin(const struct loop *l, unsigned int k, double *mag, double *phase,
	      double *coh)
{
	double re = l->sxy[2 * k], im = l->sxy[2 * k + 1];
	double sxx = l->sxx[k], syy = l->syy[k];

	*mag = sxx > 0 ? sqrt(re * re + im * im) / sxx : 0;
	*phase = atan2(im, re);
	*coh = sxx > 0 && syy > 0 ? (re * re + im * im) / (sxx * syy) : 0;
}

long loop_lag(const double *x, const double *y, unsigned int log2n,
	      double *frac)
{
	unsigned int n = 1u << log2n, m = 2 * n, k, at = 0;
	double *za = malloc(4 * (size_t)m * sizeof(double)), *zb;
	double l, c, r;

	*frac = 0;
	if (!za)
		return 0;
	zb = za + 2 * m;
	// padded to twice the length, so that the correlation doesn't wrap
	for (k = 0; k < m; k++) {
		za[2 * k] = k < n ? x[k] : 0;
		za[2 * k + 1] = 0;
		zb[2 * k] = k < n ? y[k] : 0;
		zb[2 * k + 1] = 0;
	}
	tie_fft(za, log2n + 1);
	tie_fft(zb, log2n + 1);
	// the conjugate of Y times X, whose forward transform is 2n times the
	// sum over k of y[k + lag] x[k], at index lag, the negative ones from
	// the top
	for (k = 0; k < m; k++) {
		double xr = za[2 * k], xi = za[2 * k + 1];
		double yr = zb[2 * k], yi = zb[2 * k + 1];

		za[2 * k] = yr * xr + yi * xi;
		za[2 * k + 1] = yr * xi - yi * xr;
	}
	tie_fft(za, log2n + 1);
	// the largest either way, since a path may invert
	for (k = 1; k < m; k++)
		if (fabs(za[2 * k]) > fabs(za[2 * at]))
			at = k;
	l = za[2 * ((at + m - 1) % m)];
	c = za[2 * at];
	r = za[2 * ((at + 1) % m)];
	if (l - 2 * c + r != 0)
		*frac = 0.5 * (l - r) / (l - 2 * c + r);
	free(za);
	return at < n ? (long)at : (long)at - (long)m;
}
