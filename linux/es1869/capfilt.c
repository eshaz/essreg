// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The filter on Audio 1's recordings (capfilt.h).
 *
 * Notes:
 *
 * The record path rolls off at the top of the band: on the Armada, through
 * 69h at 44.1 kHz, -0.9 dB at 10 kHz and -4.2 dB at 20 kHz on the left, -1.3
 * and -5.4 dB on the right, measured against Audio 2 at 2x, whose DAC the
 * treble correction holds flat to 20 kHz. Most of that is the chip's
 * switched-capacitor filter, whose corner ESS sets at 0.875 of half the rate
 * (filter_corner, register A2h), and the rest the analog path before it. The
 * filter's taps come from a measurement of that path (es1869scope's loop view)
 * and differ between the channels.
 *
 * A pass copies its frames after the last raw ones, so that each output reads
 * the raw samples around it in one place, and moves the newest raw ones to the
 * front for the next pass. The two taps at the same distance share a multiply.
 * The sum is 64 bits wide, since a tap of up to 2 times a sum of two samples
 * leaves 32 bits behind, and is rounded and clipped to 16 bits.
 *
 * The linearity stage takes back what the record path does to a sample's
 * size, after the FIR, in the same pass and with no more delay. On the
 * Armada the path compresses small samples about a centre some 15 LSB under
 * the recording's mean, by up to 4 LSB with a knee near 50 LSB (es1869scope
 * -C and -E measure it, LOOP_TRANSFORM.md), and turns a small signal's phase
 * back by up to a degree. First a curve about each side's centre adds what
 * the path took, between knots at 0, 2, 4, 6, 8, 12, 16 and on in half
 * octaves to 512, and its last value past that. Then the analytic signal: the
 * FIR's output with its Hilbert transform, from a FIR of 31 taps on the same
 * raw samples, whose odd taps alone count, and the FIR's output less its DC,
 * followed over 2^15 frames from the curve's centre, since the ADC's offset
 * of some -175 LSB would read as the envelope of every quiet signal. Its
 * power and its phase advance from frame to frame, smoothed over 256 frames,
 * give the envelope and the instantaneous frequency, which pick a gain and a
 * turn of the phase from the
 * tables, by band and by envelope between bins an octave apart. The turn
 * adds the Hilbert transform: Re((y + jh) e^(jt)) = y - t h for a small t.
 *
 * The Hilbert FIR is short, so below some 2 kHz it falls short of the true
 * transform (0.61 at 1 kHz, 0.14 at 200 Hz). The bands' edges and the
 * envelope are what it gives, not the signal's true ones, and the tables
 * take that in: es1869scope measures what is left on the stage's own terms
 * and moves the tables until nothing is.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */
#include "capfilt.h"

// the Hilbert FIR's odd taps from 1 to 15: 2 / (pi k) under a Kaiser window
// (beta 6), in 1/16384, within 0.1 % from 3 to 18 kHz at 44.1 kHz
static const s16 cl_hilb[] = { 10304, 3113, 1527, 795, 393, 171, 58, 10 };

// the bands' edges, the phase advance against the power in 1/16384: 200, 800
// and 3200 Hz at 44.1 kHz, through the FIR above
static const s32 cl_edge[ES_CL_BANDS - 1] = { 124, 1508, 7214 };

void es1869_cf_reset(struct es1869_cf *cf)
{
	int c;

	memset(cf->raw, 0, sizeof(cf->raw));
	memset(cf->cl_pow, 0, sizeof(cf->cl_pow));
	memset(cf->cl_adv, 0, sizeof(cf->cl_adv));
	memset(cf->cl_re, 0, sizeof(cf->cl_re));
	memset(cf->cl_im, 0, sizeof(cf->cl_im));
	for (c = 0; c < 2; c++)
		cf->cl_dc[c] = (s64)cf->cl.centre[c] * 16 * (1 << ES_CL_DC);
}

void es1869_cl_identity(struct es1869_cl *cl)
{
	int c, i;

	memset(cl, 0, sizeof(*cl));
	for (c = 0; c < 2; c++)
		for (i = 0; i < ES_CL_PER; i++)
			cl->gain[c][i] = ES_CF_ONE;
}

s64 es1869_cl_hilbert(const s16 *x)
{
	s64 h = 0;
	u32 k;

	for (k = 0; k < ARRAY_SIZE(cl_hilb); k++) {
		s32 o = 2 * k + 1;

		h += (s64)cl_hilb[k] * (x[-o] - x[o]);
	}
	return h;
}

void es1869_cl_place(s64 pow, s64 adv, u32 *band, u32 *pos)
{
	u64 p = pow > 1 ? pow : 1, under;
	u32 b = 0, top, l8;
	s32 at;

	while (b < ES_CL_BANDS - 1 && adv * ES_CF_ONE >= cl_edge[b] * (s64)p)
		b++;
	// log2 of the power in 1/256: its top bit, then the 8 bits under it
	top = fls64(p) - 1;
	under = top >= 8 ? p >> (top - 8) : p << (8 - top);
	l8 = top << 8 | (u32)(under & 255);
	// the power is in (1/16 LSB)^2 with 8 bits more, smoothed, and bin j
	// an envelope of 4 << j LSB, so it sits at log2 of 2^16 * 16 * 4^j
	at = ((s32)l8 - (20 << 8)) / 2;
	*pos = clamp(at, 0, (ES_CL_ENV - 1) << 8);
	*band = b;
}

// the curve's value u from its centre, in 1/256 LSB, between its knots: 0
// and 2 apart to 8, then two a power of two, the half-way and the next
static s32 cl_curve(const s16 *v, s32 u)
{
	const s16 *mid = v + ES_CL_KNOTS - 1;
	s32 a = u < 0 ? -u : u, dir = u < 0 ? -1 : 1, k0, lo, hi;
	u32 i, sh, m;

	if (a >= ES_CL_REACH)
		return mid[dir * (ES_CL_KNOTS - 1)];
	if (a < 4) {
		i = a >> 1;
		sh = 1;
	} else {
		m = fls(a) - 1;
		sh = m - 1;
		i = 2 * m - 2 + ((a >> (m - 1)) & 1);
	}
	k0 = i < 2 ? 2 * i : (2 + (i & 1)) << (((i + 2) >> 1) - 1);
	lo = mid[dir * (s32)i];
	hi = mid[dir * (s32)(i + 1)];
	return lo + (((hi - lo) * (a - k0)) >> sh);
}

// a table by envelope at pos, between its bins
static s32 cl_lerp(const s16 *t, u32 pos)
{
	u32 j = pos >> 8, f = pos & 255;

	if (j >= ES_CL_ENV - 1)
		return t[ES_CL_ENV - 1];
	return t[j] + ((((s32)t[j + 1] - t[j]) * (s32)f) >> 8);
}

// the stage on a frame of channel c: acc is the FIR's output in 1/16384
// LSB, x the raw samples about it
static s64 cl_frame(struct es1869_cf *cf, u32 c, s64 acc, const s16 *x)
{
	const struct es1869_cl *cl = &cf->cl;
	s64 h = es1869_cl_hilbert(x), re = acc >> 10, im = h >> 10;
	u32 band, pos, at;
	s32 u, g, t;

	// the analytic signal in 1/16 LSB, about the DC: power and phase
	// advance, smoothed, with ES_CL_SMOOTH bits more than they have, so
	// that the smallest signals still move them
	cf->cl_dc[c] += re - (cf->cl_dc[c] >> ES_CL_DC);
	re -= cf->cl_dc[c] >> ES_CL_DC;
	cf->cl_pow[c] += re * re + im * im - (cf->cl_pow[c] >> ES_CL_SMOOTH);
	cf->cl_adv[c] += im * cf->cl_re[c] - re * cf->cl_im[c] -
			 (cf->cl_adv[c] >> ES_CL_SMOOTH);
	cf->cl_re[c] = re;
	cf->cl_im[c] = im;
	es1869_cl_place(cf->cl_pow[c], cf->cl_adv[c], &band, &pos);
	// the curve, about the side's centre
	u = (s32)((acc + ES_CF_ONE / 2) >> 14) - cl->centre[c];
	acc += (s64)cl_curve(cl->curve[c], u) * (ES_CF_ONE / ES_CL_ONE);
	// the turn, then the gain, so that the one doesn't scale the other
	at = band * ES_CL_ENV;
	g = cl_lerp(cl->gain[c] + at, pos);
	t = cl_lerp(cl->turn[c] + at, pos);
	acc -= (t * h) >> 14;
	return acc + ((acc * (g - ES_CF_ONE)) >> 14);
}

void es1869_cf_identity(struct es1869_cf *cf)
{
	memset(cf->taps, 0, sizeof(cf->taps));
	cf->taps[0][0] = ES_CF_ONE;
	cf->taps[1][0] = ES_CF_ONE;
}

void es1869_cf_run(struct es1869_cf *cf, s16 *buf, u32 n, u32 channels)
{
	const u32 keep = ES_CF_TAPS - 1;

	while (n) {
		u32 k = n < ES_CF_CHUNK ? n : ES_CF_CHUNK, i, c, m;

		for (c = 0; c < channels; c++) {
			const s16 *t = cf->taps[c];
			s16 *w = cf->raw[c];

			for (i = 0; i < k; i++)
				w[keep + i] = buf[i * channels + c];
			for (i = 0; i < k; i++) {
				// the centre is ES_CF_HALF frames back from the
				// newest raw sample this output may read
				const s16 *x = w + i + ES_CF_HALF;
				s64 acc = (s64)t[0] * x[0];
				s64 v;

				for (m = 1; m <= ES_CF_HALF; m++)
					acc += (s64)t[m] * (x[-(s32)m] + x[m]);
				if (cf->cl_on)
					acc = cl_frame(cf, c, acc, x);
				v = (acc + ES_CF_ONE / 2) >> 14;
				buf[i * channels + c] = v > 32767  ? 32767 :
							v < -32768 ? -32768 :
								     (s16)v;
			}
			memmove(w, w + k, keep * sizeof(*w));
		}
		buf += k * channels;
		n -= k;
	}
}
