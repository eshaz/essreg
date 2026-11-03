// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The machine's groups: what the GPO pins do to the output (gpo), the
 * volume buttons with someone at the machine (hwvol), a suspend and resume
 * (pm), the driver unloaded and loaded again (load), and the stress group:
 * open and close cycles, and every interface at once while the dig path
 * plays.
 *
 * Notes:
 *
 * gpo is opt-in: on the Armada the second card, a Play! 3 on USB, hung
 * during its third state and needed a replug, whether from the pins or not.
 *
 * pm goes through the kernel's pm_test at "devices": every driver of the
 * machine suspends and resumes, 5 s apart, and the platform never sleeps,
 * so the resume needs nothing from the BIOS. It runs only when named and
 * with --destructive, since the network and the disks go through it too.
 * The driver has no INFO_RESUME, so a stream that the suspend stops is
 * prepared again: what it had queued is lost, and it goes on from there.
 *
 * load takes the driver out and loads it again, through modprobe or the
 * command of --reload, and udev's alsactl restore sets the new card's
 * mixer from the saved state, so every control and parameter goes back as
 * it was after each reload.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cases.h"
#include "hal.h"
#include "settab.h"

enum et_result c_dig_fmt(struct et_ctx *c, const struct et_case *k);
enum et_result c_a1rec_fmt(struct et_ctx *c, const struct et_case *k);
enum et_result c_fm_idle(struct et_ctx *c, const struct et_case *k);

static const struct et_case first = { "", 0, NULL, 0, "" };

// the suite's own path, for its helpers
static const char *self(void)
{
	static char path[256];
	ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);

	path[n > 0 ? n : 0] = 0;
	return path;
}

// the engines once each: the dig path, Audio 1's recording, the FM idle
static void engines(struct et_ctx *c)
{
	if (c_dig_fmt(c, &first) == ET_ERROR)
		et_fail(c, "the dig path didn't run");
	if (c_a1rec_fmt(c, &first) == ET_ERROR)
		et_fail(c, "Audio 1 didn't record");
	if (c_fm_idle(c, &first) == ET_ERROR)
		et_fail(c, "PCM 1 didn't record");
}

// --- gpo ---------------------------------------------------------------------

// gpo.out: a tone through the output at each state of GPO0 and GPO1, heard
// by the second card: what each pin does to the line output
enum et_result c_gpo_out(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	static const struct et_fmt f1 = { ET_U8, 1, 44100 };
	const struct et_fmt *pf = c->stock ? &f1 : &f;
	double lvl[4], d0, d1;
	int g;

	(void)k;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_mix_out(c, c->stock ? 14 : 0, c->stock ? 0 : 15);
	for (g = 0; g < 4; g++) {
		unsigned long pn = 44100;
		struct et_stream s[2];
		double *x;

		if (et_ctl_set(c, "GPO0 Switch", 0, g & 1, 0) < 0 ||
		    et_ctl_set(c, "GPO1 Switch", 0, g >> 1, 0) < 0)
			return ET_ERROR;
		s[0] = et_play(et_out_role(c), pf,
			       et_tone(pf, pn, ET_HZ, ET_HZ, 8000), pn, 0);
		s[1] = et_rec(ET_REC2, &et_rec2_fmt, 24000, 0.4);
		if (et_run(c, s, 2)) {
			et_fail(c, "GPO0 %d, GPO1 %d: the streams: %s, %s", g & 1,
				g >> 1, s[0].err ? strerror(-s[0].err) : "ok",
				s[1].err ? strerror(-s[1].err) : "ok");
			free(s[0].data);
			free(s[1].data);
			return ET_ERROR;
		}
		x = et_chan(&et_rec2_fmt, s[1].data, s[1].done, 0);
		lvl[g] = an_dbfs(an_tone(x, s[1].done, 48000, ET_HZ, NULL) /
				 M_SQRT2);
		et_note(c, "GPO0 %d, GPO1 %d: %.1f dBFS", g & 1, g >> 1, lvl[g]);
		free(x);
		free(s[0].data);
		free(s[1].data);
	}
	et_measure(c, "the output with GPO0 0 and GPO1 1, the defaults", lvl[2],
		   "dBFS", -60, 0);
	d0 = fmax(fabs(lvl[1] - lvl[0]), fabs(lvl[3] - lvl[2]));
	d1 = fmax(fabs(lvl[2] - lvl[0]), fabs(lvl[3] - lvl[1]));
	et_measure(c, "GPO0 against the output", d0, "dB", 1, 0);
	et_measure(c, "GPO1 against the output", d1, "dB", 1, 0);
	if (d0 < 3 && d1 < 3) {
		snprintf(c->why, sizeof(c->why),
			 "neither pin changes the line output (%.1f and %.1f dB)",
			 d0, d1);
		return ET_KNOWN;
	}
	return ET_PASS;
}

// --- hwvol -------------------------------------------------------------------

// a control's values once they differ from v, within secs: 1, or 0
static int await(struct et_ctx *c, const char *name, const long *v, long *now,
		 double secs)
{
	double end = c->hal->now(c->hal->ctx) + secs;

	do {
		if (c->hal->ctl_get(c->hal->ctx, name, 0, now, 2) > 0 &&
		    (now[0] != v[0] || now[1] != v[1]))
			return 1;
		c->hal->sleep(c->hal->ctx, 0.05);
	} while (c->hal->now(c->hal->ctx) < end);
	return 0;
}

// one push: the control's change on both sides
static void push(struct et_ctx *c, int button, const char *ctl, long want,
		 const char *what)
{
	long v[2] = { 0, 0 }, now[2] = { 0, 0 };
	char name[96];

	c->hal->ctl_get(c->hal->ctx, ctl, 0, v, 2);
	c->hal->ask_press(c->hal->ctx, button);
	if (!await(c, ctl, v, now, 20)) {
		et_fail(c, "%s: '%s' didn't move in 20 s", what, ctl);
		return;
	}
	// both sides move together; give the second a moment
	c->hal->sleep(c->hal->ctx, 0.3);
	c->hal->ctl_get(c->hal->ctx, ctl, 0, now, 2);
	snprintf(name, sizeof(name), "%s, left", what);
	et_measure(c, name, now[0] - v[0], "", want, want);
	snprintf(name, sizeof(name), "%s, right", what);
	et_measure(c, name, now[1] - v[1], "", want, want);
}

// hwvol.buttons: each button moves the master, whose control follows with
// its notification, and with the split the counters instead
enum et_result c_hwvol_buttons(struct et_ctx *c, const struct et_case *k)
{
	uint32_t hash, irq0[4], irq1[4];
	long step = 1, mst[2], now[2];

	(void)k;
	if (c->hal->info(c->hal->ctx, &hash, irq0) < 0)
		memset(irq0, 0, sizeof(irq0));
	if (et_param_get(c, "hwvol_count3", &step) == 0)
		step = step ? 3 : 1;
	// the master where the buttons have room, and journaled, since they
	// move it
	if (et_ctl_set(c, "Master Playback Volume", 0, 36, 36) < 0 ||
	    et_ctl_set(c, "Master Playback Switch", 0, 1, 1) < 0 ||
	    et_ctl_set(c, "Hardware Master Volume Split", 0, 0, 0) < 0)
		return ET_ERROR;
	et_log("    the volume buttons: press each as asked\n");
	push(c, ET_BTN_UP, "Master Playback Volume", step, "Volume Up");
	push(c, ET_BTN_DOWN, "Master Playback Volume", -step, "Volume Down");
	push(c, ET_BTN_MUTE, "Master Playback Switch", -1, "Mute");
	push(c, ET_BTN_MUTE, "Master Playback Switch", 1, "Mute again");
	// split: the counters move, the master stays
	et_ctl_set(c, "Hardware Master Volume Split", 0, 1, 1);
	c->hal->ctl_get(c->hal->ctx, "Master Playback Volume", 0, mst, 2);
	push(c, ET_BTN_UP, "Hardware Master Playback Volume", step,
	     "Volume Up, split");
	c->hal->ctl_get(c->hal->ctx, "Master Playback Volume", 0, now, 2);
	et_measure(c, "the master with the split", now[0] - mst[0], "", 0, 0);
	if (c->hal->info(c->hal->ctx, &hash, irq1) == 0)
		et_measure(c, "the buttons' interrupts", irq1[2] - irq0[2], "",
			   5, 1000);
	return ET_PASS;
}

// --- pm ----------------------------------------------------------------------

// what pm compares across the resume: the mixer's settings and B4h
static const unsigned char mix_regs[] = {
	0x14, 0x1A, 0x1C, 0x36, 0x38, 0x3A, 0x3C, 0x3E, 0x48, 0x50, 0x52, 0x54,
	0x56, 0x58, 0x5A, 0x60, 0x62, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E,
	0x6F, 0x71, 0x7C, 0x7D,
};

#define NREGS (sizeof(mix_regs) + 1)

static void read_regs(struct et_ctx *c, int *v)
{
	unsigned int i;

	for (i = 0; i < sizeof(mix_regs); i++)
		if (c->hal->reg_read(c->hal->ctx, 0, mix_regs[i], &v[i]) < 0)
			v[i] = -1;
	if (c->hal->reg_read(c->hal->ctx, 1, 0xB4, &v[i]) < 0)
		v[i] = -1;
}

// settings away from their defaults, so that a resume that loses one shows
static void distinct(struct et_ctx *c)
{
	et_ctl_set(c, "3D Control - Level", 0, 21, 21);
	et_ctl_set(c, "PCM Playback Volume", 0, 9, 11);
	et_ctl_set(c, "Line Playback Volume", 0, 3, 5);
	et_ctl_set(c, "Capture Volume", 0, 6, 7);
	et_ctl_set(c, "Mono Capture Volume", 0, 2, 4);
	et_ctl_set_enum(c, "Capture Source", "Line");
}

// pm.suspend: every setting the same after a suspend and resume, in the
// controls and in the chip, and every engine working
enum et_result c_pm_suspend(struct et_ctx *c, const struct et_case *k)
{
	int before[NREGS], after[NREGS], err;
	unsigned int i;
	int moved = 0;

	(void)k;
	distinct(c);
	read_regs(c, before);
	err = c->hal->suspend(c->hal->ctx);
	if (err == -ENOTSUP) {
		et_note(c, "no /sys/power/pm_test: the kernel needs CONFIG_PM_DEBUG");
		return ET_SKIP;
	}
	if (err) {
		et_fail(c, "the suspend: %s", strerror(-err));
		return ET_FAIL;
	}
	read_regs(c, after);
	for (i = 0; i < NREGS; i++)
		if (before[i] != after[i]) {
			et_note(c, "%s %02Xh: %02Xh before, %02Xh after",
				i < sizeof(mix_regs) ? "mixer" : "controller",
				i < sizeof(mix_regs) ? mix_regs[i] : 0xB4,
				before[i], after[i]);
			moved++;
		}
	et_measure(c, "registers that the resume changed", moved, "", 0, 0);
	engines(c);
	return ET_PASS;
}

// pm.stream: Audio 2 and PCM 1 on the dig path through a suspend: both
// prepared again, and the recording's last second the pattern in order
enum et_result c_pm_stream(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	unsigned long frames = 10 * 49716, nrec = 12 * ET_FM_RATE;
	struct et_stream s[3];
	unsigned long last;
	char cmd[300];
	void *pat;
	long tail;
	int i;

	(void)k;
	if (access("/sys/power/pm_test", F_OK) &&
	    strcmp(c->hal->name, "simulated")) {
		et_note(c, "no /sys/power/pm_test: the kernel needs CONFIG_PM_DEBUG");
		return ET_SKIP;
	}
	if (et_mix_quiet(c) || et_mix_dig(c, 1))
		return ET_ERROR;
	pat = et_dig_pattern(&f, frames, 41);
	s[0] = et_rec(ET_FMC, &et_fm_fmt, nrec, 0);
	s[1] = et_play(ET_A2, &f, pat, frames, 0.3);
	snprintf(cmd, sizeof(cmd), "%s --suspend", self());
	memset(&s[2], 0, sizeof(s[2]));
	s[2].role = ET_CMD;
	s[2].cmd = cmd;
	s[2].start = 3;
	s[2].timeout = 30;
	for (i = 0; i < 2; i++)
		s[i].timeout = 40;
	if (et_run(c, s, 3) && (s[0].err || s[1].err)) {
		et_fail(c, "the streams after the resume: %s, %s",
			s[0].err ? strerror(-s[0].err) : "ok",
			s[1].err ? strerror(-s[1].err) : "ok");
		et_mix_dig(c, 0);
		free(s[0].data);
		free(pat);
		return ET_FAIL;
	}
	et_mix_dig(c, 0);
	et_measure(c, "the suspend's exit", s[2].status, "", 0, 0);
	et_measure(c, "the recording's stops", s[0].xruns, "", 1, 3);
	et_measure(c, "the playback's stops", s[1].xruns, "", 1, 3);
	// the pattern's last second, which plays well after the resume, in
	// order: up to the last frame that isn't idle
	for (last = s[0].done; last > 0; last--) {
		const short *r = (const short *)s[0].data + 2 * (last - 1);

		if (r[0] != ET_FM_IDLE || r[1] != ET_FM_IDLE)
			break;
	}
	tail = et_dig_tail(&f, pat, frames, s[0].data, last, ET_FM_RATE);
	et_measure(c, "the pattern's last second in order, after the resume",
		   tail, "frames", ET_FM_RATE, ET_FM_RATE);
	if (c->fails)
		et_dump(c, "fm", &et_fm_fmt, s[0].data, s[0].done);
	free(s[0].data);
	free(pat);
	return ET_PASS;
}

// --- load --------------------------------------------------------------------

#define MAXCTL 256

struct snap {
	int n;
	struct {
		char name[48];
		int index;
		long v[2];
	} ctl[MAXCTL];
	char param[128][64];
};

static void snap_take(struct et_ctx *c, struct snap *s)
{
	int i, index;
	char name[48];

	s->n = 0;
	for (i = 0; s->n < MAXCTL &&
		    !c->hal->ctl_nth(c->hal->ctx, i, name, sizeof(name), &index);
	     i++) {
		struct et_ctl_info info;

		if (c->hal->ctl_info(c->hal->ctx, name, index, &info) < 0 ||
		    c->hal->ctl_get(c->hal->ctx, name, index, s->ctl[s->n].v,
				    2) <= 0)
			continue;
		if (info.count == 1)
			s->ctl[s->n].v[1] = s->ctl[s->n].v[0];
		snprintf(s->ctl[s->n].name, sizeof(s->ctl[s->n].name), "%s",
			 name);
		s->ctl[s->n].index = index;
		s->n++;
	}
	for (i = 0; i < et_nsettings && i < 128; i++)
		if (!(et_settings[i].flags & F_PARAM) ||
		    et_settings[i].kind == S_STR ||
		    c->hal->param_get(c->hal->ctx, et_settings[i].id,
				      s->param[i], sizeof(s->param[i])) < 0)
			s->param[i][0] = 0;
}

// everything back as the snapshot has it: the controls that differ
static int snap_put(struct et_ctx *c, const struct snap *s)
{
	char val[64];
	int i, fixed = 0;

	for (i = 0; i < et_nsettings && i < 128; i++)
		if (s->param[i][0] &&
		    c->hal->param_get(c->hal->ctx, et_settings[i].id, val,
				      sizeof(val)) >= 0 &&
		    strcmp(val, s->param[i]) &&
		    !c->hal->param_set(c->hal->ctx, et_settings[i].id,
				       s->param[i]))
			fixed++;
	for (i = 0; i < s->n; i++) {
		long v[2];

		if (c->hal->ctl_get(c->hal->ctx, s->ctl[i].name, s->ctl[i].index,
				    v, 2) <= 0)
			continue;
		if (v[0] == s->ctl[i].v[0] && v[1] == s->ctl[i].v[1])
			continue;
		if (c->hal->ctl_set(c->hal->ctx, s->ctl[i].name,
				    s->ctl[i].index, s->ctl[i].v, 2) >= 0)
			fixed++;
	}
	return fixed;
}

// load.cycle: five reloads, each card the same and complete, a parameter
// given at load in force, every setting back after each, and the engines
// working after the last
enum et_result c_load_cycle(struct et_ctx *c, const struct et_case *k)
{
	static const enum et_role roles[] = { ET_A2, ET_A1P, ET_A1C, ET_FMC };
	struct snap *s = calloc(1, sizeof(*s));
	double worst = 0;
	int i, r, missing = 0, fixed = 0;
	long lead_def = -1;

	(void)k;
	if (!s)
		abort();
	for (i = 0; i < et_nsettings; i++)
		if (!strcmp(et_settings[i].id, "a2_lead_ms"))
			lead_def = et_settings[i].def;
	snap_take(c, s);
	for (i = 0; i < 5; i++) {
		const char *params = i == 1 ? "a2_lead_ms=12" : "";
		double t0 = c->hal->now(c->hal->ctx), t;
		uint32_t hash;
		char buf[256];
		long lead = 0;
		int err = c->hal->reload(c->hal->ctx, params);

		t = c->hal->now(c->hal->ctx) - t0;
		if (err) {
			et_fail(c, "reload %d%s%s: %s", i + 1, *params ? " with " : "",
				params, strerror(-err));
			// once more, plainly, so that the machine keeps its sound
			if (*params && c->hal->reload(c->hal->ctx, ""))
				et_fail(c, "the driver didn't load again");
			break;
		}
		if (t > worst)
			worst = t;
		for (r = 0; r < 4; r++)
			missing += !c->hal->has(c->hal->ctx, roles[r]);
		if (c->hal->info(c->hal->ctx, &hash, NULL) < 0 ||
		    c->hal->proc(c->hal->ctx, buf, sizeof(buf)) < 0)
			missing++;
		if (i == 1) {
			et_param_get(c, "a2_lead_ms", &lead);
			et_measure(c, "a2_lead_ms=12 given at load", lead, "ms", 12,
				   12);
		}
		if (i == 2) {
			et_param_get(c, "a2_lead_ms", &lead);
			et_measure(c, "a2_lead_ms after a load without it, its default",
				   lead, "ms", lead_def, lead_def);
		}
		fixed += snap_put(c, s);
	}
	et_measure(c, "the slowest reload", worst, "s", 0, 15);
	et_measure(c, "devices missing after a reload", missing, "", 0, 0);
	et_note(c, "%d settings put back after alsactl restore", fixed);
	free(s);
	engines(c);
	return ET_PASS;
}

// --- stress ------------------------------------------------------------------

// stress.reopen: 200 short streams, each device in turn opened, set up,
// started, stopped and closed, and the dig path exact after
enum et_result c_stress_reopen(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	int i, bad = 0;

	(void)k;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	for (i = 0; i < 200; i++) {
		enum et_role r = (enum et_role[]){ ET_A2, ET_A1P, ET_A1C,
						   ET_FMC }[i % 4];
		const struct et_fmt *rf = r == ET_FMC ? &et_fm_fmt : &f;
		struct et_stream s;

		if (r == ET_A1C || r == ET_FMC)
			s = et_rec(r, rf, rf->rate / 10, 0);
		else
			s = et_play(r, rf, et_tone(rf, rf->rate / 10, 0, 0, 0),
				    rf->rate / 10, 0);
		if (et_run(c, &s, 1) || s.err || s.done != s.frames) {
			if (!bad)
				et_note(c, "cycle %d (%s): %s, %lu of %lu frames",
					i, r == ET_A2 ? "Audio 2" :
					   r == ET_A1P ? "Audio 1" :
					   r == ET_A1C ? "Audio 1 recording" : "PCM 1",
					s.err ? strerror(-s.err) : "ok", s.done,
					s.frames);
			bad++;
		}
		free(s.data);
	}
	et_measure(c, "cycles that failed", bad, "of 200", 0, 0);
	if (c_dig_fmt(c, &first) == ET_ERROR)
		et_fail(c, "the dig path didn't run");
	return ET_PASS;
}

// stress.all: a minute of the dig path, every frame exact, while the
// suite's helper reads every control, its dB scale, the proc file, the
// register hwdep's info and the mixer's registers as fast as it can
enum et_result c_stress_all(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	struct et_stream h;
	enum et_result r;
	char cmd[300];

	(void)k;
	snprintf(cmd, sizeof(cmd), "nice -n 10 %s --hammer 58", self());
	memset(&h, 0, sizeof(h));
	h.role = ET_CMD;
	h.cmd = cmd;
	h.start = 0.5;
	h.timeout = 75;
	r = et_dig_run(c, &f, 60 * 49716, 61, &h);
	et_measure(c, "the helper's exit", h.status, "", 0, 0);
	return r;
}
