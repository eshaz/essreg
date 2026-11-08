// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The monitor: PCM device 3, capture, the frames Audio 2's DAC took, read back
 * from what its DMA reads, just behind the DMA. They carry what the driver did
 * to the stream on its way there: the rate correction, the treble table, the
 * oversampling, and dmix's mixing in place.
 *
 * Notes:
 *
 * It delivers S16_LE stereo at the rate the DMA runs through the ring, the
 * stream's rate times a2_oversample's, so that 2x and 3x show as such. An
 * 8-bit sample becomes the high byte, a mono one both sides. On a 16-bit
 * channel it reads the ring of words, and on an 8-bit one the application's
 * buffer, which the DMA reads there.
 *
 * A ring frame behind the DMA goes to the next lap once a reading of Audio 2's
 * position has moved hw_ptr past it, so every such reading takes what the DMA
 * took since the last one, and so do the fills that start from an older
 * hw_ptr, before they copy. A timer of its own takes the rest, at each of the
 * monitor's period boundaries and at least every MON_STEP frames, which keeps
 * a take short with interrupts off, and calls snd_pcm_period_elapsed() only
 * past a boundary.
 *
 * The position is the DMA's, which runs ahead of the DAC by Audio 2's FIFO of
 * 64 bytes, 16 frames of 16-bit stereo; a stop drops what the FIFO holds
 * unplayed. The pointer reads the 8237 right before the core takes its
 * htstamp, so that a status gives a frame and its time.
 *
 * The monitor follows Audio 2 and holds it up nowhere. It opens at any time,
 * and from its own start, or Audio 2's next start, takes every frame the DMA
 * takes. Audio 2's stop, pause, underrun, drain's end or restart for
 * a2_oversample breaks the frames off, and the timer stops the monitor with an
 * xrun; after a prepare it goes on at Audio 2's next start. A start at another
 * rate is an xrun too, and a prepare fails with EBUSY while Audio 2 is
 * prepared at another rate. A read waits for as long as Audio 2 doesn't play.
 * The dual device's Audio 2 isn't followed.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/hrtimer.h>
#include <linux/minmax.h>
#include <linux/spinlock.h>
#include <sound/info.h>
#include <sound/pcm_params.h>

#include "es1869.h"

#define MON_BUF (256 * 1024) // the most a buffer holds, 65536 frames
#define MON_STEP 512UL // the most frames the timer takes at once
#define MON_STEP_MIN 64UL // and the least it waits for
#define MON_NOISE 32UL // the frames a reading of the 8237 may come early by
#define MON_LATE_NS (100 * NSEC_PER_USEC) // the timer's margin past a boundary
#define MON_WAIT_MS (24 * 3600 * 1000) // a read's wait for Audio 2: a day

static const struct snd_pcm_hardware mon_hw = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_KNOT,
	.rate_min = 3100,
	.rate_max = ES_CLK_HI,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = MON_BUF,
	.period_bytes_min = 1024,
	.period_bytes_max = MON_BUF / 2,
	.periods_min = 2,
	.periods_max = 1024,
};

static struct snd_pcm_substream *mon_ss(struct es1869_mon *mon)
{
	return mon->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
}

// n frames of what the DMA reads, from p, as S16_LE stereo at d
static void mon_convert(const struct es1869_mon *mon, __le16 *d, const u8 *p,
			unsigned long n)
{
	unsigned int w = mon->word, step = mon->fb * w;
	u16 flip = mon->is_signed ? 0 : 0x8000, l, r;

	for (; n; n--, p += step, d += 2) {
		if (mon->bits == 16) {
			l = (p[0] | p[w] << 8) ^ flip;
			r = mon->stereo ? (p[2 * w] | p[3 * w] << 8) ^ flip : l;
		} else {
			l = (p[0] << 8) ^ flip;
			r = mon->stereo ? (p[w] << 8) ^ flip : l;
		}
		d[0] = cpu_to_le16(l);
		d[1] = cpu_to_le16(r);
	}
}

// the frames broke off, on Audio 2's side or the reader's: the timer stops
// the stream with an xrun, outside the lock of whichever stream got here. A
// timer that runs sees the gap under the lock, now or at its next tick, and a
// start under it would make its forward one of a queued timer
static void mon_gap(struct es1869_mon *mon, bool overrun)
{
	mon->state = ES_MON_GAP;
	if (overrun)
		mon->overruns++;
	else
		mon->gaps++;
	if (!hrtimer_callback_running(&mon->tick))
		hrtimer_start(&mon->tick, ns_to_ktime(0),
			      HRTIMER_MODE_REL_HARD);
}

// what the DMA took, from where the monitor stands to the DMA's frame to,
// into the monitor's buffer. A reading a little early holds, and one that
// went most of a lap lost the DMA's place
static void mon_copy(struct es1869_mon *mon, unsigned long to)
{
	struct snd_pcm_runtime *rt = mon->rt;
	unsigned long b = rt->buffer_size, bound = rt->boundary, lap = mon->lap;
	unsigned long back = (mon->at + lap - to) % lap;
	unsigned long n = (to + lap - mon->at) % lap, avail;

	if (!n || back <= min(MON_NOISE, lap / 4))
		return;
	if (n > lap - lap / 4) {
		mon_gap(mon, false);
		return;
	}
	// a reader a buffer behind, unless it asked to be overrun
	if (READ_ONCE(rt->stop_threshold) < bound) {
		avail = (mon->pos + bound - READ_ONCE(rt->control->appl_ptr)) %
			bound;
		if (avail + n > b) {
			mon_gap(mon, true);
			return;
		}
	}
	mon->frames += n;
	while (n) {
		unsigned long piece = min3(n, lap - mon->at, b - mon->pos % b);

		mon_convert(mon, (__le16 *)rt->dma_area + 2 * (mon->pos % b),
			    mon->src + mon->at * mon->fb * mon->word, piece);
		mon->at = (mon->at + piece) % lap;
		mon->pos = (mon->pos + piece) % bound;
		n -= piece;
	}
}

// where Audio 2's DMA is in what it runs through, in frames, under the lock
static unsigned long mon_dma(struct es1869 *chip)
{
	struct es1869_mon *mon = &chip->mon;
	u32 bytes = mon->lap * mon->fb * mon->word, pos;

	spin_lock(&chip->reg_lock);
	pos = es1869_dma_pos(&chip->hw, chip->dma2, bytes);
	spin_unlock(&chip->reg_lock);
	return pos / mon->word / mon->fb;
}

// a reading of the DMA by the monitor itself, under the lock
static void mon_read(struct es1869 *chip)
{
	struct es1869_mon *mon = &chip->mon;

	if (mon->state == ES_MON_ON && mon->a2_on)
		mon_copy(mon, mon_dma(chip));
}

// Audio 2's DMA read at its frame, under Audio 2's stream lock: what it took
// since the last take, before a fill can copy the next lap over it
void es1869_mon_take(struct es1869 *chip, u32 frame)
{
	struct es1869_mon *mon = &chip->mon;
	unsigned long flags;

	if (!es1869_mon_taking(chip))
		return;
	spin_lock_irqsave(&mon->lock, flags);
	if (mon->state == ES_MON_ON && mon->a2_on)
		mon_copy(mon, frame);
	spin_unlock_irqrestore(&mon->lock, flags);
}

// whether the monitor's rate is the ring's, within the rounding of a rate
// that isn't a whole number of hertz
static bool mon_rate_ok(const struct es1869_mon *mon,
			const struct snd_pcm_runtime *rt)
{
	u64 r = (u64)rt->rate * mon->den, off;

	off = r > mon->num ? r - mon->num : mon->num - r;
	return mon->den && off < mon->den;
}

// the time to the monitor's next period boundary, or to a take of MON_STEP
// frames, a quarter of the lap or a quarter of its buffer if less; under the
// lock
static u64 mon_wait_ns(const struct es1869_mon *mon)
{
	const struct snd_pcm_runtime *rt = mon->rt;
	unsigned long left =
		(mon->next + rt->boundary - mon->pos) % rt->boundary;
	unsigned long most = min3(MON_STEP, (unsigned long)mon->lap / 4,
				  rt->buffer_size / 4);

	left = clamp(left, 1UL, max(most, MON_STEP_MIN));
	return div_u64((u64)left * mon->den * NSEC_PER_SEC, mon->num) +
	       MON_LATE_NS;
}

// the monitor's clock: its takes, its period boundaries, and a gap's xrun
static enum hrtimer_restart mon_tick(struct hrtimer *t)
{
	struct es1869_mon *mon = container_of(t, struct es1869_mon, tick);
	struct es1869 *chip = container_of(mon, struct es1869, mon);
	unsigned long flags, bound, p, past;
	bool gap, period = false;
	u64 ns = 0;

	spin_lock_irqsave(&mon->lock, flags);
	mon_read(chip);
	gap = mon->state == ES_MON_GAP;
	if (mon->state == ES_MON_ON) {
		bound = mon->rt->boundary;
		p = mon->rt->period_size;
		past = (mon->pos + bound - mon->next) % bound;
		// past one boundary or more, as hw_ptr_interrupt moves
		if (past < bound / 2) {
			mon->next = (mon->next + (past / p + 1) * p) % bound;
			period = true;
		}
		ns = mon_wait_ns(mon);
	}
	spin_unlock_irqrestore(&mon->lock, flags);
	if (gap) {
		if (es1869_val(ES_S_irq_trace))
			dev_info_ratelimited(
				chip->dev, "Audio 2 monitor: a gap, an xrun\n");
		snd_pcm_stop_xrun(mon_ss(mon));
		return HRTIMER_NORESTART;
	}
	if (period)
		snd_pcm_period_elapsed(mon_ss(mon));
	if (!ns)
		return HRTIMER_NORESTART;
	hrtimer_forward_now(t, ns_to_ktime(ns));
	return HRTIMER_RESTART;
}

// --- Audio 2's side ------------------------------------------------------------

// Audio 2's ring rate as prepared, num / den Hz; den 0 once it closes
void es1869_mon_a2_rate(struct es1869 *chip, u32 num, u32 den)
{
	struct es1869_mon *mon = &chip->mon;
	unsigned long flags;

	spin_lock_irqsave(&mon->lock, flags);
	mon->num = num;
	mon->den = den;
	spin_unlock_irqrestore(&mon->lock, flags);
}

// Audio 2's DMA starts: fresh, from the first frame it runs through, or else
// where it stood, as at the release of the chip's own pause on an 8-bit
// channel. A monitor that waits takes from there
void es1869_mon_a2_go(struct es1869 *chip, struct snd_pcm_runtime *rt,
		      bool fresh)
{
	struct es1869_mirror *m = &chip->mirror;
	struct es1869_mon *mon = &chip->mon;
	unsigned long flags;
	u64 ns = 0;

	spin_lock_irqsave(&mon->lock, flags);
	mon->src = m->spread ? m->ring.area : rt->dma_area;
	mon->word = m->spread ? 2 : 1;
	mon->fb = frames_to_bytes(rt, 1);
	mon->bits = snd_pcm_format_width(rt->format);
	mon->is_signed = snd_pcm_format_signed(rt->format) > 0;
	mon->stereo = rt->channels == 2;
	mon->lap = m->lap;
	mon->a2_on = true;
	if (mon->state == ES_MON_WAIT && !mon_rate_ok(mon, mon->rt)) {
		mon_gap(mon, false);
	} else if (mon->state == ES_MON_WAIT) {
		mon->at = fresh ? 0 : mon_dma(chip);
		mon->state = ES_MON_ON;
		ns = mon_wait_ns(mon);
	}
	spin_unlock_irqrestore(&mon->lock, flags);
	if (ns)
		hrtimer_start(&mon->tick, ns_to_ktime(ns),
			      HRTIMER_MODE_REL_HARD);
}

// Audio 2's DMA stops, after a last take: the frames break off there
void es1869_mon_a2_stop(struct es1869 *chip)
{
	struct es1869_mon *mon = &chip->mon;
	unsigned long flags;

	spin_lock_irqsave(&mon->lock, flags);
	mon->a2_on = false;
	if (mon->state == ES_MON_ON)
		mon_gap(mon, false);
	spin_unlock_irqrestore(&mon->lock, flags);
}

// --- the stream ----------------------------------------------------------------

static int mon_open(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_mon *mon = &chip->mon;
	const struct snd_pcm_hw_constraint_ratnums *rates = &es1869_a1_rates;
	struct snd_pcm_runtime *rt = ss->runtime;
	unsigned long flags;
	int err;

	if (!es1869_val(ES_S_monitor_device))
		return -EPERM;
	if (chip->dma2 < 0 || chip->dead)
		return -ENODEV;
	rt->hw = mon_hw;
	ss->wait_time = MON_WAIT_MS;
	// at the ring's rate where Audio 2 is prepared, else at any of the
	// clocks' rates
	spin_lock_irqsave(&mon->lock, flags);
	mon->rat = (struct snd_ratnum){
		.num = mon->num,
		.den_min = mon->den,
		.den_max = mon->den,
		.den_step = 1,
	};
	spin_unlock_irqrestore(&mon->lock, flags);
	mon->rats.nrats = 1;
	mon->rats.rats = &mon->rat;
	if (mon->rat.den_min)
		rates = &mon->rats;
	err = snd_pcm_hw_constraint_ratnums(rt, 0, SNDRV_PCM_HW_PARAM_RATE,
					    rates);
	if (err >= 0)
		err = es1869_whole_periods(rt);
	return err < 0 ? err : 0;
}

static int mon_close(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_mon *mon = &chip->mon;
	unsigned long flags;

	hrtimer_cancel(&mon->tick);
	spin_lock_irqsave(&mon->lock, flags);
	mon->state = ES_MON_OFF;
	mon->rt = NULL;
	spin_unlock_irqrestore(&mon->lock, flags);
	return 0;
}

static int mon_prepare(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_mon *mon = &chip->mon;
	struct snd_pcm_runtime *rt = ss->runtime;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&mon->lock, flags);
	// Audio 2 prepared at another rate, which the monitor can't follow
	if (mon->den && !mon_rate_ok(mon, rt))
		err = -EBUSY;
	mon->state = ES_MON_OFF;
	spin_unlock_irqrestore(&mon->lock, flags);
	return err;
}

// a reset, which a prepare makes after the prepare above, sets hw_ptr
// afresh: the frames taken count from it, with those taken past it and not
// yet read in, under a running stream
static int mon_ioctl(struct snd_pcm_substream *ss, unsigned int cmd, void *arg)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_mon *mon = &chip->mon;
	struct snd_pcm_runtime *rt = ss->runtime;
	unsigned long flags, hw, bound = rt->boundary, ahead = 0;
	int err = snd_pcm_lib_ioctl(ss, cmd, arg);

	if (err < 0 || cmd != SNDRV_PCM_IOCTL1_RESET)
		return err;
	spin_lock_irqsave(&mon->lock, flags);
	hw = rt->status->hw_ptr;
	if (mon->state != ES_MON_OFF)
		ahead = (mon->pos + bound - hw) % bound % rt->buffer_size;
	mon->pos = (hw + ahead) % bound;
	mon->next = (hw - hw % rt->period_size + rt->period_size) % bound;
	spin_unlock_irqrestore(&mon->lock, flags);
	return err;
}

static int mon_trigger(struct snd_pcm_substream *ss, int cmd)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_mon *mon = &chip->mon;
	unsigned long flags;
	u64 ns = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		spin_lock_irqsave(&mon->lock, flags);
		mon->rt = ss->runtime;
		if (!mon->a2_on) {
			mon->state = ES_MON_WAIT;
		} else if (!mon_rate_ok(mon, ss->runtime)) {
			mon_gap(mon, false);
		} else {
			mon->at = mon_dma(chip);
			mon->state = ES_MON_ON;
			ns = mon_wait_ns(mon);
		}
		spin_unlock_irqrestore(&mon->lock, flags);
		if (ns)
			hrtimer_start(&mon->tick, ns_to_ktime(ns),
				      HRTIMER_MODE_REL_HARD);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		spin_lock_irqsave(&mon->lock, flags);
		mon->state = ES_MON_OFF;
		spin_unlock_irqrestore(&mon->lock, flags);
		hrtimer_try_to_cancel(&mon->tick);
		return 0;
	}
	return -EINVAL;
}

// the frames taken: a fresh reading of the DMA first, so that the core's
// htstamp falls on it
static snd_pcm_uframes_t mon_pointer(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_mon *mon = &chip->mon;
	snd_pcm_uframes_t pos;
	unsigned long flags;

	spin_lock_irqsave(&mon->lock, flags);
	mon_read(chip);
	pos = mon->pos % ss->runtime->buffer_size;
	spin_unlock_irqrestore(&mon->lock, flags);
	return pos;
}

static int mon_sync_stop(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);

	hrtimer_cancel(&chip->mon.tick);
	return 0;
}

static const struct snd_pcm_ops mon_ops = {
	.open = mon_open,
	.close = mon_close,
	.ioctl = mon_ioctl,
	.prepare = mon_prepare,
	.trigger = mon_trigger,
	.pointer = mon_pointer,
	.sync_stop = mon_sync_stop,
};

void es1869_mon_proc(struct es1869 *chip, struct snd_info_buffer *buf)
{
	static const char *const states[] = { "stopped", "waiting for Audio 2",
					      "taking", "at a gap" };
	struct es1869_mon *mon = &chip->mon;
	unsigned long flags;
	u32 gaps, overruns;
	u64 frames;
	u8 state;

	spin_lock_irqsave(&mon->lock, flags);
	state = mon->state;
	frames = mon->frames;
	gaps = mon->gaps;
	overruns = mon->overruns;
	spin_unlock_irqrestore(&mon->lock, flags);
	snd_iprintf(buf,
		    "Audio 2 monitor: %s; %llu frames, %u gaps, %u overruns\n",
		    states[state], frames, gaps, overruns);
}

int es1869_mon_new(struct es1869 *chip)
{
	struct es1869_mon *mon = &chip->mon;
	int err;

	spin_lock_init(&mon->lock);
	hrtimer_setup(&mon->tick, mon_tick, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL_HARD);
	err = snd_pcm_new(chip->card, "ES1869 Monitor", 3, 0, 1, &mon->pcm);
	if (err < 0)
		return err;
	mon->pcm->private_data = chip;
	strscpy(mon->pcm->name, "ES1869 Monitor");
	snd_pcm_set_ops(mon->pcm, SNDRV_PCM_STREAM_CAPTURE, &mon_ops);
	strscpy(mon_ss(mon)->name, "Audio 2 DAC");
	snd_pcm_set_managed_buffer_all(mon->pcm, SNDRV_DMA_TYPE_VMALLOC, NULL,
				       0, MON_BUF);
	return 0;
}
