// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Dual playback: both DACs as one four-channel device, PCM device 2.
 * Channels 1 and 2 play on Audio 1 and channels 3 and 4 on Audio 2, from
 * the same buffer, at the same rate and from the same start.
 *
 * Notes:
 *
 * The application's buffer holds four-channel frames. Its first half of
 * each frame is copied to Audio 1's DMA buffer, and its second half to
 * Audio 2's ring of words, as mirror.c copies Audio 2's playback. Audio 2
 * runs on Audio 1's clock (71h bit 1 clear), so the two never drift, and
 * its interrupt is off: Audio 1's periods are the device's. As in mirror.c,
 * .ack copies a little, a work item the rest, under a timer.
 *
 * The start holds DMA 1 masked over B8h bit 0 as Audio 1's playback does,
 * and the timer that unmasks it writes 78h right after, so that both DACs
 * begin within microseconds of each other. dual_playback opens it.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/minmax.h>
#include <sound/pcm_params.h>

#include "es1869.h"

#define DUAL_BUDGET 8192 // the stream bytes the timer copies, at most
#define DUAL_SHORT 1024 // and .ack or the work item
#define DUAL_GUARD 16 // the bytes left alone just ahead of the DMA
#define DUAL_FIFO (2 * ES_FIFO_A1) // Audio 1's, in four-channel bytes

static const struct snd_pcm_hardware dual_hw = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_SYNC_APPLPTR |
		SNDRV_PCM_INFO_DRAIN_TRIGGER,
	.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
		   SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE,
	.rates = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_KNOT,
	.rate_min = 3100,
	.rate_max = ES_CLK_HI,
	.channels_min = 4,
	.channels_max = 4,
	.buffer_bytes_max = 2 * ES1869_A1_BUF,
	.period_bytes_min = 128,
	.period_bytes_max = 2 * ES1869_A1_BUF,
	.periods_min = 2,
	.periods_max = 1024,
};

// where the DMA is in the application's buffer: Audio 1's position, in
// its two-channel frames
static snd_pcm_uframes_t dual_offset(struct es1869 *chip,
				     struct snd_pcm_runtime *rt)
{
	u32 half = frames_to_bytes(rt, 1) / 2;
	unsigned long flags;
	u32 pos;

	spin_lock_irqsave(&chip->reg_lock, flags);
	pos = es1869_dma_pos(&chip->hw, chip->dma1, rt->buffer_size * half);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return (pos / half) % rt->buffer_size;
}

// frames of the application's buffer, from absolute frame from, into
// both halves
static void dual_copy(struct es1869 *chip, struct snd_pcm_runtime *rt,
		      snd_pcm_uframes_t from, snd_pcm_uframes_t frames)
{
	u32 fb = frames_to_bytes(rt, 1), half = fb / 2;
	snd_pcm_uframes_t b = rt->buffer_size;
	u8 *a1 = chip->a1_buf.area, *ring = chip->mirror.ring.area;

	while (frames) {
		snd_pcm_uframes_t a = from % b, n = min(frames, b - a);

		es1869_split(a1 + a * half, ring + 2 * a * half,
			     rt->dma_area + a * fb, n, half);
		from += n;
		frames -= n;
	}
}

// the silence past a drain's last frame, in the application's buffer and
// both halves
static int dual_drain_fill(struct snd_pcm_substream *ss, int channel,
			   unsigned long pos, unsigned long bytes)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;

	snd_pcm_format_set_silence(rt->format, rt->dma_area + pos,
				   bytes_to_samples(rt, bytes));
	dual_copy(chip, rt, bytes_to_frames(rt, pos), bytes_to_frames(rt, bytes));
	return 0;
}

// the core's silence: under a standing pointer it stays in the application's
// buffer for the timer to copy, as mirror.c does for Audio 2's ring
static int dual_fill_silence(struct snd_pcm_substream *ss, int channel,
			     unsigned long pos, unsigned long bytes)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_dual *d = &chip->dual;
	struct snd_pcm_runtime *rt = ss->runtime;

	if (d->running && !d->fill.standing)
		return dual_drain_fill(ss, channel, pos, bytes);
	snd_pcm_format_set_silence(rt->format, rt->dma_area + pos,
				   bytes_to_samples(rt, bytes));
	return 0;
}

// a drain's start: past the last frame, silence in the application's
// buffer and both halves, with Audio 1's FIFO, as far as the buffer is free
// now and then at each pointer reading (es1869_drain_silence)
static void dual_drain(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_dual *d = &chip->dual;
	struct snd_pcm_runtime *rt = ss->runtime;

	d->silent_to = rt->control->appl_ptr;
	d->draining = true;
	es1869_drain_silence(ss, rt->status->hw_ptr, DUAL_FIFO, &d->silent_to,
			     dual_drain_fill);
}

// both halves brought up to date, under the stream's lock, by at most
// budget bytes of the stream, with the DMA at hw
static void dual_fill(struct es1869 *chip, struct snd_pcm_runtime *rt,
		      u32 budget, snd_pcm_uframes_t hw)
{
	struct es1869_dual *d = &chip->dual;
	unsigned long start, frames;

	frames = es1869_fill_plan(&d->fill, rt->control->appl_ptr, hw,
				  bytes_to_frames(rt, budget), &start);
	if (frames)
		dual_copy(chip, rt, start, frames);
}

// what .ack left, a piece at a time, with interrupts on in between
static void dual_copy_work(struct work_struct *work)
{
	struct es1869 *chip = container_of(work, struct es1869, dual.copy_work);
	struct es1869_dual *d = &chip->dual;
	struct snd_pcm_substream *ss = chip->a1_sub;
	unsigned long flags;
	bool more;

	if (!ss)
		return;
	do {
		snd_pcm_stream_lock_irqsave(ss, flags);
		more = d->running;
		if (more) {
			dual_fill(chip, ss->runtime, DUAL_SHORT,
				  ss->runtime->status->hw_ptr);
			more = d->fill.backlog;
		}
		snd_pcm_stream_unlock_irqrestore(ss, flags);
		cond_resched();
	} while (more);
	if (!READ_ONCE(d->fill.standing))
		hrtimer_try_to_cancel(&d->tick);
}

static enum hrtimer_restart dual_tick(struct hrtimer *t)
{
	struct es1869 *chip = container_of(t, struct es1869, dual.tick);
	struct es1869_dual *d = &chip->dual;
	struct snd_pcm_substream *ss = chip->a1_sub;
	u32 us = es1869_val(ES_S_a2_tick_us);
	unsigned long flags;

	if (!ss || !d->running)
		return HRTIMER_NORESTART;
	snd_pcm_stream_lock_irqsave(ss, flags);
	dual_fill(chip, ss->runtime, DUAL_BUDGET,
		  es1869_abs_frame(ss->runtime, dual_offset(chip, ss->runtime)));
	if (d->fill.standing)
		us = es1869_standing_us(ss->runtime, DUAL_BUDGET);
	snd_pcm_stream_unlock_irqrestore(ss, flags);
	if (!d->fill.backlog && !d->fill.standing)
		return HRTIMER_NORESTART;
	hrtimer_forward_now(t, us_to_ktime(us));
	return HRTIMER_RESTART;
}

// from the settle: Audio 2's start, right after Audio 1's channel opened
void es1869_dual_settled(struct es1869 *chip)
{
	es1869_a2_go(&chip->hw, chip->dual.s2.burst, chip->dual.s2.single);
	chip->a2_running = true;
}

static void dual_release(struct es1869 *chip)
{
	mutex_lock(&chip->state_mutex);
	chip->a1_owner = chip->a2_owner = ES_OWN_NONE;
	chip->a1_sub = NULL;
	mutex_unlock(&chip->state_mutex);
}

static int dual_open(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;
	int err = 0;

	if (!es1869_val(ES_S_dual_playback))
		return -EPERM;
	if (!chip->mirror.spread)
		return -ENODEV; // the copy needs Audio 2's ring
	mutex_lock(&chip->state_mutex);
	if (chip->dead)
		err = -ENODEV;
	else if (chip->a1_owner != ES_OWN_NONE ||
		 chip->a2_owner != ES_OWN_NONE)
		err = -EBUSY;
	else {
		chip->a1_owner = chip->a2_owner = ES_OWN_DUAL;
		chip->a1_sub = ss;
		chip->a1_state = ES_A1_IDLE;
		err = es1869_power_up(chip);
		if (err)
			chip->a1_owner = chip->a2_owner = ES_OWN_NONE;
	}
	mutex_unlock(&chip->state_mutex);
	if (err)
		return err;
	rt->hw = dual_hw;
	err = es1869_whole_periods(rt);
	if (err >= 0)
		err = snd_pcm_hw_constraint_ratnums(rt, 0,
						    SNDRV_PCM_HW_PARAM_RATE,
						    &es1869_a1_rates);
	if (err >= 0 && es1869_val(ES_S_rate_limit))
		err = snd_pcm_hw_constraint_minmax(rt, SNDRV_PCM_HW_PARAM_RATE,
						   3100,
						   es1869_val(ES_S_a1p_max_rate));
	// the core calls no close for an open that failed
	if (err < 0)
		dual_release(chip);
	return err < 0 ? err : 0;
}

static int dual_close(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);

	cancel_work_sync(&chip->dual.copy_work);
	dual_release(chip);
	es1869_power_idle(chip);
	return 0;
}

static int dual_prepare(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;
	struct es1869_dual *d = &chip->dual;
	u32 half = frames_to_bytes(rt, 1) / 2;
	struct es1869_stream s1 = {
		.bits = snd_pcm_format_width(rt->format),
		.is_signed = snd_pcm_format_signed(rt->format) > 0,
		.stereo = true,
		.period = rt->period_size * half,
		.ch = chip->dma1,
		.dma_phys = chip->a1_buf.addr,
		.dma_bytes = rt->buffer_size * half,
		.single = es1869_val(ES_S_dma_single),
		.burst = es1869_val(ES_S_a1_burst),
		.corner = es1869_val(ES_S_filter_corner),
		.a8 = es1869_val(ES_S_a8_play_bits),
	};
	const u8 *sil = snd_pcm_format_silence_64(rt->format);
	u8 low = sil ? sil[0] : 0, high = sil ? sil[1] : 0;
	unsigned long flags;
	u32 i;
	int err, tries;

	if (rt->buffer_size * half > ES1869_A1_BUF ||
	    2 * rt->buffer_size * half > chip->mirror.ring.bytes)
		return -EINVAL;
	hrtimer_cancel(&d->tick);
	cancel_work_sync(&d->copy_work);
	d->running = false;
	es1869_fill_init(&d->fill, rt, DUAL_GUARD, 0);
	d->last = 0;
	d->draining = false;
	es1869_stream_rate(rt, true, &s1.rate_num, &s1.rate_den);
	d->s2 = s1;
	d->s2.ch = chip->dma2;
	d->s2.dma_phys = chip->mirror.ring.addr;
	d->s2.dma_bytes = 2 * rt->buffer_size * half;
	d->s2.burst = es1869_val(ES_S_a2_burst);
	// Audio 2 on Audio 1's clock: 70h only has to be valid
	d->s2.rate_num = ES_CLK_HI;
	d->s2.rate_den = 18;
	// both halves silent to start
	for (i = 0; i < rt->buffer_size * half; i++) {
		u8 v = i & 1 ? high : low;

		chip->a1_buf.area[i] = v;
		chip->mirror.ring.area[2 * i] = v;
		chip->mirror.ring.area[2 * i + 1] = 0;
	}
	for (tries = 0; tries < 2; tries++) {
		spin_lock_irqsave(&chip->reg_lock, flags);
		chip->a1_state = ES_A1_IDLE;
		chip->a1_last = 0;
		es1869_mix_update(&chip->hw, 0x71, 0x02, 0x00);
		err = es1869_a1_prepare(&chip->hw, &s1, false,
					es1869_val(ES_S_record_monitor));
		if (!err)
			err = es1869_a2_prepare(&chip->hw, &d->s2, false);
		if (!err && !chip->a1_dac) {
			err = es1869_a1_dac(&chip->hw, true);
			chip->a1_dac = !err;
		}
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		if ((err != -EBUSY && err != -EIO) || tries ||
		    es1869_dsp_recover(chip))
			break;
	}
	return err;
}

static int dual_hw_free(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	es1869_a2_stop(&chip->hw, chip->dma2);
	es1869_a1_stop(&chip->hw, false, chip->dma1, false);
	es1869_mix_update(&chip->hw, 0x71, 0x02,
			  es1869_val(ES_S_audio2_own_clock) ? 0x02 : 0);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (chip->a1_dac) {
		msleep(25);
		spin_lock_irqsave(&chip->reg_lock, flags);
		es1869_a1_dac(&chip->hw, false);
		chip->a1_dac = false;
		spin_unlock_irqrestore(&chip->reg_lock, flags);
	}
	return 0;
}

static int dual_trigger(struct snd_pcm_substream *ss, int cmd)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;
	struct es1869_dual *d = &chip->dual;
	unsigned long flags;
	int err = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		d->running = true;
		dual_fill(chip, rt, max_t(u32, DUAL_BUDGET,
					  frames_to_bytes(rt, d->fill.lead)),
			  es1869_abs_frame(rt, dual_offset(chip, rt)));
		spin_lock_irqsave(&chip->reg_lock, flags);
		err = es1869_a1_go(&chip->hw, false);
		if (!err)
			es1869_a1_start_settle(chip, rt);
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		hrtimer_start(&d->tick, us_to_ktime(es1869_val(ES_S_a2_tick_us)),
			      HRTIMER_MODE_REL_HARD);
		return err;
	case SNDRV_PCM_TRIGGER_DRAIN:
		dual_drain(ss);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		d->running = false;
		d->draining = false;
		hrtimer_try_to_cancel(&d->tick);
		hrtimer_try_to_cancel(&chip->a1_settle);
		spin_lock_irqsave(&chip->reg_lock, flags);
		es1869_a2_stop(&chip->hw, chip->dma2);
		chip->a2_running = false;
		err = es1869_a1_stop(&chip->hw, false, chip->dma1, false);
		chip->a1_state = ES_A1_IDLE;
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return err;
	}
	return -EINVAL;
}

static snd_pcm_uframes_t dual_pointer(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;
	snd_pcm_uframes_t off = dual_offset(chip, rt);
	struct es1869_dual *d = &chip->dual;

	if (d->draining)
		es1869_drain_silence(ss, es1869_abs_frame(rt, off), DUAL_FIFO,
				     &d->silent_to, dual_drain_fill);
	return es1869_play_pointer(rt, off, DUAL_FIFO, d->draining, &d->last);
}

static int dual_ack(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_dual *d = &chip->dual;

	if (!d->running)
		return 0;
	dual_fill(chip, ss->runtime, DUAL_SHORT, ss->runtime->status->hw_ptr);
	if (!d->fill.backlog)
		return 0;
	queue_work(system_highpri_wq, &d->copy_work);
	if (!hrtimer_active(&d->tick))
		hrtimer_start(&d->tick, us_to_ktime(es1869_val(ES_S_a2_tick_us)),
			      HRTIMER_MODE_REL_HARD);
	return 0;
}

static int dual_sync_stop(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);

	hrtimer_cancel(&chip->dual.tick);
	cancel_work_sync(&chip->dual.copy_work);
	hrtimer_cancel(&chip->a1_settle);
	synchronize_irq(chip->irq);
	return 0;
}

static const struct snd_pcm_ops dual_ops = {
	.open = dual_open,
	.close = dual_close,
	.hw_free = dual_hw_free,
	.prepare = dual_prepare,
	.trigger = dual_trigger,
	.pointer = dual_pointer,
	.ack = dual_ack,
	.sync_stop = dual_sync_stop,
	.fill_silence = dual_fill_silence,
};

int es1869_dual_new(struct es1869 *chip)
{
	struct es1869_dual *d = &chip->dual;
	int err;

	hrtimer_setup(&d->tick, dual_tick, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL_HARD);
	INIT_WORK(&d->copy_work, dual_copy_work);
	err = snd_pcm_new(chip->card, "ES1869 Dual", 2, 1, 0, &d->pcm);
	if (err < 0)
		return err;
	d->pcm->private_data = chip;
	strscpy(d->pcm->name, "ES1869 Dual");
	snd_pcm_set_ops(d->pcm, SNDRV_PCM_STREAM_PLAYBACK, &dual_ops);
	strscpy(d->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream->name,
		"Audio 1 and Audio 2");
	snd_pcm_set_managed_buffer_all(d->pcm, SNDRV_DMA_TYPE_VMALLOC, NULL, 0,
				       2 * ES1869_A1_BUF);
	return 0;
}
