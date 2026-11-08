// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The mon group: PCM device 3, the monitor, which reads back the frames
 * Audio 2's DAC took from what its DMA reads. A pattern played on Audio 2
 * has to come back exactly, from its first frame; through the rate
 * correction the frames are the filter's, which the same filter's C code
 * makes here; and on the dig path, where PCM 1 records what the DMA took
 * through the chip itself, the two agree frame for frame whatever the filter
 * does. A pause of Audio 2 is a gap that an xrun marks. Beside the monitor,
 * Audio 1 records the analog loop from Audio 2's DAC through the record
 * mixer, the measurement the device is for.
 *
 * Notes:
 *
 * The monitor starts before Audio 2, so that it takes Audio 2's first frame,
 * and reads fewer frames than Audio 2 plays, so that it is done before the
 * drain's end, which is a gap.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "an.h"
#include "cases.h"
#include "hal.h"
#include "sim_rs.h"

#define MON_FIFO 16 // frames of 16-bit stereo in Audio 2's FIFO
#define MON_PERIOD 1024 // the monitor's period and buffer
#define MON_BUFFER 8192
#define MON_MATCH 32 // the frames that place a recording in what was played
#define MON_SEARCH 64 // and how far from where it should be
#define MON_STAMPS 1024 // the readings of a stream's position kept
#define MON_TRIM_ZERO 12 // Audio 2 Rate Trim's item for no trim

enum { MON_OFF, MON_TRIM, MON_CLOCK };

// what a case plays, through what, and beside what
static const struct mon_case {
	enum et_format format;
	unsigned int channels, rate;
	int mode; // a2_resample
	long ppb; // a2_trim_ppb
	unsigned int over; // the ring frames to a stream frame
	int dig; // PCM 1 records what the DMA took, as the reference
	int pause; // Audio 2 pauses for 0.3 s
	int loop; // Audio 1 records the analog loop
} mon_t[] = {
	{ ET_S16, 2, 44100, MON_OFF, 0, 1, 0, 0, 0 },
	{ ET_U8, 1, 22050, MON_OFF, 0, 1, 0, 0, 0 },
	{ ET_S16, 2, 44100, MON_TRIM, 1000000, 1, 0, 0, 0 },
	{ ET_S16, 2, 44100, MON_TRIM, 30000, 2, 0, 0, 0 },
	{ ET_S16, 2, 44100, MON_CLOCK, 0, 1, 1, 0, 0 },
	{ ET_S16, 2, 44100, MON_OFF, 0, 1, 0, 1, 0 },
	{ ET_S16, 2, 44100, MON_OFF, 0, 1, 0, 0, 1 },
};

// LFSR words in every bit for an exact copy; through the filter, the FM's
// path or the analog loop, noise at a quarter of full scale, so that
// neither the filter's overshoot nor the FM's idle level leaves 16 bits
static void *mon_signal(const struct et_fmt *f, unsigned long frames, int quiet)
{
	unsigned int st = 0x5EED, ch;
	unsigned long i;
	void *d;

	if (!quiet)
		return et_pattern(f, frames, 0x5EED);
	d = et_alloc_frames(f, frames);
	for (i = 0; i < frames; i++)
		for (ch = 0; ch < f->channels; ch++)
			et_pack1(f, d, i, ch,
				 (int)(an_lfsr(&st) & 0x3FFF) - 0x2000);
	return d;
}

// frame i of buf as the monitor gives it: 16-bit stereo, an 8-bit sample in
// the high byte, a mono one on both sides
static void mon_frame(const struct et_fmt *f, const void *buf, unsigned long i,
		      int out[2])
{
	const char *p = (const char *)buf + i * et_fmt_bytes(f);
	double v[2];

	et_unpack(f, p, 1, 0, &v[0]);
	if (f->channels > 1)
		et_unpack(f, p, 1, 1, &v[1]);
	else
		v[1] = v[0];
	out[0] = (int)v[0];
	out[1] = (int)v[1];
}

static int mon_same(const struct et_fmt *rf, const void *ref, unsigned long i,
		    const short *rec, unsigned long j)
{
	int e[2];

	mon_frame(rf, ref, i, e);
	return rec[2 * j] == e[0] && rec[2 * j + 1] == e[1];
}

// the offset o at which rec's frame j is ref's j + o, from lo to hi, found
// in MON_MATCH frames in a row from rec's frame from; 0 and o, or -1
static int mon_place(const struct et_fmt *rf, const void *ref,
		     unsigned long nref, const short *rec, unsigned long nrec,
		     unsigned long from, long lo, long hi, long *o)
{
	long k;

	for (k = lo; k <= hi; k++) {
		unsigned long j;

		if ((long)from + k < 0)
			continue;
		for (j = 0;
		     j < MON_MATCH && from + j < nrec && from + j + k < nref;
		     j++)
			if (!mon_same(rf, ref, from + j + k, rec, from + j))
				break;
		if (j == MON_MATCH) {
			*o = k;
			return 0;
		}
	}
	return -1;
}

// rec's frames from a to b against ref's from a + o: those compared, and
// those that differ, the first of them noted
static void mon_cmp(struct et_ctx *c, const struct et_fmt *rf, const void *ref,
		    unsigned long nref, const short *rec, unsigned long a,
		    unsigned long b, long o, unsigned long *cmp,
		    unsigned long *diff)
{
	unsigned long j;

	for (j = a; j < b && (long)j + o < (long)nref; j++) {
		int e[2];

		(*cmp)++;
		mon_frame(rf, ref, j + o, e);
		if (rec[2 * j] == e[0] && rec[2 * j + 1] == e[1])
			continue;
		if (*diff < 4)
			et_note(c, "frame %lu: %d, %d where %d, %d", j,
				rec[2 * j], rec[2 * j + 1], e[0], e[1]);
		(*diff)++;
	}
}

// a line through a stream's readings of its position: its rate in ppm off
// nominal, and the rms left in us; t0 and f0 one point on it
static int mon_line(const struct et_stream *s, double *ppm, double *rms,
		    double *t0, double *f0)
{
	const struct et_stamp *p = s->stamps;
	double st = 0, sf = 0, tt = 0, tf = 0, sum = 0, slope, n;
	unsigned long i;

	if (s->stamped < 8)
		return -1;
	n = s->stamped;
	*t0 = p[0].t;
	*f0 = p[0].frame;
	for (i = 0; i < s->stamped; i++) {
		double t = p[i].t - *t0, f = p[i].frame - *f0;

		st += t;
		sf += f;
		tt += t * t;
		tf += t * f;
	}
	if (n * tt - st * st <= 0)
		return -1;
	slope = (n * tf - st * sf) / (n * tt - st * st);
	*f0 += (sf - slope * st) / n;
	for (i = 0; i < s->stamped; i++) {
		double r = (p[i].frame - *f0 - slope * (p[i].t - *t0)) / slope;

		sum += r * r;
	}
	*ppm = (slope / s->fmt.rate - 1) * 1e6;
	*rms = sqrt(sum / n) * 1e6;
	return 0;
}

// the loop's noise repeats every 65535 frames, the period of an_lfsr()'s
// words, so a lag holds only modulo that: the one nearest 0 is the loop's
#define MON_NOISE 65535L

static long mon_wrap(long lag)
{
	lag %= MON_NOISE;
	if (lag > MON_NOISE / 2)
		lag -= MON_NOISE;
	else if (lag < -MON_NOISE / 2)
		lag += MON_NOISE;
	return lag;
}

// the analog loop against the monitor: the lag of Audio 1's recording, in
// frames, on the monitor's first second and on its last, and their delay by
// the two streams' timestamps
static void mon_loop(struct et_ctx *c, const struct et_stream *m,
		     const struct et_stream *a1)
{
	unsigned long sec = m->fmt.rate, last = m->done - sec;
	double *x = et_chan(&m->fmt, m->data, m->done, 0);
	double *y = et_chan(&a1->fmt, a1->data, a1->done, 0);
	double corr = 0, corr2 = 0, ppm, rms, mt, mf, at, af;
	long lag, lag2;

	lag = mon_wrap(an_lag(x, sec, y, a1->done, a1->done, &corr));
	lag2 = mon_wrap(an_lag(x + last, sec, y, a1->done, a1->done, &corr2) -
			(long)last);
	et_measure(c, "the loop's correlation, first second", corr, "", 0.5, 1);
	et_measure(c, "and last", corr2, "", 0.5, 1);
	et_measure(c, "the loop's lag, last second less first", lag2 - lag,
		   "frames", -1, 1);
	if (!mon_line(m, &ppm, &rms, &mt, &mf) &&
	    !mon_line(a1, &ppm, &rms, &at, &af)) {
		// when the monitor's first frame was taken, and recorded
		double took = mt - mf / m->fmt.rate;
		double heard = at + (lag - af) / a1->fmt.rate;

		et_measure(c, "the loop's delay by the timestamps",
			   (heard - took) * 1e6, "us", 1, 0);
	}
	free(x);
	free(y);
}

// mon: a stream on Audio 2 with the monitor recording it, and the case's
// reference beside it
enum et_result c_mon(struct et_ctx *c, const struct et_case *k)
{
	const struct mon_case *t = &mon_t[k->arg];
	const struct et_fmt f = { t->format, t->channels, t->rate };
	const struct et_fmt mf = { ET_S16, 2, t->rate * t->over };
	static const struct et_fmt af = { ET_S16, 2, 44100 };
	unsigned long n = (t->pause ? 4 : 3) * f.rate;
	unsigned long m = (t->pause ? 3 : 2) * mf.rate;
	// PCM 1 from before Audio 2's start to past its end, a frame of it at
	// each of the FM's
	unsigned long nrec = n + (unsigned long)(0.9 * ET_FM_RATE);
	unsigned long cmp = 0, diff = 0, made = n, from, end;
	struct et_stream s[4], *ms, *ds = NULL, *ls = NULL;
	struct et_fmt rf = f;
	void *d = mon_signal(&f, n, t->mode || t->dig || t->loop), *ref = d;
	short *dig_ref = NULL;
	enum et_result r = ET_PASS;
	double ppm, rms, t0, f0;
	int i = 0;
	long o = 0;

	if (et_param_set(c, "a2_trim", MON_TRIM_ZERO) ||
	    et_param_set(c, "a2_trim_ppb", t->ppb) ||
	    et_param_set(c, "a2_resample", t->mode) ||
	    et_param_set(c, "a2_resample_dither", 0) ||
	    et_param_set(c, "a2_treble", 0) ||
	    et_param_set(c, "a2_oversample", t->over - 1) || et_mix_quiet(c) ||
	    et_mix_dig(c, t->dig)) {
		free(d);
		et_fail(c, "the settings or the mixer refused the case");
		return ET_ERROR;
	}
	if (t->loop) {
		// the loop through the record mixer, silent on the speakers
		et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
		et_ctl_set(c, "Audio 2 Filter Playback Switch", 0, 1, 1);
		et_ctl_set(c, "Audio 1 Filter Switch", 0, 1, 1);
		if (et_mix_rec_a2(c, 13) < 0) {
			free(d);
			return ET_ERROR;
		}
	}
	memset(s, 0, sizeof(s));
	if (t->dig) {
		s[i] = et_rec(ET_FMC, &et_fm_fmt, nrec, 0);
		ds = &s[i++];
	}
	if (t->loop) {
		s[i] = et_rec(ET_A1C, &af, 4 * af.rate, 0);
		s[i].stamps = calloc(MON_STAMPS, sizeof(*s[i].stamps));
		s[i].max_stamps = MON_STAMPS;
		ls = &s[i++];
	}
	s[i] = et_play(ET_A2, &f, d, n, 0.3);
	if (t->pause) {
		s[i].pause_at = 1.5;
		s[i].pause_for = 0.3;
	}
	i++;
	s[i] = et_rec(ET_MON, &mf, m, 0.1);
	s[i].period = MON_PERIOD;
	s[i].buffer = MON_BUFFER;
	s[i].stamps = calloc(MON_STAMPS, sizeof(*s[i].stamps));
	s[i].max_stamps = MON_STAMPS;
	ms = &s[i++];
	if (!ms->stamps || (ls && !ls->stamps))
		abort();
	if (et_run(c, s, i)) {
		et_fail(c, "the streams: the monitor %s, Audio 2 %s",
			ms->err ? strerror(-ms->err) : "ok",
			s[i - 2].err ? strerror(-s[i - 2].err) : "ok");
		r = ET_ERROR;
		goto out;
	}
	et_mix_dig(c, 0);
	if (ms->done < m) {
		et_fail(c, "the monitor gave %lu frames of %lu", ms->done, m);
		r = ET_ERROR;
		goto out;
	}

	// the reference: the stream, through the filter here, or what PCM 1
	// recorded of what the DMA took, its sides back and less its idle
	if (t->dig) {
		unsigned long a, j;

		for (a = 0; a < ds->done; a++) {
			const short *p = (const short *)ds->data + 2 * a;

			if (p[0] != ET_FM_IDLE || p[1] != ET_FM_IDLE)
				break;
		}
		made = ds->done - a;
		dig_ref = malloc((made ? made : 1) * 4);
		if (!dig_ref)
			abort();
		for (j = 0; j < made; j++) {
			const short *p = (const short *)ds->data + 2 * (a + j);

			dig_ref[2 * j] = (short)(p[1] - ET_FM_IDLE);
			dig_ref[2 * j + 1] = (short)(p[0] - ET_FM_IDLE);
		}
		ref = dig_ref;
		rf = (struct et_fmt){ ET_S16, 2, mf.rate };
	} else if (t->mode || t->over > 1) {
		ref = sim_rs_over(d, n, f.channels, t->ppb, t->over, 0, &made);
		rf = (struct et_fmt){ ET_S16, f.channels, mf.rate };
	}

	// where the monitor's frames lie in it: from the stream's first frame,
	// or, on the dig path, as far from PCM 1's first as PCM 1 went without
	if (mon_place(&rf, ref, made, (short *)ms->data, ms->done,
		      t->dig ? MON_SEARCH : 0, t->dig ? -MON_SEARCH : 0,
		      MON_SEARCH, &o)) {
		et_fail(c, "the monitor's frames aren't the stream's");
		et_dump(c, "mon", &mf, ms->data, ms->done);
		r = ET_FAIL;
		goto out;
	}
	if (t->dig)
		et_measure(c, "the stream's first frames PCM 1 went without",
			   -o, "", 0, MON_FIFO);
	else
		et_measure(c, "the stream's first frames the monitor missed", o,
			   "", 0, 0);
	// PCM 1 begins that many of the monitor's frames in
	from = o < 0 ? (unsigned long)-o : 0;
	end = ms->done;
	if (t->pause) {
		unsigned long j;
		long o2 = 0;

		// the frames in order up to the gap, and from where Audio 2
		// went on after it: a FIFO back at most, and a buffer of the
		// monitor's ahead, the frames its reader hadn't read
		for (j = from; j < ms->done; j++)
			if (!mon_same(&rf, ref, j + o, (short *)ms->data, j))
				break;
		end = j;
		et_measure(c, "the frames before the gap", end, "",
			   0.5 * mf.rate, ms->done);
		if (end < ms->done) {
			if (mon_place(&rf, ref, made, (short *)ms->data,
				      ms->done, end, -MON_FIFO - 1,
				      MON_BUFFER + MON_FIFO, &o2)) {
				et_fail(c,
					"nothing after the gap is the stream's");
				r = ET_FAIL;
				goto out;
			}
			mon_cmp(c, &rf, ref, made, (short *)ms->data, end,
				ms->done, o2, &cmp, &diff);
			et_measure(c, "the frames the gap skipped", o2, "",
				   -MON_FIFO - 1, MON_BUFFER + MON_FIFO);
		}
		et_measure(c, "xruns", ms->xruns, "", 1, 1);
	} else {
		et_measure(c, "xruns", ms->xruns, "", 0, 0);
	}
	mon_cmp(c, &rf, ref, made, (short *)ms->data, from, end, o, &cmp,
		&diff);
	et_measure(c, "the frames compared", cmp, "", 0.98 * ms->done,
		   ms->done);
	et_measure(c, "the frames that differ", diff, "", 0, 0);
	if (diff)
		et_dump(c, "mon", &mf, ms->data, ms->done);
	// the timestamps run at the ring's rate, where the DMA does
	if (!t->dig && !t->pause && !mon_line(ms, &ppm, &rms, &t0, &f0)) {
		et_measure(c, "the monitor's rate by its timestamps", ppm,
			   "ppm", -100, 100);
		et_measure(c, "their scatter", rms, "us", 0, 500);
	}
	if (t->loop) {
		et_measure(c, "Audio 1's xruns", ls->xruns, "", 0, 0);
		mon_loop(c, ms, ls);
	}
out:
	et_mix_dig(c, 0);
	for (i = 0; i < 4; i++)
		if (s[i].capture) {
			free(s[i].data);
			free(s[i].stamps);
		}
	if (ref != d)
		free(ref);
	free(d);
	return r;
}
