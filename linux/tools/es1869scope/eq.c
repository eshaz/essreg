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
#include <stdio.h>
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

// The rules of what music may do, apart from the code that measures it:
// - at 3x Audio 1 can't record, since the ISA bus can't carry both and the
//   DAC stalls, and a rate past Audio 1's it can't record at all;
// - the trends, and the refits from them, count only what was measured like
//   their start: through the record path at the rate es1869cal calibrated it
//   at, at 2x, where the top of the loop holds still from run to run (at 1x
//   it moves with where the DAC and the ADC sample in a frame), and with
//   Audio 1's correction as it was at the start. Music measured otherwise
//   shows, and counts again once the setup comes back;
// - the trim moves only in the Clock mode, whose residual it answers, and
//   only for the 44.1 kHz family, since the 48 kHz family's clock runs
//   about 252 ppm apart and its trend would be another
int eq_policy(const struct eq_setup *now, const struct eq_setup *base,
	      char *why, unsigned int len)
{
	int f = EQ_COUNT | EQ_TRIM;

	why[0] = 0;
	if (now->factor > 2) {
		snprintf(why, len,
			 "Audio 2 at %ux, which Audio 1's recording would "
			 "stall: Audio 1 rests",
			 now->factor);
		return EQ_REST;
	}
	if (!now->rate || now->rate > EQ_A1_MAX) {
		snprintf(
			why, len,
			"a stream at %u Hz, past what Audio 1 records: Audio 1 "
			"rests",
			now->rate);
		return EQ_REST;
	}
	if (now->mode != 2 || now->rate % 11025)
		f &= ~EQ_TRIM;
	if (now->rate != EQ_CAL_RATE)
		snprintf(why, len,
			 "%u Hz, where the record path wasn't calibrated: "
			 "shown, not counted",
			 now->rate);
	else if (now->factor < 2)
		snprintf(why, len,
			 "at 1x the top of the loop moves from run to run: "
			 "shown, not counted");
	else if (base && (now->factor != base->factor || now->a1c != base->a1c))
		snprintf(why, len,
			 "not as the trends started (%ux, Audio 1's correction "
			 "%s): shown, not counted",
			 base->factor, base->a1c ? "on" : "off");
	else
		return f;
	return f & ~EQ_COUNT;
}
