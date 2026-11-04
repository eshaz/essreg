// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The chip's clock against the system's.
 *
 * Notes:
 *
 * The chip's stream interrupts come at a known count of its master clock,
 * and es1869_clock_irq() in resample.c fits a line through them against the
 * raw system clock, whichever stream runs: Audio 2 while it plays, Audio 1,
 * playing or recording, otherwise. The chain has two links, so that each is
 * fitted over the time it needs: the chip against the raw clock, two
 * crystals side by side, measured to a few ppb in minutes, and the raw clock
 * against the one NTP steers, which needs chronyd running as a service and
 * is the rate of that steering over a quarter of an hour. Without a daemon
 * the two clocks are one, and the stream locks to the laptop's own crystal.
 * On the Armada the first link looks constant, the chip on the system's own
 * crystal, so that it is the second that follows the crystal's drift.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <sound/info.h>

#include "es1869.h"

void es1869_clock_setup(struct es1869 *chip)
{
	es1869_clock_init(&chip->clk);
}

// a stream at num / den: what its interrupt adds to the master's count
u32 es1869_clock_period(u32 num, u32 den, u32 frames)
{
	return es1869_clock_tpf(num, den) * frames;
}

// a source's interrupt, under reg_lock, at the raw time the handler began,
// or 0 for a later pass. Audio 2 feeds the clock while it runs, Audio 1
// otherwise; a change of source or a start begins a new stretch
void es1869_clock_tick(struct es1869 *chip, u8 source, u64 raw)
{
	u32 ticks = source == ES_IRQ_A2 ? chip->clk_a2 : chip->clk_a1;
	struct system_time_snapshot snap;

	if (!ticks)
		return;
	if (source == ES_IRQ_A1 && chip->clk_a2 && chip->a2_running)
		return;
	if (chip->clk_src != source) {
		es1869_clock_stretch(&chip->clk);
		chip->clk_src = source;
	}
	if (!es1869_clock_irq(&chip->clk, ticks, raw))
		return;
	ktime_get_snapshot(&snap);
	es1869_clock_system(&chip->clk, ktime_to_ns(snap.raw),
			    ktime_to_ns(snap.boot));
	WRITE_ONCE(chip->clk_offset, es1869_clock_offset(&chip->clk));
}

void es1869_clock_proc(struct es1869 *chip, struct snd_info_buffer *buf)
{
	struct es1869_clock c;
	unsigned long flags;
	u8 src;

	if (es1869_val(ES_S_a2_resample) != 2 && !chip->clk.samples)
		return;
	spin_lock_irqsave(&chip->reg_lock, flags);
	c = chip->clk;
	src = chip->clk_src;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_iprintf(buf, "\nclock: the master %+lld ppb of %u Hz on the raw clock, %u samples in the line, %u in all, from %s\n",
		    es1869_clock_ppb(c.chip.rate), ES_CLOCK_HZ, c.chip.k + 1,
		    c.samples, src == ES_IRQ_A2 ? "Audio 2" :
				src == ES_IRQ_A1 ? "Audio 1" : "no stream");
	snd_iprintf(buf, "clock: the steered clock %+lld ppb of the raw one, %u samples\n",
		    es1869_clock_ppb(c.system.rate), c.system.k + 1);
	if (chip->mirror.rs.on)
		snd_iprintf(buf, "clock: Audio 2's stream takes %+lld ppb more of itself a frame\n",
			    ((s64)READ_ONCE(chip->mirror.rs.step) * 1000000000) >> 32);
}
