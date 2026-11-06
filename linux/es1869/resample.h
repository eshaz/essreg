/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * A small change of rate for Audio 2's ring, and the chip's clock against
 * the system's, with no kernel API, so that they also build on the host.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ES1869_RESAMPLE_H
#define ES1869_RESAMPLE_H

#include "hw_compat.h"

#define ES_RS_TAPS 68 // of the filter, four to an MMX word
#define ES_RS_BEHIND 33 // the frames before a position that it reads
#define ES_RS_AHEAD 34 // and from the position on
#define ES_RS_ROWS 128 // the places between two frames that it holds
#define ES_RS_STRIDE (2 * ES_RS_TAPS) // a row: the taps, then their steps
#define ES_RS_TABLE (ES_RS_ROWS * ES_RS_STRIDE) // the table's entries
#define ES_RS_CHUNK 256 // the stream frames of a run, at most
#define ES_RS_WINDOW (ES_RS_CHUNK + ES_RS_TAPS + 2) // the frames it may read
#define ES_RS_ROOM 64 // the frames kept clear ahead of the DMA, at the least
#define ES_RS_OVER 3 // the most ring frames to a stream frame

// a run's last frame reads the taps from there on, so its copies need no more
// than the window, in the words es1869_rs_shift copies
_Static_assert((ES_RS_CHUNK + ES_RS_TAPS + 3) / 4 * 4 <= ES_RS_WINDOW,
	       "es1869_rs_shift would run past the window");

#if defined(__i386__) || defined(__x86_64__)
#define ES1869_RS_MMX
#endif

// one run through the filter
struct es1869_rs_job {
	const s16 *tab; // es1869_rs_table's
	u32 centre; // what brings the centre word to the sum's scale, in bits
	const s16 *ch[2]; // the channels, from the first frame's first tap
	const s16 *odd[2][3]; // and from their 2nd to 4th sample, for MMX
	u8 *dst; // the ring: each byte in the low byte of a word
	u32 frames; // the stream frames to go through
	u32 room; // the ring frames to write, at most
	u32 used; // comes back: the stream frames gone through
	u32 frac; // where the frame falls between two stream frames, 32 bits
	u32 whole; // the stream frames a ring frame takes, whole
	u32 step; // and past those, 32 bits
	u32 over; // the ring frames to a stream frame
	bool stereo;
	bool dither; // the 16-bit rounding with triangular noise of 1 LSB
	u32 seed; // the noise's state
	s32 last[2]; // its last values
};

// Audio 2's stream through the filter: the ring holds its frame w at the
// stream's frame pos and frac past it, which lie on a line, and pos is w + rot
// in the buffer
struct es1869_rs {
	s16 *tab; // the filter, made for the first stream that uses it
	s16 *flat; // and with the DAC's hold corrected, when that's asked for
	bool treble; // the stream goes through that one
	bool on; // this stream goes through it
	bool mmx; // the CPU has MMX
	bool clock; // it follows the chip's clock (a2_resample 2)
	bool dither;
	bool lined; // w and pos are on the line: the last fill went through it
	u32 frac; // how far past pos, 32 bits
	s32 trim; // a2_trim_ppb in the step's 32 bits
	s32 base; // what the stream's clock is off by, in the same
	s32 step; // what a ring frame takes over a stream frame, in the same
	u32 over; // the ring frames to a stream frame
	u64 adv; // the stream frames a ring frame takes, 32 bits of fraction
	unsigned long w; // the next ring frame to write
	unsigned long pos; // the stream's frame that falls there
	u32 since; // the stream's frames before pos, as far as they matter
	u32 seed; // the dither's state
	s32 last[2];
	s16 ch[2][ES_RS_WINDOW] __aligned(8); // the frames around the run
	s16 odd[2][3][ES_RS_WINDOW] __aligned(8); // the same a sample on, each
};

// what a fill works on
struct es1869_rs_stream {
	const void *src; // the application's buffer
	u8 *ring; // the ring, each byte in the low byte of a word
	unsigned long buffer, boundary; // in frames, as the PCM core has them
	unsigned long lap; // the ring's frames, over times the buffer's
	unsigned long appl; // the first frame not yet written
	unsigned long *rot; // the stream's frame at the ring's start
	bool stereo;
	bool mmx; // the caller holds the FPU
};

void es1869_rs_table(s16 *tab, bool flat);
void es1869_rs_split(s16 *l, s16 *r, const void *src, u32 frames, bool stereo);
void es1869_rs_shift(struct es1869_rs *rs, struct es1869_rs_job *j, u32 n,
		     bool stereo);
u32 es1869_rs_run_c(struct es1869_rs_job *j);
#ifdef ES1869_RS_MMX
u32 es1869_rs_run_mmx(struct es1869_rs_job *j);
#endif
void es1869_rs_start(struct es1869_rs *rs, unsigned long pos);
void es1869_rs_pace(struct es1869_rs *rs, s32 step);
void es1869_rs_seek(struct es1869_rs *rs, const struct es1869_rs_stream *s,
		    long d);
void es1869_rs_fill(struct es1869_rs *rs, const struct es1869_rs_stream *s,
		    unsigned long start, unsigned long frames);
unsigned long es1869_rs_at(const struct es1869_rs *rs, unsigned long a,
			   unsigned long buffer);
unsigned long es1869_rs_limit(unsigned long to, unsigned long hw,
			      unsigned long room, unsigned long buffer,
			      unsigned long boundary);

// the master clock in Hz, which 30 divides to the 793.8 kHz clock and 31 to
// what the chip calls 768 kHz, 768193.5 Hz
#define ES_CLOCK_HZ 23814000
#define ES_TRIM_ZERO 12 // a2_trim's item for no trim
#define ES_CLOCK_GROUP_NS 1073741824 // a second of interrupts, one sample

// a line through samples of a time against a reference, over the last kmax
#define ES_FIT_CHIP 256
#define ES_FIT_SYSTEM 1024

struct es1869_fit {
	s64 x; // the time less the reference, in ns
	s64 line; // where the line has it
	s64 rate; // the line's slope, 40 bits
	u32 k; // the samples in the slope, less one
	u32 kmax;
	u32 odd; // the samples in a row off the line
	bool seeded; // this stretch has its first sample
};

// the chip's clock against the raw system clock, and that against the
// clock NTP steers, which together give the chip against the steered clock
struct es1869_clock {
	struct es1869_fit chip, system;
	u64 ticks; // the master's ticks in this stretch
	u64 g_ticks, g_raw; // where the group of interrupts began
	s64 g_best; // the earliest of them against the fitted clock, in ns
	u64 b_ticks, b_raw; // that interrupt
	u64 l_ticks, l_raw; // the last sample
	u64 u_raw, u_mono; // the last reading of the system's two clocks
	u32 ns_q16; // ns to a tick, 16 bits
	u32 samples; // taken since the start
	bool have; // this stretch has its first interrupt
	bool fed; // and its first sample
	bool timed; // and its first reading of the system's two clocks
};

u32 es1869_clock_tpf(u32 num, u32 den);
s32 es1869_clock_base(u32 num);
s32 es1869_clock_q32(s32 ppb);
s32 es1869_trim_ppb(unsigned int item);
s64 es1869_clock_ppb(s64 rate);
void es1869_clock_init(struct es1869_clock *c);
void es1869_clock_stretch(struct es1869_clock *c);
bool es1869_clock_irq(struct es1869_clock *c, u32 ticks, u64 raw);
void es1869_clock_system(struct es1869_clock *c, u64 raw, u64 mono);
s32 es1869_clock_offset(const struct es1869_clock *c);

#endif /* ES1869_RESAMPLE_H */
