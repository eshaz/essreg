// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * chain.c: es1869scope attached to es1869eq (chain.h). Notes:
 *
 * The service holds the devices and measures. The viewer runs as the user at
 * the console, maps the state that the engine writes (eqstate.h), and opens
 * no audio device. It asks the engine to watch every few seconds, which keeps
 * the state's live parts coming, and switches the settings that change the
 * sound through their ALSA controls, as the user, with the names, items and
 * descriptions of the driver's settings table (linux/test/settab.c).
 *
 * The chain view lists what the audio goes through, from Audio 2's stream to
 * the line out and back on Audio 1, each stage with what it does now and the
 * key that switches it, and beside the list the selected stage's picture of
 * what it does to the sound. A switch reads "pending" until the control and
 * the engine's state agree with it, and "not taken" if they don't within a
 * few seconds. The first switch of the sound holds the engine's refits and
 * trim, so that nothing else moves during a comparison. At its end the viewer
 * puts back the switches as they were when it attached, and ends that hold,
 * unless K keeps them. 3x asks the engine to rest Audio 1 first, since the
 * DAC stalls while Audio 1 records.
 *
 * Drawing is what the Celeron feels: the chain's panels are drawn again at
 * most twice a second, and the spectrogram a row at a time.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "chain.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include "../../test/settab.h"
#include "ctl.h"
#include "eq.h"
#include "eqstate.h"
#include "plot.h"
#include "say.h"
#include "spectro.h"

#define CH_WATCH_S 4 // a watch asked for again this often
#define CH_ALIVE_S 2 // the engine's lock looked at this often
#define CH_PENDING_S 5 // a switch not taken by then is said so
#define CH_DRAW_NS 500000000LL // the panels drawn again at most this often
#define CH_MASTER_HZ 23814000.0 // the chip's master clock, the irqs' ticks
#define CH_LAST 0.062 // the DAC's share of the last sample (treble.h)
#define CH_AMBER 0xffcf5a
#define CH_SEL 0x24303c
#define CH_LIST_W 37 // the list's width in characters

static const uint32_t ch_rgb[2] = { 0x5aa9ff, 0xffa64d }; // left, right

enum {
	ST_STREAM,
	ST_RATE,
	ST_DITHER,
	ST_TREBLE,
	ST_OVER,
	ST_ZSTEP,
	ST_DAC,
	ST_LIVE,
	ST_A1C,
	ST_ENGINE,
	ST_N
};

// the stages, in the order the audio meets them: name, key, the setting
// it switches, and whether that changes what is heard
static const struct ch_stage {
	const char *name;
	int key;
	const char *id;
	int sound;
} ch_stages[ST_N] = {
	{ "Audio 2's stream", 0, NULL, 0 },
	{ "rate correction", 'r', "a2_resample", 1 },
	{ "rounding dither", 'd', "a2_resample_dither", 1 },
	{ "treble correction", 't', "a2_treble", 1 },
	{ "output rate", 'o', "a2_oversample", 1 },
	{ "zero step", 'z', "a2_zstep", 1 },
	{ "the DAC's own stages", 0, "audio2_oversampling", 0 },
	{ "live corrections", 'h', NULL, 0 },
	{ "Audio 1's correction", 'a', "a1c_filter", 0 },
	{ "the engine", 0, NULL, 0 },
};

struct chain {
	const struct eqs_state *map;
	struct eqs_state *st; // the last whole copy
	int have, alive, pid;
	char path[256], cmd[256], card[40];
	snd_ctl_t *ctl;
	int view, sel, keep, held_by_us, frozen, dirty;
	uint32_t num, seq;
	long long watch_at, alive_at, copy_at, drawn_at, stream_at;
	const struct et_setting *set[ST_N];
	long orig[ST_N], want[ST_N];
	int saved[ST_N], pending[ST_N];
	long long pending_at[ST_N];
	uint32_t rows[EQS_SOURCES]; // each source's rows drawn
	uint32_t cmap[256];
	char stream[120]; // Audio 2's stream as /proc has it
};

static long long ch_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static const struct et_setting *ch_setting(const char *id)
{
	int i;

	for (i = 0; id && i < et_nsettings; i++)
		if (!strcmp(et_settings[i].id, id))
			return &et_settings[i];
	return NULL;
}

// a setting's item by its number, as settings.def names it
static void ch_item(const struct et_setting *s, long v, char *out, size_t size)
{
	const char *p;
	long i;

	if (!s || s->kind == S_BOOL) {
		snprintf(out, size, "%s", v ? "on" : "off");
		return;
	}
	if (s->kind != S_ENUM || !s->items) {
		snprintf(out, size, "%ld", v);
		return;
	}
	for (p = s->items, i = 0; i < v && p; i++) {
		p = strchr(p, '|');
		if (p)
			p++;
	}
	if (!p) {
		snprintf(out, size, "%ld", v);
		return;
	}
	snprintf(out, size, "%.*s", (int)strcspn(p, "|"), p);
}

// a stage's control's value now: 0, or -1 without one
static int ch_get(struct chain *w, int st, long *v)
{
	const struct et_setting *s = w->set[st];
	long val[2];
	unsigned int n;

	if (!w->ctl || !s || !s->ctl || ctl_read(w->ctl, s->ctl, val, &n))
		return -1;
	*v = val[0];
	return 0;
}

static long ch_ctl(struct chain *w, const char *name, long dflt)
{
	long val[2];
	unsigned int n;

	return w->ctl && !ctl_read(w->ctl, name, val, &n) ? val[0] : dflt;
}

// a command to the engine, its number, or 0 when no engine reads them
static uint32_t ch_send(struct chain *w, const char *word)
{
	uint32_t n = ++w->num;
	int err = eqs_send(w->cmd, word, n);

	if (!err)
		return n;
	if (w->alive)
		say("the engine doesn't read its commands: %s", strerror(-err));
	return 0;
}

static int ch_copy(struct chain *w)
{
	if (eqs_copy(w->map, w->st))
		return 0;
	w->have = 1;
	if (w->st->seq == w->seq)
		return 0;
	w->seq = w->st->seq;
	return 1;
}

struct chain *chain_open(const char *path, const char *ctl, int alive)
{
	struct chain *w = calloc(1, sizeof(*w));
	const char *c;
	char *slash;
	int err, i;

	if (!w || !(w->st = calloc(1, sizeof(*w->st)))) {
		free(w);
		return NULL;
	}
	snprintf(w->path, sizeof(w->path), "%s", path);
	snprintf(w->cmd, sizeof(w->cmd), "%s", path);
	slash = strrchr(w->cmd, '/');
	snprintf(slash ? slash + 1 : w->cmd,
		 sizeof(w->cmd) - (size_t)(slash ? slash + 1 - w->cmd : 0),
		 "eq.cmd");
	err = eqs_map(path, &w->map);
	if (err) {
		say("%s: %s", path,
		    err == -ESTALE ? "a state of another layout" :
				     strerror(-err));
		goto bad;
	}
	w->pid = eqs_alive(path);
	w->alive = w->pid != 0;
	if (alive && !w->alive) {
		say("%s: the engine that wrote it isn't running", path);
		goto bad;
	}
	if (ctl && snd_ctl_open(&w->ctl, ctl, SND_CTL_NONBLOCK) >= 0)
		snd_ctl_subscribe_events(w->ctl, 1);
	else
		w->ctl = NULL;
	c = ctl ? strstr(ctl, "CARD=") : NULL;
	if (c)
		snprintf(w->card, sizeof(w->card), "%.*s",
			 (int)strcspn(c + 5, ",:"), c + 5);
	for (i = 0; i < ST_N; i++)
		w->set[i] = ch_setting(ch_stages[i].id);
	spectro_map(w->cmap);
	w->view = CHAIN_VIEW;
	w->sel = ST_STREAM;
	ch_copy(w);
	if (w->alive)
		ch_send(w, "watch");
	w->watch_at = w->alive_at = ch_now();
	w->dirty = 1;
	return w;
bad:
	if (w->map)
		munmap((void *)w->map, sizeof(*w->map));
	free(w->st);
	free(w);
	return NULL;
}

void chain_close(struct chain *w)
{
	char item[48];
	long v[2];
	int st;

	if (!w)
		return;
	// as it was at attach, unless kept
	for (st = 0; !w->keep && st < ST_N; st++) {
		if (!w->saved[st] || ch_get(w, st, v) || v[0] == w->orig[st])
			continue;
		v[0] = v[1] = w->orig[st];
		ch_item(w->set[st], w->orig[st], item, sizeof(item));
		if (!ctl_write(w->ctl, w->set[st]->ctl, v, 1))
			say("%s back to %s", ch_stages[st].name, item);
	}
	if (w->held_by_us && !w->keep && w->alive)
		ch_send(w, "resume");
	if (w->ctl)
		snd_ctl_close(w->ctl);
	munmap((void *)w->map, sizeof(*w->map));
	free(w->st);
	free(w);
}

void chain_view(struct chain *w, int view)
{
	if (view >= CHAIN_CLOCK && view <= CHAIN_VIEW)
		w->view = view;
	w->dirty = 1;
}

void chain_select(struct chain *w, int stage)
{
	if (stage >= 0 && stage < ST_N)
		w->sel = stage;
	w->dirty = 1;
}

// --- switching -----------------------------------------------------------------

// the engine's ack of command n, waited for up to two seconds
static int ch_acked(struct chain *w, uint32_t n)
{
	int i;

	for (i = 0; i < 100; i++) {
		if (ch_copy(w) >= 0 && w->have && w->st->ack == n)
			return 1;
		usleep(20000);
	}
	return 0;
}

static void ch_switch(struct chain *w, int st)
{
	const struct et_setting *s = w->set[st];
	char item[48];
	long v[2], next;
	uint32_t n;

	if (ch_get(w, st, v)) {
		say("%s: no control here to switch", ch_stages[st].name);
		return;
	}
	next = s->kind == S_ENUM ? (v[0] + 1) % (s->max + 1) : !v[0];
	// the first switch of the sound holds the refits and the trim, so
	// that only the switch changes what is heard
	if (ch_stages[st].sound && w->alive && w->have && !w->st->hold &&
	    !w->held_by_us && ch_send(w, "hold"))
		w->held_by_us = 1;
	if (!w->saved[st]) {
		w->orig[st] = v[0];
		w->saved[st] = 1;
	}
	// 3x: Audio 1 rests first, since the DAC stalls while it records
	if (st == ST_OVER && next == 2 && w->alive) {
		n = ch_send(w, "rest");
		if (!n || !ch_acked(w, n)) {
			say("the engine didn't rest Audio 1: 3x waits");
			return;
		}
	}
	v[0] = v[1] = next;
	ch_item(s, next, item, sizeof(item));
	if (ctl_write(w->ctl, s->ctl, v, 1)) {
		say("%s: the driver refused %s", ch_stages[st].name, item);
		return;
	}
	w->want[st] = next;
	w->pending[st] = 1;
	w->pending_at[st] = ch_now();
	w->sel = st;
	w->dirty = 1;
	say("%s: %s", ch_stages[st].name, item);
}

// a switch taken: the control as asked, and where the engine applies it,
// the engine's state too
static void ch_pending(struct chain *w, long long now)
{
	long v;
	int st, taken;

	for (st = 0; st < ST_N; st++) {
		if (!w->pending[st] || ch_get(w, st, &v))
			continue;
		taken = v == w->want[st];
		if (taken && st == ST_OVER && w->alive)
			taken = w->have && w->st->factor == (uint32_t)v + 1;
		if (taken && st == ST_A1C && w->alive)
			taken = w->have && w->st->a1c == (uint32_t)v;
		if (taken) {
			w->pending[st] = 0;
			w->dirty = 1;
		} else if (now - w->pending_at[st] >
			   CH_PENDING_S * 1000000000LL) {
			w->pending[st] = 0;
			w->dirty = 1;
			say("%s: not taken%s%s", ch_stages[st].name,
			    w->have && w->st->why[0] ? ": " : "",
			    w->have ? w->st->why : "");
		}
	}
}

static int ch_key(struct chain *w, int key)
{
	int st;

	if (key == 'q')
		return -1;
	if (key >= '1' && key <= '5') {
		chain_view(w, key - '0');
		return 0;
	}
	if (key == CHAIN_UP || key == 'k') {
		w->sel = (w->sel + ST_N - 1) % ST_N;
		w->dirty = 1;
	} else if (key == CHAIN_DOWN || key == 'j') {
		w->sel = (w->sel + 1) % ST_N;
		w->dirty = 1;
	} else if (key == ' ') {
		w->frozen = !w->frozen;
	} else if (key == 'R' || key == 12) {
		w->dirty = 1;
	} else if (key == 'K') {
		w->keep = 1;
		say("the switches and the hold stay as they are after the "
		    "viewer");
	} else if (key == 'h') {
		w->sel = ST_LIVE;
		if (!w->alive || !w->have)
			say("no engine to hold");
		else if (w->st->hold && ch_send(w, "resume"))
			w->held_by_us = 0;
		else if (!w->st->hold)
			ch_send(w, "hold");
		w->dirty = 1;
	} else {
		for (st = 0; st < ST_N; st++)
			if (ch_stages[st].key == key && ch_stages[st].id)
				ch_switch(w, st);
	}
	return 0;
}

// --- what each stage says ------------------------------------------------------

// Audio 2's stream from /proc, which the core keeps without the chip
static void ch_stream(struct chain *w, long long now)
{
	char path[96], line[80], fmt[24] = "", st[24] = "";
	unsigned int rate = 0, ch = 0;
	FILE *f;

	if (now - w->stream_at < 1000000000LL)
		return;
	w->stream_at = now;
	snprintf(w->stream, sizeof(w->stream), "closed");
	if (!w->card[0])
		return;
	snprintf(path, sizeof(path), "/proc/asound/%s/pcm0p/sub0/hw_params",
		 w->card);
	f = fopen(path, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		sscanf(line, "rate: %u", &rate);
		sscanf(line, "format: %23s", fmt);
		sscanf(line, "channels: %u", &ch);
	}
	fclose(f);
	snprintf(path, sizeof(path), "/proc/asound/%s/pcm0p/sub0/status",
		 w->card);
	f = fopen(path, "r");
	if (f) {
		while (fgets(line, sizeof(line), f))
			sscanf(line, "state: %23s", st);
		fclose(f);
	}
	if (rate)
		snprintf(w->stream, sizeof(w->stream), "%u Hz %s %u ch, %s",
			 rate, fmt, ch, st[0] ? st : "open");
}

static const char *ch_state(const struct eqs_state *s)
{
	switch (s->state) {
	case EQS_STARTING:
		return "starting";
	case EQS_FINDING:
		return "finding the lag";
	case EQS_MEASURING:
		return "measuring";
	case EQS_RESTING:
		return "resting: nothing plays";
	case EQS_A1_RESTS:
		return "Audio 1 rests";
	default:
		return "stopped";
	}
}

static const struct eqs_block *ch_last(const struct eqs_state *s)
{
	return s->hist_count ? &s->hist[(s->hist_next - 1) % EQS_HIST] : NULL;
}

// a stage's line under its name
static void ch_says(struct chain *w, int st, char *out, size_t size)
{
	const struct eqs_state *s = w->st;
	const struct eqs_block *b = w->have ? ch_last(s) : NULL;
	char item[48] = "-";
	long v = 0;
	int known = w->set[st] && !ch_get(w, st, &v);

	if (known)
		ch_item(w->set[st], v, item, sizeof(item));
	switch (st) {
	case ST_STREAM:
		snprintf(out, size, "%s", w->stream);
		break;
	case ST_RATE:
		if (b && isfinite(b->applied))
			snprintf(out, size, "%s, %+.3f ppm applied", item,
				 b->applied);
		else
			snprintf(out, size, "%s", item);
		break;
	case ST_TREBLE:
		if (b && isfinite(b->corr20k))
			snprintf(out, size,
				 "%s, %+.2f dB at 20 kHz since start", item,
				 b->corr20k);
		else
			snprintf(out, size, "%s", item);
		break;
	case ST_OVER:
		snprintf(out, size, "%s: the DAC at %u Hz", item,
			 w->have ? s->stream_rate * s->factor : 0);
		break;
	case ST_ZSTEP:
		snprintf(out, size, "%s, gap %.2f and %.2f LSB", item,
			 ch_ctl(w, "Audio 2 Zero Step Left", 0) / 256.0,
			 ch_ctl(w, "Audio 2 Zero Step Right", 0) / 256.0);
		break;
	case ST_DAC:
		snprintf(out, size, "oversampling %s, filter %s",
			 ch_ctl(w, "Audio 2 Oversampling Playback Switch", 0) ?
				 "on" :
				 "off",
			 ch_ctl(w, "Audio 2 Filter Playback Switch", 0) ?
				 "on" :
				 "off");
		break;
	case ST_LIVE:
		if (!w->have)
			snprintf(out, size, "-");
		else
			snprintf(out, size, "%s, block %u, trim %d ppb",
				 s->hold ? "held" : "following", s->blocks,
				 b ? b->trim : 0);
		break;
	case ST_A1C:
		snprintf(out, size, "%s%s", item,
			 known && w->have && (long)s->a1c != v ?
				 ", Audio 1 not yet" :
				 "");
		break;
	case ST_ENGINE:
		if (!w->have)
			snprintf(out, size, "no state");
		else
			snprintf(out, size, "%s%s", w->alive ? "" : "ended, ",
				 ch_state(s));
		break;
	default:
		snprintf(out, size, "%s", item);
	}
}

// --- the pictures ---------------------------------------------------------------

// a row of a source's spectrogram, the newest back n rows, into x and y
static int ch_row(const struct eqs_state *s, int src, unsigned int back,
		  double *x, double *y)
{
	unsigned int j;
	const int8_t *row;

	if (s->row_next[src] <= back || !s->row_hz[src])
		return 0;
	row = s->rows[src][(s->row_next[src] - 1 - back) % EQS_ROWS];
	for (j = 0; j < EQS_BINS; j++) {
		x[j] = (j + 0.5) * s->row_hz[src] / EQS_BINS;
		y[j] = row[j];
	}
	return EQS_BINS;
}

static void ch_none(struct canvas *c, const struct axes *a, const char *why)
{
	text(c, a->x + 8, a->y + a->h / 2, C_DIM, why);
}

// what plays and comes back: the newest row of each spectrogram
static void ch_vis_spectra(struct chain *w, struct canvas *c, int bx, int by,
			   int bw, int bh, const char *title, double floor)
{
	struct axes a = { .xunit = "Hz", .yunit = "dB", .y1 = 0 };
	double x[EQS_BINS], y[EQS_BINS];
	int n, src;

	a.y0 = floor;
	a.x0 = 0;
	a.x1 = w->have && w->st->row_hz[EQS_MON] ? w->st->row_hz[EQS_MON] :
						   22050;
	axes_draw(c, &a, bx, by, bw, bh, title);
	for (src = 0; w->have && src < EQS_SOURCES; src++) {
		n = ch_row(w->st, src, 0, x, y);
		if (n)
			axes_series(c, &a, x, y, (size_t)n, ch_rgb[src]);
	}
	axes_key(c, &a, 0, ch_rgb[0], "what the DAC took");
	axes_key(c, &a, 1, ch_rgb[1], "what came back on Audio 1");
	if (!w->have || !w->st->row_next[EQS_MON])
		ch_none(c, &a,
			"no rows yet: the engine sends them while watched");
}

// the clock over the blocks: the chip, what the driver applies, the
// residual, in ppm against minutes
static void ch_vis_clock(struct chain *w, struct canvas *c, int bx, int by,
			 int bw, int bh)
{
	const struct eqs_state *s = w->st;
	struct axes a = { .xunit = "", .yunit = "" };
	double t[EQS_HIST], y[3][EQS_HIST];
	unsigned int i, n = w->have ? s->hist_count : 0, k;
	double lo, hi, l2, h2;

	for (i = 0; i < n; i++) {
		const struct eqs_block *b =
			&s->hist[(s->hist_next - n + i) % EQS_HIST];

		t[i] = b->t / 60.0;
		y[0][i] = b->chip;
		y[1][i] = b->applied;
		y[2][i] = b->resid;
	}
	a.x0 = n ? t[0] : 0;
	a.x1 = n && t[n - 1] > t[0] ? t[n - 1] : a.x0 + 1;
	lo = -1;
	hi = 1;
	for (k = 0; k < 3; k++)
		if (range_of(y[k], n, &l2, &h2, 0)) {
			lo = fmin(lo, l2);
			hi = fmax(hi, h2);
		}
	a.y0 = lo;
	a.y1 = hi;
	axes_draw(c, &a, bx, by, bw, bh,
		  "the clock a block, ppm against minutes");
	axes_series(c, &a, t, y[0], n, ch_rgb[0]);
	axes_series(c, &a, t, y[1], n, ch_rgb[1]);
	axes_series(c, &a, t, y[2], n, 0x41c7b9);
	axes_key(c, &a, 0, ch_rgb[0], "the chip on the steered clock");
	axes_key(c, &a, 1, ch_rgb[1], "what the driver applies");
	axes_key(c, &a, 2, 0x41c7b9, "the residual");
	if (!n)
		ch_none(c, &a, "no blocks yet");
}

// the DAC's gain at f against its hold and the 6.2 % it keeps of the last
// sample, at over times the stream's rate (tools/es1869treble.py's model)
static double ch_dac(double f, double rate, unsigned int over)
{
	double x = 2 * M_PI * f / rate / (over ? over : 1);
	double hold = x ? sin(x / 2) / (x / 2) : 1;
	double re = 1 + CH_LAST * cos(x), im = -CH_LAST * sin(x);

	return fabs(hold) * sqrt(re * re + im * im) / (1 + CH_LAST);
}

// the treble correction: the DAC's droop, the correction's live taps at the
// rate in use, and what comes out of both
static void ch_vis_treble(struct chain *w, struct canvas *c, int bx, int by,
			  int bw, int bh)
{
	const struct eqs_state *s = w->st;
	struct axes a = { .xunit = "Hz", .yunit = "dB", .logx = 1 };
	double f[96], y[3][96];
	long side[EQS_SIDES];
	double rate = w->have && s->stream_rate ? s->stream_rate : 44100;
	unsigned int over = w->have && s->factor ? s->factor : 1, i, k;
	int on = w->have ? (int)s->treble : 0;

	for (k = 0; k < EQS_SIDES; k++)
		side[k] = w->have ? s->taps[(over - 1) % 3 * EQS_SIDES + k] : 0;
	for (i = 0; i < 96; i++) {
		double g;

		f[i] = 100 * pow(rate / 2 / 100, i / 95.0);
		g = ch_dac(f[i], rate, over);
		y[0][i] = 20 * log10(g);
		y[1][i] =
			20 * log10(fmax(eq_sides_gain(side, f[i], rate), 1e-6));
		y[2][i] = y[0][i] + (on ? y[1][i] : 0);
	}
	a.x0 = 100;
	a.x1 = rate / 2;
	a.y0 = -6;
	a.y1 = 6;
	axes_draw(c, &a, bx, by, bw, bh, "the DAC, its correction, and both");
	axes_series(c, &a, f, y[0], 96, C_DIM);
	axes_series(c, &a, f, y[1], 96, on ? ch_rgb[1] : C_GRID);
	axes_series(c, &a, f, y[2], 96, ch_rgb[0]);
	axes_key(c, &a, 0, C_DIM, "the DAC's droop");
	axes_key(c, &a, 1, on ? ch_rgb[1] : C_GRID,
		 on ? "the correction" : "the correction, off");
	axes_key(c, &a, 2, ch_rgb[0], "what comes out");
}

// the output rate: what the DAC took, to half its own rate, where the
// images of the stream's top lie, and the hold's droop at that rate
static void ch_vis_over(struct chain *w, struct canvas *c, int bx, int by,
			int bw, int bh)
{
	const struct eqs_state *s = w->st;
	struct axes a = { .xunit = "Hz", .yunit = "dB" };
	double x[EQS_BINS], y[EQS_BINS], hx[64], hy[64];
	unsigned int over = w->have && s->factor ? s->factor : 1, i;
	double rate = w->have && s->stream_rate ? s->stream_rate : 44100;
	int n = w->have ? ch_row(s, EQS_MON, 0, x, y) : 0;

	a.x0 = 0;
	a.x1 = rate * over / 2;
	a.y0 = -120;
	a.y1 = 0;
	axes_draw(c, &a, bx, by, bw, bh,
		  "what the DAC took, to half its own rate");
	for (i = 0; i < 64; i++) {
		hx[i] = a.x1 * i / 63;
		hy[i] = 20 * log10(fmax(ch_dac(hx[i], rate, over), 1e-6));
	}
	if (n)
		axes_series(c, &a, x, y, (size_t)n, ch_rgb[0]);
	axes_series(c, &a, hx, hy, 64, C_DIM);
	fill(c, a.x + (int)(a.w * (rate / 2) / a.x1), a.y, 1, a.h, CH_AMBER);
	axes_key(c, &a, 0, ch_rgb[0], "the monitor");
	axes_key(c, &a, 1, C_DIM, "the hold's droop");
	axes_key(c, &a, 2, CH_AMBER, "the stream's top");
	if (!n)
		ch_none(c, &a, "no rows yet");
}

// the driver's remap of a code about the gap (hw.c's es1869_zstep)
static double ch_zstep(int c, long step)
{
	long t = (long)c * 256 + (step - 256) / 2, v;

	if (!step)
		return c;
	if (t >= step)
		v = (t - step + 128) >> 8;
	else if (t <= -256)
		v = (t + 128) >> 8;
	else
		v = c < 0 ? -1 : 0;
	return (double)v;
}

// the zero step: the DAC's level against its code near 0, as the DAC makes
// it and as the remap asks it, and how often the music comes there
static void ch_vis_zstep(struct chain *w, struct canvas *c, int bx, int by,
			 int bw, int bh)
{
	struct axes a = { .xunit = "", .yunit = "" };
	long step = ch_ctl(w, "Audio 2 Zero Step Left", 0);
	int on = (int)ch_ctl(w, "Audio 2 Zero Step Playback Switch", 0);
	double x[33], raw[33], cor[33], hist[33], most = 1;
	double gap = step / 256.0;
	int i;

	for (i = 0; i < 33; i++) {
		int code = i - 16;
		double m = ch_zstep(code, step);

		x[i] = code;
		raw[i] = code + (code >= 0 ? gap : 0);
		cor[i] = m + (m >= 0 ? gap : 0);
		hist[i] = w->have ? w->st->zhist[0][i] : 0;
		most = fmax(most, hist[i]);
	}
	a.x0 = -16;
	a.x1 = 16;
	a.y0 = -20;
	a.y1 = 26;
	axes_draw(c, &a, bx, by, bw, bh,
		  "the DAC's level against its code, left");
	// the music's codes there, as bars along the bottom
	for (i = 0; i < 33; i++) {
		int h = (int)(a.h / 4 * hist[i] / most);
		int px = a.x + (int)((double)i / 32 * (a.w - 3));

		fill(c, px, a.y + a.h - h, 3, h, C_GRID);
	}
	axes_series(c, &a, x, raw, 33, on ? C_DIM : ch_rgb[1]);
	if (on)
		axes_series(c, &a, x, cor, 33, ch_rgb[0]);
	axes_key(c, &a, 0, on ? C_DIM : ch_rgb[1], "the DAC alone");
	axes_key(c, &a, 1, on ? ch_rgb[0] : C_GRID,
		 on ? "with the remap" : "the remap, off");
	axes_key(c, &a, 2, C_GRID, "the music's codes");
}

// the correction's 20 kHz and the trim over the blocks
static void ch_vis_blocks(struct chain *w, struct canvas *c, int bx, int by,
			  int bw, int bh)
{
	const struct eqs_state *s = w->st;
	struct axes b = { .xunit = "", .yunit = "" };
	double t[EQS_HIST], corr[EQS_HIST], trim[EQS_HIST], lo, hi;
	unsigned int i, n = w->have ? s->hist_count : 0;

	for (i = 0; i < n; i++) {
		const struct eqs_block *k =
			&s->hist[(s->hist_next - n + i) % EQS_HIST];

		t[i] = k->t / 60.0;
		corr[i] = k->corr20k;
		trim[i] = k->trim / 1000.0;
	}
	b.x0 = n ? t[0] : 0;
	b.x1 = n && t[n - 1] > t[0] ? t[n - 1] : b.x0 + 1;
	b.y0 = -1;
	b.y1 = 1;
	if (range_of(trim, n, &lo, &hi, 0)) {
		b.y0 = fmin(b.y0, lo);
		b.y1 = fmax(b.y1, hi);
	}
	axes_draw(c, &b, bx, by, bw, bh,
		  "over the blocks: 20 kHz (dB), trim (ppm)");
	axes_series(c, &b, t, corr, n, ch_rgb[1]);
	axes_series(c, &b, t, trim, n, 0x41c7b9);
	axes_key(c, &b, 0, ch_rgb[1], "the correction at 20 kHz");
	axes_key(c, &b, 1, 0x41c7b9, "the trim");
}

// the live corrections: each band's drift from the start, as the trends
// see it, and the correction's 20 kHz and the trim over the blocks
static void ch_vis_live(struct chain *w, struct canvas *c, int bx, int by,
			int bw, int bh)
{
	const struct eqs_state *s = w->st;
	struct axes a = { .xunit = "Hz", .yunit = "dB", .logx = 1 };
	double f[EQS_BANDS], dev[EQS_BANDS], base[EQS_BANDS];
	unsigned int i;
	int h1 = bh * 3 / 5;

	for (i = 0; i < EQS_BANDS; i++) {
		f[i] = w->have && s->band_hz[i] > 0 ? s->band_hz[i] : 250;
		dev[i] = w->have ? s->band_dev[i] : NAN;
		base[i] = w->have ? s->band_base[i] : NAN;
	}
	a.x0 = 200;
	a.x1 = 22000;
	a.y0 = -3;
	a.y1 = 3;
	axes_draw(c, &a, bx, by, bw, h1 - 2,
		  w->have && s->hold ? "the loop's deviation (held)" :
				       "the loop's deviation");
	axes_series(c, &a, f, base, EQS_BANDS, C_DIM);
	axes_points(c, &a, f, dev, EQS_BANDS, ch_rgb[0]);
	axes_key(c, &a, 0, C_DIM, "at the start");
	axes_key(c, &a, 1, ch_rgb[0], "the last block");
	ch_vis_blocks(w, c, bx, by + h1, bw, bh - h1);
}

// Audio 1's correction: its taps' response, the record path it takes back
// (their product was made flat at the calibration), and what comes out
static void ch_vis_a1c(struct chain *w, struct canvas *c, int bx, int by,
		       int bw, int bh)
{
	struct axes a = { .xunit = "Hz", .yunit = "dB", .logx = 1 };
	double f[80], g[2][80], path[80];
	double rate = w->have && w->st->a1_rate ? w->st->a1_rate : 44100;
	int on = w->have ? (int)w->st->a1c : 0, i, side, m;

	for (i = 0; i < 80; i++) {
		f[i] = 100 * pow(20000 / 100.0, i / 79.0);
		for (side = 0; side < 2; side++) {
			const int32_t *t = w->have ? w->st->a1taps + side * 17 :
						     NULL;
			double sum = t ? t[0] : 16384;

			for (m = 1; t && m <= 16; m++)
				sum += 2.0 * t[m] *
				       cos(2 * M_PI * f[i] * m / rate);
			g[side][i] = 20 * log10(fmax(fabs(sum / 16384), 1e-6));
		}
		path[i] = -g[0][i];
	}
	a.x0 = 100;
	a.x1 = 20000;
	a.y0 = -8;
	a.y1 = 8;
	axes_draw(c, &a, bx, by, bw, bh,
		  "the record path, its correction, and both");
	axes_series(c, &a, f, path, 80, C_DIM);
	axes_series(c, &a, f, g[0], 80, on ? ch_rgb[0] : C_GRID);
	axes_series(c, &a, f, g[1], 80, on ? ch_rgb[1] : C_GRID);
	axes_key(c, &a, 0, C_DIM, "the record path, as calibrated");
	axes_key(c, &a, 1, on ? ch_rgb[0] : C_GRID,
		 on ? "the correction, left" : "the correction, off");
	axes_key(c, &a, 2, on ? ch_rgb[1] : C_GRID, "and right");
}

// lines of text in a panel
static void ch_vis_text(struct chain *w, struct canvas *c, int bx, int by,
			int bw, int bh, int st)
{
	const struct eqs_state *s = w->st;
	int y = by + 8, fh = (int)c->font->h + 2;

	fill(c, bx, by, bw, bh, C_PANEL);
	box(c, bx, by, bw, bh, C_GRID);
	if (st == ST_DAC) {
		textf(c, bx + 8, y, C_TEXT, "ESS's oversampling: %s",
		      ch_ctl(w, "Audio 2 Oversampling Playback Switch", 0) ?
			      "on" :
			      "off");
		y += fh;
		textf(c, bx + 8, y, C_TEXT, "Audio 2's filter: %s",
		      ch_ctl(w, "Audio 2 Filter Playback Switch", 0) ? "on" :
								       "off");
		y += fh;
		textf(c, bx + 8, y, C_TEXT, "Audio 1's filter: %s",
		      ch_ctl(w, "Audio 1 Filter Switch", 0) ? "on" : "off");
		y += fh;
		textf(c, bx + 8, y, C_TEXT, "Master: %ld of 63, PCM: %ld of 15",
		      ch_ctl(w, "Master Playback Volume", -1),
		      ch_ctl(w, "PCM Playback Volume", -1));
		y += 2 * fh;
		text(c, bx + 8, y, C_DIM,
		     "the chip's own stages, off for the driver's correction");
		return;
	}
	if (!w->have) {
		text(c, bx + 8, y, C_DIM, "no state from the engine");
		return;
	}
	textf(c, bx + 8, y, C_TEXT, "%s, pid %d: %s",
	      w->alive ? "running" : "ended", w->pid, ch_state(s));
	y += fh;
	if (s->why[0]) {
		textf(c, bx + 8, y, CH_AMBER, "%.60s", s->why);
		y += fh;
	}
	textf(c, bx + 8, y, C_TEXT, "the stream %u Hz at %ux; Audio 1 %u Hz%s",
	      s->stream_rate, s->factor, s->a1_rate,
	      s->a1_rest ? ", resting" : "");
	y += fh;
	textf(c, bx + 8, y, C_TEXT, "lag %d, delay %+.3f, overlay %.4f", s->lag,
	      s->delay, s->overlay);
	y += fh;
	textf(c, bx + 8, y, C_TEXT, "segments %u, blocks %u, restarts %u, %s",
	      s->segments, s->blocks, s->restarts,
	      s->hold ? "held" : "following");
	y += fh;
	textf(c, bx + 8, y, C_DIM, "the last command done: %u", s->ack);
}

// a stage's picture
static void ch_vis(struct chain *w, struct canvas *c, int st, int bx, int by,
		   int bw, int bh)
{
	switch (st) {
	case ST_STREAM:
		ch_vis_spectra(w, c, bx, by, bw, bh,
			       "what plays and what comes back", -120);
		break;
	case ST_RATE:
		ch_vis_clock(w, c, bx, by, bw, bh);
		break;
	case ST_DITHER:
		ch_vis_spectra(w, c, bx, by, bw, bh,
			       "the floor under the music", -130);
		break;
	case ST_TREBLE:
		ch_vis_treble(w, c, bx, by, bw, bh);
		break;
	case ST_OVER:
		ch_vis_over(w, c, bx, by, bw, bh);
		break;
	case ST_ZSTEP:
		ch_vis_zstep(w, c, bx, by, bw, bh);
		break;
	case ST_LIVE:
		ch_vis_live(w, c, bx, by, bw, bh);
		break;
	case ST_A1C:
		ch_vis_a1c(w, c, bx, by, bw, bh);
		break;
	default:
		ch_vis_text(w, c, bx, by, bw, bh, st);
	}
}

// a setting's description, wrapped under the picture
static void ch_describe(struct canvas *c, const char *d, int x, int y, int w,
			int lines)
{
	int per = w / (int)c->font->w, len = (int)strlen(d), at = 0, l, cut;

	for (l = 0; l < lines && at < len; l++) {
		cut = len - at <= per ? len - at : per;
		while (cut < len - at && cut > 0 && d[at + cut] != ' ')
			cut--;
		if (cut <= 0)
			cut = per;
		textf(c, x, y + l * (int)c->font->h, C_DIM, "%.*s", cut,
		      d + at);
		at += cut;
		while (d[at] == ' ')
			at++;
	}
}

// --- the views -------------------------------------------------------------------

static void ch_status(struct chain *w, struct canvas *c)
{
	static const char *const names[] = { "",     "clock",	  "spectrogram",
					     "loop", "over time", "chain" };
	const struct eqs_state *s = w->st;
	int fh = (int)c->font->h, x = 6, k;

	fill(c, 0, 0, c->w, 2 * fh + 6, C_BACK);
	x += textf(c, x, 2, C_TEXT, "es1869scope  %s  ", names[w->view]);
	if (w->have)
		textf(c, x, 2, w->alive ? C_TEXT : CH_AMBER,
		      "attached to es1869eq, pid %d: %s%s", w->pid,
		      w->alive ? "" : "ended, ", ch_state(s));
	else
		textf(c, x, 2, CH_AMBER, "no state from the engine");
	x = 6;
	if (w->have && s->watched) {
		for (k = 0; k < 4; k++)
			x += textf(c, x, fh + 4, k < 2 ? ch_rgb[0] : ch_rgb[1],
				   "%s %s %4.0f dB  ", k < 2 ? "DAC" : "A1",
				   k % 2 ? "R" : "L", s->rms[k]);
	}
	text(c, x, fh + 4, C_DIM, "1-5 views, j/k stages, K keeps, q quits");
}

static void ch_chain(struct chain *w, struct canvas *c)
{
	int fh = (int)c->font->h, fw = (int)c->font->w, top = 2 * fh + 10;
	int lw = CH_LIST_W * fw, px = lw + 14, pw = (int)c->w - px - 6;
	int ph = (int)c->h - top - say_bar(c) - 5 * fh - 10, st, y;
	char line[160];

	for (st = 0, y = top; st < ST_N; st++, y += 2 * fh + 6) {
		uint32_t rgb = w->pending[st] ? CH_AMBER : C_TEXT;

		if (st == w->sel)
			fill(c, 2, y - 2, lw + 6, 2 * fh + 4, CH_SEL);
		if (ch_stages[st].key)
			textf(c, 6, y, rgb, "[%c] %s", ch_stages[st].key,
			      ch_stages[st].name);
		else
			textf(c, 6, y, C_DIM, "    %s", ch_stages[st].name);
		ch_says(w, st, line, sizeof(line));
		textf(c, 6 + 4 * fw, y + fh, w->pending[st] ? CH_AMBER : C_DIM,
		      "%.*s%s", CH_LIST_W - 4 - (w->pending[st] ? 10 : 0), line,
		      w->pending[st] ? " (pending)" : "");
	}
	ch_vis(w, c, w->sel, px, top, pw, ph);
	if (w->set[w->sel] && w->set[w->sel]->desc)
		ch_describe(c, w->set[w->sel]->desc, px, top + ph + 6, pw, 5);
	else if (w->sel == ST_LIVE)
		ch_describe(c,
			    "h holds the engine's refits of the DAC's "
			    "correction and its trim of the clock, or lets "
			    "them go on; the first switch of the sound holds "
			    "them too",
			    px, top + ph + 6, pw, 5);
}

// the spectrogram: the monitor's rows over Audio 1's, newest at the
// marker, drawn a row at a time as they come
static void ch_spectro_rows(struct chain *w, struct canvas *c, int full)
{
	int fh = (int)c->font->h, top = 2 * fh + 26, src, j;
	int ah = ((int)c->h - top - say_bar(c) - 2 * fh - 8) / 2;
	int ax = 6 * (int)c->font->w, aw = (int)c->w - ax - 8;

	for (src = 0; src < EQS_SOURCES; src++) {
		const struct eqs_state *s = w->st;
		int ay = top + src * (ah + fh + 6);
		uint32_t next = w->have ? s->row_next[src] : 0, r;

		if (full) {
			fill(c, ax - 1, ay - fh - 4, aw + 2, fh + 2, C_BACK);
			textf(c, ax, ay - fh - 2, src ? ch_rgb[1] : ch_rgb[0],
			      "%s, 0 to %u Hz",
			      src ? "what came back on Audio 1" :
				    "what the DAC took",
			      w->have ? s->row_hz[src] : 0);
			box(c, ax - 1, ay - 1, aw + 2, ah + 2, C_AXIS);
			fill(c, ax, ay, aw, ah, 0);
			w->rows[src] = next > (uint32_t)ah ? next - ah : 0;
		}
		for (r = w->rows[src]; r < next; r++) {
			const int8_t *row = s->rows[src][r % EQS_ROWS];
			int y = ay + (int)(r % (uint32_t)ah);
			uint32_t *p = c->px + (size_t)y * c->w + ax;

			if (next - r > EQS_ROWS)
				continue;
			for (j = 0; j < aw; j++) {
				int v = row[j * EQS_BINS / aw];

				p[j] = w->cmap[(v + 128) * 2 > 255 ?
						       255 :
						       (v + 128) * 2];
			}
			c->dirty[y] = 1;
			fill(c, ax, ay + (int)((r + 1) % (uint32_t)ah), aw, 1,
			     C_TEXT);
		}
		w->rows[src] = next;
	}
}

// the loop as the engine measures it: the response against 1 kHz, the phase
// past the delay, what isn't the path, and the impulse response
static void ch_loop(struct chain *w, struct canvas *c)
{
	const struct eqs_state *s = w->st;
	int fh = (int)c->font->h, top = 2 * fh + 10, half = (int)c->w / 2;
	int h = ((int)c->h - top - say_bar(c)) / 2;
	struct axes m = { .xunit = "Hz", .yunit = "dB", .logx = 1 };
	struct axes p = { .xunit = "Hz", .yunit = "", .logx = 1 };
	struct axes r = { .xunit = "Hz", .yunit = "dB", .logx = 1 };
	struct axes q = { .xunit = "", .yunit = "" };
	double f[EQS_POINTS], y[2][EQS_POINTS], t[EQS_IR], ir[2][EQS_IR];
	int i, k;

	for (i = 0; i < EQS_POINTS; i++)
		f[i] = w->have && s->hz[i] > 0 ? s->hz[i] :
						 20 * pow(1000, i / 127.0);
	m.x0 = p.x0 = r.x0 = 20;
	m.x1 = p.x1 = r.x1 = 20000;
	m.y0 = -6;
	m.y1 = 3;
	axes_draw(c, &m, 2, top, half - 3, h - 2, "the loop against 1 kHz");
	p.y0 = -30;
	p.y1 = 30;
	axes_draw(c, &p, half + 1, top, half - 3, h - 2,
		  "the phase past the delay, degrees");
	r.y0 = -60;
	r.y1 = 0;
	axes_draw(c, &r, 2, top + h, half - 3, h - 2, "what isn't the path");
	q.x0 = -32;
	q.x1 = 32;
	q.y0 = -0.5;
	q.y1 = 1.1;
	axes_draw(c, &q, half + 1, top + h, half - 3, h - 2,
		  "the impulse response, frames");
	if (!w->have || !s->hz[0]) {
		ch_none(c, &m,
			"no response yet: the engine sends it while watched");
		return;
	}
	for (k = 0; k < 2; k++) {
		for (i = 0; i < EQS_POINTS; i++)
			y[k][i] = s->mag[k][i];
		axes_series(c, &m, f, y[k], EQS_POINTS, ch_rgb[k]);
		for (i = 0; i < EQS_POINTS; i++)
			y[k][i] = s->phase[k][i];
		axes_series(c, &p, f, y[k], EQS_POINTS, ch_rgb[k]);
		for (i = 0; i < EQS_POINTS; i++)
			y[k][i] = 10 * log10(fmax(1 - s->coh[k][i], 1e-6));
		axes_series(c, &r, f, y[k], EQS_POINTS, ch_rgb[k]);
	}
	for (i = 0; i < EQS_IR; i++) {
		t[i] = i - 32;
		for (k = 0; k < 2; k++)
			ir[k][i] = s->ir[k][i];
	}
	axes_series(c, &q, t, ir[0], EQS_IR, ch_rgb[0]);
	axes_series(c, &q, t, ir[1], EQS_IR, ch_rgb[1]);
	axes_key(c, &m, 0, ch_rgb[0], "left");
	axes_key(c, &m, 1, ch_rgb[1], "right");
}

// over time: each band's deviation block by block, as colours, and the
// correction and the trim under it
static void ch_time(struct chain *w, struct canvas *c)
{
	const struct eqs_state *s = w->st;
	int fh = (int)c->font->h, top = 2 * fh + 26, ax = 7 * (int)c->font->w;
	int aw = (int)c->w - ax - 8;
	int ah = ((int)c->h - top - say_bar(c)) * 3 / 5, b, y;
	unsigned int n = w->have ? s->hist_count : 0, i;

	textf(c, ax, top - fh - 2, C_TEXT,
	      "each band's deviation, a block a column, -1.5 to +1.5 dB");
	box(c, ax - 1, top - 1, aw + 2, ah + 2, C_AXIS);
	fill(c, ax, top, aw, ah, 0);
	for (i = 0; i < n; i++) {
		const struct eqs_block *k =
			&s->hist[(s->hist_next - n + i) % EQS_HIST];
		int x0 = ax + (int)(i * (unsigned int)aw / EQS_HIST);
		int x1 = ax + (int)((i + 1) * (unsigned int)aw / EQS_HIST);

		for (b = 0; b < EQS_BANDS; b++) {
			double d = k->dev[b];
			int v = isfinite(d) ? (int)((d + 1.5) / 3 * 255) : 0;

			v = v < 0 ? 0 : v > 255 ? 255 : v;
			y = top + ah - (b + 1) * ah / EQS_BANDS;
			fill(c, x0, y, x1 - x0,
			     top + ah - b * ah / EQS_BANDS - y,
			     isfinite(d) ? w->cmap[v] : C_PANEL);
		}
	}
	text(c, 6, top, C_DIM, "20k");
	text(c, 6, top + ah - fh, C_DIM, "250");
	ch_vis_blocks(w, c, 2, top + ah + 8, (int)c->w - 4,
		      (int)c->h - top - ah - 8 - say_bar(c));
}

// es1869_irqs: how late each of the newest interrupts came, against the
// line through them by the master's ticks, in microseconds
static int ch_irqs(struct chain *w, double *late, int most)
{
	char path[96], line[96];
	double t[512], x[512], sx = 0, sy = 0, sxx = 0, sxy = 0, a, b, lo;
	unsigned int seq, src, ticks;
	unsigned long long raw;
	long double sum = 0;
	int n = 0, i;
	FILE *f;

	if (!w->card[0])
		return 0;
	snprintf(path, sizeof(path), "/proc/asound/%s/es1869_irqs", w->card);
	f = fopen(path, "r");
	if (!f)
		return 0;
	while (n < 512 && n < most && fgets(line, sizeof(line), f))
		if (sscanf(line, "%u %u %u %llu", &seq, &src, &ticks, &raw) ==
			    4 &&
		    src == 2 && raw) {
			sum += ticks;
			x[n] = (double)(sum / CH_MASTER_HZ);
			t[n++] = raw / 1e9;
		}
	fclose(f);
	if (n < 3)
		return 0;
	for (i = 0; i < n; i++) {
		sx += x[i];
		sy += t[i] - t[0];
		sxx += x[i] * x[i];
		sxy += x[i] * (t[i] - t[0]);
	}
	b = (n * sxy - sx * sy) / (n * sxx - sx * sx);
	a = (sy - b * sx) / n;
	lo = 1e9;
	for (i = 0; i < n; i++) {
		late[i] = (t[i] - t[0] - a - b * x[i]) * 1e6;
		lo = fmin(lo, late[i]);
	}
	for (i = 0; i < n; i++)
		late[i] -= lo;
	return n;
}

static void ch_clock(struct chain *w, struct canvas *c)
{
	int fh = (int)c->font->h, top = 2 * fh + 10;
	int h = ((int)c->h - top - say_bar(c)) / 2, n, i;
	struct axes a = { .xunit = "", .yunit = "" };
	double late[512], k[512];

	ch_vis_clock(w, c, 2, top, (int)c->w - 4, h - 2);
	n = ch_irqs(w, late, 512);
	for (i = 0; i < n; i++)
		k[i] = i;
	a.x0 = 0;
	a.x1 = n > 1 ? n - 1 : 1;
	a.y0 = 0;
	a.y1 = 200;
	axes_draw(c, &a, 2, top + h, (int)c->w - 4, h - 2,
		  "Audio 2's interrupts, how late each came, us");
	axes_points(c, &a, k, late, (size_t)n, ch_rgb[0]);
	if (!n)
		ch_none(c, &a,
			"no stamps: es1869_irqs keeps them in the Clock mode");
}

void chain_frame(struct chain *w, struct canvas *c)
{
	ch_stream(w, ch_now());
	fill(c, 0, 0, c->w, c->h, C_BACK);
	ch_status(w, c);
	switch (w->view) {
	case CHAIN_CLOCK:
		ch_clock(w, c);
		break;
	case CHAIN_SPECTRO:
		ch_spectro_rows(w, c, 1);
		break;
	case CHAIN_LOOP:
		ch_loop(w, c);
		break;
	case CHAIN_TIME:
		ch_time(w, c);
		break;
	default:
		ch_chain(w, c);
	}
	say_draw(c);
	w->drawn_at = ch_now();
	w->dirty = 0;
}

int chain_step(struct chain *w, struct canvas *c, int key)
{
	long long now = ch_now();
	snd_ctl_event_t *ev;
	int moved = 0, events = 0;

	if (key >= 0 && ch_key(w, key) < 0)
		return -1;
	// the engine: whether it runs, asked to watch, and its state
	if (now - w->alive_at > CH_ALIVE_S * 1000000000LL) {
		int pid = eqs_alive(w->path);

		w->alive_at = now;
		if (!pid && w->alive) {
			say("the engine has stopped: what shows is its last "
			    "state");
			w->dirty = 1;
		}
		w->alive = pid != 0;
		if (pid)
			w->pid = pid;
	}
	if (w->alive && now - w->watch_at > CH_WATCH_S * 1000000000LL) {
		w->watch_at = now;
		ch_send(w, "watch");
	}
	if (now - w->copy_at >= 100000000LL) {
		w->copy_at = now;
		moved = ch_copy(w) > 0;
	}
	if (w->ctl) {
		snd_ctl_event_alloca(&ev);
		while (snd_ctl_read(w->ctl, ev) > 0)
			events = 1;
	}
	ch_pending(w, now);
	if (w->frozen)
		return 0;
	if (w->dirty) {
		chain_frame(w, c);
		return 1;
	}
	if (w->view == CHAIN_SPECTRO && moved) {
		ch_spectro_rows(w, c, 0);
		if (say_due())
			say_draw(c);
		return 1;
	}
	if ((moved || events || say_due()) && now - w->drawn_at > CH_DRAW_NS) {
		chain_frame(w, c);
		return 1;
	}
	return 0;
}
