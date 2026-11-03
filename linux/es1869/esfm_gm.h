/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The ESFM General MIDI engine: MIDI in, native ESFM register writes out,
 * as the fixed ESFM.DRV plays it (docs/ESFM_ENGINE.md). It has no kernel
 * API, so that snd-es1869-esfm and the host tests drive the same code.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ES1869_ESFM_GM_H
#define ES1869_ESFM_GM_H

#include "hw_compat.h"

#define ESFM_VOICES 18
#define ESFM_CHANNELS 16
#define ESFM_BANK_MAX 0x7FF0

struct esfm_gm_io {
	void *ctx;
	void (*write)(void *ctx, u16 reg, u8 val);
};

// the fixed driver's [ESFM.DRV] switches; each false gives ESS's behaviour
// there (docs/ESFM_ENGINE.md section 9)
struct esfm_gm_opts {
	bool vibrato; // the modulation wheel and channel pressure
	bool tuning; // RPN 0 with cents, RPN 1 and 2, bend ranges past 24
	bool reset_controllers; // controller 121 as RP-015 has it
	bool live_pan; // a pan change moves sounding notes
	bool sysex; // GM, GM2, GS and XG resets, the master volume
	bool pedal_release; // a program change lets the pedal up
	bool silence; // a close keys off what the pedal holds too
	bool sides; // pan left on the chip's left output, register 6 bit 5
};

struct esfm_voice {
	u8 flags; // 1 keyed, 2 keyed off, 4 held by the pedal, 8 a second voice
	u8 ch, note;
	u8 hdr0, hdr3; // the patch voice's header bytes 0 and 3
	u32 clock; // when it was keyed on or off
	u16 base[4]; // each operator's F-number before the bend
	u8 reg5[4]; // its delay, block and F-number bits 9:8 before the bend
	u8 level[4]; // its KSL and attenuation with the velocity
};

struct esfm_channel {
	u8 prog, flags; // flags: 1 the pedal, 2 and 4 ESS's RPN 0 halves
	u8 pan; // the output bits of register 6, 30h the center (pan_bits)
	u8 vol, expr, rng, cents, coarse;
	u8 mod, press, vib; // vib: 1 vibrato, 3 deep vibrato too
	u16 bend, fine, rpn;
};

struct esfm_gm {
	const struct esfm_gm_io *io;
	const u8 *bank;
	u32 bank_len;
	struct esfm_gm_opts opts;
	struct esfm_voice voice[ESFM_VOICES];
	struct esfm_channel ch[ESFM_CHANNELS];
	u32 clock;
	u8 master; // the master volume's attenuation
	u8 opregs[ESFM_VOICES * 4][2]; // each operator's registers 0 and 6
};

int esfm_bank_check(const u8 *bank, u32 len);
void esfm_gm_init(struct esfm_gm *g, const struct esfm_gm_io *io,
		  const u8 *bank, u32 len, const struct esfm_gm_opts *opts);
void esfm_gm_reset(struct esfm_gm *g);
void esfm_gm_short(struct esfm_gm *g, u8 status, u8 d1, u8 d2);
void esfm_gm_sysex(struct esfm_gm *g, const u8 *data, u32 len);
void esfm_gm_gm_reset(struct esfm_gm *g);
void esfm_gm_close(struct esfm_gm *g);

#endif
