/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The driver's settings as the suite sees them: settings.def's rows,
 * compiled here with the same columns, and the hash the driver reports.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ET_SETTAB_H
#define ET_SETTAB_H

#include <stdint.h>

#include "../es1869/settings_kinds.h"

struct et_setting {
	const char *id;
	int kind, bank, reg, shift, width, min, max, def, tlv, flags, index;
	const char *ctl, *items, *desc;
};

extern const struct et_setting et_settings[];
extern const int et_nsettings;

// the FNV-1a hash of the names, as es1869_settings_hash() makes it
uint32_t et_settings_hash(void);
// the values a control of the setting holds
int et_setting_count(const struct et_setting *s);

#endif
