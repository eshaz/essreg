// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The driver's rate correction for the simulated chip: resample.c as it is,
 * on a whole stream, in a file of its own, since its types aren't essreg's.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <stdlib.h>
#include <string.h>

#include "../es1869/resample.h"
#include "sim_rs.h"

// a2_trim's item in ppb
long sim_rs_trim(int item)
{
	return es1869_trim_ppb(item);
}

static s16 *rs_tab(int flat)
{
	static s16 *tab[2];

	if (!tab[flat]) {
		tab[flat] = malloc(ES_RS_TABLE * sizeof(*tab[flat]));
		if (!tab[flat])
			abort();
		es1869_rs_table(tab[flat], flat);
	}
	return tab[flat];
}

short *sim_rs_parts(const short *src, unsigned long frames,
		    unsigned int channels, const struct sim_rs_part *parts,
		    int n, unsigned long *made)
{
	static s16 ch[2][ES_RS_WINDOW];
	static u8 ring[8 * (ES_RS_CHUNK + 8)];
	unsigned long pos = 0, o = 0, cap = frames + frames / 128 + 256;
	struct es1869_rs_job j;
	short *data;
	unsigned int c;
	int at = 0;

	data = calloc(cap, 2 * channels);
	if (!data)
		abort();
	memset(&j, 0, sizeof(j));
	j.ch[0] = ch[0];
	j.ch[1] = ch[1];
	j.stereo = channels == 2;
	j.seed = 0x12345;
	while (pos < frames && o + ES_RS_CHUNK + 8 < cap) {
		unsigned long i, chunk = ES_RS_CHUNK, end = frames;
		u32 out;

		while (at + 1 < n && parts[at + 1].from <= pos)
			at++;
		if (at + 1 < n)
			end = parts[at + 1].from;
		if (end - pos < chunk)
			chunk = end - pos;
		if (!parts[at].on) {
			// a copy leaves the fraction behind, as the driver does
			j.frac = 0;
			for (i = 0; i < chunk * channels; i++)
				data[o * channels + i] = src[pos * channels + i];
			o += chunk;
			pos += chunk;
			continue;
		}
		j.step = es1869_clock_q32(parts[at].ppb);
		j.dither = parts[at].dither;
		j.tab = rs_tab(parts[at].treble);
		j.centre = parts[at].treble ? 1 : 2;
		// the frames around the run, silence before and after the stream
		for (i = 0; i < ES_RS_WINDOW; i++) {
			long f = (long)(pos + i) - ES_RS_BEHIND;

			for (c = 0; c < channels; c++)
				ch[c][i] = f >= 0 && f < (long)frames ?
						   src[f * channels + c] : 0;
		}
		j.dst = ring;
		j.frames = chunk;
		j.room = ES_RS_CHUNK + 8;
		out = es1869_rs_run_c(&j);
		for (i = 0; i < out * channels; i++)
			data[o * channels + i] = ring[4 * i] | ring[4 * i + 2] << 8;
		o += out;
		pos += j.used;
	}
	*made = o;
	return data;
}

// a stream of 16-bit frames as the ring would hold it, with ppb the change
// of rate and sign_768 how the 768 kHz clock's offset adds to it, or NULL
// with nothing to change; a buffer of *made frames for the caller to free
short *sim_rs_stream(const short *src, unsigned long frames,
		     unsigned int channels, long ppb, int sign_768,
		     unsigned long *made)
{
	struct sim_rs_part part = { 0, ppb, 1, 0, 0 };

	if (!ppb && !sign_768)
		return NULL;
	if (sign_768)
		part.ppb += (long)((double)sign_768 *
				   es1869_clock_base(768000) * 1e9 / 4294967296.0);
	return sim_rs_parts(src, frames, channels, &part, 1, made);
}

double sim_rs_cycles(unsigned int channels, int mmx)
{
#if defined(__i386__) || defined(__x86_64__)
	static s16 ch[2][4][4096 + ES_RS_TAPS + 8] __aligned(8);
	static u8 ring[16 * 4096];
	struct es1869_rs_job j;
	unsigned long long best = ~0ULL;
	unsigned int i, c, k;
	int pass;

	memset(&j, 0, sizeof(j));
	for (c = 0; c < 2; c++) {
		for (k = 0; k < 4; k++)
			for (i = 0; i < 4096 + ES_RS_TAPS + 2; i++)
				ch[c][k][i] = (s16)((i + k) * (c ? 2246822519U :
							       2654435761U) >> 16) / 4;
		j.ch[c] = ch[c][0];
		for (k = 0; k < 3; k++)
			j.odd[c][k] = ch[c][k + 1];
	}
	j.tab = rs_tab(0);
	j.centre = 2;
	j.dst = ring;
	j.frames = 4096;
	j.room = 4096 + 8;
	j.stereo = channels == 2;
	j.step = es1869_clock_q32(30000);
	for (pass = 0; pass < 20; pass++) {
		unsigned long long t;

		t = __builtin_ia32_rdtsc();
#ifdef ES1869_RS_MMX
		if (mmx)
			es1869_rs_run_mmx(&j);
		else
#endif
			es1869_rs_run_c(&j);
		t = __builtin_ia32_rdtsc() - t;
		if (t < best)
			best = t;
	}
	return (double)best / 4096;
#else
	(void)channels;
	(void)mmx;
	return 0;
#endif
}
