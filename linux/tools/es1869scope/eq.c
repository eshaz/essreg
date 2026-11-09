// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * eq.c: corrections over time (eq.h).
 *
 * A correction applied now lands a block later at the earliest, so the value
 * it answers is the trend's at that time, not the last block's: a line
 * through the last EQ_HIST blocks, which a slow drift follows and a single
 * odd block moves only a little. Fast changes aren't followed, by design.
 *
 * The DAC correction's response is c0 + 2 sum s_k cos(k w), and with its
 * centre c0 = 1 - 2 sum s_k, so that its gain at 0 Hz stays 1, that is
 * 1 + 2 sum s_k (cos(k w) - 1): linear in the side taps, whose least squares
 * fit is a 16 x 16 system. From bands that mostly lie low, where every tap's
 * response looks alike, that system is all but singular (a condition of
 * 1e16), and its taps would go anywhere along the directions the bands leave
 * free. A ridge of EQ_RIDGE towards the taps before keeps them there: the
 * bands are met as closely, and above the top band the response stays as it
 * was.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "eq.h"

#include <math.h>
#include <string.h>

#define EQ_RIDGE 1e-6 // the pull towards the taps before, against a band's 1

void eq_reset(struct eq_series *s)
{
	memset(s, 0, sizeof(*s));
}

void eq_push(struct eq_series *s, double t, double v)
{
	s->t[s->next] = t;
	s->v[s->next] = v;
	s->next = (s->next + 1) % EQ_HIST;
	if (s->n < EQ_HIST)
		s->n++;
}

double eq_predict(const struct eq_series *s, double t, double *slope)
{
	double st = 0, sv = 0, stt = 0, stv = 0, t0, den, b;
	int i;

	if (slope)
		*slope = 0;
	if (!s->n)
		return 0;
	// about the first time kept, for the sums' precision
	t0 = s->t[(s->next + EQ_HIST - s->n) % EQ_HIST];
	for (i = 0; i < s->n; i++) {
		double u = s->t[i] - t0;

		st += u;
		sv += s->v[i];
		stt += u * u;
		stv += u * s->v[i];
	}
	den = s->n * stt - st * st;
	if (s->n < 3 || den <= 0)
		return sv / s->n;
	b = (s->n * stv - st * sv) / den;
	if (slope)
		*slope = b;
	return (sv - b * st) / s->n + b * (t - t0);
}

double eq_sides_gain(const long *sides, double f, double rate)
{
	double g = 1, w = 2 * M_PI * f / rate;
	int k;

	for (k = 1; k <= EQ_SIDES; k++)
		g += 2.0 * sides[k - 1] / EQ_Q30 * (cos(k * w) - 1);
	return g;
}

int eq_fit_sides(const double *f, const double *target, const double *w, int n,
		 double rate, const long *prior, long *sides)
{
	enum { N = EQ_SIDES };
	double a[N][N + 1], x[N];
	int i, j, k, r;

	memset(a, 0, sizeof(a));
	for (k = 0; k < n; k++) {
		double b[N], wk = 2 * M_PI * f[k] / rate;

		for (i = 0; i < N; i++)
			b[i] = 2 * (cos((i + 1) * wk) - 1);
		for (i = 0; i < N; i++) {
			for (j = 0; j < N; j++)
				a[i][j] += w[k] * b[i] * b[j];
			a[i][N] += w[k] * b[i] * (target[k] - 1);
		}
	}
	for (i = 0; i < N; i++) {
		a[i][i] += EQ_RIDGE;
		a[i][N] += EQ_RIDGE * (prior ? prior[i] / EQ_Q30 : 0);
	}
	// Gauss with the largest pivot of each column
	for (i = 0; i < N; i++) {
		r = i;
		for (j = i + 1; j < N; j++)
			if (fabs(a[j][i]) > fabs(a[r][i]))
				r = j;
		if (fabs(a[r][i]) < 1e-15)
			return -1;
		for (j = 0; j <= N; j++) {
			double t = a[i][j];

			a[i][j] = a[r][j];
			a[r][j] = t;
		}
		for (j = i + 1; j < N; j++) {
			double q = a[j][i] / a[i][i];

			for (k = i; k <= N; k++)
				a[j][k] -= q * a[i][k];
		}
	}
	for (i = N; i-- > 0;) {
		double s = a[i][N];

		for (j = i + 1; j < N; j++)
			s -= a[i][j] * x[j];
		x[i] = s / a[i][i];
	}
	for (i = 0; i < N; i++)
		if (!(fabs(x[i]) < 1))
			return -1;
	for (i = 0; i < N; i++)
		sides[i] = lrint(x[i] * EQ_Q30);
	return 0;
}
