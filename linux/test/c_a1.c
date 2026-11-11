// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The a1rec and a1play groups. Audio 1 records Audio 2's tones through the
 * record mixer (source 5, 69h) with the master muted; Audio 1's playback
 * is heard by the second card through the cable, which carries the left
 * output, with 441 Hz on the left and 661.5 Hz on the right.
 *
 * Notes:
 *
 * The start race shows in both: a 16-bit stream a byte off is noise, and
 * an 8-bit stereo stream a byte off has its sides swapped. The Armada's
 * second card, a USB microphone input, sets its own gain through the first
 * second of a tone and runs its clock thousands of ppm off, so the cable's
 * cases measure late in each tone and allow its pitch 6000 ppm.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cases.h"
#include "hal.h"

#define AMP 6000.0 // -14.7 dBFS: the output stays below -20 dB

static const struct et_fmt fmts[] = {
	{ ET_S16, 2, 44100 }, { ET_S16, 1, 44100 }, { ET_U8, 2, 22050 },
	{ ET_U8, 1, 22050 },  { ET_S8, 2, 22050 },  { ET_U16, 2, 44100 },
};

static const unsigned int rates[] = { 8000,  11025, 16000, 22050,
				      32000, 44100, 48000 };

static int eight(const struct et_fmt *f)
{
	return f->format == ET_U8 || f->format == ET_S8;
}

// the tones of a recording at rate: 441 Hz and 661.5 Hz on a channel, its
// THD+N against 441 Hz, its pitch and its glitches
struct tones {
	double a441, a661, thdn, hz;
	int glitches;
};

// THD+N over lo Hz, to the band's top
static void measure(const double *x, long n, double rate, double lo,
		    struct tones *t)
{
	struct an_fit fit;

	t->a441 = an_dbfs(an_tone(x, n, rate, ET_HZ, NULL) / M_SQRT2);
	t->a661 = an_dbfs(an_tone(x, n, rate, ET_HZ_R, NULL) / M_SQRT2);
	t->thdn = an_thdn(x, n, rate, ET_HZ, lo, rate / 2 < 20000 ? rate / 2 :
								 20000);
	t->hz = an_fit_sine(x, n, rate, ET_HZ, 1, &fit) ? 0 : fit.f;
	t->glitches = an_glitches(x, n, rate, ET_HZ, (long)(rate / 100), 20,
				  NULL, 0);
}

// --- a1rec -------------------------------------------------------------------

static void a1rec_check(struct et_ctx *c, const struct et_fmt *f,
			const struct tones *t, double ppm)
{
	et_measure(c, "441 Hz", t[0].a441, "dBFS", -40, -1);
	if (f->channels == 2) {
		et_measure(c, "661.5 Hz on the left", t[0].a661 - t[0].a441,
			   "dB", -200, -20);
		et_measure(c, "441 Hz on the right", t[1].a441 - t[1].a661,
			   "dB", -200, -20);
	}
	et_measure(c, "THD+N, left", t[0].thdn, "dB", -200,
		   eight(f) ? -25 : -30);
	et_measure(c, "pitch", (t[0].hz / ET_HZ - 1) * 1e6, "ppm", -ppm, ppm);
	et_measure(c, "glitches", t[0].glitches, "", 0, 0);
}

// a recording of Audio 2's tones, checked before its dump, so that a
// failure keeps its recording
static enum et_result a1rec_run(struct et_ctx *c, const struct et_fmt *cf,
				double skip, double secs, double ppm)
{
	static const struct et_fmt pf = { ET_S16, 2, 44100 };
	unsigned long pn = (unsigned long)((secs + skip + 0.6) * 44100);
	unsigned long cn = (unsigned long)((secs + skip) * cf->rate);
	struct et_stream s[2];
	struct tones t[2];
	unsigned int ch;
	double *x;

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (et_mix_rec_a2(c, 13) < 0) {
		et_fail(c, "the record mixer refused its setup");
		return ET_ERROR;
	}
	// a mono recording sums the sides, so both carry 441 Hz then
	s[0] = et_play(ET_A2, &pf,
		       et_tone(&pf, pn, ET_HZ, cf->channels > 1 ? ET_HZ_R : ET_HZ,
			       AMP),
		       pn, 0);
	s[1] = et_rec(ET_A1C, cf, cn, 0.3);
	if (et_run(c, s, 2)) {
		et_fail(c, "the streams failed");
		free(s[0].data);
		free(s[1].data);
		return ET_ERROR;
	}
	for (ch = 0; ch < cf->channels; ch++) {
		long from = (long)(skip * cf->rate);

		x = et_chan(cf, s[1].data, s[1].done, ch);
		measure(x + from, (long)s[1].done - from, cf->rate, 100, &t[ch]);
		free(x);
	}
	a1rec_check(c, cf, t, ppm);
	if (c->fails)
		et_dump(c, "a1c", cf, s[1].data, s[1].done);
	free(s[0].data);
	free(s[1].data);
	return ET_PASS;
}

// a1rec.fmt.*: each format at 44.1 or 22.05 kHz
enum et_result c_a1rec_fmt(struct et_ctx *c, const struct et_case *k)
{
	return a1rec_run(c, &fmts[k->arg], 0.2, 1.5, 100);
}

// a1rec.rate.*: 16-bit stereo at each rate; 48 kHz runs from the other
// clock, measured 250 ppm from the 793.8 kHz one
enum et_result c_a1rec_rate(struct et_ctx *c, const struct et_case *k)
{
	struct et_fmt f = { ET_S16, 2, rates[k->arg] };

	return a1rec_run(c, &f, 0.2, 1.5, f.rate % 8000 ? 100 : 600);
}

// the mean of each side over the last second of a recording of secs
// seconds, past those before it, in which the ADC's input settles
static int rec_dc(struct et_ctx *c, double dc[2], unsigned int secs)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	struct et_stream s = et_rec(ET_A1C, &f, secs * f.rate, 0);
	unsigned int ch;
	long i, n;

	if (et_run(c, &s, 1)) {
		et_fail(c, "the recording failed");
		free(s.data);
		return -1;
	}
	n = (long)s.done - f.rate;
	for (ch = 0; ch < 2; ch++) {
		double *x = et_chan(&f, s.data, s.done, ch), sum = 0;

		for (i = 0; i < (long)f.rate; i++)
			sum += x[n + i];
		dc[ch] = sum / f.rate;
		free(x);
	}
	free(s.data);
	return 0;
}

// a1rec.dc: the ADC's own offset, from a record source that nothing drives
// at the lowest level, as ESSDC.EXE measures it. It doesn't depend on the
// source or the level, so it arises after them. Over half a step of the
// offset registers, "Capture DC Offset" has something to correct. A step of
// that control moves it by 64 (DS p.71). The input settles first from what
// came before: after a1rec.start's tone on the left, in a full run on the
// Armada, the left side read 19 low at 1.5 s and 3.4 low 2.4 s later, a
// time constant of about 1.4 s, so the first recording takes 5 s
enum et_result c_a1rec_dc(struct et_ctx *c, const struct et_case *k)
{
	double dc[2], moved[2];

	(void)k;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set_enum(c, "Capture Source", "Line");
	et_ctl_set(c, "Capture Volume", 0, 0, 0);
	et_ctl_set(c, "Capture DC Offset", 0, 0, 0);
	if (rec_dc(c, dc, 5))
		return ET_ERROR;
	et_measure(c, "the ADC's offset, left", dc[0], "", -32, 32);
	et_measure(c, "the ADC's offset, right", dc[1], "", -32, 32);
	et_ctl_set(c, "Capture DC Offset", 0, 1, 1);
	if (rec_dc(c, moved, 2))
		return ET_ERROR;
	et_measure(c, "a step of Capture DC Offset, left", moved[0] - dc[0], "",
		   58, 70);
	et_measure(c, "a step of Capture DC Offset, right", moved[1] - dc[1], "",
		   58, 70);
	return ET_PASS;
}

// a recording's start against its tone, fitted over 50-250 ms: the
// frames at the start more than 5 % of the tone off it, and the offset
// over the first 5 ms, which the ADC's input carries while it settles
static long stale(const struct et_fmt *f, const void *buf, unsigned long n,
		  double *first, double *offset)
{
	long a = f->rate / 20, len = f->rate / 5, i, m = f->rate / 200;
	double *x = et_chan(f, buf, n, 0);
	struct an_fit fit;
	double sum = 0;

	if ((long)n < a + len || an_fit_sine(x + a, len, f->rate, ET_HZ, 1, &fit)) {
		free(x);
		return -1;
	}
	*first = x[0];
	for (i = 0; i < m; i++) {
		double t = (double)(i - a) / f->rate;

		sum += x[i] - (fit.dc + fit.amp * sin(2 * M_PI * fit.f * t +
						     fit.phase));
	}
	*offset = sum / m;
	for (i = 0; i < a; i++) {
		double t = (double)(i - a) / f->rate;
		double v = fit.dc + fit.amp * sin(2 * M_PI * fit.f * t + fit.phase);

		if (fabs(x[i] - v) < 0.05 * fit.amp)
			break;
	}
	free(x);
	return i;
}

// ten short recordings, each after a pause long enough for the ADC's input
// to move away (as between two programs' recordings); the worst start
static enum et_result starts(struct et_ctx *c, const struct et_fmt *f,
			     long *most, double *worst)
{
	static const struct et_fmt pf = { ET_S16, 2, 44100 };
	unsigned long pn = 44100, cn = f->rate * 3 / 10;
	double first = 0, offset = 0;
	long n;
	int i;

	*most = 0;
	*worst = 0;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (et_mix_rec_a2(c, 13) < 0)
		return ET_ERROR;
	for (i = 0; i < 10; i++) {
		struct et_stream s[2];

		// a mono recording sums the sides: the same tone on both
		s[0] = et_play(ET_A2, &pf,
			       et_tone(&pf, pn, ET_HZ,
				       f->channels > 1 ? ET_HZ_R : ET_HZ, AMP),
			       pn, 0);
		s[1] = et_rec(ET_A1C, f, cn, 0.3);
		if (et_run(c, s, 2)) {
			free(s[0].data);
			free(s[1].data);
			return ET_ERROR;
		}
		n = stale(f, s[1].data, s[1].done, &first, &offset);
		if (n > *most || fabs(offset) > fabs(*worst)) {
			et_note(c, "run %d: %ld frames off the tone, the first %.0f, an offset of %.0f",
				i, n, first, offset);
			et_dump(c, "a1c", f, s[1].data, s[1].done);
		}
		*most = n > *most ? n : *most;
		if (fabs(offset) > fabs(*worst))
			*worst = offset;
		free(s[0].data);
		free(s[1].data);
		c->hal->sleep(c->hal->ctx, 0.6);
	}
	return ET_PASS;
}

// a1rec.start.*: each recording the tone from its first frame
enum et_result c_a1rec_start(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt fmts_start[] = {
		{ ET_S16, 2, 44100 }, { ET_S16, 1, 44100 }, { ET_U8, 2, 22050 },
		{ ET_S16, 2, 8000 },  { ET_S16, 2, 48000 },
	};
	double worst;
	long most;
	enum et_result r = starts(c, &fmts_start[k->arg], &most, &worst);

	if (r != ET_PASS)
		return r;
	et_measure(c, "frames at the start off the tone", most, "", 0, 0);
	et_measure(c, "offset over the first 5 ms", worst, "", -330, 330);
	return ET_PASS;
}

// a1rec.settle0: the same without the settle, as ESS's drivers start: the
// input's offset decaying through the first 100 ms is KNOWN
enum et_result c_a1rec_settle0(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	double worst;
	long most;
	enum et_result r;

	(void)k;
	if (et_param_set(c, "a1c_settle_ms", 0) < 0)
		return ET_ERROR;
	r = starts(c, &f, &most, &worst);
	if (r != ET_PASS)
		return r;
	et_measure(c, "frames at the start off the tone", most, "", 1, 0);
	et_measure(c, "offset over the first 5 ms", worst, "", 1, 0);
	if (most || fabs(worst) > 330) {
		snprintf(c->why, sizeof(c->why),
			 "without the settle a recording starts %.0f off, as with ESS's drivers",
			 worst);
		return ET_KNOWN;
	}
	return ET_PASS;
}

// the tone's share of what a recording holds, in dB: 0 for the tone alone,
// some 30 dB less for a 16-bit stream a byte off, which is noise
static double tone_share(const double *x, long n, double rate)
{
	double total = an_rms(x, n, 1);

	return total > 0 ? 20 * log10(an_tone(x, n, rate, ET_HZ, NULL) /
				      M_SQRT2 / total) :
			   -200;
}

// a1rec.restart: a recording begun again on its open stream, dropped,
// prepared and started, as a program does that stops a recording and starts
// it without closing it: the tone after the restart as in step as before
// it. With its channel unmasked before the start, the first byte of the
// start's first DMA request was at risk, and the second run of es1869scope
// -C came back a byte off now and then (2026-11-10)
enum et_result c_a1rec_restart(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt pf = { ET_S16, 2, 44100 };
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	// the gap between the drop and the start: at once, as es1869scope
	// begins its second run, and longer, the FIFO full meanwhile
	static const double gaps[] = { 0, 0.002, 0.01, 0.05 };
	unsigned long pn = 2 * 44100, cn = 44100;
	long at = (long)(0.4 * f.rate), len = f.rate / 5;
	double worst = 0;
	int i;

	(void)k;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (et_mix_rec_a2(c, 13) < 0)
		return ET_ERROR;
	for (i = 0; i < 8; i++) {
		struct et_stream s[2];
		double *x, before, after;

		s[0] = et_play(ET_A2, &pf,
			       et_tone(&pf, pn, ET_HZ, ET_HZ_R, AMP), pn, 0);
		s[1] = et_rec(ET_A1C, &f, cn, 0.3);
		s[1].flags |= ET_RESTART;
		s[1].pause_at = 0.4;
		s[1].pause_for = gaps[i % 4];
		if (et_run(c, s, 2)) {
			free(s[0].data);
			free(s[1].data);
			return ET_ERROR;
		}
		if ((long)s[1].done < at + (long)f.rate / 20 + len) {
			free(s[0].data);
			free(s[1].data);
			et_fail(c, "the recording ended at its restart");
			return ET_FAIL;
		}
		x = et_chan(&f, s[1].data, s[1].done, 0);
		// before the restart, and after it once its FIFO has passed
		before = tone_share(x + at - len, len, f.rate);
		after = tone_share(x + at + (long)f.rate / 20, len, f.rate);
		if (fabs(after - before) > fabs(worst)) {
			worst = after - before;
			et_note(c,
				"run %d, a gap of %g ms: the tone's share %.1f dB before the restart, %.1f after",
				i, 1000 * gaps[i % 4], before, after);
			et_dump(c, "a1c", &f, s[1].data, s[1].done);
		}
		free(x);
		free(s[0].data);
		free(s[1].data);
	}
	et_measure(c, "the tone's share after a restart, less before", worst,
		   "dB", -1, 1);
	return ET_PASS;
}

// --- a1play ------------------------------------------------------------------

static enum et_result a1play_run(struct et_ctx *c, const struct et_fmt *f,
				 struct tones *t)
{
	// the second card's own gain control settles through the first second
	// of a tone, so the tone lasts 2.5 s and the last 1.2 s are measured
	unsigned long pn = (unsigned long)(2.5 * f->rate);
	unsigned long rn = (unsigned long)(3.6 * 48000);
	struct et_stream s[2];
	double *x, noise;
	long on;

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_mix_out(c, 14, 0);
	s[0] = et_rec(ET_REC2, &et_rec2_fmt, rn, 0);
	s[1] = et_play(ET_A1P, f,
		       et_tone(f, pn, ET_HZ, f->channels > 1 ? ET_HZ_R : 0,
			       AMP),
		       pn, 0.4);
	if (et_run(c, s, 2)) {
		et_fail(c, "the streams failed");
		free(s[0].data);
		free(s[1].data);
		return ET_ERROR;
	}
	x = et_chan(&et_rec2_fmt, s[0].data, s[0].done, 0);
	// the second card's capture can start late enough that its first
	// samples hold the tone's start, and the onset then came late
	noise = et_noise_floor(x, s[0].done);
	on = an_onset(x, s[0].done, 8 * noise + 30);
	if (on < 0 || on + 115200 > (long)s[0].done) {
		et_fail(c, "the second card heard nothing");
		et_dump(c, "rec2", &et_rec2_fmt, s[0].data, s[0].done);
		free(x);
		free(s[0].data);
		free(s[1].data);
		return ET_PASS;
	}
	// THD+N over 600 Hz: the second card's clock wanders while it
	// records, which leaves sidebands within 20 Hz of the tone, and a
	// stream a byte off is broadband
	measure(x + on + 52800, 57600, 48000, 600, t);
	if (c->fails || t->thdn > (eight(f) ? -12 : -15))
		et_dump(c, "rec2", &et_rec2_fmt, s[0].data, s[0].done);
	free(x);
	free(s[0].data);
	free(s[1].data);
	return ET_PASS;
}

static void a1play_check(struct et_ctx *c, const struct et_fmt *f,
			 const struct tones *t)
{
	et_measure(c, "441 Hz", t->a441, "dBFS", -60, -1);
	if (f->channels == 2)
		et_measure(c, "661.5 Hz, the right side", t->a661 - t->a441,
			   "dB", -200, -20);
	// the cable ends in a USB microphone input: a byte-off 16-bit
	// stream reads near 0 dB there, an in-step one -18 to -25 dB
	et_measure(c, "THD+N", t->thdn, "dB", -200, eight(f) ? -12 : -15);
	et_measure(c, "pitch, against the second card",
		   (t->hz / ET_HZ - 1) * 1e6, "ppm", -6000, 6000);
}

// a run, and up to two more while one hears more noise than the cable
// allows, as the second card's gain control settles
static enum et_result a1play_settled(struct et_ctx *c, const struct et_fmt *f,
				     struct tones *t)
{
	enum et_result r = a1play_run(c, f, t);
	int tries;

	for (tries = 0; tries < 2 && r == ET_PASS && !c->fails &&
			t->thdn > (eight(f) ? -12 : -15);
	     tries++) {
		et_note(c, "THD+N %.1f dB: again, the second card settled",
			t->thdn);
		r = a1play_run(c, f, t);
	}
	return r;
}

// a1play.fmt.*: each format through the cable
enum et_result c_a1play_fmt(struct et_ctx *c, const struct et_case *k)
{
	struct tones t;
	enum et_result r = a1play_settled(c, &fmts[k->arg], &t);

	if (r == ET_PASS && !c->fails)
		a1play_check(c, &fmts[k->arg], &t);
	return r;
}

// a1play.rate.*: 16-bit stereo at each rate
enum et_result c_a1play_rate(struct et_ctx *c, const struct et_case *k)
{
	struct et_fmt f = { ET_S16, 2, rates[k->arg] };
	struct tones t;
	enum et_result r = a1play_settled(c, &f, &t);

	if (r == ET_PASS && !c->fails)
		a1play_check(c, &f, &t);
	return r;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return x < y ? -1 : x > y;
}

// a1play.drain: a second of tone at the lowest byte rate, whose last frame
// ends a period, drained: the chip's FIFO holds 32 ms of it then, which a
// stop at the DMA's end would drop. Heard on the second card, at half the
// tone's level, since its capture starts with a thump of its own. With a
// pause of 200 ms before the drain (a1play.pausedrain), the buffer that
// was queued still plays: the core takes a position short of the period's
// end for an interrupt acknowledged twice, right after a release
enum et_result c_a1play_drain(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_U8, 1, 8000 };
	double len = k->arg ? 1200 : 1000;
	unsigned long pn = 8000, rn = 2 * 48000;
	struct et_stream s[2];
	double *x, *sorted, level;
	long on, off, i;

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_mix_out(c, 14, 0);
	s[0] = et_rec(ET_REC2, &et_rec2_fmt, rn, 0);
	s[1] = et_play(ET_A1P, &f, et_tone(&f, pn, ET_HZ, 0, AMP), pn, 0.4);
	s[1].period = 1000;
	s[1].buffer = 4000;
	s[1].flags = k->arg;
	if (et_run(c, s, 2)) {
		et_fail(c, "the streams failed");
		free(s[0].data);
		free(s[1].data);
		return ET_ERROR;
	}
	x = et_chan(&et_rec2_fmt, s[0].data, s[0].done, 0);
	// the tone's level, past a click that its largest sample might be
	sorted = malloc(s[0].done * sizeof(*sorted));
	if (!sorted) {
		free(x);
		free(s[0].data);
		free(s[1].data);
		return ET_ERROR;
	}
	for (i = 0; i < (long)s[0].done; i++)
		sorted[i] = fabs(x[i]);
	qsort(sorted, s[0].done, sizeof(*sorted), cmp_double);
	level = 0.6 * sorted[s[0].done * 99 / 100];
	free(sorted);
	for (on = 0; on < (long)s[0].done && fabs(x[on]) < level; on++)
		;
	for (off = (long)s[0].done - 1; off > on; off--)
		if (fabs(x[off]) > level)
			break;
	if (level < 100) {
		et_fail(c, "the second card heard nothing");
	} else {
		et_measure(c, "the tone's length", (off - on) * 1000.0 / 48000,
			   "ms", len - 15, len + 30);
	}
	if (c->fails)
		et_dump(c, "rec2", &et_rec2_fmt, s[0].data, s[0].done);
	free(x);
	free(s[0].data);
	free(s[1].data);
	return ET_PASS;
}

// a1play.click: what a pause's release does to the output, heard by the second
// card: the largest sample in the 5 ms after the pause's silence ends, against
// the tone's peak, six times, since the pause falls anywhere in the tone
enum et_result c_a1play_click(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	unsigned long pn = 2 * 44100, rn = 3 * 48000;
	double worst = 0, sum = 0;
	void *tone = et_tone(&f, pn, ET_HZ, ET_HZ, AMP);
	int run;

	(void)k;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_mix_out(c, 14, 0);
	for (run = 0; run < 6; run++) {
		struct et_stream s[2];
		double *x, peak = 0, top = 0, env = 0;
		long i, quiet = -1, end = -1;

		s[0] = et_rec(ET_REC2, &et_rec2_fmt, rn, 0);
		s[1] = et_play(ET_A1P, &f, tone, pn, 0.3);
		s[1].period = 512;
		s[1].buffer = 2048;
		s[1].pause_at = 1.0 + 0.003 * run;
		s[1].pause_for = 0.2;
		if (et_run(c, s, 2)) {
			free(s[0].data);
			free(tone);
			return ET_ERROR;
		}
		x = et_chan(&et_rec2_fmt, s[0].data, s[0].done, 0);
		for (i = 24000; i < 48000; i++)
			if (fabs(x[i]) > peak)
				peak = fabs(x[i]);
		// the first stretch of 40 ms under an eighth of the tone
		for (i = 0; i < (long)s[0].done; i++) {
			env = fabs(x[i]) > peak / 8 ? 0 : env + 1;
			if (env > 1920 && quiet < 0)
				quiet = i;
			if (quiet >= 0 && env == 0) {
				end = i;
				break;
			}
		}
		if (end < 0 || peak < 100) {
			et_fail(c, "no pause in what the second card heard");
			et_dump(c, "rec2", &et_rec2_fmt, s[0].data, s[0].done);
			free(x);
			free(s[0].data);
			free(tone);
			return ET_FAIL;
		}
		for (i = end; i < end + 240 && i < (long)s[0].done; i++)
			if (fabs(x[i]) > top)
				top = fabs(x[i]);
		if (top / peak > worst) {
			worst = top / peak;
			if (getenv("ET_KEEP"))
				et_dump(c, "rec2", &et_rec2_fmt, s[0].data, s[0].done);
		}
		sum += top / peak;
		free(x);
		free(s[0].data);
	}
	free(tone);
	et_measure(c, "the worst click, of the tone's peak", worst, "", 1, 0);
	et_measure(c, "the mean", sum / 6, "", 1, 0);
	if (worst > 1.2 && !c->fails) {
		snprintf(c->why, sizeof(c->why),
			 "a pause's release clicks, up to %.1f times the tone",
			 worst);
		return ET_KNOWN;
	}
	return ET_PASS;
}

// a1play.start: ten starts each of 16-bit and 8-bit stereo, in step: the
// 16-bit ones clean, the 8-bit ones with the sides where they belong
enum et_result c_a1play_start(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f16 = { ET_S16, 2, 44100 };
	static const struct et_fmt f8 = { ET_U8, 2, 22050 };
	int i, ok16 = 0, ok8 = 0;
	struct tones t;

	(void)k;
	for (i = 0; i < 20; i++) {
		const struct et_fmt *f = i & 1 ? &f8 : &f16;
		int before = c->fails;

		if (a1play_run(c, f, &t) != ET_PASS)
			return ET_ERROR;
		if (c->fails != before)
			continue;
		if (i & 1)
			ok8 += t.a661 < t.a441 - 20;
		else
			ok16 += t.thdn < -15;
	}
	et_measure(c, "16-bit starts in step", ok16, "of 10", 10, 10);
	et_measure(c, "8-bit starts with their sides", ok8, "of 10", 10, 10);
	return ET_PASS;
}
