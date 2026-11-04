// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The dig group: Audio 2 slaved to the FM (71h bit 0) and the music DAC
 * recorded by PCM 1, which holds what Audio 2 played sample for sample, its
 * sides swapped and the FM's idle level added. Every byte of the DMA 5
 * ring's path is checked: formats, starts, pauses, mmap and dmix.
 *
 * Notes:
 *
 * Slaved to the FM, Audio 2 plays one frame a music DAC frame, at 49716 Hz
 * whatever rate the stream has, so the recording runs that long.
 *
 * The chip's interrupt comes when the DMA has fetched a period's last byte,
 * up to its FIFO of 64 bytes before that byte plays, and a stop clears the
 * FIFO. So a drain loses the frames still in it, and a pause's release,
 * which starts afresh, can only estimate where to go on: both within a
 * FIFO are KNOWN, anything more a FAIL.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "an.h"
#include "cases.h"

static int idle(const short *r)
{
	return r[0] == ET_FM_IDLE && r[1] == ET_FM_IDLE;
}

static int same(const short *r, const int *e)
{
	return r[0] == e[0] && r[1] == e[1];
}

struct dig_cmp {
	unsigned long first; // the pattern frame the recording begins with
	unsigned long matched; // frames of the pattern heard, in order
	long gap; // idle frames between them
	long jump; // where a pause went on, against where it stopped
	long bad; // the first frame that is neither, or -1
};

// after a gap, the pattern frame within a FIFO of j that the recording
// goes on with, as an offset from j, or a value past the FIFO
static long resume(const struct et_fmt *f, const void *pat,
		   unsigned long frames, const short *r, unsigned long j,
		   long fifo)
{
	long d;
	int e[2];

	for (d = -fifo; d <= fifo; d++) {
		if ((long)j + d < 0 || j + d >= frames)
			continue;
		et_dig_frame(f, pat, j + d, e);
		if (same(r, e))
			return d;
	}
	return fifo + 1;
}

// the recording against the pattern: every frame in order, idle frames
// before, after and at a pause, and a pause's release within a FIFO
static void compare(const struct et_fmt *f, const void *pat,
		    unsigned long frames, const short *rec, unsigned long nrec,
		    long fifo, unsigned long skip, struct dig_cmp *m)
{
	unsigned long i = 0, j = 0;
	long idles = 0; // idle frames since the last match: a gap once
			// the pattern goes on after them
	int e[2], after_gap = 0;

	memset(m, 0, sizeof(*m));
	m->bad = -1;
	while (i < nrec && idle(rec + 2 * i))
		i++;
	// a stream allowed to lose its first frames: where it begins
	for (j = 0; i < nrec && j <= skip && j < frames; j++) {
		et_dig_frame(f, pat, j, e);
		if (same(rec + 2 * i, e))
			break;
	}
	if (j > skip || j == frames)
		j = 0;
	m->first = j;
	for (; i < nrec && j < frames; i++) {
		et_dig_frame(f, pat, j, e);
		if (same(rec + 2 * i, e)) {
			j++;
			m->gap += idles;
			idles = 0;
			after_gap = 0;
		} else if (idle(rec + 2 * i)) {
			idles++;
			after_gap = 1;
		} else if (after_gap && !m->jump &&
			   labs(resume(f, pat, frames, rec + 2 * i, j, fifo)) <=
				   fifo) {
			m->jump = resume(f, pat, frames, rec + 2 * i, j, fifo);
			j += m->jump + 1;
			m->gap += idles;
			idles = 0;
			after_gap = 0;
		} else {
			m->bad = (long)i;
			break;
		}
	}
	m->matched = j;
	// past the pattern, only idle frames
	for (; m->bad < 0 && i < nrec; i++)
		if (!idle(rec + 2 * i))
			m->bad = (long)i;
}

// a pattern with room for the FM's +128: no 16-bit sample above 32639
static void *dig_pattern(const struct et_fmt *f, unsigned long frames,
			 int seed)
{
	void *pat = et_pattern(f, frames, seed);
	unsigned long i;
	unsigned int ch;
	double v;

	memset(pat, 0x55, et_fmt_bytes(f));
	for (i = 0; i < frames; i++)
		for (ch = 0; ch < f->channels; ch++) {
			et_unpack(f, (char *)pat + i * et_fmt_bytes(f), 1, ch,
				  &v);
			if (v > 32767 - ET_FM_IDLE)
				et_pack1(f, pat, i, ch, 32767 - ET_FM_IDLE);
		}
	return pat;
}

static enum et_result dig_run(struct et_ctx *c, const struct et_fmt *f,
			      unsigned long frames, unsigned int flags,
			      double pause_at, double pause_for, int seed,
			      long *gap_out, struct et_stream *extra,
			      unsigned long buffer, unsigned long period)
{
	double secs = 0.3 + (double)frames / ET_FM_RATE + pause_for + 0.6;
	unsigned long nrec = (unsigned long)(secs * ET_FM_RATE);
	long fifo = 64 / et_fmt_bytes(f);
	// under dmix the ring copies the window ahead of the DMA every quarter
	// of a2_lead_ms, and dmix mixes a stream in at the DMA: what it loses
	long lead = 40;
	unsigned long skip = 0;
	struct et_stream s[3];
	struct dig_cmp m;
	int known = 0;
	void *pat = dig_pattern(f, frames, seed);

	if (et_mix_quiet(c) || et_mix_dig(c, 1)) {
		free(pat);
		et_fail(c, "the mixer refused the dig path");
		return ET_ERROR;
	}
	s[0] = et_rec(ET_FMC, &et_fm_fmt, nrec, 0);
	s[1] = et_play(ET_A2, f, pat, frames, 0.3);
	s[1].flags = flags;
	s[1].pause_at = pause_at;
	s[1].pause_for = pause_for;
	s[1].buffer = buffer;
	s[1].period = period;
	if (extra)
		s[2] = *extra;
	if (et_run(c, s, extra ? 3 : 2)) {
		et_fail(c, "the streams: %s, %s",
			s[0].err ? strerror(-s[0].err) : "ok",
			s[1].err ? strerror(-s[1].err) : "ok");
		free(s[0].data);
		free(pat);
		et_mix_dig(c, 0);
		return ET_ERROR;
	}
	et_mix_dig(c, 0);
	if (extra)
		*extra = s[2];
	if (flags & ET_DMIX) {
		et_param_get(c, "a2_lead_ms", &lead);
		skip = (unsigned long)(lead / 4.0 / 1000 * ET_FM_RATE) + fifo;
	}
	compare(f, pat, frames, s[0].data, s[0].done, fifo, skip, &m);
	if (skip) {
		et_measure(c, "frames missing at the start", m.first, "", 0, skip);
		known |= m.first != 0;
	}
	if (m.bad >= 0) {
		short *r = (short *)s[0].data + 2 * m.bad;

		et_fail(c, "recorded frame %ld is %d,%d, after %lu of %lu frames",
			m.bad, r[0], r[1], m.matched, frames);
		et_dump(c, "fm", &et_fm_fmt, s[0].data, s[0].done);
	} else {
		et_measure(c, "frames missing at the end", frames - m.matched,
			   "", 0, fifo);
		known |= m.matched < frames;
	}
	if (pause_for > 0) {
		et_measure(c, "the release against the pause", m.jump, "frames",
			   -fifo, fifo);
		known |= m.jump != 0;
	}
	if (gap_out)
		*gap_out = m.gap;
	et_measure(c, "xruns", s[1].xruns, "", 0, 0);
	free(s[0].data);
	free(pat);
	if (known && !c->fails) {
		snprintf(c->why, sizeof(c->why),
			 "%lu frames missing at the start, %lu at the end, a release %+ld frames from its pause",
			 m.first, frames - m.matched, m.jump);
		return ET_KNOWN;
	}
	return ET_PASS;
}

// dig.fmt.*: a second of a pattern in one format, exactly
enum et_result c_dig_fmt(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt fmts[] = {
		{ ET_S16, 2, 44100 }, { ET_S16, 1, 44100 },
		{ ET_U8, 2, 22050 },  { ET_U8, 1, 22050 },
		{ ET_S8, 2, 44100 },  { ET_U16, 2, 48000 },
	};

	return dig_run(c, &fmts[k->arg], 49716, 0, 0, 0, k->arg + 1, NULL,
		       NULL, 0, 0);
}

// dig.start: twenty short starts, none losing or adding a frame
enum et_result c_dig_start(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	int i, bad = 0;

	(void)k;
	for (i = 0; i < 20; i++) {
		int before = c->fails;
		enum et_result r = dig_run(c, &f, 4000 + 37 * i, 0, 0, 0,
					   100 + i, NULL, NULL, 0, 0);

		if (r == ET_ERROR)
			return r;
		bad += c->fails != before;
		c->why[0] = 0;
	}
	et_measure(c, "starts in step", 20 - bad, "of 20", 20, 20);
	return ET_PASS;
}

// dig.pause: a pause in the middle resumes on the next frame
enum et_result c_dig_pause(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	enum et_result r;
	long gap = 0;

	(void)k;
	r = dig_run(c, &f, 2 * 49716, 0, 0.8, 0.4, 7, &gap, NULL, 0, 0);
	et_measure(c, "pause, idle frames", gap, "", 0.3 * ET_FM_RATE,
		   0.6 * ET_FM_RATE);
	return r;
}

// dig.mmap and dig.dmix: the same through mmap and through dmix
enum et_result c_dig_access(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 48000 };

	return dig_run(c, &f, 3 * 49716, k->arg, 0, 0, 9 + k->arg, NULL,
		       NULL, 0, 0);
}

// dig.long: half a minute, every frame
enum et_result c_dig_long(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };

	(void)k;
	return dig_run(c, &f, 30 * 49716, 0, 0, 0, 31, NULL, NULL, 0, 0);
}

// dig.drain.full: periods of 1000 frames asked for in a buffer of 8192 and
// 14092 frames, whose last write leaves the buffer nearly full as the drain
// begins, with the end in the middle of a period: only idle frames after
// the pattern, none of what the ring held before. A driver that took the
// buffer as asked, not a whole number of periods, had its interrupts move
// against the buffer at each wrap, and the drain played the ring's old
// frames up to the next interrupt
enum et_result c_dig_drain(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };

	(void)k;
	return dig_run(c, &f, 8192 + 5000 + 900, 0, 0, 0, 51, NULL, NULL, 8192,
		       1000);
}

// dig.drain.two: two periods in the buffer, full as the drain begins: the
// DMA reaches the last frame before the drain ends, and its silence has to
// reach that far
enum et_result c_dig_drain_two(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };

	(void)k;
	return dig_run(c, &f, 8192 + 8192 + 900, 0, 0, 0, 53, NULL, NULL, 8192,
		       4096);
}

// dig.two: two periods in the buffer, polled as JACK does it, where nothing
// but the interrupts reads the pointer: each reading is then over half the
// buffer on from the last, whenever its interrupt came later than the one
// before. A driver that took that for a step back lost a buffer at the next,
// which played again with no xrun
enum et_result c_dig_two(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };

	(void)k;
	return dig_run(c, &f, 3 * 49716, ET_MMAP | ET_POLLED, 0, 0, 13, NULL,
		       NULL, 4096, 2048);
}

// dig.runout: an application that stops writing, with no stop threshold and
// the core's silence behind the DMA, as OSS's emulation and free-running
// clients ask for it: idle frames after the pattern, none of what the ring
// held before
enum et_result c_dig_runout(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };

	(void)k;
	return dig_run(c, &f, 8192 + 5000, ET_RUNOUT, 0, 0, 52, NULL, NULL, 8192,
		       1024);
}

enum et_result et_dig_run(struct et_ctx *c, const struct et_fmt *f,
			  unsigned long frames, int seed,
			  struct et_stream *extra)
{
	return dig_run(c, f, frames, 0, 0, 0, seed, NULL, extra, 0, 0);
}

void *et_dig_pattern(const struct et_fmt *f, unsigned long frames, int seed)
{
	return dig_pattern(f, frames, seed);
}

long et_dig_tail(const struct et_fmt *f, const void *pat, unsigned long frames,
		 const short *rec, unsigned long nrec, unsigned long n)
{
	unsigned long i = nrec > n ? nrec - n : 0, j, k;
	int e[2];

	// the recording's first frame of the stretch in the pattern, with the
	// three after it
	for (j = 0; j + 3 < frames; j++) {
		for (k = 0; k < 4; k++) {
			et_dig_frame(f, pat, j + k, e);
			if (!same(rec + 2 * (i + k), e))
				break;
		}
		if (k == 4)
			break;
	}
	if (j + 3 >= frames)
		return -1;
	for (k = 0; i + k < nrec && j + k < frames; k++) {
		et_dig_frame(f, pat, j + k, e);
		if (!same(rec + 2 * (i + k), e))
			break;
	}
	return (long)k;
}

// --- the rate correction ------------------------------------------------------

#define TRIM_PPB 1000000L // 1000 ppm, a clear step
#define TRIM_HZ 997.0 // a tone that shares no factor with the rates
#define TRIM_LEVEL 20000

// a tone through the rate correction, read back as the DMA took it: its
// frames recorded one for one, whatever the clock, so that the tone's pitch
// against the stream's rate is the step. How far it moved in ppm, its noise
// and distortion in dB
static enum et_result trim_tone(struct et_ctx *c, const struct et_fmt *f,
				unsigned int flags, unsigned long buffer,
				unsigned long period, double *ppm, double *thdn,
				int *glitches)
{
	unsigned long frames = 6 * f->rate;
	unsigned long nrec = (unsigned long)((0.3 + 6.0 * f->rate / ET_FM_RATE +
					      0.6) * ET_FM_RATE);
	void *tone = et_tone(f, frames, TRIM_HZ, TRIM_HZ, TRIM_LEVEL);
	struct et_stream s[2];
	struct an_fit fit;
	unsigned long a = 0, b, i;
	double *x;

	if (et_mix_quiet(c) || et_mix_dig(c, 1)) {
		free(tone);
		et_fail(c, "the mixer refused the dig path");
		return ET_ERROR;
	}
	s[0] = et_rec(ET_FMC, &et_fm_fmt, nrec, 0);
	s[1] = et_play(ET_A2, f, tone, frames, 0.3);
	s[1].flags = flags;
	s[1].buffer = buffer;
	s[1].period = period;
	if (et_run(c, s, 2)) {
		et_fail(c, "the streams: %s, %s",
			s[0].err ? strerror(-s[0].err) : "ok",
			s[1].err ? strerror(-s[1].err) : "ok");
		free(s[0].data);
		free(tone);
		et_mix_dig(c, 0);
		return ET_ERROR;
	}
	et_mix_dig(c, 0);
	x = et_chan(&et_fm_fmt, s[0].data, s[0].done, 0);
	free(tone);
	// the tone's middle, past its start's and end's edges
	for (i = 0; i < s[0].done; i++) {
		if (fabs(x[i] - ET_FM_IDLE) > 1000) {
			if (!a)
				a = i;
			b = i;
		}
	}
	if (!a || b < a + 3 * f->rate) {
		free(s[0].data);
		free(x);
		et_fail(c, "no tone came back");
		return ET_ERROR;
	}
	a += f->rate / 2;
	b -= f->rate / 2;
	if (an_fit_sine(x + a, b - a, f->rate, TRIM_HZ, 1, &fit)) {
		free(s[0].data);
		free(x);
		et_fail(c, "no tone came back");
		return ET_ERROR;
	}
	*ppm = (fit.f / TRIM_HZ - 1) * 1e6;
	*thdn = an_thdn(x + a, b - a, f->rate, TRIM_HZ, 20, 0);
	*glitches = an_glitches(x + a, b - a, f->rate, TRIM_HZ, 512, 20, NULL, 0);
	if (*glitches)
		et_dump(c, "fm", &et_fm_fmt, s[0].data, s[0].done);
	free(s[0].data);
	free(x);
	return ET_PASS;
}

// dig.trim: 1000 ppm in a2_trim_ppb moves the samples' pitch by that much,
// clean of steps and of the filter's noise, in stereo, in mono, and for a
// client that polls two periods, which its period's timer has to wake;
// down as well as up, and at 48 kHz
enum et_result c_dig_trim(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt st = { ET_S16, 2, 44100 };
	static const struct et_fmt mo = { ET_S16, 1, 44100 };
	static const struct et_fmt st48 = { ET_S16, 2, 48000 };
	long ppb = k->arg == 3 ? -TRIM_PPB : TRIM_PPB;
	double ppm, thdn;
	int glitches;
	enum et_result r;

	if (et_param_set(c, "a2_resample", 1) ||
	    et_param_set(c, "a2_trim_ppb", ppb))
		return ET_ERROR;
	r = trim_tone(c, k->arg == 1 ? &mo : k->arg == 4 ? &st48 : &st,
		      k->arg == 2 ? ET_MMAP | ET_POLLED : 0,
		      k->arg == 2 ? 4096 : 0, k->arg == 2 ? 2048 : 0, &ppm,
		      &thdn, &glitches);
	if (r != ET_PASS)
		return r;
	et_measure(c, "the pitch's change", ppm, "ppm", ppb / 1e3 - 0.1,
		   ppb / 1e3 + 0.1);
	et_measure(c, "THD+N", thdn, "dB", -200, -75);
	et_measure(c, "glitches", glitches, "", 0, 0);
	return ET_PASS;
}

// dig.trim.exact: with nothing to change the stream takes the mirror's copy,
// so that every frame comes back as it went in
enum et_result c_dig_trim_exact(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };

	(void)k;
	if (et_param_set(c, "a2_resample", 1) ||
	    et_param_set(c, "a2_trim_ppb", 0))
		return ET_ERROR;
	return dig_run(c, &f, 2 * 49716, 0, 0, 0, 61, NULL, NULL, 0, 0);
}

// dig.clock: 48 kHz in the Clock mode. The chip's 768 kHz clock plays 252
// ppm sharp, so the samples go down by that and by what the driver measures
// of the master clock. Slaved to the FM a stream's interrupts tell nothing
// of the chip's clock, so a few seconds off the dig path come first, for the
// fits, whose first samples would move the pitch within the tone
enum et_result c_dig_clock(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 48000 };
	unsigned long warm = 4 * 48000;
	struct et_stream s;
	double ppm, thdn, master, steered;
	int glitches;
	enum et_result r;

	(void)k;
	if (et_param_set(c, "a2_resample", 2) ||
	    et_param_set(c, "a2_trim_ppb", 0))
		return ET_ERROR;
	et_mix_dig(c, 0);
	s = et_play(ET_A2, &f, et_alloc_frames(&f, warm), warm, 0);
	if (et_run(c, &s, 1)) {
		free(s.data);
		return ET_ERROR;
	}
	free(s.data);
	r = trim_tone(c, &f, 0, 0, 0, &ppm, &thdn, &glitches);
	if (r != ET_PASS)
		return r;
	if (et_master_ppm(c, &master, &steered)) {
		et_fail(c, "the driver has no reading of the clock");
		return ET_ERROR;
	}
	et_measure(c, "the pitch's change, less the clock", ppm + master - steered,
		   "ppm", -251.95 - 3, -251.95 + 3);
	et_measure(c, "THD+N", thdn, "dB", -200, -75);
	et_measure(c, "glitches", glitches, "", 0, 0);
	return ET_PASS;
}

