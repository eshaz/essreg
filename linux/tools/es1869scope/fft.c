// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * fft.c: a fixed-point FFT of 16-bit samples, in MMX and in C (fft.h).
 *
 * A butterfly takes a and b and the twiddle w, and leaves (a + w b) / 2 and
 * (a - w b) / 2. The complex product rounds as MMX does: each part a sum of
 * two Q15 products in 32 bits, shifted right by 15, saturated to 16 bits. The
 * C loop does the same arithmetic, so the two agree to the bit.
 *
 * The MMX loop takes two butterflies of a stage at once, k and k + 1, whose a
 * and b are each two complex values side by side in a 64-bit register:
 * pmaddwd of b with (wr, -wi) for each gives the real parts of both products,
 * and with (wi, wr) the imaginary ones. A stage of half-length 1 has no pair,
 * and runs in C.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "fft.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int16_t sat16(int32_t v)
{
	return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

int fft_have_mmx(void)
{
#if defined(__i386__) || defined(__x86_64__)
	return __builtin_cpu_supports("mmx");
#else
	return 0;
#endif
}

int fft_init(struct fft *f, unsigned int log2n, int mmx)
{
	unsigned int n = 1u << log2n, i, len, k, at = 0;

	memset(f, 0, sizeof(*f));
	if (log2n < 4 || log2n > FFT_MAX_LOG2)
		return -1;
	f->log2n = log2n;
	f->n = n;
	f->mmx = mmx && fft_have_mmx();
	// eight words for each pair of butterflies k, k + 1, stage by stage:
	// wr, -wi of both, then wi, wr of both, as the MMX loop takes them
	f->tw = malloc(8 * n * sizeof(*f->tw));
	f->window = malloc(n * sizeof(*f->window));
	f->rev = malloc(n * sizeof(*f->rev));
	if (!f->tw || !f->window || !f->rev) {
		fft_free(f);
		return -1;
	}
	for (i = 0; i < n; i++) {
		unsigned int r = 0, b;

		for (b = 0; b < log2n; b++)
			r |= ((i >> b) & 1) << (log2n - 1 - b);
		f->rev[i] = r;
		f->window[i] = (int16_t)lrint(
			32767 * (0.5 - 0.5 * cos(2 * M_PI * i / n)));
	}
	for (len = 2; len <= n; len <<= 1)
		for (k = 0; k < len / 2; k += 2) {
			int16_t *w = f->tw + at;
			int j;

			for (j = 0; j < 2; j++) {
				double a = -2 * M_PI * (k + j) / len;
				int16_t wr = sat16(lrint(32767 * cos(a)));
				int16_t wi = sat16(lrint(32767 * sin(a)));

				w[2 * j] = wr;
				w[2 * j + 1] = (int16_t)-wi;
				w[4 + 2 * j] = wi;
				w[4 + 2 * j + 1] = wr;
			}
			at += 8;
		}
	return 0;
}

void fft_free(struct fft *f)
{
	free(f->tw);
	free(f->window);
	free(f->rev);
	f->tw = f->window = NULL;
	f->rev = NULL;
}

// one butterfly in C, the arithmetic of the MMX loop: wre is wr, -wi and wim
// is wi, wr
static void fly(int16_t *a, int16_t *b, const int16_t *wre, const int16_t *wim)
{
	int32_t tr = ((int32_t)b[0] * wre[0] + (int32_t)b[1] * wre[1]) >> 15;
	int32_t ti = ((int32_t)b[0] * wim[0] + (int32_t)b[1] * wim[1]) >> 15;
	int16_t r = sat16(tr), i = sat16(ti);
	int16_t ar = a[0], ai = a[1];

	a[0] = sat16((int32_t)sat16(ar + r) >> 1);
	a[1] = sat16((int32_t)sat16(ai + i) >> 1);
	b[0] = sat16((int32_t)sat16(ar - r) >> 1);
	b[1] = sat16((int32_t)sat16(ai - i) >> 1);
}

#if defined(__i386__) || defined(__x86_64__)
// two butterflies, k and k + 1, in MMX: a and b point at two complex values
// each, w at the pair's eight twiddle words
static void fly2_mmx(int16_t *a, int16_t *b, const int16_t *w)
{
	__asm__ volatile("movq (%[b]), %%mm0\n\t" // b0r b0i b1r b1i
			 "movq %%mm0, %%mm1\n\t"
			 "pmaddwd (%[w]), %%mm0\n\t" // the real parts, 32 bits
			 "pmaddwd 8(%[w]), %%mm1\n\t" // the imaginary ones
			 "psrad $15, %%mm0\n\t"
			 "psrad $15, %%mm1\n\t"
			 "packssdw %%mm0, %%mm0\n\t" // tr0 tr1, saturated
			 "packssdw %%mm1, %%mm1\n\t" // ti0 ti1
			 "punpcklwd %%mm1, %%mm0\n\t" // tr0 ti0 tr1 ti1
			 "movq (%[a]), %%mm2\n\t"
			 "movq %%mm2, %%mm3\n\t"
			 "paddsw %%mm0, %%mm2\n\t"
			 "psubsw %%mm0, %%mm3\n\t"
			 "psraw $1, %%mm2\n\t"
			 "psraw $1, %%mm3\n\t"
			 "movq %%mm2, (%[a])\n\t"
			 "movq %%mm3, (%[b])\n\t"
			 :
			 : [a] "r"(a), [b] "r"(b), [w] "r"(w)
			 : "memory", "mm0", "mm1", "mm2", "mm3");
}

static void mmx_done(void)
{
	__asm__ volatile("emms");
}
#endif

void fft_run(const struct fft *f, const int16_t *x, int16_t *z)
{
	unsigned int n = f->n, i, len, k, at = 0;

	for (i = 0; i < n; i++) {
		unsigned int r = f->rev[i];

		z[2 * r] = (int16_t)(((int32_t)x[i] * f->window[i]) >> 15);
		z[2 * r + 1] = 0;
	}
	for (len = 2; len <= n; len <<= 1) {
		unsigned int half = len / 2;
		const int16_t *w = f->tw + at;

		for (i = 0; i < n; i += len) {
			int16_t *a = z + 2 * i, *b = z + 2 * (i + half);

#if defined(__i386__) || defined(__x86_64__)
			if (f->mmx && half >= 2) {
				for (k = 0; k < half; k += 2)
					fly2_mmx(a + 2 * k, b + 2 * k,
						 w + 4 * k);
				continue;
			}
#endif
			for (k = 0; k < half; k++) {
				const int16_t *p =
					w + 8 * (k / 2) + 2 * (k % 2);

				fly(a + 2 * k, b + 2 * k, p, p + 4);
			}
		}
		at += 8 * ((half + 1) / 2);
	}
#if defined(__i386__) || defined(__x86_64__)
	if (f->mmx)
		mmx_done();
#endif
}

// log2 of v in 1/64ths, by the top bit and a table of the next six
static int log2_64(uint32_t v)
{
	static uint8_t frac[64];
	static int made;
	int top, i;

	if (!made) {
		for (i = 0; i < 64; i++)
			frac[i] = (uint8_t)lrint(64 * log2(1 + (i + 0.5) / 64));
		made = 1;
	}
	if (!v)
		return -64 * 32;
	top = 31 - __builtin_clz(v);
	i = top >= 6 ? (v >> (top - 6)) & 63 : (v << (6 - top)) & 63;
	return 64 * top + frac[i];
}

void fft_db(const struct fft *f, const int16_t *z, int16_t *db)
{
	unsigned int k;
	// a full-scale sine's bin: 32767 through the Hann window's gain of a
	// half, and the length's halving, is 32767 / 4, squared
	int ref = log2_64((uint32_t)(8192 * 8192));

	for (k = 0; k < f->n / 2; k++) {
		int32_t re = z[2 * k], im = z[2 * k + 1];
		uint32_t p = (uint32_t)(re * re) + (uint32_t)(im * im);

		// 10 log10(p) = 3.0103 log2(p): in tenths, 30.103 / 64 a step
		db[k] = (int16_t)(((log2_64(p) - ref) * 30103) / 64000);
	}
}
