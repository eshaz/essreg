// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * loopview.c: the loop view (loopview.h).
 *
 * Notes:
 *
 * Audio 1 and Audio 2 run from one crystal, so once the lag between what was
 * played and what came back is found, it holds. The view finds it once, from
 * the cross-correlation of the first 65536 frames, and starts again after an
 * xrun or a change of a setting, either of which moves it. The noise is
 * independent between the channels, so the four paths, each output from each
 * input, come out of the same segments, through two complex FFTs a segment.
 * They are solved together, against the inputs' cross-spectrum: noise that is
 * independent still correlates a little over a finite time, and taken one by
 * one the crosstalk would read that, about -16 dB after 4 s, instead of the
 * path's -66.
 *
 * The record mixer takes Audio 2 through 69h with every other input at 0 and
 * the master off, so that nothing reaches the speakers. What the view changes
 * is written first to /var/tmp/es1869scope.UID.mixer, which the next start
 * puts back if this one ended without doing so. 3x can't run while Audio 1
 * records, so Audio 2's output rate goes to 2x at most, and its rate
 * correction off, so that what plays is what was written.
 *
 * With a monitor, what went in is what Audio 2's DAC took, read back by the
 * driver (PCM device 3), and the view plays nothing: the music plays on, and
 * the output and Audio 2's settings stay as they are. Audio 2 at 3x would
 * stall while Audio 1 records, so the view doesn't start then. At 2x it takes
 * every other frame of the monitor, which folds back only the images the
 * driver's filter leaves between 22.05 and 24.1 kHz, to above 20 kHz.
 *
 * At 1x the ADC samples in step with the DAC and folds the DAC's images back
 * into the band, so the top of the loop's response depends on where in a
 * frame the two sample, which moved by 0.44 of a frame between two starts on
 * the Armada: the status line shows it as the delay past the lag.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "loopview.h"

#include <alsa/asoundlib.h>
#include <complex.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "loop.h"
#include "plot.h"
#include "tie.h"

#define LV_LOG2N 13 // Welch segments of 8192 frames, half over the last
#define LV_LAG_LOG2N 16 // the lag, from the first 65536 frames of each
#define LV_RING (1u << 17) // frames kept of each, a power of two
#define LV_CHUNK 1024 // frames a write or a read
#define LV_FIT_LO 200.0 // the band the delay is fitted over, Hz
#define LV_FIT_HI 3000.0
#define LV_IR 32 // impulse response taps each side of 0
#define LV_SAVED 16
#define LV_RESTARTS 20
#define LV_REDRAW_NS 500000000LL

// L from L, R from R, L from R, R from L, and the inputs' cross-spectrum
enum { LL, RR, LR, RL, XX, PATHS };

static const uint32_t lv_rgb[4] = { 0x5aa9ff, 0xffa64d, 0x41c7b9, 0xff7eb6 };
static const char *const lv_name[4] = { "L", "R", "L from R", "R from L" };

struct lv_saved {
	char name[48];
	unsigned int count;
	long v[2];
};

struct loopview {
	struct loopview_cfg cfg;
	unsigned int rate, n, bins;
	struct loop path[PATHS];
	double *seg[4], *zs[4]; // x L, x R, y L, y R: a segment and its spectra
	// live
	snd_pcm_t *play, *cap, *mon;
	unsigned int factor; // monitor frames a frame
	uint64_t mon_frames;
	uint64_t lag_at; // the frame of the last search for the lag
	long long retry; // when to try the streams again
	snd_ctl_t *ctl;
	struct lv_saved saved[LV_SAVED];
	int nsaved, treble, over;
	char journal[64];
	int16_t *xring, *yring; // stereo frames
	uint64_t played, captured, next;
	uint32_t seed[2];
	double amp;
	unsigned int restarts;
	// files
	double *fx, *fy; // stereo frames
	size_t nx, ny;
	// both
	long lag;
	int lagged;
	char why[96];
	long long drawn;
	// the figures, made for each drawing
	double complex *h[4]; // the paths, solved together
	double *coh[2]; // each output's multiple coherence
	double *f, *mag[2], *ph[2], *inc[2], *xt[2], *ir[2], *t, *zir;
	double gain[2], delay[2], coh1k[2];
	int invert[2];
};

static long long lv_now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000LL + t.tv_nsec;
}

// --- the mixer -----------------------------------------------------------------

static int ctl_info(snd_ctl_t *h, const char *name, snd_ctl_elem_id_t *id,
		    snd_ctl_elem_info_t *info)
{
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, name);
	snd_ctl_elem_info_set_id(info, id);
	return snd_ctl_elem_info(h, info);
}

// a control's values, the first two
static int ctl_read(snd_ctl_t *h, const char *name, long *v,
		    unsigned int *count)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_type_t type;
	unsigned int i;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	if (ctl_info(h, name, id, info) < 0)
		return -1;
	type = snd_ctl_elem_info_get_type(info);
	*count = snd_ctl_elem_info_get_count(info);
	if (*count > 2)
		*count = 2;
	snd_ctl_elem_value_set_id(val, id);
	if (snd_ctl_elem_read(h, val) < 0)
		return -1;
	for (i = 0; i < *count; i++)
		v[i] = type == SND_CTL_ELEM_TYPE_BOOLEAN ?
			       snd_ctl_elem_value_get_boolean(val, i) :
		       type == SND_CTL_ELEM_TYPE_ENUMERATED ?
			       (long)snd_ctl_elem_value_get_enumerated(val, i) :
			       snd_ctl_elem_value_get_integer(val, i);
	return 0;
}

// a control written, each of its values from v, the last one repeated
static int ctl_write(snd_ctl_t *h, const char *name, const long *v,
		     unsigned int count)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_type_t type;
	unsigned int i, all;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	if (!count || ctl_info(h, name, id, info) < 0)
		return -1;
	type = snd_ctl_elem_info_get_type(info);
	all = snd_ctl_elem_info_get_count(info);
	snd_ctl_elem_value_set_id(val, id);
	for (i = 0; i < all; i++) {
		long x = v[i < count ? i : count - 1];

		if (type == SND_CTL_ELEM_TYPE_BOOLEAN)
			snd_ctl_elem_value_set_boolean(val, i, x);
		else if (type == SND_CTL_ELEM_TYPE_ENUMERATED)
			snd_ctl_elem_value_set_enumerated(val, i,
							  (unsigned int)x);
		else
			snd_ctl_elem_value_set_integer(val, i, x);
	}
	return snd_ctl_elem_write(h, val) < 0 ? -1 : 0;
}

// an enumerated control's item, by its name
static int ctl_item(snd_ctl_t *h, const char *name, const char *item)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	unsigned int i, n;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	if (ctl_info(h, name, id, info) < 0 ||
	    snd_ctl_elem_info_get_type(info) != SND_CTL_ELEM_TYPE_ENUMERATED)
		return -1;
	n = snd_ctl_elem_info_get_items(info);
	for (i = 0; i < n; i++) {
		snd_ctl_elem_info_set_item(info, i);
		if (snd_ctl_elem_info(h, info) < 0)
			return -1;
		if (!strcmp(snd_ctl_elem_info_get_item_name(info), item))
			return (int)i;
	}
	return -1;
}

// what the run changed, before it changes anything more: a line a control
static void journal_write(struct loopview *v)
{
	char tmp[80];
	FILE *f;
	int i;

	snprintf(tmp, sizeof(tmp), "%s.new", v->journal);
	f = fopen(tmp, "w");
	if (!f)
		return;
	for (i = 0; i < v->nsaved; i++)
		fprintf(f, "%u %ld %ld %s\n", v->saved[i].count,
			v->saved[i].v[0], v->saved[i].v[1], v->saved[i].name);
	if (fflush(f) || fsync(fileno(f))) {
		fclose(f);
		unlink(tmp);
		return;
	}
	if (!fclose(f))
		rename(tmp, v->journal);
}

// what an earlier run left changed, put back
static void journal_apply(snd_ctl_t *h, const char *path)
{
	FILE *f = fopen(path, "r");
	char line[128];

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		long val[2];
		unsigned int count;
		int at = 0;

		line[strcspn(line, "\n")] = 0;
		if (sscanf(line, "%u %ld %ld %n", &count, &val[0], &val[1],
			   &at) >= 3 &&
		    at)
			ctl_write(h, line + at, val, count);
	}
	fclose(f);
	unlink(path);
	fprintf(stderr,
		"es1869scope: put back the mixer an earlier run left\n");
}

// a control set for the run, its old value saved first
static int lv_set(struct loopview *v, const char *name, long a, long b)
{
	long old[2], val[2] = { a, b };
	unsigned int count;
	int i;

	if (ctl_read(v->ctl, name, old, &count) < 0)
		return -1;
	for (i = 0; i < v->nsaved; i++)
		if (!strcmp(v->saved[i].name, name))
			break;
	if (i == v->nsaved) {
		struct lv_saved *s;

		if (v->nsaved == LV_SAVED)
			return -1;
		s = &v->saved[v->nsaved++];
		snprintf(s->name, sizeof(s->name), "%s", name);
		s->count = count;
		s->v[0] = old[0];
		s->v[1] = count > 1 ? old[1] : old[0];
		journal_write(v);
	}
	return ctl_write(v->ctl, name, val, 2);
}

static void lv_restore(struct loopview *v)
{
	int i;

	for (i = v->nsaved - 1; i >= 0; i--)
		ctl_write(v->ctl, v->saved[i].name, v->saved[i].v,
			  v->saved[i].count);
	v->nsaved = 0;
	unlink(v->journal);
}

static void lv_settings(struct loopview *v)
{
	long val[2];
	unsigned int n;

	v->treble = ctl_read(v->ctl,
			     "Audio 2 Treble Correction Playback Switch", val,
			     &n) ?
			    -1 :
			    (int)val[0];
	v->over = ctl_read(v->ctl, "Audio 2 Output Rate", val, &n) ?
			  -1 :
			  (int)val[0];
}

// Audio 2 into Audio 1 through the record mixer, and nothing else. With the
// view's own noise the master goes off, Audio 2's rate correction off and its
// rate to 2x at most; with the music, those stay, and 3x refuses
static int lv_route(struct loopview *v)
{
	static const char *const quiet[] = {
		"Mic Capture Volume", "Line Capture Volume",
		"FM Capture Volume",  "CD Capture Volume",
		"Aux Capture Volume", "Mono Capture Volume",
		"Mic Boost (+26dB)",
	};
	int pcm = v->cfg.gain[0] >= 0 ? v->cfg.gain[0] : 8;
	int adc = v->cfg.gain[1] >= 0 ? v->cfg.gain[1] : 4;
	long val[2];
	unsigned int i, n;
	int item;

	if (v->mon && !ctl_read(v->ctl, "Audio 2 Output Rate", val, &n) &&
	    val[0] > 1) {
		fprintf(stderr,
			"es1869scope: Audio 2 runs at 3x, which Audio 1's recording "
			"would stall: set \"Audio 2 Output Rate\" to 2x\n");
		return -2;
	}
	if (!v->mon)
		lv_set(v, "Master Playback Switch", 0, 0);
	for (i = 0; i < sizeof(quiet) / sizeof(quiet[0]); i++)
		lv_set(v, quiet[i], 0, 0);
	item = ctl_item(v->ctl, "Capture Source", "Mix");
	if (item < 0 || lv_set(v, "Capture Source", item, item) ||
	    lv_set(v, "Capture Switch", 1, 1) ||
	    lv_set(v, "Capture Volume", adc, adc) ||
	    lv_set(v, "PCM Capture Volume", pcm, pcm))
		return -1;
	if (!v->mon) {
		item = ctl_item(v->ctl, "Audio 2 Rate Correction", "Off");
		if (item >= 0)
			lv_set(v, "Audio 2 Rate Correction", item, item);
		if (!ctl_read(v->ctl, "Audio 2 Output Rate", val, &n) &&
		    val[0] > 1)
			lv_set(v, "Audio 2 Output Rate", 1, 1);
	}
	lv_settings(v);
	return 0;
}

// the monitor at the rate it offers, which is Audio 2's ring's while Audio 2
// is prepared: Audio 1's rate, or twice that
static int lv_mon_open(struct loopview *v)
{
	snd_pcm_hw_params_t *hw;
	snd_pcm_uframes_t frames;
	unsigned int rate = v->rate;
	int err;

	if (v->mon)
		snd_pcm_close(v->mon);
	v->mon = NULL;
	snd_pcm_hw_params_alloca(&hw);
	err = snd_pcm_open(&v->mon, v->cfg.monitor, SND_PCM_STREAM_CAPTURE,
			   SND_PCM_NONBLOCK);
	if (err < 0) {
		v->mon = NULL;
		goto bad;
	}
	frames = v->rate / 5;
	if ((err = snd_pcm_hw_params_any(v->mon, hw)) < 0 ||
	    (err = snd_pcm_hw_params_set_access(
		     v->mon, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
	    (err = snd_pcm_hw_params_set_format(v->mon, hw,
						SND_PCM_FORMAT_S16_LE)) < 0 ||
	    (err = snd_pcm_hw_params_set_channels(v->mon, hw, 2)) < 0 ||
	    (err = snd_pcm_hw_params_set_rate_near(v->mon, hw, &rate, NULL)) <
		    0 ||
	    (err = snd_pcm_hw_params_set_buffer_size_near(v->mon, hw,
							  &frames)) < 0 ||
	    (err = snd_pcm_hw_params(v->mon, hw)) < 0)
		goto bad;
	if (rate % v->rate || rate / v->rate > 2) {
		snprintf(
			v->why, sizeof(v->why),
			"the monitor runs at %u Hz: Audio 2 at 1x or 2x, please",
			rate);
		return -1;
	}
	v->factor = rate / v->rate;
	return 0;
bad:
	snprintf(v->why, sizeof(v->why), "%s: %s", v->cfg.monitor,
		 snd_strerror(err));
	if (v->mon)
		snd_pcm_close(v->mon);
	v->mon = NULL;
	return -1;
}

// --- the streams ---------------------------------------------------------------

static int lv_pcm(snd_pcm_t **p, const char *dev, snd_pcm_stream_t dir,
		  unsigned int rate)
{
	int err;

	*p = NULL;
	err = snd_pcm_open(p, dev, dir, SND_PCM_NONBLOCK);
	if (!err)
		err = snd_pcm_set_params(*p, SND_PCM_FORMAT_S16_LE,
					 SND_PCM_ACCESS_RW_INTERLEAVED, 2, rate,
					 0, 200000);
	if (err < 0) {
		fprintf(stderr, "es1869scope: %s: %s\n", dev,
			snd_strerror(err));
		if (*p)
			snd_pcm_close(*p);
		*p = NULL;
		return -1;
	}
	return 0;
}

// uniform noise of the run's level, each channel its own
static int16_t lv_noise(uint32_t *s, double amp)
{
	uint32_t x = *s;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*s = x;
	return (int16_t)lrint(((double)x / 2147483648.0 - 1.0) * amp);
}

// a segment's spectra into the four paths' sums
static void lv_add(struct loopview *v)
{
	loop_spectra(&v->path[LL], v->seg[0], v->seg[1], v->zs[0], v->zs[1]);
	loop_spectra(&v->path[LL], v->seg[2], v->seg[3], v->zs[2], v->zs[3]);
	loop_add_spectra(&v->path[LL], v->zs[0], v->zs[2]);
	loop_add_spectra(&v->path[RR], v->zs[1], v->zs[3]);
	loop_add_spectra(&v->path[LR], v->zs[1], v->zs[2]);
	loop_add_spectra(&v->path[RL], v->zs[0], v->zs[3]);
	// its sxy is L times the conjugate of R
	loop_add_spectra(&v->path[XX], v->zs[1], v->zs[0]);
}

static void lv_reset(struct loopview *v)
{
	int i;

	for (i = 0; i < PATHS; i++)
		loop_reset(&v->path[i]);
	v->lagged = 0;
}

// the lag of y behind x from their first 2^lg frames of the left channel, if
// they correlate enough to call it a loop
static int lv_lag(struct loopview *v, const double *x, const double *y,
		  unsigned int lg)
{
	long n = 1L << lg, k, lag;
	double frac, num = 0, ex = 0, ey = 0;

	lag = loop_lag(x, y, lg, &frac);
	for (k = 0; k < n; k++) {
		if (k + lag < 0 || k + lag >= n)
			continue;
		num += x[k] * y[k + lag];
		ex += x[k] * x[k];
		ey += y[k + lag] * y[k + lag];
	}
	if (ex <= 0 || ey <= 0 || fabs(num) < 0.3 * sqrt(ex * ey)) {
		snprintf(
			v->why, sizeof(v->why),
			"no loop: what came back doesn't follow what was played");
		return -1;
	}
	v->lag = lag;
	v->lagged = 1;
	v->why[0] = 0;
	return 0;
}

// the lag from the latest frames of each, so that it doesn't matter when
// either began: y's frame is x's plus the lag
static int lv_live_lag(struct loopview *v)
{
	unsigned int n = 1u << LV_LAG_LOG2N, k;
	uint64_t x0 = v->played - n, y0 = v->captured - n;
	double *x = malloc(2 * (size_t)n * sizeof(double)), *y;
	int r;

	if (!x)
		return -1;
	y = x + n;
	for (k = 0; k < n; k++) {
		x[k] = v->xring[2 * ((x0 + k) % LV_RING)];
		y[k] = v->yring[2 * ((y0 + k) % LV_RING)];
	}
	v->lag_at = v->captured;
	r = lv_lag(v, x, y, LV_LAG_LOG2N);
	free(x);
	if (!r) {
		v->lag += (long)(int64_t)(y0 - x0);
		v->next = y0;
	}
	return r;
}

// every whole segment that has come back, against what was played
static void lv_live_segments(struct loopview *v)
{
	unsigned int n = v->n, k;

	if (!v->lagged) {
		// both a window's worth in, and half a window since a search
		// that found nothing, as music may be quiet for a while
		if (v->captured < (1u << LV_LAG_LOG2N) ||
		    v->played < (1u << LV_LAG_LOG2N) ||
		    (v->lag_at &&
		     v->captured - v->lag_at < (1u << (LV_LAG_LOG2N - 1))))
			return;
		if (lv_live_lag(v))
			return;
	}
	while (v->next + n <= v->captured) {
		int64_t xs = (int64_t)v->next - v->lag;

		if (xs >= 0 && (uint64_t)xs + n <= v->played &&
		    v->played - (uint64_t)xs <= LV_RING &&
		    v->captured - v->next <= LV_RING) {
			for (k = 0; k < n; k++) {
				const int16_t *xp =
					&v->xring[2 * ((xs + k) % LV_RING)];
				const int16_t *yp =
					&v->yring[2 * ((v->next + k) % LV_RING)];

				v->seg[0][k] = xp[0];
				v->seg[1][k] = xp[1];
				v->seg[2][k] = yp[0];
				v->seg[3][k] = yp[1];
			}
			lv_add(v);
		}
		v->next += n / 2;
	}
}

static int lv_feed(struct loopview *v)
{
	int16_t buf[2 * LV_CHUNK];

	for (;;) {
		snd_pcm_sframes_t room = snd_pcm_avail_update(v->play), w;
		snd_pcm_sframes_t i;

		if (room < 0)
			return (int)room;
		if (room < LV_CHUNK / 4)
			return 0;
		if (room > LV_CHUNK)
			room = LV_CHUNK;
		for (i = 0; i < room; i++) {
			int16_t *at =
				&v->xring[2 * ((v->played + i) % LV_RING)];

			buf[2 * i] = lv_noise(&v->seed[0], v->amp);
			buf[2 * i + 1] = lv_noise(&v->seed[1], v->amp);
			at[0] = buf[2 * i];
			at[1] = buf[2 * i + 1];
		}
		w = snd_pcm_writei(v->play, buf, room);
		if (w == -EAGAIN)
			return 0;
		if (w < 0)
			return (int)w;
		v->played += w;
	}
}

// what the monitor has, every factor-th frame of it
static int lv_take(struct loopview *v)
{
	int16_t buf[2 * LV_CHUNK];

	for (;;) {
		snd_pcm_sframes_t r = snd_pcm_readi(v->mon, buf, LV_CHUNK), i;

		if (r == -EAGAIN)
			return 0;
		if (r < 0)
			return (int)r;
		for (i = 0; i < r; i++, v->mon_frames++) {
			int16_t *at;

			if (v->mon_frames % v->factor)
				continue;
			at = &v->xring[2 * (v->played % LV_RING)];
			at[0] = buf[2 * i];
			at[1] = buf[2 * i + 1];
			v->played++;
		}
	}
}

static int lv_pull(struct loopview *v)
{
	int16_t buf[2 * LV_CHUNK];

	for (;;) {
		snd_pcm_sframes_t r = snd_pcm_readi(v->cap, buf, LV_CHUNK), i;

		if (r == -EAGAIN)
			return 0;
		if (r < 0)
			return (int)r;
		for (i = 0; i < r; i++)
			memcpy(&v->yring[2 * ((v->captured + i) % LV_RING)],
			       &buf[2 * i], 4);
		v->captured += r;
		lv_live_segments(v);
	}
}

// both streams from the start, the sums cleared
static int lv_start(struct loopview *v)
{
	lv_reset(v);
	v->played = v->captured = v->next = v->mon_frames = v->lag_at = 0;
	v->why[0] = 0;
	if (v->cfg.monitor) {
		// a new monitor each time, at whatever rate Audio 2 runs now
		if (lv_mon_open(v))
			return -1;
		snd_pcm_drop(v->cap);
		if (snd_pcm_prepare(v->mon) < 0 ||
		    snd_pcm_prepare(v->cap) < 0 || snd_pcm_start(v->mon) < 0 ||
		    snd_pcm_start(v->cap) < 0) {
			snprintf(v->why, sizeof(v->why),
				 "the streams didn't start");
			return -1;
		}
		return 0;
	}
	snd_pcm_drop(v->play);
	snd_pcm_drop(v->cap);
	if (snd_pcm_prepare(v->play) < 0 || snd_pcm_prepare(v->cap) < 0 ||
	    snd_pcm_start(v->cap) < 0)
		return -1;
	// the playback starts once its buffer is full
	return lv_feed(v) < 0 ? -1 : 0;
}

// --- files ---------------------------------------------------------------------

// a WAV file's frames as stereo doubles in -1..1: 16 or 24-bit, one or two
// channels, a single channel on both sides
static double *lv_wav(const char *path, size_t *frames, unsigned int *rate)
{
	FILE *f = fopen(path, "rb");
	unsigned char h[12], ck[8], fm[16];
	unsigned int ch = 0, bits = 0;
	double *out = NULL;

	if (!f || fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) ||
	    memcmp(h + 8, "WAVE", 4))
		goto bad;
	while (fread(ck, 1, 8, f) == 8) {
		uint32_t len = ck[4] | ck[5] << 8 | ck[6] << 16 |
			       (uint32_t)ck[7] << 24;

		if (!memcmp(ck, "fmt ", 4)) {
			if (len < 16 || fread(fm, 1, 16, f) != 16)
				goto bad;
			ch = fm[2] | fm[3] << 8;
			*rate = fm[4] | fm[5] << 8 | fm[6] << 16;
			bits = fm[14] | fm[15] << 8;
			fseek(f, (long)(len - 16 + (len & 1)), SEEK_CUR);
		} else if (!memcmp(ck, "data", 4)) {
			unsigned int bytes = bits / 8, fb = bytes * ch;
			unsigned char *raw;
			size_t i, n;

			if ((bits != 16 && bits != 24) || !ch || ch > 2)
				goto bad;
			n = len / fb;
			raw = malloc(n * fb);
			out = malloc(2 * n * sizeof(double));
			if (!raw || !out || fread(raw, fb, n, f) != n) {
				free(raw);
				goto bad;
			}
			for (i = 0; i < 2 * n; i++) {
				const unsigned char *p =
					raw + (i / 2) * fb +
					(ch == 2 ? (i % 2) * bytes : 0);
				int32_t s =
					bytes == 2 ?
						(int16_t)(p[0] | p[1] << 8) :
						(int32_t)((uint32_t)p[0] << 8 |
							  (uint32_t)p[1] << 16 |
							  (uint32_t)p[2]
								  << 24) >>
							8;

				out[i] = s / (bytes == 2 ? 32768.0 : 8388608.0);
			}
			free(raw);
			fclose(f);
			*frames = n;
			return out;
		} else {
			fseek(f, (long)(len + (len & 1)), SEEK_CUR);
		}
	}
bad:
	if (f)
		fclose(f);
	free(out);
	fprintf(stderr, "es1869scope: %s: not a 16 or 24-bit WAV\n", path);
	return NULL;
}

static int lv_files(struct loopview *v)
{
	unsigned int rx = 0, ry = 0, lg = LV_LAG_LOG2N, n = v->n, k;
	double *x, *y;
	size_t s;

	v->fx = lv_wav(v->cfg.ref, &v->nx, &rx);
	v->fy = lv_wav(v->cfg.rec, &v->ny, &ry);
	if (!v->fx || !v->fy)
		return -1;
	if (rx != ry) {
		fprintf(stderr, "es1869scope: %s at %u Hz, %s at %u Hz\n",
			v->cfg.ref, rx, v->cfg.rec, ry);
		return -1;
	}
	v->rate = ry;
	while (lg > LV_LOG2N && ((size_t)1 << lg) > v->nx)
		lg--;
	while (lg > LV_LOG2N && ((size_t)1 << lg) > v->ny)
		lg--;
	if (((size_t)1 << lg) > v->nx || ((size_t)1 << lg) > v->ny) {
		fprintf(stderr, "es1869scope: the files are too short\n");
		return -1;
	}
	x = malloc(2 * ((size_t)1 << lg) * sizeof(double));
	if (!x)
		return -1;
	y = x + ((size_t)1 << lg);
	for (s = 0; s < ((size_t)1 << lg); s++) {
		x[s] = v->fx[2 * s];
		y[s] = v->fy[2 * s];
	}
	k = lv_lag(v, x, y, lg);
	free(x);
	if (k)
		return 0; // the reason shows
	for (s = v->lag > 0 ? (size_t)v->lag : 0;
	     s + n <= v->ny && (long)s - v->lag + (long)n <= (long)v->nx;
	     s += n / 2) {
		size_t xs = (size_t)((long)s - v->lag);

		for (k = 0; k < n; k++) {
			v->seg[0][k] = v->fx[2 * (xs + k)];
			v->seg[1][k] = v->fx[2 * (xs + k) + 1];
			v->seg[2][k] = v->fy[2 * (s + k)];
			v->seg[3][k] = v->fy[2 * (s + k) + 1];
		}
		lv_add(v);
	}
	return 0;
}

// --- open and close ------------------------------------------------------------

struct loopview *loopview_open(const struct loopview_cfg *cfg)
{
	struct loopview *v = calloc(1, sizeof(*v));
	unsigned int i, n = 1u << LV_LOG2N;

	if (!v)
		return NULL;
	v->cfg = *cfg;
	v->n = n;
	v->bins = n / 2 + 1;
	v->rate = cfg->rate ? cfg->rate : 44100;
	v->treble = v->over = -1;
	for (i = 0; i < PATHS; i++)
		if (loop_init(&v->path[i], LV_LOG2N))
			goto bad;
	for (i = 0; i < 4; i++) {
		v->h[i] = calloc(v->bins, sizeof(double complex));
		v->seg[i] = malloc(n * sizeof(double));
		v->zs[i] = malloc((n + 2) * sizeof(double));
		if (!v->h[i] || !v->seg[i] || !v->zs[i])
			goto bad;
	}
	v->f = calloc(v->bins, sizeof(double));
	v->t = calloc(2 * LV_IR + 1, sizeof(double));
	v->zir = calloc(2 * (size_t)n, sizeof(double));
	if (!v->f || !v->t || !v->zir)
		goto bad;
	for (i = 0; i < 2; i++) {
		v->coh[i] = calloc(v->bins, sizeof(double));
		v->mag[i] = calloc(v->bins, sizeof(double));
		v->ph[i] = calloc(v->bins, sizeof(double));
		v->inc[i] = calloc(v->bins, sizeof(double));
		v->xt[i] = calloc(v->bins, sizeof(double));
		v->ir[i] = calloc(2 * LV_IR + 1, sizeof(double));
		if (!v->coh[i] || !v->mag[i] || !v->ph[i] || !v->inc[i] ||
		    !v->xt[i] || !v->ir[i])
			goto bad;
	}
	if (cfg->ref && cfg->rec) {
		if (lv_files(v))
			goto bad;
		return v;
	}
	v->xring = calloc(2 * (size_t)LV_RING, sizeof(int16_t));
	v->yring = calloc(2 * (size_t)LV_RING, sizeof(int16_t));
	if (!v->xring || !v->yring)
		goto bad;
	v->seed[0] = 0x12345678;
	v->seed[1] = 0x9abcdef1;
	v->amp = pow(10, cfg->level / 20) * sqrt(3) * 32767;
	if (v->amp > 32767)
		v->amp = 32767;
	snprintf(v->journal, sizeof(v->journal),
		 "/var/tmp/es1869scope.%u.mixer", (unsigned int)getuid());
	if (snd_ctl_open(&v->ctl, cfg->ctl, 0) < 0) {
		fprintf(stderr, "es1869scope: %s: no mixer\n", cfg->ctl);
		v->ctl = NULL;
		goto bad;
	}
	journal_apply(v->ctl, v->journal);
	// the devices first, so that a busy one leaves the mixer as it was
	if (lv_pcm(&v->cap, cfg->capture, SND_PCM_STREAM_CAPTURE, v->rate))
		goto bad;
	if (cfg->monitor ? lv_mon_open(v) :
			   lv_pcm(&v->play, cfg->play, SND_PCM_STREAM_PLAYBACK,
				  v->rate)) {
		if (v->why[0])
			fprintf(stderr, "es1869scope: %s\n", v->why);
		goto bad;
	}
	i = (unsigned int)-lv_route(v);
	if (i) {
		if (i == 1)
			fprintf(stderr, "es1869scope: the record mixer can't "
					"take the loop\n");
		goto bad;
	}
	if (lv_start(v)) {
		fprintf(stderr, "es1869scope: %s\n",
			v->why[0] ? v->why : "the streams didn't start");
		goto bad;
	}
	return v;
bad:
	loopview_close(v);
	return NULL;
}

void loopview_close(struct loopview *v)
{
	int i;

	if (!v)
		return;
	if (v->play)
		snd_pcm_close(v->play);
	if (v->mon)
		snd_pcm_close(v->mon);
	if (v->cap)
		snd_pcm_close(v->cap);
	if (v->ctl) {
		lv_restore(v);
		snd_ctl_close(v->ctl);
	}
	for (i = 0; i < PATHS; i++)
		loop_free(&v->path[i]);
	for (i = 0; i < 4; i++) {
		free(v->h[i]);
		free(v->seg[i]);
		free(v->zs[i]);
	}
	for (i = 0; i < 2; i++) {
		free(v->coh[i]);
		free(v->mag[i]);
		free(v->ph[i]);
		free(v->inc[i]);
		free(v->xt[i]);
		free(v->ir[i]);
	}
	free(v->f);
	free(v->t);
	free(v->zir);
	free(v->xring);
	free(v->yring);
	free(v->fx);
	free(v->fy);
	free(v);
}

// --- the figures ---------------------------------------------------------------

static double lv_wrap(double a)
{
	return a - 2 * M_PI * floor((a + M_PI) / (2 * M_PI));
}

static unsigned int lv_bin(const struct loopview *v, double hz)
{
	long k = lrint(hz * v->n / v->rate);

	return k < 0 ? 0 : k >= (long)v->bins ? v->bins - 1 : (unsigned int)k;
}

static double complex lv_sxy(const struct loop *p, unsigned int k)
{
	return p->sxy[2 * k] + I * p->sxy[2 * k + 1];
}

// the four paths at each bin, solved together: H = Syx Sxx^-1, with Sxx the
// inputs' 2 x 2 cross-spectrum. Where the inputs are nearly one, as music in
// mono is, the solve has nothing to stand on, and each path is taken alone,
// with no crosstalk to show
static void lv_solve(struct loopview *v)
{
	unsigned int k;

	for (k = 0; k < v->bins; k++) {
		double a = v->path[LL].sxx[k], b = v->path[RR].sxx[k];
		double yl = v->path[LL].syy[k], yr = v->path[RR].syy[k];
		double complex c = lv_sxy(&v->path[XX], k);
		double complex sll = lv_sxy(&v->path[LL], k);
		double complex slr = lv_sxy(&v->path[LR], k);
		double complex srl = lv_sxy(&v->path[RL], k);
		double complex srr = lv_sxy(&v->path[RR], k);
		double det = a * b - creal(c * conj(c));

		if (a <= 0 || b <= 0) {
			v->h[LL][k] = v->h[RR][k] = v->h[LR][k] = v->h[RL][k] =
				0;
			v->coh[0][k] = v->coh[1][k] = 0;
			continue;
		}
		if (det < 1e-3 * a * b) {
			v->h[LL][k] = sll / a;
			v->h[RR][k] = srr / b;
			v->h[LR][k] = v->h[RL][k] = NAN;
			v->coh[0][k] =
				yl > 0 ? creal(sll * conj(sll)) / (a * yl) : 0;
			v->coh[1][k] =
				yr > 0 ? creal(srr * conj(srr)) / (b * yr) : 0;
			continue;
		}
		v->h[LL][k] = (sll * b - slr * conj(c)) / det;
		v->h[LR][k] = (slr * a - sll * c) / det;
		v->h[RL][k] = (srl * b - srr * conj(c)) / det;
		v->h[RR][k] = (srr * a - srl * c) / det;
		// each output's multiple coherence: what both inputs explain of it
		v->coh[0][k] = yl > 0 ? creal(v->h[LL][k] * conj(sll) +
					      v->h[LR][k] * conj(slr)) /
						yl :
					0;
		v->coh[1][k] = yr > 0 ? creal(v->h[RL][k] * conj(srl) +
					      v->h[RR][k] * conj(srr)) /
						yr :
					0;
	}
}

// the impulse response of each channel, from the response by the inverse FFT,
// upright where the path inverts
static void lv_ir(struct loopview *v)
{
	unsigned int n = v->n, k, c;
	double *z = v->zir;
	int t;

	for (c = 0; c < 2; c++) {
		memset(z, 0, 2 * (size_t)n * sizeof(double));
		// the conjugate of the response, whose forward transform over n
		// is the response's inverse one
		for (k = 0; k <= n / 2; k++) {
			double re = creal(v->h[c][k]), im = cimag(v->h[c][k]);

			z[2 * k] = re;
			z[2 * k + 1] = -im;
			if (k && k < n / 2) {
				z[2 * (n - k)] = re;
				z[2 * (n - k) + 1] = im;
			}
		}
		tie_fft(z, v->path[c].log2n);
		for (t = -LV_IR; t <= LV_IR; t++) {
			unsigned int i = (unsigned int)(t + (int)n) % n;

			v->ir[c][t + LV_IR] =
				z[2 * i] / n * (v->invert[c] ? -1 : 1);
		}
	}
	for (t = -LV_IR; t <= LV_IR; t++)
		v->t[t + LV_IR] = t;
}

static void lv_figures(struct loopview *v)
{
	double hz = (double)v->rate / v->n;
	unsigned int k1 = lv_bin(v, 1000), k0 = lv_bin(v, LV_FIT_LO);
	unsigned int kh = lv_bin(v, LV_FIT_HI), k, c;

	lv_solve(v);
	for (k = 0; k < v->bins; k++)
		v->f[k] = k * hz;
	for (c = 0; c < 2; c++) {
		const double complex *h = v->h[c];
		double m1 = cabs(h[k1]), u = 0, prev = 0, slope = 0, icpt = 0;
		double sw = 0, swf = 0, swu = 0, swff = 0, swfu = 0, den;

		v->gain[c] = m1 > 0 ? 20 * log10(m1) : NAN;
		v->coh1k[c] = v->coh[c][k1];
		// the delay: the phase unwrapped over the fit band, and the line
		// through it by least squares, weighted by the coherence
		for (k = k0; k <= kh; k++) {
			double ph = carg(h[k]), co = v->coh[c][k], w;

			u = k == k0 ? ph : u + lv_wrap(ph - prev);
			prev = ph;
			w = co / fmax(1 - co, 1e-6);
			sw += w;
			swf += w * v->f[k];
			swu += w * u;
			swff += w * v->f[k] * v->f[k];
			swfu += w * v->f[k] * u;
		}
		den = sw * swff - swf * swf;
		if (sw > 0 && den != 0) {
			slope = (sw * swfu - swf * swu) / den;
			icpt = lv_wrap((swu - slope * swf) / sw);
		}
		v->invert[c] = fabs(icpt) > M_PI / 2;
		v->delay[c] = -slope / (2 * M_PI) * v->rate;
		for (k = 0; k < v->bins; k++) {
			double m = cabs(h[k]), turn = v->invert[c] ? M_PI : 0;

			v->mag[c][k] =
				k && m > 0 && m1 > 0 ? 20 * log10(m / m1) : NAN;
			v->ph[c][k] = k && m > 0 ?
					      lv_wrap(carg(h[k]) -
						      slope * v->f[k] - turn) *
						      180 / M_PI :
					      NAN;
			v->inc[c][k] = k && m > 0 ?
					       10 * log10(fmax(1 - v->coh[c][k],
							       1e-10)) :
					       NAN;
		}
	}
	// the crosstalk, each side's against its own path
	for (k = 0; k < v->bins; k++) {
		double a = cabs(v->h[LR][k]), b = cabs(v->h[LL][k]);
		double d = cabs(v->h[RL][k]), e = cabs(v->h[RR][k]);

		v->xt[0][k] = k && a > 0 && b > 0 ? 20 * log10(a / b) : NAN;
		v->xt[1][k] = k && d > 0 && e > 0 ? 20 * log10(d / e) : NAN;
	}
	lv_ir(v);
}

double loopview_seconds(const struct loopview *v)
{
	return v->path[LL].segments * (v->n / 2) / (double)v->rate;
}

// --- the drawing ---------------------------------------------------------------

static int lv_range(double *const *s, int count, unsigned int k0,
		    unsigned int k1, double *lo, double *hi)
{
	double a, b;
	int i, have = 0;

	for (i = 0; i < count; i++)
		if (range_of(s[i] + k0, k1 - k0, &a, &b, 0)) {
			*lo = have ? fmin(*lo, a) : a;
			*hi = have ? fmax(*hi, b) : b;
			have = 1;
		}
	return have;
}

static void lv_band(struct axes *a, const struct loopview *v)
{
	a->logx = 1;
	a->xunit = "Hz";
	a->yunit = "";
	a->x0 = 20;
	a->x1 = v->rate / 2.0;
}

static void lv_panel_mag(struct loopview *v, struct canvas *c, int bx, int by,
			 int bw, int bh)
{
	static const double at[] = { 10000, 16000, 20000, 22000 };
	struct axes a = { 0 };
	unsigned int k0 = lv_bin(v, 20), i;
	int fh = c->font->h, y;
	double lo, hi;

	lv_band(&a, v);
	if (!lv_range(v->mag, 2, k0, v->bins, &lo, &hi)) {
		lo = -10;
		hi = 2;
	}
	if (hi - lo < 2) {
		lo = (hi + lo) / 2 - 1;
		hi = lo + 2;
	}
	a.y0 = lo;
	a.y1 = hi;
	axes_draw(c, &a, bx, by, bw, bh, "response against 1 kHz, dB");
	for (i = 0; i < 2; i++)
		axes_series(c, &a, v->f + k0, v->mag[i] + k0, v->bins - k0,
			    lv_rgb[i]);
	for (i = 0; i < 2; i++)
		axes_key(c, &a, (int)i, lv_rgb[i], lv_name[i]);
	// how much filter: the top of the band, in figures
	y = a.y + a.h - (int)(sizeof(at) / sizeof(at[0])) * (fh + 1) - 4;
	for (i = 0; i < sizeof(at) / sizeof(at[0]); i++, y += fh + 1) {
		unsigned int k = lv_bin(v, at[i]);

		if (at[i] >= v->rate / 2.0)
			continue;
		textf(c, a.x + 6, y, C_TEXT, "%2.0f kHz %+6.2f %+6.2f dB",
		      at[i] / 1000, v->mag[0][k], v->mag[1][k]);
	}
}

static void lv_panel_phase(struct loopview *v, struct canvas *c, int bx, int by,
			   int bw, int bh)
{
	struct axes a = { 0 };
	unsigned int k0 = lv_bin(v, 20), i;
	double lo, hi;

	lv_band(&a, v);
	if (!lv_range(v->ph, 2, k0, v->bins, &lo, &hi)) {
		lo = -10;
		hi = 10;
	}
	if (hi - lo < 10) {
		lo = (hi + lo) / 2 - 5;
		hi = lo + 10;
	}
	a.y0 = lo;
	a.y1 = hi;
	axes_draw(c, &a, bx, by, bw, bh, "phase past the delay, degrees");
	for (i = 0; i < 2; i++)
		axes_series(c, &a, v->f + k0, v->ph[i] + k0, v->bins - k0,
			    lv_rgb[i]);
	for (i = 0; i < 2; i++)
		axes_key(c, &a, (int)i, lv_rgb[i], lv_name[i]);
}

static void lv_panel_rest(struct loopview *v, struct canvas *c, int bx, int by,
			  int bw, int bh)
{
	double *s[4] = { v->inc[0], v->inc[1], v->xt[0], v->xt[1] };
	static const char *const names[4] = { "1-coh L", "1-coh R", "L from R",
					      "R from L" };
	struct axes a = { 0 };
	unsigned int k0 = lv_bin(v, 20), i;
	double lo, hi;

	lv_band(&a, v);
	if (!lv_range(s, 4, k0, v->bins, &lo, &hi)) {
		lo = -100;
		hi = 0;
	}
	a.y0 = fmax(lo, -100);
	a.y1 = fmax(hi, a.y0 + 10);
	axes_draw(c, &a, bx, by, bw, bh,
		  "what isn't the path, dB: 1 - coherence, crosstalk");
	// the crosstalk first, under the coherence, and the keys over both
	for (i = 4; i-- > 0;)
		axes_series(c, &a, v->f + k0, s[i] + k0, v->bins - k0,
			    lv_rgb[i]);
	for (i = 0; i < 4; i++)
		axes_key(c, &a, (int)i, lv_rgb[i], names[i]);
}

static void lv_panel_ir(struct loopview *v, struct canvas *c, int bx, int by,
			int bw, int bh)
{
	struct axes a = { .xunit = "", .yunit = "" };
	unsigned int i;
	double lo, hi;

	a.x0 = -LV_IR;
	a.x1 = LV_IR;
	if (!lv_range(v->ir, 2, 0, 2 * LV_IR + 1, &lo, &hi)) {
		lo = -1;
		hi = 1;
	}
	a.y0 = lo;
	a.y1 = hi;
	axes_draw(c, &a, bx, by, bw, bh,
		  v->invert[0] ? "impulse response, frames (inverted back)" :
				 "impulse response, frames");
	for (i = 0; i < 2; i++) {
		axes_series(c, &a, v->t, v->ir[i], 2 * LV_IR + 1, lv_rgb[i]);
		axes_points(c, &a, v->t, v->ir[i], 2 * LV_IR + 1, lv_rgb[i]);
	}
	for (i = 0; i < 2; i++)
		axes_key(c, &a, (int)i, lv_rgb[i], lv_name[i]);
}

static void lv_status(struct loopview *v, struct canvas *c)
{
	static const char *const rates[] = { "1x", "2x", "3x" };
	int fh = c->font->h;
	char set[64] = "";

	const char *what = v->mon  ? "the music into Audio 1" :
			   v->play ? "Audio 2 into Audio 1" :
				     "files";

	fill(c, 0, 0, c->w, 2 * fh + 6, C_BACK);
	if (v->play || v->mon)
		snprintf(set, sizeof(set), "treble %s, %s, restarts %u",
			 v->treble < 0 ? "?" :
			 v->treble     ? "on" :
					 "off",
			 v->over >= 0 && v->over < 3 ? rates[v->over] : "?",
			 v->restarts);
	if (!v->lagged) {
		textf(c, 6, 2, C_TEXT, "es1869scope  loop  %s: %s", what,
		      v->why[0]		   ? v->why :
		      v->mon && !v->played ? "waiting for Audio 2 to play" :
					     "finding the lag...");
		text(c, 6, 4 + fh, C_DIM, set);
		return;
	}
	textf(c, 6, 2, C_TEXT,
	      "es1869scope  loop  %s, %u Hz, %u-point Welch, %.1f s, lag %ld %+.3f%s",
	      what, v->rate, v->n, loopview_seconds(v), v->lag, v->delay[0],
	      v->invert[0] ? ", inverted" : "");
	textf(c, 6, 4 + fh, C_DIM,
	      "1 kHz %.2f %.2f dB, coherence %.5f %.5f, R after L %+.2f us  %s",
	      v->gain[0], v->gain[1], v->coh1k[0], v->coh1k[1],
	      (v->delay[1] - v->delay[0]) / v->rate * 1e6, set);
}

static void lv_draw(struct loopview *v, struct canvas *c)
{
	int top = c->font->h * 2 + 8, w = c->w / 2, h = (c->h - top) / 2;

	fill(c, 0, 0, c->w, c->h, C_BACK);
	if (v->lagged && v->path[LL].segments)
		lv_figures(v);
	lv_status(v, c);
	if (!v->lagged || !v->path[LL].segments)
		return;
	lv_panel_mag(v, c, 2, top, w - 3, h - 2);
	lv_panel_phase(v, c, w + 1, top, w - 3, h - 2);
	lv_panel_rest(v, c, 2, top + h, w - 3, h - 2);
	lv_panel_ir(v, c, w + 1, top + h, w - 3, h - 2);
	v->drawn = lv_now();
}

void loopview_frame(struct loopview *v, struct canvas *c)
{
	lv_draw(v, c);
}

int loopview_step(struct loopview *v, struct canvas *c)
{
	unsigned long before = v->path[LL].segments;
	int lagged = v->lagged;

	// the music stopped or changed its rate: try again each second
	if (v->retry) {
		if (lv_now() < v->retry)
			return 0;
		v->retry = lv_start(v) ? lv_now() + 1000000000LL : 0;
		lv_draw(v, c);
		return 1;
	}
	if (v->play || v->mon) {
		int err = v->mon ? lv_take(v) : lv_feed(v);
		char what[48];

		if (err >= 0)
			err = lv_pull(v);
		if (err < 0) {
			snprintf(what, sizeof(what), "%s", snd_strerror(err));
			v->restarts++;
			if (v->cfg.monitor) {
				// a pause or a stop of the music, as often as
				// it comes
				if (lv_start(v))
					v->retry = lv_now() + 1000000000LL;
			} else if (v->restarts > LV_RESTARTS || lv_start(v)) {
				return -1;
			}
			if (!v->retry)
				snprintf(v->why, sizeof(v->why),
					 "%s, started again", what);
			lv_draw(v, c);
			return 1;
		}
	}
	if (v->lagged != lagged || (v->path[LL].segments != before &&
				    lv_now() - v->drawn > LV_REDRAW_NS)) {
		lv_draw(v, c);
		return 1;
	}
	return 0;
}

void loopview_key(struct loopview *v, int key)
{
	long val[2];
	unsigned int n;

	// the music's settings are its own
	if (!v->play && !(v->mon && key == 'c'))
		return;
	if (key == 't' &&
	    !ctl_read(v->ctl, "Audio 2 Treble Correction Playback Switch", val,
		      &n))
		lv_set(v, "Audio 2 Treble Correction Playback Switch", !val[0],
		       !val[0]);
	else if (key == 'o' &&
		 !ctl_read(v->ctl, "Audio 2 Output Rate", val, &n))
		lv_set(v, "Audio 2 Output Rate", !val[0], !val[0]);
	else if (key != 'c')
		return;
	lv_settings(v);
	if (lv_start(v))
		snprintf(v->why, sizeof(v->why), "the streams didn't start");
}

void loopview_report(struct loopview *v, FILE *f)
{
	static const double at[] = { 20,    50,	   100,	  1000,	 5000, 10000,
				     15000, 18000, 20000, 21000, 22000 };
	unsigned int i;

	if (!v->lagged || !v->path[LL].segments) {
		fprintf(f, "none %s\n", v->why);
		return;
	}
	lv_figures(v);
	fprintf(f, "lag %ld\nsegments %lu\nrate %u\n", v->lag,
		v->path[LL].segments, v->rate);
	fprintf(f, "delay %.4f %.4f\ninvert %d %d\ngain %.3f %.3f\n",
		v->delay[0], v->delay[1], v->invert[0], v->invert[1],
		v->gain[0], v->gain[1]);
	for (i = 0; i < sizeof(at) / sizeof(at[0]); i++) {
		unsigned int k = lv_bin(v, at[i]);

		if (at[i] >= v->rate / 2.0)
			continue;
		fprintf(f,
			"at %g mag %.3f %.3f phase %.2f %.2f incoh %.1f %.1f xtalk %.1f %.1f\n",
			at[i], v->mag[0][k], v->mag[1][k], v->ph[0][k],
			v->ph[1][k], v->inc[0][k], v->inc[1][k], v->xt[0][k],
			v->xt[1][k]);
	}
	fprintf(f, "ir");
	for (i = LV_IR - 4; i <= LV_IR + 4; i++)
		fprintf(f, " %.5f", v->ir[0][i]);
	fprintf(f, "\n");
}
