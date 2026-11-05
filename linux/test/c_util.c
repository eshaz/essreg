// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * What the case groups share: test signals in every format, the music
 * DAC's view of Audio 2, recordings as doubles, and the mixer set up for
 * each path through the chip.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cases.h"
#include "hal.h"

const struct et_fmt et_fm_fmt = { ET_S16, 2, ET_FM_RATE };
const struct et_fmt et_rec2_fmt = { ET_S16, 2, 48000 };

void *et_tone(const struct et_fmt *f, unsigned long frames, double hz_l,
	      double hz_r, double amp)
{
	void *buf = et_alloc_frames(f, frames);
	unsigned long i;

	for (i = 0; i < frames; i++) {
		double t = (double)i / f->rate;

		et_pack1(f, buf, i, 0, hz_l ? amp * sin(2 * M_PI * hz_l * t) : 0);
		if (f->channels > 1)
			et_pack1(f, buf, i, 1,
				 hz_r ? amp * sin(2 * M_PI * hz_r * t) : 0);
	}
	return buf;
}

void *et_pattern(const struct et_fmt *f, unsigned long frames,
		 unsigned int seed)
{
	unsigned char *buf = et_alloc_frames(f, frames);
	unsigned long i, n = frames * et_fmt_bytes(f);
	unsigned int st = seed;

	for (i = 0; i < n; i += 2) {
		unsigned int w = an_lfsr(&st);

		buf[i] = w & 0xFF;
		if (i + 1 < n)
			buf[i + 1] = w >> 8;
	}
	return buf;
}

void et_dig_frame(const struct et_fmt *f, const void *buf, unsigned long i,
		  int out[2])
{
	double v[2];

	et_unpack(f, (const char *)buf + i * et_fmt_bytes(f), 1, 0, &v[0]);
	if (f->channels > 1)
		et_unpack(f, (const char *)buf + i * et_fmt_bytes(f), 1, 1,
			  &v[1]);
	else
		v[1] = v[0];
	// the music DAC has Audio 2's left on its right
	out[0] = (int)v[1] + ET_FM_IDLE;
	out[1] = (int)v[0] + ET_FM_IDLE;
}

double *et_chan(const struct et_fmt *f, const void *buf, unsigned long frames,
		unsigned int ch)
{
	double *x = malloc((frames ? frames : 1) * sizeof(*x));

	if (!x)
		abort();
	et_unpack(f, buf, frames, ch, x);
	return x;
}

int et_mix_quiet(struct et_ctx *c)
{
	static const char *const inputs[] = {
		"Line Playback Volume", "CD Playback Volume",
		"Aux Playback Volume", "Mic Playback Volume",
		"Mono Playback Volume",
	};
	unsigned int i;
	int err = 0;

	for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++)
		et_ctl_set(c, inputs[i], 0, 0, 0);
	et_ctl_set(c, "Mic Boost (+26dB)", 0, 0, 0);
	et_ctl_set(c, "3D Control - Switch", 0, 0, 0);
	err |= et_ctl_set(c, "Video Playback Switch", 0, 0, 0) < 0;
	// snd-es18xx has no such switch
	if (!c->stock)
		et_ctl_set(c, "Mono Input Direct Playback Switch", 0, 0, 0);
	return err ? -1 : 0;
}

int et_mix_dig(struct et_ctx *c, int on)
{
	struct et_ctl_info info;

	if (et_ctl_set(c, "Video Playback Switch", 0, 0, 0) < 0)
		return -1;
	// snd-es18xx has no such switch, and the path is off there
	if (!on && c->hal->ctl_info(c->hal->ctx, "Audio 2 To FM Playback Switch",
				    0, &info) == -ENOENT)
		return 0;
	return et_ctl_set(c, "Audio 2 To FM Playback Switch", 0, on, on) < 0 ?
		       -1 :
		       0;
}

double et_noise_floor(const double *x, unsigned long n)
{
	double a, b;

	if (n < 2 * 9600)
		return an_rms(x, n, 1);
	a = an_rms(x, 9600, 1);
	b = an_rms(x + n - 9600, 9600, 1);
	return a < b ? a : b;
}

enum et_role et_out_role(const struct et_ctx *c)
{
	return c->stock ? ET_A1P : ET_A2;
}

int et_mix_rec_a2(struct et_ctx *c, int level)
{
	static const char *const others[] = {
		"Mic Capture Volume", "Line Capture Volume",
		"FM Capture Volume", "CD Capture Volume",
		"Aux Capture Volume", "Mono Capture Volume",
	};
	unsigned int i;

	for (i = 0; i < sizeof(others) / sizeof(others[0]); i++)
		et_ctl_set(c, others[i], 0, 0, 0);
	if (et_ctl_set_enum(c, "Capture Source", "Mix") < 0)
		return -1;
	et_ctl_set(c, "Capture Switch", 0, 1, 1);
	// B4h at 0 dB on the line inputs' scale
	et_ctl_set(c, "Capture Volume", 0, 4, 4);
	return et_ctl_set(c, "PCM Capture Volume", 0, level, level) < 0 ? -1 :
									  0;
}

int et_mix_out(struct et_ctx *c, int a1, int a2)
{
	et_ctl_set(c, "Master Playback Switch", 0, 1, 1);
	if (et_ctl_set(c, "Master Playback Volume", 0, 54,
		       c->rec2_left ? 0 : 54) < 0)
		return -1;
	et_ctl_set(c, "PCM Playback Volume", 0, a2, a2);
	et_ctl_set(c, "PCM Playback Volume", 1, a1, a1);
	return 0;
}

struct et_stream et_play(enum et_role r, const struct et_fmt *f, void *data,
			 unsigned long frames, double start)
{
	struct et_stream s;

	memset(&s, 0, sizeof(s));
	s.role = r;
	s.fmt = *f;
	s.data = data;
	s.frames = frames;
	s.start = start;
	return s;
}

struct et_stream et_rec(enum et_role r, const struct et_fmt *f,
			unsigned long frames, double start)
{
	struct et_stream s = et_play(r, f, et_alloc_frames(f, frames), frames,
				     start);

	s.capture = 1;
	return s;
}
