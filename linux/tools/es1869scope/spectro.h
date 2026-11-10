/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * spectro.h: the spectrogram view: a capture, or a WAV file, through the
 * fixed-point FFT of fft.c, drawn as a waterfall that wraps, frequency across
 * and time down, so that each block changes one row of the screen.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef SPECTRO_H
#define SPECTRO_H

#include "draw.h"

struct spectro;

/*
 * A capture device at rate, or a WAV file (16-bit, one or two channels) when
 * wav isn't NULL, with FFTs of 2^log2n samples, in MMX where the CPU has it
 * and mmx asks. NULL on failure, with the reason on stderr.
 */
struct spectro *spectro_open(const char *dev, unsigned int rate,
			     const char *wav, unsigned int log2n, int mmx);
void spectro_close(struct spectro *s);
/* the colour map from black through purple to yellow, 256 steps */
void spectro_map(uint32_t *map);

/* the whole view again, then rows as blocks come */
void spectro_frame(struct spectro *s, struct canvas *c);
/*
 * what the capture or file has, a row for each full block; returns the rows
 * drawn, or -1 when the capture failed
 */
int spectro_step(struct spectro *s, struct canvas *c);

#endif /* SPECTRO_H */
