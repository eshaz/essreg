// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The ES1869's chip core, see hw.h.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Parts of the protocols come from essreg's src/esshw.c and src/essp.c,
 * which their author relicensed for this driver.
 */

#include "hw.h"

#define DSP_DATA 0x0A // Audio_Base+Ah: read data
#define DSP_STATUS 0x0C // +Ch: bit 7 busy, bit 6 read data, 2:0 Audio 1 IRQ
#define DSP_ACK 0x0E // +Eh: any read acknowledges Audio 1

static u8 in(struct es1869_hw *hw, u16 port)
{
	return hw->io->inb(hw->io->ctx, port);
}

static void out(struct es1869_hw *hw, u16 port, u8 val)
{
	hw->io->outb(hw->io->ctx, port, val);
}

static u64 now(struct es1869_hw *hw)
{
	return hw->io->now_ns(hw->io->ctx);
}

// --- port protocols ---------------------------------------------------------

// poll Audio_Base+Ch until (status & mask) == want, at most us. The clock is
// read before the status, so that a stall between the two, an SMI's, still
// gets a reading; the reads are counted too, about a microsecond each,
// because a PIT clock stands still with interrupts off
static int dsp_poll(struct es1869_hw *hw, u8 mask, u8 want, unsigned int us)
{
	u64 end = now(hw) + (u64)us * 1000;
	unsigned int reads;

	for (reads = 4 * us; reads; reads--) {
		bool late = now(hw) > end;

		if ((in(hw, hw->port + DSP_STATUS) & mask) == want)
			return 0;
		if (late)
			break;
	}
	return -ETIMEDOUT;
}

int es1869_dsp_read(struct es1869_hw *hw, unsigned int us)
{
	// Audio_Base+Ch bit 6, since a read of +Eh acknowledges Audio 1
	if (dsp_poll(hw, 0x40, 0x40, us))
		return -ETIMEDOUT;
	return in(hw, hw->port + DSP_DATA);
}

int es1869_dsp_cmd(struct es1869_hw *hw, const u8 *bytes, unsigned int n)
{
	unsigned int i;

	if (hw->desync)
		return -EIO;
	for (i = 0; i < n; i++) {
		// the first byte waits up to 1 ms; once the DSP has it, the next
		// byte it gets is the rest of the command, whoever sends it, so
		// the operands wait longer and a timeout leaves it out of step
		if (dsp_poll(hw, 0x80, 0, i ? 2000 : 1000)) {
			if (!i) {
				hw->stats.dsp_busy++;
				return -EBUSY;
			}
			hw->desync = true;
			hw->stats.dsp_desync++;
			return -EIO;
		}
		out(hw, hw->port + DSP_STATUS, bytes[i]);
	}
	return 0;
}

// the controller registers after a reset, as a card read them back (rst.ext
// in docs/ESS_EXTENDED.md); B1h and B2h lose bit 6
static const u8 reset_ctrl[][2] = {
	{ 0xA1, 0xDC }, { 0xA2, 0xE7 }, { 0xA4, 0x00 }, { 0xA5, 0xF8 },
	{ 0xA8, 0x02 }, { 0xB6, 0x80 }, { 0xB7, 0x44 }, { 0xB8, 0x00 },
	{ 0xB9, 0x00 },
};

int es1869_dsp_reset(struct es1869_hw *hw)
{
	unsigned int i;
	int v = -ENODEV;

	// +6 = 3 also empties the FIFO; the reads hold it a few microseconds
	out(hw, hw->port + 6, 3);
	for (i = 0; i < 3; i++)
		in(hw, hw->port + 6);
	out(hw, hw->port + 6, 0);
	// AAh within 2 ms, after at most a few bytes left from before
	for (i = 0; i < 5; i++) {
		v = es1869_dsp_read(hw, 2000);
		if (v < 0 || v == 0xAA)
			break;
	}
	if (v != 0xAA)
		return -ENODEV;
	hw->desync = false;
	for (i = 0; i < ARRAY_SIZE(reset_ctrl); i++)
		hw->ctrl[reset_ctrl[i][0] - 0xA0] = reset_ctrl[i][1];
	hw->ctrl[0xB1 - 0xA0] &= ~0x40;
	hw->ctrl[0xB2 - 0xA0] &= ~0x40;
	return 0;
}

int es1869_reset(struct es1869_hw *hw)
{
	static const u8 ext = 0xC6;
	int err = es1869_dsp_reset(hw);

	// a reset leaves the Extended mode, which the controller registers
	// need (DS p.47)
	return err ? err : es1869_dsp_cmd(hw, &ext, 1);
}

u8 es1869_mix_read(struct es1869_hw *hw, u8 reg)
{
	out(hw, hw->port + 4, reg);
	return in(hw, hw->port + 5);
}

void es1869_mix_write(struct es1869_hw *hw, u8 reg, u8 val)
{
	out(hw, hw->port + 4, reg);
	out(hw, hw->port + 5, val);
	if (reg < ARRAY_SIZE(hw->mix))
		hw->mix[reg] = val;
}

// the bits of mask from val, the others as last written; written only when
// that changes the register
void es1869_mix_update(struct es1869_hw *hw, u8 reg, u8 mask, u8 val)
{
	u8 v = (hw->mix[reg] & ~mask) | (val & mask);

	if (v != hw->mix[reg])
		es1869_mix_write(hw, reg, v);
}

int es1869_ctrl_read(struct es1869_hw *hw, u8 reg)
{
	u8 cmd[2] = { 0xC0, reg };
	int err = es1869_dsp_cmd(hw, cmd, 2);

	return err ? err : es1869_dsp_read(hw, 2000);
}

int es1869_ctrl_write(struct es1869_hw *hw, u8 reg, u8 val)
{
	u8 cmd[2] = { reg, val };
	int err = es1869_dsp_cmd(hw, cmd, 2);

	if (!err)
		hw->ctrl[reg - 0xA0] = val;
	return err;
}

int es1869_ctrl_update(struct es1869_hw *hw, u8 reg, u8 mask, u8 val)
{
	u8 old = hw->ctrl[reg - 0xA0];
	u8 v = (old & ~mask) | (val & mask);

	return v == old ? 0 : es1869_ctrl_write(hw, reg, v);
}

// a PnP register through Config_Base+0/+1 (ldn FFh: the card's), the index
// and the logical device put back after it
static u8 pnp_access(struct es1869_hw *hw, u8 ldn, u8 reg, bool write,
		     u8 val)
{
	u16 cfg = hw->cfg;
	u8 old_index, old_ldn = 0, ret = 0;

	if (!cfg)
		return 0xFF;
	old_index = in(hw, cfg);
	if (ldn != 0xFF) {
		out(hw, cfg, 0x07);
		old_ldn = in(hw, cfg + 1);
		out(hw, cfg + 1, ldn);
	}
	out(hw, cfg, reg);
	if (write)
		out(hw, cfg + 1, val);
	else
		ret = in(hw, cfg + 1);
	if (ldn != 0xFF) {
		out(hw, cfg, 0x07);
		out(hw, cfg + 1, old_ldn);
	}
	out(hw, cfg, old_index);
	return ret;
}

u8 es1869_pnp_read(struct es1869_hw *hw, u8 ldn, u8 reg)
{
	return pnp_access(hw, ldn, reg, false, 0);
}

void es1869_pnp_write(struct es1869_hw *hw, u8 ldn, u8 reg, u8 val)
{
	pnp_access(hw, ldn, reg, true, val);
}

// the number of an optional logical device (ES_LDN_*): they follow LDN 2
// in the order card register 25h lists them, MPU-401 (bit 0), CD-ROM
// (bit 1), modem (bit 2) and general purpose (bits 4:3); -ENODEV if the
// card has none
int es1869_ldn(struct es1869_hw *hw, u8 ldn)
{
	u8 have = es1869_pnp_read(hw, 0xFF, 0x25);
	u8 n = 3, i;

	if (ldn < ES_LDN_MPU)
		return ldn;
	for (i = 0; i < 4; i++) {
		bool present = i < 3 ? have & BIT(i) : have & 0x18;

		if (ES_LDN_MPU + i == ldn)
			return present ? n : -ENODEV;
		if (present)
			n++;
	}
	return -ENODEV;
}

// Audio_Base+7 isn't read back: bits 2 and 7 are pulses, so the shadow
// keeps them clear
void es1869_port7_write(struct es1869_hw *hw, u8 val)
{
	hw->port7 = val & ~0x84;
	out(hw, hw->port + 7, hw->port7);
}

void es1869_port7_pulse(struct es1869_hw *hw, u8 bit)
{
	out(hw, hw->port + 7, hw->port7 | bit);
	out(hw, hw->port + 7, hw->port7);
}

// --- identification ---------------------------------------------------------

int es1869_identify(struct es1869_hw *hw, struct es1869_ident *id)
{
	static const u8 e7 = 0xE7;
	unsigned int i;
	u8 hi, lo;
	int v;

	// no chip answers the reset within 2 ms
	if (es1869_dsp_reset(hw))
		return -ENODEV;
	// E7h: 68h and 8xh from ESS's chips; other Sound Blaster
	// compatibles, QEMU's sb16 among them, give nothing
	if (es1869_dsp_cmd(hw, &e7, 1))
		return -ENODEV;
	for (i = 0; i < 2; i++) {
		v = es1869_dsp_read(hw, 2000);
		if (v < 0)
			return -ENODEV;
		id->dsp[i] = v;
	}
	if (id->dsp[0] != 0x68 || (id->dsp[1] & 0xF0) != 0x80)
		return -ENODEV;
	// mixer 40h: 18h, 69h, then Config_Base, each read moving on
	out(hw, hw->port + 4, 0x40);
	id->mixer[0] = in(hw, hw->port + 5);
	id->mixer[1] = in(hw, hw->port + 5);
	hi = in(hw, hw->port + 5);
	lo = in(hw, hw->port + 5);
	if (id->mixer[0] != 0x18 || id->mixer[1] != 0x69)
		return -ENODEV;
	// Config_Base is 100h to FF8h and 8-aligned (DS p.28)
	id->cfg = (hi << 8) | lo;
	if (id->cfg < 0x100 || id->cfg > 0xFF8 || (id->cfg & 7))
		return -ENODEV;
	return 0;
}

// every source on the audio line: the buttons' and the MPU-401's (card
// registers 27h, 28h) and Audio 2's (LDN 1 72h). Card register 26h bits 3:2
// let Audio 2's and Audio 1's DMA requests out: the data sheet calls them
// masks (DS p.15, p.31), but on the Armada with them clear neither engine's
// request reached the 8237, and set, both did; bit 4, the external
// device's, stays as found
void es1869_route(struct es1869_hw *hw, u8 irq, bool mpu)
{
	es1869_pnp_write(hw, 0xFF, 0x27, irq);
	if (mpu)
		es1869_pnp_write(hw, 0xFF, 0x28, irq);
	es1869_pnp_write(hw, 1, 0x72, irq);
	es1869_pnp_write(hw, 0xFF, 0x26,
			 es1869_pnp_read(hw, 0xFF, 0x26) | 0x0C);
}

// --- rates ------------------------------------------------------------------

// Audio 1's A1h for a clock and divisor: 793.8 kHz with bit 7 (256 - d),
// 396.9 kHz without it, and 768 kHz only in the new mode of mixer 71h bit
// 5, where A1h works like 70h
int es1869_a1_rate(u32 num, u32 den, u8 *a1, bool *new_mode)
{
	if (den < 1 || den > 128)
		return -EINVAL;
	switch (num) {
	case ES_CLK_HI:
		*a1 = 256 - den;
		*new_mode = false;
		return 0;
	case ES_CLK_LO:
		*a1 = 128 - den;
		*new_mode = false;
		return 0;
	case ES_CLK_768:
		*a1 = 0x80 | (128 - den);
		*new_mode = true;
		return 0;
	}
	return -EINVAL;
}

// Audio 2's 70h: bit 7 picks 768 kHz, bits 6:0 are 128 - d
int es1869_a2_rate(u32 num, u32 den, u8 *r70)
{
	if (den < 1 || den > 128)
		return -EINVAL;
	if (num == ES_CLK_HI)
		*r70 = (128 - den) & 0x7F;
	else if (num == ES_CLK_768)
		*r70 = 0x80 | ((128 - den) & 0x7F);
	else
		return -EINVAL;
	return 0;
}

// A2h or 72h: 256 minus the divisor of 7.16 MHz that puts the
// switched-capacitor filter's corner, its clock / 82, at corner/1000 of
// half the rate; ESS writes FDh from 44.1 kHz up
u8 es1869_filter(u32 rate, u16 corner)
{
	u64 num = (u64)7160000 * 2000;
	u32 den;
	u32 div;

	if (!rate || !corner || rate >= 44100)
		return 0xFD;
	den = 82 * corner * rate;
	div = div_u64(num + den / 2, den);
	div = clamp(div, 3U, 255U);
	return 256 - div;
}

// the clock and divisor nearest to a rate, for a rate that came without
// them: 793.8 kHz on a tie
int es1869_best_rate(u32 rate, bool a1, u32 *num, u32 *den)
{
	static const u32 clocks[] = { ES_CLK_HI, ES_CLK_768, ES_CLK_LO };
	u32 best_err = ~0U;
	unsigned int i;

	if (!rate)
		return -EINVAL;
	for (i = 0; i < (a1 ? 3U : 2U); i++) {
		u32 d = clamp(DIV_ROUND_CLOSEST(clocks[i], rate), 1U, 128U);
		u32 got = clocks[i] / d;
		u32 err = got > rate ? got - rate : rate - got;

		if (err < best_err) {
			best_err = err;
			*num = clocks[i];
			*den = d;
		}
	}
	return 0;
}

// --- engines ----------------------------------------------------------------

// Audio 1's FIFO emptied (+6 bit 1, DS p.48), 2 then 0 as ESS's drivers
// write it
void es1869_fifo_reset(struct es1869_hw *hw)
{
	out(hw, hw->port + 6, 2);
	out(hw, hw->port + 6, 0);
}

static void dma_mask(struct es1869_hw *hw, u8 ch, bool masked)
{
	hw->io->dma_mask(hw->io->ctx, ch, masked);
}

int es1869_a1_prepare(struct es1869_hw *hw, const struct es1869_stream *s,
		      bool capture, bool monitor)
{
	u8 regs[12][2];
	unsigned int i, n = 0;
	bool new_mode;
	u8 a1, b7, mode;
	u32 rate;
	u16 count;
	int err;

	if (!s->period || s->period > 0x10000 || !s->rate_den)
		return -EINVAL;
	err = es1869_a1_rate(s->rate_num, s->rate_den, &a1, &new_mode);
	if (err)
		return err;
	rate = s->rate_num / s->rate_den;
	count = (u16)(0x10000 - s->period);

	// stopped as ESS's drivers leave it, B8h & 30h; playback then takes
	// the DAC direction with auto-initialize (04h), as ESS's player does,
	// and a recording takes the ADC's in its start, in one write, or with
	// adc the codec's turn to the ADC (08h) now, as the data sheet's steps
	// begin, so that its input settles before the start (DS p.50)
	err = es1869_ctrl_write(hw, 0xB8, (hw->ctrl[0x18] & 0x30) |
					  (capture ? (s->adc ? 0x08 : 0) : 0x04));
	if (err)
		return err;
	dma_mask(hw, s->ch, true);
	es1869_fifo_reset(hw);
	es1869_mix_update(hw, 0x71, 0x20, new_mode ? 0x20 : 0);

	// A8h bits 1:0, 01 stereo and 10 mono; bit 3 the record monitor,
	// which puts the record source on AOUT in place of the playback mix
	// while the ADC records (DS p.49; without it the playback mix goes
	// on), and which waits for the start while the ADC's input settles
	// (DS p.50, steps 7 and 8); the others from the stream: ESS's
	// recording writes F5h or F6h
	hw->a1_monitor = monitor && s->adc;
	regs[n][0] = 0xA8;
	regs[n++][1] = (s->a8 & 0xF4) | (s->stereo ? 1 : 2) |
		       (monitor && !s->adc ? 0x08 : 0);
	// B9h: demand transfers of 2 bytes, as ESS's drivers use them; 11b
	// runs the DMA at twice the rate (a1.b9_3)
	regs[n][0] = 0xB9;
	regs[n++][1] = s->single ? 0 : min_t(u8, s->burst, 2);
	regs[n][0] = 0xA1;
	regs[n++][1] = a1;
	regs[n][0] = 0xA2;
	regs[n++][1] = es1869_filter(rate, s->corner);
	regs[n][0] = 0xA4;
	regs[n++][1] = count & 0xFF;
	regs[n][0] = 0xA5;
	regs[n++][1] = count >> 8;
	// the format: B6h for playback, and B7h twice, the first of which
	// initializes the DAC and prevents pops (DS p.49 Table 16, p.50
	// Table 17); bit 6 is the opposite of bit 3
	b7 = 0xD0;
	if (s->is_signed)
		b7 ^= 0x20;
	if (s->bits == 16)
		b7 ^= 0x04;
	if (s->stereo)
		b7 ^= 0x48;
	if (!capture) {
		regs[n][0] = 0xB6;
		regs[n++][1] = s->is_signed ? 0x00 : 0x80;
	}
	regs[n][0] = 0xB7;
	regs[n++][1] = s->is_signed ? 0x71 : 0x51;
	regs[n][0] = 0xB7;
	regs[n++][1] = b7;
	for (i = 0; i < n; i++) {
		err = es1869_ctrl_write(hw, regs[i][0], regs[i][1]);
		if (err)
			return err;
	}
	// the counter's interrupt and the DMA requests, with bit 4 as the
	// programming steps set it
	err = es1869_ctrl_update(hw, 0xB1, 0xF0, 0x50);
	if (!err)
		err = es1869_ctrl_update(hw, 0xB2, 0xF0, 0x50);
	if (err)
		return err;
	mode = capture ? ES_DMA_REC : ES_DMA_PLAY;
	if (s->single)
		mode |= ES_DMA_SINGLE;
	hw->io->dma_program(hw->io->ctx, s->ch, s->dma_phys, s->dma_bytes,
			    mode);
	return 0;
}

// the start: a recording's channel unmasked first; playback's stays masked
// until the caller unmasks it after the settle, because the chip loses the
// first byte when the DMA brings it right after the start's first request
// (fix.late0 to fix.late20 in docs/ESS_EXTENDED.md)
int es1869_a1_go(struct es1869_hw *hw, bool capture)
{
	int err;

	if (!capture)
		hw->stats.settles++;
	else if (hw->a1_monitor) {
		err = es1869_ctrl_update(hw, 0xA8, 0x08, 0x08);
		if (err)
			return err;
	}
	return es1869_ctrl_write(hw, 0xB8, (hw->ctrl[0x18] & 0x30) |
					  (capture ? 0x0F : 0x05));
}

// the stop as ESS's drivers make it: auto-initialize off (B8h bit 2),
// then B8h & 30h, or with adc the codec left on the ADC (08h), so that a
// recording's next start finds its input settled
int es1869_a1_stop(struct es1869_hw *hw, bool capture, u8 ch, bool adc)
{
	int err = es1869_ctrl_update(hw, 0xB8, 0x04, 0);

	if (!err)
		err = es1869_ctrl_write(hw, 0xB8, (hw->ctrl[0x18] & 0x30) |
						  (adc ? 0x08 : 0));
	dma_mask(hw, ch, true);
	if (!capture)
		es1869_fifo_reset(hw);
	return err;
}

// a pause: the FIFO off the converter (B7h bit 7), then the DMA requests
// off (B2h bit 6), and back in the reverse order, which resumed in step 12
// times in 12; D0h doesn't pause the Extended mode
int es1869_a1_pause(struct es1869_hw *hw, bool push)
{
	int err;

	if (push) {
		err = es1869_ctrl_update(hw, 0xB7, 0x80, 0x00);
		return err ? err : es1869_ctrl_update(hw, 0xB2, 0x40, 0x00);
	}
	err = es1869_ctrl_update(hw, 0xB2, 0x40, 0x40);
	return err ? err : es1869_ctrl_update(hw, 0xB7, 0x80, 0x80);
}

// Audio 1's DAC into the mixer (D1h) or out of it (D3h)
int es1869_a1_dac(struct es1869_hw *hw, bool on)
{
	u8 cmd = on ? 0xD1 : 0xD3;

	return es1869_dsp_cmd(hw, &cmd, 1);
}

int es1869_a2_prepare(struct es1869_hw *hw, const struct es1869_stream *s,
		      bool irq)
{
	u16 count;
	u8 r70, r7a, mode;

	int err;

	if (!s->period || s->period > 0x10000 || !s->rate_den)
		return -EINVAL;
	err = es1869_a2_rate(s->rate_num, s->rate_den, &r70);
	if (err)
		return err;
	count = (u16)(0x10000 - s->period);

	es1869_mix_write(hw, 0x78, 0x00);
	dma_mask(hw, s->ch, true);
	es1869_mix_write(hw, 0x70, r70);
	es1869_mix_write(hw, 0x72,
			 es1869_filter(s->rate_num / s->rate_den, s->corner));
	es1869_mix_write(hw, 0x74, count & 0xFF);
	es1869_mix_write(hw, 0x76, count >> 8);
	// 7Ah bit 7 is the interrupt's latch, which a 1 would set
	r7a = (irq ? 0x40 : 0) | (s->is_signed ? 0x04 : 0) |
	      (s->stereo ? 0x02 : 0) | (s->bits == 16 ? 0x01 : 0);
	es1869_mix_write(hw, 0x7A, r7a);
	mode = ES_DMA_PLAY | (s->single ? ES_DMA_SINGLE : 0);
	hw->io->dma_program(hw->io->ctx, s->ch, s->dma_phys, s->dma_bytes,
			    mode);
	// the chip asks for nothing until 78h bit 1
	dma_mask(hw, s->ch, false);
	return 0;
}

// one write with bits 1:0 together: es1938's two-step start lost 1-3
// bytes (a2.start2); bit 5 stays clear, since with it the chip counts
// words and plays nothing usable on a 16-bit channel (a2.wide)
void es1869_a2_go(struct es1869_hw *hw, u8 burst, bool single)
{
	es1869_mix_write(hw, 0x78,
			 ((single ? 0 : burst & 3) << 6) | 0x10 | 0x03);
}

void es1869_a2_stop(struct es1869_hw *hw, u8 ch)
{
	es1869_mix_write(hw, 0x78, 0x00);
	es1869_mix_write(hw, 0x7A, hw->mix[0x7A] & 0x7F);
	dma_mask(hw, ch, true);
}

// --- the interrupt and the position -----------------------------------------

// the sources Config_Base+6 shows, taken back where they live: one read
// finds them (on the Armada it never missed one that polling found), Audio
// 2's latch is cleared from the shadow, and the MPU-401's byte the caller
// reads
u8 es1869_irq_take(struct es1869_hw *hw)
{
	u8 st = in(hw, hw->cfg + 6) & hw->imr & 0x0F;
	unsigned int i;

	if (st & ES_IRQ_A2)
		es1869_mix_write(hw, 0x7A, hw->mix[0x7A] & 0x7F);
	if (st & ES_IRQ_A1)
		in(hw, hw->port + DSP_ACK);
	if (st & ES_IRQ_HWV) {
		es1869_mix_write(hw, 0x66, 0x00);
		for (i = 0x60; i <= 0x63; i++)
			hw->mix[i] = es1869_mix_read(hw, i);
	}
	for (i = 0; i < 4; i++)
		if (st & BIT(i))
			hw->stats.irq[i]++;
	return st;
}

// the sources pending, found where they live and each acknowledged, as
// ESS's drivers do it (ES1869.DRV 3:01B9): the MPU-401's data, 7Ah bit 7
// (written 0), +Ch bits 2:0 (a read of +Eh), and 64h bit 4 (a write to
// 66h); the MPU-401's byte the caller reads
u8 es1869_irq_poll(struct es1869_hw *hw)
{
	unsigned int i;
	u8 st = 0, v, status;

	// Config_Base+6 as well, to learn whether it shows every source that
	// polling finds (stats.status_miss), so that one read could do
	status = in(hw, hw->cfg + 6);
	if ((hw->imr & ES_IRQ_MPU) && hw->mpu &&
	    !(in(hw, hw->mpu + 1) & 0x80))
		st |= ES_IRQ_MPU;
	if (hw->imr & ES_IRQ_A2) {
		v = es1869_mix_read(hw, 0x7A);
		if (v & 0x80) {
			es1869_mix_write(hw, 0x7A, v & 0x7F);
			st |= ES_IRQ_A2;
		}
	}
	if ((hw->imr & ES_IRQ_A1) && (in(hw, hw->port + DSP_STATUS) & 0x07)) {
		in(hw, hw->port + DSP_ACK);
		st |= ES_IRQ_A1;
	}
	if ((hw->imr & ES_IRQ_HWV) && (es1869_mix_read(hw, 0x64) & 0x10)) {
		es1869_mix_write(hw, 0x66, 0x00);
		for (i = 0x60; i <= 0x63; i++)
			hw->mix[i] = es1869_mix_read(hw, i);
		st |= ES_IRQ_HWV;
	}
	for (i = 0; i < 4; i++)
		if (st & BIT(i))
			hw->stats.irq[i]++;
	if (st & ~status)
		hw->stats.status_miss++;
	return st;
}

u8 es1869_irq_enter(struct es1869_hw *hw)
{
	unsigned int i;
	u8 st, stuck;

	// the line drops with the mask cleared, and the status reads before
	// the mask (DS p.18)
	out(hw, hw->cfg + 7, 0);
	st = in(hw, hw->cfg + 6) & hw->imr;
	// Audio 1's request is always taken back: one left standing is the
	// last interrupt it gives (a1.noack)
	if (st & ES_IRQ_A1)
		in(hw, hw->port + DSP_ACK);
	if (st & ES_IRQ_A2)
		es1869_mix_write(hw, 0x7A, hw->mix[0x7A] & 0x7F);
	if (st & ES_IRQ_HWV) {
		es1869_mix_write(hw, 0x66, 0x00);
		for (i = 0x60; i <= 0x63; i++)
			hw->mix[i] = es1869_mix_read(hw, i);
	}
	for (i = 0; i < 4; i++)
		if (st & BIT(i))
			hw->stats.irq[i]++;
	if (!st)
		hw->stats.irq_none++;
	// a source still asserted after its acknowledge raises the line again
	// at every restore of the mask, with nothing to stop it: after 64 such
	// calls in a row it comes off the mask (the MPU-401's is taken back
	// later, by reading its data)
	stuck = st ? in(hw, hw->cfg + 6) & st & ~ES_IRQ_MPU : 0;
	if (!stuck) {
		hw->stuck_run = 0;
	} else if (++hw->stuck_run >= 64) {
		hw->imr &= ~stuck;
		hw->stats.irq_dropped |= stuck;
		hw->stats.irq_stuck++;
		hw->stuck_run = 0;
	}
	return st;
}

// a source still pending raises the line again: a new edge
void es1869_irq_exit(struct es1869_hw *hw)
{
	out(hw, hw->cfg + 7, hw->imr);
}

// the line dropped and raised: a source pending from before, or left by the
// polled handler's last pass, holds it high, where no other source's edge
// can show, so it gets one of its own
void es1869_irq_edge(struct es1869_hw *hw)
{
	out(hw, hw->cfg + 7, 0);
	out(hw, hw->cfg + 7, hw->imr);
}

// the storm guard, at each call of the handler, on the caller's clock of hz
// ticks a second: true for the call that makes its second a storm, which
// takes every source off the line until the driver loads again. The second
// is its start and an unsigned difference, so that a start of 0 at load,
// or one long past, opens a new one: time_after() against 0 sees none in a
// 32-bit boot's first five minutes (INITIAL_JIFFIES is -300 s), and the
// calls since the boot added up to a storm
bool es1869_irq_storm(struct es1869_hw *hw, unsigned long now,
		      unsigned long hz)
{
	if (now - hw->storm_start >= hz) {
		hw->storm_start = now;
		hw->storm_count = 0;
	}
	if (hw->storm || ++hw->storm_count <= ES_IRQ_STORM)
		return false;
	hw->storm = true;
	hw->imr = 0;
	return true;
}

// what the DMA has moved of a buffer of bytes: the 8237's count read until
// two reads agree within a cycle, the earlier one kept; never masked, which
// would stall a demand transfer in front of a FIFO of a third of a ms
u32 es1869_dma_pos(struct es1869_hw *hw, u8 ch, u32 bytes)
{
	// two readings within 32 transfers agree: at 99 kHz on a word channel
	// the count moves every 2.5 us, about as long as a reading takes,
	// while a reading the low byte's wrap tore is 256 transfers off
	u32 tol = 32 * (ch >= 4 ? 2 : 1);
	u32 r[2] = { 0, 0 };
	unsigned int i;

	for (i = 0; i < 4; i++) {
		hw->io->dma_residue(hw->io->ctx, ch, r);
		hw->stats.pos_reads += 2;
		if ((r[0] > r[1] ? r[0] - r[1] : r[1] - r[0]) <= tol) {
			r[0] = max(r[0], r[1]);
			break;
		}
		hw->stats.pos_retries++;
	}
	if (!r[0] || r[0] > bytes)
		return 0;
	return bytes - r[0];
}

// a DMA offset in frames that never steps back: a reading behind the last
// one by at most window frames was read late, or follows a release or a
// drain's start, and ALSA would take the step back for a wrap and add a
// buffer. Anything further back is a step forward past the wrap, which
// between two interrupts of a two-period stream is over half the buffer
unsigned long es1869_forward(unsigned long *last, unsigned long off,
			     unsigned long size, unsigned long window)
{
	if (off != *last &&
	    (*last + size - off) % size <= min(window, size / 4))
		return *last;
	*last = off;
	return off;
}

// what to copy next of an application's buffer into what the DMA reads, in
// absolute frames, to appl from hw; *start and the frames, 0 for nothing.
//
// An application that moves its pointer gets what it wrote, once: the DMA
// past the copy is a late fill, and a rewind takes the copy back with the
// pointer. Right after a release the DMA may be up to slack frames behind
// the pointer, and the newest buffer of what was written wins.
//
// A pointer that stands, as under dmix, which mixes straight into the
// buffer and never moves it (it is 0 while hw runs to the boundary), gets
// the frames from just ahead of the DMA to the lead, again each time. What
// tells the two apart is whether the pointer moved, not how far it is
// from the DMA: a standing one comes near hw as hw nears the boundary.
// When a standing pointer moves, the writes begin where it stood.
unsigned long es1869_fill_plan(struct es1869_fill *f, unsigned long appl,
			       unsigned long hw, unsigned long budget,
			       unsigned long *start)
{
	unsigned long b = f->buffer, bound = f->boundary, end, frames;
	unsigned long ahead = (appl + bound - hw) % bound;

	f->backlog = false;
	if (appl != f->seen) {
		if (f->standing)
			f->filled = f->seen;
		f->standing = false;
	} else if (!f->standing && (!ahead || ahead > b + f->slack)) {
		f->standing = true;
	}
	f->seen = appl;
	if (f->standing) {
		*start = (hw + f->guard) % bound;
		end = (hw + f->lead) % bound;
	} else {
		if ((f->filled + bound - hw) % bound > b) {
			f->late++;
			f->filled = hw;
		}
		if ((f->filled + bound - hw) % bound > ahead)
			f->filled = appl;
		*start = f->filled;
		end = appl;
		if ((end + bound - *start) % bound > b)
			*start = (end + bound - b) % bound;
	}
	frames = (end + bound - *start) % bound;
	if (frames > b)
		return 0;
	if (frames > budget) {
		frames = budget;
		// a standing pointer's window starts again at each fill, so
		// that nothing is left behind
		f->backlog = !f->standing;
	}
	if (frames)
		f->filled = (*start + frames) % bound;
	return frames;
}

// two stream bytes to a 32-bit store, in the low bytes of their words (the
// ISA DMA is little-endian, as the machines it is on)
void es1869_spread(u8 *dst, const u8 *src, u32 n)
{
	u32 i, w;

	for (i = 0; i + 2 <= n; i += 2) {
		w = src[i] | (u32)src[i + 1] << 16;
		memcpy(dst + 2 * i, &w, 4);
	}
	if (i < n) {
		dst[2 * i] = src[i];
		dst[2 * i + 1] = 0;
	}
}

// n frames of two halves of half bytes: the first half to a, the second in
// the low bytes of the words at w
void es1869_split(u8 *a, u8 *w, const u8 *src, u32 n, u32 half)
{
	u32 j;

	for (; n; n--) {
		for (j = 0; j < half; j++)
			*a++ = *src++;
		for (j = 0; j < half; j++) {
			*w++ = *src++;
			*w++ = 0;
		}
	}
}

// --- power ------------------------------------------------------------------

// a power-down request (DS p.75): Audio_Base+7 bit 3 set for a partial one,
// in which the mixer and the analog inputs go on, clear for a full one;
// then bit 2 pulsed
void es1869_power_down(struct es1869_hw *hw, bool full)
{
	if (full)
		hw->port7 &= ~0x08;
	else
		hw->port7 |= 0x08;
	out(hw, hw->port + 7, hw->port7 | 0x04);
	out(hw, hw->port + 7, hw->port7);
}

// out of a power-down. From a partial one, any audio access but
// Audio_Base+4h to +7h: the DSP's status. From a full one, which a crystal
// doesn't leave by itself (DS p.74), Audio_Base+7 bit 3 high starts the
// oscillator, for 25 ms (DS p.76), and the reset is held as ESS's VxD holds
// it for 100 ms, until es1869_power_woken; AOUT comes back in that time
void es1869_power_wake(struct es1869_hw *hw, bool full)
{
	if (!full) {
		in(hw, hw->port + DSP_STATUS);
		return;
	}
	hw->port7 |= 0x08;
	out(hw, hw->port + 7, hw->port7);
	out(hw, hw->port + 6, 1);
}

void es1869_power_woken(struct es1869_hw *hw)
{
	out(hw, hw->port + 6, 0);
}

// --- the MPU-401 ------------------------------------------------------------

// an MPU-401 that a power loss took out of its UART mode: its reset, then
// the mode, with the answer of each read, as ESSMPU.DRV does at a resume
void es1869_mpu_uart(struct es1869_hw *hw)
{
	static const u8 cmd[2] = { 0xFF, 0x3F };
	unsigned int i, n;

	if (!hw->mpu)
		return;
	for (i = 0; i < 2; i++) {
		for (n = 0; n < 200 && (in(hw, hw->mpu + 1) & 0x40); n++)
			hw->io->udelay(hw->io->ctx, 5);
		out(hw, hw->mpu + 1, cmd[i]);
		for (n = 0; n < 200; n++) {
			if (!(in(hw, hw->mpu + 1) & 0x80)) {
				in(hw, hw->mpu);
				break;
			}
			hw->io->udelay(hw->io->ctx, 5);
		}
	}
}

// --- FM ---------------------------------------------------------------------

// the synthesizer held in reset by Audio_Base+7 bit 5, which also brings it
// back to the OPL3 emulation mode, with its level at 0 meanwhile, as ESS's
// VxD does it, so that the reset makes no click
void es1869_fm_reset(struct es1869_hw *hw)
{
	out(hw, hw->port + 4, 0x36);
	out(hw, hw->port + 5, 0);
	out(hw, hw->port + 7, hw->port7 | 0x20);
	hw->io->udelay(hw->io->ctx, 25);
	out(hw, hw->port + 7, hw->port7 & ~0x20);
	out(hw, hw->port + 5, hw->mix[0x36]);
}

static void fm_wait(struct es1869_hw *hw, unsigned int reads)
{
	while (reads--)
		in(hw, hw->fm);
}

// the native mode: register 105h bit 7, written in the emulation mode as
// the high bank's register 05h, paced as ESFM.DRV paces its writes
void es1869_fm_native(struct es1869_hw *hw)
{
	fm_wait(hw, 6);
	out(hw, hw->fm + 2, 0x05);
	fm_wait(hw, 2);
	out(hw, hw->fm + 3, 0x80);
	fm_wait(hw, 6);
}

// a native register: +2 its bits 7:0, +3 its bits 15:8, +1 the data, with
// status reads between them as ESFM.DRV paces them
void es1869_fm_write(struct es1869_hw *hw, u16 reg, u8 val)
{
	out(hw, hw->fm + 2, reg & 0xFF);
	fm_wait(hw, 2);
	out(hw, hw->fm + 3, reg >> 8);
	fm_wait(hw, hw->fm_delay);
	out(hw, hw->fm + 1, val);
	fm_wait(hw, hw->fm_delay);
}

u8 es1869_fm_read(struct es1869_hw *hw, u16 reg)
{
	out(hw, hw->fm + 2, reg & 0xFF);
	fm_wait(hw, 2);
	out(hw, hw->fm + 3, reg >> 8);
	fm_wait(hw, hw->fm_delay);
	return in(hw, hw->fm + 1);
}
