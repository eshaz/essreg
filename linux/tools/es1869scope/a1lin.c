// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * a1lin.c: what Audio 1's record path does to a sample's size, and the
 * linearity stage's tables that take it back (a1lin.h). Notes:
 *
 * The path's curve is fitted as what came back raw less what should have,
 * against what should have, as the sum of hats on the stage's knots about the
 * centre and a line. Past the last knots the curve holds, so the line is told
 * apart from it: the line is the path's own gain, which the loop's response
 * already takes, and the curve alone is taken back. A light penalty on the
 * knots' second differences carries it over knots that the music seldom
 * reaches. What should have come carries no noise, so the fit has no bias
 * from the ADC's; fitted the other way, against the raw value, the quiet
 * passages, many frames about the DC, would make a noise gate of it, the raw
 * value's best guess being the DC itself. The stage's table is then the
 * curve's inverse, solved at each raw knot: in one step, with no loop. What
 * the stage in use left is fitted the same way, for the view.
 *
 * The profile is the complex gain from what should have come back to what
 * did, as their analytic signals have it, fitted per band in the bins of the
 * stage's envelope between which the stage itself interpolates: so each
 * frame counts towards the two bins its envelope lies between, as much as
 * the stage would take from each. The band and the envelope are the stage's
 * own, from the same Hilbert FIR and smoothing (capfilt.c), run over the
 * recording. A bin moves only where what should have come explains most of
 * what came, a coherence of 0.8: in silence what came is the ADC's noise,
 * whose gain and phase against nearly nothing mean nothing.
 *
 * The next tables move a share of the way: the curve to what was fitted,
 * the gain by the magnitude left. The turn adds the stage's own Hilbert
 * transform of the recording, which the short FIR makes weak low down and
 * which, in music, carries the other bands' treble at full strength, so how
 * far a turn moves a bin's phase is measured, not assumed: the transform
 * against what should have come back, in the same bins, is the gain's
 * derivative by the turn (twice the real transform's cross term, whose
 * other half turns at twice the frequency and averages out). The turn moves
 * by the phase left over that, a Newton step, only where a turn moves the
 * phase by at least 0.3 of itself: low down, where the bass leads, the bins'
 * phase is mostly the bass's, which the short FIR hardly turns, while the
 * treble riding on it turns whole, so a turn large enough to move the bass
 * would turn the treble by as much, which reads at its zero crossings as
 * many LSB. It stays within 2 degrees and the gain within 1 dB.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "a1lin.h"

#include <math.h>
#include <string.h>

#include "capfilt.h"

_Static_assert(A1L_KNOTS == ES_CL_KNOTS && A1L_ENV == ES_CL_ENV &&
		       A1L_BANDS == ES_CL_BANDS && A1L_ONE == ES_CF_ONE,
	       "a1lin.h and capfilt.h disagree");

#define A1L_LINE A1L_VALS // the line's place in the normal equations
#define A1L_SMOOTH 1e-4 // the penalty on second differences, of the mean
#define A1L_TURN_MAX 573 // 2 degrees
#define A1L_GAIN_LO 14602 // -1 dB
#define A1L_GAIN_HI 18383 // +1 dB
#define A1L_MIN_KNOT 50.0 // the frames a value moves with
#define A1L_MIN_BIN 2000.0
#define A1L_MIN_TURN 0.3 // the least a turn moves a bin's phase, to follow
#define A1L_MIN_COH 0.8 // the least coherence a bin moves with

static int a1l_k(int i)
{
	return i < 2 ? 2 * i : (2 + (i & 1)) << (((i + 2) >> 1) - 1);
}

int a1lin_knot(int k)
{
	int mid = A1L_KNOTS - 1;

	return k < mid ? -a1l_k(mid - k) : a1l_k(k - mid);
}

// u's two knots and the weight of the outer one
static void a1l_hat(double u, int *i0, int *i1, double *w)
{
	int mid = A1L_KNOTS - 1, i, m, ai;
	double a = fabs(u), lo, hi;

	if (a >= a1l_k(mid)) {
		*i0 = *i1 = u < 0 ? 0 : A1L_VALS - 1;
		*w = 0;
		return;
	}
	ai = (int)a;
	if (ai < 4) {
		i = ai >> 1;
	} else {
		for (m = 2; (ai >> (m + 1)) != 0; m++)
			;
		i = 2 * m - 2 + ((ai >> (m - 1)) & 1);
	}
	lo = a1l_k(i);
	hi = a1l_k(i + 1);
	*w = (a - lo) / (hi - lo);
	*i0 = u < 0 ? mid - i : mid + i;
	*i1 = u < 0 ? mid - i - 1 : mid + i + 1;
}

// a fitted curve at u, between its knots
static double a1l_at(const double *curve, double u)
{
	int i0, i1;
	double w;

	a1l_hat(u, &i0, &i1, &w);
	return curve[i0] * (1 - w) + curve[i1] * w;
}

// a frame into a curve's normal equations: e against the hats at u and a
// line, the upper triangle
static void a1l_sum(double m[A1L_VALS + 1][A1L_VALS + 1], double *b, double u,
		    double e)
{
	double r = u / a1lin_knot(A1L_VALS - 1), w, w0;
	int i0, i1;

	a1l_hat(u, &i0, &i1, &w);
	w0 = 1 - w;
	m[i0][i0] += w0 * w0;
	m[i1][i1] += w * w;
	if (i1 != i0)
		m[i0 < i1 ? i0 : i1][i0 < i1 ? i1 : i0] += w0 * w;
	else
		m[i0][i0] += 2 * w0 * w;
	m[i0][A1L_LINE] += w0 * r;
	m[i1][A1L_LINE] += w * r;
	m[A1L_LINE][A1L_LINE] += r * r;
	b[i0] += w0 * e;
	b[i1] += w * e;
	b[A1L_LINE] += r * e;
}

void a1lin_identity(struct a1lin_tables *t, const long *centre)
{
	int c, i;

	memset(t, 0, sizeof(*t));
	for (c = 0; c < 2; c++) {
		t->centre[c] = centre ? centre[c] : 0;
		for (i = 0; i < A1L_PER; i++)
			t->gain[c][i] = A1L_ONE;
	}
}

void a1lin_restart(struct a1lin *a, const double *dc)
{
	int c;

	for (c = 0; c < 2; c++) {
		a->s[c].pow = a->s[c].adv = 0;
		a->s[c].re = a->s[c].im = 0;
		a->s[c].dc = (int64_t)lrint(dc[c] * 16) * (1 << ES_CL_DC);
	}
}

void a1lin_reset(struct a1lin *a, const double *centre)
{
	memset(a, 0, sizeof(*a));
	a->centre[0] = centre[0];
	a->centre[1] = centre[1];
	a1lin_restart(a, centre);
}

// the stage's own envelope and band at frame t of y16, as capfilt.c follows
// them, its output standing for the FIR's; its Hilbert transform, LSB
static double a1l_follow(struct a1lin_side *s, const int16_t *y16, u32 *band,
			 u32 *pos)
{
	int64_t h = es1869_cl_hilbert(y16), re = (int64_t)y16[0] * 16;
	int64_t im = h >> 10;

	s->dc += re - (s->dc >> ES_CL_DC);
	re -= s->dc >> ES_CL_DC;
	s->pow += re * re + im * im - (s->pow >> ES_CL_SMOOTH);
	s->adv += im * s->re - re * s->im - (s->adv >> ES_CL_SMOOTH);
	s->re = (int32_t)re;
	s->im = (int32_t)im;
	es1869_cl_place(s->pow, s->adv, band, pos);
	return (double)h / ES_CF_ONE;
}

void a1lin_lead(struct a1lin *a, int c, const int16_t *r16, unsigned int n)
{
	unsigned int t;
	u32 band, pos;

	for (t = 0; t < n; t++)
		a1l_follow(&a->s[c], &r16[t], &band, &pos);
}

void a1lin_add(struct a1lin *a, int c, const double *p, double dc,
	       const int16_t *r16, const double *y, const double *hp,
	       const double *hy, unsigned int n)
{
	struct a1lin_side *s = &a->s[c];
	unsigned int t;

	for (t = 0; t < n; t++) {
		double v = p[t] + dc, w0, w1, pw;
		double complex zp = p[t] + I * hp[t];
		double complex zy = (y[t] - dc) + I * hy[t];
		double complex x = zy * conj(zp), xh;
		u32 band, pos, j;

		// what the path added, and what the stage in use left
		a1l_sum(s->a, s->b, v - a->centre[c], r16[t] - v);
		a1l_sum(s->la, s->lb, v - a->centre[c], y[t] - v);
		// the profile, in the two bins the stage reads between
		xh = a1l_follow(s, &r16[t], &band, &pos) * conj(zp);
		j = pos >> 8;
		w1 = j < A1L_ENV - 1 ? (pos & 255) / 256.0 : 0;
		w0 = 1 - w1;
		pw = creal(zp * conj(zp));
		s->pp[band][j][0] += w0 * w0 * pw;
		s->yp[band][j] += w0 * x;
		s->hp[band][j] += w0 * xh;
		s->yy[band][j] += w0 * creal(zy * conj(zy));
		s->frames[band][j] += w0;
		if (w1 > 0) {
			s->pp[band][j][1] += w0 * w1 * pw;
			s->pp[band][j + 1][0] += w1 * w1 * pw;
			s->yp[band][j + 1] += w1 * x;
			s->hp[band][j + 1] += w1 * xh;
			s->yy[band][j + 1] += w1 * creal(zy * conj(zy));
			s->frames[band][j + 1] += w1;
		}
	}
	s->n += n;
}

// a symmetric positive definite system in place, by Cholesky: 0, or -1
static int a1l_solve(double m[A1L_VALS + 1][A1L_VALS + 1], double *x, int size)
{
	int i, j, k;

	for (j = 0; j < size; j++) {
		double d = m[j][j];

		for (k = 0; k < j; k++)
			d -= m[j][k] * m[j][k];
		if (!(d > 0))
			return -1;
		m[j][j] = sqrt(d);
		for (i = j + 1; i < size; i++) {
			double s = m[i][j];

			for (k = 0; k < j; k++)
				s -= m[i][k] * m[j][k];
			m[i][j] = s / m[j][j];
		}
	}
	for (i = 0; i < size; i++) {
		for (k = 0; k < i; k++)
			x[i] -= m[i][k] * x[k];
		x[i] /= m[i][i];
	}
	for (i = size - 1; i >= 0; i--) {
		for (k = i + 1; k < size; k++)
			x[i] -= m[k][i] * x[k];
		x[i] /= m[i][i];
	}
	return 0;
}

// a curve from its normal equations (the upper triangle), smoothed: its
// values at the knots, LSB; 0, or -1
static int a1l_curve(const double sa[A1L_VALS + 1][A1L_VALS + 1],
		     const double *sb, double *out)
{
	static double m[A1L_VALS + 1][A1L_VALS + 1];
	static const double d[3] = { 1, -2, 1 };
	double x[A1L_VALS + 1], mean = 0, lam;
	int i, j, p, q;

	for (i = 0; i <= A1L_LINE; i++)
		for (j = i; j <= A1L_LINE; j++)
			m[i][j] = m[j][i] = sa[i][j];
	for (i = 0; i < A1L_VALS; i++)
		mean += m[i][i] / A1L_VALS;
	// the knots' second differences, lightly, and a ridge so slight that
	// only what nothing reaches stands on it
	lam = A1L_SMOOTH * mean + 1e-9;
	for (i = 0; i + 2 < A1L_VALS; i++)
		for (p = 0; p < 3; p++)
			for (q = 0; q < 3; q++)
				m[i + p][i + q] += lam * d[p] * d[q];
	for (i = 0; i <= A1L_LINE; i++)
		m[i][i] += 1e-9 * mean + 1e-12;
	memcpy(x, sb, sizeof(x));
	if (a1l_solve(m, x, A1L_LINE + 1))
		return -1;
	memcpy(out, x, A1L_VALS * sizeof(*out));
	return 0;
}

int a1lin_fit(const struct a1lin *a, struct a1lin_fit *f)
{
	int c, b, i, j, ok = 0;

	memset(f, 0, sizeof(*f));
	for (c = 0; c < 2; c++) {
		const struct a1lin_side *s = &a->s[c];

		f->n[c] = s->n;
		if (s->n < 10000 || a1l_curve(s->a, s->b, f->path[c]) ||
		    a1l_curve(s->la, s->lb, f->curve[c]))
			continue;
		// the table at each raw knot r: the v whose raw value it is,
		// v + path(v) = r, less r
		for (i = 0; i < A1L_VALS; i++) {
			double r = a1lin_knot(i), v = r;

			for (j = 0; j < 30; j++)
				v = r - a1l_at(f->path[c], v);
			f->table[c][i] = v - r;
			f->weight[c][i] = s->a[i][i];
		}
		// the profile: per band, the bins' system, three diagonals,
		// pulled to a gain of 1 where nothing stands; the turn's
		// derivative through the same system
		for (b = 0; b < A1L_BANDS; b++) {
			double dia[A1L_ENV], off[A1L_ENV], top = 0, eps;
			double complex r[A1L_ENV], h[A1L_ENV];

			for (j = 0; j < A1L_ENV; j++)
				if (s->pp[b][j][0] > top)
					top = s->pp[b][j][0];
			eps = 1e-6 * top + 1e-9;
			for (j = 0; j < A1L_ENV; j++) {
				dia[j] = s->pp[b][j][0] + eps;
				off[j] = s->pp[b][j][1];
				r[j] = s->yp[b][j] + eps;
				h[j] = s->hp[b][j];
			}
			// Thomas: forward, then back
			for (j = 1; j < A1L_ENV; j++) {
				double k = off[j - 1] / dia[j - 1];

				dia[j] -= k * off[j - 1];
				r[j] -= k * r[j - 1];
				h[j] -= k * h[j - 1];
			}
			r[A1L_ENV - 1] /= dia[A1L_ENV - 1];
			h[A1L_ENV - 1] /= dia[A1L_ENV - 1];
			for (j = A1L_ENV - 2; j >= 0; j--) {
				r[j] = (r[j] - off[j] * r[j + 1]) / dia[j];
				h[j] = (h[j] - off[j] * h[j + 1]) / dia[j];
			}
			for (j = 0; j < A1L_ENV; j++) {
				double num =
					creal(s->yp[b][j] * conj(s->yp[b][j]));
				double den = s->yy[b][j] * s->pp[b][j][0];

				// the bin's coherence, as near as its own sums
				// give it: what should have come explains this
				// much of what came
				f->coh[c][b * A1L_ENV + j] =
					den > 0 ? num / den : 0;
				f->g[c][b * A1L_ENV + j] = r[j];
				// the stage takes t h away: the gain moves by
				// -2 h a radian
				f->dg[c][b * A1L_ENV + j] = -2 * h[j];
				f->frames[c][b * A1L_ENV + j] = s->frames[b][j];
			}
		}
		ok = 1;
	}
	return ok ? 0 : -1;
}

int a1lin_next(const struct a1lin_fit *f, const struct a1lin_tables *in,
	       double step, struct a1lin_tables *out)
{
	int c, k, i, moved = 0;

	*out = *in;
	for (c = 0; c < 2; c++) {
		if (f->n[c] < 10000)
			continue;
		for (k = 0; k < A1L_VALS; k++) {
			long nv;

			if (f->weight[c][k] < A1L_MIN_KNOT)
				continue;
			nv = lrint(in->curve[c][k] +
				   step * (256 * f->table[c][k] -
					   in->curve[c][k]));
			nv = nv > 8192 ? 8192 : nv < -8192 ? -8192 : nv;
			moved += nv != in->curve[c][k];
			out->curve[c][k] = nv;
		}
		for (i = 0; i < A1L_PER; i++) {
			double complex g = f->g[c][i], d = f->dg[c][i];
			double mag = cabs(g), k;
			long ng, nt;

			if (f->frames[c][i] < A1L_MIN_BIN ||
			    f->coh[c][i] < A1L_MIN_COH || !(mag > 0.5) ||
			    !(mag < 2))
				continue;
			ng = lrint(in->gain[c][i] * pow(mag, -step));
			ng = ng < A1L_GAIN_LO ? A1L_GAIN_LO :
			     ng > A1L_GAIN_HI ? A1L_GAIN_HI :
						ng;
			moved += ng != in->gain[c][i];
			out->gain[c][i] = ng;
			// the turn that brings the phase to 0, if a turn
			// moves it here at all: Im(g + d t) = 0
			k = cimag(d * conj(g)) / (mag * mag);
			if (!(k > A1L_MIN_TURN))
				continue;
			nt = lrint(in->turn[c][i] -
				   step * carg(g) / k * A1L_ONE);
			nt = nt > A1L_TURN_MAX	? A1L_TURN_MAX :
			     nt < -A1L_TURN_MAX ? -A1L_TURN_MAX :
						  nt;
			moved += nt != in->turn[c][i];
			out->turn[c][i] = nt;
		}
	}
	return moved;
}

double a1lin_path(const struct a1lin_fit *f, int c, int k)
{
	return f->path[c][k];
}
