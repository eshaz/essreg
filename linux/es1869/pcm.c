// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The ES1869's PCM devices, and Audio 1: its playback, its recording, and
 * the music DAC recorded digitally.
 *
 * Notes:
 *
 * Device 0 is snd-es18xx's: playback subdevice 0 is Audio 2 (mirror.c),
 * subdevice 1 Audio 1, and capture subdevice 0 Audio 1. Device 1 records
 * the music DAC on Audio 1 (mixer 7Fh bit 4): 16-bit stereo at 49716 Hz.
 * Audio 1 does one of the three at a time, and the others get -EBUSY.
 * Device 2 is both DACs at once (dual.c), and device 3 what Audio 2's DAC
 * took (monitor.c).
 *
 * Every rate of the chip's clocks is offered, from 3101 Hz (396.9 kHz /
 * 128) up to the limit the settings give (rate.max in es1869test measures
 * the chip's).
 *
 * A playback start keeps the 8237 channel masked over B8h bit 0 and
 * unmasks it from a timer at least 32 us and two sample periods later,
 * because the chip loses the first byte when the DMA brings it right after
 * the start's first request (docs/ESS_EXTENDED.md, fix.late0 to
 * fix.late2000).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <asm/dma.h>
#include <sound/pcm_params.h>

#include "es1869.h"

#define ES_FORMATS (SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 | \
		    SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE)

static const struct snd_ratnum a1_clocks[] = {
	{ .num = ES_CLK_HI, .den_min = 1, .den_max = 128, .den_step = 1 },
	{ .num = ES_CLK_768, .den_min = 1, .den_max = 128, .den_step = 1 },
	{ .num = ES_CLK_LO, .den_min = 1, .den_max = 128, .den_step = 1 },
};

const struct snd_pcm_hw_constraint_ratnums es1869_a1_rates = {
	.nrats = ARRAY_SIZE(a1_clocks),
	.rats = a1_clocks,
};

static const struct snd_pcm_hardware a1_play_hw = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_PAUSE,
	.formats = ES_FORMATS,
	.rates = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_KNOT,
	.rate_min = 3100,
	.rate_max = ES_CLK_HI,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = ES1869_A1_BUF,
	.period_bytes_min = 64,
	.period_bytes_max = ES1869_A1_BUF,
	.periods_min = 2,
	.periods_max = 1024,
};

static const struct snd_pcm_hardware fm_capture_hw = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_KNOT,
	.rate_min = ES_FM_RATE,
	.rate_max = ES_FM_RATE,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = ES1869_A1_BUF,
	.period_bytes_min = 64,
	.period_bytes_max = ES1869_A1_BUF,
	.periods_min = 2,
	.periods_max = 1024,
};

// --- the rate limits ---------------------------------------------------------

struct es1869_limit {
	int rate_id; // the setting with the converter's highest rate
	int bps_id; // and with the DMA's highest byte rate
};

static const struct es1869_limit limit_a1p = { ES_S_a1p_max_rate,
					       ES_S_a1_max_bps };
static const struct es1869_limit limit_a1c = { ES_S_a1c_max_rate,
					       ES_S_a1_max_bps };
const struct es1869_limit es1869_limit_a2 = { ES_S_a2_max_rate,
					      ES_S_a2_max_bps };

// the rate, at most the converter's limit and what the DMA moves at the
// smallest frame left
static int rule_rate(struct snd_pcm_hw_params *params,
		     struct snd_pcm_hw_rule *rule)
{
	const struct es1869_limit *lim = rule->private;
	struct snd_interval *rate = hw_param_interval(params,
						      SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *fb = hw_param_interval(params,
						    SNDRV_PCM_HW_PARAM_FRAME_BITS);
	struct snd_interval t;
	unsigned int max;

	if (!es1869_val(ES_S_rate_limit))
		return 0;
	max = es1869_val(lim->rate_id);
	if (fb->min)
		max = min(max, (unsigned int)div_u64((u64)es1869_val(lim->bps_id) *
						     8, fb->min));
	snd_interval_any(&t);
	t.max = max;
	return snd_interval_refine(rate, &t);
}

// and the frame, at most what the DMA moves at the lowest rate left
static int rule_frame(struct snd_pcm_hw_params *params,
		      struct snd_pcm_hw_rule *rule)
{
	const struct es1869_limit *lim = rule->private;
	struct snd_interval *rate = hw_param_interval(params,
						      SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *fb = hw_param_interval(params,
						    SNDRV_PCM_HW_PARAM_FRAME_BITS);
	struct snd_interval t;

	if (!es1869_val(ES_S_rate_limit) || !rate->min)
		return 0;
	snd_interval_any(&t);
	t.max = div_u64((u64)es1869_val(lim->bps_id) * 8, rate->min);
	return snd_interval_refine(fb, &t);
}

// a whole number of periods in the buffer, two at least (periods_min):
// without them the interrupts moved against the buffer at each wrap, and
// speaker-test, which wants two periods, took one of 12000 frames, more
// than half of the most Audio 2's buffer holds, 16384 frames, and stopped
int es1869_whole_periods(struct snd_pcm_runtime *runtime)
{
	return snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
}

int es1869_pcm_limits(struct snd_pcm_runtime *runtime,
		      const struct es1869_limit *lim)
{
	int err;

	err = es1869_whole_periods(runtime);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  rule_rate, (void *)lim,
				  SNDRV_PCM_HW_PARAM_FRAME_BITS,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;
	return snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FRAME_BITS,
				   rule_frame, (void *)lim,
				   SNDRV_PCM_HW_PARAM_RATE,
				   SNDRV_PCM_HW_PARAM_FRAME_BITS, -1);
}

// the clock and divisor of the stream's rate: the ratnum's, or the nearest
void es1869_stream_rate(struct snd_pcm_runtime *runtime, bool a1, u32 *num,
			u32 *den)
{
	if (runtime->rate_den && runtime->rate_den <= 128 &&
	    (runtime->rate_num == ES_CLK_HI || runtime->rate_num == ES_CLK_768 ||
	     (a1 && runtime->rate_num == ES_CLK_LO))) {
		*num = runtime->rate_num;
		*den = runtime->rate_den;
		return;
	}
	es1869_best_rate(runtime->rate, a1, num, den);
}

// the DMA runs up to a FIFO ahead of the DAC, the rest of the stream's
// delay; a drain counts it in the position instead, so that it ends once
// the DAC has played the last frame, where the stop would drop what the
// FIFO holds. A step back like that is a reading es1869_forward holds, so
// that FIFO is its window with the 32 transfers two readings may differ by
snd_pcm_uframes_t es1869_play_pointer(struct snd_pcm_runtime *rt,
				      unsigned long off, unsigned int fifo,
				      bool draining, unsigned long *last)
{
	snd_pcm_uframes_t size = rt->buffer_size;
	snd_pcm_uframes_t back = min_t(snd_pcm_uframes_t,
				       bytes_to_frames(rt, fifo), size / 4);

	rt->delay = draining ? 0 : bytes_to_frames(rt, fifo);
	if (draining) {
		off = (off + size - back) % size;
		// short of the period's end at an interrupt, which the core
		// takes for an interrupt acknowledged twice, and adds a buffer,
		// if its last reading looks old, as right after a pause's
		// release: this reading is what it compares with
		rt->hw_ptr_jiffies = jiffies;
	}
	return es1869_forward(last, off, size,
			      bytes_to_frames(rt, fifo + 32));
}

// a stream's fill plan: the guard in bytes, the lead from a2_lead_ms, and
// slack, the frames the DMA may start behind the pointer
void es1869_fill_init(struct es1869_fill *f, struct snd_pcm_runtime *rt,
		      unsigned int guard, unsigned long slack)
{
	unsigned long g = max_t(unsigned long, bytes_to_frames(rt, guard), 1);
	unsigned long lead = div_u64((u64)rt->rate *
				     es1869_val(ES_S_a2_lead_ms), 1000);

	*f = (struct es1869_fill){
		.buffer = rt->buffer_size,
		.boundary = rt->boundary,
		.guard = g,
		.lead = min(lead, rt->buffer_size - g),
		.slack = slack,
	};
}

// the timer's period while the pointer stands: a quarter of the lead, and
// at most half of what one tick copies, so that the frames ahead of the
// DMA are copied again before it gets there, whatever the rate
u32 es1869_standing_us(struct snd_pcm_runtime *rt, u32 budget)
{
	u32 half = div_u64((u64)bytes_to_frames(rt, budget) * 500000, rt->rate);
	u32 lead = es1869_val(ES_S_a2_lead_ms) * 1000 / 4;

	return max_t(u32, es1869_val(ES_S_a2_tick_us), min(lead, half));
}

// a drain's silence past the last frame, in the application's buffer and
// the ring that the DMA reads: as far as the DMA has freed the buffer (hw,
// the absolute frame it has reached), up to a period and two of the
// engine's FIFOs, the most it plays before the interrupt that ends the
// drain. A drain that begins with the buffer nearly full gets the rest as
// the DMA moves on, at each pointer reading; *to is how far it has gone
void es1869_drain_silence(struct snd_pcm_substream *ss, snd_pcm_uframes_t hw,
			  unsigned int fifo, snd_pcm_uframes_t *to,
			  es1869_silence_fn fill)
{
	struct snd_pcm_runtime *rt = ss->runtime;
	snd_pcm_uframes_t b = rt->buffer_size, bound = rt->boundary;
	snd_pcm_uframes_t appl = rt->control->appl_ptr;
	snd_pcm_uframes_t queued = (appl + bound - hw) % bound;
	snd_pcm_uframes_t want = rt->period_size + 2 * bytes_to_frames(rt, fifo);
	snd_pcm_uframes_t done = (*to + bound - appl) % bound, a, n, part;

	// the DMA past the last frame, which it reaches before the drain ends:
	// the whole buffer has played
	if (queued > b)
		queued = 0;
	want = min(want, b - queued);
	if (done >= want)
		return;
	a = *to % b;
	for (n = want - done; n; n -= part) {
		part = min(n, b - a);
		fill(ss, 0, frames_to_bytes(rt, a), frames_to_bytes(rt, part));
		a = (a + part) % b;
	}
	*to = (appl + want) % bound;
}

// --- Audio 1 -------------------------------------------------------------------

// a DSP that stayed busy, or that a timeout left out of step, takes a reset,
// with Audio 1 stopped: Audio 2 and the mixer go on through it (rst.ext in
// docs/ESS_EXTENDED.md), and the settings on its registers go back
int es1869_dsp_recover(struct es1869 *chip)
{
	unsigned long flags;
	int err;

	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->hw.desync = false;
	chip->adc_on = false;
	chip->a1_dac = false;
	err = es1869_reset(&chip->hw);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (err) {
		dev_err(chip->dev, "the DSP doesn't answer (%d)\n", err);
		return err;
	}
	dev_warn(chip->dev, "the DSP was out of step: reset\n");
	chip->recovering = true;
	es1869_settings_apply_ctrl(chip);
	chip->recovering = false;
	return 0;
}

static bool a1_capture(struct es1869 *chip)
{
	return chip->a1_owner == ES_OWN_A1C || chip->a1_owner == ES_OWN_FMC;
}

bool es1869_a1_busy(struct es1869 *chip)
{
	return chip->a1_owner != ES_OWN_NONE;
}

static enum hrtimer_restart a1_settle_fn(struct hrtimer *t)
{
	struct es1869 *chip = container_of(t, struct es1869, a1_settle);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->a1_state == ES_A1_SETTLING_REC) {
		// the ADC's input settled: the recording's start
		if (es1869_val(ES_S_irq_trace))
			dev_info(chip->dev, "Audio 1's input settled\n");
		chip->io.dma_mask(chip, chip->dma1, false);
		if (es1869_a1_go(&chip->hw, true))
			dev_err(chip->dev, "the DSP refused a recording's start\n");
		chip->a1_state = ES_A1_RUNNING;
	} else if (chip->a1_state == ES_A1_SETTLING) {
		if (es1869_val(ES_S_irq_trace))
			dev_info(chip->dev, "Audio 1 settled\n");
		chip->io.dma_mask(chip, chip->dma1, false);
		// dual playback: Audio 2's start right after, so that the two
		// DACs begin together
		if (chip->a1_owner == ES_OWN_DUAL)
			es1869_dual_settled(chip);
		chip->a1_state = ES_A1_RUNNING;
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return HRTIMER_NORESTART;
}

static u64 a1_settle_ns(struct snd_pcm_runtime *runtime)
{
	u64 ns = (u64)es1869_val(ES_S_a1_settle_us) * NSEC_PER_USEC;
	u64 frames = div_u64((u64)es1869_val(ES_S_a1_settle_frames) *
			     NSEC_PER_SEC, runtime->rate ? runtime->rate : 1);

	return max(ns, frames);
}

static void a1_release(struct es1869 *chip)
{
	mutex_lock(&chip->state_mutex);
	chip->a1_owner = ES_OWN_NONE;
	chip->a1_sub = NULL;
	mutex_unlock(&chip->state_mutex);
}

static int a1_open(struct snd_pcm_substream *substream, enum es1869_owner who)
{
	struct es1869 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int tied;
	int err = 0;

	if (who == ES_OWN_FMC && !es1869_val(ES_S_fmdig_device))
		return -EPERM;
	mutex_lock(&chip->state_mutex);
	if (chip->dead)
		err = -ENODEV;
	else if (chip->a1_owner != ES_OWN_NONE)
		err = -EBUSY;
	else {
		chip->a1_owner = who;
		chip->a1_sub = substream;
		chip->a1_state = ES_A1_IDLE;
		err = es1869_power_up(chip);
		if (err)
			chip->a1_owner = ES_OWN_NONE;
	}
	tied = es1869_a2_tied_rate(chip);
	mutex_unlock(&chip->state_mutex);
	if (err)
		return err;

	if (who == ES_OWN_FMC) {
		runtime->hw = fm_capture_hw;
		err = es1869_whole_periods(runtime);
		goto out;
	}
	runtime->hw = a1_play_hw;
	if (who == ES_OWN_A1C)
		runtime->hw.info &= ~SNDRV_PCM_INFO_PAUSE;
	else
		runtime->hw.info |= SNDRV_PCM_INFO_DRAIN_TRIGGER;
	err = snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					    &es1869_a1_rates);
	// a slaved Audio 2 plays at A1h's rate, which is Audio 1's too
	if (err >= 0 && tied)
		err = snd_pcm_hw_constraint_single(runtime,
						   SNDRV_PCM_HW_PARAM_RATE, tied);
	if (err >= 0)
		err = es1869_pcm_limits(runtime, who == ES_OWN_A1C ? &limit_a1c :
								     &limit_a1p);
out:
	// the core calls no close for an open that failed
	if (err < 0)
		a1_release(chip);
	return err < 0 ? err : 0;
}

static int a1_close(struct snd_pcm_substream *substream)
{
	struct es1869 *chip = snd_pcm_substream_chip(substream);

	chip->clk_a1 = 0;
	a1_release(chip);
	es1869_power_idle(chip);
	return 0;
}

static int a1_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	struct es1869 *chip = snd_pcm_substream_chip(substream);

	// one buffer for Audio 1's three uses, which never run at once
	snd_pcm_set_runtime_buffer(substream, &chip->a1_buf);
	substream->runtime->dma_bytes = params_buffer_bytes(params);
	return 0;
}

static int a1_hw_free(struct snd_pcm_substream *substream)
{
	struct es1869 *chip = snd_pcm_substream_chip(substream);
	unsigned long flags;
	bool dac;

	spin_lock_irqsave(&chip->reg_lock, flags);
	// the codec back on the DAC
	es1869_a1_stop(&chip->hw, a1_capture(chip), chip->dma1, false);
	chip->adc_on = false;
	if (chip->a1_owner == ES_OWN_FMC)
		es1869_mix_update(&chip->hw, 0x7F, 0x11,
				  es1869_val(ES_S_video_switch) ? 0x01 : 0);
	dac = chip->a1_dac && chip->a1_owner == ES_OWN_A1P;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	// the FIFO played out, then Audio 1 off the mixer (DS p.49)
	if (dac) {
		msleep(25);
		spin_lock_irqsave(&chip->reg_lock, flags);
		es1869_a1_dac(&chip->hw, false);
		chip->a1_dac = false;
		spin_unlock_irqrestore(&chip->reg_lock, flags);
	}
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

static int a1_prepare(struct snd_pcm_substream *substream)
{
	struct es1869 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	bool capture = a1_capture(chip);
	struct es1869_stream s = {
		.bits = snd_pcm_format_width(runtime->format),
		.is_signed = snd_pcm_format_signed(runtime->format) > 0,
		.stereo = runtime->channels == 2,
		.period = frames_to_bytes(runtime, runtime->period_size),
		.ch = chip->dma1,
		.dma_phys = runtime->dma_addr,
		.dma_bytes = snd_pcm_lib_buffer_bytes(substream),
		.single = es1869_val(ES_S_dma_single),
		.burst = es1869_val(ES_S_a1_burst),
		.corner = es1869_val(ES_S_filter_corner),
		.a8 = es1869_val(capture ? ES_S_a8_rec_bits : ES_S_a8_play_bits),
	};
	unsigned int tied;
	unsigned long flags;
	int err, tries;

	mutex_lock(&chip->state_mutex);
	tied = es1869_a2_tied_rate(chip);
	mutex_unlock(&chip->state_mutex);
	if (tied && tied != runtime->rate)
		return -EBUSY;
	if (es1869_val(ES_S_irq_trace))
		dev_info(chip->dev, "%s: DMA %d at %#x, %u bytes, period %u\n",
			 es1869_owner_names[chip->a1_owner], s.ch, s.dma_phys,
			 s.dma_bytes, s.period);
	if (chip->a1_owner == ES_OWN_FMC) {
		// the chip records the music DAC at its own 49716 Hz, with A1h
		// as ESS's driver sets it, for 48 kHz in the new mode
		s.rate_num = ES_CLK_768;
		s.rate_den = 16;
	} else {
		es1869_stream_rate(runtime, true, &s.rate_num, &s.rate_den);
	}
	chip->clk_a1 = chip->a1_owner == ES_OWN_FMC ||
		       es1869_val(ES_S_a2_resample) != 2 ? 0 :
		es1869_clock_period(s.rate_num, s.rate_den,
				    runtime->period_size);
	// a recording turns the codec to the ADC now, unless it is there
	s.adc = chip->a1_owner == ES_OWN_A1C && es1869_val(ES_S_a1c_settle_ms);
	// a read waits out the settle on top of the core's own timeout, the
	// buffer's time and a tenth or 100 ms
	substream->wait_time = s.adc ? es1869_val(ES_S_a1c_settle_ms) +
				max_t(long, 100, div_u64((u64)runtime->buffer_size *
							 1100, runtime->rate)) :
				0;
	for (tries = 0; tries < 2; tries++) {
		spin_lock_irqsave(&chip->reg_lock, flags);
		chip->a1_state = ES_A1_IDLE;
		chip->a1_draining = false;
		chip->a1_last = 0;
		err = es1869_a1_prepare(&chip->hw, &s, capture,
					es1869_val(ES_S_record_monitor));
		if (!err && s.adc && !chip->adc_on)
			chip->adc_since = ktime_get();
		chip->adc_on = !err && s.adc;
		if (!err && chip->a1_owner == ES_OWN_FMC)
			es1869_mix_update(&chip->hw, 0x7F, 0x11, 0x10);
		if (!err && !capture && !chip->a1_dac) {
			err = es1869_a1_dac(&chip->hw, true);
			chip->a1_dac = !err;
		}
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		if ((err != -EBUSY && err != -EIO) || tries ||
		    es1869_dsp_recover(chip))
			break;
	}
	if (es1869_val(ES_S_irq_trace))
		dev_info(chip->dev, "%s prepared: %d\n",
			 es1869_owner_names[chip->a1_owner], err);
	return err;
}

void es1869_a1_start_settle(struct es1869 *chip,
			    struct snd_pcm_runtime *runtime)
{
	u64 ns = a1_settle_ns(runtime);

	if (es1869_val(ES_S_a1_settle_mode)) {
		ndelay(ns);
		chip->io.dma_mask(chip, chip->dma1, false);
		if (chip->a1_owner == ES_OWN_DUAL)
			es1869_dual_settled(chip);
		chip->a1_state = ES_A1_RUNNING;
		return;
	}
	chip->a1_state = ES_A1_SETTLING;
	hrtimer_start(&chip->a1_settle, ns_to_ktime(ns),
		      HRTIMER_MODE_REL_HARD);
}

// what is left of the ADC input's settle before a recording's start (ns):
// the input's offset after the codec turns to the ADC decays through the
// first 100 ms, a halving about every 25 ms, from near full scale
static u64 a1c_settle_left(struct es1869 *chip)
{
	s64 left;

	if (chip->a1_owner != ES_OWN_A1C || !chip->adc_on)
		return 0;
	left = (s64)es1869_val(ES_S_a1c_settle_ms) * NSEC_PER_MSEC -
	       ktime_to_ns(ktime_sub(ktime_get(), chip->adc_since));
	return left > 0 ? left : 0;
}

// the silence in the buffer that the DMA reads, for a drain and the core
static int a1_fill_silence(struct snd_pcm_substream *substream, int channel,
			   unsigned long pos, unsigned long bytes)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_format_set_silence(runtime->format, runtime->dma_area + pos,
				   bytes_to_samples(runtime, bytes));
	return 0;
}

static int a1_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct es1869 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	bool capture = a1_capture(chip);
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (es1869_val(ES_S_irq_trace))
		dev_info(chip->dev, "Audio 1 trigger %d\n", cmd);
	chip->clk_src = 0;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (capture && a1c_settle_left(chip)) {
			chip->a1_state = ES_A1_SETTLING_REC;
			hrtimer_start(&chip->a1_settle,
				      ns_to_ktime(a1c_settle_left(chip)),
				      HRTIMER_MODE_REL_HARD);
			break;
		}
		if (capture) {
			chip->io.dma_mask(chip, chip->dma1, false);
			err = es1869_a1_go(&chip->hw, true);
			chip->a1_state = ES_A1_RUNNING;
			break;
		}
		err = es1869_a1_go(&chip->hw, false);
		if (!err)
			es1869_a1_start_settle(chip, runtime);
		break;
	case SNDRV_PCM_TRIGGER_DRAIN:
		chip->a1_silent_to = runtime->control->appl_ptr;
		chip->a1_draining = true;
		es1869_drain_silence(substream, runtime->status->hw_ptr,
				     ES_FIFO_A1, &chip->a1_silent_to,
				     a1_fill_silence);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		hrtimer_try_to_cancel(&chip->a1_settle);
		err = es1869_a1_stop(&chip->hw, capture, chip->dma1,
				     chip->adc_on);
		chip->a1_state = ES_A1_IDLE;
		chip->a1_draining = false;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (chip->a1_state == ES_A1_SETTLING)
			hrtimer_try_to_cancel(&chip->a1_settle);
		err = es1869_a1_pause(&chip->hw, true);
		// a start that never unmasked settles again at the release
		chip->a1_state = chip->a1_state == ES_A1_SETTLING ?
					 ES_A1_PAUSED_MASKED : ES_A1_PAUSED;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		err = es1869_a1_pause(&chip->hw, false);
		if (chip->a1_state == ES_A1_PAUSED_MASKED)
			es1869_a1_start_settle(chip, runtime);
		else
			chip->a1_state = ES_A1_RUNNING;
		break;
	default:
		err = -EINVAL;
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return err;
}

static snd_pcm_uframes_t a1_pointer(struct snd_pcm_substream *substream)
{
	struct es1869 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long flags, off;
	u32 pos;

	spin_lock_irqsave(&chip->reg_lock, flags);
	pos = es1869_dma_pos(&chip->hw, chip->dma1,
			     snd_pcm_lib_buffer_bytes(substream));
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (es1869_val(ES_S_irq_trace) >= 2)
		dev_info_ratelimited(chip->dev, "Audio 1 at %u\n", pos);
	off = bytes_to_frames(runtime, pos);
	if (a1_capture(chip))
		return es1869_forward(&chip->a1_last, off, runtime->buffer_size,
				      bytes_to_frames(runtime, 32));
	if (chip->a1_draining)
		es1869_drain_silence(substream, es1869_abs_frame(runtime, off),
				     ES_FIFO_A1, &chip->a1_silent_to,
				     a1_fill_silence);
	return es1869_play_pointer(runtime, off, ES_FIFO_A1, chip->a1_draining,
				   &chip->a1_last);
}

static int a1_sync_stop(struct snd_pcm_substream *substream)
{
	struct es1869 *chip = snd_pcm_substream_chip(substream);

	hrtimer_cancel(&chip->a1_settle);
	synchronize_irq(chip->irq);
	return 0;
}

// the stream whose period ended, if Audio 1 runs one
void es1869_a1_irq(struct es1869 *chip, struct snd_pcm_substream **sub)
{
	if (chip->a1_state == ES_A1_RUNNING || chip->a1_state == ES_A1_SETTLING)
		*sub = chip->a1_sub;
}

static int a1c_open(struct snd_pcm_substream *substream)
{
	return a1_open(substream, ES_OWN_A1C);
}

static int fmc_open(struct snd_pcm_substream *substream)
{
	return a1_open(substream, ES_OWN_FMC);
}

static const struct snd_pcm_ops a1c_ops = {
	.open = a1c_open,
	.close = a1_close,
	.hw_params = a1_hw_params,
	.hw_free = a1_hw_free,
	.prepare = a1_prepare,
	.trigger = a1_trigger,
	.pointer = a1_pointer,
	.sync_stop = a1_sync_stop,
};

static const struct snd_pcm_ops fmc_ops = {
	.open = fmc_open,
	.close = a1_close,
	.hw_params = a1_hw_params,
	.hw_free = a1_hw_free,
	.prepare = a1_prepare,
	.trigger = a1_trigger,
	.pointer = a1_pointer,
	.sync_stop = a1_sync_stop,
};

// --- device 0's playback: subdevice 0 is Audio 2, 1 is Audio 1 ----------------

#define PLAY(op, ...)                                                 \
	(substream->number == 0 ? es1869_a2_ops.op(__VA_ARGS__) : \
				  a1_##op(__VA_ARGS__))

static int play_open(struct snd_pcm_substream *substream)
{
	return substream->number == 0 ? es1869_a2_ops.open(substream) :
					a1_open(substream, ES_OWN_A1P);
}

static int play_close(struct snd_pcm_substream *substream)
{
	return PLAY(close, substream);
}

static int play_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params)
{
	if (substream->number == 0)
		return es1869_a2_ops.hw_params ?
			       es1869_a2_ops.hw_params(substream, params) : 0;
	return a1_hw_params(substream, params);
}

static int play_hw_free(struct snd_pcm_substream *substream)
{
	if (substream->number == 0)
		return es1869_a2_ops.hw_free ? es1869_a2_ops.hw_free(substream) :
					       0;
	return a1_hw_free(substream);
}

static int play_prepare(struct snd_pcm_substream *substream)
{
	return PLAY(prepare, substream);
}

static int play_trigger(struct snd_pcm_substream *substream, int cmd)
{
	return PLAY(trigger, substream, cmd);
}

static snd_pcm_uframes_t play_pointer(struct snd_pcm_substream *substream)
{
	return PLAY(pointer, substream);
}

static int play_ack(struct snd_pcm_substream *substream)
{
	return substream->number == 0 && es1869_a2_ops.ack ?
		       es1869_a2_ops.ack(substream) : 0;
}

static int play_sync_stop(struct snd_pcm_substream *substream)
{
	return PLAY(sync_stop, substream);
}

static int play_fill_silence(struct snd_pcm_substream *substream, int channel,
			     unsigned long pos, unsigned long bytes)
{
	return PLAY(fill_silence, substream, channel, pos, bytes);
}

static const struct snd_pcm_ops play_ops = {
	.open = play_open,
	.close = play_close,
	.hw_params = play_hw_params,
	.hw_free = play_hw_free,
	.prepare = play_prepare,
	.trigger = play_trigger,
	.pointer = play_pointer,
	.ack = play_ack,
	.sync_stop = play_sync_stop,
	.fill_silence = play_fill_silence,
};

int es1869_pcm_new(struct es1869 *chip)
{
	struct snd_card *card = chip->card;
	struct snd_pcm_substream *sub;
	struct snd_dma_buffer *buf;
	int err;

	buf = snd_devm_alloc_pages(card->dev, SNDRV_DMA_TYPE_DEV, ES1869_A1_BUF);
	if (!buf)
		return -ENOMEM;
	chip->a1_buf = *buf;
	hrtimer_setup(&chip->a1_settle, a1_settle_fn, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL_HARD);

	err = snd_pcm_new(card, "ES1869", 0, 2, 1, &chip->pcm);
	if (err < 0)
		return err;
	chip->pcm->private_data = chip;
	strscpy(chip->pcm->name, "ESS AudioDrive ES1869");
	snd_pcm_set_ops(chip->pcm, SNDRV_PCM_STREAM_PLAYBACK, &play_ops);
	snd_pcm_set_ops(chip->pcm, SNDRV_PCM_STREAM_CAPTURE, &a1c_ops);
	sub = chip->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	strscpy(sub->name, "Audio 2");
	strscpy(sub->next->name, "Audio 1");
	strscpy(chip->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream->name,
		"Audio 1");
	err = es1869_mirror_init(chip);
	if (err < 0)
		return err;

	err = es1869_dual_new(chip);
	if (err < 0)
		return err;
	err = es1869_mon_new(chip);
	if (err < 0)
		return err;

	err = snd_pcm_new(card, "ES1869 FM", 1, 0, 1, &chip->pcm_fm);
	if (err < 0)
		return err;
	chip->pcm_fm->private_data = chip;
	strscpy(chip->pcm_fm->name, "ES1869 FM digital");
	snd_pcm_set_ops(chip->pcm_fm, SNDRV_PCM_STREAM_CAPTURE, &fmc_ops);
	strscpy(chip->pcm_fm->streams[SNDRV_PCM_STREAM_CAPTURE].substream->name,
		"Music DAC");
	return 0;
}
