// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * loopview.c: the loop view (loopview.h).
 *
 * Notes:
 *
 * Audio 1 and Audio 2 run from one crystal, so once the lag between what was
 * played and what came back is found, it holds. The view finds it from the
 * cross-correlation of the latest frames of each, and again after an xrun or
 * a change of a setting, either of which moves it. The noise is
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
 * A thread of its own reads both streams into rings as their frames come, so
 * that the figures, a drawing or a block's refit, which take the Celeron
 * longer than Audio 1's buffer of 16384 frames lasts, hold up neither. A
 * break of the music (a pause, a stop, the next file) stops the monitor with
 * an xrun. The view then starts the monitor alone again, at the rate it ran,
 * so that the next start of Audio 2 carries it on, while Audio 1 records on,
 * and finds the lag again. After LV_IDLE_S seconds with no frame from the
 * monitor, nothing plays: Audio 1 and the reader stop, which saved the
 * Armada 6 % of its CPU and half its wakes beside an idle MPD, and the step
 * only asks the monitor whether frames have come, which start both again.
 *
 * Each start puts the DAC and the ADC another fraction of a frame apart, and
 * the monitor's every other frame at 2x half a frame either way, so the
 * cross-spectra of two runs of the music don't add: at the top their phases
 * differ by up to 80 degrees, which took up to 2.3 dB off 20 kHz on the
 * Armada. So a run is measured alone, and its sums start again at the next.
 * Over time, a run's deviation in each band goes into the block, weighted by
 * its segments.
 *
 * Over time, the chip's clock comes from the monitor's positions, which the
 * driver reads from the 8237 right before the core stamps them, after the
 * first second of each run. Audio 1's position moves a period at a time once
 * its correction filter runs, too coarse for a tenth of a ppm.
 *
 * The monitor's frames carry Audio 2's DAC correction, so the loop against
 * them doesn't see it: through the record path that the calibration made
 * flat, it is the DAC itself. The correction takes back what that drifts
 * from where it was over the first LV_EQ_BASE blocks, when the calibration
 * had just made the chain flat.
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
#include <pthread.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "loop.h"
#include "eq.h"
#include "zstep.h"
#include "plot.h"
#include "tie.h"

#define LV_LOG2N 13 // Welch segments of 8192 frames, half over the last
// the lag of a live loop, from 32768 frames of each, a few milliseconds by
// the coarse search, which reach a monitor that starts a lap behind; from
// files, from 65536 frames, which reach the half second a recording may
// start late
#define LV_LAG_LOG2N 15
#define LV_FILE_LAG_LOG2N 16
#define LV_RING (1u << 17) // frames kept of each, a power of two
#define LV_CHUNK 1024 // frames a write or a read
#define LV_FIT_LO 200.0 // the band the delay is fitted over, Hz
#define LV_FIT_HI 3000.0
#define LV_IR 32 // impulse response taps each side of 0
#define LV_SAVED 16
#define LV_RESTARTS 20
#define LV_REDRAW_NS 500000000LL
#define LV_CF_HALF 16 // the correction filter's taps each side (capfilt.h)
#define LV_CF_ONE 16384
#define LV_CAL_MAX 4.0 // the most it may lift, 12 dB
#define LV_CAL_TOP 20000.0 // the top of the band it flattens, Hz
#define LV_CAL_AT 6

static const double lv_cal_at[LV_CAL_AT] = { 1000,  5000,  10000,
					     15000, 18000, 20000 };

#define LV_EQ_BANDS 24 // log-spaced from 250 Hz to 20 kHz
#define LV_EQ_STEP 0.5 // the most an update moves the correction, dB
#define LV_EQ_MIN 0.05 // a deviation smaller than this leaves it alone, dB
#define LV_EQ_COH 0.5 // the least coherence a band counts with
#define LV_EQ_TRIM 500 // the most a trim moves a block, ppb
#define LV_EQ_TRIM_MIN 10 // a move smaller than this is left alone, ppb
#define LV_EQ_STAMPS 256 // the fewest monitor positions a block's clock takes
#define LV_EQ_SETTLE 1.0 // the seconds of a run's start the clock leaves out
#define LV_EQ_RUN 32 // the fewest segments a run of the music counts with
#define LV_EQ_BASE 3 // the blocks the drift is measured from
#define LV_EQ_AFTER 3 // and the blocks after them before it counts
#define LV_EQ_SPAN 3.0 // the most the refits move it from the start, dB
#define LV_READ_US 10000 // the reader's wait between its reads
#define LV_IDLE_S 10 // seconds with no frame from the monitor before resting
#define LV_ZS_LO 1000.0 // the band the DAC's gap at its sign comes from, Hz
#define LV_ZS_HI 8000.0
#define LV_ZS_MOVE 0.1 // the least change of the gap the driver is told, LSB
#define LV_SEG_RHO 0.5 // the least |r| of a segment that counts
#define LV_SEG_BAD 8 // segments in a row under it that lose the lag
#define LV_SEG_QUIET 3.0 // an input this quiet, rms in steps, says nothing
#define LV_EQ_JUMP 0.5 // a block's clock this far off its trend waits, ppm

// the streams, for a start of some of them: the view's input (the monitor or
// the noise) and Audio 1's recording
enum { LV_IN = 1, LV_CAP = 2, LV_BOTH = 3 };

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
	unsigned int want; // the factor asked of the next monitor
	uint64_t mon_frames;
	uint64_t lag_at; // the frame of the last search for the lag
	long long retry; // when to try the streams again
	snd_ctl_t *ctl;
	struct lv_saved saved[LV_SAVED];
	int nsaved, treble, over;
	char journal[64];
	int16_t *xring, *yring; // stereo frames
	uint64_t played, captured, next;
	pthread_t wthread; // the noise's writer
	int writing, wstop, werr;
	pthread_t rthread; // the streams' reader
	int reading, rstop, rerr, rwhich;
	int again; // the streams a retry starts
	int idle; // nothing plays: Audio 1 and the reader stopped
	// the DAC's gap at its sign: each kept monitor frame's count of codes
	// from 0 up among those it stands for, each side's sign in a segment
	// and its spectra, a run's sums, the block's gaps weighted by their
	// segments, and what the driver was last told, LSB
	uint8_t *qring;
	double *segq[2], *zq[2];
	struct zstep zst;
	double zs_w[2], zs_n, zs_set[2];
	int zs_blocks;
	uint64_t seen; // the monitor's frames at seen_at, when they last moved
	long long seen_at;
	long long slowest; // the longest step since the last status line, ns
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
	// the calibration: 0 measures, 1 verifies with the taps in, 2 is done
	int phase;
	// corrections over time: the bands' and the clock's trends, the
	// start, and the trim made. The monitor's positions on the steered
	// clock, a line through each run of it between breaks: this run's sums
	// about its first position, and the block's runs before, about their
	// means; the reader's and the step's, under clk_lock
	struct eq_series band[LV_EQ_BANDS], bias;
	pthread_mutex_t clk_lock;
	int clk_init;
	double cfirst, ct0, cy0, cn, cu, cy, cuu, cuy, cyy;
	double csn, csxx, csxy, csyy, cruns, t0;
	int blocks;
	// the block's bands from its runs before this one: the sums of the
	// coherence and of the deviation weighted by it, and the segments; each
	// band's start over the first LV_EQ_BASE blocks
	double eq_w[LV_EQ_BANDS], eq_v[LV_EQ_BANDS];
	double eq_n[LV_EQ_BANDS]; // what the music doesn't explain, Syy (1 - coh)
	unsigned long eq_segs;
	double base[LV_EQ_BANDS];
	int nbase[LV_EQ_BANDS], based;
	// how what went in and what came back overlay, segment by segment:
	// those in a row that didn't, and the block's sums of |r| at the lag,
	// of the peak's place between frames and its square, and the misses
	unsigned int bad;
	double ov_n, ov_r, ov_d, ov_dd;
	unsigned long ov_bad;
	// the chip's rate a block, its trend, and a block's that jumped from it
	struct eq_series chip_s;
	double chip_out;
	long trim;
	long taps0[3 * EQ_SIDES]; // the DAC's correction when it began
	int have0;
	FILE *eqlog;
	double before[2][LV_CAL_AT]; // the record path before, dB against 1 kHz
	long taps[2][LV_CF_HALF + 1];
	double peak[2]; // the correction's most gain, dB
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

// an integer control of count values, all written
static int ctl_write_all(snd_ctl_t *h, const char *name, const long *v,
			 unsigned int count)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	unsigned int i;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	if (ctl_info(h, name, id, info) < 0 ||
	    snd_ctl_elem_info_get_count(info) != count)
		return -1;
	snd_ctl_elem_value_set_id(val, id);
	for (i = 0; i < count; i++)
		snd_ctl_elem_value_set_integer(val, i, v[i]);
	return snd_ctl_elem_write(h, val) < 0 ? -1 : 0;
}

// an integer control's count values, all read
static int ctl_read_all(snd_ctl_t *h, const char *name, long *v,
			unsigned int count)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *val;
	unsigned int i;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&val);
	if (ctl_info(h, name, id, info) < 0 ||
	    snd_ctl_elem_info_get_count(info) != count)
		return -1;
	snd_ctl_elem_value_set_id(val, id);
	if (snd_ctl_elem_read(h, val) < 0)
		return -1;
	for (i = 0; i < count; i++)
		v[i] = snd_ctl_elem_value_get_integer(val, i);
	return 0;
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
	// the calibrated levels (TEST_LEVELS.md): -1.8 dB from Audio 2 to the
	// recording, clean to full scale
	int pcm = v->cfg.gain[0] >= 0 ? v->cfg.gain[0] : 14;
	int adc = v->cfg.gain[1] >= 0 ? v->cfg.gain[1] : 3;
	long val[2];
	unsigned int i, n;
	int item;

	// over time, beside the music: at 2x, since at 1x the top of the
	// loop's response moves with where the ADC and the DAC sample in a
	// frame (the notes above) and 3x stalls while Audio 1 records, and the
	// DAC's correction on, which it refits
	if (v->cfg.equalize) {
		lv_set(v, "Audio 2 Output Rate", 1, 1);
		lv_set(v, "Audio 2 Treble Correction Playback Switch", 1, 1);
	}
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
	// the calibration measures the record path alone: Audio 2 at 2x with
	// its treble correction is flat to 20 kHz, and the folding of 1x's
	// images is far above the band; the recording without the filter
	if (v->cfg.calibrate) {
		long off[2] = { 0, 0 };

		if (lv_set(v, "Audio 2 Treble Correction Playback Switch", 1,
			   1) ||
		    lv_set(v, "Audio 2 Output Rate", 1, 1) ||
		    ctl_write(v->ctl, "Audio 1 Correction Capture Switch", off,
			      2)) {
			fprintf(stderr, "es1869scope: the driver has no "
					"correction filter to calibrate\n");
			return -2;
		}
	}
	lv_settings(v);
	return 0;
}

// positions stamped on the monotonic clock, which NTP steers as it does the
// boot clock the Clock mode follows
static void lv_tstamp(snd_pcm_t *p)
{
	snd_pcm_sw_params_t *sw;

	snd_pcm_sw_params_alloca(&sw);
	if (!snd_pcm_sw_params_current(p, sw) &&
	    !snd_pcm_sw_params_set_tstamp_mode(p, sw, SND_PCM_TSTAMP_ENABLE) &&
	    !snd_pcm_sw_params_set_tstamp_type(p, sw,
					       SND_PCM_TSTAMP_TYPE_MONOTONIC))
		snd_pcm_sw_params(p, sw);
}

// the monitor at the rate it offers, which is Audio 2's ring's while Audio 2
// is prepared: Audio 1's rate, or twice that. Between two runs of the music
// it asks for the rate the last one ran at, which the next start then carries
// on, where any other would break it off at once
static int lv_mon_open(struct loopview *v)
{
	snd_pcm_hw_params_t *hw;
	snd_pcm_uframes_t frames;
	unsigned int rate = v->rate * (v->want ? v->want : 1);
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
	// half a second at the rate it runs, which at 2x is twice Audio 1's
	frames = v->rate;
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
	v->factor = v->want = rate / v->rate;
	lv_tstamp(v->mon);
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
		  unsigned int rate, unsigned int latency)
{
	int err;

	*p = NULL;
	err = snd_pcm_open(p, dev, dir, SND_PCM_NONBLOCK);
	if (!err)
		err = snd_pcm_set_params(*p, SND_PCM_FORMAT_S16_LE,
					 SND_PCM_ACCESS_RW_INTERLEAVED, 2, rate,
					 0, latency);
	if (!err)
		lv_tstamp(*p);
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

// what the writer has played or the reader taken of the monitor, and what
// the reader has had back, each read whole on a 32-bit CPU too
static uint64_t lv_played(const struct loopview *v)
{
	return __atomic_load_n(&v->played, __ATOMIC_ACQUIRE);
}

static uint64_t lv_captured(const struct loopview *v)
{
	return __atomic_load_n(&v->captured, __ATOMIC_ACQUIRE);
}

#define LV_DECIMATE 8 // the coarse search's step, frames
#define LV_FINE 16 // and the fine one's reach each way about it

// the lag from the latest frames of each, so that it doesn't matter when
// either began: y's frame is x's plus the lag. A search over the full rate
// takes the Celeron longer than Audio 2's buffer lasts, so it goes coarse
// first, both channels' sums averaged over LV_DECIMATE frames, which the
// loop's flat low band carries, and then over LV_FINE frames each way at the
// full rate, sums of products straight; a few milliseconds in all
static int lv_live_lag(struct loopview *v)
{
	unsigned int n = 1u << LV_LAG_LOG2N, m = n / LV_DECIMATE, k, j;
	uint64_t x0 = lv_played(v) - n, y0 = lv_captured(v) - n;
	double *x = malloc(2 * (size_t)n * sizeof(double)), *y, *xd, *yd;
	double best = 0, frac;
	long coarse, lag, l;

	if (!x)
		return -1;
	y = x + n;
	for (k = 0; k < n; k++) {
		const int16_t *xp = &v->xring[2 * ((x0 + k) % LV_RING)];
		const int16_t *yp = &v->yring[2 * ((y0 + k) % LV_RING)];

		x[k] = xp[0] + xp[1];
		y[k] = yp[0] + yp[1];
	}
	v->lag_at = y0 + n;
	xd = malloc(2 * (size_t)m * sizeof(double));
	if (!xd) {
		free(x);
		return -1;
	}
	yd = xd + m;
	for (k = 0; k < m; k++) {
		xd[k] = yd[k] = 0;
		for (j = 0; j < LV_DECIMATE; j++) {
			xd[k] += x[k * LV_DECIMATE + j];
			yd[k] += y[k * LV_DECIMATE + j];
		}
	}
	coarse = loop_lag(xd, yd, LV_LAG_LOG2N - 3, &frac) * LV_DECIMATE;
	free(xd);
	// the fine search, either way up
	lag = coarse;
	for (l = coarse - LV_FINE; l <= coarse + LV_FINE; l++) {
		double sum = 0;

		for (k = 0; k < n; k++)
			if ((long)k + l >= 0 && (long)k + l < (long)n)
				sum += x[k] * y[k + l];
		if (fabs(sum) > best) {
			best = fabs(sum);
			lag = l;
		}
	}
	// lv_lag()'s test of a loop, at that lag
	{
		double num = 0, ex = 0, ey = 0;

		for (k = 0; k < n; k++) {
			if ((long)k + lag < 0 || (long)k + lag >= (long)n)
				continue;
			num += x[k] * y[k + lag];
			ex += x[k] * x[k];
			ey += y[k + lag] * y[k + lag];
		}
		free(x);
		if (ex <= 0 || ey <= 0 || fabs(num) < 0.3 * sqrt(ex * ey)) {
			snprintf(v->why, sizeof(v->why),
				 "no loop: what came back doesn't follow what "
				 "was played (%.2f at %ld)",
				 ex > 0 && ey > 0 ? fabs(num) / sqrt(ex * ey) :
						    0.0,
				 lag);
			return -1;
		}
	}
	v->lag = lag + (long)(int64_t)(y0 - x0);
	v->lagged = 1;
	v->why[0] = 0;
	v->next = y0;
	return 0;
}

static void lv_eq_run(struct loopview *v);

// the run's sums end here: over time into the block, else away
static void lv_run_end(struct loopview *v)
{
	int i;

	if (v->cfg.equalize) {
		lv_eq_run(v);
		return;
	}
	for (i = 0; i < PATHS; i++)
		loop_reset(&v->path[i]);
}

// how what went in and what came back over a segment overlay: their Pearson
// correlation, both channels summed, at the lag and a frame either way, in
// r[0..2], and the input's rms, in steps, in *rms
static void lv_pearson(const struct loopview *v, double *r, double *rms)
{
	unsigned int n = v->n, k;
	double m = n - 2, sx = 0, sxx = 0, vx;
	int d;

	for (k = 1; k < n - 1; k++) {
		double x = v->seg[0][k] + v->seg[1][k];

		sx += x;
		sxx += x * x;
	}
	vx = sxx - sx * sx / m;
	*rms = sqrt(fmax(vx / m, 0)) / 2;
	for (d = -1; d <= 1; d++) {
		double sy = 0, syy = 0, sxy = 0, vy;

		for (k = 1; k < n - 1; k++) {
			double x = v->seg[0][k] + v->seg[1][k];
			double y = v->seg[2][k + d] + v->seg[3][k + d];

			sy += y;
			syy += y * y;
			sxy += x * y;
		}
		vy = syy - sy * sy / m;
		r[d + 1] = vx > 0 && vy > 0 ?
				   (sxy - sx * sy / m) / sqrt(vx * vy) :
				   0;
	}
}

// a segment that has come back, if it overlays what went in: a stop of the
// DAC or a lost block moves one against the other, and the sums take only
// what still overlays. Too many in a row that don't end the run, and the
// lag is found again. Silence says nothing either way
static void lv_segment(struct loopview *v)
{
	double r[3], rms, a, b, c, den;

	lv_pearson(v, r, &rms);
	if (rms < LV_SEG_QUIET)
		return;
	a = fabs(r[0]);
	b = fabs(r[1]);
	c = fabs(r[2]);
	if (b < LV_SEG_RHO) {
		v->ov_bad++;
		if (++v->bad >= LV_SEG_BAD) {
			lv_run_end(v);
			v->lagged = 0;
			v->lag_at = 0;
			v->bad = 0;
			snprintf(
				v->why, sizeof(v->why),
				"what came back stopped overlaying: the lag again");
		}
		return;
	}
	v->bad = 0;
	lv_add(v);
	// over time, the DAC's gap: the sign's spectra beside the segment's
	if (v->mon && v->cfg.equalize) {
		loop_spectra(&v->path[LL], v->segq[0], v->segq[1], v->zq[0],
			     v->zq[1]);
		zstep_add(&v->zst, v->zs[0], v->zs[1], v->zq[0], v->zq[1],
			  v->zs[2], v->zs[3]);
	}
	v->ov_n++;
	v->ov_r += b;
	// the peak between frames, a parabola through the three, later +
	den = a - 2 * b + c;
	if (den < 0) {
		double at = 0.5 * (a - c) / den;

		v->ov_d += at;
		v->ov_dd += at * at;
	}
}

// every whole segment that has come back, against what was played
static void lv_live_segments(struct loopview *v)
{
	unsigned int n = v->n, k;
	uint64_t cap = lv_captured(v), played = lv_played(v);

	if (!v->lagged) {
		// both a window's worth in, and half a window since a search
		// that found nothing, as music may be quiet for a while
		if (cap < (1u << LV_LAG_LOG2N) ||
		    played < (1u << LV_LAG_LOG2N) ||
		    (v->lag_at && cap - v->lag_at < (1u << (LV_LAG_LOG2N - 1))))
			return;
		if (lv_live_lag(v))
			return;
		cap = lv_captured(v);
		played = lv_played(v);
	}
	// one segment a step; behind by more than a few, on to the newest,
	// since a segment takes the Celeron about as long as it lasts
	if (cap >= v->next + 4 * n)
		v->next = (cap - n) / (n / 2) * (n / 2);
	if (v->next + n <= cap) {
		int64_t xs = (int64_t)v->next - v->lag;

		if (xs >= 0 && (uint64_t)xs + n <= played &&
		    played - (uint64_t)xs <= LV_RING - LV_CHUNK &&
		    cap - v->next <= LV_RING - LV_CHUNK) {
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
			// each side's sign, the share of the DAC's codes from
			// 0 up that a kept frame stands for, less a half
			for (k = 0; v->mon && k < n; k++) {
				const uint8_t *q =
					&v->qring[2 * ((xs + k) % LV_RING)];

				v->segq[0][k] = (double)q[0] / v->factor - 0.5;
				v->segq[1][k] = (double)q[1] / v->factor - 0.5;
			}
			lv_segment(v);
		}
		v->next += n / 2;
	}
}

// the noise's writer, a thread of its own with blocking writes, as aplay and
// MPD write Audio 2: each chunk goes to the ring of what was played before it
// goes to the device, and played moves after both
static void *lv_writer(void *arg)
{
	struct loopview *v = arg;
	int16_t buf[2 * LV_CHUNK];

	while (!__atomic_load_n(&v->wstop, __ATOMIC_ACQUIRE)) {
		uint64_t at = v->played;
		snd_pcm_sframes_t i, w;

		for (i = 0; i < LV_CHUNK; i++) {
			int16_t *x = &v->xring[2 * ((at + i) % LV_RING)];

			buf[2 * i] = lv_noise(&v->seed[0], v->amp);
			buf[2 * i + 1] = lv_noise(&v->seed[1], v->amp);
			x[0] = buf[2 * i];
			x[1] = buf[2 * i + 1];
		}
		w = snd_pcm_writei(v->play, buf, LV_CHUNK);
		if (w < 0) {
			__atomic_store_n(&v->werr, (int)w, __ATOMIC_RELEASE);
			break;
		}
		__atomic_store_n(&v->played, at + w, __ATOMIC_RELEASE);
	}
	return NULL;
}

static void lv_writer_stop(struct loopview *v)
{
	if (!v->writing)
		return;
	__atomic_store_n(&v->wstop, 1, __ATOMIC_RELEASE);
	snd_pcm_drop(v->play);
	pthread_join(v->wthread, NULL);
	v->writing = 0;
}

// where the monitor stands on the steered clock: the frames read and those
// waiting, at the core's stamp of the DMA's position, into this run's sums,
// less where the nominal rate would have it, which keeps the sums small
static void lv_stamp(struct loopview *v)
{
	snd_pcm_status_t *st;
	snd_htimestamp_t ts;
	double t, y, nom = (double)v->rate * v->factor;

	snd_pcm_status_alloca(&st);
	if (snd_pcm_status(v->mon, st) < 0 ||
	    snd_pcm_status_get_state(st) != SND_PCM_STATE_RUNNING)
		return;
	snd_pcm_status_get_htstamp(st, &ts);
	if (!ts.tv_sec)
		return;
	t = ts.tv_sec + ts.tv_nsec * 1e-9;
	y = (double)v->mon_frames + snd_pcm_status_get_avail(st);
	pthread_mutex_lock(&v->clk_lock);
	// not the run's first second, where the monitor may still be taking
	// up what the DMA ran through before it
	if (!v->cfirst)
		v->cfirst = t;
	if (t - v->cfirst < LV_EQ_SETTLE) {
		pthread_mutex_unlock(&v->clk_lock);
		return;
	}
	if (!v->cn) {
		v->ct0 = t;
		v->cy0 = y;
	}
	t -= v->ct0;
	y -= v->cy0 + nom * t;
	v->cn++;
	v->cu += t;
	v->cy += y;
	v->cuu += t * t;
	v->cuy += t * y;
	v->cyy += y * y;
	pthread_mutex_unlock(&v->clk_lock);
}

// this run's line into the block's, about its own mean; under clk_lock
static void lv_clock_fold(struct loopview *v)
{
	if (v->cn >= 3) {
		v->csn += v->cn;
		v->csxx += v->cuu - v->cu * v->cu / v->cn;
		v->csxy += v->cuy - v->cu * v->cy / v->cn;
		v->csyy += v->cyy - v->cy * v->cy / v->cn;
		v->cruns++;
	}
	v->cn = v->cu = v->cy = v->cuu = v->cuy = v->cyy = 0;
}

// the sums of the paths, and of the block's bands and clock, afresh
static void lv_clear(struct loopview *v)
{
	int i;

	for (i = 0; i < PATHS; i++)
		loop_reset(&v->path[i]);
	for (i = 0; i < LV_EQ_BANDS; i++)
		v->eq_w[i] = v->eq_v[i] = v->eq_n[i] = 0;
	v->eq_segs = 0;
	pthread_mutex_lock(&v->clk_lock);
	v->cn = v->cu = v->cy = v->cuu = v->cuy = v->cyy = 0;
	v->csn = v->csxx = v->csxy = v->csyy = v->cruns = 0;
	pthread_mutex_unlock(&v->clk_lock);
}

// what the monitor has, every factor-th frame of it; the reader's. A read
// short of a chunk has caught up: the monitor's position follows the DMA as
// it is read, so a read goes on finding a few more frames, and reading on
// until there were none would read for as long as the music plays
static int lv_take(struct loopview *v)
{
	int16_t buf[2 * LV_CHUNK];
	uint64_t at = v->played;
	int got = 0;

	for (;;) {
		snd_pcm_sframes_t r = snd_pcm_readi(v->mon, buf, LV_CHUNK), i;

		if (r == -EAGAIN)
			break;
		if (r < 0)
			return (int)r;
		for (i = 0; i < r; i++, v->mon_frames++) {
			const int16_t *b = &buf[2 * i];
			int16_t *x;
			uint8_t *q;

			if (v->mon_frames % v->factor) {
				// a frame the kept one stands for: its codes
				// from 0 up too, for the DAC's gap
				if (at) {
					q = &v->qring[2 * ((at - 1) % LV_RING)];
					q[0] += b[0] >= 0;
					q[1] += b[1] >= 0;
				}
				continue;
			}
			x = &v->xring[2 * (at % LV_RING)];
			x[0] = b[0];
			x[1] = b[1];
			q = &v->qring[2 * (at % LV_RING)];
			q[0] = b[0] >= 0;
			q[1] = b[1] >= 0;
			at++;
		}
		__atomic_store_n(&v->played, at, __ATOMIC_RELEASE);
		got = 1;
		if (r < LV_CHUNK)
			break;
	}
	if (got && v->cfg.equalize)
		lv_stamp(v);
	return 0;
}

// what came back on Audio 1, until a read short of a chunk; the reader's
static int lv_pull(struct loopview *v)
{
	int16_t buf[2 * LV_CHUNK];
	uint64_t at = v->captured;

	for (;;) {
		snd_pcm_sframes_t r = snd_pcm_readi(v->cap, buf, LV_CHUNK), i;

		if (r == -EAGAIN)
			return 0;
		if (r < 0)
			return (int)r;
		for (i = 0; i < r; i++)
			memcpy(&v->yring[2 * ((at + i) % LV_RING)], &buf[2 * i],
			       4);
		at += r;
		__atomic_store_n(&v->captured, at, __ATOMIC_RELEASE);
		if (r < LV_CHUNK)
			return 0;
	}
}

// the streams' reader, a thread of its own: what each stream has, as it
// comes, until the first error, which the step takes up
static void *lv_reader(void *arg)
{
	struct loopview *v = arg;

	while (!__atomic_load_n(&v->rstop, __ATOMIC_ACQUIRE)) {
		int err = v->mon ? lv_take(v) : 0, which = LV_IN;

		if (err >= 0) {
			err = lv_pull(v);
			which = LV_CAP;
		}
		if (err < 0) {
			__atomic_store_n(&v->rwhich, which, __ATOMIC_RELAXED);
			__atomic_store_n(&v->rerr, err, __ATOMIC_RELEASE);
			break;
		}
		usleep(LV_READ_US);
	}
	return NULL;
}

static int lv_reader_start(struct loopview *v)
{
	v->rstop = 0;
	v->rerr = 0;
	if (pthread_create(&v->rthread, NULL, lv_reader, v))
		return -1;
	v->reading = 1;
	return 0;
}

static void lv_reader_stop(struct loopview *v)
{
	if (!v->reading)
		return;
	__atomic_store_n(&v->rstop, 1, __ATOMIC_RELEASE);
	pthread_join(v->rthread, NULL);
	v->reading = 0;
}

// a stream's trouble, if any, and which: the writer's or the reader's
static int lv_fault(struct loopview *v, int *which)
{
	int err = v->writing ? __atomic_load_n(&v->werr, __ATOMIC_ACQUIRE) : 0;

	*which = LV_IN;
	if (!err && v->reading) {
		err = __atomic_load_n(&v->rerr, __ATOMIC_ACQUIRE);
		*which = __atomic_load_n(&v->rwhich, __ATOMIC_RELAXED);
	}
	return err;
}

// the streams from the start, those of which (LV_IN, LV_CAP), the lag to be
// found again, and the sums cleared with clear. With the music, either may
// start alone, and the sums hold, since the loop is the same, unless the
// DAC's rate changed; the noise and Audio 1 always start together
static int lv_start(struct loopview *v, int which, int clear)
{
	unsigned int factor = v->factor;

	// neither thread left to move a count after it is cleared
	lv_reader_stop(v);
	lv_writer_stop(v);
	if (!clear)
		lv_run_end(v);
	v->bad = 0;
	v->lagged = 0;
	v->lag_at = 0;
	v->why[0] = 0;
	if (!v->cfg.monitor)
		which = LV_BOTH;
	if (which & LV_IN) {
		pthread_mutex_lock(&v->clk_lock);
		lv_clock_fold(v);
		v->cfirst = 0;
		pthread_mutex_unlock(&v->clk_lock);
		v->played = v->mon_frames = 0;
	}
	if (which & LV_CAP)
		v->captured = v->next = 0;
	if (v->cfg.monitor) {
		if ((which & LV_IN) && lv_mon_open(v))
			return -1;
		if (clear || v->factor != factor)
			lv_clear(v);
		if ((which & LV_IN) &&
		    (snd_pcm_prepare(v->mon) < 0 || snd_pcm_start(v->mon) < 0))
			goto bad;
		if (which & LV_CAP) {
			snd_pcm_drop(v->cap);
			if (snd_pcm_prepare(v->cap) < 0 ||
			    snd_pcm_start(v->cap) < 0)
				goto bad;
		}
		return lv_reader_start(v);
	}
	lv_clear(v);
	snd_pcm_drop(v->cap);
	if (snd_pcm_prepare(v->play) < 0 || snd_pcm_prepare(v->cap) < 0 ||
	    snd_pcm_start(v->cap) < 0)
		goto bad;
	// the playback starts once the writer has filled its buffer
	v->wstop = 0;
	v->werr = 0;
	if (pthread_create(&v->wthread, NULL, lv_writer, v))
		goto bad;
	v->writing = 1;
	return lv_reader_start(v);
bad:
	snprintf(v->why, sizeof(v->why), "the streams didn't start");
	return -1;
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

static int lv_design(struct loopview *v, int c);
static int lv_param(const char *name, long *v, const long *set);
static void lv_eq_block(struct loopview *v, double now);
static void lv_figures(struct loopview *v);
static unsigned int lv_bin(const struct loopview *v, double hz);

static int lv_files(struct loopview *v)
{
	unsigned int rx = 0, ry = 0, lg = LV_FILE_LAG_LOG2N, n = v->n, k;
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
	// from files, the taps are fitted and shown, and nothing is loaded
	if (v->cfg.calibrate && v->path[LL].segments) {
		int c, i;

		lv_figures(v);
		for (c = 0; c < 2; c++) {
			for (i = 0; i < LV_CAL_AT; i++)
				v->before[c][i] =
					v->mag[c][lv_bin(v, lv_cal_at[i])];
			if (lv_design(v, c))
				return 0;
		}
		v->phase = 1;
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
	v->chip_out = NAN;
	pthread_mutex_init(&v->clk_lock, NULL);
	v->clk_init = 1;
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
		v->segq[i] = malloc(n * sizeof(double));
		v->zq[i] = malloc((n + 2) * sizeof(double));
		if (!v->coh[i] || !v->mag[i] || !v->ph[i] || !v->inc[i] ||
		    !v->xt[i] || !v->ir[i] || !v->segq[i] || !v->zq[i])
			goto bad;
		v->zs_set[i] = -1;
	}
	if (zstep_init(&v->zst, v->bins))
		goto bad;
	if (cfg->ref && cfg->rec) {
		if (lv_files(v))
			goto bad;
		return v;
	}
	v->xring = calloc(2 * (size_t)LV_RING, sizeof(int16_t));
	v->yring = calloc(2 * (size_t)LV_RING, sizeof(int16_t));
	v->qring = calloc(2 * (size_t)LV_RING, sizeof(uint8_t));
	if (!v->xring || !v->yring || !v->qring)
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
	if (cfg->equalize) {
		struct timespec ts;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		v->t0 = ts.tv_sec + ts.tv_nsec * 1e-9;
		lv_param("a2_trim_ppb", &v->trim, NULL);
		v->have0 = !ctl_read_all(v->ctl,
					 "Audio 2 Treble Correction Taps",
					 v->taps0, 3 * EQ_SIDES);
		if (cfg->log && !(v->eqlog = fopen(cfg->log, "a")))
			fprintf(stderr, "es1869scope: %s: %s\n", cfg->log,
				strerror(errno));
	}
	// the devices first, so that a busy one leaves the mixer as it was
	if (lv_pcm(&v->cap, cfg->capture, SND_PCM_STREAM_CAPTURE, v->rate,
		   500000))
		goto bad;
	if (cfg->monitor ? lv_mon_open(v) :
			   (lv_pcm(&v->play, cfg->play, SND_PCM_STREAM_PLAYBACK,
				   v->rate, 500000) ||
			    snd_pcm_nonblock(v->play, 0) < 0)) {
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
	if (lv_start(v, LV_BOTH, 1)) {
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
	lv_reader_stop(v);
	if (v->play) {
		lv_writer_stop(v);
		snd_pcm_close(v->play);
	}
	if (v->mon)
		snd_pcm_close(v->mon);
	if (v->cap)
		snd_pcm_close(v->cap);
	if (v->ctl) {
		lv_restore(v);
		snd_ctl_close(v->ctl);
	}
	if (v->eqlog)
		fclose(v->eqlog);
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
		free(v->segq[i]);
		free(v->zq[i]);
	}
	zstep_free(&v->zst);
	free(v->qring);
	free(v->f);
	free(v->t);
	free(v->zir);
	free(v->xring);
	free(v->yring);
	free(v->fx);
	free(v->fy);
	if (v->clk_init)
		pthread_mutex_destroy(&v->clk_lock);
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

// the correction filter's response, from its taps, at f
static double lv_cf_gain(const long *t, double f, unsigned int rate)
{
	double a = t[0];
	int m;

	for (m = 1; m <= LV_CF_HALF; m++)
		a += 2.0 * t[m] * cos(2 * M_PI * f * m / rate);
	return a / LV_CF_ONE;
}

// taps that take back channel c's measured response, against its gain at
// 1 kHz, up to LV_CAL_TOP: the symmetric FIR of capfilt.c, fitted by least
// squares to the inverse, held at the top's value above it with little
// weight, and lifting by LV_CAL_MAX at most
static int lv_design(struct loopview *v, int c)
{
	enum { N = LV_CF_HALF + 1 };
	double a[N][N + 1], x[N], ref = cabs(v->h[c][lv_bin(v, 1000)]);
	double top = cabs(v->h[c][lv_bin(v, LV_CAL_TOP)]) / ref, hz;
	unsigned int k, i, j, r;

	if (!(ref > 0) || !(top > 0))
		return -1;
	memset(a, 0, sizeof(a));
	hz = (double)v->rate / v->n;
	for (k = 1; k < v->bins; k++) {
		double f = k * hz, m = cabs(v->h[c][k]) / ref, t, w, b[N];

		if (f < 20)
			continue;
		if (f <= LV_CAL_TOP) {
			t = m > 0 ? 1 / m : LV_CAL_MAX;
			w = 1;
		} else {
			t = 1 / top;
			w = 0.02;
		}
		t = fmin(t, LV_CAL_MAX);
		for (i = 0; i < N; i++)
			b[i] = i ? 2 * cos(2 * M_PI * f * i / v->rate) : 1;
		for (i = 0; i < N; i++) {
			for (j = 0; j < N; j++)
				a[i][j] += w * b[i] * b[j];
			a[i][N] += w * b[i] * t;
		}
	}
	// Gauss with the largest pivot of each column
	for (i = 0; i < N; i++) {
		double p;

		r = i;
		for (j = i + 1; j < N; j++)
			if (fabs(a[j][i]) > fabs(a[r][i]))
				r = j;
		if (fabs(a[r][i]) < 1e-12)
			return -1;
		for (j = 0; j <= N; j++) {
			p = a[i][j];
			a[i][j] = a[r][j];
			a[r][j] = p;
		}
		for (j = i + 1; j < N; j++) {
			double q = a[j][i] / a[i][i];

			for (r = i; r <= N; r++)
				a[j][r] -= q * a[i][r];
		}
	}
	for (i = N; i-- > 0;) {
		double s = a[i][N];

		for (j = i + 1; j < N; j++)
			s -= a[i][j] * x[j];
		x[i] = s / a[i][i];
	}
	v->peak[c] = -1e9;
	for (i = 0; i < N; i++) {
		long q = lrint(x[i] * LV_CF_ONE);

		v->taps[c][i] = q > 32767 ? 32767 : q < -32768 ? -32768 : q;
	}
	for (k = 0; k < v->bins; k++)
		v->peak[c] = fmax(v->peak[c],
				  20 * log10(fabs(lv_cf_gain(v->taps[c], k * hz,
							     v->rate)) +
					     1e-12));
	return 0;
}

// the measurement's response kept, the taps fitted and loaded, the filter on,
// and the loop started again to verify
static int lv_calibrate(struct loopview *v)
{
	long all[2 * (LV_CF_HALF + 1)], on[2] = { 1, 1 };
	int c, i;

	lv_figures(v);
	for (c = 0; c < 2; c++) {
		for (i = 0; i < LV_CAL_AT; i++)
			v->before[c][i] = v->mag[c][lv_bin(v, lv_cal_at[i])];
		if (lv_design(v, c))
			return -1;
		for (i = 0; i <= LV_CF_HALF; i++)
			all[c * (LV_CF_HALF + 1) + i] = v->taps[c][i];
	}
	if (ctl_write_all(v->ctl, "Audio 1 Correction Capture Taps", all,
			  2 * (LV_CF_HALF + 1)) ||
	    ctl_write(v->ctl, "Audio 1 Correction Capture Switch", on, 2))
		return -1;
	return lv_start(v, LV_BOTH, 1);
}

// the Clock mode's ratio that the driver applies, ppb: "Audio 2's stream
// takes +X ppb more of itself a frame" in its /proc page
static int lv_applied_ppb(double *ppb)
{
	FILE *f = fopen("/proc/asound/ES1869/es1869", "r");
	char line[160];
	long long x;
	int ok = 0;

	if (!f)
		return -1;
	while (!ok && fgets(line, sizeof(line), f))
		ok = sscanf(line,
			    "clock: Audio 2's stream takes %lld ppb more of itself a frame",
			    &x) == 1;
	fclose(f);
	if (ok)
		*ppb = (double)x;
	return ok ? 0 : -1;
}

static int lv_param(const char *name, long *v, const long *set)
{
	char path[96];
	FILE *f;
	int ok;

	snprintf(path, sizeof(path), "/sys/module/snd_es1869/parameters/%s",
		 name);
	f = fopen(path, set ? "w" : "r");
	if (!f)
		return -1;
	ok = set ? fprintf(f, "%ld\n", *set) > 0 : fscanf(f, "%ld", v) == 1;
	return fclose(f) || !ok ? -1 : 0;
}

// the chip's clock over the block on the steered clock, ppm against its
// nominal: the slope of the monitor's position, pooled over the runs between
// the music's breaks; and *tie, the position's time error about the runs'
// lines, us rms: the DMA's steps through its FIFO and the stamps' own, which
// hide the crystal's jitter. The block's sums start again
static int lv_clock_rate(struct loopview *v, double *ppm, double *tie)
{
	double n, sxx, sxy, syy, runs, nom = (double)v->rate * v->factor;

	pthread_mutex_lock(&v->clk_lock);
	lv_clock_fold(v);
	n = v->csn;
	sxx = v->csxx;
	sxy = v->csxy;
	syy = v->csyy;
	runs = v->cruns;
	v->csn = v->csxx = v->csxy = v->csyy = v->cruns = 0;
	pthread_mutex_unlock(&v->clk_lock);
	if (n < LV_EQ_STAMPS || sxx <= 0 || !v->factor)
		return -1;
	*ppm = sxy / sxx / nom * 1e6;
	*tie = sqrt(fmax(syy - sxy * sxy / sxx, 0) / (n - 2 * runs)) / nom *
	       1e6;
	return 0;
}

// a block's chip rate against its trend: one LV_EQ_JUMP off it is left out,
// as a stop of the DAC moves it, until a second agrees with it, as a real
// change would; 1 if it counts
static int lv_eq_chip(struct loopview *v, double now, double chip)
{
	double pred = eq_predict(&v->chip_s, now, NULL);

	if (v->chip_s.n >= 3 && fabs(chip - pred) > LV_EQ_JUMP) {
		if (!isfinite(v->chip_out) ||
		    fabs(chip - v->chip_out) > LV_EQ_JUMP / 2) {
			v->chip_out = chip;
			return 0;
		}
		eq_reset(&v->chip_s);
		eq_push(&v->chip_s, now - v->cfg.secs, v->chip_out);
	}
	v->chip_out = NAN;
	eq_push(&v->chip_s, now, chip);
	return 1;
}

// a band's centre, b of LV_EQ_BANDS from 250 Hz to 20 kHz, and its edges
static double lv_eq_fc(int b, double *lo, double *hi)
{
	double fc = 250 * pow(80, b / (LV_EQ_BANDS - 1.0));

	if (lo)
		*lo = fc * pow(80, -0.5 / (LV_EQ_BANDS - 1.0));
	if (hi)
		*hi = fc * pow(80, 0.5 / (LV_EQ_BANDS - 1.0));
	return fc;
}

// a run of the music into the block: each band's deviation against 1 kHz
// from the run's own sums, weighted by the band's coherence and the run's
// segments, and the band's noise: what came back that the music through the
// loop doesn't explain, each output's power times one less its multiple
// coherence. A run too short to stand on is left out. The run's sums start
// again, with the lag kept
static void lv_eq_run(struct loopview *v)
{
	unsigned long segs = v->path[LL].segments;
	int b, k;

	if (segs >= LV_EQ_RUN) {
		lv_figures(v);
		for (b = 0; b < LV_EQ_BANDS; b++) {
			double lo, hi;
			unsigned int k0, k1;

			lv_eq_fc(b, &lo, &hi);
			k0 = lv_bin(v, lo);
			k1 = lv_bin(v, hi);
			for (k = (int)k0; k <= (int)k1; k++) {
				double c = fmin(v->coh[0][k], v->coh[1][k]);

				v->eq_n[b] += (v->path[LL].syy[k] *
						       (1 - v->coh[0][k]) +
					       v->path[RR].syy[k] *
						       (1 - v->coh[1][k])) /
					      2;
				if (c < LV_EQ_COH || !isfinite(v->mag[0][k]) ||
				    !isfinite(v->mag[1][k]))
					continue;
				v->eq_w[b] += segs * c;
				v->eq_v[b] += segs * c *
					      (v->mag[0][k] + v->mag[1][k]) / 2;
			}
		}
		v->eq_segs += segs;
	}
	// the DAC's gap from this run, in LSB, into the block by its segments
	if (v->zst.segments >= LV_EQ_RUN) {
		unsigned int lo = lv_bin(v, LV_ZS_LO), hi = lv_bin(v, LV_ZS_HI);

		for (k = 0; k < 2; k++)
			v->zs_w[k] += v->zst.segments *
				      zstep_gap(&v->zst, k, lo, hi, NULL);
		v->zs_n += v->zst.segments;
	}
	zstep_reset(&v->zst);
	for (k = 0; k < PATHS; k++)
		loop_reset(&v->path[k]);
}

// the block's noise in band b, its density in dB a Hz against a full-scale
// sine: the band's power over its width
static double lv_eq_noise(const struct loopview *v, int b)
{
	double lo, hi,
		p = 16 * v->eq_n[b] / (3.0 * v->eq_segs * (double)v->n * v->n);
	unsigned int k0, k1;

	lv_eq_fc(b, &lo, &hi);
	k0 = lv_bin(v, lo);
	k1 = lv_bin(v, hi);
	p /= (k1 - k0 + 1) * (double)v->rate / v->n;
	return p > 0 ? 10 * log10(p / (32768.0 * 32768.0 / 2)) : -INFINITY;
}

// the DAC's correction refitted where its table is in use, to take back the
// drift predicted in each band: a step of LV_EQ_STEP at most towards the
// correction it began with less the drift, within LV_EQ_SPAN of that; 1 if
// loaded. *corr is its change at 20 kHz since it began, dB
static int lv_eq_refit(struct loopview *v, const double *drift, int have,
		       double *corr)
{
	double f[LV_EQ_BANDS + 3], target[LV_EQ_BANDS + 3], w[LV_EQ_BANDS + 3];
	long all[3 * EQ_SIDES], sides[EQ_SIDES], over[2], cal[2];
	double off = 0;
	const long *row0;
	unsigned int n;
	long *row;
	int b, k, nf = 0, fitted = 0;

	*corr = NAN;
	if (!v->have0 ||
	    ctl_read(v->ctl, "Audio 1 Correction Capture Switch", cal, &n) ||
	    !cal[0] || ctl_read(v->ctl, "Audio 2 Output Rate", over, &n) ||
	    over[0] > 2 ||
	    ctl_read_all(v->ctl, "Audio 2 Treble Correction Taps", all,
			 3 * EQ_SIDES))
		return 0;
	row = all + over[0] * EQ_SIDES;
	row0 = v->taps0 + over[0] * EQ_SIDES;
	for (b = 0; b < LV_EQ_BANDS; b++) {
		double fc = lv_eq_fc(b, NULL, NULL);
		double g0 = eq_sides_gain(row0, fc, v->rate);
		double g = eq_sides_gain(row, fc, v->rate), want = g, step;

		if (isfinite(drift[b]))
			want = g0 * pow(10, -drift[b] / 20);
		want = fmax(g0 * pow(10, -LV_EQ_SPAN / 20),
			    fmin(g0 * pow(10, LV_EQ_SPAN / 20), want));
		step = 20 * log10(want / g);
		off = fmax(off, fabs(step));
		f[nf] = fc;
		target[nf] =
			g *
			pow(10, fmax(-LV_EQ_STEP, fmin(LV_EQ_STEP, step)) / 20);
		w[nf++] = 1;
	}
	// the low end held where it is
	for (k = 0; k < 3; k++, nf++) {
		f[nf] = 50 * (k + 1);
		target[nf] = eq_sides_gain(row, f[nf], v->rate);
		w[nf] = 0.2;
	}
	if (have >= LV_EQ_BANDS / 2 && off >= LV_EQ_MIN &&
	    !eq_fit_sides(f, target, w, nf, v->rate, row, sides)) {
		memcpy(row, sides, sizeof(sides));
		fitted = !ctl_write_all(v->ctl,
					"Audio 2 Treble Correction Taps", all,
					3 * EQ_SIDES);
		if (!fitted)
			ctl_read_all(v->ctl, "Audio 2 Treble Correction Taps",
				     all, 3 * EQ_SIDES);
	}
	*corr = 20 * log10(eq_sides_gain(row, 20000, v->rate) /
			   eq_sides_gain(row0, 20000, v->rate));
	return fitted;
}

// a block's end: the loop's deviation in bands and the clock's residual
// into their trends, each predicted a block ahead; the DAC's correction
// refitted to the drift and loaded, and the trim moved; lines to the log
// the DAC's gap at its sign over the block, each side's in LSB, which the
// driver's a2_zstep_l and a2_zstep_r take back while a2_zstep is on: told
// from the second block on, once it moves by LV_ZS_MOVE, and kept after,
// since it is the DAC's, as alsactl keeps its controls
static void lv_eq_zstep(struct loopview *v, double now, FILE *out)
{
	static const char *const name[2] = { "a2_zstep_l", "a2_zstep_r" };
	int c, told = 0;

	if (!v->zs_n)
		return;
	fprintf(out, "%.0f zstep", now - v->t0);
	for (c = 0; c < 2; c++)
		fprintf(out, " %.2f", v->zs_w[c] / v->zs_n);
	if (++v->zs_blocks >= 2)
		for (c = 0; c < 2; c++) {
			double g = fmin(fmax(v->zs_w[c] / v->zs_n, 0), 32);
			long want = lrint(g * 256);

			if (fabs(g - v->zs_set[c]) < LV_ZS_MOVE ||
			    lv_param(name[c], NULL, &want))
				continue;
			v->zs_set[c] = want / 256.0;
			told = 1;
		}
	fprintf(out, " LSB, %.0f segments%s\n", v->zs_n, told ? " (told)" : "");
}

static void lv_eq_block(struct loopview *v, double now)
{
	double m[LV_EQ_BANDS], drift[LV_EQ_BANDS], chip, tie, applied, ahead;
	double corr = NAN, most = 0;
	unsigned int n;
	long cal[2];
	int b, at = LV_EQ_BANDS - 1, have = 0, fitted = 0, trimmed = 0;
	int based = 1;
	long trim;
	FILE *out = v->eqlog ? v->eqlog : stdout;

	lv_eq_run(v);
	ahead = now + v->cfg.secs;
	for (b = 0; b < LV_EQ_BANDS; b++) {
		m[b] = drift[b] = NAN;
		if (v->eq_w[b] > 0) {
			m[b] = v->eq_v[b] / v->eq_w[b];
			// where it starts from: the first blocks with this band
			if (v->nbase[b] < LV_EQ_BASE)
				v->base[b] +=
					(m[b] - v->base[b]) / ++v->nbase[b];
			eq_push(&v->band[b], now, m[b]);
		}
		if (v->nbase[b] < LV_EQ_BASE)
			based = 0;
		// a trend through more than the blocks it is measured from
		if (v->nbase[b] < LV_EQ_BASE ||
		    v->band[b].n < LV_EQ_BASE + LV_EQ_AFTER)
			continue;
		drift[b] = eq_predict(&v->band[b], ahead, NULL) - v->base[b];
		have++;
		if (fabs(drift[b]) > most) {
			most = fabs(drift[b]);
			at = b;
		}
	}
	// only through a record path that the calibration made flat: without
	// it, the deviation is the record path's own loss, not the DAC's
	if (!v->blocks &&
	    !ctl_read(v->ctl, "Audio 1 Correction Capture Switch", cal, &n) &&
	    !cal[0])
		fprintf(out, "0 the record path isn't calibrated (es1869scope "
			     "-C): the DAC's correction stays as it is\n");
	if (have)
		fitted = lv_eq_refit(v, drift, have, &corr);
	if (based && !v->based) {
		v->based = 1;
		fprintf(out, "%.0f base", now - v->t0);
		for (b = 0; b < LV_EQ_BANDS; b += 3)
			fprintf(out, " %.0f:%+.2f", lv_eq_fc(b, NULL, NULL),
				v->base[b]);
		fprintf(out, " %.0f:%+.2f\n", 20000.0,
			v->base[LV_EQ_BANDS - 1]);
	}
	// the clock: the chip's rate on the steered clock, with what the driver
	// applies on top, is what the stream does there, the residual. Less the
	// trim, it is the driver's own error, which the trim answers: its trend,
	// a block ahead, taken back whole, LV_EQ_TRIM a block at most. A trend of
	// the residual itself would follow the trim's own steps and overshoot
	if (lv_clock_rate(v, &chip, &tie) || lv_applied_ppb(&applied) ||
	    lv_param("a2_trim_ppb", &trim, NULL)) {
		// no clock this block
	} else if (!lv_eq_chip(v, now, chip)) {
		fprintf(out,
			"%.0f clock chip %+.4f tie %.1f off its trend, left out\n",
			now - v->t0, chip, tie);
	} else {
		double resid = chip + applied / 1e3, bias = resid - trim / 1e3;
		double pred;
		long want;

		eq_push(&v->bias, now, bias);
		pred = eq_predict(&v->bias, ahead, NULL);
		want = -lrint(pred * 1e3);
		want = trim + (want > trim + LV_EQ_TRIM ? LV_EQ_TRIM :
			       want < trim - LV_EQ_TRIM ? -LV_EQ_TRIM :
							  want - trim);
		if (v->bias.n >= 3 && labs(want - trim) >= LV_EQ_TRIM_MIN &&
		    !lv_param("a2_trim_ppb", NULL, &want)) {
			trim = want;
			trimmed = 1;
		}
		v->trim = trim;
		fprintf(out,
			"%.0f clock chip %+.4f tie %.1f applied %+.4f resid %+.4f bias %+.4f pred %+.4f trim %ld%s\n",
			now - v->t0, chip, tie, applied / 1e3, resid, bias,
			pred, trim, trimmed ? " (moved)" : "");
	}
	// how what went in and what came back overlaid: |r| at the lag, and
	// the peak's place between frames, its mean and spread
	if (v->ov_n > 0) {
		double d = v->ov_d / v->ov_n;

		fprintf(out,
			"%.0f overlay r %.5f delay %+.3f sd %.3f missed %lu\n",
			now - v->t0, v->ov_r / v->ov_n, d,
			sqrt(fmax(v->ov_dd / v->ov_n - d * d, 0)), v->ov_bad);
	}
	v->ov_n = v->ov_r = v->ov_d = v->ov_dd = 0;
	v->ov_bad = 0;
	fprintf(out, "%.0f eq", now - v->t0);
	for (b = 0; b < LV_EQ_BANDS; b += 3)
		fprintf(out, " %.0f:%+.2f", lv_eq_fc(b, NULL, NULL), m[b]);
	fprintf(out, " %.0f:%+.2f", 20000.0, m[LV_EQ_BANDS - 1]);
	if (have)
		fprintf(out, " drift %.0f:%+.2f", lv_eq_fc(at, NULL, NULL),
			drift[at]);
	if (isfinite(corr))
		fprintf(out, " corr %+.2f", corr);
	fprintf(out, "%s\n", fitted ? " (refitted)" : "");
	// the noise in each band, dB a Hz against a full-scale sine: Hann's
	// window keeps 3/8 of a segment's power, and the band's bins sum one
	// side
	if (v->eq_segs) {
		fprintf(out, "%.0f noise", now - v->t0);
		for (b = 0; b < LV_EQ_BANDS; b += 3)
			fprintf(out, " %.0f:%.1f", lv_eq_fc(b, NULL, NULL),
				lv_eq_noise(v, b));
		fprintf(out, " %.0f:%.1f\n", 20000.0,
			lv_eq_noise(v, LV_EQ_BANDS - 1));
	}
	lv_eq_zstep(v, now, out);
	fflush(out);
	v->blocks++;
	// the next block from here, with the lag kept
	for (b = 0; b < LV_EQ_BANDS; b++)
		v->eq_w[b] = v->eq_v[b] = v->eq_n[b] = 0;
	v->eq_segs = 0;
	v->zs_w[0] = v->zs_w[1] = v->zs_n = 0;
}

int loopview_idle(const struct loopview *v)
{
	return v->idle;
}

int loopview_done(const struct loopview *v)
{
	if (v->cfg.equalize)
		return 0;
	if (v->cfg.calibrate)
		return v->phase == 2;
	return loopview_seconds(v) >= v->cfg.secs;
}

double loopview_seconds(const struct loopview *v)
{
	return (v->eq_segs + v->path[LL].segments) * (v->n / 2) /
	       (double)v->rate;
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

	static const char *const phases[3] = { "calibrating, measuring",
					       "calibrating, verifying",
					       "calibrated" };
	const char *what = v->cfg.calibrate ? phases[v->phase] :
			   v->mon	    ? "the music into Audio 1" :
			   v->play	    ? "Audio 2 into Audio 1" :
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
		      v->why[0]		      ? v->why :
		      v->mon && !lv_played(v) ? "waiting for Audio 2 to play" :
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

// a drawing where there is a screen
static void lv_redraw(struct loopview *v, struct canvas *c)
{
	if (!v->cfg.headless)
		lv_draw(v, c);
}

// nothing played for LV_IDLE_S seconds: the run ends, Audio 1 and the reader
// stop, and the monitor waits for Audio 2's next start
static void lv_idle(struct loopview *v)
{
	lv_reader_stop(v);
	lv_run_end(v);
	pthread_mutex_lock(&v->clk_lock);
	lv_clock_fold(v);
	v->cfirst = 0;
	pthread_mutex_unlock(&v->clk_lock);
	snd_pcm_drop(v->cap);
	snd_pcm_drop(v->mon);
	if (snd_pcm_prepare(v->mon) >= 0)
		snd_pcm_start(v->mon);
	v->played = v->mon_frames = v->captured = v->next = 0;
	v->lagged = 0;
	v->lag_at = 0;
	v->bad = 0;
	v->idle = 1;
	snprintf(v->why, sizeof(v->why), "nothing plays: Audio 1 rests");
	fprintf(stderr,
		"es1869scope: nothing played for %d s, Audio 1 rests "
		"until the music comes\n",
		LV_IDLE_S);
}

// whether the view rests, as it starts to or goes on: the monitor's first
// frames, or a stop of it, as a start of Audio 2 at another rate brings,
// start both streams again
static int lv_rest(struct loopview *v, long long now)
{
	uint64_t p;

	if (v->idle) {
		snd_pcm_sframes_t a = snd_pcm_avail_update(v->mon);

		if (!a)
			return 1;
		// a stop of the monitor while nothing plays is started again;
		// one that won't prepare means Audio 2 runs at another rate
		if (a < 0 && snd_pcm_prepare(v->mon) >= 0 &&
		    snd_pcm_start(v->mon) >= 0)
			return 1;
		v->idle = 0;
		v->seen_at = 0;
		fprintf(stderr, "es1869scope: the music came, Audio 1 again\n");
		if (lv_start(v, LV_BOTH, 0)) {
			v->again = LV_BOTH;
			v->retry = lv_now() + 1000000000LL;
		}
		return 1;
	}
	p = __atomic_load_n(&v->played, __ATOMIC_ACQUIRE);
	if (!v->seen_at || p != v->seen) {
		v->seen = p;
		v->seen_at = now;
	} else if (now - v->seen_at > LV_IDLE_S * 1000000000LL) {
		lv_idle(v);
		return 1;
	}
	return 0;
}

int loopview_step(struct loopview *v, struct canvas *c)
{
	unsigned long before = v->path[LL].segments;
	int lagged = v->lagged, which, err, r = 0;
	long long t0 = lv_now(), now;

	// the music stopped or changed its rate: try again each second
	if (v->retry) {
		if (t0 < v->retry)
			return 0;
		v->retry = lv_start(v, v->again, 0) ? lv_now() + 1000000000LL :
						      0;
		lv_redraw(v, c);
		return 1;
	}
	err = lv_fault(v, &which);
	if (err < 0) {
		fprintf(stderr,
			"es1869scope: %s on %s after %llu frames, started "
			"again\n",
			snd_strerror(err),
			which == LV_CAP ? "Audio 1's recording" :
			v->mon		? "the monitor" :
					  "the playback",
			(unsigned long long)lv_captured(v));
		v->restarts++;
		if (v->cfg.monitor) {
			// a pause or a stop of the music, as often as it
			// comes: that stream again, the other running on
			if (lv_start(v, which, 0)) {
				v->again = LV_BOTH;
				v->retry = lv_now() + 1000000000LL;
			}
		} else if (v->restarts > LV_RESTARTS ||
			   lv_start(v, LV_BOTH, 1)) {
			return -1;
		}
		if (!v->retry)
			snprintf(v->why, sizeof(v->why), "%s, started again",
				 snd_strerror(err));
		lv_redraw(v, c);
		return 1;
	}
	if (v->mon && lv_rest(v, t0)) {
		lv_redraw(v, c);
		return 1;
	}
	if (v->play || v->mon)
		lv_live_segments(v);
	if (v->cfg.equalize && v->lagged &&
	    loopview_seconds(v) >= v->cfg.secs) {
		struct timespec ts;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		lv_eq_block(v, ts.tv_sec + ts.tv_nsec * 1e-9);
		// without a screen, the picture every fifth block
		if (!v->cfg.headless || !(v->blocks % 5)) {
			lv_draw(v, c);
			r = 2;
		}
	} else if (v->cfg.calibrate && v->phase < 2 && v->lagged &&
		   loopview_seconds(v) >= v->cfg.secs) {
		if (v->phase == 0 && lv_calibrate(v)) {
			snprintf(v->why, sizeof(v->why),
				 "the taps didn't fit or load");
			v->phase = 2;
		} else {
			v->phase++;
		}
		lv_redraw(v, c);
		r = 1;
	} else if (!v->cfg.headless && (v->lagged != lagged ||
					(v->path[LL].segments != before &&
					 lv_now() - v->drawn > LV_REDRAW_NS))) {
		lv_draw(v, c);
		r = 1;
	}
	// without a screen, a line on stderr every five seconds
	now = lv_now();
	if (now - t0 > v->slowest)
		v->slowest = now - t0;
	if (v->cfg.headless && now - v->drawn > 5000000000LL) {
		fprintf(stderr,
			"es1869scope: in %llu, back %llu, lag %s %ld, %lu "
			"segments, slowest step %lld ms%s%s\n",
			(unsigned long long)lv_played(v),
			(unsigned long long)lv_captured(v),
			v->lagged ? "found" : "not found", v->lag,
			v->path[LL].segments, v->slowest / 1000000,
			v->why[0] ? ", " : "", v->why);
		v->drawn = now;
		v->slowest = 0;
	}
	return r;
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
	if (lv_start(v, LV_BOTH, 1))
		snprintf(v->why, sizeof(v->why), "the streams didn't start");
}

void loopview_report(struct loopview *v, FILE *f)
{
	static const double at[] = { 20,    50,	   100,	  1000,	 5000, 10000,
				     15000, 18000, 20000, 21000, 22000 };
	unsigned int i;
	int c;

	if (!v->lagged || !v->path[LL].segments) {
		fprintf(f,
			"none %s (restarts %u, played %llu, recorded %llu)\n",
			v->why, v->restarts, (unsigned long long)lv_played(v),
			(unsigned long long)lv_captured(v));
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
	if (!v->cfg.calibrate || v->phase < 1)
		return;
	for (c = 0; c < 2; c++) {
		fprintf(f, "taps %c", "LR"[c]);
		for (i = 0; i <= LV_CF_HALF; i++)
			fprintf(f, " %ld", v->taps[c][i]);
		fprintf(f, "\npeak %c %.2f\n", "LR"[c], v->peak[c]);
		fprintf(f, "before %c", "LR"[c]);
		for (i = 0; i < LV_CAL_AT; i++)
			fprintf(f, " %g:%.2f", lv_cal_at[i], v->before[c][i]);
		fprintf(f, "\nafter %c", "LR"[c]);
		for (i = 0; i < LV_CAL_AT; i++)
			fprintf(f, " %g:%.2f", lv_cal_at[i],
				v->mag[c][lv_bin(v, lv_cal_at[i])]);
		fprintf(f, "\n");
	}
}
