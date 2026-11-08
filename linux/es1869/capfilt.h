/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * A filter on Audio 1's recordings, with no kernel API, so that it also builds
 * on the host: for each channel a linear-phase FIR of ES_CF_TAPS taps, run in
 * place over 16-bit frames that the DMA wrote, with the last raw frames kept
 * from one run to the next. A frame comes out ES_CF_HALF frames late.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ES1869_CAPFILT_H
#define ES1869_CAPFILT_H

#include "hw_compat.h"

#define ES_CF_HALF 16 // taps each side of the centre
#define ES_CF_TAPS (2 * ES_CF_HALF + 1)
#define ES_CF_ONE 16384 // a tap of 1
#define ES_CF_CHUNK 256 // frames a pass takes

struct es1869_cf {
	// each channel's taps: the centre, then outward, the same both sides
	s16 taps[2][ES_CF_HALF + 1];
	// the raw samples of each channel: the last ES_CF_TAPS - 1, then a pass
	s16 raw[2][ES_CF_TAPS - 1 + ES_CF_CHUNK];
};

/* silence behind the next frame, as at a stream's start */
void es1869_cf_reset(struct es1869_cf *cf);
/* taps that change nothing but the delay */
void es1869_cf_identity(struct es1869_cf *cf);
/* n frames of 1 or 2 channels, filtered in place; mono takes the left's taps */
void es1869_cf_run(struct es1869_cf *cf, s16 *buf, u32 n, u32 channels);

#endif /* ES1869_CAPFILT_H */
