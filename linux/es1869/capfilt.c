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
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */
#include "capfilt.h"

void es1869_cf_reset(struct es1869_cf *cf)
{
	memset(cf->raw, 0, sizeof(cf->raw));
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
