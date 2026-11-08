// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * tie.c: a clock's time error against another's, and what is in it
 * (tie.h). x87 doubles throughout: none of it runs per sample of audio.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "tie.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void tie_fit(const double *t, const double *x, size_t n, struct tie_line *l)
{
	double st = 0, sx = 0, stt = 0, stx = 0, d;
	size_t i;

	l->a = l->b = 0;
	if (!n)
		return;
	// about the first point, which keeps big times from swamping the sums
	for (i = 0; i < n; i++) {
		double u = t[i] - t[0];

		st += u;
		sx += x[i];
		stt += u * u;
		stx += u * x[i];
	}
	d = n * stt - st * st;
	if (n < 2 || d == 0) {
		l->a = sx / n;
		return;
	}
	l->b = (n * stx - st * sx) / d;
	l->a = (sx - l->b * st) / n - l->b * t[0];
}

// x[i + 2m] - 2 x[i + m] + x[i]
static double dd(const double *x, size_t i, size_t m)
{
	return x[i + 2 * m] - 2 * x[i + m] + x[i];
}

double tie_adev(const double *x, size_t n, double tau0, size_t m)
{
	double s = 0;
	size_t i;

	if (!m || n < 2 * m + 1)
		return NAN;
	for (i = 0; i + 2 * m < n; i++) {
		double d = dd(x, i, m);

		s += d * d;
	}
	return sqrt(s / (2.0 * (n - 2 * m))) / (m * tau0);
}

double tie_mdev(const double *x, size_t n, double tau0, size_t m)
{
	double s = 0, inner = 0;
	size_t i, j, terms;

	if (!m || n < 3 * m)
		return NAN;
	// the inner sum slides along, one term in and one out
	for (i = 0; i < m; i++)
		inner += dd(x, i, m);
	terms = n - 3 * m + 1;
	for (j = 0; j < terms; j++) {
		s += inner * inner;
		if (j + 1 < terms)
			inner += dd(x, j + m, m) - dd(x, j, m);
	}
	return sqrt(s / (2.0 * terms)) / ((double)m * m * tau0);
}

double tie_tdev(const double *x, size_t n, double tau0, size_t m)
{
	return m * tau0 / sqrt(3.0) * tie_mdev(x, n, tau0, m);
}

double tie_acov(const double *x1, const double *x2, size_t n, double tau0,
		size_t m)
{
	double s = 0;
	size_t i;

	if (!m || n < 2 * m + 1)
		return NAN;
	for (i = 0; i + 2 * m < n; i++)
		s += dd(x1, i, m) * dd(x2, i, m);
	return s / (2.0 * (n - 2 * m)) / ((double)m * m * tau0 * tau0);
}

void tie_hat(const double *d1, const double *d2, size_t n, double tau0,
	     size_t m, double var[3])
{
	double a1 = tie_adev(d1, n, tau0, m), a2 = tie_adev(d2, n, tau0, m);
	double b = -tie_acov(d1, d2, n, tau0, m);

	var[0] = a1 * a1 - b;
	var[1] = b;
	var[2] = a2 * a2 - b;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return x < y ? -1 : x > y;
}

static double median_of(double *v, size_t n)
{
	qsort(v, n, sizeof(*v), cmp_double);
	return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

size_t tie_pulses(const double *r, size_t n, double k, size_t *idx, size_t max,
		  double *median, double *mad)
{
	double *v, med, dev;
	size_t i, found = 0;

	*median = *mad = 0;
	if (!n)
		return 0;
	v = malloc(n * sizeof(*v));
	if (!v)
		return 0;
	memcpy(v, r, n * sizeof(*v));
	med = median_of(v, n);
	for (i = 0; i < n; i++)
		v[i] = fabs(r[i] - med);
	dev = median_of(v, n);
	free(v);
	*median = med;
	*mad = dev;
	for (i = 0; i < n; i++)
		if (fabs(r[i] - med) > k * dev) {
			if (found < max)
				idx[found] = i;
			found++;
		}
	return found;
}

void tie_fft(double *z, unsigned int log2len)
{
	size_t n = (size_t)1 << log2len, i, j, len;

	for (i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;

		for (; j & bit; bit >>= 1)
			j ^= bit;
		j |= bit;
		if (i < j) {
			double re = z[2 * i], im = z[2 * i + 1];

			z[2 * i] = z[2 * j];
			z[2 * i + 1] = z[2 * j + 1];
			z[2 * j] = re;
			z[2 * j + 1] = im;
		}
	}
	for (len = 2; len <= n; len <<= 1) {
		double a = -2 * M_PI / len;
		double wr = cos(a), wi = sin(a);

		for (i = 0; i < n; i += len) {
			double cr = 1, ci = 0;
			size_t k;

			for (k = 0; k < len / 2; k++) {
				double *p = z + 2 * (i + k);
				double *q = z + 2 * (i + k + len / 2);
				double tr = q[0] * cr - q[1] * ci;
				double ti = q[0] * ci + q[1] * cr;
				double t;

				q[0] = p[0] - tr;
				q[1] = p[1] - ti;
				p[0] += tr;
				p[1] += ti;
				t = cr * wr - ci * wi;
				ci = cr * wi + ci * wr;
				cr = t;
			}
		}
	}
}

int tie_psd(const double *x, size_t n, double tau0, unsigned int log2len,
	    double *psd)
{
	size_t len = (size_t)1 << log2len, half = len / 2, i, start;
	double *z, *t, wsum = 0;
	int windows = 0;

	for (i = 0; i <= half; i++)
		psd[i] = 0;
	if (n < len || !log2len)
		return 0;
	z = malloc(2 * len * sizeof(*z));
	t = malloc(len * sizeof(*t));
	if (!z || !t) {
		free(z);
		free(t);
		return -1;
	}
	for (i = 0; i < len; i++) {
		double w = 0.5 - 0.5 * cos(2 * M_PI * i / len);

		wsum += w * w;
		t[i] = i;
	}
	for (start = 0; start + len <= n; start += half) {
		struct tie_line l;

		tie_fit(t, x + start, len, &l);
		for (i = 0; i < len; i++) {
			double w = 0.5 - 0.5 * cos(2 * M_PI * i / len);

			z[2 * i] = w * (x[start + i] - l.a - l.b * i);
			z[2 * i + 1] = 0;
		}
		tie_fft(z, log2len);
		for (i = 0; i <= half; i++) {
			double re = z[2 * i], im = z[2 * i + 1];

			psd[i] += (i && i < half ? 2 : 1) *
				  (re * re + im * im) * tau0 / wsum;
		}
		windows++;
	}
	for (i = 0; i <= half; i++)
		psd[i] /= windows;
	free(z);
	free(t);
	return windows;
}

#define PERIOD_BIN 0.0005 // s

double tie_period(const double *t, size_t n, double pmin, double pmax,
		  double *strength)
{
	size_t nb = (size_t)(pmax / PERIOD_BIN) + 1, lo = pmin / PERIOD_BIN;
	size_t i, j, b, best = 0, first;
	double *h, mean = 0, at;

	*strength = 0;
	if (n < 8 || pmin <= 0 || pmax <= pmin || lo + 2 >= nb)
		return 0;
	h = calloc(nb, sizeof(*h));
	if (!h)
		return 0;
	// every pair of pulses less than pmax apart, by the time between them
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n && t[j] - t[i] < pmax; j++)
			h[(size_t)((t[j] - t[i]) / PERIOD_BIN)]++;
	for (b = lo; b < nb; b++) {
		mean += h[b];
		if (h[b] > h[best] || best < lo)
			best = b;
	}
	mean /= nb - lo;
	if (h[best] <= mean) {
		free(h);
		return 0;
	}
	// the fundamental: the first bin that stands nearly as tall, since its
	// multiples stand too
	for (first = lo; first < best; first++)
		if (h[first] >= 0.7 * h[best] && h[first] >= h[first - 1] &&
		    h[first] >= h[first + 1])
			break;
	// the centre of the peak, from the bins beside it
	at = first + 0.5;
	if (first > 0 && first + 1 < nb) {
		double l = h[first - 1], c = h[first], r = h[first + 1];

		at += (r - l) / (2 * (2 * c - l - r) + 1e-30) * 0.5;
	}
	// standard deviations above the mean, as counts of pairs go
	*strength = (h[first] - mean) / sqrt(mean > 1 ? mean : 1);
	free(h);
	return at * PERIOD_BIN;
}
