/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The ES1869's chip core: its port protocols, identification, rates, the
 * engines' register sequences, the interrupt and the DMA position. It uses
 * no kernel API, only struct es1869_io, so that essreg's tests build the
 * same file on the host and run it against their model of the chip.
 *
 * Notes:
 *
 * Every function here expects the caller to hold the driver's register
 * lock, with interrupts off, because the DSP takes whatever byte comes
 * next as the rest of a command, and the mixer's index and data, the PnP
 * index and the 8237's flip-flop are pairs.
 *
 * The facts behind the sequences are essreg's measurements of a card
 * (docs/ESS_EXTENDED.md, docs/AUDIO1.md, docs/AUDIO_PIPELINE.md) and ESS's
 * drivers as essreg rebuilt them.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Parts of the protocols come from essreg's src/esshw.c and src/essp.c,
 * which their author relicensed for this driver.
 */

#ifndef ES1869_HW_H
#define ES1869_HW_H

#include "hw_compat.h"
#include "regs.h"

// the measured clocks (docs/ESS_EXTENDED.md: the data sheet's 795.5 and
// 397.7 kHz came out at 793.8 and 396.9 kHz to the hertz)
#define ES_CLK_HI 793800 // A1h bit 7, 70h bit 7 clear
#define ES_CLK_LO 396900 // A1h bit 7 clear, in the old A1h mode
#define ES_CLK_768 768000 // 70h bit 7, or A1h in the new mode (71h bit 5)
#define ES_FM_RATE 49716 // the music DAC: 14.31818 MHz / 288

// FIFO depths: what the interrupt and the DMA position run ahead of the
// converter by
#define ES_FIFO_A1 256
#define ES_FIFO_A2 64

// Config_Base+6 and +7: the interrupt sources
#define ES_IRQ_A1 0x01
#define ES_IRQ_A2 0x02
#define ES_IRQ_HWV 0x04
#define ES_IRQ_MPU 0x08

// more than this many calls of the handler in a second is a storm: the
// line can't carry a real one (Audio 1's and Audio 2's shortest periods
// give about 1500 each)
#define ES_IRQ_STORM 20000

// 8237 modes, without the channel: memory to the chip (play) and back
#define ES_DMA_PLAY 0x18 // demand, auto-initialize, read
#define ES_DMA_REC 0x14 // demand, auto-initialize, write
#define ES_DMA_SINGLE 0x40 // or'ed in for single transfers

struct es1869_io {
	void *ctx;
	u8 (*inb)(void *ctx, u16 port);
	void (*outb)(void *ctx, u16 port, u8 val);
	void (*udelay)(void *ctx, unsigned int us);
	u64 (*now_ns)(void *ctx);
	// the 8237, with asm/dma.h's port order: program a channel and leave
	// it masked, mask or unmask it, and read what it still has to move,
	// in bytes, as get_dma_residue() counts them, twice over at once
	void (*dma_program)(void *ctx, unsigned int ch, u32 phys, u32 bytes,
			    u8 mode);
	void (*dma_mask)(void *ctx, unsigned int ch, bool masked);
	void (*dma_residue)(void *ctx, unsigned int ch, u32 *r);
};

struct es1869_stats {
	u32 irq[4]; // Audio 1, Audio 2, hardware volume, MPU
	u32 irq_none; // the line with no source of ours
	u32 dsp_busy; // a command refused: the DSP stayed busy
	u32 dsp_desync; // a command left half sent
	u32 pos_retries; // DMA position reads that disagreed
	u32 settles; // Audio 1 starts held masked
	u32 irq_stuck; // sources still asserted after their acknowledge
	u8 irq_dropped; // sources taken off the mask for that
	u32 pos_reads; // 8237 count readings, two to a position
	u32 status_miss; // a source polled pending that Config_Base+6 hid
};

struct es1869_hw {
	const struct es1869_io *io;
	u16 port; // Audio_Base
	u16 cfg; // Config_Base, 0 until found
	u16 fm; // FM_Base: the alias, or Audio_Base
	u16 mpu; // the MPU-401, 0 without one
	u8 mix[0x80]; // what the driver last wrote to the mixer
	u8 ctrl[0x20]; // and to the controller registers A0h-BFh
	u8 port7; // Audio_Base+7, which pulses bits 2 and 7
	u8 imr; // Config_Base+7: the sources in use
	u8 fm_delay; // status reads after an FM address or data write
	u8 stuck_run; // handler calls in a row that left a source asserted
	unsigned long storm_start; // the storm guard's second: its start
	unsigned int storm_count; // and the handler's calls in it
	bool storm; // a storm took every source off the line
	bool desync; // a DSP command was left half sent
	bool a1_monitor; // the record monitor goes on at the recording's start
	struct es1869_stats stats;
};

struct es1869_ident {
	u8 dsp[2]; // E7h's answer, 68h 8xh
	u8 mixer[2]; // mixer 40h's first two reads, 18h 69h
	u16 cfg; // Config_Base, from the next two
};

// a stream as the chip runs it
struct es1869_stream {
	u32 rate_num, rate_den; // the clock and its divisor
	u8 bits; // 8 or 16
	bool is_signed, stereo;
	u32 period; // bytes a period, as the chip counts them
	u8 ch; // the DMA channel
	u32 dma_phys, dma_bytes; // the buffer as the 8237 moves it
	bool single; // single transfers: 8237 single mode, B9h 0, 78h type 0
	u8 burst; // demand bursts: B9h (Audio 1) or 78h bits 7:6 (Audio 2)
	u16 corner; // the filter's corner, in thousandths of half the rate
	u8 a8; // A8h's bits beside its monitor (3) and channels (1:0)
	bool adc; // a recording: the codec turned to the ADC at prepare
};

// a copy of an application's buffer that the DMA reads, for es1869_fill_plan;
// frames, absolute but for the first line, which the stream sets
struct es1869_fill {
	unsigned long buffer, boundary, guard, lead, slack;
	unsigned long filled; // how far the copy has come
	unsigned long seen; // the application's pointer at the last plan
	bool standing; // that pointer stands, as under dmix
	bool backlog; // the budget left some of what was written
	u32 late; // the DMA got past the copy
};

// --- port protocols ---------------------------------------------------------

int es1869_dsp_reset(struct es1869_hw *hw);
int es1869_reset(struct es1869_hw *hw);
int es1869_dsp_cmd(struct es1869_hw *hw, const u8 *bytes, unsigned int n);
int es1869_dsp_read(struct es1869_hw *hw, unsigned int us);

u8 es1869_mix_read(struct es1869_hw *hw, u8 reg);
void es1869_mix_write(struct es1869_hw *hw, u8 reg, u8 val);
void es1869_mix_update(struct es1869_hw *hw, u8 reg, u8 mask, u8 val);

int es1869_ctrl_read(struct es1869_hw *hw, u8 reg);
int es1869_ctrl_write(struct es1869_hw *hw, u8 reg, u8 val);
int es1869_ctrl_update(struct es1869_hw *hw, u8 reg, u8 mask, u8 val);

u8 es1869_pnp_read(struct es1869_hw *hw, u8 ldn, u8 reg);
void es1869_pnp_write(struct es1869_hw *hw, u8 ldn, u8 reg, u8 val);
int es1869_ldn(struct es1869_hw *hw, u8 ldn);

void es1869_port7_write(struct es1869_hw *hw, u8 val);
void es1869_port7_pulse(struct es1869_hw *hw, u8 bit);

// --- identification and routing --------------------------------------------

int es1869_identify(struct es1869_hw *hw, struct es1869_ident *id);
void es1869_route(struct es1869_hw *hw, u8 irq, bool mpu);

// --- rates ------------------------------------------------------------------

int es1869_a1_rate(u32 num, u32 den, u8 *a1, bool *new_mode);
int es1869_a2_rate(u32 num, u32 den, u8 *r70);
u8 es1869_filter(u32 rate, u16 corner);
int es1869_best_rate(u32 rate, bool a1, u32 *num, u32 *den);

// --- engines ----------------------------------------------------------------

int es1869_a1_prepare(struct es1869_hw *hw, const struct es1869_stream *s,
		      bool capture, bool monitor);
int es1869_a1_go(struct es1869_hw *hw, bool capture);
int es1869_a1_dac(struct es1869_hw *hw, bool on);
u8 es1869_beep_level(u8 level, u8 m60, u8 m62);
int es1869_a1_stop(struct es1869_hw *hw, bool capture, u8 ch, bool adc);
int es1869_a1_pause(struct es1869_hw *hw, bool push);

int es1869_a2_prepare(struct es1869_hw *hw, const struct es1869_stream *s,
		      bool irq);
void es1869_a2_go(struct es1869_hw *hw, u8 burst, bool single);
void es1869_a2_stop(struct es1869_hw *hw, u8 ch);

void es1869_fifo_reset(struct es1869_hw *hw);

// --- the interrupt and the position -----------------------------------------

void es1869_power_down(struct es1869_hw *hw, bool full);
void es1869_power_wake(struct es1869_hw *hw, bool full);
void es1869_power_woken(struct es1869_hw *hw);
void es1869_mpu_uart(struct es1869_hw *hw);
u8 es1869_irq_take(struct es1869_hw *hw);
u8 es1869_irq_poll(struct es1869_hw *hw);
u8 es1869_irq_enter(struct es1869_hw *hw);
void es1869_irq_exit(struct es1869_hw *hw);
void es1869_irq_edge(struct es1869_hw *hw);
bool es1869_irq_storm(struct es1869_hw *hw, unsigned long now,
		      unsigned long hz);
u32 es1869_dma_pos(struct es1869_hw *hw, u8 ch, u32 bytes);
unsigned long es1869_forward(unsigned long *last, unsigned long off,
			     unsigned long size, unsigned long window);
unsigned long es1869_fill_plan(struct es1869_fill *f, unsigned long appl,
			       unsigned long hw, unsigned long budget,
			       unsigned long *start);

// each byte of src in the low byte of a word of dst, as a 16-bit channel
// gives the chip one byte a cycle
void es1869_spread(u8 *dst, const u8 *src, u32 n);
void es1869_split(u8 *a, u8 *w, const u8 *src, u32 n, u32 half);

// --- FM ---------------------------------------------------------------------

void es1869_fm_reset(struct es1869_hw *hw);
void es1869_fm_native(struct es1869_hw *hw);
void es1869_fm_write(struct es1869_hw *hw, u16 reg, u8 val);
u8 es1869_fm_read(struct es1869_hw *hw, u16 reg);

#endif /* ES1869_HW_H */
