// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The ESFM General MIDI engine: MIDI in, native ESFM register writes out,
 * as the fixed ESFM.DRV plays it, with the logic docs/ESFM_ENGINE.md
 * describes: the free voice that has been off longest, stealing by channel
 * priority, the F-number tables and the bend's ratio tables, the four level
 * modes, ESS's controllers and the fixed driver's General MIDI.
 *
 * Notes:
 *
 * Register n of operator k of voice v is v * 20h + k * 8 + n; voice v's
 * key-on is 240h + v, and voices 16 and 17 key on in two halves, 250h-251h
 * and 252h-253h. A bank (docs/ESFM_BANK.md) gives each program, and each
 * drum note of channel 10, a patch of one or two voices.
 *
 * ESS's behaviour that the fixed driver keeps is kept here too (section 11
 * of the document): a bend past block 7 plays an octave low, volume 0
 * clears KSL, and controllers 126 and 127 sound off.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "esfm_gm.h"

#define KEYED 0x01
#define OFF 0x02
#define HELD 0x04
#define SECOND 0x08
#define NONE 0xFF
#define DRUMS 9
#define CENTER 0x2000

// the F-numbers of the semitones from the block's start, and the detune's
// scale for each (section 6)
static const u16 fnum_tab[12] = {
	0x202, 0x220, 0x241, 0x263, 0x287, 0x2AE,
	0x2D7, 0x302, 0x330, 0x360, 0x394, 0x3CA
};

static const u16 detune_tab[12] = {
	256, 242, 228, 215, 203, 192, 181, 171, 161, 152, 144, 136
};

// the bend's ratio: 2^(s/12) times 256 for s of -24 to 24 semitones, and
// 2^(k/768) times 1024 for k 64ths of a semitone
static const u16 semi_tab[49] = {
	0x100, 0x10f, 0x11f, 0x130, 0x143, 0x156, 0x16a, 0x180, 0x196,
	0x1af, 0x1c8, 0x1e3, 0x200, 0x21e, 0x23f, 0x261, 0x285, 0x2ab,
	0x2d4, 0x2ff, 0x32d, 0x35d, 0x390, 0x3c7, 0x400, 0x43d, 0x47d,
	0x4c2, 0x50a, 0x557, 0x5a8, 0x5fe, 0x659, 0x6ba, 0x721, 0x78d,
	0x800, 0x87a, 0x8fb, 0x983, 0xa14, 0xaae, 0xb50, 0xbfd, 0xcb3,
	0xd74, 0xe41, 0xf1a, 0x1000
};

static const u16 fine_tab[64] = {
	1024, 1025, 1026, 1027, 1028, 1029, 1030, 1030, 1031, 1032, 1033,
	1034, 1035, 1036, 1037, 1038, 1039, 1040, 1041, 1042, 1043, 1044,
	1045, 1045, 1046, 1047, 1048, 1049, 1050, 1051, 1052, 1053, 1054,
	1055, 1056, 1057, 1058, 1059, 1060, 1061, 1062, 1063, 1064, 1065,
	1065, 1066, 1067, 1068, 1069, 1070, 1071, 1072, 1073, 1074, 1075,
	1076, 1077, 1078, 1079, 1080, 1081, 1082, 1083, 1084
};

// the master volume's attenuation for each 512th of its 14 bits
static const u8 master_tab[32] = {
	63, 63, 57, 49, 44, 40, 36, 33, 30, 27, 25, 23, 21, 19, 18, 16,
	15, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 2, 1, 0
};

static void wr(struct esfm_gm *g, u16 reg, u8 val)
{
	g->io->write(g->io->ctx, reg, val);
}

int esfm_bank_check(const u8 *bank, u32 len)
{
	unsigned int i, off, mode;

	if (len < 512 || len > ESFM_BANK_MAX)
		return -EINVAL;
	for (i = 0; i < 256; i++) {
		off = bank[2 * i] | bank[2 * i + 1] << 8;
		if (!off)
			continue;
		if (off < 512 || off + 36 > len)
			return -EINVAL;
		mode = (bank[off] >> 1) & 3;
		if ((mode == 1 || mode == 2) && off + 72 > len)
			return -EINVAL;
	}
	return 0;
}

// --- the chip voices ------------------------------------------------------------

static void key(struct esfm_gm *g, unsigned int v, u8 on)
{
	if (v < 16) {
		wr(g, 0x240 + v, on);
	} else {
		wr(g, 0x250 + 2 * (v - 16), on);
		wr(g, 0x251 + 2 * (v - 16), on);
	}
}

static void voice_off(struct esfm_gm *g, unsigned int v)
{
	key(g, v, 0);
	g->voice[v].flags = OFF;
	g->voice[v].clock = g->clock++;
}

// the two free voices that have been off longest: a patch voice with header
// bit 0 set keeps off voices 16 and 17. A voice still playing the same note
// on the channel is keyed off first (find_voices)
static void find_voices(struct esfm_gm *g, u8 ch, u8 note, bool not16_2,
			bool not16_1, u8 *free1, u8 *free2)
{
	u32 best1 = 0, best2 = 0, age;
	unsigned int i;

	*free1 = *free2 = NONE;
	for (i = 0; i < 16; i++) {
		struct esfm_voice *v = &g->voice[i];

		if ((v->flags & KEYED) && v->ch == ch && v->note == note)
			voice_off(g, i);
		if (v->flags & KEYED)
			continue;
		age = g->clock - v->clock;
		if (age >= best1) {
			best2 = best1;
			*free2 = *free1;
			best1 = age;
			*free1 = i;
		} else if (age >= best2) {
			best2 = age;
			*free2 = i;
		}
	}
	for (i = 16; i < 18; i++) {
		struct esfm_voice *v = &g->voice[i];

		if ((v->flags & KEYED) && v->ch == ch && v->note == note)
			voice_off(g, i);
		if (v->flags & KEYED)
			continue;
		age = g->clock - v->clock;
		if (!not16_1 && age >= best1) {
			if (i == 16) {
				best2 = best1;
				*free2 = *free1;
				best1 = age;
			} else if (!(*free1 == 16 && not16_2)) {
				*free2 = *free1;
			}
			*free1 = i;
		} else if (!not16_2 && age >= best2) {
			if (i == 16)
				best2 = age;
			*free2 = i;
		}
	}
}

// no voice free: a second voice of a two-voice patch first, then the
// highest channel, drums lowest, then the oldest (steal_voice)
static u8 steal_voice(struct esfm_gm *g, bool not16)
{
	unsigned int i, count = not16 ? 16 : 18, prio, best_prio = 0;
	u32 age, best_age = 0;
	u8 sec = 0, chosen = 0;
	bool take;

	for (i = 0; i < count; i++) {
		struct esfm_voice *v = &g->voice[i];

		age = g->clock - v->clock;
		prio = v->ch == DRUMS ? 1 : v->ch + 2;
		take = false;
		if ((v->flags & SECOND) != sec) {
			if (!sec) {
				sec = SECOND;
				take = true;
			}
		} else if (prio > best_prio) {
			take = true;
		} else if (prio == best_prio && age > best_age) {
			take = true;
		}
		if (take) {
			best_prio = prio;
			best_age = age;
			chosen = i;
		}
	}
	voice_off(g, chosen);
	return chosen;
}

// --- pitch ------------------------------------------------------------------------

// fnum scaled by the bend: 1/256 semitones from the 14-bit value and the
// range, through the semitone and 64th tables (calc_pitch)
static u16 calc_pitch(unsigned int rng, unsigned int bend, u16 fnum)
{
	int s, idx;
	u32 ratio;

	if (bend == CENTER)
		return fnum;
	if (bend >= 0x3F80)
		bend = 0x4000;
	s = ((int)(bend - CENTER) * (int)rng) >> 5;
	s = (s16)(s + 0x1800);
	idx = clamp(s >> 8, 0, 48);
	ratio = ((u32)semi_tab[idx] * fine_tab[(s >> 2) & 0x3F]) >> 10;
	ratio &= 0xFFFF;
	return ((ratio * fnum + 0x200) >> 10) & 0xFFFF;
}

// the fixed driver's tuning: the range in cents, the bend's offset in 1/256
// semitones, RPN 1's fine tuning, within two octaves either way
static u16 pitch(struct esfm_gm *g, u8 ch, unsigned int bend, u16 fnum)
{
	struct esfm_channel *c = &g->ch[ch];
	int r, off, q;

	if (!g->opts.tuning)
		return calc_pitch(c->rng, bend, fnum);
	r = min(12700, c->rng * 100 + c->cents);
	if (bend >= 0x3F80)
		bend = 0x4000;
	off = ((int)bend - CENTER) * r / 3200;
	off += (s16)(c->fine - CENTER) >> 5;
	off = clamp(off, -24 * 256, 24 * 256 - 1);
	q = off * 4;
	if (q >= 0)
		q += 2;
	return calc_pitch(24, CENTER + q / 3, fnum);
}

// a block and F-number in register 5's and 4's layout: the F-number below
// 400h, the block at most 7, an octave low past it as ESS has it
static u16 block_fnum(int block, u32 f)
{
	while (f >= 0x400) {
		block++;
		f >>= 1;
	}
	if (block > 7)
		block = 7;
	return block << 10 | f;
}

// --- level --------------------------------------------------------------------

// the attenuation a level mode adds for x, velocity, volume or expression
static unsigned int curve(unsigned int mode, unsigned int x)
{
	switch (mode) {
	case 1:
		return (127 - x) >> 4;
	case 2:
		return (127 - x) >> 3;
	case 3:
		return x >= 64 ? (127 - x) >> 2 : ((63 - x) >> 1) + 16;
	}
	return 0;
}

// the operator's register 1: its level with the velocity, plus the volume,
// the expression and the master volume in its level mode (calc_level)
static u8 calc_level(struct esfm_gm *g, u8 ch, unsigned int mode, u8 level)
{
	struct esfm_channel *c = &g->ch[ch];
	unsigned int add = 0;

	if (!c->vol)
		return 0x3F;
	if (mode == 1 || mode == 2)
		add = ((127 - c->vol) >> (mode == 1 ? 4 : 3)) +
		      ((127 - c->expr) >> (mode == 1 ? 4 : 3));
	else if (mode == 3)
		add = curve(3, c->expr) + curve(3, c->vol);
	if (mode)
		add += g->master;
	return (level & 0xC0) | min(0x3Fu, (level & 0x3F) + add);
}

// --- programming ----------------------------------------------------------------

static void program_operator(struct esfm_gm *g, unsigned int v,
			     unsigned int op, const u8 *p, int note, u8 vel,
			     bool fixed_freq, unsigned int mode, u8 ch)
{
	struct esfm_channel *c = &g->ch[ch];
	u16 base = v * 0x20 + op * 8, fnum, r;
	int semi, block, off, delta;
	u8 r0, r4, r5, r6, level;

	if (g->opts.tuning && ch != DRUMS)
		note += c->coarse - 0x40;
	wr(g, base + 7, 0);
	if (!fixed_freq) {
		off = ((p[5] << 2) | (p[4] & 3)) & 0x7F;
		note += off & 0x40 ? off - 0x80 : off;
	}
	if (note < 19)
		note += (30 - note) / 12 * 12;
	if (note > 114)
		note -= (note - 103) / 12 * 12;
	semi = (note - 19) % 12;
	block = (note - 19) / 12;

	g->opregs[v * 4 + op][0] = p[0];
	r0 = p[0] | (c->vib & 1 ? 0x40 : 0);
	wr(g, base + 0, r0);
	level = (p[1] & 0xC0) | min(0x3Fu, (p[1] & 0x3Fu) + curve(mode, vel));
	g->voice[v].level[op] = level;
	wr(g, base + 1, calc_level(g, ch, mode, level));
	wr(g, base + 2, p[2]);
	wr(g, base + 3, p[3]);
	if (!fixed_freq) {
		s8 d = (s8)(p[4] & 0xFC);

		delta = 0;
		if (d) {
			delta = (s8)((detune_tab[semi] * (d >> 2)) >> 8);
			if (block > 1)
				delta >>= block - 1;
		}
		fnum = (fnum_tab[semi] + delta) & 0xFFFF;
		r5 = ((fnum >> 8) & 3) + (p[5] & 0xE0) + block * 4;
		g->voice[v].reg5[op] = r5;
		g->voice[v].base[op] = fnum;
		r = block_fnum(block, pitch(g, ch, c->bend, fnum));
		r4 = r & 0xFF;
		r5 = (r >> 8) + (r5 & 0xE0);
	} else {
		r4 = p[4];
		r5 = p[5];
	}
	wr(g, base + 4, r4);
	wr(g, base + 5, r5);
	r6 = p[6];
	if ((r6 & 0x30) && c->pan != 0x30)
		r6 = (r6 & 0xCF) | c->pan;
	g->opregs[v * 4 + op][1] = p[6];
	if (c->vib & 2)
		r6 |= 0x40;
	wr(g, base + 6, r6);
	wr(g, base + 7, p[7]);
}

static void program_voice(struct esfm_gm *g, unsigned int v, const u8 *pv,
			  u8 ch, u8 note, u8 vel)
{
	struct esfm_voice *voice = &g->voice[v];
	unsigned int op;

	for (op = 0; op < 4; op++)
		program_operator(g, v, op, pv + 4 + 8 * op, note, vel,
				 pv[0] & (0x10 << op), (pv[3] >> (2 * op)) & 3,
				 ch);
	voice->hdr0 = pv[0];
	voice->hdr3 = pv[3];
	voice->flags = KEYED;
	voice->clock = g->clock++;
	voice->ch = ch;
	voice->note = note;
}

static void note_on(struct esfm_gm *g, u8 ch, u8 note, u8 vel)
{
	unsigned int entry = ch == DRUMS ? 128 + note : g->ch[ch].prog;
	unsigned int off = g->bank[2 * entry] | g->bank[2 * entry + 1] << 8;
	const u8 *p;
	u8 f1, f2;

	if (!off)
		return;
	p = g->bank + off;
	switch ((p[0] >> 1) & 3) {
	case 0:
		find_voices(g, ch, note, false, p[0] & 1, &f1, &f2);
		if (f1 == NONE)
			f1 = steal_voice(g, p[0] & 1);
		program_voice(g, f1, p, ch, note, vel);
		key(g, f1, 1);
		break;
	case 1:
		// the second voice only if another one is free
		find_voices(g, ch, note, p[36] & 1, p[0] & 1, &f1, &f2);
		if (f1 == NONE)
			f1 = steal_voice(g, p[0] & 1);
		program_voice(g, f1, p, ch, note, vel);
		if (f2 != NONE) {
			program_voice(g, f2, p + 36, ch, note, vel);
			g->voice[f2].flags |= SECOND;
			key(g, f2, 1);
		}
		key(g, f1, 1);
		break;
	case 2:
		find_voices(g, ch, note, p[36] & 1, p[0] & 1, &f1, &f2);
		if (f1 == NONE)
			f1 = steal_voice(g, p[0] & 1);
		if (f2 == NONE)
			f2 = steal_voice(g, p[36] & 1);
		program_voice(g, f1, p, ch, note, vel);
		program_voice(g, f2, p + 36, ch, note, vel);
		key(g, f1, 1);
		key(g, f2, 1);
		break;
	}
}

static void note_off(struct esfm_gm *g, u8 ch, u8 note)
{
	unsigned int i;

	for (i = 0; i < ESFM_VOICES; i++) {
		struct esfm_voice *v = &g->voice[i];

		if (!(v->flags & KEYED) || v->ch != ch || v->note != note)
			continue;
		if (g->ch[ch].flags & 1)
			v->flags |= HELD;
		else
			voice_off(g, i);
	}
}

static void sustain(struct esfm_gm *g, u8 ch, u8 val)
{
	unsigned int i;

	if (val >= 64) {
		g->ch[ch].flags |= 1;
		return;
	}
	g->ch[ch].flags &= ~1;
	for (i = 0; i < ESFM_VOICES; i++)
		if ((g->voice[i].flags & HELD) && g->voice[i].ch == ch)
			voice_off(g, i);
}

// the channel's keyed voices at the new bend: registers 5 and 4 of each
// operator that follows the note
static void pitch_bend(struct esfm_gm *g, u8 ch, u16 bend)
{
	unsigned int i, op;
	u16 r;

	g->ch[ch].bend = bend;
	for (i = 0; i < ESFM_VOICES; i++) {
		struct esfm_voice *v = &g->voice[i];

		if (v->ch != ch || !(v->flags & KEYED))
			continue;
		for (op = 0; op < 4; op++) {
			if (v->hdr0 & (0x10 << op))
				continue;
			r = block_fnum((v->reg5[op] & 0x1C) >> 2,
				       pitch(g, ch, bend, v->base[op]));
			wr(g, i * 0x20 + op * 8 + 5, (v->reg5[op] & 0xE0) | (r >> 8));
			wr(g, i * 0x20 + op * 8 + 4, r & 0xFF);
		}
	}
}

// register 1 of every operator of the channel's keyed voices, or of every
// channel's for NONE
static void update_volume(struct esfm_gm *g, u8 ch)
{
	unsigned int i, op;

	for (i = 0; i < ESFM_VOICES; i++) {
		struct esfm_voice *v = &g->voice[i];

		if (!(v->flags & KEYED) || (v->ch != ch && ch != NONE))
			continue;
		for (op = 0; op < 4; op++)
			wr(g, i * 0x20 + op * 8 + 1,
			   calc_level(g, v->ch, (v->hdr3 >> (2 * op)) & 3,
				      v->level[op]));
	}
}

// registers 0 and 6 of the channel's keyed voices, with its vibrato and pan
static void refresh(struct esfm_gm *g, u8 ch)
{
	struct esfm_channel *c = &g->ch[ch];
	unsigned int i, op;
	u8 r0, r6;

	for (i = 0; i < ESFM_VOICES; i++) {
		if (!(g->voice[i].flags & KEYED) || g->voice[i].ch != ch)
			continue;
		for (op = 0; op < 4; op++) {
			r0 = g->opregs[i * 4 + op][0] | (c->vib & 1 ? 0x40 : 0);
			r6 = g->opregs[i * 4 + op][1];
			wr(g, i * 0x20 + op * 8, r0);
			if ((r6 & 0x30) && c->pan != 0x30)
				r6 = (r6 & 0xCF) | c->pan;
			if (c->vib & 2)
				r6 |= 0x40;
			wr(g, i * 0x20 + op * 8 + 6, r6);
		}
	}
}

// the modulation wheel or channel pressure, whichever is higher: vibrato
// from 1, deep vibrato too from 64
static void vibrato(struct esfm_gm *g, u8 ch)
{
	struct esfm_channel *c = &g->ch[ch];
	u8 m = max(c->mod, c->press), vib = !m ? 0 : m < 64 ? 1 : 3;

	if (vib != c->vib) {
		c->vib = vib;
		refresh(g, ch);
	}
}

// the output bits of a pan position: ESS's 10h for the left, which the
// chip plays on its right output, or with sides 20h, its left
static u8 pan_bits(const struct esfm_gm *g, u8 val)
{
	u8 left = g->opts.sides ? 0x20 : 0x10;

	return val > 0x50 ? 0x30 ^ left : val < 0x30 ? left : 0x30;
}

// --- messages ----------------------------------------------------------------------

// the fixed driver's controllers, where its switches have them; false for
// the rest, which ESS's code takes
static bool control_fixed(struct esfm_gm *g, u8 ch, u8 cc, u8 val)
{
	struct esfm_channel *c = &g->ch[ch];
	struct esfm_gm_opts *o = &g->opts;

	if (cc == 1 && o->vibrato) {
		c->mod = val;
		vibrato(g, ch);
	} else if ((cc == 8 || cc == 10) && o->live_pan) {
		if (c->pan != pan_bits(g, val)) {
			c->pan = pan_bits(g, val);
			refresh(g, ch);
		}
	} else if ((cc == 98 || cc == 99) && o->tuning) {
		c->rpn = 0x7F7F;
	} else if (cc == 100 && o->tuning) {
		c->rpn = (c->rpn & 0xFF00) | val;
	} else if (cc == 101 && o->tuning) {
		c->rpn = (c->rpn & 0xFF) | val << 8;
	} else if (cc == 6 && o->tuning) {
		if (c->rpn == 1) {
			c->fine = val << 7;
		} else if (c->rpn == 2) {
			c->coarse = val;
			return true;
		} else if (c->rpn == 0) {
			c->rng = val;
			c->cents = 0;
		} else {
			return true;
		}
		pitch_bend(g, ch, c->bend);
	} else if (cc == 38 && o->tuning) {
		if (c->rpn == 1)
			c->fine = (c->fine & 0x3F80) | val;
		else if (c->rpn == 0)
			c->cents = val;
		else
			return true;
		pitch_bend(g, ch, c->bend);
	} else if (cc == 121 && o->reset_controllers) {
		sustain(g, ch, 0);
		c->expr = 127;
		c->mod = 0;
		c->press = 0;
		c->flags &= 0xF9;
		c->rpn = 0x7F7F;
		update_volume(g, ch);
		vibrato(g, ch);
		pitch_bend(g, ch, CENTER);
	} else {
		return false;
	}
	return true;
}

static void control(struct esfm_gm *g, u8 ch, u8 cc, u8 val)
{
	struct esfm_channel *c = &g->ch[ch];
	unsigned int i;

	if (control_fixed(g, ch, cc, val))
		return;
	switch (cc) {
	case 6:
		// ESS's RPN 0: both halves of the number selected
		if ((c->flags & 6) == 6)
			c->rng = val;
		break;
	case 7:
		c->vol = val;
		update_volume(g, ch);
		break;
	case 8:
	case 10:
		c->pan = pan_bits(g, val);
		break;
	case 11:
		c->expr = val;
		update_volume(g, ch);
		break;
	case 64:
		sustain(g, ch, val);
		break;
	case 98:
		c->flags &= ~2;
		break;
	case 99:
		c->flags &= ~4;
		break;
	case 100:
		c->flags = val ? c->flags & ~2 : c->flags | 2;
		break;
	case 101:
		c->flags = val ? c->flags & ~4 : c->flags | 4;
		break;
	case 120:
	case 126:
	case 127:
		for (i = 0; i < ESFM_VOICES; i++)
			if ((g->voice[i].flags & KEYED) && g->voice[i].ch == ch)
				voice_off(g, i);
		break;
	case 121:
		if (c->flags & 1)
			for (i = 0; i < ESFM_VOICES; i++)
				if ((g->voice[i].flags & KEYED) &&
				    g->voice[i].ch == ch &&
				    (g->voice[i].flags & HELD))
					voice_off(g, i);
		c->vol = 100;
		c->expr = 127;
		c->flags &= ~1;
		c->bend = CENTER;
		c->pan = 0x30;
		c->rng = 2;
		break;
	case 123:
	case 124:
	case 125:
		for (i = 0; i < ESFM_VOICES; i++)
			if ((g->voice[i].flags & KEYED) && g->voice[i].ch == ch &&
			    !(g->voice[i].flags & HELD))
				voice_off(g, i);
		break;
	}
}

void esfm_gm_short(struct esfm_gm *g, u8 status, u8 d1, u8 d2)
{
	u8 ch = status & 0x0F;

	d1 &= 0x7F;
	d2 &= 0x7F;
	switch (status & 0xF0) {
	case 0x80:
		note_off(g, ch, d1);
		break;
	case 0x90:
		if (d2)
			note_on(g, ch, d1, d2);
		else
			note_off(g, ch, d1);
		break;
	case 0xB0:
		control(g, ch, d1, d2);
		break;
	case 0xC0:
		g->ch[ch].prog = d1;
		if (g->opts.pedal_release && (g->ch[ch].flags & 1))
			sustain(g, ch, 0);
		break;
	case 0xD0:
		if (g->opts.vibrato) {
			g->ch[ch].press = d1;
			vibrato(g, ch);
		}
		break;
	case 0xE0:
		pitch_bend(g, ch, d2 << 7 | d1);
		break;
	}
}

// controllers 121 and 123 on each channel, then GM's defaults, without a
// register written (fix_gm_reset)
void esfm_gm_gm_reset(struct esfm_gm *g)
{
	unsigned int n;

	for (n = 0; n < ESFM_CHANNELS; n++) {
		struct esfm_channel *c = &g->ch[n];

		control(g, n, 121, 0);
		control(g, n, 123, 0);
		c->prog = 0;
		c->vol = 100;
		c->pan = 0x30;
		c->rng = 2;
		c->cents = 0;
		c->coarse = 0x40;
		c->fine = CENTER;
	}
	g->master = 0;
}

// a byte of a SysEx pattern: any device number where the pattern has 00h
// at a device position
static bool sysex_is(const u8 *d, u32 len, const u8 *pat, u32 plen,
		     u32 dev_at, u8 dev_mask)
{
	u32 i;

	if (len != plen)
		return false;
	for (i = 0; i < len; i++)
		if (i == dev_at ? (d[i] & ~dev_mask) != pat[i] : d[i] != pat[i])
			return false;
	return true;
}

void esfm_gm_sysex(struct esfm_gm *g, const u8 *d, u32 len)
{
	static const u8 gm_on[] = { 0xF0, 0x7E, 0x00, 0x09, 0x01, 0xF7 };
	static const u8 gm2_on[] = { 0xF0, 0x7E, 0x00, 0x09, 0x03, 0xF7 };
	static const u8 gs[] = { 0xF0, 0x41, 0x00, 0x42, 0x12, 0x40,
				 0x00, 0x7F, 0x00, 0x41, 0xF7 };
	static const u8 xg[] = { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E,
				 0x00, 0xF7 };

	if (!g->opts.sysex)
		return;
	if (sysex_is(d, len, gm_on, sizeof(gm_on), 2, 0x7F) ||
	    sysex_is(d, len, gm2_on, sizeof(gm2_on), 2, 0x7F) ||
	    sysex_is(d, len, gs, sizeof(gs), 2, 0x7F) ||
	    sysex_is(d, len, xg, sizeof(xg), 2, 0x0F)) {
		esfm_gm_gm_reset(g);
		return;
	}
	// the master volume: F0 7F dd 04 01 ll mm F7
	if (len == 8 && d[0] == 0xF0 && d[1] == 0x7F && d[3] == 0x04 &&
	    d[4] == 0x01 && d[7] == 0xF7) {
		g->master = master_tab[((d[6] & 0x7F) << 7 | (d[5] & 0x7F)) >> 9];
		update_volume(g, NONE);
	}
}

// the close: every pedal up and every voice keyed off, or, without the
// silence switch, ESS's all notes off, which leaves the pedal's notes
void esfm_gm_close(struct esfm_gm *g)
{
	unsigned int i;

	if (g->opts.silence) {
		for (i = 0; i < ESFM_CHANNELS; i++)
			g->ch[i].flags &= ~1;
		for (i = 0; i < ESFM_VOICES; i++)
			voice_off(g, i);
		return;
	}
	for (i = 0; i < ESFM_VOICES; i++)
		note_off(g, g->voice[i].ch, g->voice[i].note);
}

// ESS's reset: every register from 000h to 253h written 00h, which keys
// the voices off and silences the operators, and the channels' defaults
// (chip_reset)
void esfm_gm_reset(struct esfm_gm *g)
{
	unsigned int i;
	u16 r;

	for (r = 0; r < 0x254; r++)
		wr(g, r, 0);
	for (i = 0; i < ESFM_CHANNELS; i++) {
		struct esfm_channel *c = &g->ch[i];
		u8 prog = c->prog;

		memset(c, 0, sizeof(*c));
		c->prog = prog;
		c->rng = 2;
		c->bend = CENTER;
		c->pan = 0x30;
		c->vol = 100;
		c->expr = 127;
		c->coarse = 0x40;
		c->rpn = 0x7F7F;
		c->fine = CENTER;
	}
	for (i = 0; i < ESFM_VOICES; i++) {
		g->voice[i].flags = 0;
		g->voice[i].clock = 0;
	}
	g->clock = 0;
	g->master = 0;
	memset(g->opregs, 0, sizeof(g->opregs));
}

void esfm_gm_init(struct esfm_gm *g, const struct esfm_gm_io *io,
		  const u8 *bank, u32 len, const struct esfm_gm_opts *opts)
{
	memset(g, 0, sizeof(*g));
	g->io = io;
	g->bank = bank;
	g->bank_len = len;
	g->opts = *opts;
	esfm_gm_reset(g);
}
