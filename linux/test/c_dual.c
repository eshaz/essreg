// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The dual group: PCM device 2, both DACs as one four-channel device,
 * heard on the left output through the cable. Channel 1 is Audio 1's left
 * and channel 3 Audio 2's, which sum on the output.
 *
 * Notes:
 *
 * Each DAC plays its own tone from frame 0, 441 Hz on channel 1 and 882 Hz
 * on channel 3, and the phases that the output's fit finds give when each
 * began, whatever the DACs' gains, to well under a frame. A whole number of
 * the higher tone's periods fits in the lower's, so the difference is known
 * within half the higher's period, 25 frames.
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

static const unsigned int dual_rates[] = { 44100, 22050, 48000 };
static struct et_fmt f4 = { ET_S16, 4, 44100 };

// a run of the dual device with the buffer given, heard by the second
// card; the recording's left side (malloc'ed) and its frames
static double *dual_run(struct et_ctx *c, void *buf, unsigned long frames,
			unsigned long *n)
{
	struct et_stream s[2];
	double *x = NULL;

	s[0] = et_rec(ET_REC2, &et_rec2_fmt,
		      (unsigned long)((frames / (double)f4.rate + 1.2) * 48000),
		      0);
	s[1] = et_play(ET_DUAL, &f4, buf, frames, 0.4);
	if (!et_run(c, s, 2)) {
		x = et_chan(&et_rec2_fmt, s[0].data, s[0].done, 0);
		*n = s[0].done;
	} else {
		et_fail(c, "the dual device: %s",
			s[1].err ? strerror(-s[1].err) : "ok");
	}
	free(s[0].data);
	return x;
}

static int dual_on(struct et_ctx *c)
{
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_mix_out(c, 14, 15);
	if (et_param_set(c, "dual_playback", 1) < 0) {
		et_fail(c, "dual_playback=1 refused");
		return -1;
	}
	return 0;
}

// a run of 441 Hz on channel 1 and 882 Hz on channel 3, both from
// frame 0: each tone fitted over a steady half second of the output
static int dual_tones(struct et_ctx *c, struct an_fit *f1, struct an_fit *f3)
{
	unsigned long frames = 2 * f4.rate, i, n = 0;
	void *buf = et_alloc_frames(&f4, frames);
	double *x, noise;
	long on, a;
	int bad;

	if (dual_on(c)) {
		free(buf);
		return -1;
	}
	for (i = 0; i < frames; i++) {
		double t = (double)i / f4.rate;

		et_pack1(&f4, buf, i, 0, 4000 * sin(2 * M_PI * ET_HZ * t));
		et_pack1(&f4, buf, i, 2, 4000 * sin(2 * M_PI * 2 * ET_HZ * t));
	}
	x = dual_run(c, buf, frames, &n);
	free(buf);
	if (!x)
		return -1;
	noise = et_noise_floor(x, n);
	on = an_onset(x, n, 8 * noise + 30);
	a = on + 24000;
	bad = on < 0 || a + 24000 > (long)n ||
	      an_fit_sine(x + a, 24000, 48000, ET_HZ, 1, f1) ||
	      an_fit_sine(x + a, 24000, 48000, 2 * ET_HZ, 1, f3);
	if (bad)
		et_fail(c, "the second card didn't hear both tones");
	free(x);
	return bad ? -1 : 0;
}

// dual.map: channel 1 on Audio 1 and channel 3 on Audio 2 both heard, on
// one clock: their pitches exactly 2 apart
enum et_result c_dual_map(struct et_ctx *c, const struct et_case *k)
{
	struct an_fit f1, f3;

	(void)k;
	f4.rate = 44100;
	if (dual_tones(c, &f1, &f3))
		return ET_ERROR;
	et_measure(c, "channel 1, 441 Hz", an_dbfs(f1.amp / M_SQRT2), "dBFS",
		   -60, -1);
	et_measure(c, "channel 3, 882 Hz", an_dbfs(f3.amp / M_SQRT2), "dBFS",
		   -60, -1);
	et_measure(c, "their pitches, against 2", (f3.f / f1.f / 2 - 1) * 1e6,
		   "ppm", -100, 100);
	return ET_PASS;
}

// dual.align: when each DAC began, from its tone's phase: a sine from
// frame 0 has the phase 2 pi f (t - start) at t, so the starts differ by
// what the phases leave after their own frequencies
enum et_result c_dual_align(struct et_ctx *c, const struct et_case *k)
{
	struct an_fit f1, f3;
	double s1, s3, d, p1, p3;

	f4.rate = dual_rates[k->arg];
	if (dual_tones(c, &f1, &f3))
		return ET_ERROR;
	// the start of each, in the recording's seconds before its window,
	// within one period of the tone: sin(2 pi f t + phase)
	p1 = 1 / f1.f;
	p3 = 1 / f3.f;
	s1 = fmod(fmod(-f1.phase / (2 * M_PI * f1.f), p1) + p1, p1);
	s3 = fmod(fmod(-f3.phase / (2 * M_PI * f3.f), p3) + p3, p3);
	// the difference within half of channel 3's period
	d = fmod(s3 - s1, p3);
	if (d > p3 / 2)
		d -= p3;
	if (d < -p3 / 2)
		d += p3;
	et_measure(c, "Audio 2 against Audio 1", d * f4.rate, "frames", -6, 6);
	et_measure(c, "Audio 2 against Audio 1, in time", d * 1e6, "us", 1, 0);
	// on the Armada the two began 1 to 4.5 frames apart, not the same at
	// every rate or every start, so no fixed skew puts them in step
	if (fabs(d * f4.rate) > 0.5 && !c->fails) {
		snprintf(c->why, sizeof(c->why),
			 "Audio 2 began %+.1f frames from Audio 1, not in step",
			 d * f4.rate);
		return ET_KNOWN;
	}
	return ET_PASS;
}

// --- both: Audio 1 started and stopped under Audio 2 -------------------------

static int by_value(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return x < y ? -1 : x > y;
}

static double median(const double *v, long n)
{
	double *t = malloc((n > 0 ? n : 1) * sizeof(*t)), m;

	if (!t)
		abort();
	memcpy(t, v, n * sizeof(*t));
	qsort(t, n, sizeof(*t), by_value);
	m = n > 0 ? t[n / 2] : -200;
	free(t);
	return m;
}

// both.a1play and both.a1rec: Audio 2 plays 441 Hz to the cable for 14 s
// while aplay or arecord starts and stops Audio 1 eight times; Audio 2's
// level in every 50 ms of it within 6 dB of its median, so that no start
// or stop of Audio 1 (its close sends DSP D3h, the speaker off) silences
// Audio 2. mix.monitor: the same recordings with the record monitor on and
// a silent record source, which the monitor puts on AOUT in place of the
// playback mix, so that Audio 2 falls silent in each
enum et_result c_both_restart(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	unsigned long pn = 14 * 44100, rn = 15 * 48000;
	const long win = 2400; // 50 ms at 48 kHz
	struct et_stream s[3];
	double *x, *lvl, noise, med, worst = 0;
	long on, i, nw, at = -1;
	int dips = 0;
	char cmd[400];

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_mix_out(c, 14, 15);
	if (k->arg == 2) {
		// the record mixer, every input of it at 0
		if (et_mix_rec_a2(c, 0) < 0)
			return ET_ERROR;
	}
	et_ctl_set(c, "Record Monitor", 0, k->arg == 2, k->arg == 2);
	snprintf(cmd, sizeof(cmd),
		 k->arg ? "for i in 1 2 3 4 5 6 7 8; do arecord -q -D hw:CARD=ES1869,DEV=0 -t raw -f S16_LE -r 44100 -c 2 -s 22050 >/dev/null; sleep 0.5; done" :
			  "for i in 1 2 3 4 5 6 7 8; do head -c 88200 /dev/zero | aplay -q -D hw:CARD=ES1869,DEV=0,SUBDEV=1 -t raw -f S16_LE -r 44100 -c 2 -; sleep 0.5; done");
	s[0] = et_rec(ET_REC2, &et_rec2_fmt, rn, 0);
	s[1] = et_play(ET_A2, &f, et_tone(&f, pn, ET_HZ, ET_HZ, 6000), pn, 0.3);
	memset(&s[2], 0, sizeof(s[2]));
	s[2].role = ET_CMD;
	s[2].cmd = cmd;
	s[2].start = 2;
	s[2].timeout = 12;
	if (et_run(c, s, 3) && (s[0].err || s[1].err)) {
		et_fail(c, "the streams: %s, %s", s[0].err ? strerror(-s[0].err) : "ok",
			s[1].err ? strerror(-s[1].err) : "ok");
		free(s[0].data);
		free(s[1].data);
		return ET_ERROR;
	}
	et_measure(c, k->arg ? "arecord's exit" : "aplay's exit", s[2].status,
		   "", 0, 0);
	et_measure(c, "Audio 2's xruns", s[1].xruns, "", 0, 0);
	x = et_chan(&et_rec2_fmt, s[0].data, s[0].done, 0);
	noise = et_noise_floor(x, s[0].done);
	on = an_onset(x, s[0].done, 8 * noise + 30);
	if (on < 0) {
		et_fail(c, "the second card heard nothing");
		goto out;
	}
	// from 1 s after the onset to 1 s before the tone's end, past the
	// second card's gain control settling
	on += 48000;
	nw = (long)(12 * 48000) / win;
	if (on + nw * win > (long)s[0].done)
		nw = ((long)s[0].done - on) / win;
	lvl = calloc(nw > 0 ? nw : 1, sizeof(*lvl));
	if (!lvl)
		abort();
	for (i = 0; i < nw; i++)
		lvl[i] = an_dbfs(an_tone(x + on + i * win, win, 48000, ET_HZ,
					 NULL) / M_SQRT2);
	med = median(lvl, nw);
	for (i = 0; i < nw; i++) {
		if (lvl[i] - med < worst) {
			worst = lvl[i] - med;
			at = i;
		}
		// the first dips over 3 dB, where a start or a stop came
		if (lvl[i] - med < -3 && dips++ < 8)
			et_note(c, "%.1f dB at %.2f s after the onset",
				lvl[i] - med, 1 + i * 0.05);
	}
	et_note(c, "Audio 2 at %.1f dBFS in the cable's median 50 ms", med);
	if (at >= 0)
		et_note(c, "its lowest 50 ms %.1f s after the onset", 1 + at * 0.05);
	if (k->arg == 2)
		et_measure(c, "the output's lowest 50 ms with the monitor, against its median",
			   worst, "dB", -200, -20);
	else
		et_measure(c, "Audio 2's lowest 50 ms against its median", worst,
			   "dB", -6, 0);
	free(lvl);
out:
	if (c->fails)
		et_dump(c, "rec2", &et_rec2_fmt, s[0].data, s[0].done);
	free(x);
	free(s[0].data);
	free(s[1].data);
	return ET_PASS;
}
