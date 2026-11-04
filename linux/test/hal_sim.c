// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * es1869test's simulated machine: an ES1869 with settings.def's controls
 * and parameters, the cable from its left output to a second card, and
 * virtual time. Every recording is made from the streams that played in
 * the same run, so that the cases pass here as on a healthy chip, and the
 * faults of hal.h each fail the cases that look for them.
 *
 * Notes:
 *
 * The paths: Audio 2 slaved to the FM goes to PCM 1 exactly, sides
 * swapped, plus +128, at 49716 Hz; the record mixer takes Audio 2 at 69h's
 * level; the output is Audio 1 and Audio 2 at their levels and the master,
 * and the second card hears its left side, on a clock 800 ppm off. A note
 * from --fm-note is a sine on the sides its bits give. Record source 7
 * goes through src/s3dsim.c's 3-D effect, with the registers that the 3-D
 * controls hold, without its path's gain of 0.5.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cases.h"
#include "hal.h"
#include "settab.h"
#include "s3dsim.h"
#include "sim_rs.h"

#define REC2_PPM 800.0
#define SIM_MASTER_PPB 880 // the master clock against the raw system clock

struct sim {
	unsigned int faults;
	double now;
	long val[256][2];
	int lost52; // SIM_RESUME: 52h at 0 in the chip since a resume
	uint32_t irqs[4];
	long hwcount[2]; // 61h and 63h, which the buttons move with the split
	char klog[512];
	double mute_at; // SIM_A1_MUTES: Audio 2 silent 100 ms from here
};

static struct sim sim;

static int find_ctl(const char *name, int index)
{
	int i;

	for (i = 0; i < et_nsettings; i++)
		if ((et_settings[i].flags & F_CTL) &&
		    et_settings[i].index == index &&
		    !strcmp(et_settings[i].ctl, name))
			return i;
	return -1;
}

static int find_id(const char *id)
{
	int i;

	for (i = 0; i < et_nsettings; i++)
		if (!strcmp(et_settings[i].id, id))
			return i;
	return -1;
}

static long ctl(const char *name, int index, int ch)
{
	int i = find_ctl(name, index);

	return i < 0 ? 0 : sim.val[i][ch];
}

static long param(const char *id)
{
	int i = find_id(id);

	return i < 0 ? 0 : sim.val[i][0];
}

// --- the signals -------------------------------------------------------------

// sample i of channel ch of a stream, in s16 scale, or 0 past its ends
static double sample(const struct et_stream *s, long i, int ch)
{
	double v;

	if (i < 0 || i >= (long)s->frames || !s->data)
		return 0;
	et_unpack(&s->fmt, (const char *)s->data + i * et_fmt_bytes(&s->fmt),
		  1, ch < (int)s->fmt.channels ? ch : 0, &v);
	return v;
}

// a playback stream's signal at time t, linearly interpolated, with a
// pause's gap; rate is the rate it really plays at
static double at(const struct et_stream *s, double t, double rate, int ch)
{
	double x;
	long i;

	t -= s->start;
	if (s->pause_at > 0 && t > s->pause_at)
		t = t < s->pause_at + s->pause_for ? -1 : t - s->pause_for;
	if (t < 0)
		return 0;
	x = t * rate;
	i = (long)floor(x);
	return sample(s, i, ch) + (x - i) * (sample(s, i + 1, ch) -
					     sample(s, i, ch));
}

// the driver's dB scales (mixer.c), in hundredths of a dB
static long tlv_cdb(int tlv, long v)
{
	switch (tlv) {
	case T_A1:
		return !v ? ET_DB_MUTE : v < 8 ? -3300 + 300 * v :
					      -900 + 150 * (v - 8);
	case T_A2:
		return !v ? ET_DB_MUTE : v < 8 ? -3450 + 300 * v :
					      -1050 + 150 * (v - 8);
	case T_MIC:
		return !v ? ET_DB_MUTE : v < 8 ? -2400 + 300 * v :
					      150 * (v - 8);
	case T_AUX:
		return !v ? ET_DB_MUTE : v < 8 ? -3150 + 300 * v :
					      -750 + 150 * (v - 8);
	case T_MASTER:
		return -4725 + 75 * v;
	case T_BEEP:
		return !v ? ET_DB_MUTE : -2700 + 300 * v;
	case T_3D:
		return -3045 + 75 * v;
	case T_REC:
		// B4h for the sources other than the microphone
		return -600 + 150 * v;
	}
	return 0;
}

// a control's gain as the simulated chip applies it, one step off with
// SIM_TLV_OFF for Audio 2's playback volume
static double ctl_gain(const char *name, int index, int ch)
{
	int i = find_ctl(name, index);
	long v, cdb;

	if (i < 0)
		return 1;
	v = sim.val[i][ch];
	if ((sim.faults & SIM_TLV_OFF) && et_settings[i].tlv == T_A2 && v > 1)
		v--;
	cdb = tlv_cdb(et_settings[i].tlv, v);
	return cdb == ET_DB_MUTE ? 0 : pow(10, cdb / 2000.0);
}

// Audio 2's real rate: slaved to the FM, the FM's; with the data sheet's
// clocks, the 793.8 kHz family runs 2140 ppm fast
static double a2_rate(const struct et_stream *s)
{
	if (ctl("Audio 2 To FM Playback Switch", 0, 0))
		return ET_FM_RATE;
	if ((sim.faults & SIM_CLOCK_DS) && s->fmt.rate % 11025 == 0)
		return s->fmt.rate * 795.5 / 793.8;
	return s->fmt.rate;
}

static double a2_out(const struct et_stream *a2, double t, int ch)
{
	if (!a2 || (sim.faults & SIM_A2_SILENT))
		return 0;
	if (sim.mute_at > 0 && t >= sim.mute_at && t < sim.mute_at + 0.1)
		return 0;
	if (sim.faults & SIM_A2_SWAP)
		ch = !ch;
	return at(a2, t, a2_rate(a2), ch);
}

// a note from an --fm-note command: frequency, and which channels it
// sounds on
static int fm_note(const struct et_stream *s, double *hz, int side[2],
		   double *len)
{
	const char *spec = s->cmd ? strstr(s->cmd, "--fm-note ") : NULL;
	char mode[8], bits[8];
	int fnum, block, mirror;

	if (!spec || sscanf(spec + 10, "%7[^:]:%7[^:]:%i:%i:%lf", mode, bits,
			    &fnum, &block, len) != 5)
		return 0;
	*hz = fnum * ET_FM_RATE * pow(2, block - 20);
	if (!strcmp(mode, "esfm")) {
		int b = (int)strtol(bits, NULL, 0);

		side[0] = (b & 0x20) != 0;
		side[1] = (b & 0x10) != 0;
		return 1;
	}
	// OPL3: A is bit 4, the chip's right, swapped back with fm_sides
	mirror = !param("fm_sides") || (sim.faults & SIM_FM_SIDES);
	side[mirror ? 1 : 0] = strchr(bits, 'L') != NULL;
	side[mirror ? 0 : 1] = strchr(bits, 'R') != NULL;
	return 1;
}

// a low-pass biquad (RBJ), Butterworth when q is 1/sqrt(2)
struct biquad {
	double b0, b1, b2, a1, a2, z1, z2;
};

static void lowpass(struct biquad *f, double fc, double fs, double q)
{
	double w = 2 * M_PI * fc / fs, al = sin(w) / (2 * q), a0 = 1 + al;

	f->b0 = (1 - cos(w)) / 2 / a0;
	f->b1 = (1 - cos(w)) / a0;
	f->b2 = f->b0;
	f->a1 = -2 * cos(w) / a0;
	f->a2 = (1 - al) / a0;
	f->z1 = f->z2 = 0;
}

static double biquad(struct biquad *f, double x)
{
	double y = f->b0 * x + f->z1;

	f->z1 = f->b1 * x - f->a1 * y + f->z2;
	f->z2 = f->b2 * x - f->a2 * y;
	return y;
}

// the 3-D effect's registers, 50h and 52h-5Ah, from its controls
static void regs_3d(u8 *reg)
{
	reg[0] = (ctl("3D Control - Switch", 0, 0) ? 0x0C : 0x04) |
		 ctl("3D Control - Mono Switch", 0, 0) << 1 |
		 ctl("3D Control - Limit Switch", 0, 0);
	reg[1] = ctl("3D Control - Level", 0, 0);
	reg[2] = ctl("3D Control - Limit Rise Level", 0, 0);
	reg[3] = ctl("3D Control - Limit Fall Level", 0, 0);
	reg[4] = ctl("3D Control - Limit Rise Speed", 0, 0) << 4 |
		 ctl("3D Control - Limit Fall Speed", 0, 0);
	reg[5] = ctl("3D Control - Limit Fixed Level", 0, 0);
}

static s16 clip16(double v)
{
	return v > 32767 ? 32767 : v < -32768 ? -32768 : (s16)lrint(v);
}

// the rate correction, through the driver's own filter: Audio 2's stream as
// the ring would hold it, in a copy of the stream
static long sim_setting(const char *id)
{
	int i = find_id(id);

	return i < 0 ? 0 : sim.val[i][0];
}

static const struct et_stream *sim_resampled(const struct et_stream *a2)
{
	static struct et_stream out;
	static short *data;
	long mode = sim_setting("a2_resample");
	long ppb = sim_setting("a2_trim_ppb");
	int sign = 0;
	short *made;

	if (!mode || a2->fmt.format != ET_S16 || (sim.faults & SIM_TRIM))
		return a2;
	if (mode == 2) {
		ppb -= SIM_MASTER_PPB;
		if (a2->fmt.rate % 8000 == 0)
			sign = sim.faults & SIM_CLOCK_BASE ? -1 : 1;
	}
	out = *a2;
	made = sim_rs_stream(a2->data, a2->frames, a2->fmt.channels, ppb, sign,
			     &out.frames);
	if (!made)
		return a2;
	free(data);
	data = made;
	out.data = data;
	return &out;
}

static void fill(struct et_stream *r, struct et_stream *s, int n)
{
	// Audio 2's DAC stages, as low-passes at 0.45 of its rate: the
	// filter a second-order one, the oversampling a fourth-order one
	struct biquad stage[2][2];
	int stages = 0;
	const struct et_stream *a2 = NULL, *a1 = NULL, *cmd = NULL;
	const struct et_stream *dual = NULL, *rec_cmd = NULL;
	unsigned long i;
	double noise = r->role == ET_REC2 ? 6 : 0.7;
	unsigned int seed = 12345;
	struct s3dsim fx;
	int j, src7 = 0;

	for (j = 0; j < n; j++) {
		if (s[j].capture)
			continue;
		if (s[j].role == ET_A2)
			a2 = sim_resampled(&s[j]);
		else if (s[j].role == ET_A1P)
			a1 = &s[j];
		else if (s[j].role == ET_CMD && strstr(s[j].cmd, "arecord -q"))
			rec_cmd = &s[j];
		else if (s[j].role == ET_CMD)
			cmd = &s[j];
		else if (s[j].role == ET_DUAL)
			dual = &s[j];
	}
	if (r->role == ET_A1C && ctl("Capture Source", 0, 0) == 7) {
		u8 reg[6];

		regs_3d(reg);
		s3dsim_reset(&fx, r->fmt.rate);
		s3dsim_regs(&fx, reg);
		src7 = 1;
	}
	if (a2 && r->role == ET_A1C) {
		// below the recording's Nyquist, where a biquad holds; with
		// SIM_FILTER_FIXED the corner stays at 44.1 kHz's
		double fc = fmin(0.45 * (sim.faults & SIM_FILTER_FIXED ? 44100 :
					 a2_rate(a2)),
				 0.45 * r->fmt.rate);
		int ch;

		if (ctl("Audio 2 Oversampling Playback Switch", 0, 0))
			stages = 2;
		else if (ctl("Audio 2 Filter Playback Switch", 0, 0))
			stages = 1;
		for (ch = 0; ch < 2; ch++) {
			lowpass(&stage[ch][0], fc, r->fmt.rate,
				stages == 2 ? 0.5412 : M_SQRT1_2);
			lowpass(&stage[ch][1], fc, r->fmt.rate, 1.3066);
		}
	}
	for (i = 0; i < r->frames; i++) {
		double rate = r->role == ET_REC2 ?
				      r->fmt.rate * (1 + REC2_PPM * 1e-6) :
				      r->fmt.rate;
		double t = r->start + 0.001 + i / rate, v[2] = { 0, 0 };
		double hz, len;
		int ch, side[2] = { 0, 0 };

		if (r->role == ET_FMC) {
			int dig = a2 && ctl("Audio 2 To FM Playback Switch", 0, 0);
			double u = dig ? t - a2->start - 0.0013 : -1;
			long k;

			k = u >= 0 ? (long)floor(u * ET_FM_RATE) : -1;
			// a pause holds the frames back, and the next frame
			// plays after it
			if (dig && a2->pause_at > 0 && u >= a2->pause_at)
				k = u < a2->pause_at + a2->pause_for ?
					    -1 :
					    (long)ceil(a2->pause_at * ET_FM_RATE) +
						    (long)floor((u - a2->pause_at -
								 a2->pause_for) *
								ET_FM_RATE);

			if (sim.faults & SIM_LOSS)
				k += k / 10000;
			// a buffer played again, with no xrun
			if (a2 && (sim.faults & SIM_TWO_PERIODS) && k > 20000 &&
			    a2->buffer_got == 2 * a2->period_got)
				k -= a2->buffer_got;
			if (dig && k >= 0 && k < (long)a2->frames &&
			    !(sim.faults & SIM_A2_SILENT)) {
				int out[2];

				et_dig_frame(&a2->fmt, a2->data, k, out);
				v[0] = out[0] - ET_FM_IDLE;
				v[1] = out[1] - ET_FM_IDLE;
			} else if (dig && (sim.faults & SIM_RUNOUT_STALE) &&
				   (a2->flags & ET_RUNOUT) &&
				   k >= (long)a2->frames &&
				   k < (long)(a2->frames + a2->buffer_got)) {
				// the ring goes round on what it held
				int out[2];

				et_dig_frame(&a2->fmt, a2->data,
					     k - a2->buffer_got, out);
				v[0] = out[0] - ET_FM_IDLE;
				v[1] = out[1] - ET_FM_IDLE;
			} else if (dig && (sim.faults & SIM_DRAIN_STALE) &&
				   a2->frames > a2->buffer_got &&
				   a2->frames % a2->period_got &&
				   k >= (long)a2->frames &&
				   k < (long)(a2->frames + a2->period_got -
					      a2->frames % a2->period_got)) {
				// past the end, to the period's end, what the
				// ring held a buffer before
				int out[2];

				et_dig_frame(&a2->fmt, a2->data,
					     k - a2->buffer_got, out);
				v[0] = out[0] - ET_FM_IDLE;
				v[1] = out[1] - ET_FM_IDLE;
			}
			if (cmd && fm_note(cmd, &hz, side, &len) &&
			    t > cmd->start + 0.01 && t < cmd->start + len)
				for (ch = 0; ch < 2; ch++)
					v[ch] += side[ch] * 8000 *
						 sin(2 * M_PI * hz * t);
			for (ch = 0; ch < 2; ch++)
				et_pack1(&r->fmt, r->data, i, ch,
					 v[ch] + ET_FM_IDLE);
			continue;
		}
		if (r->role == ET_A1C) {
			long src = ctl("Capture Source", 0, 0);
			double a2v2[2];
			s16 fx_in[2], fx_out[2] = { 0, 0 };

			int note = cmd && fm_note(cmd, &hz, side, &len) &&
				   t > cmd->start + 0.01 && t < cmd->start + len;

			for (ch = 0; ch < 2; ch++) {
				a2v2[ch] = a2_out(a2, t, ch);
				if (stages)
					a2v2[ch] = biquad(&stage[ch][0], a2v2[ch]);
				if (stages == 2)
					a2v2[ch] = biquad(&stage[ch][1], a2v2[ch]);
			}
			// source 7 through the 3-D effect, Audio 2 at its
			// volume before it
			if (src7) {
				for (ch = 0; ch < 2; ch++)
					fx_in[ch] = clip16(ctl_gain("PCM Playback Volume",
								    0, ch) *
							   a2v2[ch]);
				s3dsim_run(&fx, fx_in, fx_out, 1);
			}
			for (ch = 0; ch < 2; ch++) {
				// 5 the record mixer, 7 the output before the
				// master's volume, after its switch
				double a2v = a2v2[ch];
				double in;

				in = src == 5 ?
					ctl_gain("PCM Capture Volume", 0, ch) *
							a2v +
						(note ? ctl_gain("FM Capture Volume",
								 0, ch) *
								side[ch] * 8000 *
								sin(2 * M_PI * hz * t) :
							0) :
				    src == 7 && ctl("Master Playback Switch", 0, 0) ?
					2.0 * fx_out[ch] :
				    // 3 AOUT: after the master, unless the
				    // record monitor puts the record source,
				    // AOUT itself, there
				    src == 3 && ctl("Master Playback Switch", 0, 0) &&
					    !ctl("Record Monitor", 0, 0) ?
					ctl_gain("Master Playback Volume", 0, ch) *
						ctl_gain("PCM Playback Volume", 0,
							 ch) *
						a2v :
					0;

				v[ch] = ctl_gain("Capture Volume", 0,
						 ch ^ ((sim.faults & SIM_B4_SWAP) != 0)) *
					in;
				// after the level, as the chip's own offset
				v[ch] += 64 * ctl("Capture DC Offset", 0, ch) +
					 (sim.faults & SIM_ADC_DC ? 200 : 8);
			}
		} else {
			double m = ctl("Master Playback Switch", 0, 0) ?
					   ctl_gain("Master Playback Volume", 0, 0) :
					   0;
			int a1_on = a1 != NULL;
			double l, a1t = t;

			// off the mixer since a resume, or the FIFO lost
			if (a1 && (sim.faults & SIM_A1_RESUME) && cmd &&
			    strstr(cmd->cmd, "--suspend") && t > cmd->start)
				a1_on = 0;
			if (a1 && (sim.faults & SIM_A1_DRAIN) &&
			    t > a1->start + (a1->frames - 256.0) / a1->fmt.rate)
				a1_on = 0;
			// the pause when the last frame is written, or the
			// buffer it dropped
			if (a1 && (a1->flags & ET_PAUSEDRAIN) &&
			    t > a1->start + 0.5) {
				a1t = t - 0.2 > a1->start + 0.5 ? t - 0.2 :
								  a1->start + 0.5;
				if ((sim.faults & SIM_A1_PAUSEDRAIN) &&
				    a1t > a1->start + 0.6)
					a1_on = 0;
			}
			l = m * (ctl_gain("PCM Playback Volume", 0, 0) *
					 a2_out(a2, t, 0) +
				 ctl_gain("PCM Playback Volume", 1, 0) *
					 (a1_on ? at(a1, a1t, a1->fmt.rate, 0) : 0));

			// dual: channel 1 on Audio 1, channel 3 on Audio 2
			if (dual)
				l += m * (ctl_gain("PCM Playback Volume", 1, 0) *
						  at(dual, t, dual->fmt.rate, 0) +
					  ctl_gain("PCM Playback Volume", 0, 0) *
						  at(dual, t, dual->fmt.rate, 2));

			// the PC speaker, which the master doesn't reach
			if (cmd && strstr(cmd->cmd, "--tone") && t > cmd->start &&
			    t < cmd->start + 0.4)
				l += (sim.faults & SIM_BEEP ?
					      7 :
					      ctl("Beep Playback Volume", 0, 0)) *
				     1200 * sin(2 * M_PI * 1000 * t);
			// a recording with the record monitor on: AOUT
			// carries the record source, here silent
			if (rec_cmd && ctl("Record Monitor", 0, 0) &&
			    t > rec_cmd->start &&
			    fmod(t - rec_cmd->start, 1.0) < 0.75 &&
			    t < rec_cmd->start + 8)
				l = 0;
			v[0] = v[1] = 0.5 * l;
		}
		for (ch = 0; ch < (int)r->fmt.channels && ch < 2; ch++) {
			double u = (an_lfsr(&seed) / 65536.0 - 0.5) * 3.46;

			et_pack1(&r->fmt, r->data, i, ch, v[ch] + noise * u);
		}
	}
	// the start race: the stream a byte late
	if ((sim.faults & SIM_A1_BYTE_OFF) && r->role == ET_A1C) {
		unsigned long nb = r->frames * et_fmt_bytes(&r->fmt);

		memmove(r->data, (char *)r->data + 1, nb - 1);
	}
}

static int sim_run(void *ctx, struct et_stream *s, int n)
{
	double end = 0;
	int i;

	(void)ctx;
	sim.mute_at = -1;
	for (i = 0; i < n; i++) {
		struct et_stream *x = &s[i];

		x->err = 0;
		x->xruns = 0;
		x->t_trigger = x->start + 0.001;
		x->period_got = x->period ? x->period : 1024;
		x->buffer_got = x->buffer ? x->buffer : 4096;
		x->delay0 = x->capture ? 0 :
					 (long)x->buffer_got +
						 (sim.faults & SIM_NO_FIFO ? 0 : 16);
		if (x->role == ET_CMD && strstr(x->cmd, "--suspend")) {
			x->status = 0;
			x->t_end = x->start + 5.5;
		} else if (x->role == ET_CMD && (strstr(x->cmd, "aplay -q") ||
						  strstr(x->cmd, "arecord -q"))) {
			x->status = 0;
			x->t_end = x->start + 8.5;
			// the first close of Audio 1
			if (sim.faults & SIM_A1_MUTES)
				sim.mute_at = x->start + 0.6;
		} else if (x->role == ET_CMD && strstr(x->cmd, "--hammer")) {
			x->status = 0;
			x->t_end = x->start + atof(strstr(x->cmd, "--hammer") + 8);
		} else if (x->role == ET_CMD) {
			double hz, len;
			int side[2];

			x->status = fm_note(x, &hz, side, &len) ? 0 : 1;
			x->t_end = x->start + (x->status ? 0 : len + 0.2);
		} else {
			double rate = x->role == ET_A2 && !x->capture ?
					      a2_rate(x) :
					      x->fmt.rate;

			x->done = x->frames;
			x->t_end = x->t_trigger + x->frames / rate +
				   (x->pause_at > 0 ? x->pause_for : 0);
			if ((x->flags & ET_STALL) && x->pause_at > 0 &&
			    x->pause_for * x->fmt.rate > x->buffer_got)
				x->xruns = 1;
			if (x->probe_at > 0) {
				double played = x->probe_at * rate;

				x->probe_t = x->t_trigger + x->probe_at;
				x->probe_done = (unsigned long)played +
						x->buffer_got;
				if (x->probe_done > x->frames)
					x->probe_done = x->frames;
				x->probe_avail = x->buffer_got -
						 (x->probe_done - (long)played);
				x->probe_delay = x->probe_done - (long)played +
						 (sim.faults & SIM_NO_FIFO ? 0 :
									     16);
			}
		}
		if (x->t_end > end)
			end = x->t_end;
	}
	for (i = 0; i < n; i++)
		if (s[i].capture)
			fill(&s[i], s, n);
	// a suspend in the run stops every stream once, and they go on
	for (i = 0; i < n; i++)
		if (s[i].role == ET_CMD && strstr(s[i].cmd, "--suspend")) {
			int j;

			for (j = 0; j < n; j++)
				if (s[j].role != ET_CMD)
					s[j].xruns++;
			if (sim.faults & SIM_RESUME)
				sim.lost52 = 1;
		}
	for (i = 0; i < n; i++)
		if (s[i].role == ET_A1P || s[i].role == ET_A2)
			sim.irqs[s[i].role == ET_A2] +=
				s[i].frames / (s[i].period_got ? s[i].period_got : 1);
	sim.now += end + 0.05;
	return 0;
}

static int sim_has(void *ctx, enum et_role r)
{
	(void)ctx;
	return r != ET_DUAL || param("dual_playback");
}

static const char *sim_rec2(void *ctx)
{
	(void)ctx;
	return "sim:rec2";
}

static int hwcount_ctl(const char *name, int index)
{
	return !index && !strcmp(name, "Hardware Master Playback Volume");
}

static int sim_ctl_info(void *ctx, const char *name, int index,
			struct et_ctl_info *info)
{
	int i = find_ctl(name, index);
	const struct et_setting *s;
	const char *p;

	(void)ctx;
	if (hwcount_ctl(name, index)) {
		memset(info, 0, sizeof(*info));
		info->type = ET_CTL_INT;
		info->count = 2;
		info->max = 63;
		return 0;
	}
	if (i < 0)
		return -ENOENT;
	s = &et_settings[i];
	memset(info, 0, sizeof(*info));
	info->count = et_setting_count(s);
	info->min = s->min;
	info->max = s->max;
	if (s->kind == S_BOOL || s->kind == S_MASTERSW) {
		info->type = ET_CTL_BOOL;
	} else if (s->kind == S_ENUM) {
		info->type = ET_CTL_ENUM;
		info->items = 1;
		for (p = s->items; *p; p++)
			info->items += *p == '|';
	} else {
		info->type = ET_CTL_INT;
	}
	return 0;
}

static int sim_ctl_get(void *ctx, const char *name, int index, long *v,
		       int n)
{
	int i = find_ctl(name, index), j, count;

	(void)ctx;
	if (hwcount_ctl(name, index)) {
		for (j = 0; j < 2 && j < n; j++)
			v[j] = sim.hwcount[j];
		return j;
	}
	if (i < 0)
		return -ENOENT;
	count = et_setting_count(&et_settings[i]);
	for (j = 0; j < count && j < n; j++)
		v[j] = sim.val[i][j];
	return j;
}

static int sim_ctl_set(void *ctx, const char *name, int index, const long *v,
		       int n)
{
	int i = find_ctl(name, index), j, count;

	(void)ctx;
	if (i < 0)
		return -ENOENT;
	count = et_setting_count(&et_settings[i]);
	for (j = 0; j < count; j++) {
		long x = v[j < n ? j : n - 1];

		if (x < et_settings[i].min || x > et_settings[i].max)
			return -EINVAL;
		sim.val[i][j] = x;
	}
	if (count == 1)
		sim.val[i][1] = sim.val[i][0];
	if (et_settings[i].reg == 0x52 && et_settings[i].bank == B_MIX)
		sim.lost52 = 0;
	return count;
}

static int sim_ctl_enum_name(void *ctx, const char *name, int index, int item,
			     char *buf, int len)
{
	int i = find_ctl(name, index), n = 0;
	const char *p, *e;

	(void)ctx;
	if (i < 0)
		return -ENOENT;
	for (p = et_settings[i].items; p; p = e ? e + 1 : NULL, n++) {
		e = strchr(p, '|');
		if (n == item) {
			snprintf(buf, len, "%.*s",
				 (int)(e ? e - p : (long)strlen(p)), p);
			return 0;
		}
	}
	return -EINVAL;
}

static int sim_ctl_db(void *ctx, const char *name, int index, long value,
		      long *cdb)
{
	int i = find_ctl(name, index);

	(void)ctx;
	if (i < 0 || !et_settings[i].tlv)
		return -ENOENT;
	*cdb = tlv_cdb(et_settings[i].tlv, value);
	return 0;
}

static int sim_ctl_nth(void *ctx, int nth, char *name, int len, int *index)
{
	int i, n = 0;

	(void)ctx;
	for (i = 0; i < et_nsettings; i++)
		if ((et_settings[i].flags & F_CTL) && n++ == nth) {
			snprintf(name, len, "%s", et_settings[i].ctl);
			*index = et_settings[i].index;
			return 0;
		}
	return -ENOENT;
}

static int sim_param_get(void *ctx, const char *id, char *buf, int len)
{
	int i = find_id(id);

	(void)ctx;
	if (i < 0 || !(et_settings[i].flags & F_PARAM))
		return -ENOENT;
	if (et_settings[i].kind == S_STR)
		return snprintf(buf, len, "%s", et_settings[i].items);
	if (et_setting_count(&et_settings[i]) == 2)
		return snprintf(buf, len, "%ld,%ld", sim.val[i][0],
				sim.val[i][1]);
	return snprintf(buf, len, "%ld", sim.val[i][0]);
}

static int sim_param_set(void *ctx, const char *id, const char *val)
{
	int i = find_id(id), j, n;
	long v[2];
	char *end;

	(void)ctx;
	if (i < 0 || !(et_settings[i].flags & F_PARAM))
		return -ENOENT;
	if (et_settings[i].kind == S_STR)
		return 0;
	v[0] = strtol(val, &end, 0);
	if (end == val)
		return -EINVAL;
	n = 1;
	if (*end == ',') {
		v[1] = strtol(end + 1, &end, 0);
		n = 2;
	}
	if (*end && *end != '\n')
		return -EINVAL;
	if (n == 1)
		v[1] = v[0];
	for (j = 0; j < 2; j++)
		if (v[j] < et_settings[i].min || v[j] > et_settings[i].max)
			return -ERANGE;
	sim.val[i][0] = v[0];
	sim.val[i][1] = v[1];
	return 0;
}

// a mixer or controller register as the simulated chip holds it: the
// settings.def rows that live in it, at their values
static int reg_value(int bank, int reg)
{
	int i, v = 0;

	for (i = 0; i < et_nsettings; i++) {
		const struct et_setting *t = &et_settings[i];
		long a = sim.val[i][0], b = sim.val[i][1], x;
		int mask = (1 << t->width) - 1;

		if (t->bank != bank)
			continue;
		if (t->kind == S_STEREO && t->reg == reg) {
			v |= t->flags & F_SWAP ? b << 4 | a : a << 4 | b;
		} else if ((t->kind == S_MASTER || t->kind == S_MASTERSW) &&
			   (reg == t->reg || reg == t->reg + 2)) {
			x = reg == t->reg ? a : b;
			if (t->kind == S_MASTERSW)
				x = !x;
			v |= (x & mask) << t->shift;
		} else if (t->reg == reg && t->kind != S_SMAG &&
			   t->kind != S_STR && t->kind != S_STEREO &&
			   t->kind != S_MASTER && t->kind != S_MASTERSW) {
			x = t->flags & F_INV ? !a : a;
			// the 3-D switch sets bits 3:2 together, 01 when off
			if ((t->flags & F_HOOK) && bank == B_MIX && reg == 0x50)
				v |= a ? 0x0C : 0x04;
			else
				v |= (x & mask) << t->shift;
		}
	}
	if (bank == B_MIX && reg == 0x52 && sim.lost52)
		v = 0;
	return v & 0xFF;
}

static int sim_reg_read(void *ctx, int bank, int reg, int *val)
{
	(void)ctx;
	*val = bank == 0 || bank == 1 ? reg_value(bank ? B_CTRL : B_MIX, reg) :
					0;
	return 0;
}

static int sim_info(void *ctx, uint32_t *hash, uint32_t *irqs)
{
	(void)ctx;
	*hash = et_settings_hash();
	if (irqs)
		memcpy(irqs, sim.irqs, sizeof(sim.irqs));
	return 0;
}

// the buttons, as the chip moves the master (or 61h and 63h with the
// split) and the driver's control follows it
static void sim_ask_press(void *ctx, int button)
{
	int vol = find_ctl("Master Playback Volume", 0);
	int sw = find_ctl("Master Playback Switch", 0);
	long step = param("hwvol_count3") ? 3 : 1;
	int ch;

	(void)ctx;
	sim.irqs[2]++;
	sim.now += 1;
	if (ctl("Hardware Master Volume Split", 0, 0)) {
		for (ch = 0; ch < 2; ch++)
			sim.hwcount[ch] += button == ET_BTN_UP ? step :
					   button == ET_BTN_DOWN ? -step : 0;
		return;
	}
	if (sim.faults & SIM_HWVOL)
		return;
	for (ch = 0; ch < 2; ch++)
		if (button == ET_BTN_MUTE)
			sim.val[sw][ch] = !sim.val[sw][ch];
		else
			sim.val[vol][ch] += button == ET_BTN_UP ? step : -step;
}

static int sim_suspend(void *ctx)
{
	(void)ctx;
	sim.now += 6;
	if (sim.faults & SIM_RESUME)
		sim.lost52 = 1;
	return 0;
}

// a fresh load: every setting at its default, then the parameters given
static int sim_reload(void *ctx, const char *params)
{
	char buf[256], *p, *save = NULL;
	int i;

	(void)ctx;
	for (i = 0; i < et_nsettings && i < 256; i++)
		sim.val[i][0] = sim.val[i][1] = et_settings[i].def;
	sim.lost52 = 0;
	snprintf(buf, sizeof(buf), "%s", params);
	for (p = strtok_r(buf, " ", &save); p && !(sim.faults & SIM_RELOAD);
	     p = strtok_r(NULL, " ", &save)) {
		char *eq = strchr(p, '=');

		if (eq) {
			*eq = 0;
			sim_param_set(ctx, p, eq + 1);
		}
	}
	snprintf(sim.klog, sizeof(sim.klog),
		 "6 snd_es1869 es1869.0: ESS AudioDrive ES1869 at 0x220, irq 5, dma 1, 5\n");
	sim.now += 2;
	return 0;
}

static int sim_klog(void *ctx, char *buf, int len)
{
	int n = 0;

	(void)ctx;
	if (len)
		buf[0] = 0;
	if (sim.faults & SIM_KLOG) {
		snprintf(sim.klog + strlen(sim.klog),
			 sizeof(sim.klog) - strlen(sim.klog),
			 "3 snd_es1869 es1869.0: DSP command C6h timed out\n");
	}
	if (sim.klog[0] && len) {
		const char *p;

		snprintf(buf, len, "%s", sim.klog);
		for (p = sim.klog; *p; p++)
			n += *p == '\n';
	}
	sim.klog[0] = 0;
	return n;
}

static int sim_type_f(void *ctx)
{
	(void)ctx;
	return 0;
}

static int sim_proc(void *ctx, char *buf, int len)
{
	(void)ctx;
	// the cost as a stream of 10 s at 21.5 interrupts a second leaves it
	return snprintf(buf, len,
			"ES1869 (simulated)\ninterrupts: Audio 1 0, Audio 2 0, buttons 0, MPU-401 0, none 0\n"
			"cost: interrupts %lu, 20000 cycles each; ring ticks %lu, 4000 cycles each; 8237 readings 0\n"
			"clock: the master %+d ppb of 23814000 Hz on the raw clock, 257 samples in the line, 300 in all, from Audio 2\n"
			"clock: the steered clock %+d ppb of the raw one, 300 samples\n",
			(unsigned long)(sim.now * 21.5),
			(unsigned long)(sim.now * 75), SIM_MASTER_PPB, 0);
}

static int sim_cpu(void *ctx, double *busy, double *total)
{
	(void)ctx;
	*busy = sim.now * 0.002;
	*total = sim.now;
	return 0;
}

static double sim_cpu_hz(void *ctx)
{
	(void)ctx;
	return 400e6;
}

static double sim_now(void *ctx)
{
	(void)ctx;
	return sim.now;
}

static void sim_sleep(void *ctx, double s)
{
	(void)ctx;
	sim.now += s;
}

struct et_hal *et_hal_sim(unsigned int faults)
{
	static struct et_hal h = {
		.name = "simulated",
		.ctx = &sim,
		.run = sim_run,
		.has = sim_has,
		.rec2 = sim_rec2,
		.ctl_info = sim_ctl_info,
		.ctl_get = sim_ctl_get,
		.ctl_set = sim_ctl_set,
		.ctl_enum_name = sim_ctl_enum_name,
		.ctl_db = sim_ctl_db,
		.ctl_nth = sim_ctl_nth,
		.param_get = sim_param_get,
		.param_set = sim_param_set,
		.reg_read = sim_reg_read,
		.info = sim_info,
		.proc = sim_proc,
		.cpu = sim_cpu,
		.cpu_hz = sim_cpu_hz,
		.now = sim_now,
		.sleep = sim_sleep,
		.ask_press = sim_ask_press,
		.suspend = sim_suspend,
		.reload = sim_reload,
		.klog = sim_klog,
		.type_f = sim_type_f,
	};
	int i;

	memset(&sim, 0, sizeof(sim));
	sim.faults = faults;
	for (i = 0; i < et_nsettings && i < 256; i++)
		sim.val[i][0] = sim.val[i][1] = et_settings[i].def;
	return &h;
}
