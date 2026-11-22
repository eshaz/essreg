// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The cal and fm groups. cal.rec2 finds whether the second card hears the
 * chip's left output through the cable, and how loud; the fm cases record
 * the FM digitally on PCM 1: its idle level, which output each side bit
 * reaches in the native and the OPL3 mode, and a note's pitch.
 *
 * Notes:
 *
 * The notes come from the suite itself, run as a command (--fm-note), so
 * that the hwdep is open only while the note sounds.
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

// cal.rec2: on snd-es18xx, whose Audio 2 is silent on a 16-bit channel,
// Audio 1 plays instead, 8-bit mono, which its start race can only delay
// by a sample, with 441 Hz on both sides
enum et_result c_cal_rec2(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	static const struct et_fmt f1 = { ET_U8, 1, 44100 };
	unsigned long frames = 66150, nrec = 2 * 48000 + 9600;
	struct et_stream s[2];
	double *l, a441, a661, noise, lvl;
	long on;

	(void)k;
	c->have_rec2 = 0;
	if (!c->hal->has(c->hal->ctx, ET_REC2))
		return ET_SKIP;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_mix_out(c, c->stock ? 14 : 0, c->stock ? 0 : 15);
	s[0] = et_rec(ET_REC2, &et_rec2_fmt, nrec, 0);
	s[1] = c->stock ? et_play(ET_A1P, &f1,
				  et_tone(&f1, frames, ET_HZ, ET_HZ, 8192),
				  frames, 0.3) :
			  et_play(ET_A2, &f,
				  et_tone(&f, frames, ET_HZ, ET_HZ_R, 8192),
				  frames, 0.3);
	if (et_run(c, s, 2)) {
		et_fail(c, "the streams failed");
		free(s[0].data);
		free(s[1].data);
		return ET_ERROR;
	}
	l = et_chan(&et_rec2_fmt, s[0].data, s[0].done, 0);
	noise = et_noise_floor(l, s[0].done);
	on = an_onset(l, s[0].done, 8 * noise + 30);
	if (on < 0 || on + 48000 > (long)s[0].done) {
		et_note(c, "the second card heard nothing from the output");
		free(l);
		free(s[0].data);
		free(s[1].data);
		return ET_SKIP;
	}
	a441 = an_tone(l + on + 9600, 28800, 48000, ET_HZ, NULL);
	a661 = an_tone(l + on + 9600, 28800, 48000, ET_HZ_R, NULL);
	lvl = an_dbfs(a441 / M_SQRT2);
	et_measure(c, "441 Hz, left", lvl, "dBFS", -60, -1);
	et_measure(c, "661.5 Hz, right", an_dbfs(a661 / M_SQRT2), "dBFS", 1, 0);
	et_measure(c, "noise", an_dbfs(noise), "dBFS", 1, 0);
	et_measure(c, "clock against the chip",
		   (an_pitch(l + on + 4800, 38400, 48000, ET_HZ) / ET_HZ - 1) *
			   1e6,
		   "ppm", 1, 0);
	if (lvl > -60 && a441 > 10 * a661) {
		c->have_rec2 = 1;
		c->rec2_gain = lvl - an_dbfs(8192 / M_SQRT2);
		snprintf(c->rec2_name, sizeof(c->rec2_name), "%s",
			 c->hal->rec2(c->hal->ctx));
		et_note(c, "the second card hears the left output, %+.1f dB",
			c->rec2_gain);
	} else {
		et_fail(c, "the second card doesn't hear the left side alone");
	}
	free(l);
	free(s[0].data);
	free(s[1].data);
	return ET_PASS;
}

// the suite's own path, for --fm-note
static const char *self(void)
{
	static char path[256];
	ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);

	path[n > 0 ? n : 0] = 0;
	return path;
}

// a note through the suite's helper while PCM 1 records 2 s; ch0, ch1 rms
// and the frequency over the steady part
static int fm_record(struct et_ctx *c, const char *note, double *lvl,
		     double *hz, short **rec, unsigned long *nrec)
{
	char cmd[400];
	struct et_stream s[2];
	unsigned long n = 2 * ET_FM_RATE;
	double *x;
	int ch;

	snprintf(cmd, sizeof(cmd), "%s --fm-note %s", self(), note);
	// PCM 1 takes the music DAC before the master
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	et_ctl_set(c, "Video Playback Switch", 0, 0, 0);
	s[0] = et_rec(ET_FMC, &et_fm_fmt, n, 0);
	memset(&s[1], 0, sizeof(s[1]));
	s[1].role = ET_CMD;
	s[1].cmd = cmd;
	s[1].start = 0.2;
	s[1].timeout = 5;
	if (et_run(c, s, 2) || s[1].status) {
		et_fail(c, "the note: %s, exit %d",
			s[1].err ? strerror(-s[1].err) : "ok", s[1].status);
		free(s[0].data);
		return -1;
	}
	for (ch = 0; ch < 2; ch++) {
		x = et_chan(&et_fm_fmt, s[0].data, s[0].done, ch);
		lvl[ch] = an_rms(x + ET_FM_RATE / 2, ET_FM_RATE / 2, 1);
		if (hz) {
			struct an_fit fit;

			hz[ch] = an_fit_sine(x + ET_FM_RATE / 2, ET_FM_RATE / 2,
					     ET_FM_RATE, 440.7, 1, &fit) ?
					 0 :
					 fit.f;
		}
		free(x);
	}
	if (rec) {
		*rec = s[0].data;
		*nrec = s[0].done;
	} else {
		free(s[0].data);
	}
	return 0;
}

// fm.idle: PCM 1 reads the FM's idle level, exactly
enum et_result c_fm_idle(struct et_ctx *c, const struct et_case *k)
{
	struct et_stream s = et_rec(ET_FMC, &et_fm_fmt, ET_FM_RATE / 2, 0);
	short *r;
	unsigned long i, other = 0;

	(void)k;
	et_ctl_set(c, "Video Playback Switch", 0, 0, 0);
	if (et_run(c, &s, 1)) {
		et_fail(c, "PCM 1: %s", strerror(-s.err));
		free(s.data);
		return ET_ERROR;
	}
	r = s.data;
	for (i = 0; i < 2 * s.done; i++)
		other += r[i] != ET_FM_IDLE;
	et_measure(c, "samples other than +128", other, "", 0, 0);
	free(s.data);
	return ET_PASS;
}

// fm.esfm.sides: register 6 bit 5 sounds on the left, bit 4 on the right
enum et_result c_fm_esfm_sides(struct et_ctx *c, const struct et_case *k)
{
	static const char *const notes[2] = { "esfm:0x20:581:4:1.2",
					      "esfm:0x10:581:4:1.2" };
	double lvl[2];
	int side;

	(void)k;
	for (side = 0; side < 2; side++) {
		if (fm_record(c, notes[side], lvl, NULL, NULL, NULL))
			return ET_ERROR;
		et_measure(c, side ? "bit 4, right" : "bit 5, left",
			   an_dbfs(lvl[side]), "dBFS", -40, 0);
		et_measure(c, side ? "bit 4, left" : "bit 5, right",
			   lvl[!side], "rms", 0, 0);
	}
	return ET_PASS;
}

// fm.opl3.sides: channel A on the left with fm_sides, mirrored without
enum et_result c_fm_opl3_sides(struct et_ctx *c, const struct et_case *k)
{
	static const char *const notes[2] = { "opl3:L:581:4:1.2",
					      "opl3:R:581:4:1.2" };
	long sides = 1;
	double lvl[2];
	int side, pass;

	(void)k;
	et_param_get(c, "fm_sides", &sides);
	for (pass = 0; pass < (geteuid() ? 1 : 2); pass++) {
		int mirror;

		if (pass && et_param_set(c, "fm_sides", !sides) < 0)
			break;
		mirror = pass ? sides : !sides;
		for (side = 0; side < 2; side++) {
			char name[64];
			int at = side ^ mirror;

			if (fm_record(c, notes[side], lvl, NULL, NULL, NULL))
				return ET_ERROR;
			snprintf(name, sizeof(name), "fm_sides=%d, %s on the %s",
				 !mirror, side ? "B" : "A",
				 at ? "right" : "left");
			et_measure(c, name, an_dbfs(lvl[at]), "dBFS", -40, 0);
			snprintf(name, sizeof(name), "fm_sides=%d, %s on the %s",
				 !mirror, side ? "B" : "A",
				 at ? "left" : "right");
			et_measure(c, name, lvl[!at], "rms", 0, 0);
		}
	}
	return ET_PASS;
}

// fm.pitch: a native note at F-number 581, block 4 plays 581 / 2^16 of the
// FM's rate, exactly, on PCM 1's own clock
enum et_result c_fm_pitch(struct et_ctx *c, const struct et_case *k)
{
	double lvl[2], hz[2], want = 581.0 * ET_FM_RATE / 65536;

	(void)k;
	if (fm_record(c, "esfm:0x30:581:4:1.2", lvl, hz, NULL, NULL))
		return ET_ERROR;
	et_measure(c, "frequency", hz[0], "Hz", want - 0.005, want + 0.005);
	et_measure(c, "sides", 20 * log10((lvl[0] + 1) / (lvl[1] + 1)), "dB",
		   -0.1, 0.1);
	return ET_PASS;
}
