// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * settings.def's rows, compiled for the suite.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "settab.h"

const struct et_setting et_settings[] = {
#define ES1869_SET(id, ID, kind, bank, reg, shift, width, min, max, def, tlv, \
		   flags, index, ctl, items, desc)                            \
	{ #id, kind, bank, reg, shift, width, min, max, def, tlv, flags, index, \
	  ctl, items, desc },
#include "../es1869/settings.def"
#undef ES1869_SET
};

const int et_nsettings = sizeof(et_settings) / sizeof(et_settings[0]);

uint32_t et_settings_hash(void)
{
	uint32_t h = 2166136261u;
	const char *p;
	int i;

	for (i = 0; i < et_nsettings; i++) {
		for (p = et_settings[i].id; *p; p++)
			h = (h ^ (uint8_t)*p) * 16777619u;
		h = (h ^ ',') * 16777619u;
	}
	return h;
}

int et_setting_count(const struct et_setting *s)
{
	return s->kind == S_STEREO || s->kind == S_MASTER ||
			       s->kind == S_MASTERSW || s->kind == S_SMAG ?
		       2 :
		       1;
}
