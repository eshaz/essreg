/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * phase.h: a pilot tone's phase in blocks of a capture, as a time error. The
 * tone is mixed down at its nominal frequency on the capture's own sample
 * count under a Hann window, so that the phase moves only as the clock that
 * played it moves against the clock that records it.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef PHASE_H
#define PHASE_H

#include <stddef.h>

struct ph_est {
	double f; /* the tone's nominal frequency, Hz */
	double rate; /* the capture's nominal rate, Hz */
	double cycles; /* the last block's phase, unwrapped, in cycles */
	unsigned long long at; /* samples before the next block */
	int started;
};

void ph_init(struct ph_est *p, double f, double rate);

/*
 * A block of n samples of one channel, stride shorts apart. Returns the time
 * error at the block's middle in seconds: positive while the player's clock
 * runs ahead of the capture's. amp gets the tone's amplitude in the samples'
 * units. A block may move the phase by under half a cycle, which at 19.6 kHz
 * and 0.1 s is 255 ppm between the clocks.
 */
double ph_block(struct ph_est *p, const short *s, size_t stride, size_t n,
		double *amp);

#endif /* PHASE_H */
