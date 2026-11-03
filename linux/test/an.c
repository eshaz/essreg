// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * es1869test's analysis, see an.h.
 *
 * Notes:
 *
 * Sums over a recording are long doubles, which the x87 keeps at full
 * precision, so that a fit over a million samples loses nothing in them.
 * The sines that the fits and the tone filter need come from a rotation by
 * one sample's angle, set to the exact values again every 256 samples.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "an.h"

#define PI 3.14159265358979323846264338327950288L
#define PEAK_MAX (1L << 20) // an_peak_hz's longest transform
#define OSC_EXACT 256 // samples between a rotation's exact values
#define FIT_ITER 30 // Gauss-Newton steps at most
#define FIT_TOL 1e-10 // the relative change of frequency that ends them

// --- helpers -----------------------------------------------------------------

// the smallest power of 2 at least n
static long pow2(long n)
{
	long p = 1;

	while (p < n)
		p <<= 1;
	return p;
}

static double mean(const double *x, long n)
{
	long double sum = 0;
	long i;

	for (i = 0; i < n; i++)
		sum += x[i];
	return n > 0 ? sum / n : 0;
}

static void swap(double *a, double *b)
{
	double t = *a;

	*a = *b;
	*b = t;
}

// an angle in -pi..pi
static double wrap(long double a)
{
	a = fmodl(a, 2 * PI);
	if (a > PI)
		a -= 2 * PI;
	else if (a < -PI)
		a += 2 * PI;
	return a;
}

// --- sines -------------------------------------------------------------------

// cos and sin of w (i - m) for i = 0, 1, 2 ..., by a rotation
struct osc {
	double w, m;
	double cw, sw; // one sample's rotation
	double c, s;
	long i;
};

// the exact values at i
static void osc_exact(struct osc *o)
{
	long double a = (long double)o->w * (o->i - o->m);

	o->c = cosl(a);
	o->s = sinl(a);
}

static void osc_init(struct osc *o, double w, double m)
{
	o->w = w;
	o->m = m;
	o->cw = cos(w);
	o->sw = sin(w);
	o->i = 0;
	osc_exact(o);
}

// on to the next sample
static inline void osc_next(struct osc *o)
{
	double c = o->c;

	if (++o->i % OSC_EXACT == 0) {
		osc_exact(o);
		return;
	}
	o->c = c * o->cw - o->s * o->sw;
	o->s = o->s * o->cw + c * o->sw;
}

// --- transforms --------------------------------------------------------------

static void tw_set(double *w, long k, double re, double im)
{
	w[2 * k] = re;
	w[2 * k + 1] = im;
}

// e^(-2 pi i k / n) for k < n / 2, real and imaginary parts in turn: an
// eighth of a turn computed and the rest by symmetry
static void fft_twiddles(double *w, long n)
{
	long h = n / 2, q = n / 4, k;

	if (n < 8) {
		for (k = 0; k < h; k++)
			tw_set(w, k, cos(2 * PI * k / n), -sin(2 * PI * k / n));
		return;
	}
	for (k = 0; k <= n / 8; k++) {
		double c = cos(2 * PI * k / n), s = sin(2 * PI * k / n);

		tw_set(w, k, c, -s);
		tw_set(w, q - k, s, -c);
		if (k) {
			tw_set(w, q + k, -s, -c);
			tw_set(w, h - k, -c, -s);
		}
	}
}

// the butterflies of the blocks of len, with the twiddles of the table w,
// or computed without one
static void fft_pass(double *re, double *im, long n, long len, const double *w)
{
	long half = len / 2, step = n / len, i, j;

	for (i = 0; i < n; i += len) {
		for (j = 0; j < half; j++) {
			long a = i + j, b = a + half;
			double c, s, tr, ti;

			if (w) {
				c = w[2 * j * step];
				s = w[2 * j * step + 1];
			} else {
				c = cos(2 * PI * j / len);
				s = -sin(2 * PI * j / len);
			}
			tr = c * re[b] - s * im[b];
			ti = c * im[b] + s * re[b];
			re[b] = re[a] - tr;
			im[b] = im[a] - ti;
			re[a] += tr;
			im[a] += ti;
		}
	}
}

void an_fft(double *re, double *im, long n)
{
	double *w;
	long i, j, len;

	if (n < 2 || (n & (n - 1)))
		return;
	// bit-reversed order
	for (i = 1, j = 0; i < n; i++) {
		long bit = n >> 1;

		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			swap(&re[i], &re[j]);
			swap(&im[i], &im[j]);
		}
	}
	w = malloc(n * sizeof(*w));
	if (w)
		fft_twiddles(w, n);
	for (len = 2; len <= n; len <<= 1)
		fft_pass(re, im, n, len, w);
	free(w);
}

// the inverse transform, scaled by 1 / n
static void fft_inv(double *re, double *im, long n)
{
	long i;

	for (i = 0; i < n; i++)
		im[i] = -im[i];
	an_fft(re, im, n);
	for (i = 0; i < n; i++) {
		re[i] /= n;
		im[i] = -im[i] / n;
	}
}

// x limited to lo-hi Hz (0 for no limit) into y, which may be x, by
// zeroing the other bins of a transform padded to at least 2n; 0, or -1
// without memory
static int bandpass(const double *x, long n, double rate, double lo, double hi,
		    double *y)
{
	long len = pow2(2 * n), k;
	double *re = calloc(2 * len, sizeof(*re)), *im;

	if (!re)
		return -1;
	im = re + len;
	memcpy(re, x, n * sizeof(*re));
	an_fft(re, im, len);
	for (k = 0; k <= len / 2; k++) {
		double f = k * rate / len;
		long j = (len - k) % len; // its negative frequency

		if ((lo > 0 && f < lo) || (hi > 0 && f > hi)) {
			re[k] = 0;
			im[k] = 0;
			re[j] = 0;
			im[j] = 0;
		}
	}
	fft_inv(re, im, len);
	memcpy(y, re, n * sizeof(*y));
	free(re);
	return 0;
}

// --- levels and tones --------------------------------------------------------

double an_rms(const double *x, long n, int ac)
{
	long double sum = 0, mu = 0;
	long i;

	if (n <= 0)
		return 0;
	if (ac)
		mu = mean(x, n);
	for (i = 0; i < n; i++)
		sum += (x[i] - mu) * (x[i] - mu);
	return sqrtl(sum / n);
}

double an_dbfs(double rms)
{
	if (rms <= 0)
		return -200;
	return 20 * log10(rms / (32767 / sqrt(2)));
}

double an_tone(const double *x, long n, double rate, double f, double *phase)
{
	long double re = 0, im = 0, sum = 0;
	struct osc t, h;
	long i;

	if (phase)
		*phase = 0;
	if (n < 2 || !(rate > 0))
		return 0;
	osc_init(&t, 2 * PI * f / rate, 0);
	osc_init(&h, 2 * PI / n, 0);
	for (i = 0; i < n; i++) {
		double win = 0.5 - 0.5 * h.c;

		sum += win;
		re += (long double)win * x[i] * t.c;
		im += (long double)win * x[i] * t.s;
		osc_next(&t);
		osc_next(&h);
	}
	// amp sin(w i + phase) leaves re = amp sum sin(phase) / 2 and
	// im = amp sum cos(phase) / 2
	if (phase)
		*phase = atan2l(re, im);
	return 2 * sqrtl(re * re + im * im) / sum;
}

static double power(const double *re, const double *im, long k)
{
	return re[k] * re[k] + im[k] * im[k];
}

// x's n samples without their mean and through a Hann window, into y
static void hann(const double *x, long n, double *y)
{
	double mu = mean(x, n);
	struct osc o;
	long i;

	osc_init(&o, 2 * PI / n, 0);
	for (i = 0; i < n; i++) {
		y[i] = (x[i] - mu) * (0.5 - 0.5 * o.c);
		osc_next(&o);
	}
}

// the top of the parabola through the log powers of bin k and its
// neighbours, in bins, when k is a peak
static double peak_bin(const double *re, const double *im, long len, long k)
{
	double a, b, c, d;

	if (k < 1 || k >= len / 2)
		return k;
	a = power(re, im, k - 1);
	b = power(re, im, k);
	c = power(re, im, k + 1);
	if (!(a > 0 && c > 0) || b < a || b < c)
		return k;
	a = log(a);
	b = log(b);
	c = log(c);
	d = a - 2 * b + c;
	return d < 0 ? k + 0.5 * (a - c) / d : k;
}

double an_peak_hz(const double *x, long n, double rate, double lo, double hi)
{
	long len = 1, k, k0, k1, best;
	double *re, b0, b1, f = 0;

	if (n < 2 || !(rate > 0) || !(hi >= lo))
		return 0;
	while (len < PEAK_MAX && len / 4 < n)
		len <<= 1;
	b0 = lo * len / rate;
	b1 = hi * len / rate;
	if (b0 > len / 2 || b1 < 0)
		return 0;
	k0 = b0 > 0 ? (long)ceil(b0) : 0;
	k1 = b1 < len / 2 ? (long)floor(b1) : len / 2;
	if (k0 > k1)
		return 0;
	re = calloc(2 * len, sizeof(*re));
	if (!re)
		return 0;
	// past PEAK_MAX samples, the first PEAK_MAX
	hann(x, n < len ? n : len, re);
	an_fft(re, re + len, len);
	best = k0;
	for (k = k0 + 1; k <= k1; k++)
		if (power(re, re + len, k) > power(re, re + len, best))
			best = k;
	if (power(re, re + len, best) > 0)
		f = peak_bin(re, re + len, len, best) * rate / len;
	free(re);
	return f;
}

// --- sine fits ---------------------------------------------------------------

// the normal equations of a least-squares fit to k columns
struct ne {
	long double a[4][4], b[4]; // a's upper triangle
	int k;
	long n; // samples added
};

static void ne_add(struct ne *e, const double *col, double x)
{
	int r, c;

	for (r = 0; r < e->k; r++) {
		for (c = r; c < e->k; c++)
			e->a[r][c] += (long double)col[r] * col[c];
		e->b[r] += (long double)col[r] * x;
	}
	e->n++;
}

// the fit's parameters, by Cholesky on the equations scaled to a unit
// diagonal; 0, or -1 when a column is empty or follows from the others
static int ne_solve(const struct ne *e, double *p)
{
	long double l[4][4], d[4], y[4], v;
	int k = e->k, r, c, j;

	for (j = 0; j < k; j++) {
		if (!(e->a[j][j] > 1e-20L * e->n))
			return -1;
		d[j] = 1 / sqrtl(e->a[j][j]);
	}
	for (j = 0; j < k; j++) {
		for (r = j; r < k; r++) {
			v = e->a[j][r] * d[j] * d[r];
			for (c = 0; c < j; c++)
				v -= l[r][c] * l[j][c];
			if (r > j) {
				l[r][j] = v / l[j][j];
				continue;
			}
			if (!(v > 1e-12L))
				return -1;
			l[j][j] = sqrtl(v);
		}
	}
	// l y = d b, then l' y = y in place
	for (r = 0; r < k; r++) {
		v = e->b[r] * d[r];
		for (c = 0; c < r; c++)
			v -= l[r][c] * y[c];
		y[r] = v / l[r][r];
	}
	for (r = k - 1; r >= 0; r--) {
		v = y[r];
		for (c = r + 1; c < k; c++)
			v -= l[c][r] * y[c];
		y[r] = v / l[r][r];
	}
	for (r = 0; r < k; r++)
		p[r] = y[r] * d[r];
	return 0;
}

// x[i] = p0 cos(w (i - m)) + p1 sin(w (i - m)) + p2; 0, or -1
static int fit3(const double *x, long n, double w, double m, double *p)
{
	struct ne e = { .k = 3 };
	struct osc o;
	long i;

	osc_init(&o, w, m);
	for (i = 0; i < n; i++) {
		double col[3] = { o.c, o.s, 1 };

		ne_add(&e, col, x[i]);
		osc_next(&o);
	}
	return ne_solve(&e, p);
}

// a Gauss-Newton step from the sine p0, p1 at w: the new p0-p2, and the
// change of w in p3; 0, or -1
static int fit4(const double *x, long n, double w, double m, double *p)
{
	struct ne e = { .k = 4 };
	struct osc o;
	long i;

	osc_init(&o, w, m);
	for (i = 0; i < n; i++) {
		double col[4] = { o.c, o.s, 1,
				  (i - m) * (p[1] * o.c - p[0] * o.s) };

		ne_add(&e, col, x[i]);
		osc_next(&o);
	}
	return ne_solve(&e, p);
}

// w refined by fits of four parameters, as IEEE 1057 has them; 0, or -1
// when w leaves wlo-whi
static int fit_refine(const double *x, long n, double m, double *w, double wlo,
		      double whi)
{
	double p[4];
	int it;

	if (fit3(x, n, *w, m, p))
		return -1;
	for (it = 0; it < FIT_ITER; it++) {
		if (fit4(x, n, *w, m, p))
			return -1;
		*w += p[3];
		if (!isfinite(*w) || *w < wlo || *w > whi)
			return -1;
		if (fabs(p[3]) < FIT_TOL * *w)
			break;
	}
	return 0;
}

// x less the sine p at w, to r unless NULL; its rms
static double fit_resid(const double *x, long n, double w, double m,
			const double *p, double *r)
{
	long double sum = 0;
	struct osc o;
	long i;

	osc_init(&o, w, m);
	for (i = 0; i < n; i++) {
		double v = x[i] - (p[0] * o.c + p[1] * o.s + p[2]);

		if (r)
			r[i] = v;
		sum += (long double)v * v;
		osc_next(&o);
	}
	return n > 0 ? sqrtl(sum / n) : 0;
}

// the fit of an_fit_sine and an_thdn: w, and p as fit3 has it about the
// middle sample; 0, or -1
static int fit_sine(const double *x, long n, double rate, double f0, int refine,
		    double *w, double *p)
{
	double m = (n - 1) / 2.0;

	if (n < 3 || !(rate > 0) || !(f0 > 0) || f0 >= rate / 2)
		return -1;
	*w = 2 * PI * f0 / rate;
	if (refine) {
		double f = an_peak_hz(x, n, rate, 0.8 * f0, 1.2 * f0);

		if (f > 0)
			*w = 2 * PI * f / rate;
		if (fit_refine(x, n, m, w, PI * f0 / rate,
			       fmin(4 * PI * f0 / rate, PI)))
			return -1;
	}
	return fit3(x, n, *w, m, p);
}

int an_fit_sine(const double *x, long n, double rate, double f0, int refine,
		struct an_fit *fit)
{
	double w, p[3], m = (n - 1) / 2.0;

	memset(fit, 0, sizeof(*fit));
	if (fit_sine(x, n, rate, f0, refine, &w, p))
		return -1;
	fit->f = w * rate / (2 * PI);
	fit->amp = hypot(p[0], p[1]);
	// p0 = amp sin(phase), p1 = amp cos(phase) at the middle sample
	fit->phase = wrap(atan2(p[0], p[1]) - (long double)w * m);
	fit->dc = p[2];
	fit->resid = fit_resid(x, n, w, m, p, NULL);
	return 0;
}

double an_thdn(const double *x, long n, double rate, double f0, double lo,
	       double hi)
{
	double w, p[3], amp, rms, *r;

	if (fit_sine(x, n, rate, f0, 1, &w, p))
		return 0;
	amp = hypot(p[0], p[1]);
	r = malloc(n * sizeof(*r));
	if (!r || !(amp > 0))
		goto fail;
	rms = fit_resid(x, n, w, (n - 1) / 2.0, p, r);
	if (lo > 0 || hi > 0) {
		if (bandpass(r, n, rate, lo, hi, r))
			goto fail;
		rms = an_rms(r, n, 0);
	}
	free(r);
	rms /= amp / sqrt(2);
	return rms > 1e-10 ? 20 * log10(rms) : -200;
fail:
	free(r);
	return 0;
}

// --- pitch -------------------------------------------------------------------

// the rising zero crossings of y: their count, and the times of crossings
// first and last, counted from 0
static long crossings(const double *y, long n, long first, long last,
		      double *t1, double *t2)
{
	long i, c = 0;

	for (i = 0; i + 1 < n; i++) {
		if (y[i] < 0 && y[i + 1] >= 0) {
			double t = i + y[i] / (y[i] - y[i + 1]);

			if (c == first)
				*t1 = t;
			if (c == last)
				*t2 = t;
			c++;
		}
	}
	return c;
}

double an_pitch(const double *x, long n, double rate, double f0)
{
	double *y, t1 = 0, t2 = 0;
	long c, skip;

	if (n < 2 || !(rate > 0) || !(f0 > 0))
		return 0;
	y = malloc(n * sizeof(*y));
	if (!y)
		return 0;
	if (bandpass(x, n, rate, 0.85 * f0, 1.15 * f0, y)) {
		free(y);
		return 0;
	}
	c = crossings(y, n, -1, -1, &t1, &t2);
	// the band's edges ring, so past 10 crossings 3 at each end go
	skip = c > 10 ? 3 : 0;
	if (c >= 3)
		crossings(y, n, skip, c - 1 - skip, &t1, &t2);
	free(y);
	if (c < 3 || !(t2 > t1))
		return 0;
	return (c - 1 - 2 * skip) * rate / (t2 - t1);
}

// --- glitches ----------------------------------------------------------------

static int cmp_double(const void *a, const void *b)
{
	double u = *(const double *)a, v = *(const double *)b;

	return (u > v) - (u < v);
}

// the rms left in n samples after a sine of w and a dc are fitted, or
// after the dc alone when the sine can't be
static double win_resid(const double *x, long n, double w)
{
	double p[3], m = (n - 1) / 2.0;

	if (fit3(x, n, w, m, p))
		return an_rms(x, n, 1);
	return fit_resid(x, n, w, m, p, NULL);
}

int an_glitches(const double *x, long n, double rate, double f, long win,
		double thr, long *where, int max)
{
	long step = win / 2, nw, j;
	double *res, *sorted, w, lim;
	int count = 0, prev = 0;

	if (win < 3 || n < win || !(rate > 0) || !(f > 0) || f >= rate / 2)
		return 0;
	nw = (n - win) / step + 1;
	res = malloc(2 * nw * sizeof(*res));
	if (!res)
		return 0;
	sorted = res + nw;
	w = 2 * PI * f / rate;
	for (j = 0; j < nw; j++)
		res[j] = win_resid(x + j * step, win, w);
	memcpy(sorted, res, nw * sizeof(*res));
	qsort(sorted, nw, sizeof(*sorted), cmp_double);
	lim = nw % 2 ? sorted[nw / 2] :
		       (sorted[nw / 2 - 1] + sorted[nw / 2]) / 2;
	// a median of at least 1, for a tone that fits exactly
	lim = fmax(lim, 1.0) * pow(10, thr / 20);
	// a run of windows over the limit is one glitch
	for (j = 0; j < nw; j++) {
		int bad = res[j] > lim;

		if (bad && !prev) {
			if (where && count < max)
				where[count] = j * step;
			count++;
		}
		prev = bad;
	}
	free(res);
	return count;
}

// --- delays ------------------------------------------------------------------

// the transform of a + ib made into conj(A) B, A and B the transforms of
// the real a and b
static void cross_spectrum(double *re, double *im, long len)
{
	long k;

	for (k = 0; k <= len / 2; k++) {
		long j = (len - k) % len;
		double ar = (re[k] + re[j]) / 2, ai = (im[k] - im[j]) / 2;
		double br = (im[k] + im[j]) / 2, bi = (re[j] - re[k]) / 2;

		re[k] = ar * br + ai * bi;
		im[k] = ar * bi - ai * br;
		re[j] = re[k];
		im[j] = -im[k];
	}
}

// the correlation of a[i] and b[i + lag] where both are, each without its
// mean
static double overlap_corr(const double *a, long na, const double *b, long nb,
			   long lag)
{
	long double sab = 0, saa = 0, sbb = 0;
	long i0 = lag < 0 ? -lag : 0, i1 = na < nb - lag ? na : nb - lag, i;
	double ma, mb;

	if (i1 - i0 < 2)
		return 0;
	ma = mean(a + i0, i1 - i0);
	mb = mean(b + i0 + lag, i1 - i0);
	for (i = i0; i < i1; i++) {
		double u = a[i] - ma, v = b[i + lag] - mb;

		sab += (long double)u * v;
		saa += (long double)u * u;
		sbb += (long double)v * v;
	}
	return saa > 0 && sbb > 0 ? sab / sqrtl(saa * sbb) : 0;
}

long an_lag(const double *a, long na, const double *b, long nb, long maxlag,
	    double *corr)
{
	long big = na > nb ? na : nb, len, i, lag, best = 0;
	double *re, *im, ma, mb, top = 0;

	if (corr)
		*corr = 0;
	if (na <= 0 || nb <= 0)
		return 0;
	if (maxlag < 0)
		maxlag = 0;
	if (maxlag >= big)
		maxlag = big - 1;
	// padded so that no lag up to maxlag wraps around
	len = pow2(big + maxlag);
	re = calloc(2 * len, sizeof(*re));
	if (!re)
		return 0;
	im = re + len;
	ma = mean(a, na);
	mb = mean(b, nb);
	for (i = 0; i < na; i++)
		re[i] = a[i] - ma;
	for (i = 0; i < nb; i++)
		im[i] = b[i] - mb;
	an_fft(re, im, len);
	cross_spectrum(re, im, len);
	fft_inv(re, im, len);
	// re[lag mod len] = sum of a[i] b[i + lag]
	for (lag = -maxlag; lag <= maxlag; lag++) {
		double v = re[lag < 0 ? lag + len : lag];

		if (lag == -maxlag || v > top) {
			top = v;
			best = lag;
		}
	}
	free(re);
	if (corr)
		*corr = overlap_corr(a, na, b, nb, best);
	return best;
}

long an_onset(const double *x, long n, double thr)
{
	long i;

	for (i = 0; i < n; i++)
		if (fabs(x[i] - x[0]) > thr)
			return i;
	return -1;
}

// --- test signals ------------------------------------------------------------

unsigned int an_lfsr(unsigned int *state)
{
	unsigned int s = *state & 0xFFFF, bit;
	int i;

	if (!s)
		s = 0xACE1;
	// a step shifts right, with taps 16, 14, 13 and 11 into bit 15
	for (i = 0; i < 16; i++) {
		bit = (s ^ (s >> 2) ^ (s >> 3) ^ (s >> 5)) & 1;
		s = (s >> 1) | (bit << 15);
	}
	*state = s;
	return s;
}
