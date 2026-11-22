// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The Linux driver's ESFM engine on the host, for tests/test_esfmgm.py: an
 * engine whose register writes go to a list the tests read.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <stdlib.h>

#include "esfm_gm.h"

#define LX_MAX 65536

static u16 lx_reg[LX_MAX];
static u8 lx_val[LX_MAX];
static unsigned int lx_n;

static void lx_write(void *ctx __attribute__((unused)), u16 reg, u8 val)
{
	if (lx_n < LX_MAX) {
		lx_reg[lx_n] = reg;
		lx_val[lx_n] = val;
		lx_n++;
	}
}

static const struct esfm_gm_io lx_io = { NULL, lx_write };

// opts: bit 0 Vibrato, 1 Tuning, 2 ResetControllers, 3 LivePan, 4 SysEx,
// 5 PedalRelease, 6 SilenceOnClose, 7 the sides (fm_sides, TrueSides), 8
// a440 (esfm_a440)
struct esfm_gm *lx_esfm_new(const u8 *bank, u32 len, unsigned int opts)
{
	struct esfm_gm *g = calloc(1, sizeof(*g));
	struct esfm_gm_opts o = {
		.vibrato = opts & 1, .tuning = opts & 2,
		.reset_controllers = opts & 4, .live_pan = opts & 8,
		.sysex = opts & 16, .pedal_release = opts & 32,
		.silence = opts & 64, .sides = opts & 128,
		.a440 = opts & 256,
	};

	if (g)
		esfm_gm_init(g, &lx_io, bank, len, &o);
	return g;
}

void lx_esfm_free(struct esfm_gm *g)
{
	free(g);
}

// the writes since the last call: each register and value
unsigned int lx_esfm_writes(u16 *reg, u8 *val, unsigned int max)
{
	unsigned int n = lx_n < max ? lx_n : max, i;

	for (i = 0; i < n; i++) {
		reg[i] = lx_reg[i];
		val[i] = lx_val[i];
	}
	lx_n = 0;
	return n;
}

// the voice table, as esfmemu reads ESS's: flags, channel and note
void lx_esfm_voice(const struct esfm_gm *g, unsigned int v, u8 out[3])
{
	out[0] = g->voice[v].flags;
	out[1] = g->voice[v].ch;
	out[2] = g->voice[v].note;
}
