// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Audio 2's playback: device 0, playback subdevice 0.
 *
 * Notes:
 *
 * On an 8-bit DMA channel the chip reads the application's buffer. On a
 * 16-bit channel, which moves a word a cycle of which the chip keeps the
 * low byte, the chip plays from a ring of words that this file fills from
 * the application's buffer, each byte in the low byte of a word, as ESS's
 * drivers do (docs/ESS_EXTENDED.md, a2.base). The 8237 then counts words,
 * which are the chip's bytes. snd-es18xx set mixer 78h bit 5 there
 * instead, which the data sheet reserves, and the chip played nothing
 * usable (a2.wide).
 *
 * The ring is filled when the application moves its pointer (.ack, which
 * SNDRV_PCM_INFO_SYNC_APPLPTR brings for mmap clients too), at a timer
 * tick and at the start, which copies a whole lead. The tick is a hard
 * timer, since a soft one ran late under load at 99 kHz, and the DMA then
 * played what the ring held before. es1869_fill_plan in the chip core
 * decides what each fill copies: what the application wrote, once, while
 * its pointer moves, and when the pointer stands, as under dmix, which
 * mixes straight into the buffer, the frames from just ahead of the DMA to
 * a lead of a2_lead_ms, again at each tick.
 *
 * The core's silence, which alsa-lib's drain asks for past the last frame
 * and free-running clients behind what has played, goes to the ring as well
 * (.fill_silence): a copy that follows the application's pointer never
 * reaches past it, and the DMA, which runs on to the next period's
 * interrupt, played what the ring held before. A drain silences a period
 * and two FIFOs past the last frame itself, as far as the buffer is free,
 * and goes on at each pointer reading as the DMA frees more: a drain that
 * began with the buffer full, as after a resume's restart, played up to a
 * period of the ring's old data. Its position counts the FIFO, so that it
 * ends once the DAC has played the last frame.
 *
 * A pause stops the engine and a release starts it afresh from the paused
 * frame, which plays in step every time; the chip's own pause through 78h
 * resumed in step 6 times in 12.
 *
 * Audio 2 slaved to Audio 1's clock (Audio 2 Own Clock off) plays at A1h's
 * rate, so it reaches A1h's low clock, down to 3101 Hz, and takes Audio
 * 1's rate while Audio 1 runs.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/minmax.h>
#include <linux/sizes.h>
#include <sound/pcm_params.h>

#include "es1869.h"

#define ES_FORMATS (SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 | \
		    SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE)
#define MIRROR_BUDGET 8192 // the stream bytes one fill copies, at most
#define MIRROR_GUARD 16 // the bytes left alone just ahead of the DMA

static const struct snd_ratnum a2_clocks[] = {
	{ .num = ES_CLK_HI, .den_min = 1, .den_max = 128, .den_step = 1 },
	{ .num = ES_CLK_768, .den_min = 1, .den_max = 128, .den_step = 1 },
	{ .num = ES_CLK_LO, .den_min = 1, .den_max = 128, .den_step = 1 },
};

// on its own clock, 70h's two; slaved, A1h's three
static const struct snd_pcm_hw_constraint_ratnums a2_rates_own = {
	.nrats = 2,
	.rats = a2_clocks,
};

static const struct snd_pcm_hw_constraint_ratnums a2_rates_slaved = {
	.nrats = 3,
	.rats = a2_clocks,
};

static const struct snd_pcm_hardware a2_hw = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_DRAIN_TRIGGER,
	.formats = ES_FORMATS,
	.rates = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_KNOT,
	.rate_min = 3100,
	.rate_max = ES_CLK_HI,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = ES1869_A2_BUF,
	.period_bytes_min = 64,
	.period_bytes_max = ES1869_A2_BUF,
	.periods_min = 2,
	.periods_max = 1024,
};

// --- the ring ------------------------------------------------------------------

// frames from the application's buffer, at absolute position from, into
// the ring, both wrapping
static void mirror_copy(struct es1869 *chip, struct snd_pcm_runtime *rt,
			snd_pcm_uframes_t from, snd_pcm_uframes_t frames)
{
	struct es1869_mirror *m = &chip->mirror;
	snd_pcm_uframes_t b = rt->buffer_size;
	unsigned int fb = frames_to_bytes(rt, 1);

	while (frames) {
		snd_pcm_uframes_t a = from % b;
		snd_pcm_uframes_t r = (a + b - m->rot) % b;
		snd_pcm_uframes_t n = min3(frames, b - a, b - r);

		es1869_spread(m->ring.area + 2 * r * fb, rt->dma_area + a * fb,
			      n * fb);
		from += n;
		frames -= n;
	}
}

// where the DMA is in the application's buffer
static snd_pcm_uframes_t mirror_offset(struct es1869 *chip,
				       struct snd_pcm_runtime *rt)
{
	struct es1869_mirror *m = &chip->mirror;
	u32 word = m->spread ? 2 : 1;
	u32 ring = word * frames_to_bytes(rt, rt->buffer_size), pos;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	pos = es1869_dma_pos(&chip->hw, chip->dma2, ring) / word;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return (bytes_to_frames(rt, pos) + m->rot) % rt->buffer_size;
}

// that position as an absolute frame: the last hw_ptr and how far the DMA
// went on from it, or, up to a FIFO, how far it is behind, as after a
// pause's release, which plays again what the stop dropped from the FIFO
static snd_pcm_uframes_t mirror_hw(struct snd_pcm_runtime *rt,
				   snd_pcm_uframes_t off)
{
	snd_pcm_uframes_t b = rt->buffer_size, known = rt->status->hw_ptr;
	snd_pcm_uframes_t d = (off + b - known % b) % b;

	if (d && b - d <= bytes_to_frames(rt, ES_FIFO_A2))
		return (known + rt->boundary - (b - d)) % rt->boundary;
	return (known + d) % rt->boundary;
}

// the ring brought up to date, under the stream's lock, by at most budget
// bytes of the stream, with the DMA at hw
static void mirror_fill(struct es1869 *chip, struct snd_pcm_runtime *rt,
			u32 budget, snd_pcm_uframes_t hw)
{
	struct es1869_mirror *m = &chip->mirror;
	unsigned long start, frames;

	frames = es1869_fill_plan(&m->fill, rt->control->appl_ptr, hw,
				  bytes_to_frames(rt, budget), &start);
	if (!frames)
		return;
	mirror_copy(chip, rt, start, frames);
	m->max_chunk = max_t(u32, m->max_chunk, frames_to_bytes(rt, frames));
}

// the timer copies what .ack left: every a2_tick_us while a copy is behind,
// and es1869_standing_us while the pointer stands, as under dmix, whose
// data changes in place. An application that moves its pointer copies its
// writes at .ack, and once nothing is left the timer stops until .ack
// leaves some behind
static enum hrtimer_restart mirror_tick(struct hrtimer *t)
{
	struct es1869 *chip = container_of(t, struct es1869, mirror.tick);
	struct es1869_mirror *m = &chip->mirror;
	struct snd_pcm_substream *ss = chip->a2_sub;
	u32 us = es1869_val(ES_S_a2_tick_us);
	cycles_t c0 = get_cycles();
	unsigned long flags;

	if (!ss || !m->running)
		return HRTIMER_NORESTART;
	snd_pcm_stream_lock_irqsave(ss, flags);
	mirror_fill(chip, ss->runtime, MIRROR_BUDGET,
		    mirror_hw(ss->runtime, mirror_offset(chip, ss->runtime)));
	if (m->fill.standing)
		us = es1869_standing_us(ss->runtime, MIRROR_BUDGET);
	snd_pcm_stream_unlock_irqrestore(ss, flags);
	m->ticks++;
	m->tick_cycles += get_cycles() - c0;
	if (!m->fill.backlog && !m->fill.standing)
		return HRTIMER_NORESTART;
	hrtimer_forward_now(t, us_to_ktime(us));
	return HRTIMER_RESTART;
}

// the ring silent in the stream's format
static void mirror_silence(struct es1869 *chip, struct snd_pcm_runtime *rt)
{
	const u8 *p = snd_pcm_format_silence_64(rt->format);
	u32 n = frames_to_bytes(rt, rt->buffer_size), i;
	u8 low = p ? p[0] : 0, high = p ? p[1] : 0;
	u8 *w = chip->mirror.ring.area;

	// the pattern of a 16-bit sample, which an 8-bit one's byte repeats
	for (i = 0; i < n; i++) {
		w[2 * i] = i & 1 ? high : low;
		w[2 * i + 1] = 0;
	}
}

// the silence past a drain's last frame: in the application's buffer, which
// on an 8-bit channel is the DMA's, and the ring
static int a2_drain_fill(struct snd_pcm_substream *ss, int channel,
			 unsigned long pos, unsigned long bytes)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;

	snd_pcm_format_set_silence(rt->format, rt->dma_area + pos,
				   bytes_to_samples(rt, bytes));
	if (chip->mirror.spread)
		mirror_copy(chip, rt, bytes_to_frames(rt, pos),
			    bytes_to_frames(rt, bytes));
	return 0;
}

// the core's silence: under a standing pointer, in the interrupt, it stays
// in the application's buffer for the timer to copy with the rest
static int a2_fill_silence(struct snd_pcm_substream *ss, int channel,
			   unsigned long pos, unsigned long bytes)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_mirror *m = &chip->mirror;
	struct snd_pcm_runtime *rt = ss->runtime;

	if (es1869_val(ES_S_irq_trace))
		dev_info_ratelimited(chip->dev,
				     "Audio 2 silence at %lu, %lu bytes, appl %lu hw %lu\n",
				     pos, bytes, rt->control->appl_ptr,
				     rt->status->hw_ptr);
	if (m->running && !m->fill.standing)
		return a2_drain_fill(ss, channel, pos, bytes);
	snd_pcm_format_set_silence(rt->format, rt->dma_area + pos,
				   bytes_to_samples(rt, bytes));
	return 0;
}

// a drain's start: past the last frame, silence in the application's
// buffer and the ring, as far as the buffer is free now and then at each
// pointer reading (es1869_drain_silence); whatever a later copy brings
// from there is silent then too
static void mirror_drain(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_mirror *m = &chip->mirror;
	struct snd_pcm_runtime *rt = ss->runtime;

	m->silent_to = rt->control->appl_ptr;
	m->draining = true;
	es1869_drain_silence(ss, rt->status->hw_ptr, ES_FIFO_A2, &m->silent_to,
			     a2_drain_fill);
}

// --- the stream ----------------------------------------------------------------

// the rate a slaved Audio 2 holds Audio 1 to, which A1h sets for both, or 0
unsigned int es1869_a2_tied_rate(struct es1869 *chip)
{
	struct snd_pcm_substream *ss = chip->a2_sub;

	if (es1869_val(ES_S_audio2_own_clock) ||
	    chip->a2_owner != ES_OWN_A2P || !ss || !ss->runtime)
		return 0;
	return ss->runtime->rate;
}

// another engine's rate that slaving ties Audio 2 to, or 0
static unsigned int a1_rate(struct es1869 *chip)
{
	struct snd_pcm_substream *ss = chip->a1_sub;

	if (chip->a1_owner == ES_OWN_NONE || !ss || !ss->runtime)
		return 0;
	return ss->runtime->rate;
}

static void a2_release(struct es1869 *chip)
{
	mutex_lock(&chip->state_mutex);
	chip->a2_owner = ES_OWN_NONE;
	chip->a2_sub = NULL;
	mutex_unlock(&chip->state_mutex);
}

static int a2_open(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;
	struct es1869_mirror *m = &chip->mirror;
	bool slaved = !es1869_val(ES_S_audio2_own_clock);
	unsigned int tied;
	int err = 0;

	if (chip->dma2 < 0)
		return -ENODEV;
	mutex_lock(&chip->state_mutex);
	if (chip->dead)
		err = -ENODEV;
	else if (chip->a2_owner != ES_OWN_NONE)
		err = -EBUSY;
	else {
		chip->a2_owner = ES_OWN_A2P;
		chip->a2_sub = ss;
		err = es1869_power_up(chip);
		if (err)
			chip->a2_owner = ES_OWN_NONE;
	}
	tied = slaved ? a1_rate(chip) : 0;
	mutex_unlock(&chip->state_mutex);
	if (err)
		return err;

	rt->hw = a2_hw;
	if (m->spread) {
		rt->hw.info |= SNDRV_PCM_INFO_SYNC_APPLPTR | SNDRV_PCM_INFO_PAUSE;
		rt->hw.buffer_bytes_max = m->ring.bytes / 2;
		rt->hw.period_bytes_max = m->ring.bytes / 2;
	} else if (es1869_val(ES_S_a2_pause)) {
		rt->hw.info |= SNDRV_PCM_INFO_PAUSE;
	}
	err = snd_pcm_hw_constraint_ratnums(rt, 0, SNDRV_PCM_HW_PARAM_RATE,
					    slaved ? &a2_rates_slaved :
						     &a2_rates_own);
	if (err >= 0 && tied)
		err = snd_pcm_hw_constraint_single(rt, SNDRV_PCM_HW_PARAM_RATE,
						   tied);
	if (err >= 0)
		err = es1869_pcm_limits(rt, &es1869_limit_a2);
	// the core calls no close for an open that failed
	if (err < 0)
		a2_release(chip);
	return err < 0 ? err : 0;
}

static int a2_close(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);

	a2_release(chip);
	es1869_power_idle(chip);
	return 0;
}

static int a2_prepare(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;
	struct es1869_mirror *m = &chip->mirror;
	bool slaved = !es1869_val(ES_S_audio2_own_clock);
	u32 bytes = snd_pcm_lib_buffer_bytes(ss);
	struct es1869_stream s = {
		.bits = snd_pcm_format_width(rt->format),
		.is_signed = snd_pcm_format_signed(rt->format) > 0,
		.stereo = rt->channels == 2,
		.period = frames_to_bytes(rt, rt->period_size),
		.ch = chip->dma2,
		.dma_phys = m->spread ? m->ring.addr : rt->dma_addr,
		.dma_bytes = m->spread ? 2 * bytes : bytes,
		.single = es1869_val(ES_S_dma_single),
		.burst = es1869_val(ES_S_a2_burst),
		.corner = es1869_val(ES_S_filter_corner),
		.a8 = 0,
	};
	u32 num, den, tied;
	unsigned long flags;
	int err = 0;

	es1869_stream_rate(rt, slaved, &num, &den);
	if (es1869_val(ES_S_irq_trace))
		dev_info(chip->dev, "Audio 2: DMA %d at %#x, %u bytes, period %u\n",
			 s.ch, s.dma_phys, s.dma_bytes, s.period);
	m->last = 0;
	m->draining = false;
	if (m->spread) {
		hrtimer_cancel(&m->tick);
		m->running = false;
		m->rot = 0;
		es1869_fill_init(&m->fill, rt, MIRROR_GUARD,
				 bytes_to_frames(rt, ES_FIFO_A2));
		mirror_silence(chip, rt);
	}
	spin_lock_irqsave(&chip->reg_lock, flags);
	if (slaved) {
		// Audio 2 plays at A1h's rate: A1h is written for it unless
		// Audio 1 already runs at that rate; 70h only has to be valid
		tied = a1_rate(chip);
		if (tied && tied != rt->rate) {
			err = -EBUSY;
		} else if (!tied) {
			u8 a1;
			bool new_mode;

			err = es1869_a1_rate(num, den, &a1, &new_mode);
			if (!err) {
				es1869_mix_update(&chip->hw, 0x71, 0x20,
						  new_mode ? 0x20 : 0);
				err = es1869_ctrl_write(&chip->hw, 0xA1, a1);
			}
			if (!err)
				err = es1869_ctrl_write(&chip->hw, 0xA2,
							es1869_filter(rt->rate,
								      s.corner));
		}
		num = ES_CLK_HI;
		den = 18;
	}
	s.rate_num = num;
	s.rate_den = den;
	if (!err)
		err = es1869_a2_prepare(&chip->hw, &s, true);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	m->stream = s;
	return err;
}

static void a2_go(struct es1869 *chip, struct snd_pcm_runtime *rt)
{
	struct es1869_mirror *m = &chip->mirror;
	unsigned long flags;

	// the start copies the whole lead, since the first tick comes 2 ms
	// on and the DMA takes 4 KB in 8 ms at the highest rates
	if (m->spread) {
		m->running = true;
		mirror_fill(chip, rt, max_t(u32, MIRROR_BUDGET,
					    frames_to_bytes(rt, m->fill.lead)),
			    mirror_hw(rt, mirror_offset(chip, rt)));
	}
	spin_lock_irqsave(&chip->reg_lock, flags);
	es1869_a2_go(&chip->hw, m->stream.burst, m->stream.single);
	chip->a2_running = true;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (m->spread)
		hrtimer_start(&m->tick, us_to_ktime(es1869_val(ES_S_a2_tick_us)),
			      HRTIMER_MODE_REL_HARD);
}

// a release starts afresh from the paused frame, less the FIFO's 64 bytes
// that the stop dropped unplayed, as far as the buffer still holds them: the
// ring starts there, so all of it is copied again, and the pointer stays
// at the paused frame until the DMA passes it again (es1869_forward). A
// standing pointer's copy starts ahead of the DMA, so there it starts at
// the paused frame itself, and what the guard skips is copied here
static void a2_rotate(struct es1869 *chip, struct snd_pcm_runtime *rt)
{
	struct es1869_mirror *m = &chip->mirror;
	snd_pcm_uframes_t b = rt->buffer_size, back = 0;
	snd_pcm_uframes_t queued = (rt->control->appl_ptr + rt->boundary -
				    rt->status->hw_ptr) % rt->boundary;

	if (!m->fill.standing && queued <= b)
		back = min3(bytes_to_frames(rt, ES_FIFO_A2), b - queued, b / 4);
	m->rot = (m->pos + b - back) % b;
	m->fill.filled = mirror_hw(rt, m->rot);
	if (m->fill.standing)
		mirror_copy(chip, rt, m->pos, m->fill.guard);
}

static int a2_trigger(struct snd_pcm_substream *ss, int cmd)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;
	struct es1869_mirror *m = &chip->mirror;
	unsigned long flags;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		a2_go(chip, rt);
		return 0;
	case SNDRV_PCM_TRIGGER_DRAIN:
		mirror_drain(ss);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		m->running = false;
		m->draining = false;
		hrtimer_try_to_cancel(&m->tick);
		spin_lock_irqsave(&chip->reg_lock, flags);
		es1869_a2_stop(&chip->hw, chip->dma2);
		chip->a2_running = false;
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return 0;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (!m->spread) {
			// an 8-bit channel, a2_pause=1: the chip's own pause, bits
			// 1:0 of 78h together
			spin_lock_irqsave(&chip->reg_lock, flags);
			es1869_mix_update(&chip->hw, 0x78, 0x03, 0);
			chip->a2_running = false;
			spin_unlock_irqrestore(&chip->reg_lock, flags);
			return 0;
		}
		m->running = false;
		hrtimer_try_to_cancel(&m->tick);
		m->pos = mirror_offset(chip, rt);
		spin_lock_irqsave(&chip->reg_lock, flags);
		es1869_a2_stop(&chip->hw, chip->dma2);
		chip->a2_running = false;
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return 0;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!m->spread) {
			spin_lock_irqsave(&chip->reg_lock, flags);
			es1869_mix_update(&chip->hw, 0x78, 0x03, 0x03);
			chip->a2_running = true;
			spin_unlock_irqrestore(&chip->reg_lock, flags);
			return 0;
		}
		a2_rotate(chip, rt);
		spin_lock_irqsave(&chip->reg_lock, flags);
		es1869_a2_prepare(&chip->hw, &m->stream, true);
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		a2_go(chip, rt);
		// the core skips its check of the time since the last reading
		// once at a release, but the interrupts no longer fall on
		// period boundaries, and the first would add a buffer
		rt->hw_ptr_jiffies = jiffies;
		return 0;
	}
	return -EINVAL;
}

static snd_pcm_uframes_t a2_pointer(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_mirror *m = &chip->mirror;
	struct snd_pcm_runtime *rt = ss->runtime;
	snd_pcm_uframes_t off = mirror_offset(chip, rt);

	if (m->draining)
		es1869_drain_silence(ss, mirror_hw(rt, off), ES_FIFO_A2,
				     &m->silent_to, a2_drain_fill);
	return es1869_play_pointer(rt, off, ES_FIFO_A2, m->draining, &m->last);
}

// the core has just read the pointer for hw_ptr, so the 8237 needn't be read
// again; what the budget left, the timer copies
static int a2_ack(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_mirror *m = &chip->mirror;
	cycles_t c0 = get_cycles();

	if (!m->spread || !m->running)
		return 0;
	mirror_fill(chip, ss->runtime, MIRROR_BUDGET, ss->runtime->status->hw_ptr);
	m->acks++;
	m->ack_cycles += get_cycles() - c0;
	if (m->fill.backlog && !hrtimer_active(&m->tick))
		hrtimer_start(&m->tick, us_to_ktime(es1869_val(ES_S_a2_tick_us)),
			      HRTIMER_MODE_REL_HARD);
	return 0;
}

static int a2_sync_stop(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);

	hrtimer_cancel(&chip->mirror.tick);
	synchronize_irq(chip->irq);
	return 0;
}

const struct snd_pcm_ops es1869_a2_ops = {
	.open = a2_open,
	.close = a2_close,
	.prepare = a2_prepare,
	.trigger = a2_trigger,
	.pointer = a2_pointer,
	.ack = a2_ack,
	.fill_silence = a2_fill_silence,
	.sync_stop = a2_sync_stop,
};

void es1869_a2_irq(struct es1869 *chip, struct snd_pcm_substream **sub)
{
	if (chip->a2_running)
		*sub = chip->a2_sub;
}

// subdevice 0's buffer: the application's, and on a 16-bit channel the
// ring, 128 KiB of words in one 128 KiB page under 16 MB (the 8237 can't
// cross one), or half of that where the allocation gives no such page
int es1869_mirror_init(struct es1869 *chip)
{
	struct snd_pcm_substream *ss =
		chip->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	struct es1869_mirror *m = &chip->mirror;
	struct snd_dma_buffer *buf;
	size_t size;

	hrtimer_setup(&m->tick, mirror_tick, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL_HARD);
	m->spread = chip->dma2 >= 4;
	if (chip->dma2 < 0)
		return 0;
	if (!m->spread) {
		snd_pcm_set_managed_buffer(ss, SNDRV_DMA_TYPE_DEV, chip->card->dev,
					   ES1869_A2_BUF, ES1869_A2_BUF);
		return 0;
	}
	for (size = ES1869_RING; size >= ES1869_RING / 2; size /= 2) {
		buf = snd_devm_alloc_pages(chip->card->dev, SNDRV_DMA_TYPE_DEV,
					   size);
		if (buf && !(buf->addr & (size - 1)) &&
		    buf->addr + size <= SZ_16M)
			break;
		buf = NULL;
	}
	if (!buf) {
		dev_err(chip->dev, "no DMA ring for Audio 2 under 16 MB\n");
		return -ENOMEM;
	}
	m->ring = *buf;
	snd_pcm_set_managed_buffer(ss, SNDRV_DMA_TYPE_VMALLOC, NULL, 0,
				   m->ring.bytes / 2);
	return 0;
}
