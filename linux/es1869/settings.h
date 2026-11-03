/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The settings of settings.def: their ids, their descriptions and the
 * module-wide values that the parameters and the controls share.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ES1869_SETTINGS_H
#define ES1869_SETTINGS_H

#include <linux/compiler.h>
#include <linux/types.h>

#include "settings_kinds.h"

enum es1869_set_id {
#define ES1869_SET(id, ID, kind, bank, reg, shift, width, min, max, def, tlv, \
		   flags, index, ctl, items, desc)                            \
	ES_S_##id,
#include "settings.def"
#undef ES1869_SET
	ES_S_COUNT
};

struct es1869_setting {
	const char *name;
	u8 kind, bank, reg, shift, width, tlv, index;
	u16 flags;
	int min, max, def;
	const char *ctl, *items, *desc;
};

extern const struct es1869_setting es1869_settings[ES_S_COUNT];
extern int es1869_vals[ES_S_COUNT][2];

// a setting's module-wide value (channel 0, or the left)
static inline int es1869_val(int id)
{
	return READ_ONCE(es1869_vals[id][0]);
}

static inline int es1869_val2(int id, int ch)
{
	return READ_ONCE(es1869_vals[id][ch]);
}

// channels of a kind: 2 for the stereo ones
static inline int es1869_set_count(const struct es1869_setting *s)
{
	return s->kind == S_STEREO || s->kind == S_MASTER ||
	       s->kind == S_MASTERSW || s->kind == S_SMAG ? 2 : 1;
}

const char *es1869_bank_name(void);
u32 es1869_settings_hash(void);

#endif /* ES1869_SETTINGS_H */
