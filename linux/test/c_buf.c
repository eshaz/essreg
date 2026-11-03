// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * The buf and cpu groups: what the buffers report and how the streams
 * recover, measured on the dig path, which shows when each frame played;
 * and the CPU the driver takes while the engines play.
 *
 * Notes:
 *
 * A stream's delay counts its buffer and the chip's FIFO: 64 bytes on Audio
 * 2, 16 frames of 16-bit stereo. The trigger timestamps it is measured
 * against come after the driver's callbacks, a few milliseconds late, so
 * the timing catches a delay a period off; the FIFO's term itself is the
 * delay less what the buffer holds, from one status reading.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cases.h"
#include "hal.h"

// buf.delay: what Audio 2's delay says against when its frames played on
// the dig path: in mid-stream, the frame that the delay says is playing,
// on PCM 1's clock; and the first frame against the trigger
enum et_result c_buf_delay(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 48000 };
	unsigned long n = 2 * ET_FM_RATE;
	struct et_stream s[2];
	unsigned long i;
	short *r;
	void *pat = et_pattern(&f, n, 77);

	(void)k;
	memset(pat, 0x55, 4);
	if (et_mix_quiet(c) || et_mix_dig(c, 1)) {
		free(pat);
		return ET_ERROR;
	}
	s[0] = et_rec(ET_FMC, &et_fm_fmt, 3 * ET_FM_RATE, 0);
	s[1] = et_play(ET_A2, &f, pat, n, 0.4);
	s[1].buffer = 4096;
	s[1].period = 1024;
	s[1].probe_at = 1.0;
	if (et_run(c, s, 2)) {
		et_mix_dig(c, 0);
		free(pat);
		free(s[0].data);
		return ET_ERROR;
	}
	et_mix_dig(c, 0);
	r = s[0].data;
	for (i = 0; i < s[0].done; i++)
		if (r[2 * i] != ET_FM_IDLE || r[2 * i + 1] != ET_FM_IDLE)
			break;
	if (i == s[0].done || !s[1].probe_t) {
		et_fail(c, "%s", i == s[0].done ? "no frame reached the music DAC" :
						  "no status reading");
	} else {
		// the frame playing at the reading, by the delay, and when
		// PCM 1 heard it
		long playing = (long)s[1].probe_done - s[1].probe_delay;
		double heard = s[0].t_trigger + (double)(i + playing) / ET_FM_RATE;

		// the core stamps a trigger after the driver's callback, which
		// goes on after the start (Audio 2's first copy, a DSP
		// command for PCM 1), so these hold to a few milliseconds
		et_measure(c, "first frame after the trigger",
			   (s[0].t_trigger + (double)i / ET_FM_RATE -
			    s[1].t_trigger) *
				   1000,
			   "ms", -5, 5);
		et_measure(c, "the delay against the music DAC",
			   (heard - s[1].probe_t) * 1000, "ms", -5, 5);
		// the delay past what the buffer holds is the driver's own
		// term: the FIFO's 64 bytes, exactly
		et_measure(c, "the FIFO in the delay",
			   s[1].probe_delay -
				   ((long)s[1].buffer_got - s[1].probe_avail),
			   "frames", 64 / et_fmt_bytes(&f), 64 / et_fmt_bytes(&f));
	}
	free(pat);
	free(s[0].data);
	return ET_PASS;
}

// buf.drain: a drain ends when the last frame has played
enum et_result c_buf_drain(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	struct et_stream s = et_play(ET_A2, &f, et_tone(&f, 22050, 441, 0, 0),
				     22050, 0);

	(void)k;
	et_mix_quiet(c);
	et_mix_dig(c, 0);
	if (et_run(c, &s, 1)) {
		free(s.data);
		return ET_ERROR;
	}
	et_measure(c, "half a second's drain", s.t_end - s.t_trigger, "s",
		   0.495, 0.53);
	free(s.data);
	return ET_PASS;
}

// buf.xrun.*: a stalled reader or writer gets an xrun and carries on
enum et_result c_buf_xrun(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	struct et_stream s;

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	if (k->arg)
		s = et_rec(ET_A1C, &f, 3 * 44100, 0);
	else
		s = et_play(ET_A2, &f, et_tone(&f, 3 * 44100, 441, 0, 0),
			    3 * 44100, 0);
	s.buffer = 4096;
	s.period = 1024;
	s.flags = ET_STALL;
	s.pause_at = 1.0;
	s.pause_for = 0.5;
	et_run(c, &s, 1);
	if (s.err)
		et_note(c, "the stream: %s", strerror(-s.err));
	et_measure(c, "xruns", s.xruns, "", 1, 3);
	et_measure(c, "frames moved", s.done, "", s.frames, s.frames);
	free(s.data);
	return s.err ? ET_FAIL : ET_PASS;
}

// the driver's cycles so far, from its proc file's cost line: interrupts
// and ring ticks, each count times its average
static int cost(struct et_ctx *c, double *cycles, unsigned long *irqs,
		unsigned long *ticks)
{
	static char proc[16384];
	unsigned long icyc, tcyc;
	char *p;

	if (c->hal->proc(c->hal->ctx, proc, sizeof(proc)) < 0)
		return -1;
	p = strstr(proc, "cost: ");
	if (!p || sscanf(p, "cost: interrupts %lu, %lu cycles each; ring ticks %lu, %lu cycles each",
			 irqs, &icyc, ticks, &tcyc) != 4)
		return -1;
	*cycles = (double)*irqs * icyc + (double)*ticks * tcyc;
	return 0;
}

// cpu.*: the driver's share of the CPU while an engine plays 44.1 kHz
// 16-bit stereo for ten seconds, from its own cycle counts (its interrupts
// and ring ticks) against the CPU's clock; the whole system's share from
// /proc/stat, against ten idle seconds, as information
enum et_result c_cpu(struct et_ctx *c, const struct et_case *k)
{
	static const struct et_fmt f = { ET_S16, 2, 44100 };
	unsigned long n = 10 * 44100, irq0, irq1, tick0, tick1;
	struct et_stream s[2];
	double b0, t0, b1, t1, b2, t2, cyc0, cyc1, hz, secs;
	int i, ns = 0;
	void *tone = et_tone(&f, n, 441, 0, 300);

	et_mix_quiet(c);
	et_mix_dig(c, 0);
	et_ctl_set(c, "Master Playback Switch", 0, 0, 0);
	hz = c->hal->cpu_hz(c->hal->ctx);
	if (c->hal->cpu(c->hal->ctx, &b0, &t0) < 0 || hz <= 0) {
		free(tone);
		return ET_SKIP;
	}
	c->hal->sleep(c->hal->ctx, 10);
	c->hal->cpu(c->hal->ctx, &b1, &t1);
	for (i = 0; i < 2; i++)
		if (k->arg & (1 << i)) {
			s[ns] = et_play(i ? ET_A1P : ET_A2, &f, tone, n, 0);
			s[ns].period = 2048;
			s[ns].buffer = 8192;
			ns++;
		}
	if (cost(c, &cyc0, &irq0, &tick0) < 0) {
		et_fail(c, "no cost line in the proc file");
		free(tone);
		return ET_FAIL;
	}
	if (et_run(c, s, ns)) {
		free(tone);
		return ET_ERROR;
	}
	cost(c, &cyc1, &irq1, &tick1);
	c->hal->cpu(c->hal->ctx, &b2, &t2);
	secs = (double)n / 44100;
	et_measure(c, "interrupts a second", (irq1 - irq0) / secs, "", 1, 0);
	et_measure(c, "ring ticks a second", (tick1 - tick0) / secs, "", 1, 0);
	et_measure(c, "the driver's share", (cyc1 - cyc0) / (secs * hz) * 100,
		   "%", 0, k->arg == 3 ? 2 : 1);
	et_measure(c, "busy, idle", (b1 - b0) / (t1 - t0 > 0 ? t1 - t0 : 1) * 100,
		   "%", 1, 0);
	et_measure(c, "busy, playing",
		   (b2 - b1) / (t2 - t1 > 0 ? t2 - t1 : 1) * 100, "%", 1, 0);
	free(tone);
	return ET_PASS;
}
