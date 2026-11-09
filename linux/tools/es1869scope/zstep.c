// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * zstep.c: the step at the sign of Audio 2's codes, from the music. Notes:
 *
 * The DAC's level of a code c is c below 0 and c plus the gap from 0 up, so
 * what comes back is the path's response to the codes plus the gap times
 * the sign of each code (less a half, whose rest is a constant). The sign
 * follows the music, so it is fitted together with both sides' codes: per
 * bin a system of three, as the paths' two inputs are solved together. The
 * sign's transfer is then the gap times the side's own path, and their
 * ratio is the gap, a real number wherever the fit stands. Low down the
 * sign is mostly the music itself, which leaves the system near singular, so
 * the gap comes from a band higher up, where a sign's edges reach and the
 * music no longer does, as its median over the bins.
 *
 * A nonlinearity of the recorder's own that is soft about the same zero, as
 * Audio 1's record path has, follows the sign as well once the music swings
 * past its knee, and the fit takes it as part of the step.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "zstep.h"

int zstep_init(struct zstep *z, unsigned int bins)
{
	double **all[] = { &z->xl,    &z->xr,	 &z->lr,    &z->qq[0],
			   &z->qq[1], &z->ql[0], &z->ql[1], &z->qr[0],
			   &z->qr[1], &z->yl[0], &z->yl[1], &z->yr[0],
			   &z->yr[1], &z->yq[0], &z->yq[1] };
	unsigned int i;

	memset(z, 0, sizeof(*z));
	z->bins = bins;
	for (i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
		// each holds re and im, the powers' im unused
		*all[i] = calloc(2 * (size_t)bins, sizeof(double));
		if (!*all[i]) {
			zstep_free(z);
			return -1;
		}
	}
	return 0;
}

void zstep_free(struct zstep *z)
{
	int c;

	free(z->xl);
	free(z->xr);
	free(z->lr);
	for (c = 0; c < 2; c++) {
		free(z->qq[c]);
		free(z->ql[c]);
		free(z->qr[c]);
		free(z->yl[c]);
		free(z->yr[c]);
		free(z->yq[c]);
	}
	memset(z, 0, sizeof(*z));
}

void zstep_reset(struct zstep *z)
{
	size_t n = 2 * (size_t)z->bins * sizeof(double);
	int c;

	memset(z->xl, 0, n);
	memset(z->xr, 0, n);
	memset(z->lr, 0, n);
	for (c = 0; c < 2; c++) {
		memset(z->qq[c], 0, n);
		memset(z->ql[c], 0, n);
		memset(z->qr[c], 0, n);
		memset(z->yl[c], 0, n);
		memset(z->yr[c], 0, n);
		memset(z->yq[c], 0, n);
	}
	z->segments = 0;
}

static inline double complex at(const double *p, unsigned int k)
{
	return p[2 * k] + I * p[2 * k + 1];
}

// a sum of a times the conjugate of b, added at bin k
static inline void add(double *s, unsigned int k, double complex a,
		       double complex b)
{
	double complex v = a * conj(b);

	s[2 * k] += creal(v);
	s[2 * k + 1] += cimag(v);
}

void zstep_add(struct zstep *z, const double *xl, const double *xr,
	       const double *ql, const double *qr, const double *yl,
	       const double *yr)
{
	unsigned int k;
	int c;

	for (k = 0; k < z->bins; k++) {
		double complex l = at(xl, k), r = at(xr, k);

		add(z->xl, k, l, l);
		add(z->xr, k, r, r);
		add(z->lr, k, l, r);
		for (c = 0; c < 2; c++) {
			double complex q = at(c ? qr : ql, k);
			double complex y = at(c ? yr : yl, k);

			add(z->qq[c], k, q, q);
			add(z->ql[c], k, q, l);
			add(z->qr[c], k, q, r);
			add(z->yl[c], k, y, l);
			add(z->yr[c], k, y, r);
			add(z->yq[c], k, y, q);
		}
	}
	z->segments++;
}

// a 3 x 3 system by Gauss with the largest pivot; 0, or -1 if singular
static int solve3(double complex a[3][4])
{
	int i, j, k;

	for (i = 0; i < 3; i++) {
		int p = i;

		for (j = i + 1; j < 3; j++)
			if (cabs(a[j][i]) > cabs(a[p][i]))
				p = j;
		if (cabs(a[p][i]) == 0)
			return -1;
		if (p != i)
			for (k = 0; k < 4; k++) {
				double complex t = a[i][k];

				a[i][k] = a[p][k];
				a[p][k] = t;
			}
		for (j = 0; j < 3; j++) {
			double complex f;

			if (j == i)
				continue;
			f = a[j][i] / a[i][i];
			for (k = i; k < 4; k++)
				a[j][k] -= f * a[i][k];
		}
	}
	for (i = 0; i < 3; i++)
		a[i][3] /= a[i][i];
	return 0;
}

static int cmp(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return (x > y) - (x < y);
}

double zstep_gap(const struct zstep *z, int c, unsigned int lo, unsigned int hi,
		 double *spread)
{
	unsigned int k, n = 0;
	double *g, mid, dev = 0;

	if (spread)
		*spread = 0;
	if (hi >= z->bins)
		hi = z->bins - 1;
	if (!z->segments || lo > hi)
		return 0;
	g = malloc((hi - lo + 1) * sizeof(double));
	if (!g)
		return 0;
	for (k = lo; k <= hi; k++) {
		// the regressors L, R and the sign; row i holds each one's
		// sum against regressor i, then the output's
		double complex s[3][4] = {
			{ at(z->xl, k), conj(at(z->lr, k)), at(z->ql[c], k),
			  at(z->yl[c], k) },
			{ at(z->lr, k), at(z->xr, k), at(z->qr[c], k),
			  at(z->yr[c], k) },
			{ conj(at(z->ql[c], k)), conj(at(z->qr[c], k)),
			  at(z->qq[c], k), at(z->yq[c], k) },
		};
		double complex own;

		if (solve3(s))
			continue;
		own = s[c][3];
		if (cabs(own) == 0)
			continue;
		g[n++] = creal(s[2][3] / own);
	}
	if (!n) {
		free(g);
		return 0;
	}
	qsort(g, n, sizeof(double), cmp);
	mid = n % 2 ? g[n / 2] : (g[n / 2 - 1] + g[n / 2]) / 2;
	if (spread) {
		for (k = 0; k < n; k++)
			g[k] = fabs(g[k] - mid);
		qsort(g, n, sizeof(double), cmp);
		dev = g[n / 2] * 1.4826;
		*spread = dev / sqrt((double)n);
	}
	free(g);
	return mid;
}
