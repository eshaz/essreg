/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * fft.h: a fixed-point FFT of 16-bit samples for the spectrogram, in MMX on
 * an x86 that has it and in C everywhere, which give the same numbers. Each
 * stage halves the values, so a full-scale tone comes out at full scale over
 * the length; the magnitude goes to tenths of a dB through the bit scan and a
 * table, with no floating point per bin.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef FFT_H
#define FFT_H

#include <stdint.h>

#define FFT_MAX_LOG2 12

struct fft {
	unsigned int log2n, n;
	int16_t *tw; /* the twiddles, laid out for two butterflies at a time */
	int16_t *window; /* Hann, in Q15 */
	uint16_t *rev; /* bit reversal */
	int mmx; /* use the MMX loop */
};

/* 0, or -1 without memory or for a length out of 2^4 to 2^FFT_MAX_LOG2 */
int fft_init(struct fft *f, unsigned int log2n, int mmx);
void fft_free(struct fft *f);

/*
 * n samples through the window into z, re and im interleaved, then the FFT in
 * place; z holds 2n values
 */
void fft_run(const struct fft *f, const int16_t *x, int16_t *z);

/* bins 0 to n/2 - 1 of z in tenths of a dB against a full-scale sine */
void fft_db(const struct fft *f, const int16_t *z, int16_t *db);

/* whether this CPU has MMX */
int fft_have_mmx(void);

#endif /* FFT_H */
