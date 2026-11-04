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

// a stream of 16-bit frames as the ring would hold it, with ppb the change
// of rate and sign_768 how the 768 kHz clock's offset adds to it, or NULL
// with nothing to change; a buffer of *made frames for the caller to free
short *sim_rs_stream(const short *src, unsigned long frames,
		     unsigned int channels, long ppb, int sign_768,
		     unsigned long *made)
{
	static s16 ch[2][ES_RS_WINDOW];
	static u8 ring[8 * (ES_RS_CHUNK + 8)];
	static s16 *tab;
	unsigned long pos = 0, o = 0, n = frames + 256;
	struct es1869_rs_job j;
	s32 step = es1869_clock_q32(ppb) + sign_768 * es1869_clock_base(768000);
	short *data;
	unsigned int c;

	if (!step)
		return NULL;
	if (!tab) {
		tab = malloc(ES_RS_TABLE * sizeof(*tab));
		if (!tab)
			abort();
		es1869_rs_table(tab);
	}
	data = calloc(n, 2 * channels);
	if (!data)
		abort();
	memset(&j, 0, sizeof(j));
	j.tab = tab;
	j.ch[0] = ch[0];
	j.ch[1] = ch[1];
	j.step = step;
	j.stereo = channels == 2;
	while (pos < frames && o + ES_RS_CHUNK + 8 < n) {
		unsigned long i;
		u32 out;

		// the frames around the run, silence before and after the stream
		for (i = 0; i < ES_RS_WINDOW; i++) {
			long f = (long)(pos + i) - ES_RS_BEHIND;

			for (c = 0; c < channels; c++)
				ch[c][i] = f >= 0 && f < (long)frames ?
						   src[f * channels + c] : 0;
		}
		j.dst = ring;
		j.frames = ES_RS_CHUNK;
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
