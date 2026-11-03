// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The mix group: each volume's steps against its dB scale. A tone goes
 * through the control at every value, and the level that comes back, from
 * the highest value down, must follow what the control's TLV says, within
 * 0.5 dB while it stands 20 dB over the noise, fall with every step, and
 * reach the noise where the scale mutes.
 *
 * Notes:
 *
 * Audio 2's volume (7Ch) is heard through record source 7, the output
 * before the master's volume, with the master switched on at its lowest so
 * that the speakers stay quiet. 69h, B4h and 6Bh are heard through the
 * record mixer. The master is heard through record source 3, AOUT, with
 * the record monitor off: the monitor puts the record source on AOUT in
 * place of the playback mix, here AOUT itself. Audio 1's volume, which
 * Audio 1 can't record, is heard through the cable only when cal.linear
 * finds the second card's input linear: a microphone input with its own
 * gain control, as the Armada's Play! 3 has, hears 6 dB steps as 7 to 10
 * dB.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cases.h"
#include "hal.h"

struct path {
	const char *ctl;
	int index;
	int src; // 5 the record mixer, 7 the output, 3 AOUT, 0 the cable
	enum et_role play; // the DAC that plays the tone
	double amp; // the tone's peak
	int hi, lo, step;
};

// the suite's own path, for --fm-note
static const char *self_exe(void)
{
	static char path[256];
	ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);

	path[n > 0 ? n : 0] = 0;
	return path;
}

static const struct path paths[] = {
	{ "PCM Playback Volume", 0, 7, ET_A2, 3000, 15, 0, 1 },
	{ "PCM Capture Volume", 0, 5, ET_A2, 3000, 15, 0, 1 },
	{ "Capture Volume", 0, 5, ET_A2, 1000, 15, 0, 1 },
	{ "Master Playback Volume", 0, 3, ET_A2, 3000, 63, 15, 2 },
	{ "PCM Playback Volume", 1, 0, ET_A1P, 3000, 14, 0, 1 },
	{ "FM Capture Volume", 0, 5, ET_CMD, 0, 15, 0, 1 },
};

// the tone's level through the path at the control's present value, dBFS,
// on the left, and on the right in r
static double hear2(struct et_ctx *c, const struct path *p, double *noise,
		    double *r)
{
	static const struct et_fmt pf = { ET_S16, 2, 44100 };
	static const struct et_fmt rf = { ET_S16, 2, 44100 };
	unsigned long pn = 44100 * 9 / 10;
	struct et_stream s[2];
	const struct et_fmt *cf = p->src ? &rf : &et_rec2_fmt;
	unsigned long cn = cf->rate / 2;
	double *x, lvl;

	if (p->play == ET_CMD) {
		// a native FM note on both sides: 581 x 49716 / 2^16 Hz on
		// the FM's clock, near 441 Hz
		static char cmd[400];

		snprintf(cmd, sizeof(cmd), "%s --fm-note esfm:0x30:581:4:1.2",
			 self_exe());
		memset(&s[0], 0, sizeof(s[0]));
		s[0].role = ET_CMD;
		s[0].cmd = cmd;
		s[0].timeout = 5;
	} else {
		s[0] = et_play(p->play, &pf,
			       et_tone(&pf, pn, ET_HZ, ET_HZ, p->amp), pn, 0);
	}
	s[1] = et_rec(p->src ? ET_A1C : ET_REC2, cf, cn, 0.3);
	if (et_run(c, s, 2)) {
		if (p->play != ET_CMD)
			free(s[0].data);
		free(s[1].data);
		return -300;
	}
	x = et_chan(cf, s[1].data, s[1].done, 0);
	lvl = an_dbfs(an_tone(x, s[1].done, cf->rate, ET_HZ, NULL) / M_SQRT2);
	if (p->play == ET_CMD) {
		// the FM's own clock puts its note a little off 441 Hz
		struct an_fit fit;

		if (!an_fit_sine(x, s[1].done, cf->rate, ET_HZ, 1, &fit))
			lvl = an_dbfs(fit.amp / M_SQRT2);
	}
	// the noise near the tone, from the band above it
	*noise = an_dbfs(an_tone(x, s[1].done, cf->rate, 3 * ET_HZ + 37, NULL) /
			 M_SQRT2);
	free(x);
	if (r && p->play == ET_CMD) {
		struct an_fit fit;

		x = et_chan(cf, s[1].data, s[1].done, 1);
		*r = an_fit_sine(x, s[1].done, cf->rate, ET_HZ, 1, &fit) ?
			     -200 :
			     an_dbfs(fit.amp / M_SQRT2);
		free(x);
	} else if (r) {
		x = et_chan(cf, s[1].data, s[1].done, 1);
		*r = an_dbfs(an_tone(x, s[1].done, cf->rate, ET_HZ, NULL) /
			     M_SQRT2);
		free(x);
	}
	if (p->play != ET_CMD)
		free(s[0].data);
	free(s[1].data);
	return lvl;
}

static double hear(struct et_ctx *c, const struct path *p, double *noise)
{
	return hear2(c, p, noise, NULL);
}

// a stereo control's sides: the left high and the right low come out as
// the scale says, each on its own side
static void sides(struct et_ctx *c, const struct path *p)
{
	int hi = p->hi - 1, lo = p->hi - 7;
	long ch, cl;
	double l, r, n;

	if (et_ctl_set(c, p->ctl, p->index, hi, lo) < 0 ||
	    c->hal->ctl_db(c->hal->ctx, p->ctl, p->index, hi, &ch) < 0 ||
	    c->hal->ctl_db(c->hal->ctx, p->ctl, p->index, lo, &cl) < 0)
		return;
	l = hear2(c, p, &n, &r);
	et_measure(c, "left high, right low: their difference", l - r, "dB",
		   (ch - cl) / 100.0 - 0.5, (ch - cl) / 100.0 + 0.5);
}

// cal.linear: the cable and the second card against a tone whose digital
// level falls 6 dB a step at fixed volumes: whether levels through the
// cable can be measured (KNOWN when not)
enum et_result c_mix_cable(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt pf = { ET_S16, 2, 44100 };
	double top = 0, worst = 0;
	int i;

	(void)k;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_mix_out(c, 0, 15);
	for (i = 0; i < 6; i++) {
		unsigned long pn = 44100 * 9 / 10;
		struct et_stream s[2];
		double *x, lvl, err;

		s[0] = et_play(ET_A2, &pf,
			       et_tone(&pf, pn, ET_HZ, ET_HZ, 8000 / pow(2, i)), pn,
			       0);
		s[1] = et_rec(ET_REC2, &et_rec2_fmt, 24000, 0.3);
		if (et_run(c, s, 2)) {
			free(s[0].data);
			free(s[1].data);
			return ET_ERROR;
		}
		x = et_chan(&et_rec2_fmt, s[1].data, s[1].done, 0);
		lvl = an_dbfs(an_tone(x, s[1].done, 48000, ET_HZ, NULL) / M_SQRT2);
		if (!i)
			top = lvl;
		err = lvl - top + 6.0206 * i;
		et_note(c, "-%d dB: %.2f dBFS, %+.2f dB off", 6 * i, lvl, err);
		if (fabs(err) > fabs(worst))
			worst = err;
		free(x);
		free(s[0].data);
		free(s[1].data);
	}
	et_measure(c, "worst 6 dB step through the cable", worst, "dB", 1, 0);
	c->rec2_linear = fabs(worst) <= 0.5;
	// 3 s at the top step, so that a gain control in the second card,
	// which the quiet steps raised, is back for the cases after
	{
		unsigned long pn = 3 * 44100;
		struct et_stream s = et_play(ET_A2, &pf,
					     et_tone(&pf, pn, ET_HZ, ET_HZ, 8000),
					     pn, 0);

		et_run(c, &s, 1);
		free(s.data);
	}
	if (!c->rec2_linear) {
		snprintf(c->why, sizeof(c->why),
			 "the second card hears 6 dB steps %.1f dB off: no levels through the cable",
			 worst);
		return ET_KNOWN;
	}
	return ET_PASS;
}

enum et_result c_mix(struct et_ctx *c, const struct et_case *k)
{
	const struct path *p = &paths[k->arg];
	double top = 0, last = 0, worst = 0, noise = -200;
	long cdb_top = 0;
	int v, bad_order = 0, mute_ok = 1, steps = 0;

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	if (p->src) {
		if (et_mix_rec_a2(c, p->play == ET_CMD ? 0 : 13) < 0)
			return ET_ERROR;
		if (p->play == ET_CMD)
			et_ctl_set(c, "Video Playback Switch", 0, 0, 0);
		if (p->src == 7) {
			et_ctl_set_enum(c, "Capture Source", "Master");
			et_ctl_set(c, "Master Playback Switch", 0, 1, 1);
			et_ctl_set(c, "Master Playback Volume", 0, 0, 0);
		} else if (p->src == 3) {
			// AOUT, the record monitor off
			et_ctl_set_enum(c, "Capture Source", "AOUT");
			et_ctl_set(c, "Record Monitor", 0, 0, 0);
			et_ctl_set(c, "Master Playback Switch", 0, 1, 1);
			et_ctl_set(c, "PCM Playback Volume", 0, 15, 15);
		} else {
			et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
		}
	} else {
		et_mix_out(c, p->play == ET_A1P ? 14 : 0,
			   p->play == ET_A2 ? 15 : 0);
	}
	for (v = p->hi; v >= p->lo; v -= p->step) {
		double lvl, want, n;
		long cdb;

		if (et_ctl_set(c, p->ctl, p->index, v, v) < 0)
			return ET_ERROR;
		if (c->hal->ctl_db(c->hal->ctx, p->ctl, p->index, v, &cdb) < 0) {
			et_fail(c, "'%s' has no dB scale", p->ctl);
			return ET_FAIL;
		}
		lvl = hear(c, p, &n);
		if (lvl < -250)
			return ET_ERROR;
		if (v == p->hi) {
			top = last = lvl;
			cdb_top = cdb;
			noise = n;
			et_note(c, "%s at %d: %.2f dBFS, noise %.1f dBFS", p->ctl,
				v, lvl, n);
			continue;
		}
		if (cdb == ET_DB_MUTE) {
			// muted: down at the noise, or 50 dB under the top
			if (lvl > noise + 10 && lvl > top - 50) {
				et_fail(c, "'%s' at %d mutes, but %.1f dBFS came",
					p->ctl, v, lvl);
				mute_ok = 0;
			}
			continue;
		}
		want = top + (cdb - cdb_top) / 100.0;
		if (want > noise + 20) {
			double err = lvl - want;

			steps++;
			if (fabs(err) > fabs(worst))
				worst = err;
			if (fabs(err) > 0.5)
				et_note(c, "at %d: %.2f dBFS, the scale says %.2f",
					v, lvl, want);
		}
		if (lvl > last + 0.2)
			bad_order++;
		last = lvl;
	}
	if (p->src)
		sides(c, p);
	et_measure(c, "steps measured", steps, "", 3, 100);
	et_measure(c, "worst step against the scale", worst, "dB", -0.6, 0.6);
	et_measure(c, "steps that rose", bad_order, "", 0, 0);
	et_measure(c, "mutes that muted", mute_ok, "", 1, 1);
	return ET_PASS;
}

// dac.images: Audio 2 at 22.05 kHz plays 7 kHz; its image at 15.05 kHz,
// heard through the record mixer at 48 kHz, with the DAC's stages off (the
// default: a DAC without oversampling, whose images are KNOWN), with 4x
// oversampling (71h bit 4) and with its filter (71h bit 3)
enum et_result c_dac_images(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt pf = { ET_S16, 2, 22050 };
	static const struct et_fmt rf = { ET_S16, 2, 48000 };
	static const char *const names[3] = { "stages off", "oversampling",
					      "filter" };
	double img[3] = { 0, 0, 0 };
	int m;

	(void)k;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (et_mix_rec_a2(c, 13) < 0)
		return ET_ERROR;
	for (m = 0; m < 3; m++) {
		unsigned long pn = 2 * 22050;
		struct et_stream s[2];
		char name[64];
		double *x, tone;

		et_ctl_set(c, "Audio 2 Oversampling Playback Switch", 0, m == 1,
			   m == 1);
		et_ctl_set(c, "Audio 2 Filter Playback Switch", 0, m == 2,
			   m == 2);
		s[0] = et_play(ET_A2, &pf, et_tone(&pf, pn, 7000, 7000, 8000),
			       pn, 0);
		s[1] = et_rec(ET_A1C, &rf, 48000, 0.5);
		if (et_run(c, s, 2)) {
			free(s[0].data);
			free(s[1].data);
			return ET_ERROR;
		}
		x = et_chan(&rf, s[1].data, s[1].done, 0);
		tone = an_dbfs(an_tone(x, s[1].done, 48000, 7000, NULL) / M_SQRT2);
		img[m] = an_dbfs(an_tone(x, s[1].done, 48000, 15050, NULL) /
				 M_SQRT2) -
			 tone;
		snprintf(name, sizeof(name), "image against the tone, %s",
			 names[m]);
		et_measure(c, name, img[m], "dB", 1, 0);
		free(x);
		free(s[0].data);
		free(s[1].data);
	}
	et_measure(c, "oversampling takes the image down", img[0] - img[1],
		   "dB", 10, 200);
	et_measure(c, "the filter takes the image down", img[0] - img[2], "dB",
		   3, 200);
	if (img[0] > -40 && !c->fails) {
		snprintf(c->why, sizeof(c->why),
			 "with the stages off the image is %.0f dB, as without oversampling",
			 img[0]);
		return ET_KNOWN;
	}
	return ET_PASS;
}

// dac.corner.*: the corner of Audio 2's switched-capacitor filter, whose
// clock the driver sets from the rate (72h = 256 - 199582 / rate below 44.1
// kHz, a clock of 35.9 times the rate, and the data sheet's corner a 82nd
// of it, 0.44 of the rate): six tones from 0.15 to 0.46 of the rate,
// recorded on Audio 1's clock of the same family through the record mixer,
// with the filter in use and without it; their ratio leaves the filter
// alone, and its -3 dB point, between the tones that straddle it
static const unsigned int corner_rates[] = { 11025, 22050, 32000 };

enum et_result c_dac_corner(struct et_ctx *c, const struct et_case *k)
{
	static const double frac[] = { 0.15, 0.25, 0.32, 0.38, 0.42, 0.46 };
	enum { NT = sizeof(frac) / sizeof(frac[0]) };
	unsigned int rate = corner_rates[k->arg];
	struct et_fmt pf = { ET_S16, 2, rate };
	struct et_fmt rf = { ET_S16, 2, rate % 8000 ? 44100 : 48000 };
	unsigned long pn = 3 * rate, i;
	double lvl[2][NT], corner = -1, worst = 0;
	int on, t;
	void *pat;

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (et_mix_rec_a2(c, 13) < 0)
		return ET_ERROR;
	et_ctl_set(c, "Audio 2 Oversampling Playback Switch", 0, 0, 0);
	et_ctl_set(c, "Audio 1 Filter Switch", 0, 1, 1);
	pat = et_alloc_frames(&pf, pn);
	for (i = 0; i < pn; i++) {
		double v = 0;

		for (t = 0; t < NT; t++)
			v += 2500 * sin(2 * M_PI * frac[t] * rate * i / rate);
		et_pack1(&pf, pat, i, 0, v);
		et_pack1(&pf, pat, i, 1, v);
	}
	for (on = 0; on < 2; on++) {
		struct et_stream s[2];
		double *x;

		et_ctl_set(c, "Audio 2 Filter Playback Switch", 0, on, on);
		s[0] = et_play(ET_A2, &pf, pat, pn, 0);
		s[1] = et_rec(ET_A1C, &rf, 2 * rf.rate, 0.6);
		if (et_run(c, s, 2) || s[1].done < rf.rate) {
			free(s[1].data);
			free(pat);
			return ET_ERROR;
		}
		x = et_chan(&rf, s[1].data, s[1].done, 0);
		for (t = 0; t < NT; t++)
			lvl[on][t] = an_dbfs(an_tone(x + rf.rate / 5, rf.rate,
						     rf.rate, frac[t] * rate,
						     NULL));
		free(x);
		free(s[1].data);
	}
	free(pat);
	// the filter's response, against the lowest tone's
	for (t = 0; t < NT; t++) {
		double r = lvl[1][t] - lvl[0][t] - (lvl[1][0] - lvl[0][0]);

		et_note(c, "%.0f Hz (%.2f of the rate): %+.1f dB", frac[t] * rate,
			frac[t], r);
		if (r < worst)
			worst = r;
		if (corner < 0 && t && r <= -3) {
			double r0 = lvl[1][t - 1] - lvl[0][t - 1] -
				    (lvl[1][0] - lvl[0][0]);

			corner = frac[t - 1] + (frac[t] - frac[t - 1]) *
						       (-3 - r0) / (r - r0);
		}
	}
	if (corner < 0) {
		et_fail(c, "the filter takes no tone down 3 dB (%.1f dB at most)",
			worst);
		return ET_FAIL;
	}
	et_measure(c, "the filter's -3 dB point, of the rate", corner, "", 0.3,
		   0.5);
	et_measure(c, "the filter's -3 dB point", corner * rate, "Hz", 1, 0);
	return ET_PASS;
}
