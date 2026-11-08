// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * es1869scope: what es1869clock logs, drawn on the framebuffer of a virtual
 * terminal of its own, and redrawn as the log grows.
 *
 *   es1869scope [-l LOG] [-d FB] [-s FILE.ppm] [-W WIDTH] [-H HEIGHT]
 *               [-v 1|2|3] [-c CAPTURE] [-r RATE] [-i FILE.wav]
 *               [-x PLAYED.wav] [-p PLAY] [-a CAPTURE] [-k CTL] [-T SECONDS]
 *
 * Notes:
 *
 * The clock view has four panels:
 * - each pair's time error over the run, with its line taken out, which is
 *   what the two crystals do against each other;
 * - their Allan deviations, and where two pairs share a card, each crystal's
 *   own (the three-cornered hat), with the steered clock's against the raw
 *   one from chronyd's frequency;
 * - the lateness of each interrupt the driver stamped, against the line of
 *   its master clock's ticks: the short pulses, with how many and how big;
 * - the spectrum of the first pair's error: the long resonances, if any.
 * The status line has chronyd's view, the driver's fits and the pilots'
 * levels.
 *
 * The spectrogram view, -v 2 or the key 2, takes a capture (-c, the VX by
 * default, at -r Hz, 48000) only while it shows, or a WAV file with -i, and
 * draws it as a waterfall through the MMX FFT of fft.c (spectro.c).
 *
 * The loop view, -v 3 or the key 3, plays noise on Audio 2 (-p) and records
 * Audio 1 (-a) through the record mixer of the card -k, the ES1869's internal
 * loop, while it shows, and draws the loop's response (loopview.c): each
 * channel's magnitude and phase, what isn't the path, and the impulse
 * response. The keys c, t and o start it again, turn Audio 2's treble
 * correction over, and switch its output rate between 1x and 2x. With -x and
 * -i it takes what was played and what came back from two WAV files instead.
 *
 * Keys: q quits, 1, 2 and 3 choose the view, space holds the picture, r
 * redraws. -s draws once into a PPM file instead, at -W by -H (800 by 600),
 * for tests and for pictures of a run, after -T seconds (12) of a live loop,
 * whose figures then go to standard output, as they do for two files.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "draw.h"
#include "fb.h"
#include "loopview.h"
#include "plot.h"
#include "spectro.h"
#include "tie.h"

#define PAIRS 4
#define SOURCES 3
#define TAU0 0.1 // a pilot block
#define ES_MASTER 23814000.0 // the ES1869's master clock, Hz

static const uint32_t pair_rgb[PAIRS] = { 0x5aa9ff, 0xffa64d, 0xd383ff,
					  0x7fd17f };
#define UTC_RGB 0xb0b8c0 // the raw clock against UTC
// the crystals of the three-cornered hat, apart from the pairs
static const uint32_t hat_rgb[3] = { 0x41c7b9, 0xff7eb6, 0xe8d04a };

struct vec {
	double *v;
	size_t n, cap;
};

static int push(struct vec *a, double v)
{
	if (a->n == a->cap) {
		size_t cap = a->cap ? 2 * a->cap : 1024;
		double *p = realloc(a->v, cap * sizeof(*p));

		if (!p)
			return -ENOMEM;
		a->v = p;
		a->cap = cap;
	}
	a->v[a->n++] = v;
	return 0;
}

struct pair {
	char name[16];
	struct vec t, x; // raw s, time error s
	double amp;
};

struct data {
	struct pair pair[PAIRS];
	int pairs;
	struct vec irq_t[SOURCES], irq_ticks[SOURCES]; // raw s, ticks so far
	double ticks_sum[SOURCES];
	struct vec sys_t, sys_freq; // raw s, kernel ppb
	// the raw clock against UTC, as chronyd sees it: raw - real + its offset
	struct vec utc_x;
	double utc_off; // chronyd's last offset of the system's clock, s
	long long master_ppb, steered_ppb;
	int have_k;
	char chrony[160];
	double t0;
	int have_t0;
	long pos; // how far the log was read
};

static struct pair *pair_of(struct data *d, const char *name)
{
	int i;

	for (i = 0; i < d->pairs; i++)
		if (!strcmp(d->pair[i].name, name))
			return &d->pair[i];
	if (d->pairs == PAIRS)
		return NULL;
	snprintf(d->pair[d->pairs].name, sizeof(d->pair[0].name), "%.15s",
		 name);
	return &d->pair[d->pairs++];
}

static double t_of(struct data *d, long long raw)
{
	if (!d->have_t0) {
		d->t0 = raw * 1e-9;
		d->have_t0 = 1;
	}
	return raw * 1e-9 - d->t0;
}

// chronyc -c tracking's fields, split at commas, empty ones kept
static int chrony_fields(const char *line, char *buf, size_t size, char **f,
			 int max)
{
	char *rest = buf;
	int n = 0;

	snprintf(buf, size, "%s", line);
	while (n < max && (f[n] = strsep(&rest, ",")))
		n++;
	return n;
}

// its system time offset, field 4, while it has a source (stratum, field 2)
static void chrony_offset(struct data *d)
{
	char buf[160], *f[16];

	if (chrony_fields(d->chrony, buf, sizeof(buf), f, 16) > 4 &&
	    atoi(f[2]) > 0)
		d->utc_off = atof(f[4]);
}

static void parse(struct data *d, char *line)
{
	char name[16];
	long long raw, real, boot, a, b;
	double x, amp, freq;
	unsigned long long seq;
	unsigned int src, ticks;

	switch (line[0]) {
	case 'p':
		if (sscanf(line + 2, "%15s %lld %lf %lf", name, &raw, &x,
			   &amp) == 4) {
			struct pair *p = pair_of(d, name);

			if (p) {
				push(&p->t, t_of(d, raw));
				push(&p->x, x * 1e-9);
				p->amp = amp;
			}
		}
		break;
	case 'i':
		if (sscanf(line + 2, "%llu %u %u %lld", &seq, &src, &ticks,
			   &raw) == 4 &&
		    src < SOURCES) {
			d->ticks_sum[src] += ticks;
			// a later pass has no stamp: its ticks count, nothing more
			if (raw) {
				push(&d->irq_t[src], t_of(d, raw));
				push(&d->irq_ticks[src], d->ticks_sum[src]);
			}
		}
		break;
	case 's':
		if (sscanf(line + 2, "%lld %lld %lld %lf", &raw, &real, &boot,
			   &freq) == 4) {
			push(&d->sys_t, t_of(d, raw));
			push(&d->sys_freq, freq);
			push(&d->utc_x, (raw - real) * 1e-9 + d->utc_off);
		}
		break;
	case 'k':
		if (sscanf(line + 2, "%lld %lld", &a, &b) == 2) {
			d->master_ppb = a;
			d->steered_ppb = b;
			d->have_k = 1;
		}
		break;
	case 't':
		snprintf(d->chrony, sizeof(d->chrony), "%.159s", line + 2);
		d->chrony[strcspn(d->chrony, "\n")] = 0;
		chrony_offset(d);
		break;
	}
}

// the log's new lines
static int follow(struct data *d, const char *path)
{
	char line[512];
	FILE *f = fopen(path, "r");
	int got = 0;

	if (!f)
		return -errno;
	if (fseek(f, d->pos, SEEK_SET)) {
		fclose(f);
		return -errno;
	}
	while (fgets(line, sizeof(line), f)) {
		if (!strchr(line, '\n'))
			break; // a line still being written
		parse(d, line);
		d->pos = ftell(f);
		got++;
	}
	fclose(f);
	return got;
}

// --- the clock view ------------------------------------------------------------

// x less its line, in a fresh array
static double *detrend(const struct vec *t, const struct vec *x)
{
	double *r = malloc(x->n * sizeof(*r));
	struct tie_line l;
	size_t i;

	if (!r)
		return NULL;
	tie_fit(t->v, x->v, x->n, &l);
	for (i = 0; i < x->n; i++)
		r[i] = x->v[i] - l.a - l.b * t->v[i];
	return r;
}

static void panel_error(struct canvas *c, struct data *d, int bx, int by,
			int bw, int bh)
{
	struct axes a = { .xunit = "s", .yunit = "s" };
	double lo = INFINITY, hi = -INFINITY, *r[PAIRS] = { 0 };
	int i, any = 0;

	a.x0 = 0;
	a.x1 = 1;
	for (i = 0; i < d->pairs; i++) {
		struct pair *p = &d->pair[i];
		double l, h;

		if (p->x.n < 2)
			continue;
		r[i] = detrend(&p->t, &p->x);
		if (r[i] && range_of(r[i], p->x.n, &l, &h, 0)) {
			lo = fmin(lo, l);
			hi = fmax(hi, h);
			a.x1 = fmax(a.x1, p->t.v[p->t.n - 1]);
			any = 1;
		}
	}
	a.y0 = any ? lo : -1e-9;
	a.y1 = any ? hi : 1e-9;
	axes_draw(c, &a, bx, by, bw, bh, "time error, its line taken out");
	for (i = 0; i < d->pairs; i++) {
		if (!r[i])
			continue;
		axes_series(c, &a, d->pair[i].t.v, r[i], d->pair[i].x.n,
			    pair_rgb[i]);
		axes_key(c, &a, i, pair_rgb[i], d->pair[i].name);
		free(r[i]);
	}
	if (!any)
		text(c, a.x + 8, a.y + 8, C_DIM, "no pilot yet");
}

#define TAUS 24

static void panel_dev(struct canvas *c, struct data *d, int bx, int by, int bw,
		      int bh)
{
	struct axes a = { .logx = 1, .logy = 1, .xunit = "s", .yunit = NULL };
	double tau[TAUS], dev[PAIRS + 4][TAUS], all[(PAIRS + 4) * TAUS];
	const char *names[PAIRS + 4];
	uint32_t rgb[PAIRS + 4];
	int i, k, rows = 0, n = 0;
	size_t m;

	for (k = 0, m = 1; k < TAUS; k++, m *= 2)
		tau[k] = m * TAU0;
	for (i = 0; i < d->pairs; i++, rows++) {
		struct pair *p = &d->pair[i];

		for (k = 0, m = 1; k < TAUS; k++, m *= 2)
			dev[rows][k] = tie_adev(p->x.v, p->x.n, TAU0, m);
		names[rows] = p->name;
		rgb[rows] = pair_rgb[i];
	}
	// two pairs that share a card, measured together: each crystal alone
	if (d->pairs == 2 && d->pair[0].name[1] == d->pair[1].name[0]) {
		size_t len = d->pair[0].x.n < d->pair[1].x.n ? d->pair[0].x.n :
							       d->pair[1].x.n;
		static char who[3][2];

		for (i = 0; i < 3; i++) {
			who[i][0] = i < 2 ? d->pair[0].name[i] :
					    d->pair[1].name[1];
			who[i][1] = 0;
			names[rows + i] = who[i];
			rgb[rows + i] = hat_rgb[i];
		}
		for (k = 0, m = 1; k < TAUS; k++, m *= 2) {
			double var[3];

			tie_hat(d->pair[0].x.v, d->pair[1].x.v, len, TAU0, m,
				var);
			for (i = 0; i < 3; i++)
				dev[rows + i][k] = var[i] > 0 ? sqrt(var[i]) :
								NAN;
		}
		rows += 3;
	}
	// the raw clock, the chip's crystal, against UTC through chronyd, a
	// sample a second, so from 1 s up
	if (d->utc_x.n > 3) {
		for (k = 0, m = 1; k < TAUS; k++, m *= 2)
			dev[rows][k] = tau[k] < 1 ?
					       NAN :
					       tie_adev(d->utc_x.v, d->utc_x.n,
							1.0,
							(size_t)lrint(tau[k]));
		names[rows] = "raw-utc";
		rgb[rows++] = UTC_RGB;
	}
	for (i = 0; i < rows; i++)
		for (k = 0; k < TAUS; k++)
			all[n++] = dev[i][k];
	if (!range_of(all, n, &a.y0, &a.y1, 1)) {
		a.y0 = 1e-12;
		a.y1 = 1e-6;
	}
	a.x0 = TAU0;
	a.x1 = tau[TAUS - 1];
	for (k = TAUS - 1; k > 0; k--) {
		int seen = 0;

		for (i = 0; i < rows; i++)
			seen |= isfinite(dev[i][k]);
		if (seen)
			break;
	}
	a.x1 = fmax(10 * TAU0, pow(10, ceil(log10(tau[k]))));
	axes_draw(c, &a, bx, by, bw, bh, "Allan deviation, and each crystal");
	for (i = 0; i < rows; i++) {
		axes_series(c, &a, tau, dev[i], TAUS, rgb[i]);
		axes_points(c, &a, tau, dev[i], TAUS, rgb[i]);
		axes_key(c, &a, i, rgb[i], names[i]);
	}
}

// how late each interrupt of a stretch came: against the line under its
// earliest ones, since lateness only ever adds. The earliest of each window of
// a second, against the master's nominal rate, make the line, which a least
// squares fit through all of them would tilt
#define IRQ_WINDOW 43 // a second of 1024-frame periods at 44.1 kHz

static void lateness(const double *t, const double *k, size_t n, double *late)
{
	double mk[64], mt[64], base = INFINITY;
	size_t w, j, windows = n / IRQ_WINDOW;
	struct tie_line l = { 0, 1 / ES_MASTER };

	if (windows >= 3) {
		size_t step = windows > 64 ? (windows + 63) / 64 : 1, used = 0;

		for (w = 0; w < windows && used < 64; w += step) {
			size_t lo = w * IRQ_WINDOW, hi = lo + IRQ_WINDOW,
			       best = lo;

			for (j = lo; j < hi; j++)
				if (t[j] - k[j] / ES_MASTER <
				    t[best] - k[best] / ES_MASTER)
					best = j;
			mk[used] = k[best];
			mt[used++] = t[best];
		}
		tie_fit(mk, mt, used, &l);
	}
	for (j = 0; j < n; j++) {
		late[j] = t[j] - l.a - l.b * k[j];
		base = fmin(base, late[j]);
	}
	for (j = 0; j < n; j++)
		late[j] -= base;
}

// whether the stream restarted, or its DAC stalled, before interrupt i: a
// gap that the ticks don't count, or a step of the earliest interrupt of a
// second against the last second's, which lateness, only ever adding, can't
// make
#define STEP_S 200e-6

static double window_min(const double *t, const double *k, size_t lo, size_t hi)
{
	double m = INFINITY;
	size_t j;

	for (j = lo; j < hi; j++)
		m = fmin(m, t[j] - k[j] / ES_MASTER);
	return m;
}

static int restart(const double *t, const double *k, size_t n, size_t i)
{
	double dk = (k[i] - k[i - 1]) / ES_MASTER, dt = t[i] - t[i - 1];

	if (fabs(dt - dk) >= 0.5 * dk)
		return 1;
	if (i % IRQ_WINDOW || i < IRQ_WINDOW || i + IRQ_WINDOW > n)
		return 0;
	return fabs(window_min(t, k, i, i + IRQ_WINDOW) -
		    window_min(t, k, i - IRQ_WINDOW, i)) > STEP_S;
}

// the period the pulses keep, if any, from 5 ms to 2 s, over the last 2000
// of them, and only worked out again when there are a tenth more
static double pulse_period(const double *t, const double *late, size_t n,
			   double over, double *strength)
{
	static double times[2000], last_period, last_strength;
	static size_t last_count;
	size_t i, k, m, count = 0;

	for (i = 0; i < n; i++)
		count += late[i] > over;
	if (last_count && count >= last_count && count < last_count * 11 / 10) {
		*strength = last_strength;
		return last_period;
	}
	m = count < 2000 ? count : 2000;
	// the last m of them, in order
	for (i = n, k = m; i-- > 0 && k;)
		if (late[i] > over)
			times[--k] = t[i];
	last_count = count;
	last_period = tie_period(times, m, 0.005, 2.0, &last_strength);
	*strength = last_strength;
	return last_period;
}

static void panel_irq(struct canvas *c, struct data *d, int bx, int by, int bw,
		      int bh)
{
	struct axes a = { .xunit = "s", .yunit = "s" };
	int src = d->irq_t[2].n > d->irq_t[1].n ? 2 : 1;
	struct vec *t = &d->irq_t[src], *k = &d->irq_ticks[src];
	double *late = NULL, med = 0, mad = 0, lo, hi, period = 0, strength;
	size_t i, pulses = 0, idx[1];
	char title[96], b1[24], b2[24];

	if (t->n > 2)
		late = malloc(t->n * sizeof(*late));
	if (late) {
		size_t start = 0;

		// each stretch between restarts gets its own line
		for (i = 1; i <= t->n; i++) {
			if (i < t->n && !restart(t->v, k->v, t->n, i))
				continue;
			lateness(t->v + start, k->v + start, i - start,
				 late + start);
			start = i;
		}
		pulses = tie_pulses(late, t->n, 8, idx, 0, &med, &mad);
		period = pulse_period(t->v, late, t->n, med + 8 * mad,
				      &strength);
	}
	snprintf(title, sizeof(title), "interrupt lateness, Audio %d", src);
	a.x0 = 0;
	a.x1 = t->n ? fmax(1, t->v[t->n - 1]) : 1;
	if (!late || !range_of(late, t->n, &lo, &hi, 0)) {
		lo = 0;
		hi = 1e-4;
	}
	// on a log scale from 100 ns, where the on-time ones sit
	a.logy = 1;
	a.y0 = 1e-7;
	a.y1 = fmax(pow(10, ceil(log10(fmax(hi, 1e-5)))), 1e-5);
	axes_draw(c, &a, bx, by, bw, bh, title);
	if (late) {
		for (i = 0; i < t->n; i++)
			late[i] = fmax(late[i], 1e-7);
		axes_points(c, &a, t->v, late, t->n, 0xff6f6f);
		fill(c, a.x + 4, a.y + 2, a.w - 8, 3 * c->font->h + 8, C_PANEL);
		snprintf(title, sizeof(title), "%zu pulses of %zu stamps",
			 pulses, t->n);
		text(c, a.x + 8, a.y + 4, C_TEXT, title);
		snprintf(title, sizeof(title), "median %s, MAD %s",
			 si(med, "s", b1, sizeof(b1)),
			 si(mad, "s", b2, sizeof(b2)));
		text(c, a.x + 8, a.y + 6 + c->font->h, C_TEXT, title);
		if (period > 0 && strength > 8) {
			snprintf(title, sizeof(title),
				 "they keep a period of %s, %.0f sigma",
				 si(period, "s", b1, sizeof(b1)), strength);
			text(c, a.x + 8, a.y + 8 + 2 * c->font->h, C_TEXT,
			     title);
		}
	} else {
		text(c, a.x + 8, a.y + 8, C_DIM,
		     "no stamps: the driver has no es1869_irqs");
	}
	free(late);
}

static void panel_psd(struct canvas *c, struct data *d, int bx, int by, int bw,
		      int bh)
{
	struct axes a = { .logx = 1, .logy = 1, .xunit = "Hz", .yunit = NULL };
	unsigned int lg = 14;
	double *psd, *f, *r;
	struct pair *p = d->pairs ? &d->pair[0] : NULL;
	size_t i, half;
	int w = 0;

	while (lg > 6 && p && ((size_t)1 << lg) > p->x.n / 2)
		lg--;
	half = ((size_t)1 << lg) / 2;
	psd = calloc(half + 1, sizeof(*psd));
	f = calloc(half + 1, sizeof(*f));
	r = p && p->x.n > 2 ? detrend(&p->t, &p->x) : NULL;
	if (psd && f && r)
		w = tie_psd(r, p->x.n, TAU0, lg, psd);
	for (i = 0; f && i <= half; i++)
		f[i] = i / (((size_t)1 << lg) * TAU0);
	a.x0 = f && half ? f[1] : 1e-3;
	a.x1 = 1 / (2 * TAU0);
	if (w <= 0 || !range_of(psd + 1, half, &a.y0, &a.y1, 1)) {
		a.y0 = 1e-24;
		a.y1 = 1e-12;
	}
	axes_draw(c, &a, bx, by, bw, bh, "spectrum of the time error, s^2/Hz");
	if (w > 0) {
		axes_series(c, &a, f + 1, psd + 1, half, pair_rgb[0]);
		axes_key(c, &a, 0, pair_rgb[0], p->name);
	} else {
		text(c, a.x + 8, a.y + 8, C_DIM, "too short yet");
	}
	free(psd);
	free(f);
	free(r);
}

static void status(struct canvas *c, struct data *d, int y)
{
	char line[256], b1[24], b2[24];
	int i, x = 6;

	fill(c, 0, y, c->w, c->font->h * 2 + 6, C_BACK);
	x += textf(c, x, y + 2, C_TEXT, "es1869scope  clock  ");
	if (d->have_k)
		x += textf(
			c, x, y + 2, C_DIM,
			"driver: master %+.3f ppm of raw, steered %+.3f ppm  ",
			d->master_ppb / 1e3, d->steered_ppb / 1e3);
	for (i = 0; i < d->pairs; i++)
		x += textf(c, x, y + 2, pair_rgb[i], "%s %.0f dBFS  ",
			   d->pair[i].name,
			   20 * log10(fmax(d->pair[i].amp, 1e-3) / 32768));
	// chronyc -c tracking: ref id, name, stratum, ref time, system time,
	// last offset, rms offset, frequency ppm, ...
	if (d->chrony[0]) {
		char *f[16], buf[160];
		int n = chrony_fields(d->chrony, buf, sizeof(buf), f, 16);

		if (n >= 8) {
			snprintf(
				line, sizeof(line),
				"chronyd: %s stratum %s, offset %s, rms %s, frequency %s ppm",
				f[1], f[2], si(atof(f[4]), "s", b1, sizeof(b1)),
				si(atof(f[6]), "s", b2, sizeof(b2)), f[7]);
			text(c, 6, y + 4 + c->font->h, C_DIM, line);
		}
	}
}

static void draw(struct canvas *c, struct data *d)
{
	int top = c->font->h * 2 + 8, w = c->w / 2, h = (c->h - top) / 2;

	fill(c, 0, 0, c->w, c->h, C_BACK);
	status(c, d, 0);
	panel_error(c, d, 2, top, w - 3, h - 2);
	panel_dev(c, d, w + 1, top, w - 3, h - 2);
	panel_irq(c, d, 2, top + h, w - 3, h - 2);
	panel_psd(c, d, w + 1, top + h, w - 3, h - 2);
}

// --- the run -----------------------------------------------------------------

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: es1869scope [-l LOG] [-d FB] [-s FILE.ppm] [-W WIDTH] [-H HEIGHT]\n"
		"                   [-v 1|2|3] [-c CAPTURE] [-r RATE] [-i FILE.wav]\n"
		"                   [-x PLAYED.wav] [-p PLAY] [-a CAPTURE] [-k CTL]\n"
		"                   [-T SECONDS]\n");
}

int main(int argc, char **argv)
{
	const char *log = "/var/tmp/es1869clock.log", *dev = "/dev/fb0";
	const char *shot = NULL, *cdev = "plughw:CARD=VXPocket,DEV=0";
	const char *wav = NULL;
	unsigned int w = 800, h = 600, rate = 48000;
	struct loopview_cfg lc = { .play = "hw:CARD=ES1869,DEV=0,SUBDEV=0",
				   .capture = "hw:CARD=ES1869,DEV=0",
				   .ctl = "hw:CARD=ES1869",
				   .rate = 44100,
				   .level = -20 };
	struct loopview *lv = NULL;
	struct spectro *sp = NULL;
	double secs = 12;
	int view = 1;
	static struct data d;
	struct canvas c;
	struct font font;
	struct fb fb;
	int o, held = 0;

	while ((o = getopt(argc, argv, "l:d:s:W:H:v:c:r:i:x:p:a:k:T:")) != -1) {
		switch (o) {
		case 'l':
			log = optarg;
			break;
		case 'd':
			dev = optarg;
			break;
		case 's':
			shot = optarg;
			break;
		case 'W':
			w = atoi(optarg);
			break;
		case 'H':
			h = atoi(optarg);
			break;
		case 'v':
			view = atoi(optarg);
			if (view < 1 || view > 3)
				view = 1;
			break;
		case 'c':
			cdev = optarg;
			break;
		case 'r':
			rate = atoi(optarg);
			break;
		case 'i':
			wav = optarg;
			if (view != 3)
				view = 2;
			break;
		case 'x':
			lc.ref = optarg;
			view = 3;
			break;
		case 'p':
			lc.play = optarg;
			break;
		case 'a':
			lc.capture = optarg;
			break;
		case 'k':
			lc.ctl = optarg;
			break;
		case 'T':
			secs = atof(optarg);
			break;
		default:
			usage();
			return 1;
		}
	}
	if (font_find(&font)) {
		fprintf(stderr, "es1869scope: no console font found\n");
		return 2;
	}
	if (view == 3 && lc.ref)
		lc.rec = wav;
	if (view == 3 && lc.ref && !lc.rec) {
		fprintf(stderr, "es1869scope: -x needs -i, what came back\n");
		return 1;
	}
	if (follow(&d, log) < 0 && view == 1) {
		fprintf(stderr, "es1869scope: %s: %s\n", log, strerror(errno));
		return 2;
	}
	if (shot) {
		if (canvas_init(&c, w, h))
			return 2;
		c.font = &font;
		if (view == 3) {
			long long until;

			signal(SIGINT, on_signal);
			signal(SIGTERM, on_signal);
			signal(SIGHUP, on_signal);
			lv = loopview_open(&lc);
			if (!lv)
				return 2;
			// a live loop for -T seconds of segments, and no more
			// than that again while it finds its lag
			until = time(NULL) + (long long)(2 * secs) + 10;
			while (!lc.ref && !stop && time(NULL) < until &&
			       loopview_seconds(lv) < secs) {
				if (loopview_step(lv, &c) < 0)
					break;
				usleep(10000);
			}
			loopview_frame(lv, &c);
			loopview_report(lv, stdout);
			loopview_close(lv);
		} else if (view == 2) {
			sp = spectro_open(cdev, rate, wav, 10, 1);
			if (!sp)
				return 2;
			spectro_frame(sp, &c);
			// a device for two seconds, a file whole
			for (o = 0; o < (wav ? 1 : 200); o++) {
				if (spectro_step(sp, &c) < 0)
					break;
				if (!wav)
					usleep(10000);
			}
			spectro_close(sp);
		} else {
			draw(&c, &d);
		}
		if (canvas_ppm(&c, shot)) {
			perror(shot);
			return 2;
		}
		return 0;
	}
	if (fb_open(&fb, dev)) {
		fprintf(stderr, "es1869scope: %s: %s\n", dev, strerror(errno));
		return 2;
	}
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, on_signal);
	if (canvas_init(&c, fb.w, fb.h)) {
		fb_close(&fb);
		return 2;
	}
	c.font = &font;
	while (!stop) {
		int k = fb_key(&fb);

		if (k == 'q')
			break;
		if (k == ' ')
			held = !held;
		if (k == 'r')
			fb.redraw = 1;
		if (view == 3 && lv && (k == 'c' || k == 't' || k == 'o'))
			loopview_key(lv, k);
		if ((k == '3' || (view == 3 && !lv)) && !lv) {
			// the loop's streams and mixer only while the view shows
			spectro_close(sp);
			sp = NULL;
			lv = loopview_open(&lc);
			view = lv ? 3 : 1;
			if (lv)
				loopview_frame(lv, &c);
			fb.redraw = 1;
		} else if ((k == '1' || k == '2') && lv) {
			loopview_close(lv);
			lv = NULL;
			view = 1;
			fb.redraw = 1;
		}
		if ((k == '2' || (view == 2 && !sp)) && !sp) {
			// the capture is taken only while the view shows
			sp = spectro_open(cdev, rate, wav, 10, 1);
			view = sp ? 2 : 1;
			if (sp)
				spectro_frame(sp, &c);
			fb.redraw = 1;
		} else if (k == '1' && sp) {
			spectro_close(sp);
			sp = NULL;
			view = 1;
			fb.redraw = 1;
		}
		fb_poll_switch(&fb);
		if (view == 3) {
			int r = held ? 0 : loopview_step(lv, &c);

			if (r < 0) {
				loopview_close(lv);
				lv = NULL;
				view = 1;
				fb.redraw = 1;
				continue;
			}
			if (fb.redraw)
				loopview_frame(lv, &c);
			if (fb.shown && (r || fb.redraw))
				fb_present(&fb, &c);
			usleep(10000);
			continue;
		}
		if (view == 2) {
			if (fb.redraw)
				spectro_frame(sp, &c);
			if (!held && spectro_step(sp, &c) < 0) {
				spectro_close(sp);
				sp = NULL;
				view = 1;
			}
			if (fb.shown)
				fb_present(&fb, &c);
			usleep(10000);
			continue;
		}
		if (!held && follow(&d, log) >= 0 && fb.shown) {
			draw(&c, &d);
			fb_present(&fb, &c);
		} else if (fb.redraw && fb.shown) {
			fb_present(&fb, &c);
		}
		usleep(fb.redraw ? 1000 : 500000);
	}
	loopview_close(lv);
	spectro_close(sp);
	fb_close(&fb);
	canvas_free(&c);
	return 0;
}
