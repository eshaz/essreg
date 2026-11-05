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
 * tick and at the start, which copies a whole lead. .ack runs with
 * interrupts off, and an mmap client such as MPD through plughw hands
 * over a whole period at once, so that it copies only what a short write
 * brings, and a work item copies the rest a little at a time, with
 * interrupts on in between. The tick stays as the net under that work
 * item. It is a hard timer, since a soft one ran late under load at 99 kHz,
 * and the DMA then played what the ring held before. es1869_fill_plan in the chip core
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
 * a2_resample puts a 16-bit stream through the filter of resample.c on its
 * way to the ring. The chip then takes a ring frame for a little more or
 * less than a stream frame, by a2_trim_ppb and, in the Clock mode, by what
 * clock.c measures, so that the stream plays at its rate in the system's
 * time. The samples are no longer the application's; the mode's default is
 * off. The ring keeps a straight line between its frames and the stream's:
 * a position is the frame the ring writes next, w, plus rot and a fraction,
 * less what lies between the DMA and w. The filter is slow, for this CPU,
 * so it runs only in the work item and at the timer, never in .ack, in
 * pieces of a little under 3 ms of 44.1 kHz stereo, which keep interrupts
 * off for about 80 us, with the FPU from kernel_fpu_begin() around MMX. A filter reaches ES_RS_AHEAD frames past
 * its place, so the last frames written are made as if silence followed,
 * and again when what follows has come. A pointer that stands, as under
 * dmix, whose data changes in place, isn't filtered: its window would
 * cost ten times the filter's share of the CPU at each tick.
 *
 * a2_treble takes the same way with a table that gives back what the DAC's hold
 * takes from the top of the band, with or without a change of rate.
 *
 * a2_oversample plays the ring at two or three times the stream's rate. The
 * filter takes a half or a third of a frame a ring frame, the ring holds that
 * many times the buffer's frames, so the buffer's longest is that much shorter,
 * and the rest of the file counts in the ring's frames where the DMA is
 * concerned and in the stream's where the client is. A standing pointer isn't
 * filtered, so there a stream frame is written that many times over.
 *
 * The ring holds as many frames as the buffer, and a step down makes more
 * ring frames than stream frames, so a fill stops a little short of a whole
 * lap (es1869_rs_limit()), and the period's timer brings the rest.
 *
 * With the rate changed, a period of the ring is no longer a period of the
 * stream, and the chip's interrupts slide past the period boundaries, by a
 * period in a few minutes at 44.1 kHz. A client that waits for a period
 * would then be woken up to a period late, with a two-period buffer
 * almost empty, so a timer falls on the boundaries too.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <sound/pcm_params.h>
#ifdef CONFIG_X86
#include <asm/cpufeature.h>
#include <asm/fpu/api.h>
#endif

#include "es1869.h"

#define ES_FORMATS (SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 | \
		    SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE)
#define MIRROR_BUDGET 8192 // the stream bytes the timer copies, at most
#define MIRROR_SHORT 1024 // and .ack or the work item, about 18 us of them
#define MIRROR_GUARD 16 // the bytes left alone just ahead of the DMA
#define MIRROR_RS_SHORT 512 // what the filter takes at once, about 80 us of it
#define MIRROR_RS_START 1024 // the stream bytes the filter makes at the start
#define MIRROR_RS_MIN 256 // the frames a buffer needs for the filter
#define MIRROR_OVER_RATE 132300 // the highest rate the ring plays cleanly at
#define MIRROR_OVER_BURST 3 // and Demand 8, which leaves the bus the most

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

		if (m->over > 1) {
			// ahead of the DMA at the ring's rate, each frame again
			u32 k;

			for (k = 0; k < m->over; k++)
				es1869_spread(m->ring.area +
					      2 * (r * m->over + k) * fb,
					      rt->dma_area + a * fb, fb);
			n = 1;
		} else {
			es1869_spread(m->ring.area + 2 * r * fb,
				      rt->dma_area + a * fb, n * fb);
		}
		from += n;
		frames -= n;
	}
}

// a fill's frames through the filter. Its last frames were made as if
// silence followed, so they come again with the next fill. The stream's
// rate may change at each fill
static void mirror_resample(struct es1869 *chip, struct snd_pcm_runtime *rt,
			    unsigned long start, unsigned long frames,
			    unsigned long appl)
{
	struct es1869_mirror *m = &chip->mirror;
	struct es1869_rs *rs = &m->rs;
	struct es1869_rs_stream s = {
		.src = rt->dma_area,
		.ring = m->ring.area,
		.buffer = rt->buffer_size,
		.boundary = rt->boundary,
		.lap = m->lap,
		.appl = appl,
		.rot = &m->rot,
		.stereo = rt->channels == 2,
	};

	es1869_rs_pace(rs, rs->trim + (rs->clock ? rs->base +
				       READ_ONCE(chip->clk_offset) : 0));
	// after frames that were copied the line begins again at this frame:
	// those frames took no step
	if (!rs->lined) {
		es1869_rs_start(rs, start);
		rs->w = (start % rt->buffer_size + rt->buffer_size - m->rot) %
			rt->buffer_size * m->over;
		rs->since = start;
		rs->lined = true;
		m->rs_to = ULONG_MAX;
	}
#ifdef ES1869_RS_MMX
	s.mmx = rs->mmx && irq_fpu_usable();
	if (s.mmx)
		kernel_fpu_begin();
#endif
	es1869_rs_fill(rs, &s, start, frames);
#ifdef ES1869_RS_MMX
	if (s.mmx)
		kernel_fpu_end();
#endif
	m->rs_quiet = rs->w * 2 * frames_to_bytes(rt, 1);
	// a frame more than the filter reaches: on a step down a ring frame
	// covers less than a stream frame, so that the reach alone left a frame
	// behind that was made as if silence followed
	if (!m->fill.backlog) {
		es1869_rs_seek(rs, &s, -(ES_RS_AHEAD + 1) * (long)m->over);
		m->rs_to = (start + frames) % rt->boundary;
	}
	// the line decides where the next fill begins: a run can end a frame
	// past the plan, and those fills add up
	m->fill.filled = rs->pos;
}

// where the DMA is in the application's buffer; through the filter, its
// frames lie on the line behind what the ring was last written to
static snd_pcm_uframes_t mirror_offset(struct es1869 *chip,
				       struct snd_pcm_runtime *rt)
{
	struct es1869_mirror *m = &chip->mirror;
	u32 word = m->spread ? 2 : 1;
	u32 ring = word * frames_to_bytes(rt, m->lap), pos;
	snd_pcm_uframes_t b = rt->buffer_size, off;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	pos = es1869_dma_pos(&chip->hw, chip->dma2, ring) / word;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	off = bytes_to_frames(rt, pos);
	// before its first fill a stream has no line to read
	if (m->rs.on && m->rs.lined && !m->fill.standing)
		return es1869_rs_at(&m->rs, (m->rs.w + m->lap - off) % m->lap, b);
	return (off / m->over + m->rot) % b;
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
	unsigned long appl = rt->control->appl_ptr, to = appl, start, frames;

	if (m->rs.on) {
		// a drain's silence follows the last frame through the filter
		if (m->draining)
			to = m->silent_to;
		to = es1869_rs_limit(to, hw, rt->buffer_size, rt->boundary);
	}
	frames = es1869_fill_plan(&m->fill, to, hw, bytes_to_frames(rt, budget),
				  &start);
	if (!frames)
		return;
	// the filter with no change to make is a copy: step 0 at a whole frame
	if (m->rs.on && !m->fill.standing &&
	    (m->rs.frac || m->rs.trim || m->rs.clock || m->rs.treble ||
	     m->over > 1)) {
		// with nothing new, the last run's frames would only come again,
		// over the silence that follows a client that stopped
		if (m->rs.lined && (start + frames) % rt->boundary == m->rs_to) {
			m->fill.filled = m->rs.pos;
			return;
		}
		mirror_resample(chip, rt, start, frames, appl);
	} else {
		mirror_copy(chip, rt, start, frames);
		m->rs.lined = false;
	}
	m->max_chunk = max_t(u32, m->max_chunk, frames_to_bytes(rt, frames));
}

// a fill's cycles, since c0, and the most any fill took
static void mirror_cost(struct es1869_mirror *m, u32 *calls, u64 *total,
			cycles_t c0)
{
	u32 c = get_cycles() - c0;

	*calls += 1;
	*total += c;
	m->fill_max = max(m->fill_max, c);
}

// the filter's work from a context that shouldn't do it itself, with the
// timer as the net under it
static void mirror_kick(struct es1869 *chip)
{
	struct es1869_mirror *m = &chip->mirror;

	queue_work(system_highpri_wq, &m->copy_work);
	if (!hrtimer_active(&m->tick))
		hrtimer_start(&m->tick, us_to_ktime(es1869_val(ES_S_a2_tick_us)),
			      HRTIMER_MODE_REL_HARD);
}

// what .ack left, a piece at a time under the stream's lock, so that
// interrupts run between the pieces; hw_ptr does for the DMA's position,
// which only the count of late fills would need closer
static void mirror_copy_work(struct work_struct *work)
{
	struct es1869 *chip = container_of(work, struct es1869, mirror.copy_work);
	struct es1869_mirror *m = &chip->mirror;
	struct snd_pcm_substream *ss = chip->a2_sub;
	unsigned long flags;
	bool more;

	if (!ss)
		return;
	do {
		cycles_t c0;

		snd_pcm_stream_lock_irqsave(ss, flags);
		c0 = get_cycles();
		more = m->running;
		if (more) {
			mirror_fill(chip, ss->runtime, m->rs.on ?
				    MIRROR_RS_SHORT / m->over : MIRROR_SHORT,
				    ss->runtime->status->hw_ptr);
			more = m->fill.backlog;
			mirror_cost(m, &m->acks, &m->ack_cycles, c0);
		}
		snd_pcm_stream_unlock_irqrestore(ss, flags);
		cond_resched();
	} while (more);
	// nothing left for the net to catch; a standing pointer needs its tick,
	// which keeps the window ahead of the DMA that a short fill can't
	if (!READ_ONCE(m->fill.standing))
		hrtimer_try_to_cancel(&m->tick);
	else if (READ_ONCE(m->running) && !hrtimer_active(&m->tick))
		hrtimer_start(&m->tick, us_to_ktime(es1869_val(ES_S_a2_tick_us)),
			      HRTIMER_MODE_REL_HARD);
}

// the timer copies what the work item hasn't got to: every a2_tick_us while
// a copy is behind, and es1869_standing_us while the pointer stands, as under
// dmix, whose data changes in place. An application that moves its pointer
// gets its writes copied after .ack, and once nothing is left the timer stops
// until .ack leaves some behind
static enum hrtimer_restart mirror_tick(struct hrtimer *t)
{
	struct es1869 *chip = container_of(t, struct es1869, mirror.tick);
	struct es1869_mirror *m = &chip->mirror;
	struct snd_pcm_substream *ss = chip->a2_sub;
	u32 us = es1869_val(ES_S_a2_tick_us);
	unsigned long flags;
	cycles_t c0;

	if (!ss || !m->running)
		return HRTIMER_NORESTART;
	snd_pcm_stream_lock_irqsave(ss, flags);
	c0 = get_cycles();
	mirror_fill(chip, ss->runtime,
		    m->rs.on && !m->fill.standing ? MIRROR_RS_SHORT / m->over :
						    MIRROR_BUDGET,
		    mirror_hw(ss->runtime, mirror_offset(chip, ss->runtime)));
	if (m->fill.standing)
		us = es1869_standing_us(ss->runtime, MIRROR_BUDGET);
	mirror_cost(m, &m->ticks, &m->tick_cycles, c0);
	snd_pcm_stream_unlock_irqrestore(ss, flags);
	if (!m->fill.backlog && !m->fill.standing)
		return HRTIMER_NORESTART;
	hrtimer_forward_now(t, us_to_ktime(us));
	return HRTIMER_RESTART;
}

// the time until the stream reaches its next period's end, and a little
// more for the steps the DMA's position moves in
static u64 mirror_period_ns(struct snd_pcm_runtime *rt)
{
	snd_pcm_uframes_t to = rt->hw_ptr_interrupt + rt->period_size;
	snd_pcm_uframes_t left = (to + rt->boundary - rt->status->hw_ptr) %
				 rt->boundary;

	if (left > rt->period_size)
		left = rt->period_size;
	return div_u64((u64)left * NSEC_PER_SEC, rt->rate) + 100 * NSEC_PER_USEC;
}

// the stream's periods, which the chip's interrupts slide past; those stay,
// for the clock and as the net under this timer
static enum hrtimer_restart mirror_period(struct hrtimer *t)
{
	struct es1869 *chip = container_of(t, struct es1869, mirror.period);
	struct es1869_mirror *m = &chip->mirror;
	struct snd_pcm_substream *ss = chip->a2_sub;
	unsigned long flags;
	u64 ns;

	if (!ss || !m->running || !m->rs.on)
		return HRTIMER_NORESTART;
	snd_pcm_period_elapsed(ss);
	snd_pcm_stream_lock_irqsave(ss, flags);
	if (!ss->runtime || !m->running) {
		snd_pcm_stream_unlock_irqrestore(ss, flags);
		return HRTIMER_NORESTART;
	}
	ns = mirror_period_ns(ss->runtime);
	// what a full ring held back
	if ((ss->runtime->control->appl_ptr + ss->runtime->boundary -
	     m->fill.filled) % ss->runtime->boundary > ES_RS_AHEAD &&
	    ss->runtime->control->appl_ptr != m->rs_to)
		queue_work(system_highpri_wq, &m->copy_work);
	snd_pcm_stream_unlock_irqrestore(ss, flags);
	hrtimer_forward_now(t, ns_to_ktime(ns));
	return HRTIMER_RESTART;
}

// the ring silent in the stream's format
static void mirror_silence(struct es1869 *chip, struct snd_pcm_runtime *rt)
{
	const u8 *p = snd_pcm_format_silence_64(rt->format);
	u32 n = frames_to_bytes(rt, chip->mirror.lap), i;
	u8 low = p ? p[0] : 0, high = p ? p[1] : 0;
	u8 *w = chip->mirror.ring.area;

	// the pattern of a 16-bit sample, which an 8-bit one's byte repeats
	for (i = 0; i < n; i++) {
		w[2 * i] = i & 1 ? high : low;
		w[2 * i + 1] = 0;
	}
}

// a client that stopped writing and lets the stream run on, on the core's
// silence: nothing comes to fill the ring on the line, so its frames go
// silent from the next one to write on, and then as the DMA leaves them, up to
// a little behind it, and the DMA finds silence wherever it goes next
static void rs_ring_silence(struct es1869 *chip, struct snd_pcm_runtime *rt)
{
	struct es1869_mirror *m = &chip->mirror;
	u32 fb = 2 * frames_to_bytes(rt, 1), ring = m->lap * fb;
	u32 from = m->rs_quiet, to, n;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	to = es1869_dma_pos(&chip->hw, chip->dma2, ring);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	to = (to + ring - ES_RS_ROOM * fb) % ring;
	to -= to % fb;
	n = (to + ring - from) % ring;
	m->rs_quiet = to;
	if (from + n > ring) {
		memset(m->ring.area, 0, from + n - ring);
		n = ring - from;
	}
	memset(m->ring.area + from, 0, n);
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
	// on the line a frame of the buffer has no fixed place in the ring, so
	// the filter's fill takes the silence there: its tail reaches into it
	if (chip->mirror.rs.lined) {
		if (chip->mirror.draining)
			mirror_kick(chip);
		else
			rs_ring_silence(chip, rt);
	} else if (chip->mirror.spread) {
		mirror_copy(chip, rt, bytes_to_frames(rt, pos),
			    bytes_to_frames(rt, bytes));
	}
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
	es1869_drain_silence(ss, rt->status->hw_ptr, ES_FIFO_A2 / m->over,
			     &m->silent_to, a2_drain_fill);
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
		rt->hw.buffer_bytes_max = m->ring.bytes / 2 /
					  (es1869_val(ES_S_a2_oversample) + 1);
		rt->hw.period_bytes_max = rt->hw.buffer_bytes_max;
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

	hrtimer_cancel(&chip->mirror.period);
	cancel_work_sync(&chip->mirror.copy_work);
	hrtimer_cancel(&chip->mirror.tick);
	chip->clk_a2 = 0;
	a2_release(chip);
	es1869_power_idle(chip);
	return 0;
}

static void rs_free(void *tab)
{
	kvfree(tab);
}

// a table of the filter
static s16 *rs_table_new(struct es1869 *chip, bool flat)
{
	s16 *tab = kvmalloc_array(ES_RS_TABLE, sizeof(s16), GFP_KERNEL);

	if (!tab || devm_add_action_or_reset(chip->card->dev, rs_free, tab)) {
		dev_err(chip->dev, "no memory for the rate correction: off\n");
		return NULL;
	}
	es1869_rs_table(tab, flat);
	return tab;
}

// the filter's tables, each made once, when the settings first ask for it
static void rs_table(struct es1869 *chip)
{
	struct es1869_rs *rs = &chip->mirror.rs;

	if (!rs->tab && (es1869_val(ES_S_a2_resample) ||
			 es1869_val(ES_S_a2_oversample)))
		rs->tab = rs_table_new(chip, false);
	if (!rs->flat && es1869_val(ES_S_a2_treble))
		rs->flat = rs_table_new(chip, true);
}

// the ring frames to a stream frame: what a2_oversample asks for, where a
// divisor of the stream's own gives that rate exactly, the DMA can carry it
// and the ring has the room
static u32 rs_over(struct es1869 *chip, struct snd_pcm_runtime *rt, u32 num,
		   u32 den, bool slaved)
{
	struct es1869_mirror *m = &chip->mirror;
	u32 over = es1869_val(ES_S_a2_oversample) + 1;

	if (!m->spread || slaved || !m->rs.tab ||
	    rt->format != SNDRV_PCM_FORMAT_S16_LE ||
	    rt->buffer_size < MIRROR_RS_MIN)
		return 1;
	while (over > 1 && (den % over || num / den * over > MIRROR_OVER_RATE ||
			    2 * frames_to_bytes(rt, rt->buffer_size) * over >
			    m->ring.bytes))
		over--;
	return over;
}

// the settings as the stream takes them, under its lock, from a prepare, a
// start or a change while it plays. Going through the filter begins at the
// next frame to copy, on the line from the ring frame it falls on, and
// coming out leaves that frame's fraction behind, less than a frame
static void rs_apply(struct es1869 *chip, struct snd_pcm_runtime *rt)
{
	struct es1869_mirror *m = &chip->mirror;
	struct es1869_rs *rs = &m->rs;
	u32 mode = es1869_val(ES_S_a2_resample);
	bool treble = es1869_val(ES_S_a2_treble) && rs->flat;
	bool want = (mode || m->over > 1 ? treble || rs->tab : treble) &&
		    m->spread && rt->format == SNDRV_PCM_FORMAT_S16_LE &&
		    rt->buffer_size >= MIRROR_RS_MIN;
	u32 clk = want && mode == 2 ? m->clk_period : 0;

	rs->dither = es1869_val(ES_S_a2_resample_dither);
	rs->treble = treble;
	rs->trim = es1869_clock_q32(es1869_trim_ppb(es1869_val(ES_S_a2_trim)) +
				    es1869_val(ES_S_a2_trim_ppb));
	rs->clock = mode == 2;
	if (clk != chip->clk_a2) {
		chip->clk_a2 = clk;
		chip->clk_src = 0;
	}
	if (want == rs->on)
		return;
	rs->on = want;
	if (!want) {
		hrtimer_try_to_cancel(&m->period);
		rs->frac = 0;
		return;
	}
	rs->lined = false;
	es1869_rs_pace(rs, rs->trim);
	if (m->running) {
		queue_work(system_highpri_wq, &m->copy_work);
		hrtimer_start(&m->period, ns_to_ktime(mirror_period_ns(rt)),
			      HRTIMER_MODE_REL_HARD);
	}
}

// a setting of the rate correction written: a playing stream takes it at once,
// and one that hasn't started at its start
void es1869_a2_rate_changed(struct es1869 *chip)
{
	struct snd_pcm_substream *ss = chip->a2_sub;
	unsigned long flags;

	rs_table(chip);
	if (!ss || chip->a2_owner != ES_OWN_A2P)
		return;
	snd_pcm_stream_lock_irqsave(ss, flags);
	if (chip->mirror.running)
		rs_apply(chip, ss->runtime);
	snd_pcm_stream_unlock_irqrestore(ss, flags);
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
	m->clk_period = es1869_clock_period(num, den, rt->period_size);
	chip->clk_a2 = 0;
	rs_table(chip);
	m->over = rs_over(chip, rt, num, den, slaved);
	m->rs.over = m->over;
	m->lap = rt->buffer_size * m->over;
	den /= m->over;
	s.period *= m->over;
	s.dma_bytes *= m->over;
	if (m->over > 1)
		s.burst = MIRROR_OVER_BURST;
	if (es1869_val(ES_S_irq_trace))
		dev_info(chip->dev, "Audio 2: DMA %d at %#x, %u bytes, period %u\n",
			 s.ch, s.dma_phys, s.dma_bytes, s.period);
	m->last = 0;
	m->draining = false;
	if (m->spread) {
		hrtimer_cancel(&m->period);
		cancel_work_sync(&m->copy_work);
		hrtimer_cancel(&m->tick);
		m->running = false;
		m->rot = 0;
		es1869_fill_init(&m->fill, rt, MIRROR_GUARD,
				 bytes_to_frames(rt, ES_FIFO_A2));
		mirror_silence(chip, rt);
		m->rs.on = false;
		m->rs.lined = false;
		es1869_rs_start(&m->rs, 0);
		m->rs.seed = 0x2545F491;
		m->rs.last[0] = m->rs.last[1] = 0;
		m->rs.base = es1869_clock_base(num);
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
		rs_apply(chip, rt);
		m->running = true;
		m->fill_max = 0;
		mirror_fill(chip, rt, m->rs.on ? MIRROR_RS_START :
				max_t(u32, MIRROR_BUDGET,
				      frames_to_bytes(rt, m->fill.lead)),
			    mirror_hw(rt, mirror_offset(chip, rt)));
	}
	spin_lock_irqsave(&chip->reg_lock, flags);
	es1869_a2_go(&chip->hw, m->stream.burst, m->stream.single);
	chip->a2_running = true;
	chip->clk_src = 0;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (m->spread)
		hrtimer_start(&m->tick, us_to_ktime(es1869_val(ES_S_a2_tick_us)),
			      HRTIMER_MODE_REL_HARD);
	if (m->rs.on) {
		queue_work(system_highpri_wq, &m->copy_work);
		hrtimer_start(&m->period, ns_to_ktime(mirror_period_ns(rt)),
			      HRTIMER_MODE_REL_HARD);
	}
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
	m->rs.lined = false;
	es1869_rs_start(&m->rs, m->fill.filled);
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
		hrtimer_try_to_cancel(&m->period);
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
		hrtimer_try_to_cancel(&m->period);
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
		es1869_drain_silence(ss, mirror_hw(rt, off), ES_FIFO_A2 / m->over,
				     &m->silent_to, a2_drain_fill);
	return es1869_play_pointer(rt, off, ES_FIFO_A2 / m->over, m->draining,
				   &m->last);
}

// the core has just read the pointer for hw_ptr, so the 8237 needn't be read
// again; what a short copy leaves goes to the work item, under the timer
static int a2_ack(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);
	struct es1869_mirror *m = &chip->mirror;
	cycles_t c0 = get_cycles();

	if (!m->spread || !m->running)
		return 0;
	if (m->rs.on) {
		mirror_kick(chip);
		return 0;
	}
	mirror_fill(chip, ss->runtime, MIRROR_SHORT, ss->runtime->status->hw_ptr);
	mirror_cost(m, &m->acks, &m->ack_cycles, c0);
	if (!m->fill.backlog)
		return 0;
	queue_work(system_highpri_wq, &m->copy_work);
	if (!hrtimer_active(&m->tick))
		hrtimer_start(&m->tick, us_to_ktime(es1869_val(ES_S_a2_tick_us)),
			      HRTIMER_MODE_REL_HARD);
	return 0;
}

static int a2_sync_stop(struct snd_pcm_substream *ss)
{
	struct es1869 *chip = snd_pcm_substream_chip(ss);

	hrtimer_cancel(&chip->mirror.period);
	cancel_work_sync(&chip->mirror.copy_work);
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

	es1869_clock_setup(chip);
#ifdef CONFIG_X86
	m->rs.mmx = boot_cpu_has(X86_FEATURE_MMX);
#endif
	hrtimer_setup(&m->tick, mirror_tick, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL_HARD);
	hrtimer_setup(&m->period, mirror_period, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL_HARD);
	INIT_WORK(&m->copy_work, mirror_copy_work);
	m->over = 1;
	m->rs.over = 1;
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
