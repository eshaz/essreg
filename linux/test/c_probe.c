// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The probe and params groups: the card's devices, its controls and the
 * module's parameters against settings.def, and the parameters written at
 * runtime, each reaching its control.
 *
 * Notes:
 *
 * params.rw writes only the settings that are mixer registers and harmless
 * to change: not the GPOs (the Armada's amplifier), the DRQ latch, the
 * power-downs or the PIIX4's timing, which can hang a recording.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../es1869/uapi/es1869.h"
#include "cases.h"
#include "hal.h"
#include "settab.h"

static const char *const never_write[] = {
	"gpo0",		     "gpo1",	      "drq_latch",    "pwr_save",
	"pwr_full",	     "piix4_type_f",  "self_power_down",
	"gpo_power_down",    "wake_no_mute",  "regs_write",   "irq_trace",
	"rate_limit",	     NULL,
};

static int writable(const struct et_setting *s)
{
	int i;

	for (i = 0; never_write[i]; i++)
		if (!strcmp(s->id, never_write[i]))
			return 0;
	return (s->flags & F_CTL) && (s->flags & F_PARAM) &&
	       !(s->flags & F_HOOK) && s->kind != S_STR;
}

// probe.card: the devices, the proc file, the register hwdep, and the
// driver built from the same settings.def as the suite
enum et_result c_probe_card(struct et_ctx *c, const struct et_case *k)
{
	static const enum et_role roles[] = { ET_A2, ET_A1P, ET_A1C, ET_FMC };
	static char proc[16384];
	uint32_t hash;
	long v[2];
	int i, n = 0, r7c;

	(void)k;
	for (i = 0; i < 4; i++)
		n += c->hal->has(c->hal->ctx, roles[i]) != 0;
	et_measure(c, "PCM devices", n, "of 4", 4, 4);
	if (c->hal->proc(c->hal->ctx, proc, sizeof(proc)) < 0 ||
	    !strstr(proc, "interrupts:"))
		et_fail(c, "no /proc/asound/ES1869/es1869");
	if (c->hal->info(c->hal->ctx, &hash, NULL) < 0) {
		et_fail(c, "the register hwdep doesn't answer");
	} else if (hash != et_settings_hash()) {
		et_fail(c, "the driver's settings.def differs from the suite's");
	}
	if (c->hal->reg_read(c->hal->ctx, ES1869_BANK_MIXER, 0x7C, &r7c) >= 0 &&
	    et_ctl_get(c, "PCM Playback Volume", 0, v, 2) == 2)
		et_measure(c, "7Ch against its control",
			   r7c - (v[0] << 4 | v[1]), "", 0, 0);
	return ET_PASS;
}

static int parse_param(const char *buf, long v[2])
{
	char *end;

	v[0] = strtol(buf, &end, 0);
	if (end == buf)
		return -EINVAL;
	v[1] = *end == ',' ? strtol(end + 1, NULL, 0) : v[0];
	return 0;
}

// probe.alsamixer: what someone looks for in alsamixer is there, in its
// Playback view: a control can exist and not make it to the simple mixer
enum et_result c_probe_alsamixer(struct et_ctx *c, const struct et_case *k)
{
	static const char *const names[] = {
		"Audio 2 Rate Correction", "Audio 2 Rate Trim",
		"Audio 2 Rate Dither", "Audio 2 Filter", "Audio 2 Oversampling",
		"Audio 2 Own Clock", "Audio 2 To FM",
	};
	unsigned int i;
	int seen = 0;

	(void)k;
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		int playback = 0;

		if (c->hal->selem(c->hal->ctx, names[i], &playback) < 0)
			et_fail(c, "no element '%s'", names[i]);
		else if (!playback)
			et_fail(c, "'%s' isn't in the Playback view", names[i]);
		else
			seen++;
	}
	et_measure(c, "elements in the Playback view", seen, "",
		   sizeof(names) / sizeof(names[0]),
		   sizeof(names) / sizeof(names[0]));
	return ET_PASS;
}

// probe.controls: every control of settings.def, its type and range, and
// its parameter holding the same value
enum et_result c_probe_controls(struct et_ctx *c, const struct et_case *k)
{
	int i, bad = 0, n = 0;

	(void)k;
	for (i = 0; i < et_nsettings; i++) {
		const struct et_setting *s = &et_settings[i];
		struct et_ctl_info info;
		int want = s->kind == S_BOOL || s->kind == S_MASTERSW ?
				   ET_CTL_BOOL :
			   s->kind == S_ENUM ? ET_CTL_ENUM :
					       ET_CTL_INT;
		char buf[64];
		long cv[2], pv[2];

		if (!(s->flags & F_CTL))
			continue;
		n++;
		if (c->hal->ctl_info(c->hal->ctx, s->ctl, s->index, &info) < 0) {
			et_fail(c, "no control '%s' %d", s->ctl, s->index);
			bad++;
			continue;
		}
		if (info.type != want || info.count != et_setting_count(s) ||
		    (want == ET_CTL_INT &&
		     (info.min != s->min || info.max != s->max))) {
			et_fail(c, "'%s': type %d, %d values, %ld to %ld", s->ctl,
				info.type, info.count, info.min, info.max);
			bad++;
		}
		if (want == ET_CTL_ENUM) {
			int items = 1;
			const char *p;

			for (p = s->items; *p; p++)
				items += *p == '|';
			if (info.items != items) {
				et_fail(c, "'%s': %d items, not %d", s->ctl,
					info.items, items);
				bad++;
			}
		}
		if (!(s->flags & F_PARAM) ||
		    c->hal->param_get(c->hal->ctx, s->id, buf, sizeof(buf)) < 0 ||
		    parse_param(buf, pv) < 0 ||
		    et_ctl_get(c, s->ctl, s->index, cv, 2) < 1)
			continue;
		if (info.count == 1)
			cv[1] = cv[0];
		if (cv[0] != pv[0] || cv[1] != pv[1]) {
			et_fail(c, "'%s' holds %ld,%ld, %s holds %s", s->ctl,
				cv[0], cv[1], s->id, buf);
			bad++;
		}
	}
	et_measure(c, "controls as settings.def has them", n - bad, "", n, n);
	return ET_PASS;
}

// probe.params: every parameter readable, in its range
enum et_result c_probe_params(struct et_ctx *c, const struct et_case *k)
{
	int i, n = 0, ok = 0;

	(void)k;
	for (i = 0; i < et_nsettings; i++) {
		const struct et_setting *s = &et_settings[i];
		char buf[64];
		long v[2];

		if (!(s->flags & F_PARAM))
			continue;
		n++;
		if (c->hal->param_get(c->hal->ctx, s->id, buf, sizeof(buf)) < 0) {
			et_fail(c, "no parameter %s", s->id);
			continue;
		}
		if (s->kind == S_STR) {
			ok++;
			continue;
		}
		if (parse_param(buf, v) < 0 || v[0] < s->min || v[0] > s->max ||
		    v[1] < s->min || v[1] > s->max) {
			et_fail(c, "%s is %s, outside %d to %d", s->id, buf,
				s->min, s->max);
			continue;
		}
		ok++;
	}
	et_measure(c, "parameters", ok, "", n, n);
	return ET_PASS;
}

// params.rw: a parameter written at runtime reaches its control, and the
// control written reaches the parameter
enum et_result c_params_rw(struct et_ctx *c, const struct et_case *k)
{
	int i, n = 0, ok = 0;

	(void)k;
	for (i = 0; i < et_nsettings; i++) {
		const struct et_setting *s = &et_settings[i];
		char buf[64];
		long pv[2], cv[2], nv;
		int fine = 1;

		if (!writable(s) ||
		    c->hal->param_get(c->hal->ctx, s->id, buf, sizeof(buf)) < 0 ||
		    parse_param(buf, pv) < 0)
			continue;
		n++;
		nv = pv[0] != s->max ? s->max : s->min;
		if (et_param_set(c, s->id, nv) < 0) {
			et_fail(c, "%s=%ld refused", s->id, nv);
			continue;
		}
		if (et_ctl_get(c, s->ctl, s->index, cv, 2) < 1 || cv[0] != nv) {
			et_fail(c, "%s=%ld left '%s' at %ld", s->id, nv, s->ctl,
				cv[0]);
			fine = 0;
		}
		et_ctl_set(c, s->ctl, s->index, pv[0], pv[1]);
		if (c->hal->param_get(c->hal->ctx, s->id, buf, sizeof(buf)) < 0 ||
		    parse_param(buf, cv) < 0 || cv[0] != pv[0] || cv[1] != pv[1]) {
			et_fail(c, "'%s' set back left %s at %s", s->ctl, s->id,
				buf);
			fine = 0;
		}
		ok += fine;
	}
	et_measure(c, "parameters that reach their controls", ok, "", n, n);
	return ET_PASS;
}

// params.bad: out of range is ERANGE, nonsense EINVAL, and nothing changes
enum et_result c_params_bad(struct et_ctx *c, const struct et_case *k)
{
	char before[64], after[64], val[32];
	int i, n = 0, ok = 0;

	(void)k;
	for (i = 0; i < et_nsettings; i++) {
		const struct et_setting *s = &et_settings[i];
		int e1, e2;

		if (!writable(s) || s->kind == S_BOOL || s->kind == S_MASTERSW ||
		    c->hal->param_get(c->hal->ctx, s->id, before,
				      sizeof(before)) < 0)
			continue;
		n++;
		snprintf(val, sizeof(val), "%d", s->max + 1);
		e1 = c->hal->param_set(c->hal->ctx, s->id, val);
		e2 = c->hal->param_set(c->hal->ctx, s->id, "1x2");
		c->hal->param_get(c->hal->ctx, s->id, after, sizeof(after));
		if (e1 != -ERANGE || e2 != -EINVAL || strcmp(before, after)) {
			et_fail(c, "%s: %d gives %d, 1x2 gives %d, %s became %s",
				s->id, s->max + 1, e1, e2, before, after);
			continue;
		}
		ok++;
	}
	et_measure(c, "parameters that refuse bad values", ok, "", n, n);
	return ET_PASS;
}
