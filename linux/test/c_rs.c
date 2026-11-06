// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The rs group: Audio 2's rate correction on the chip. On the dig path the
 * ring's frames come back one for one, so what the kernel's filter made is
 * there to compare, frame by frame, with the same filter's C code on this
 * machine, and to look at, in spectra: the band, two tones, a quiet tone,
 * dither. Then the stream's life under the filter: changes by the mixer's
 * controls while it plays, a pause, an underrun and a drain. Then real time:
 * the stream's position against the clock NTP steers, a line through
 * readings of it, with the same run in the Off mode as the control. Then
 * what it costs: the filter's cycles a frame, the CPU that an idle-priority
 * thread finds left, and the longest a fill keeps interrupts off.
 *
 * Notes:
 *
 * A step of 30 ppm is what the Armada's crystal takes against real time, and
 * 252 ppm what 48 kHz takes on any crystal.
 *
 * Slaved to the FM, Audio 2 plays a frame a music DAC frame, so a tone's
 * pitch in the recording's frames is the step, and the rate of a Clock mode
 * stream can't be measured there. The chip's own clocks are the rs.clock
 * cases', which don't use the dig path.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "an.h"
#include "cases.h"
#include "hal.h"
#include "sim_rs.h"

#define RS_FIFO 16 // frames of 16-bit stereo in Audio 2's FIFO
#define RS_AHEAD 34 // the frames the filter reads ahead of a position
#define RS_TRIM_ZERO 12 // Audio 2 Rate Trim's item for no trim
#define RS_TRIM_M100 2 // and for -100 ppm
#define RS_BAND_FRAMES (1UL << 17)
#define RS_CTL_MODE "Audio 2 Rate Correction"
#define RS_CTL_TRIM "Audio 2 Rate Trim"
#define RS_CTL_DITHER "Audio 2 Rate Dither Playback Switch"
#define RS_THDN_BAND -85.0 // dB, to 20 kHz
#define RS_SPUR_BAND -90.0
#define RS_TREBLE_FLAT 0.2 // dB, the DAC's response taken back to 20 kHz
#define RS_DAC_LAST 0.062 // of the last sample, in what the DAC puts out

enum { RS_OFF, RS_TRIM, RS_CLOCK };

// a recording kept for a look at it elsewhere, with ET_KEEP in the environment
static void rs_keep(struct et_ctx *c, double tag, const struct et_fmt *f,
		    const void *rec, unsigned long n)
{
	char name[32];

	if (!getenv("ET_KEEP"))
		return;
	snprintf(name, sizeof(name), "%.0f", tag);
	et_dump(c, name, f, rec, n);
}

// a change in ppb as the driver takes it, what a ring frame takes over a
// stream frame in 32 bits
static double es_step(long ppb)
{
	return floor((double)ppb * 4294967296.0 / 1e9) / 4294967296.0;
}

static int rs_ring_over = 1; // the ring frames to a stream frame

// the mode, and the change in ppb in Trim
static int rs_mode(struct et_ctx *c, int mode, long ppb)
{
	if (et_param_set(c, "a2_trim", RS_TRIM_ZERO) ||
	    et_param_set(c, "a2_trim_ppb", ppb) ||
	    et_param_set(c, "a2_resample", mode) ||
	    et_param_set(c, "a2_resample_dither", 0) ||
	    et_param_set(c, "a2_treble", 0) ||
	    et_param_set(c, "a2_oversample", rs_ring_over - 1))
		return -1;
	return 0;
}

// frames of f through Audio 2 as the DMA took them, on the dig path, in a
// recording of *nrec frames of the FM's format for the caller to free
static enum et_result rs_dig(struct et_ctx *c, const struct et_fmt *f,
			     void *data, unsigned long frames,
			     struct et_stream *tune, struct et_stream *more,
			     int nmore, short **rec, unsigned long *nrec)
{
	struct et_stream s[8];
	unsigned long n = (unsigned long)((0.3 + (double)frames * rs_ring_over /
					   ET_FM_RATE +
					   (tune ? tune->pause_for : 0) + 0.6) *
					  ET_FM_RATE);
	int i;

	if (et_mix_quiet(c) || et_mix_dig(c, 1)) {
		et_fail(c, "the mixer refused the dig path");
		return ET_ERROR;
	}
	s[0] = et_rec(ET_FMC, &et_fm_fmt, n, 0);
	s[1] = et_play(ET_A2, f, data, frames, 0.3);
	if (tune) {
		s[1].flags = tune->flags;
		s[1].period = tune->period;
		s[1].buffer = tune->buffer;
		s[1].pause_at = tune->pause_at;
		s[1].pause_for = tune->pause_for;
	}
	for (i = 0; i < nmore; i++)
		s[2 + i] = more[i];
	if (et_run(c, s, 2 + nmore)) {
		et_fail(c, "the streams: %s, %s",
			s[0].err ? strerror(-s[0].err) : "ok",
			s[1].err ? strerror(-s[1].err) : "ok");
		free(s[0].data);
		et_mix_dig(c, 0);
		return ET_ERROR;
	}
	et_mix_dig(c, 0);
	if (tune)
		tune->xruns = s[1].xruns;
	*rec = s[0].data;
	*nrec = s[0].done;
	return ET_PASS;
}

// the recording's first and last frames that aren't the FM's idle level; 0
// with nothing there
static int rs_span(const short *rec, unsigned long n, unsigned long *a,
		   unsigned long *b)
{
	unsigned long i;
	int have = 0;

	for (i = 0; i < n; i++)
		if (rec[2 * i] != ET_FM_IDLE || rec[2 * i + 1] != ET_FM_IDLE) {
			if (!have)
				*a = i;
			have = 1;
			*b = i;
		}
	return have;
}

// how many of the stream's first frames the recording, which begins at a,
// went without, or -1
static long rs_lost(const struct et_fmt *f, const void *ref, unsigned long n,
		    const short *rec, unsigned long a, unsigned long nrec)
{
	unsigned long i, k;

	for (i = 0; i < RS_FIFO * 8 && i + 32 < n; i++) {
		for (k = 0; k < 32 && a + k < nrec; k++) {
			int e[2];

			et_dig_frame(f, ref, i + k, e);
			if (rec[2 * (a + k)] != e[0] ||
			    rec[2 * (a + k) + 1] != e[1])
				break;
		}
		if (k == 32)
			return (long)i;
	}
	return -1;
}

// noise, a sweep up the band, and noise again, so that every bit of the sums
// moves and every frequency goes through, and neither the filter's overshoot
// nor the FM's idle level leaves 16 bits
static void *rs_signal(const struct et_fmt *f, unsigned long frames)
{
	void *d = et_alloc_frames(f, frames);
	unsigned int lfsr = 0xACE1 + f->channels;
	double ph = 0, f1 = 20, f2 = 20000;
	unsigned long i;
	unsigned int ch;

	for (i = 0; i < frames; i++) {
		double u = (double)i / frames;

		for (ch = 0; ch < f->channels; ch++) {
			double v;

			if (u < 0.2 || u >= 0.8) {
				v = (int)(an_lfsr(&lfsr) & 0x3FFF) - 8192;
				v *= 1.1;
			} else {
				double p = (u - 0.2) / 0.6;

				if (!ch)
					ph += 2 * M_PI * f1 * pow(f2 / f1, p) /
					      f->rate;
				v = 26000 * sin(ch ? 2 * ph : ph);
			}
			et_pack1(f, d, i, ch, v);
		}
	}
	return d;
}

// rs.exact: every frame that the DMA took against the same filter's C code
// here, which also means the MMX code against the C code, under real
// interrupts, across every chunk, seam and fill of the driver
// 3x from 22050 Hz: the recording takes as much of the bus as the stream, and
// 132.3 kHz twice over is more than the ISA bus carries
enum et_result c_rs_exact(struct et_ctx *c, const struct et_case *k)
{
	static const struct {
		unsigned int channels, rate;
		long ppb;
		int polled;
		unsigned int over;
	} t[] = {
		{ 2, 44100, 1000000, 0, 1 }, { 2, 44100, -1000000, 0, 1 },
		{ 1, 44100, 1000000, 0, 1 }, { 2, 44100, 30000, 0, 1 },
		{ 2, 48000, -251953, 0, 1 }, { 2, 44100, 1000000, 1, 1 },
		{ 2, 44100, 30000, 0, 2 }, { 2, 22050, 30000, 0, 3 },
	};
	struct et_fmt f = { ET_S16, t[k->arg].channels, t[k->arg].rate };
	unsigned long n = 4 * f.rate, made, nrec = 0, a = 0, b = 0, i, j, want;
	unsigned long diff = 0, cmp = 0;
	struct et_stream tune;
	void *d = rs_signal(&f, n);
	short *ref, *rec = NULL;
	long worst = 0, first = -1, skip;
	enum et_result r;

	memset(&tune, 0, sizeof(tune));
	if (t[k->arg].polled) {
		tune.flags = ET_MMAP | ET_POLLED;
		tune.buffer = 4096;
		tune.period = 2048;
	}
	if (t[k->arg].over > 2) {
		tune.buffer = 4096;
		tune.period = 1024;
	}
	rs_ring_over = t[k->arg].over;
	if (rs_mode(c, RS_TRIM, t[k->arg].ppb)) {
		rs_ring_over = 1;
		free(d);
		return ET_ERROR;
	}
	ref = sim_rs_over(d, n, f.channels, t[k->arg].ppb, t[k->arg].over, &made);
	r = rs_dig(c, &f, d, n, &tune, NULL, 0, &rec, &nrec);
	rs_ring_over = 1;
	if (r != ET_PASS)
		goto out;
	rs_keep(c, 0, &et_fm_fmt, rec, nrec);
	if (!rs_span(rec, nrec, &a, &b)) {
		et_fail(c, "nothing came back");
		r = ET_ERROR;
		goto out;
	}
	// a start can lose frames, and a stop the FIFO, and the frames past the
	// last are the drain's
	want = made > RS_FIFO + RS_AHEAD * t[k->arg].over ?
		       made - RS_FIFO - RS_AHEAD * t[k->arg].over : 0;
	skip = rs_lost(&f, ref, want, rec, a, nrec);
	if (skip < 0) {
		et_fail(c, "the recording doesn't begin with the stream");
		et_dump(c, "fm", &et_fm_fmt, rec, nrec);
		r = ET_FAIL;
		goto out;
	}
	et_measure(c, "the frames lost at the start", skip, "", 0, RS_FIFO);
	for (i = skip, j = a; i < want && j < nrec; i++, j++) {
		int e[2];
		long d0, d1;

		et_dig_frame(&f, ref, i, e);
		d0 = (long)rec[2 * j] - e[0];
		d1 = (long)rec[2 * j + 1] - e[1];
		cmp++;
		if (d0 || d1) {
			if (diff < 6)
				et_note(c, "frame %lu: %ld, %ld off, of %d, %d", i,
					d0, d1, e[0], e[1]);
			diff++;
			if (first < 0)
				first = (long)i;
			if (labs(d0) > worst)
				worst = labs(d0);
			if (labs(d1) > worst)
				worst = labs(d1);
		}
	}
	et_measure(c, "the frames compared", (double)cmp, "", 0.98 * want, want);
	et_measure(c, "the frames that differ", (double)diff, "", 0, 0);
	if (diff) {
		struct et_fmt rf = f;

		et_note(c, "the first is frame %ld, the worst by %ld", first, worst);
		et_dump(c, "fm", &et_fm_fmt, rec, nrec);
		et_dump(c, "ref", &rf, ref, made);
	}
out:
	free(ref);
	free(rec);
	free(d);
	return r;
}

// a tone through the filter: its level, noise and distortion, and worst spur
struct rs_tone {
	double db, thdn, spur, at;
	int glitches;
};

static enum et_result rs_tone(struct et_ctx *c, const struct et_fmt *f,
			      double hz, double amp, long ppb, unsigned long n,
			      struct rs_tone *o)
{
	void *d = et_tone(f, n + f->rate, hz, hz, amp);
	double ring = hz * (1 + es_step(ppb));
	unsigned long a = 0, b = 0, nrec = 0;
	short *rec = NULL;
	struct an_fit fit;
	enum et_result r;
	double *x;

	r = rs_dig(c, f, d, n + f->rate, NULL, NULL, 0, &rec, &nrec);
	free(d);
	if (r != ET_PASS)
		return r;
	if (!rs_span(rec, nrec, &a, &b) || b < a + n) {
		free(rec);
		et_fail(c, "no tone came back");
		return ET_ERROR;
	}
	rs_keep(c, hz, &et_fm_fmt, rec, nrec);
	x = et_chan(&et_fm_fmt, rec, nrec, 0);
	a += (b - a - n) / 2;
	if (an_fit_sine(x + a, (long)n, f->rate, ring, 0, &fit)) {
		free(rec);
		free(x);
		et_fail(c, "no tone came back");
		return ET_ERROR;
	}
	o->db = 20 * log10(fit.amp / amp);
	o->thdn = an_thdn(x + a, (long)n, f->rate, ring, 20, 20000);
	o->glitches = an_glitches(x + a, (long)n, f->rate, ring, 512, 20, NULL, 0);
	an_sub_sine(x + a, (long)n, f->rate, ring);
	o->spur = an_spur(x + a, (long)n, f->rate, 20, 20000, fit.amp, &o->at);
	free(rec);
	free(x);
	return ET_PASS;
}

// rs.band: tones up the band: the level, what is left, and the worst thing
// in it. The filter's edge shows at the top of the band first
enum et_result c_rs_band(struct et_ctx *c, const struct et_case *k)
{
	static const double hz[] = { 100.1, 997, 5000.7, 10000.9, 15000.1,
				     18000.3, 19000.1, 20000.3 };
	struct et_fmt f = { ET_S16, 2, k->arg ? 48000 : 44100 };
	long ppb = k->arg ? -251953 : 30000;
	char name[64];
	unsigned int i;

	if (rs_mode(c, RS_TRIM, ppb))
		return ET_ERROR;
	for (i = 0; i < sizeof(hz) / sizeof(hz[0]); i++) {
		struct rs_tone o;
		enum et_result r;

		r = rs_tone(c, &f, hz[i], 29000, ppb, RS_BAND_FRAMES, &o);
		if (r != ET_PASS)
			return r;
		snprintf(name, sizeof(name), "level at %.0f Hz", hz[i]);
		et_measure(c, name, o.db, "dB", -0.01, 0.01);
		snprintf(name, sizeof(name), "THD+N at %.0f Hz", hz[i]);
		et_measure(c, name, o.thdn, "dB", -200, RS_THDN_BAND);
		snprintf(name, sizeof(name), "worst spur at %.0f Hz", hz[i]);
		et_measure(c, name, o.spur, "dB", -200, RS_SPUR_BAND);
		snprintf(name, sizeof(name), "glitches at %.0f Hz", hz[i]);
		et_measure(c, name, o.glitches, "", 0, 0);
	}
	return ET_PASS;
}

// rs.treble: the DAC's response taken back, in the samples, on the dig path:
// each tone comes up by what the hold and the last sample's share take from
// it, on its own or at a step, and the correction adds nothing but its gain
enum et_result c_rs_treble(struct et_ctx *c, const struct et_case *k)
{
	static const double hz[] = { 997, 5000.7, 10000.9, 15000.1, 18000.3,
				     20000.3 };
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	long ppb = k->arg ? 30000 : 0;
	char name[64];
	unsigned int i;

	if (rs_mode(c, k->arg ? RS_TRIM : RS_OFF, ppb) ||
	    et_param_set(c, "a2_treble", 1))
		return ET_ERROR;
	for (i = 0; i < sizeof(hz) / sizeof(hz[0]); i++) {
		double x = M_PI * hz[i] / f.rate;
		double dac = 20 * log10(sin(x) / x *
					sqrt(1 + 2 * RS_DAC_LAST * cos(2 * x) +
					     RS_DAC_LAST * RS_DAC_LAST) /
					(1 + RS_DAC_LAST));
		struct rs_tone o;
		enum et_result r;

		r = rs_tone(c, &f, hz[i], 20000, ppb, RS_BAND_FRAMES, &o);
		if (r != ET_PASS)
			return r;
		snprintf(name, sizeof(name), "level over the DAC at %.0f Hz", hz[i]);
		et_measure(c, name, o.db + dac, "dB", -RS_TREBLE_FLAT, RS_TREBLE_FLAT);
		snprintf(name, sizeof(name), "THD+N at %.0f Hz", hz[i]);
		et_measure(c, name, o.thdn, "dB", -200, RS_THDN_BAND);
		snprintf(name, sizeof(name), "glitches at %.0f Hz", hz[i]);
		et_measure(c, name, o.glitches, "", 0, 0);
	}
	return ET_PASS;
}

// rs.imd: 19 and 20 kHz, and 60 Hz under 7 kHz, as the CCIF and SMPTE tests
// have them: what falls between the tones, against full scale
enum et_result c_rs_imd(struct et_ctx *c, const struct et_case *k)
{
	static const struct {
		double f1, a1, f2, a2;
	} t[] = {
		{ 19000.3, 14000, 20000.3, 14000 },
		{ 60.1, 21000, 7000.7, 5250 },
	};
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	long ppb = 30000;
	unsigned long n = RS_BAND_FRAMES, total = n + 44100, i, a = 0, b = 0;
	unsigned long nrec = 0;
	double s1 = t[k->arg].f1 * (1 + es_step(ppb));
	double s2 = t[k->arg].f2 * (1 + es_step(ppb)), at, top, *x;
	void *d = et_alloc_frames(&f, total);
	short *rec = NULL;
	enum et_result r;

	for (i = 0; i < total; i++) {
		double v = t[k->arg].a1 * sin(2 * M_PI * t[k->arg].f1 * i / 44100) +
			   t[k->arg].a2 * sin(2 * M_PI * t[k->arg].f2 * i / 44100);

		et_pack1(&f, d, i, 0, v);
		et_pack1(&f, d, i, 1, v);
	}
	if (rs_mode(c, RS_TRIM, ppb)) {
		free(d);
		return ET_ERROR;
	}
	r = rs_dig(c, &f, d, total, NULL, NULL, 0, &rec, &nrec);
	free(d);
	if (r != ET_PASS)
		return r;
	if (!rs_span(rec, nrec, &a, &b) || b < a + n) {
		free(rec);
		et_fail(c, "no tones came back");
		return ET_ERROR;
	}
	rs_keep(c, t[k->arg].f2, &et_fm_fmt, rec, nrec);
	x = et_chan(&et_fm_fmt, rec, nrec, 0);
	a += (b - a - n) / 2;
	// the two aren't quite orthogonal over the window: a few turns of each
	for (i = 0; i < 6; i++) {
		an_sub_sine(x + a, (long)n, 44100, s1);
		an_sub_sine(x + a, (long)n, 44100, s2);
	}
	top = an_spur(x + a, (long)n, 44100, 20, 20000, 32767, &at);
	et_measure(c, "what falls between them", top, "dBFS", -200, -92);
	free(rec);
	free(x);
	return ET_PASS;
}

// rs.quiet: a tone 60 dB down: its level, and the noise of 16-bit rounding
enum et_result c_rs_quiet(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	long ppb = 30000;
	struct rs_tone o;
	enum et_result r;

	(void)k;
	if (rs_mode(c, RS_TRIM, ppb))
		return ET_ERROR;
	r = rs_tone(c, &f, 997, 32.767, ppb, RS_BAND_FRAMES, &o);
	if (r != ET_PASS)
		return r;
	et_measure(c, "the level", o.db, "dB", -0.1, 0.1);
	et_measure(c, "THD+N", o.thdn, "dB", -200, -32);
	et_measure(c, "worst spur", o.spur, "dB", -200, -38);
	return ET_PASS;
}

// rs.dither: what the switch does to a quiet tone, a step of 1 ppm, where
// rounding is at its most regular: it has to pay for itself in spurs. Noise
// and the worst spur, without it and with it, in dBFS
enum et_result c_rs_dither(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	long ppb = 1000;
	double amp = 3.2767;
	struct rs_tone o[2];
	int i;

	(void)k;
	for (i = 0; i < 2; i++) {
		enum et_result r;

		if (rs_mode(c, RS_TRIM, ppb) ||
		    et_ctl_set(c, RS_CTL_DITHER, 0, i, i) < 0)
			return ET_ERROR;
		r = rs_tone(c, &f, 997, amp, ppb, RS_BAND_FRAMES, &o[i]);
		if (r != ET_PASS)
			return r;
		o[i].thdn += 20 * log10(amp / sqrt(2) / 32767);
		o[i].spur += 20 * log10(amp / 32767);
	}
	et_measure(c, "noise, without", o[0].thdn, "dBFS", -200, -90);
	et_measure(c, "noise, with", o[1].thdn, "dBFS", -200, -88);
	et_measure(c, "worst spur, without", o[0].spur, "dBFS", 1, 0);
	et_measure(c, "worst spur, with", o[1].spur, "dBFS", 1, 0);
	et_measure(c, "the noise it adds", o[1].thdn - o[0].thdn, "dB", 0, 7);
	return ET_PASS;
}

// a stretch of the recording: the pitch of a tone against nominal, in ppm
static double rs_pitch(const double *x, long n, double hz)
{
	struct an_fit fit;

	if (an_fit_sine(x, n, 44100, hz, 1, &fit))
		return 1e9;
	return (fit.f / hz - 1) * 1e6;
}

// rs.live: the controls alsamixer writes, under a stream: Off, Trim up and
// down, Clock and Off again, each taking effect with the next frames made,
// with no xrun and no step in the tone but where a switch falls
enum et_result c_rs_live(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	enum { PARTS = 5 };
	static const double each = 1.6;
	unsigned long n = (unsigned long)(PARTS * each * ET_FM_RATE), a = 0, b = 0;
	unsigned long nrec = 0, from;
	double hz = 997, master = 0, steered = 0, *x;
	double want[PARTS] = { 0, 100, -100, 0, 0 }, ppm;
	struct et_stream m[5], tune;
	short *rec = NULL;
	void *d = et_tone(&f, n, hz, hz, 20000);
	enum et_result r;
	int i, glitches;

	(void)k;
	if (rs_mode(c, RS_OFF, 0) ||
	    et_ctl_set_enum(c, RS_CTL_TRIM, "+100 ppm") < 0 ||
	    et_ctl_set_enum(c, RS_CTL_MODE, "Off") < 0) {
		free(d);
		return ET_ERROR;
	}
	memset(m, 0, sizeof(m));
	memset(&tune, 0, sizeof(tune));
	tune.buffer = 8192;
	tune.period = 1024;
	for (i = 0; i < 5; i++) {
		m[i].role = ET_CTL;
		m[i].cmd = RS_CTL_MODE;
		m[i].start = 0.3 + (i + 1) * each;
	}
	m[0].value = RS_TRIM;
	m[1].cmd = RS_CTL_TRIM;
	m[1].value = RS_TRIM_M100;
	m[2].cmd = RS_CTL_TRIM;
	m[2].value = RS_TRIM_ZERO;
	m[3].start = m[2].start;
	m[3].value = RS_CLOCK;
	m[4].start = 0.3 + 4 * each;
	m[4].value = RS_OFF;
	r = rs_dig(c, &f, d, n, &tune, m, 5, &rec, &nrec);
	free(d);
	if (r != ET_PASS)
		return r;
	if (!rs_span(rec, nrec, &a, &b) || b < a + (unsigned long)(4 * each * ET_FM_RATE)) {
		free(rec);
		et_fail(c, "no tone came back");
		return ET_ERROR;
	}
	et_measure(c, "xruns", tune.xruns, "", 0, 0);
	if (et_master_ppm(c, &master, &steered))
		master = steered = 0;
	want[3] = steered - master;
	x = et_chan(&et_fm_fmt, rec, nrec, 0);
	for (i = 0; i < PARTS; i++) {
		long len = (long)(0.8 * ET_FM_RATE);
		char name[48];

		from = a + (unsigned long)((i + 0.3) * each * ET_FM_RATE);
		ppm = rs_pitch(x + from, len, hz);
		snprintf(name, sizeof(name), "the pitch, part %d", i + 1);
		et_measure(c, name, ppm, "ppm", want[i] - 0.5, want[i] + 0.5);
	}
	glitches = an_glitches(x + a, b - a, 44100, hz, 512, 20, NULL, 0);
	et_measure(c, "steps in the tone", glitches, "", 0, 4);
	free(x);
	free(rec);
	return ET_PASS;
}

// the first run of idle frames over min frames: its start, and its end
static int rs_gap(const short *rec, unsigned long a, unsigned long b,
		  unsigned long min, unsigned long *g0, unsigned long *g1)
{
	unsigned long i, run = 0;

	for (i = a; i < b; i++) {
		if (rec[2 * i] == ET_FM_IDLE && rec[2 * i + 1] == ET_FM_IDLE) {
			run++;
			continue;
		}
		if (run >= min) {
			*g0 = i - run;
			*g1 = i;
			return 1;
		}
		run = 0;
	}
	return 0;
}

// rs.pause: a pause under the filter goes on from the paused frame, in step
// within a FIFO, and the pitch stays. rs.xrun: a stall that runs the buffer
// out recovers, and the pitch is as it was. Both on the dig path, with a
// tone whose phase tells frames
enum et_result c_rs_pause(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	long ppb = 1000000;
	double hz = 997, ring = hz * (1 + es_step(ppb)), pa, pb, slip, *x;
	unsigned long n = 5 * 44100, nrec = 0, a = 0, b = 0, g0 = 0, g1 = 0;
	struct et_stream tune;
	struct an_fit one, two;
	short *rec = NULL;
	void *d = et_tone(&f, n, hz, hz, 20000);
	enum et_result r;

	memset(&tune, 0, sizeof(tune));
	tune.pause_at = 2;
	tune.pause_for = k->arg ? 0.5 : 0.3;
	tune.flags = k->arg ? ET_STALL : 0;
	tune.buffer = k->arg ? 4096 : 0;
	tune.period = k->arg ? 1024 : 0;
	if (rs_mode(c, RS_TRIM, ppb)) {
		free(d);
		return ET_ERROR;
	}
	r = rs_dig(c, &f, d, n, &tune, NULL, 0, &rec, &nrec);
	free(d);
	if (r != ET_PASS)
		return r;
	if (!rs_span(rec, nrec, &a, &b) ||
	    !rs_gap(rec, a, b, 2000, &g0, &g1) || g0 < a + 30000 ||
	    g1 + 30000 > b) {
		free(rec);
		et_fail(c, "no gap in the tone");
		return ET_ERROR;
	}
	et_measure(c, "xruns", tune.xruns, "", k->arg, k->arg);
	x = et_chan(&et_fm_fmt, rec, nrec, 0);
	if (an_fit_sine(x + g0 - 30000, 29000, 44100, ring, 0, &one) ||
	    an_fit_sine(x + g1 + 1000, 29000, 44100, ring, 0, &two)) {
		free(x);
		free(rec);
		et_fail(c, "no tone came back");
		return ET_ERROR;
	}
	et_measure(c, "the pitch after it", rs_pitch(x + g1 + 1000, 29000, hz),
		   "ppm", 1000 - 1, 1000 + 1);
	// the phase at the last frame before the gap and the first after it,
	// less what a frame turns the tone, in frames, within half a cycle
	pa = one.phase + 2 * M_PI * ring / 44100 * 30000;
	pb = two.phase - 2 * M_PI * ring / 44100 * 1000;
	slip = remainder(pb - pa, 2 * M_PI) / (2 * M_PI * ring / 44100);
	if (k->arg == 0)
		et_measure(c, "the frames it slips at the release", slip, "",
			   -RS_FIFO, RS_FIFO);
	free(x);
	free(rec);
	return ET_PASS;
}

// rs.drain: a drain under the filter plays what was written, every frame, by
// the count of ring frames that came back: a FIFO short at most, and at most
// the filter's reach long, since it rings on past a tone that stops dead
enum et_result c_rs_drain(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	long ppb = 1000000;
	unsigned long n = 3 * 44100, nrec = 0, a = 0, b = 0;
	short *rec = NULL;
	void *d = et_tone(&f, n, 997, 997, 20000);
	enum et_result r;
	int over = k->arg + 1;

	rs_ring_over = over;
	if (rs_mode(c, RS_TRIM, ppb)) {
		rs_ring_over = 1;
		free(d);
		return ET_ERROR;
	}
	r = rs_dig(c, &f, d, n, NULL, NULL, 0, &rec, &nrec);
	rs_ring_over = 1;
	free(d);
	if (r != ET_PASS)
		return r;
	if (!rs_span(rec, nrec, &a, &b)) {
		free(rec);
		et_fail(c, "no tone came back");
		return ET_ERROR;
	}
	et_measure(c, "the frames short of the stream",
		   over * n / (1 + es_step(ppb)) - (double)(b - a + 1), "",
		   -(RS_AHEAD * over) - 2, RS_FIFO + 2);
	free(rec);
	return ET_PASS;
}

// rs.runout: a client that stops writing, with no stop threshold and the core's
// silence behind the DMA: past the tone and the filter's tail, only idle frames,
// none of what the ring held before
enum et_result c_rs_runout(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	long ppb = 1000000;
	unsigned long n = 2 * 44100, nrec = 0, a = 0, b = 0, ghosts = 0, i, end;
	struct et_stream tune;
	short *rec = NULL;
	void *d = et_tone(&f, n, 997, 997, 20000);
	enum et_result r;
	int over = k->arg + 1;

	memset(&tune, 0, sizeof(tune));
	tune.flags = ET_RUNOUT;
	tune.buffer = 8192 / over;
	tune.period = 1024;
	rs_ring_over = over;
	if (rs_mode(c, RS_TRIM, ppb)) {
		rs_ring_over = 1;
		free(d);
		return ET_ERROR;
	}
	r = rs_dig(c, &f, d, n, &tune, NULL, 0, &rec, &nrec);
	rs_ring_over = 1;
	free(d);
	if (r != ET_PASS)
		return r;
	if (!rs_span(rec, nrec, &a, &b)) {
		free(rec);
		et_fail(c, "no tone came back");
		return ET_ERROR;
	}
	end = a + (unsigned long)(over * n / (1 + es_step(ppb))) + RS_AHEAD * over +
	      RS_FIFO;
	for (i = end; i < nrec; i++)
		ghosts += rec[2 * i] != ET_FM_IDLE || rec[2 * i + 1] != ET_FM_IDLE;
	et_measure(c, "the frames past the tone that aren't silent", ghosts, "", 0,
		   0);
	et_measure(c, "xruns", tune.xruns, "", 0, 0);
	if (ghosts)
		et_dump(c, "fm", &et_fm_fmt, rec, nrec);
	free(rec);
	return ET_PASS;
}

// a line through a stream's readings, past its first skip seconds, on the
// steered clock or the raw one: how far its rate is from nominal in ppm, and
// the rms left in us
static int rs_line(const struct et_stream *s, int use_raw, double skip,
		   double *ppm, double *rms)
{
	const struct et_stamp *p = s->stamps;
	unsigned long i, n = 0;
	double t0 = 0, f0 = 0, st = 0, sf = 0, tt = 0, tf = 0, sum = 0, slope;

	for (i = 0; i < s->stamped; i++) {
		double t = use_raw ? p[i].raw : p[i].t;

		if (!n) {
			if (t < (use_raw ? p[0].raw : p[0].t) + skip)
				continue;
			t0 = t;
			f0 = p[i].frame;
		}
		t -= t0;
		st += t;
		sf += p[i].frame - f0;
		tt += t * t;
		tf += t * (p[i].frame - f0);
		n++;
	}
	if (n < 8 || !(n * tt - st * st > 0))
		return -1;
	slope = (n * tf - st * sf) / (n * tt - st * st);
	for (i = 0; i < s->stamped; i++) {
		double t = (use_raw ? p[i].raw : p[i].t) - t0, e;

		if (t < 0)
			continue;
		e = (p[i].frame - f0 - sf / n) - slope * (t - st / n);
		sum += e * e;
	}
	*ppm = (slope / s->fmt.rate - 1) * 1e6;
	*rms = sqrt(sum / n) / s->fmt.rate * 1e6;
	return 0;
}

// a silent stream for seconds, with a reading at each period: its rate on
// the steered clock and on the raw one
struct rs_rate {
	double steered, raw, rms;
	unsigned int xruns;
};

static enum et_result rs_timed(struct et_ctx *c, unsigned int rate,
			       double seconds, double skip, struct rs_rate *o)
{
	struct et_fmt f = { ET_S16, 2, rate };
	unsigned long n = (unsigned long)(seconds * rate);
	struct et_stream s = et_play(ET_A2, &f, et_alloc_frames(&f, n), n, 0);
	enum et_result r = ET_PASS;

	s.period = rate / 8;
	s.buffer = rate / 2;
	s.max_stamps = (unsigned long)(seconds * 40) + 64;
	s.stamps = calloc(s.max_stamps, sizeof(*s.stamps));
	if (!s.stamps)
		abort();
	if (et_run(c, &s, 1)) {
		et_fail(c, "the stream: %s", strerror(-s.err));
		r = ET_ERROR;
	} else if (rs_line(&s, 0, skip, &o->steered, &o->rms) ||
		   rs_line(&s, 1, skip, &o->raw, &o->rms)) {
		et_fail(c, "too few readings of the stream");
		r = ET_ERROR;
	}
	o->xruns = s.xruns;
	free(s.stamps);
	free(s.data);
	return r;
}

// rs.clock: the stream's rate in the Off mode, the control, and in Clock, past
// its first 40 s, which are the fits' on a young line. In a minute a stream
// follows what the driver has fitted of the steered clock over a quarter of an
// hour, not the daemon's slews of that minute, which reach 1.7 ppm, so a minute
// is held only to about that. Half an hour averages the slews, and there it
// is held to the steered clock itself, which is real time
enum et_result c_rs_clock(struct et_ctx *c, const struct et_case *k)
{
	unsigned int rate = k->arg & 1 ? 48000 : 44100;
	int lng = k->arg & 2;
	double master = 0, steered = 0;
	struct rs_rate off, on;
	enum et_result r;

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (rs_mode(c, RS_OFF, 0))
		return ET_ERROR;
	r = rs_timed(c, rate, 20, 1, &off);
	if (r != ET_PASS)
		return r;
	et_measure(c, "Off, on the steered clock", off.steered, "ppm", -500, 500);
	if (rs_mode(c, RS_CLOCK, 0))
		return ET_ERROR;
	r = rs_timed(c, rate, lng ? 1800 : 100, 40, &on);
	if (r != ET_PASS)
		return r;
	if (et_master_ppm(c, &master, &steered)) {
		et_fail(c, "the driver has no reading of the clock");
		return ET_ERROR;
	}
	et_measure(c, "Clock, on the raw clock", on.raw, "ppm", 1, 0);
	et_measure(c, "the driver's reading of the steered clock", steered, "ppm",
		   1, 0);
	et_measure(c, "Clock, on the raw clock, less that", on.raw - steered,
		   "ppm", -2, 2);
	et_measure(c, "Clock, on the steered clock", on.steered, "ppm",
		   lng ? -0.1 : 1, lng ? 0.1 : 0);
	et_measure(c, "what's left of the readings", on.rms, "us", 0, 600);
	et_measure(c, "the master on the raw clock", master, "ppm", 1, 0);
	et_measure(c, "xruns", on.xruns, "", 0, 0);
	return ET_PASS;
}

// rs.rates: every family of the chip's clocks in the Clock mode, each rate on
// the slopes the fits have by then: the readings move in the FIFO's steps,
// which take up more time at the low rates
enum et_result c_rs_rates(struct et_ctx *c, const struct et_case *k)
{
	static const unsigned int rates[] = { 8000, 11025, 22050, 32000, 44100,
					      48000, 88200, 96000 };
	unsigned int i;
	double master = 0, steered = 0;

	(void)k;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (rs_mode(c, RS_CLOCK, 0))
		return ET_ERROR;
	for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
		struct rs_rate o;
		enum et_result r;
		char name[48];
		double lim = rates[i] < 16000 ? 6 : 2;

		r = rs_timed(c, rates[i], 12, 3, &o);
		if (r != ET_PASS)
			return r;
		if (et_master_ppm(c, &master, &steered)) {
			et_fail(c, "the driver has no reading of the clock");
			return ET_ERROR;
		}
		snprintf(name, sizeof(name), "at %u Hz, less the steered clock",
			 rates[i]);
		et_measure(c, name, o.raw - steered, "ppm", -lim, lim);
		snprintf(name, sizeof(name), "xruns at %u Hz", rates[i]);
		et_measure(c, name, o.xruns, "", 0, 0);
	}
	return ET_PASS;
}

// the most cycles a fill took in the last stream, from the proc file
static double rs_longest(struct et_ctx *c)
{
	char proc[4096];
	const char *p;
	unsigned int late, big, cyc;

	if (c->hal->proc(c->hal->ctx, proc, sizeof(proc)) < 0)
		return -1;
	p = strstr(proc, "Audio 2 ring: late refills");
	if (!p || sscanf(p, "Audio 2 ring: late refills %u, largest %u bytes, longest %u cycles",
			 &late, &big, &cyc) != 3)
		return -1;
	return cyc;
}

// a mode's cycles in the driver over 6 s of a stream, of the CPU's, in %
static int rs_share(struct et_ctx *c, const struct et_fmt *f, int mode, double hz,
		    double *share)
{
	unsigned long n = 6 * f->rate, irq0, tick0, irq1, tick1;
	double cyc0, cyc1;
	struct et_stream s;
	void *tone = et_tone(f, n, 441, 441, 300);
	int err;

	if (rs_mode(c, mode, 0) || et_cost(c, &cyc0, &irq0, &tick0) < 0) {
		free(tone);
		return -1;
	}
	s = et_play(ET_A2, f, tone, n, 0);
	s.period = rs_ring_over > 2 ? 1024 : 2048;
	s.buffer = rs_ring_over > 2 ? 4096 : 8192;
	err = et_run(c, &s, 1);
	free(tone);
	if (err || et_cost(c, &cyc1, &irq1, &tick1) < 0)
		return -1;
	*share = (cyc1 - cyc0) / (hz * n / f->rate) * 100;
	return 0;
}

// rs.bench: the filter's own cost on this CPU in user space, MMX and C. That
// a frame takes what the driver's fills take is cpu.rs's to show
enum et_result c_rs_bench(struct et_ctx *c, const struct et_case *k)
{
	double hz = c->hal->cpu_hz(c->hal->ctx);
	double st = sim_rs_cycles(2, 1), mo = sim_rs_cycles(1, 1);
	double cc = sim_rs_cycles(2, 0);

	(void)k;
	if (st <= 0)
		return ET_SKIP;
	et_measure(c, "MMX, stereo, cycles a frame", st, "", 1, 0);
	et_measure(c, "MMX, mono, cycles a frame", mo, "", 1, 0);
	et_measure(c, "C, stereo, cycles a frame", cc, "", 1, 0);
	if (hz > 0)
		et_measure(c, "MMX, stereo at 44.1 kHz, of the CPU",
			   st * 44100 / hz * 100, "%", 0, 6);
	et_measure(c, "the C code's cost over MMX's", cc / st, "x", 1, 0);
	return ET_PASS;
}

// cpu.over: the driver's share of the CPU with the ring at 2x or 3x, at the
// stream's own rate and with the Clock mode on top
enum et_result c_cpu_over(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	double hz = c->hal->cpu_hz(c->hal->ctx), share[2];

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (hz <= 0)
		return ET_SKIP;
	rs_ring_over = k->arg + 1;
	if (rs_share(c, &f, RS_OFF, hz, &share[0]) ||
	    rs_share(c, &f, RS_CLOCK, hz, &share[1])) {
		rs_ring_over = 1;
		return ET_ERROR;
	}
	rs_ring_over = 1;
	et_measure(c, "the driver's own cycles", share[0], "%", 0, 5 * (k->arg + 1));
	et_measure(c, "and in Clock", share[1], "%", 0, 5 * (k->arg + 1));
	return ET_PASS;
}

// cpu.rs: what the filter takes of the whole machine, from what an idle-
// priority thread finds left, so that the idle loop hides nothing, with the
// mode turned off and on under one stream every 4 s, since the count moves
// with what else the machine is up to; the longest a fill held interrupts
// off, and how late a real-time timer woke
#define CPU_TURNS 8
#define CPU_SLOTS (4 * 4 * CPU_TURNS)

enum et_result c_cpu_rs(struct et_ctx *c, const struct et_case *k)
{
	static const struct {
		unsigned int channels, rate;
	} t[] = { { 2, 44100 }, { 2, 48000 }, { 1, 44100 }, { 2, 96000 } };
	struct et_fmt f = { ET_S16, t[k->arg].channels, t[k->arg].rate };
	double hz = c->hal->cpu_hz(c->hal->ctx), spins = 0, late = 0, longest;
	double trace[CPU_SLOTS + 8], sum[2] = { 0, 0 };
	unsigned long n = (unsigned long)(4.5 * CPU_TURNS * f.rate);
	void *tone = et_tone(&f, n, 441, 441, 300);
	struct et_stream s[CPU_TURNS + 1];
	int i, cnt[2] = { 0, 0 };

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (rs_mode(c, RS_OFF, 0)) {
		free(tone);
		return ET_ERROR;
	}
	if (et_ctl_set_enum(c, RS_CTL_MODE, "Clock") < 0 ||
	    et_ctl_set_enum(c, RS_CTL_MODE, "Off") < 0) {
		free(tone);
		return ET_ERROR;
	}
	memset(s, 0, sizeof(s));
	s[0] = et_play(ET_A2, &f, tone, n, 0);
	s[0].period = 2048;
	s[0].buffer = 8192;
	for (i = 1; i <= CPU_TURNS; i++) {
		s[i].role = ET_CTL;
		s[i].cmd = RS_CTL_MODE;
		s[i].start = 4.0 * i;
		s[i].value = i & 1 ? RS_CLOCK : RS_OFF;
	}
	if (!c->hal->watch ||
	    c->hal->watch(c->hal->ctx, 1, NULL, NULL, NULL, 0) < 0) {
		free(tone);
		return ET_SKIP;
	}
	if (et_run(c, s, CPU_TURNS + 1)) {
		c->hal->watch(c->hal->ctx, 0, &spins, &late, trace, CPU_SLOTS);
		free(tone);
		return ET_ERROR;
	}
	c->hal->watch(c->hal->ctx, 0, &spins, &late, trace, CPU_SLOTS);
	free(tone);
	longest = rs_longest(c);
	// each window of 4 s less its first and last half second, and the
	// first, which holds the start
	for (i = 4; i + 4 <= CPU_SLOTS; i += 4) {
		int mode = (i / 16) & 1, j;

		if (i % 16 < 4 || i % 16 > 8 || i < 16)
			continue;
		for (j = 0; j < 4; j++) {
			sum[mode] += trace[i + j];
			cnt[mode]++;
		}
	}
	if (!cnt[0] || !cnt[1] || sum[0] <= 0)
		return ET_SKIP;
	sum[0] /= cnt[0];
	sum[1] /= cnt[1];
	et_measure(c, "the filter's part, from what's left of the machine",
		   (1 - sum[1] / sum[0]) * 100, "%", 1, 0);
	et_measure(c, "the latest timer", late, "us", 1, 0);
	if (hz > 0 && longest >= 0)
		et_measure(c, "the longest fill", longest / hz * 1e6, "us", 0, 350);
	if (hz > 0) {
		double share[2];

		if (rs_share(c, &f, RS_OFF, hz, &share[0]) ||
		    rs_share(c, &f, RS_CLOCK, hz, &share[1]))
			return ET_ERROR;
		et_measure(c, "the driver's own cycles, Off", share[0], "%", 0, 1);
		et_measure(c, "and in Clock", share[1], "%", 0,
			   f.rate > 48000 ? 12 : f.channels == 2 ? 6 : 4);
	}
	return ET_PASS;
}

// rs.analog: the same tone at the DAC's output, through the ADC, with the
// filter and without it, in turn, since the level moves between sessions:
// the filter must take nothing the DAC can show
enum et_result c_rs_analog(struct et_ctx *c, const struct et_case *k)
{
	static const double hz[] = { 997, 10000.9, 16000.1 };
	static const struct et_fmt rf = { ET_S16, 2, 48000 };
	static const struct et_fmt pf = { ET_S16, 2, 44100 };
	double f = hz[k->arg], sum[2][2], eps = es_step(30000);
	unsigned long pn = 3 * 44100, rn = 2 * 48000;
	void *tone = et_tone(&pf, pn, f, f, 12000);
	int pass, i;

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	memset(sum, 0, sizeof(sum));
	if (et_mix_rec_a2(c, 13) < 0) {
		free(tone);
		return ET_ERROR;
	}
	et_ctl_set(c, "Audio 2 Filter Playback Switch", 0, 1, 1);
	et_ctl_set(c, "Audio 1 Filter Switch", 0, 1, 1);
	for (pass = 0; pass < 4; pass++) {
		struct et_stream s[2];
		struct an_fit fit;
		double *x, want = f * (pass & 1 ? 1 + eps : 1);

		if (rs_mode(c, pass & 1 ? RS_TRIM : RS_OFF, pass & 1 ? 30000 : 0)) {
			free(tone);
			return ET_ERROR;
		}
		s[0] = et_play(ET_A2, &pf, tone, pn, 0);
		s[1] = et_rec(ET_A1C, &rf, rn, 0.6);
		if (et_run(c, s, 2)) {
			free(tone);
			free(s[1].data);
			return ET_ERROR;
		}
		rs_keep(c, f + pass, &rf, s[1].data, s[1].done);
		x = et_chan(&rf, s[1].data, s[1].done, 0);
		if (s[1].done < 48000 ||
		    an_fit_sine(x + 9600, s[1].done - 9600, 48000, want, 1,
				&fit)) {
			et_fail(c, "no tone in the recording");
			free(x);
			free(s[1].data);
			free(tone);
			return ET_ERROR;
		}
		sum[pass & 1][0] += 20 * log10(fit.amp);
		sum[pass & 1][1] += an_thdn(x + 9600, s[1].done - 9600, 48000, want,
					    100, 18000);
		free(x);
		free(s[1].data);
	}
	free(tone);
	for (i = 0; i < 2; i++) {
		sum[i][0] /= 2;
		sum[i][1] /= 2;
	}
	et_measure(c, "THD+N, Off", sum[0][1], "dB", 1, 0);
	et_measure(c, "THD+N, with the filter", sum[1][1], "dB", 1, 0);
	et_measure(c, "what the filter adds to THD+N", sum[1][1] - sum[0][1], "dB",
		   -1.5, 1.5);
	et_measure(c, "what it does to the level", sum[1][0] - sum[0][0], "dB",
		   -0.1, 0.1);
	return ET_PASS;
}

// --- the DAC's rate --------------------------------------------------------

#define RS_MPD_PERIOD 4096 // what MPD asks of the card: a buffer of four periods
#define RS_MPD_BUFFER 16384

// a tone through the DAC at rate, in dB: the sample's hold, and the sample
// before it, which each sample keeps some of (docs/AUDIO_PIPELINE.md)
static double dac_db(double hz, double rate)
{
	double w = 2 * M_PI * hz / rate, a = RS_DAC_LAST;
	double hold = sin(w / 2) / (w / 2);
	double last = sqrt(1 + 2 * a * cos(w) + a * a) / (1 + a);

	return 20 * log10(hold * last);
}

// 70h for a rate: bit 7 the 768 kHz clock and the rest 128 less its divisor,
// the two sets of rates. The chip's own clocks, whatever the driver does
static int dac_r70(unsigned int rate)
{
	unsigned int clock = 793800 % rate ? 768000 : 793800;

	return (clock == 768000 ? 0x80 : 0) | ((128 - clock / rate) & 0x7F);
}

static void *dac_tones(const struct et_fmt *f, unsigned long n,
		       const double *hz, int tones, double amp)
{
	short *d = malloc(n * 2 * sizeof(*d));
	unsigned long i;
	int k;

	if (!d)
		abort();
	for (i = 0; i < n; i++) {
		double v = 0;

		for (k = 0; k < tones; k++)
			v += amp * sin(2 * M_PI * hz[k] * i / f->rate);
		d[2 * i] = d[2 * i + 1] = (short)lrint(v);
	}
	return d;
}

// the level of a tone, or of where one would be, at its frequency on the
// second card's clock, which scale gives: its own against the chip's
static double dac_level(const double *x, long n, double hz, double scale)
{
	return an_dbfs(an_tone(x, n, 48000, hz * scale, NULL) / M_SQRT2);
}

#define DAC_PIECES 4 // of a measure, so that a stall spoils only one

static int dac_cmp(const void *a, const void *b)
{
	double u = *(const double *)a, v = *(const double *)b;

	return u < v ? -1 : u > v;
}

// the middle of DAC_PIECES values, which it sorts
static double dac_median(double *v)
{
	qsort(v, DAC_PIECES, sizeof(v[0]), dac_cmp);
	return (v[DAC_PIECES / 2 - 1] + v[DAC_PIECES / 2]) / 2;
}

static int dac_setup(struct et_ctx *c)
{
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Audio 2 Oversampling Playback Switch", 0, 0, 0);
	et_ctl_set(c, "Audio 2 Filter Playback Switch", 0, 0, 0);
	return et_mix_out(c, 0, 15);
}

// dac.over: that "Audio 2 Output Rate" clocks the DAC up, and not only fills
// the ring. The dig path can't show it, since it runs on the FM's clock. A
// stream below the second card's Nyquist keeps a tone's image in its band at
// 1x, so that it has to leave at 2x and 3x; 70h has to hold the divisor of the
// family's clock, whose bit 7 chooses it; and the treble's droop has to shrink
// by what the DAC's response at the new rate says
enum et_result c_dac_over(struct et_ctx *c, const struct et_case *k)
{
	static const struct {
		unsigned int rate;
		int over[3]; // what each setting gives, by its divisor
	} t[] = {
		{ 22050, { 1, 2, 3 } },
		{ 24000, { 1, 2, 2 } },
	};
	static const double hz[3] = { 1000, 5000, 9000 };
	const unsigned int rate = t[k->arg].rate;
	const struct et_fmt f = { ET_S16, 2, rate };
	unsigned long n = 3 * rate;
	double image = rate - hz[1], first = 0, img1 = 0, droop1 = 0;
	void *d;
	int r;

	if (dac_setup(c) < 0)
		return ET_ERROR;
	d = dac_tones(&f, n, hz, 3, 4000);
	for (r = 0; r < 3; r++) {
		int over = t[k->arg].over[r], r70 = -1;
		struct et_stream s[2];
		char name[64];
		double *x, tone, top, img, droop, scale;

		rs_ring_over = r + 1;
		if (rs_mode(c, RS_OFF, 0)) {
			rs_ring_over = 1;
			free(d);
			return ET_ERROR;
		}
		rs_ring_over = 1;
		s[0] = et_play(ET_A2, &f, d, n, 0);
		s[0].flags = ET_HOLD;
		s[1] = et_rec(ET_REC2, &et_rec2_fmt, 2 * 48000, 0.5);
		if (et_run(c, s, 2)) {
			free(s[1].data);
			free(d);
			return ET_ERROR;
		}
		if (c->hal->reg_read(c->hal->ctx, 0, 0x70, &r70) < 0)
			r70 = -1;
		snprintf(name, sizeof(name), "70h asked for %dx", r + 1);
		et_measure(c, name, r70, "", dac_r70(rate * over),
			   dac_r70(rate * over));
		x = et_chan(&et_rec2_fmt, s[1].data, s[1].done, 0);
		scale = an_pitch(x, s[1].done, 48000, hz[1]) / hz[1];
		if (!(scale > 0.99 && scale < 1.01)) {
			et_fail(c, "no tone came back at %dx", over);
			scale = 1;
		}
		first = dac_level(x, s[1].done, hz[0], scale);
		tone = dac_level(x, s[1].done, hz[1], scale);
		top = dac_level(x, s[1].done, hz[2], scale);
		img = dac_level(x, s[1].done, image, scale) - tone;
		droop = top - first;
		snprintf(name, sizeof(name), "the image at %.0f Hz, %dx", image,
			 over);
		et_measure(c, name, img, "dB", 1, 0);
		if (!r) {
			img1 = img;
			droop1 = droop;
			et_measure(c, "the image is there at 1x", img, "dB", -30, 0);
		} else {
			double want = dac_db(hz[2], rate * over) -
				      dac_db(hz[0], rate * over) -
				      (dac_db(hz[2], rate) - dac_db(hz[0], rate));

			snprintf(name, sizeof(name), "the image is gone, %dx",
				 over);
			et_measure(c, name, img1 - img, "dB", 20, 200);
			snprintf(name, sizeof(name), "9 kHz up from 1x, %dx", over);
			et_measure(c, name, droop - droop1, "dB", want - 0.4,
				   want + 0.4);
		}
		free(x);
		free(s[1].data);
	}
	free(d);
	return ET_PASS;
}

// dac.over.live: the output rate moved under a playing stream with the buffer
// MPD asks for, longer than the ring can hold at 3x. A tone at 15 kHz, which
// the hold takes the most from, has to step up and down with the DAC's rate,
// with no xrun and in tune, and 70h has to end on the last rate. A stop of the
// DAC spoils the piece it falls in, so each reading is the middle of four
enum et_result c_dac_over_live(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	static const int rates[4] = { 1, 3, 2, 1 };
	static const double each = 2.0, hz = 15000;
	double level[4], pitch[4];
	unsigned long n = (unsigned long)(4 * each * f.rate);
	struct et_stream s[5];
	const char *name = "Audio 2 Output Rate";
	void *d;
	double *x;
	int i, r70 = -1, lost = 0, stalled = 0;

	(void)k;
	if (dac_setup(c) < 0)
		return ET_ERROR;
	rs_ring_over = 1;
	if (rs_mode(c, RS_OFF, 0))
		return ET_ERROR;
	d = dac_tones(&f, n, &hz, 1, 8000);
	s[0] = et_play(ET_A2, &f, d, n, 0.3);
	s[0].period = RS_MPD_PERIOD;
	s[0].buffer = RS_MPD_BUFFER;
	s[0].flags = ET_HOLD;
	s[1] = et_rec(ET_REC2, &et_rec2_fmt, (unsigned long)(4 * each * 48000),
		      0.3);
	for (i = 1; i < 4; i++) {
		struct et_stream *m = &s[i + 1];

		memset(m, 0, sizeof(*m));
		m->role = ET_CTL;
		m->cmd = name;
		m->start = 0.3 + i * each;
		m->value = rates[i] - 1;
	}
	if (et_run(c, s, 5)) {
		free(s[1].data);
		free(d);
		return ET_ERROR;
	}
	et_measure(c, "the buffer it got", s[0].buffer_got, "frames",
		   RS_MPD_BUFFER, RS_MPD_BUFFER);
	et_measure(c, "xruns", s[0].xruns, "", 0, 0);
	for (i = 2; i < 5; i++)
		lost |= s[i].err;
	et_measure(c, "control writes refused", !!lost, "", 0, 0);
	if (c->hal->reg_read(c->hal->ctx, 0, 0x70, &r70) < 0)
		r70 = -1;
	et_measure(c, "70h at the end, 1x", r70, "", dac_r70(44100),
		   dac_r70(44100));
	x = et_chan(&et_rec2_fmt, s[1].data, s[1].done, 0);
	for (i = 0; i < 4; i++) {
		long from = (long)((i + 0.25) * each * 48000);
		long len = 48000 / DAC_PIECES;
		double v[DAC_PIECES], p[DAC_PIECES];
		char label[48];
		int j;

		if (from + DAC_PIECES * len > (long)s[1].done) {
			et_fail(c, "the recording ended early");
			break;
		}
		for (j = 0; j < DAC_PIECES; j++) {
			const double *y = x + from + j * len;

			p[j] = an_pitch(y, len, 48000, hz);
			v[j] = dac_level(y, len, p[j] > 0 ? p[j] : hz, 1);
		}
		pitch[i] = dac_median(p);
		level[i] = dac_median(v);
		for (j = 0; j < DAC_PIECES; j++)
			stalled += v[j] < level[i] - 1;
		snprintf(label, sizeof(label), "15 kHz at %dx, part %d", rates[i],
			 i + 1);
		et_measure(c, label, level[i], "dBFS", 1, 0);
		if (i) {
			double want = dac_db(hz, 44100.0 * rates[i]) -
				      dac_db(hz, 44100);

			snprintf(label, sizeof(label), "up from 1x, part %d", i + 1);
			et_measure(c, label, level[i] - level[0], "dB", want - 0.4,
				   want + 0.4);
			snprintf(label, sizeof(label), "pitch, part %d", i + 1);
			// a piece of a quarter second reads it to about 20 ppm
			et_measure(c, label,
				   (pitch[i] / pitch[0] - 1) * 1e6, "ppm", -30, 30);
		}
	}
	et_measure(c, "pieces with a stall of 16", stalled, "", 1, 0);
	free(x);
	free(s[1].data);
	free(d);
	return ET_PASS;
}

// a tone's THD+N through the second card, the median of its pieces, with
// how many are over 10 dB worse in *stalled, or with hz 0 what a silent
// stream leaves, in dB and dBFS. The DAC stops when the DMA can't reach it for
// as long as its FIFO lasts, and a tone that comes late fits no one sine
static int dac_take(struct et_ctx *c, const struct et_fmt *f, double hz,
		    double *out, int *stalled)
{
	unsigned long n = 3 * f->rate;
	void *d = dac_tones(f, n, &hz, hz > 0, 8000);
	struct et_stream s[2];
	double *x, v[DAC_PIECES];
	int i;

	s[0] = et_play(ET_A2, f, d, n, 0);
	s[0].flags = ET_HOLD;
	s[1] = et_rec(ET_REC2, &et_rec2_fmt, 2 * 48000, 0.5);
	if (et_run(c, s, 2)) {
		free(s[1].data);
		free(d);
		return -1;
	}
	x = et_chan(&et_rec2_fmt, s[1].data, s[1].done, 0);
	if (hz > 0) {
		long len = s[1].done / DAC_PIECES;

		for (i = 0; i < DAC_PIECES; i++) {
			double f0 = an_pitch(x + i * len, len, 48000, hz);

			v[i] = an_thdn(x + i * len, len, 48000, f0 > 0 ? f0 : hz, 100,
				       20000);
		}
		*out = dac_median(v);
		for (i = 0; i < DAC_PIECES; i++)
			*stalled += v[i] > *out + 10;
	} else {
		*out = an_dbfs(an_rms(x, s[1].done, 1));
	}
	if (s[0].xruns || s[1].xruns)
		et_note(c, "%.0f Hz: xruns %u playing, %u recording", hz, s[0].xruns,
			s[1].xruns);
	free(x);
	free(s[1].data);
	free(d);
	return 0;
}

// dac.over.quality: what the higher rates do to the sound. The second card's
// own distortion sets the numbers, so they count against the same chain at 1x:
// each rate has to be as clean as that, within 1 dB. The noise moves from take
// to take at any rate, so it is the quietest of three takes. A stall doesn't
// count against the rate here: it is the bus's, and it is reported apart
enum et_result c_dac_quality(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	static const struct {
		const char *name;
		double hz;
		int takes;
	} t[3] = {
		{ "THD+N at 997 Hz", 997, 1 },
		{ "THD+N at 10 kHz", 10000, 1 },
		{ "noise of a silent stream", 0, 3 },
	};
	double base[3] = { 0, 0, 0 };
	int r, i, take;

	(void)k;
	if (dac_setup(c) < 0)
		return ET_ERROR;
	for (r = 0; r < 3; r++) {
		char name[64];
		int stalled = 0;

		rs_ring_over = r + 1;
		if (rs_mode(c, RS_OFF, 0)) {
			rs_ring_over = 1;
			return ET_ERROR;
		}
		rs_ring_over = 1;
		for (i = 0; i < 3; i++) {
			double v = 200;

			for (take = 0; take < t[i].takes; take++) {
				double w;

				if (dac_take(c, &f, t[i].hz, &w, &stalled))
					return ET_ERROR;
				if (w < v)
					v = w;
			}
			snprintf(name, sizeof(name), "%s, %dx", t[i].name, r + 1);
			et_measure(c, name, v, i == 2 ? "dBFS" : "dB", 1, 0);
			if (!r) {
				base[i] = v;
			} else {
				snprintf(name, sizeof(name), "%s, %dx against 1x",
					 t[i].name, r + 1);
				et_measure(c, name, v - base[i], "dB", -40, 1);
			}
		}
		snprintf(name, sizeof(name), "pieces with a stall of 8, %dx", r + 1);
		et_measure(c, name, stalled, "", 1, 0);
	}
	return ET_PASS;
}
