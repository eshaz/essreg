// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The rate, latency and music groups. rate.max finds the highest rate each
 * engine really keeps, with the driver's limits lifted (rate_limit=0), for
 * the Kconfig defaults; latency.min the smallest period that plays without
 * an xrun; music.aplay plays a file with aplay on Audio 2, frame by frame
 * on the dig path; midi.mpu sends bytes out of the MPU-401, and midi.mpu.loop
 * hears them come back through a cable.
 *
 * Notes:
 *
 * The rates are the clocks' exact divisors (793.8 and 768 kHz over whole
 * numbers), so that ALSA takes each as it is. Audio 2 is heard through the
 * record mixer by Audio 1 at 44.1 kHz, Audio 1's recording records Audio
 * 2's tone, and Audio 1's playback goes through the cable.
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

// the rates above 44.1 kHz that both clocks divide to exactly, rising
static const unsigned int high[] = {
	48000,	51200,	52920,	56700,	64000,	66150,	76800,	79380,
	88200,	96000,	99225,	113400, 128000, 132300, 153600, 158760,
};

// the tone that a path brought back at a rate: clean and at its pitch;
// through the cable, whose microphone input reaches -18 to -25 dB, by a1play's
// bar
static int clean(struct et_ctx *c, const double *x, long n, double rate,
		 double ppm, double bar, const char *what)
{
	struct an_fit fit;
	double thdn = an_thdn(x, n, rate, ET_HZ, 100, 15000);
	int g = an_glitches(x, n, rate, ET_HZ, (long)(rate / 100), 20, NULL, 0);
	double err = an_fit_sine(x, n, rate, ET_HZ, 1, &fit) ?
			     1e9 :
			     (fit.f / ET_HZ - 1) * 1e6;
	int ok = thdn < bar && !g && fabs(err) < ppm;

	et_note(c, "%s: THD+N %.1f dB, %d glitches, pitch %+.0f ppm: %s", what,
		thdn, g, err, ok ? "clean" : "not clean");
	return ok;
}

// one rate on an engine: 0 clean, 1 not
static int try_rate(struct et_ctx *c, int engine, unsigned int rate)
{
	struct et_fmt f = { ET_S16, 2, rate };
	static const struct et_fmt a2f = { ET_S16, 2, 44100 };
	static const struct et_fmt rf = { ET_S16, 2, 44100 };
	struct et_stream s[2];
	char what[48];
	double *x;
	long skip;
	int ok;

	snprintf(what, sizeof(what), "%u Hz", rate);
	if (engine == 0) {
		// Audio 2 at the rate, heard at 44.1 kHz
		unsigned long pn = 3 * rate;

		s[0] = et_play(ET_A2, &f, et_tone(&f, pn, ET_HZ, ET_HZ_R, 6000),
			       pn, 0);
		s[1] = et_rec(ET_A1C, &rf, 2 * 44100, 0.6);
	} else if (engine == 1) {
		// Audio 1 recording at the rate
		unsigned long pn = 3 * 44100;

		s[0] = et_play(ET_A2, &a2f,
			       et_tone(&a2f, pn, ET_HZ, ET_HZ_R, 6000), pn, 0);
		s[1] = et_rec(ET_A1C, &f, 2 * rate, 0.6);
	} else {
		// Audio 1 playing at the rate, heard by the second card
		unsigned long pn = 3 * rate;

		s[0] = et_play(ET_A1P, &f, et_tone(&f, pn, ET_HZ, ET_HZ_R, 6000),
			       pn, 0.3);
		s[1] = et_rec(ET_REC2, &et_rec2_fmt, 3 * 48000, 0);
	}
	if (et_run(c, s, 2) || s[0].xruns || s[1].xruns) {
		et_note(c, "%s: the streams: %d, %d, %u xruns", what, s[0].err,
			s[1].err, s[0].xruns + s[1].xruns);
		free(s[0].data);
		free(s[1].data);
		return 1;
	}
	x = et_chan(&s[1].fmt, s[1].data, s[1].done, 0);
	skip = (long)(engine == 2 ? 1.0 * s[1].fmt.rate : 0.2 * s[1].fmt.rate);
	ok = clean(c, x + skip, (long)s[1].done - skip - s[1].fmt.rate / 10,
		   s[1].fmt.rate, engine == 2 ? 3000 : 600,
		   engine == 2 ? -12 : -30, what);
	free(x);
	free(s[0].data);
	free(s[1].data);
	return !ok;
}

// rate.max.*: rising through the rates until one fails twice
enum et_result c_rate_max(struct et_ctx *c, const struct et_case *k)
{
	static const char *const names[] = { "Audio 2", "Audio 1 recording",
					     "Audio 1 playback" };
	unsigned int best = 44100, i;

	if (et_param_set(c, "rate_limit", 0) < 0)
		return ET_ERROR;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	if (k->arg < 2) {
		et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
		if (et_mix_rec_a2(c, 13) < 0)
			return ET_ERROR;
	} else {
		et_mix_out(c, 14, 0);
	}
	for (i = 0; i < sizeof(high) / sizeof(high[0]); i++) {
		if (try_rate(c, k->arg, high[i]) && try_rate(c, k->arg, high[i]))
			break;
		best = high[i];
	}
	et_note(c, "%s keeps 16-bit stereo up to %u Hz", names[k->arg], best);
	et_measure(c, "highest clean rate, 16-bit stereo", best, "Hz", 48000,
		   793800);
	return ET_PASS;
}

// latency.min.*: the smallest period of four that plays without an xrun:
// Audio 2 checked frame by frame on the dig path, Audio 1 by its xruns
enum et_result c_latency_min(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	static const unsigned long periods[] = { 32, 64, 128, 256, 512, 1024 };
	unsigned long n = 3 * 44100, best = 0, i;
	enum et_role role = k->arg ? ET_A1P : ET_A2;

	et_mix_quiet(c);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	et_mix_dig(c, 0);
	for (i = 0; i < sizeof(periods) / sizeof(periods[0]) && !best; i++) {
		struct et_stream s = et_play(role, &f,
					     et_tone(&f, n, ET_HZ, 0, 3000), n, 0);

		s.period = periods[i];
		s.buffer = 4 * periods[i];
		if (!et_run(c, &s, 1) && !s.xruns && s.period_got == periods[i])
			best = periods[i];
		et_note(c, "period %lu (took %lu, buffer %lu): %u xruns%s",
			periods[i], s.period_got, s.buffer_got, s.xruns,
			s.err ? ", failed" : "");
		free(s.data);
	}
	if (!best) {
		et_fail(c, "no period up to 1024 frames played without an xrun");
		return ET_PASS;
	}
	et_measure(c, "smallest period without an xrun", best, "frames", 32,
		   512);
	et_measure(c, "its buffer's latency", 4.0 * best / 44100 * 1000, "ms",
		   1, 0);
	return ET_PASS;
}

// music.aplay: a file through aplay on Audio 2, every frame on the dig path
enum et_result c_music_aplay(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	unsigned long n = 3 * 44100, i, rn = 5 * ET_FM_RATE;
	char path[512], cmd[700];
	struct et_stream s[2];
	unsigned long matched = 0, first = 0;
	short *r;
	void *pat;
	FILE *w;

	(void)k;
	if (access("/usr/bin/aplay", X_OK) || !strcmp(c->hal->name, "simulated"))
		return ET_SKIP;
	// chords that change every half second, as a file of music would
	pat = et_alloc_frames(&f, n);
	for (i = 0; i < n; i++) {
		static const double roots[6] = { 261.6, 329.6, 392.0,
						 220.0, 293.7, 349.2 };
		double t = (double)i / 44100, root = roots[(i / 22050) % 6];

		et_pack1(&f, pat, i, 0,
			 3000 * (sin(2 * M_PI * root * t) +
				 sin(2 * M_PI * root * 1.26 * t)));
		et_pack1(&f, pat, i, 1,
			 3000 * (sin(2 * M_PI * root * 1.5 * t) +
				 sin(2 * M_PI * root * 2 * t)));
	}
	memset(pat, 0x55, 4);
	snprintf(path, sizeof(path), "%s/music.raw", c->outdir);
	w = fopen(path, "wb");
	if (!w || fwrite(pat, et_fmt_bytes(&f), n, w) != n) {
		if (w)
			fclose(w);
		free(pat);
		return ET_ERROR;
	}
	fclose(w);
	if (et_mix_quiet(c) || et_mix_dig(c, 1)) {
		free(pat);
		return ET_ERROR;
	}
	snprintf(cmd, sizeof(cmd),
		 "aplay -q -D hw:CARD=ES1869,DEV=0,SUBDEV=0 -t raw -f S16_LE -r 44100 -c 2 '%s'",
		 path);
	s[0] = et_rec(ET_FMC, &et_fm_fmt, rn, 0);
	memset(&s[1], 0, sizeof(s[1]));
	s[1].role = ET_CMD;
	s[1].cmd = cmd;
	s[1].start = 0.4;
	s[1].timeout = 10;
	if (et_run(c, s, 2) || s[1].status) {
		et_fail(c, "aplay: exit %d", s[1].status);
		et_mix_dig(c, 0);
		free(pat);
		free(s[0].data);
		return ET_ERROR;
	}
	et_mix_dig(c, 0);
	r = s[0].data;
	for (i = 0; i < s[0].done; i++) {
		int e[2];

		if (r[2 * i] == ET_FM_IDLE && r[2 * i + 1] == ET_FM_IDLE &&
		    !matched)
			continue;
		et_dig_frame(&f, pat, matched, e);
		if (r[2 * i] != e[0] || r[2 * i + 1] != e[1])
			break;
		if (!matched)
			first = i;
		if (++matched == n)
			break;
	}
	et_measure(c, "frames played exactly", matched, "", n - 16, n);
	if (matched < n - 16)
		et_dump(c, "fm", &et_fm_fmt, s[0].data, s[0].done);
	(void)first;
	free(pat);
	free(s[0].data);
	return matched < n && matched >= n - 16 ? ET_KNOWN : ET_PASS;
}

// midi.mpu: amidi sends a note on and off out of the MPU-401
enum et_result c_midi_mpu(struct et_ctx *c, const struct et_case *k)
{
	struct et_stream s;

	(void)k;
	if (access("/usr/bin/amidi", X_OK) || !strcmp(c->hal->name, "simulated"))
		return ET_SKIP;
	memset(&s, 0, sizeof(s));
	s.role = ET_CMD;
	s.cmd = "amidi -p hw:CARD=ES1869,DEV=0 -S '90 3C 40 80 3C 00'";
	s.timeout = 5;
	et_run(c, &s, 1);
	et_measure(c, "amidi's exit", s.status, "", 0, 0);
	return ET_PASS;
}

// midi.mpu.loop: with a cable from the MPU-401's output to its input, a run of
// notes comes back as it went, which takes the chip's input and its interrupt
// as well as the output. Nothing back is no cable, not a failure
#define LOOP_NOTES 80

enum et_result c_midi_loop(struct et_ctx *c, const struct et_case *k)
{
	static char cmd[64 + 1024 + 300];
	unsigned char sent[3 * LOOP_NOTES];
	unsigned long got = 0, diff = 0;
	char path[256], *p = cmd;
	struct et_stream s;
	unsigned int b;
	FILE *f;
	int i;

	(void)k;
	if (access("/usr/bin/amidi", X_OK) || !strcmp(c->hal->name, "simulated"))
		return ET_SKIP;
	snprintf(path, sizeof(path), "%s/midi-loop.txt", c->outdir);
	p += sprintf(p, "amidi -p hw:CARD=ES1869,DEV=0 -S '");
	for (i = 0; i < LOOP_NOTES; i++) {
		sent[3 * i] = 0x90 | (i & 15);
		sent[3 * i + 1] = 0x24 + i % 60;
		sent[3 * i + 2] = 1 + (i * 37) % 126;
		p += sprintf(p, "%02X %02X %02X ", sent[3 * i], sent[3 * i + 1],
			     sent[3 * i + 2]);
	}
	snprintf(p, sizeof(cmd) - (p - cmd), "' -d -t 1 > %s 2>&1", path);
	memset(&s, 0, sizeof(s));
	s.role = ET_CMD;
	s.cmd = cmd;
	s.timeout = 5;
	et_run(c, &s, 1);
	f = fopen(path, "r");
	if (!f) {
		et_fail(c, "no output from amidi");
		return ET_ERROR;
	}
	while (fscanf(f, "%x", &b) == 1) {
		if (got < sizeof(sent) && b != sent[got])
			diff++;
		got++;
	}
	fclose(f);
	if (!got) {
		et_note(c, "nothing came back: no cable from MIDI out to MIDI in");
		return ET_SKIP;
	}
	et_measure(c, "the bytes that came back", got, "", sizeof(sent),
		   sizeof(sent));
	et_measure(c, "the bytes that differ", diff, "", 0, 0);
	return ET_PASS;
}

// the suite's own path, for --fm-note
static const char *self_exe(void)
{
	static char path[256];
	ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);

	path[n > 0 ? n : 0] = 0;
	return path;
}

// clock.*: the chip's clocks against each other and the system's: the
// 768 kHz family against the 793.8 kHz one, the FM's against the 793.8 kHz
// one, and the 793.8 kHz one against the system's clock, which has no NTP
// on the Armada, so only within 500 ppm
// what the driver's clock has measured: the master against the raw system
// clock and the clock NTP steers against that, in ppm
int et_master_ppm(struct et_ctx *c, double *master, double *steered)
{
	char proc[4096];
	const char *p, *q;
	long long m, st;

	if (c->hal->proc(c->hal->ctx, proc, sizeof(proc)) < 0)
		return -1;
	p = strstr(proc, "clock: the master ");
	q = strstr(proc, "clock: the steered clock ");
	if (!p || !q || sscanf(p, "clock: the master %lld", &m) != 1 ||
	    sscanf(q, "clock: the steered clock %lld", &st) != 1)
		return -1;
	*master = m / 1e3;
	*steered = st / 1e3;
	return 0;
}

enum et_result c_clock(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt a2f = { ET_S16, 2, 44100 };
	struct et_fmt rf = { ET_S16, 2, k->arg == 0 ? 48000 : 44100 };
	struct et_stream s[2];
	struct an_fit fit;
	char cmd[400];
	double *x, want = ET_HZ, ppm = 0;
	unsigned long pn = 4 * 44100;

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (et_mix_rec_a2(c, k->arg == 1 ? 0 : 13) < 0)
		return ET_ERROR;
	if (k->arg == 2) {
		// the driver's line through 30 s of the stream's interrupts; a
		// stream's start and end, timed by hand, are off by milliseconds
		double master, steered;

		if (et_param_set(c, "a2_resample", 2))
			return ET_ERROR;
		s[0] = et_play(ET_A2, &a2f, et_tone(&a2f, 30 * 44100, 0, 0, 0),
			       30 * 44100, 0);
		if (et_run(c, s, 1)) {
			free(s[0].data);
			return ET_ERROR;
		}
		free(s[0].data);
		if (et_master_ppm(c, &master, &steered)) {
			et_fail(c, "the driver has no reading of the clock");
			return ET_ERROR;
		}
		et_measure(c, "793.8 kHz against the system's raw clock", master,
			   "ppm", -100, 100);
		et_measure(c, "the steered clock against the raw one", steered,
			   "ppm", -500, 500);
		return ET_PASS;
	}
	if (k->arg == 0) {
		s[0] = et_play(ET_A2, &a2f, et_tone(&a2f, pn, ET_HZ, ET_HZ, 6000),
			       pn, 0);
	} else {
		// a native note, 581 / 2^16 of the FM's rate, with the output
		// muted: the record mixer takes the FM before the master
		et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
		et_ctl_set(c, "FM Capture Volume", 0, 12, 12);
		et_ctl_set(c, "Video Playback Switch", 0, 0, 0);
		snprintf(cmd, sizeof(cmd), "%s --fm-note esfm:0x30:581:4:3.5",
			 self_exe());
		memset(&s[0], 0, sizeof(s[0]));
		s[0].role = ET_CMD;
		s[0].cmd = cmd;
		s[0].timeout = 8;
		want = 581.0 * ET_FM_RATE / 65536;
	}
	s[1] = et_rec(ET_A1C, &rf, 2 * rf.rate, 0.8);
	if (et_run(c, s, 2) || (k->arg == 1 && s[0].status)) {
		if (k->arg == 0)
			free(s[0].data);
		free(s[1].data);
		return ET_ERROR;
	}
	x = et_chan(&rf, s[1].data, s[1].done, 0);
	if (an_fit_sine(x, s[1].done, rf.rate, want, 1, &fit)) {
		et_fail(c, "no tone came back");
	} else {
		// Audio 2's tone comes back low when Audio 1's 768 kHz clock
		// runs fast, and the FM's note high when the FM's clock does
		ppm = (fit.f / want - 1) * 1e6;
		if (k->arg == 0)
			ppm = -ppm;
		// the FM's clock is the master over 480, 49612.5 Hz, 10 ppm
		// under PCM 1's 49613, as essprobe measured it in DOS
		et_measure(c, k->arg == 0 ? "768 kHz against 793.8 kHz" :
					    "the FM's clock against 793.8 kHz",
			   ppm, "ppm", k->arg == 0 ? -3000 : -100,
			   k->arg == 0 ? 3000 : 100);
	}
	free(x);
	if (k->arg == 0)
		free(s[0].data);
	free(s[1].data);
	return ET_PASS;
}

// --- a2.rate -----------------------------------------------------------------

static const unsigned int a2_rates[] = { 8000,  11025, 16000, 22050, 32000,
					 44100, 48000, 88200, 96000 };

// a2.rate.*: Audio 2 at each rate, heard by Audio 1 at 48 kHz through the
// record mixer (69h), with both filters on so that no image folds onto the
// tone: its pitch on the clock the driver chose, which a wrong divisor or
// clock puts thousands of ppm off, against the 768 kHz clock Audio 1
// records on, 250 ppm from the 793.8 kHz one; and its THD+N below the first
// image
enum et_result c_a2_rate(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt rf = { ET_S16, 2, 48000 };
	unsigned int rate = a2_rates[k->arg];
	struct et_fmt pf = { ET_S16, 2, rate };
	unsigned long pn = 3 * rate, rn = 2 * 48000;
	struct et_stream s[2];
	struct an_fit fit;
	double *x, ppm, hi;

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (et_mix_rec_a2(c, 13) < 0)
		return ET_ERROR;
	et_ctl_set(c, "Audio 2 Filter Playback Switch", 0, 1, 1);
	et_ctl_set(c, "Audio 1 Filter Switch", 0, 1, 1);
	s[0] = et_play(ET_A2, &pf, et_tone(&pf, pn, ET_HZ, ET_HZ, 6000), pn, 0);
	s[1] = et_rec(ET_A1C, &rf, rn, 0.6);
	if (et_run(c, s, 2)) {
		et_fail(c, "the streams: %s, %s",
			s[0].err ? strerror(-s[0].err) : "ok",
			s[1].err ? strerror(-s[1].err) : "ok");
		free(s[0].data);
		free(s[1].data);
		return ET_ERROR;
	}
	x = et_chan(&rf, s[1].data, s[1].done, 0);
	if (s[1].done < 48000 ||
	    an_fit_sine(x + 9600, s[1].done - 9600, 48000, ET_HZ, 1, &fit)) {
		et_fail(c, "no tone in the recording");
	} else {
		ppm = (fit.f / ET_HZ - 1) * 1e6;
		et_measure(c, "pitch against Audio 1's clock", ppm, "ppm",
			   rate % 8000 ? -600 : -100, rate % 8000 ? 600 : 100);
		hi = 0.45 * rate < 20000 ? 0.45 * rate : 20000;
		et_measure(c, "THD+N below the first image",
			   an_thdn(x + 9600, s[1].done - 9600, 48000, ET_HZ, 100,
				   hi),
			   "dB", -200, -30);
	}
	free(x);
	free(s[0].data);
	free(s[1].data);
	return ET_PASS;
}
