// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * phase.c: a pilot tone's phase in blocks of a capture (phase.h). The
 * reference turns as a phasor and the window is cached, so that a block costs
 * two calls of the maths library, which matters on a Celeron at 400 MHz.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "phase.h"

#include <math.h>
#include <stdlib.h>

static double *window;
static size_t window_n;

// the Hann window of n samples, made once for each length
static const double *hann(size_t n)
{
	size_t k;

	if (n == window_n)
		return window;
	free(window);
	window = malloc(n * sizeof(*window));
	window_n = window ? n : 0;
	for (k = 0; window && k < n; k++)
		window[k] = 0.5 - 0.5 * cos(2 * M_PI * (k + 0.5) / n);
	return window;
}

void ph_init(struct ph_est *p, double f, double rate)
{
	p->f = f;
	p->rate = rate;
	p->cycles = 0;
	p->at = 0;
	p->started = 0;
}

double ph_block(struct ph_est *p, const short *s, size_t stride, size_t n,
		double *amp)
{
	const double *w = hann(n);
	double i = 0, q = 0, wsum = 0, step, ref, got, d;
	double cr, ci, sr, si;
	size_t k;

	*amp = 0;
	if (!n || !w)
		return p->cycles / p->f;
	// the reference's phase at the block's start in cycles, kept in [0, 1)
	// so that a long run loses nothing to rounding, then turned a sample at
	// a time
	step = p->f / p->rate;
	ref = fmod((double)p->at * step, 1.0);
	cr = cos(2 * M_PI * ref);
	ci = sin(2 * M_PI * ref);
	sr = cos(2 * M_PI * step);
	si = sin(2 * M_PI * step);
	for (k = 0; k < n; k++) {
		double v = w[k] * s[k * stride], t;

		i += v * cr;
		q -= v * ci;
		wsum += w[k];
		t = cr * sr - ci * si;
		ci = cr * si + ci * sr;
		cr = t;
	}
	*amp = 2 * sqrt(i * i + q * q) / wsum;
	// against the reference, which the symmetric window puts at the middle
	got = atan2(q, i) / (2 * M_PI);
	if (!p->started) {
		p->cycles = got;
		p->started = 1;
	} else {
		d = got - fmod(p->cycles, 1.0);
		d -= floor(d + 0.5);
		p->cycles += d;
	}
	p->at += n;
	return p->cycles / p->f;
}
