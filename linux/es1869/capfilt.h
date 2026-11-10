/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * A filter on Audio 1's recordings, with no kernel API, so that it also builds
 * on the host: for each channel a linear-phase FIR of ES_CF_TAPS taps, run in
 * place over 16-bit frames that the DMA wrote, with the last raw frames kept
 * from one run to the next. A frame comes out ES_CF_HALF frames late.
 *
 * After the FIR, the linearity stage may take back what the record path does
 * to a sample's size: a curve about each side's centre, then a gain and a turn
 * of the phase by the envelope and the instantaneous frequency of the
 * signal's analytic signal (capfilt.c).
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

// the linearity stage's tables
#define ES_CL_KNOTS 17 // the curve's knots from its centre out: 0, 2 .. 512
#define ES_CL_VALS (2 * ES_CL_KNOTS - 1) // its values, from -512 to +512
#define ES_CL_ONE 256 // a curve value of 1 LSB
#define ES_CL_REACH 512 // past this the curve holds its last value
#define ES_CL_ENV 10 // the envelope's bins: 4 LSB to 2048, an octave apart
#define ES_CL_BANDS 4 // the instantaneous frequency's bands
#define ES_CL_PER (ES_CL_BANDS * ES_CL_ENV) // a side's gains, and turns
#define ES_CL_SMOOTH 8 // the envelope's time constant, 2^8 frames
#define ES_CL_POS_ONE 256 // a bin, in what es1869_cl_place() gives

struct es1869_cl {
	s16 centre[2]; // each side's curve centre, a recorded value
	// what the curve adds at each knot, from -512 to +512, in 1/256 LSB
	s16 curve[2][ES_CL_VALS];
	// by band, then envelope bin: the gain, ES_CF_ONE for 1, and the
	// phase turned, ES_CF_ONE for a radian, ahead when positive
	s16 gain[2][ES_CL_PER];
	s16 turn[2][ES_CL_PER];
};

struct es1869_cf {
	// each channel's taps: the centre, then outward, the same both sides
	s16 taps[2][ES_CF_HALF + 1];
	// the raw samples of each channel: the last ES_CF_TAPS - 1, then a pass
	s16 raw[2][ES_CF_TAPS - 1 + ES_CF_CHUNK];
	// the linearity stage, its tables, and each channel's analytic signal:
	// its power and phase advance smoothed, 2^ES_CL_SMOOTH times their
	// mean, and the last frame's
	bool cl_on;
	struct es1869_cl cl;
	s64 cl_pow[2], cl_adv[2];
	s32 cl_re[2], cl_im[2];
};

/* silence behind the next frame, as at a stream's start */
void es1869_cf_reset(struct es1869_cf *cf);
/* taps that change nothing but the delay */
void es1869_cf_identity(struct es1869_cf *cf);
/* n frames of 1 or 2 channels, filtered in place; mono takes the left's taps */
void es1869_cf_run(struct es1869_cf *cf, s16 *buf, u32 n, u32 channels);

/* linearity tables that change nothing */
void es1869_cl_identity(struct es1869_cl *cl);
/*
 * where a channel's smoothed power and phase advance put a frame in the
 * tables: its band, and its envelope's place from bin 0 in 1/256 of a bin
 */
void es1869_cl_place(s64 pow, s64 adv, u32 *band, u32 *pos);
/* the Hilbert transform at x, from the raw samples within 15 either side */
s64 es1869_cl_hilbert(const s16 *x);

#endif /* ES1869_CAPFILT_H */
