// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the ESS ES1869 AudioDrive, with every feature of the chip.
 *
 * Notes:
 *
 * The card is found three ways: as a PnP BIOS or ACPI device (ESS1869),
 * as an ISA PnP card, or at the ports the parameters give (isapnp=0). In
 * that last case the resources not given come from the chip's own PnP
 * registers. The probe identifies the chip before it takes an interrupt or
 * a DMA channel, so that a machine without one (QEMU) fails in
 * microseconds: no answer to the DSP reset, or no ESS answer to E7h.
 *
 * One edge-triggered line carries Audio 1, Audio 2, the hardware volume
 * buttons and the MPU-401. The handler takes each source back where it
 * lives until a pass finds none, as ESS's drivers do, since a source that
 * comes while another holds the line high raises no edge of its own. With
 * irq_mode=1 it clears Config_Base+7, reads the sources from +6,
 * acknowledges each, and restores +7, so that a source still pending
 * raises a new edge (DS p.18).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/isa.h>
#include <linux/isapnp.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pnp.h>
#include <linux/timekeeping.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>

#include "es1869.h"

MODULE_AUTHOR("Ethan Halsall <ethan.s.halsall@gmail.com>");
MODULE_DESCRIPTION("ESS ES1869 AudioDrive, every feature");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP;
#ifdef CONFIG_PNP
static bool isapnp[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP;
#define is_isapnp_selected(dev) isapnp[dev]
#else
#define is_isapnp_selected(dev) 0
#endif
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the ES1869 card");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the ES1869 card");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable the ES1869 card");
#ifdef CONFIG_PNP
module_param_array(isapnp, bool, NULL, 0444);
MODULE_PARM_DESC(isapnp, "PnP detection (0: the ports below)");
#endif
module_param_hw_array(port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(port, "Audio_Base (220h)");
module_param_hw_array(fm_port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(fm_port, "FM port (388h)");
module_param_hw_array(mpu_port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port (330h)");
module_param_hw_array(irq, int, irq, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ");
module_param_hw_array(dma1, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma1, "Audio 1's DMA channel, 8-bit");
module_param_hw_array(dma2, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma2, "Audio 2's DMA channel");

const char *const es1869_owner_names[] = {
	"none", "Audio 1 playback", "Audio 1 recording", "FM recording",
	"Audio 2 playback", "dual playback",
};

// --- struct es1869_io -----------------------------------------------------------

static u8 io_inb(void *ctx, u16 p)
{
	return inb(p);
}

static void io_outb(void *ctx, u16 p, u8 val)
{
	struct es1869 *chip = ctx;

	// logged before the write, so that a write that hangs the machine is
	// the last line on the console
	if (es1869_val(ES_S_irq_trace) >= 2)
		dev_info(chip->dev, "out %03x = %02x\n", p, val);
	outb(val, p);
}

static void io_udelay(void *ctx, unsigned int us)
{
	udelay(us);
}

static u64 io_now(void *ctx)
{
	return ktime_get_ns();
}

static void io_dma_program(void *ctx, unsigned int ch, u32 phys, u32 bytes,
			   u8 mode)
{
	static const u8 page_port[8] = { 0x87, 0x83, 0x81, 0x82,
					 0x8F, 0x8B, 0x89, 0x8A };
	struct es1869 *chip = ctx;
	unsigned long flags;
	u16 addr_port, count_port, a, c;
	u8 page;

	snd_dma_program(ch, phys, bytes, mode | DMA_MODE_NO_ENABLE);
	if (!es1869_val(ES_S_irq_trace))
		return;
	// what the 8237 took, read back, against what it was given
	addr_port = ch < 4 ? 2 * ch : 0xC0 + 4 * (ch - 4);
	count_port = addr_port + (ch < 4 ? 1 : 2);
	flags = claim_dma_lock();
	clear_dma_ff(ch);
	a = inb(addr_port);
	a |= inb(addr_port) << 8;
	c = inb(count_port);
	c |= inb(count_port) << 8;
	page = inb(page_port[ch]);
	release_dma_lock(flags);
	dev_info(chip->dev,
		 "8237 ch %u: given %#x, %u bytes, mode %02x; holds page %02x, address %04x, count %04x\n",
		 ch, phys, bytes, mode, page, a, c);
}

static void io_dma_mask(void *ctx, unsigned int ch, bool masked)
{
	struct es1869 *chip = ctx;
	unsigned long flags;

	if (es1869_val(ES_S_irq_trace))
		dev_info(chip->dev, "8237 ch %u %s\n", ch,
			 masked ? "masked" : "unmasked");
	flags = claim_dma_lock();
	if (masked)
		disable_dma(ch);
	else
		enable_dma(ch);
	release_dma_lock(flags);
}

// two readings: each leaves the flip-flop at the low byte for the next
static void io_dma_residue(void *ctx, unsigned int ch, u32 *r)
{
	unsigned long flags = claim_dma_lock();

	clear_dma_ff(ch);
	r[0] = get_dma_residue(ch);
	r[1] = get_dma_residue(ch);
	release_dma_lock(flags);
}

// --- the interrupt ------------------------------------------------------------------

static irqreturn_t es1869_interrupt(int irq_no, void *dev_id)
{
	struct es1869 *chip = dev_id;
	struct es1869_hw *hw = &chip->hw;
	bool dance = es1869_val(ES_S_irq_mode) == 1;
	// when the stream's clock counts, the first thing the handler does
	u64 raw = chip->clk_a1 || chip->clk_a2 ? ktime_get_raw_ns() : 0;
	cycles_t c0 = get_cycles();
	struct snd_pcm_substream *s1, *s2;
	unsigned int pass;
	bool storm = false;
	u8 st, any = 0, dropped;

	// polled, as ESS's drivers do it, until a pass finds nothing, since a
	// source that comes while another holds the edge-triggered line high
	// raises no edge of its own; with the mask dance, one pass
	for (pass = 0; pass < 4; pass++) {
		s1 = s2 = NULL;
		spin_lock(&chip->reg_lock);
		if (!pass && es1869_irq_storm(hw, jiffies, HZ)) {
			storm = true;
			es1869_irq_sources_locked(chip);
		}
		dropped = hw->stats.irq_dropped;
		// polled: the sources Config_Base+6 shows, and every one where it
		// lives when a first pass finds none there
		if (dance)
			st = es1869_irq_enter(hw);
		else
			st = es1869_irq_take(hw);
		if (!dance && !st && !pass)
			st = es1869_irq_poll(hw);
		if (st & ES_IRQ_A1) {
			es1869_a1_irq(chip, &s1);
			es1869_clock_tick(chip, ES_IRQ_A1, pass ? 0 : raw);
		}
		if (st & ES_IRQ_A2) {
			es1869_a2_irq(chip, &s2);
			es1869_clock_tick(chip, ES_IRQ_A2, pass ? 0 : raw);
		}
		dropped ^= hw->stats.irq_dropped;
		spin_unlock(&chip->reg_lock);

		any |= st;
		if (es1869_val(ES_S_irq_trace))
			dev_info_ratelimited(chip->dev,
					     "irq: pass %u, sources %02x, mask %02x\n",
					     pass, st, hw->imr);
		if (dropped)
			dev_err(chip->dev, "sources %02x stay asserted after their acknowledge: off the line\n",
				dropped);
		if ((st & ES_IRQ_MPU) && chip->rmidi)
			snd_mpu401_uart_interrupt(irq_no, chip->rmidi->private_data);
		if (s1)
			snd_pcm_period_elapsed(s1);
		if (s2)
			snd_pcm_period_elapsed(s2);
		if (st & ES_IRQ_HWV)
			es1869_hwvol_notify(chip);
		if (dance) {
			spin_lock(&chip->reg_lock);
			es1869_irq_exit(hw);
			spin_unlock(&chip->reg_lock);
			break;
		}
		if (!st)
			break;
	}
	// four busy passes: what is left holds the line, which then shows no
	// other source, and the last pass saw it go on
	if (pass == 4) {
		spin_lock(&chip->reg_lock);
		hw->stats.irq_stuck++;
		es1869_irq_edge(hw);
		spin_unlock(&chip->reg_lock);
	}
	if (!any && !dance)
		hw->stats.irq_none++;
	chip->isr_calls++;
	chip->isr_cycles += get_cycles() - c0;
	if (storm)
		dev_err(chip->dev, "interrupt storm: every source off the line\n");
	return any ? IRQ_HANDLED : IRQ_NONE;
}

// --- the chip ------------------------------------------------------------------------

// the mixer registers a resume writes back
static const u8 mixer_kept[] = {
	0x14, 0x1A, 0x1C, 0x36, 0x38, 0x3A, 0x3C, 0x3E, 0x48, 0x50, 0x52,
	0x54, 0x56, 0x58, 0x5A, 0x60, 0x61, 0x62, 0x63, 0x64, 0x68, 0x69,
	0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x74, 0x76,
	0x7C, 0x7D, 0x7F,
};

static const u8 ctrl_kept[] = { 0xB4, 0xBA, 0xBB };

// the sources the line carries, as irq_sources allows them: both engines
// (an unacknowledged Audio 1 interrupt is its last), the buttons, and the
// MPU-401 with its rawmidi; under the register lock
void es1869_irq_sources_locked(struct es1869 *chip)
{
	struct es1869_hw *hw = &chip->hw;
	u8 want = ES_IRQ_A1 | ES_IRQ_A2 | ES_IRQ_HWV |
		  (chip->rmidi ? ES_IRQ_MPU : 0);

	want &= es1869_val(ES_S_irq_sources);
	if (hw->storm)
		want = 0;
	hw->imr = want & ~hw->stats.irq_dropped;
	es1869_mix_update(hw, 0x64, 0x42, (want & ES_IRQ_HWV ? 0x02 : 0) |
					  (want & ES_IRQ_MPU ? 0x40 : 0));
	// Config_Base+7 carries only the sources the handler takes back: one
	// left out, or the CD-ROM, modem and GP pins, which nothing here
	// serves, would hold the edge-triggered line high and silence the
	// others. The mask dance also writes it at each interrupt. A source
	// left pending from before, as across a reload, gets its edge
	es1869_irq_edge(hw);
}

// --- the chipset ---------------------------------------------------------------

// An Intel PIIX4 runs up to two DMA channels with Type F timing and its DMA
// line buffer (MBDMA, ISA bridge 76h and 77h: bit 7 on, bits 2:0 the
// channel). The Armada 1500c's BIOS sets it for both of the ES1869's: its
// playback works, but the first byte a recording moves hangs the machine.
// Unless piix4_type_f keeps it, Audio 1's channel takes ISA timing, until
// the next boot
static void es1869_type_f_find(struct es1869 *chip)
{
	struct pci_dev *isa;
	u8 reg, v;

	isa = pci_get_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_0,
			     NULL);
	if (!isa)
		return;
	for (reg = 0x76; reg <= 0x77; reg++) {
		pci_read_config_byte(isa, reg, &v);
		if ((v & 0x80) && (v & 0x07) == chip->dma1) {
			chip->isa_bridge = isa;
			chip->type_f_reg = reg;
			chip->type_f_bios = v;
			return;
		}
	}
	pci_dev_put(isa);
}

static void es1869_type_f_put(void *data)
{
	struct es1869 *chip = data;

	pci_dev_put(chip->isa_bridge);
}

void es1869_type_f_locked(struct es1869 *chip)
{
	u8 v = chip->type_f_bios;

	if (!chip->isa_bridge)
		return;
	if (!es1869_val(ES_S_piix4_type_f))
		v &= ~0x80;
	pci_write_config_byte(chip->isa_bridge, chip->type_f_reg, v);
}

static void es1869_irq_sources(struct es1869 *chip)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	es1869_irq_sources_locked(chip);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

// --- power save ----------------------------------------------------------------

// nothing has the chip: no engine open, and no user of the FM
static bool es1869_idle(struct es1869 *chip)
{
	struct snd_mpu401 *mpu = chip->rmidi ? chip->rmidi->private_data : NULL;

	return chip->a1_owner == ES_OWN_NONE &&
	       chip->a2_owner == ES_OWN_NONE &&
	       !(mpu && (mpu->mode & (MPU401_MODE_INPUT | MPU401_MODE_OUTPUT))) &&
	       !(chip->opl3 && chip->opl3->hwdep &&
		 chip->opl3->hwdep->used > chip->opl3_off);
}

static int es1869_chip_init(struct es1869 *chip, bool resume);

// pwr_save seconds after the last user left, the chip powers down: partly,
// with the mixer and the inputs still on, or fully with pwr_full
static void es1869_pwr_work(struct work_struct *work)
{
	struct es1869 *chip = container_of(to_delayed_work(work), struct es1869,
					   pwr_work);
	bool full = es1869_val(ES_S_pwr_full);
	unsigned long flags;

	mutex_lock(&chip->state_mutex);
	if (!chip->dead && !chip->pwr_down && es1869_idle(chip)) {
		spin_lock_irqsave(&chip->reg_lock, flags);
		es1869_power_down(&chip->hw, full);
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		chip->pwr_down = full ? 2 : 1;
	}
	mutex_unlock(&chip->state_mutex);
}

void es1869_power_idle(struct es1869 *chip)
{
	int secs = es1869_val(ES_S_pwr_save);

	if (secs <= 0)
		return;
	mutex_lock(&chip->state_mutex);
	if (!chip->pwr_stopped)
		mod_delayed_work(system_wq, &chip->pwr_work, secs * HZ);
	mutex_unlock(&chip->state_mutex);
}

// pwr_save written: 0 stops what is waiting, any other value counts again
void es1869_power_save_changed(struct es1869 *chip)
{
	if (es1869_val(ES_S_pwr_save) <= 0)
		cancel_delayed_work(&chip->pwr_work);
	else if (!chip->pwr_stopped)
		mod_delayed_work(system_wq, &chip->pwr_work,
				 es1869_val(ES_S_pwr_save) * HZ);
}

static void es1869_wake(struct es1869 *chip, u8 was)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	es1869_power_wake(&chip->hw, was == 2);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (was != 2)
		return;
	msleep(100);
	spin_lock_irqsave(&chip->reg_lock, flags);
	es1869_power_woken(&chip->hw);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

// from every open, under state_mutex: the chip up again before use; after
// a full power-down the shadows go back as at a resume. It stays down for
// the next open to try if that fails
int es1869_power_up(struct es1869 *chip)
{
	u8 was = chip->pwr_down;
	int err = 0;

	cancel_delayed_work(&chip->pwr_work);
	if (!was)
		return 0;
	es1869_wake(chip, was);
	if (was == 2)
		err = es1869_chip_init(chip, true);
	if (!err)
		chip->pwr_down = 0;
	return err;
}

// what the OPL3 asks for from where state_mutex would come after the
// hwdep's own mutex, or under spinlocks: the wake, and its waiters
static void es1869_wake_work(struct work_struct *work)
{
	struct es1869 *chip = container_of(work, struct es1869, wake_work);

	mutex_lock(&chip->state_mutex);
	if (!chip->pwr_stopped && es1869_power_up(chip) < 0)
		dev_err(chip->dev, "the chip didn't wake for the FM\n");
	mutex_unlock(&chip->state_mutex);
	if (chip->opl3 && chip->opl3->hwdep)
		wake_up(&chip->opl3->hwdep->open_wait);
}

// the MIDI port's users count as the chip's, and a full power-down stops it
static int es1869_mpu_open(struct snd_mpu401 *mpu)
{
	struct es1869 *chip = mpu->private_data;
	int err;

	mutex_lock(&chip->state_mutex);
	err = chip->dead ? -ENODEV : es1869_power_up(chip);
	mutex_unlock(&chip->state_mutex);
	return err;
}

static void es1869_mpu_close(struct snd_mpu401 *mpu)
{
	es1869_power_idle(mpu->private_data);
}

// before the card's disconnect, while what the work items read is there:
// from here on nothing arms them, a close that outlives the disconnect above
// all, and the next driver finds a chip that answers
static void es1869_pwr_cancel(void *data)
{
	struct es1869 *chip = data;

	mutex_lock(&chip->state_mutex);
	WRITE_ONCE(chip->pwr_stopped, true);
	mutex_unlock(&chip->state_mutex);
	cancel_delayed_work_sync(&chip->pwr_work);
	cancel_work_sync(&chip->wake_work);
	if (chip->pwr_down) {
		es1869_wake(chip, chip->pwr_down);
		chip->pwr_down = 0;
	}
}

// no source on the line, the next driver's edge its own
static void es1869_quiet_locked(struct es1869 *chip)
{
	chip->hw.imr = 0;
	es1869_irq_exit(&chip->hw);
	es1869_mix_update(&chip->hw, 0x64, 0x42, 0);
}

static void es1869_quiet(void *data)
{
	struct es1869 *chip = data;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	es1869_quiet_locked(chip);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

// the routing, a reset, the shadows and the settings; at a resume the chip
// has lost its state, so the shadows go back to it
static int es1869_chip_init(struct es1869 *chip, bool resume)
{
	struct es1869_hw *hw = &chip->hw;
	unsigned long flags;
	unsigned int i;
	int err;

	spin_lock_irqsave(&chip->reg_lock, flags);
	es1869_route(hw, chip->irq, chip->mpu_port);
	// the reset turns the codec back to the DAC, and takes Audio 1 off the
	// mixer (DS p.43, p.49)
	chip->adc_on = false;
	chip->a1_dac = false;
	err = es1869_reset(hw);
	if (err)
		goto out;
	// a reset leaves Audio 2 running (rst.ext): stopped, its latch clear
	es1869_mix_write(hw, 0x78, 0x00);
	es1869_mix_write(hw, 0x7A, 0x00);
	if (resume) {
		for (i = 0; i < ARRAY_SIZE(mixer_kept); i++)
			es1869_mix_write(hw, mixer_kept[i], hw->mix[mixer_kept[i]]);
		es1869_port7_write(hw, hw->port7);
		for (i = 0; i < ARRAY_SIZE(ctrl_kept); i++)
			es1869_ctrl_write(hw, ctrl_kept[i],
					  hw->ctrl[ctrl_kept[i] - 0xA0]);
	} else {
		// the shadows as the chip has them, but for the identification
		// (40h) and the reset and acknowledge (00h, 66h)
		for (i = 0x01; i < 0x80; i++)
			if (i != 0x40 && i != 0x66)
				hw->mix[i] = es1869_mix_read(hw, i);
		hw->port7 = chip->io.inb(chip, chip->port + 7) & ~0x84;
		// the record level and the ADC offsets, which a reset keeps
		for (i = 0; i < ARRAY_SIZE(ctrl_kept); i++) {
			err = es1869_ctrl_read(hw, ctrl_kept[i]);
			if (err >= 0)
				hw->ctrl[ctrl_kept[i] - 0xA0] = err;
		}
		err = 0;
	}
out:
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (err)
		return err;
	if (!resume)
		es1869_settings_found(chip);
	es1869_settings_apply_init(chip);
	return 0;
}

static void es1869_unlist(void *data)
{
	es1869_settings_card_del(data);
}

// a resource in an 8-bit PnP register of the audio device: the chip's unless
// a parameter gives it, and then the parameter's, which the chip otherwise
// wouldn't follow, without a word: an IRQ or a DMA channel that nothing
// answers on
static void es1869_resource8(struct es1869 *chip, int *val, int automatic,
			     u8 reg, u8 mask, const char *name)
{
	u8 v = es1869_pnp_read(&chip->hw, 1, reg) & mask;

	if (*val == automatic) {
		*val = v;
	} else if (*val >= 0 && *val != v) {
		dev_info(chip->dev, "%s %d, where the chip had %d\n", name, *val,
			 v);
		es1869_pnp_write(&chip->hw, 1, reg, *val);
	}
}

// and a port in two of them, high byte first
static void es1869_resource16(struct es1869 *chip, long *val, u8 reg,
			      const char *name)
{
	struct es1869_hw *hw = &chip->hw;
	u16 v = es1869_pnp_read(hw, 1, reg) << 8 |
		es1869_pnp_read(hw, 1, reg + 1);

	if (*val == SNDRV_AUTO_PORT) {
		*val = v ? v : -1;
	} else if (*val > 0 && *val != v) {
		dev_info(chip->dev, "%s 0x%lx, where the chip had 0x%x\n", name,
			 *val, v);
		es1869_pnp_write(hw, 1, reg, *val >> 8);
		es1869_pnp_write(hw, 1, reg + 1, *val & 0xFF);
	}
}

static void es1869_chip_resources(struct es1869 *chip, int dev)
{
	es1869_resource8(chip, &irq[dev], SNDRV_AUTO_IRQ, 0x70, 0x0F, "IRQ");
	es1869_resource8(chip, &dma1[dev], SNDRV_AUTO_DMA, 0x74, 0x07,
			 "Audio 1's DMA");
	es1869_resource8(chip, &dma2[dev], SNDRV_AUTO_DMA, 0x75, 0x07,
			 "Audio 2's DMA");
	es1869_resource16(chip, &fm_port[dev], 0x62, "the FM's port");
	es1869_resource16(chip, &mpu_port[dev], 0x64, "the MPU-401's port");
}

static int es1869_probe(struct snd_card *card, int dev)
{
	struct es1869 *chip = card->private_data;
	struct device *d = card->dev;
	struct es1869_hw *hw = &chip->hw;
	int err;

	chip->card = card;
	chip->dev = d;
	chip->dev_index = dev;
	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->fm_lock);
	INIT_DELAYED_WORK(&chip->pwr_work, es1869_pwr_work);
	INIT_WORK(&chip->wake_work, es1869_wake_work);
	mutex_init(&chip->state_mutex);
	INIT_LIST_HEAD(&chip->list);
	chip->io = (struct es1869_io){
		.ctx = chip,
		.inb = io_inb,
		.outb = io_outb,
		.udelay = io_udelay,
		.now_ns = io_now,
		.dma_program = io_dma_program,
		.dma_mask = io_dma_mask,
		.dma_residue = io_dma_residue,
	};
	chip->port = port[dev];
	hw->io = &chip->io;
	hw->port = chip->port;
	hw->fm_delay = es1869_val(ES_S_esfm_delay);

	if (!devm_request_region(d, chip->port, 16, "ES1869"))
		return -EBUSY;
	// nobody else uses the chip before the interrupt is taken
	err = es1869_identify(hw, &chip->ident);
	if (err) {
		dev_dbg(d, "no ES1869 at 0x%lx\n", chip->port);
		return -ENODEV;
	}
	hw->cfg = chip->ident.cfg;
	if (!devm_request_region(d, hw->cfg, 8, "ES1869 config")) {
		dev_err(d, "Config_Base 0x%x is in use\n", hw->cfg);
		return -EBUSY;
	}
	es1869_chip_resources(chip, dev);
	chip->irq = irq[dev];
	chip->dma1 = dma1[dev];
	// 4 is the chip's "none" (DS p.31): Audio 1 runs alone
	chip->dma2 = dma2[dev] == 4 ? -1 : dma2[dev];
	chip->fm_port = fm_port[dev] > 0 ? fm_port[dev] : 0;
	chip->mpu_port = mpu_port[dev] > 0 ? mpu_port[dev] : 0;
	hw->fm = chip->fm_port ? chip->fm_port : chip->port;
	hw->mpu = chip->mpu_port;
	if (chip->dma1 < 0 || chip->dma1 > 3 || chip->dma1 == 2) {
		dev_err(d, "Audio 1 needs an 8-bit DMA channel, not %d\n",
			chip->dma1);
		return -EINVAL;
	}
	if (chip->dma2 > 7 || chip->dma2 == chip->dma1) {
		dev_err(d, "Audio 2 needs a DMA channel of its own, not %d\n",
			chip->dma2);
		return -EINVAL;
	}
	if (chip->dma2 < 0)
		dev_info(d, "no DMA channel for Audio 2: it stays closed\n");
	if (chip->irq <= 0) {
		dev_err(d, "no IRQ\n");
		return -EINVAL;
	}
	es1869_type_f_find(chip);
	if (chip->isa_bridge) {
		err = devm_add_action_or_reset(d, es1869_type_f_put, chip);
		if (err)
			return err;
		dev_info(d, "the PIIX4's Type F DMA timing on DMA %d %s\n",
			 chip->dma1, es1869_val(ES_S_piix4_type_f) ?
			 "kept (piix4_type_f=1)" : "off: recording hangs with it");
	}

	err = es1869_chip_init(chip, false);
	if (err) {
		dev_err(d, "the DSP doesn't answer (%d)\n", err);
		return err;
	}
	err = snd_devm_request_dma(d, chip->dma1, "ES1869 Audio 1");
	if (err)
		return err;
	if (chip->dma2 >= 0) {
		err = snd_devm_request_dma(d, chip->dma2, "ES1869 Audio 2");
		if (err)
			return err;
	}
	err = devm_request_irq(d, chip->irq, es1869_interrupt, 0, "ES1869",
			       chip);
	if (err) {
		dev_err(d, "IRQ %d is in use\n", chip->irq);
		return err;
	}
	card->sync_irq = chip->irq;
	err = devm_add_action_or_reset(d, es1869_quiet, chip);
	if (err)
		return err;

	// the card id comes from the shortname's last word: ES1869, as with
	// snd-es18xx, so that alsactl's saved state and hw:ES1869 still fit
	strscpy(card->driver, "ES1869");
	strscpy(card->shortname, "ESS AudioDrive ES1869");
	strscpy(card->mixername, card->shortname);
	scnprintf(card->longname, sizeof(card->longname),
		  "%s at 0x%lx, irq %d, dma1 %d, dma2 %d", card->shortname,
		  chip->port, chip->irq, chip->dma1, chip->dma2);

	err = es1869_pcm_new(chip);
	if (err < 0)
		return err;
	err = es1869_settings_controls(chip);
	if (err < 0)
		return err;
	err = es1869_mixer_extra(chip);
	if (err < 0)
		return err;
	err = es1869_fm_new(chip);
	if (err < 0)
		return err;
	err = es1869_esfm_new(chip);
	if (err < 0)
		return err;
	es1869_opl3_gate(chip);
	if (chip->mpu_port) {
		err = snd_mpu401_uart_new(card, 0, MPU401_HW_ES18XX,
					  chip->mpu_port, MPU401_INFO_IRQ_HOOK,
					  -1, &chip->rmidi);
		if (err < 0) {
			dev_warn(d, "no MPU-401 at 0x%lx\n", chip->mpu_port);
		} else {
			struct snd_mpu401 *mpu = chip->rmidi->private_data;

			mpu->private_data = chip;
			mpu->open_input = es1869_mpu_open;
			mpu->open_output = es1869_mpu_open;
			mpu->close_input = es1869_mpu_close;
			mpu->close_output = es1869_mpu_close;
		}
	}
	err = es1869_regs_hwdep_new(chip);
	if (err < 0)
		return err;
	es1869_proc_init(chip);
	es1869_irq_sources(chip);

	err = snd_card_register(card);
	if (err < 0)
		return err;
	err = devm_add_action_or_reset(d, es1869_pwr_cancel, chip);
	if (err)
		return err;
	err = devm_add_action_or_reset(d, es1869_unlist, chip);
	if (err)
		return err;
	es1869_settings_card_add(chip);
	es1869_power_idle(chip);
	return 0;
}

static int es1869_card_new(struct device *d, int dev, struct snd_card **cardp)
{
	return snd_devm_card_new(d, index[dev], id[dev], THIS_MODULE,
				 sizeof(struct es1869), cardp);
}

// --- power ---------------------------------------------------------------------------

static int es1869_suspend(struct snd_card *card)
{
	struct es1869 *chip = card->private_data;
	unsigned long flags;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	// a resume puts back what the chip has now
	es1869_settings_sync(chip);
	cancel_delayed_work_sync(&chip->pwr_work);
	cancel_work_sync(&chip->wake_work);
	hrtimer_cancel(&chip->a1_settle);
	hrtimer_cancel(&chip->mirror.tick);
	hrtimer_cancel(&chip->mirror.period);
	hrtimer_cancel(&chip->dual.tick);
	cancel_work_sync(&chip->mirror.copy_work);
	cancel_work_sync(&chip->dual.copy_work);
	spin_lock_irqsave(&chip->reg_lock, flags);
	// a software reset wouldn't stop Audio 2: both engines stopped, and
	// no source left on the line; a chip fully down hears no command
	es1869_mix_write(&chip->hw, 0x78, 0x00);
	if (chip->pwr_down != 2)
		es1869_ctrl_write(&chip->hw, 0xB8, 0x00);
	es1869_quiet_locked(chip);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

// an open MIDI port, out of its UART mode with the chip's reset
static void es1869_mpu_resume(struct es1869 *chip)
{
	struct snd_mpu401 *mpu = chip->rmidi ? chip->rmidi->private_data : NULL;
	unsigned long flags;

	if (!mpu || !(mpu->mode & (MPU401_MODE_INPUT | MPU401_MODE_OUTPUT)))
		return;
	spin_lock_irqsave(&chip->reg_lock, flags);
	es1869_mpu_uart(&chip->hw);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static int es1869_resume(struct snd_card *card)
{
	struct es1869 *chip = card->private_data;
	struct es1869_ident ident;
	unsigned long flags;
	int err;

	// a chip that kept its power stays powered down without this
	if (chip->pwr_down)
		es1869_wake(chip, chip->pwr_down);
	spin_lock_irqsave(&chip->reg_lock, flags);
	err = es1869_identify(&chip->hw, &ident);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (!err && ident.cfg != chip->hw.cfg)
		err = -ENODEV;
	if (!err)
		err = es1869_chip_init(chip, true);
	if (err) {
		dev_err(chip->dev, "the ES1869 didn't come back (%d)\n", err);
		chip->dead = true;
		// D0 all the same: every client sleeps until the card is there,
		// in close() too, and nothing could end it
		snd_power_change_state(card, SNDRV_CTL_POWER_D0);
		return 0;
	}
	chip->dead = false;
	chip->pwr_down = 0;
	es1869_fm_resume(chip);
	es1869_mpu_resume(chip);
	es1869_irq_sources(chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	es1869_power_idle(chip);
	return 0;
}

// --- ISA: the ports the parameters give ------------------------------------------

static int es1869_isa_match(struct device *pdev, unsigned int dev)
{
	return enable[dev] && !is_isapnp_selected(dev);
}

static int es1869_isa_probe(struct device *pdev, unsigned int dev)
{
	static const long ports[] = { 0x220, 0x240, 0x260, 0x280 };
	bool scan = port[dev] == SNDRV_AUTO_PORT;
	struct snd_card *card;
	unsigned int i;
	int err = -ENODEV;

	for (i = 0; i < ARRAY_SIZE(ports); i++) {
		void *try;

		if (scan)
			port[dev] = ports[i];
		else if (i)
			break;
		// a failed try gives back its card, with its index, and ports
		try = devres_open_group(pdev, NULL, GFP_KERNEL);
		if (!try)
			return -ENOMEM;
		err = es1869_card_new(pdev, dev, &card);
		if (!err)
			err = es1869_probe(card, dev);
		if (!err) {
			devres_close_group(pdev, try);
			dev_set_drvdata(pdev, card);
			return 0;
		}
		devres_release_group(pdev, try);
	}
	if (scan)
		port[dev] = SNDRV_AUTO_PORT;
	return err;
}

static int es1869_isa_suspend(struct device *dev, unsigned int n,
			      pm_message_t state)
{
	return es1869_suspend(dev_get_drvdata(dev));
}

static int es1869_isa_resume(struct device *dev, unsigned int n)
{
	return es1869_resume(dev_get_drvdata(dev));
}

static struct isa_driver es1869_isa_driver = {
	.match = es1869_isa_match,
	.probe = es1869_isa_probe,
	.suspend = es1869_isa_suspend,
	.resume = es1869_isa_resume,
	.driver = {
		.name = "es1869",
	},
};

#ifdef CONFIG_PNP
// --- PnP BIOS and ACPI: the device ESS1869 ------------------------------------

static const struct pnp_device_id es1869_pnp_ids[] = {
	{ .id = "ESS1869" },
	{ .id = "" }
};
MODULE_DEVICE_TABLE(pnp, es1869_pnp_ids);

static void es1869_pnp_resources(int dev, struct pnp_dev *pdev)
{
	port[dev] = pnp_port_start(pdev, 0);
	if (pnp_port_valid(pdev, 1))
		fm_port[dev] = pnp_port_start(pdev, 1);
	if (pnp_port_valid(pdev, 2))
		mpu_port[dev] = pnp_port_start(pdev, 2);
	irq[dev] = pnp_irq(pdev, 0);
	dma1[dev] = pnp_dma(pdev, 0);
	dma2[dev] = pnp_dma(pdev, 1);
}

static int es1869_pnp_probe(struct pnp_dev *pdev,
			    const struct pnp_device_id *pid)
{
	static int dev;
	struct snd_card *card;
	int err;

	if (pnp_device_is_isapnp(pdev))
		return -ENOENT; // the card driver below takes those
	for (; dev < SNDRV_CARDS; dev++)
		if (enable[dev] && isapnp[dev])
			break;
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (pnp_activate_dev(pdev) < 0) {
		dev_err(&pdev->dev, "PnP configure failure\n");
		return -EBUSY;
	}
	es1869_pnp_resources(dev, pdev);
	// the ISA probe (isapnp=0) may have the chip already
	if (!request_region(port[dev], 16, "ES1869"))
		return -ENODEV;
	release_region(port[dev], 16);
	err = es1869_card_new(&pdev->dev, dev, &card);
	if (err < 0)
		return err;
	err = es1869_probe(card, dev);
	if (err < 0)
		return err;
	pnp_set_drvdata(pdev, card);
	dev++;
	return 0;
}

static int es1869_pnp_suspend(struct pnp_dev *pdev, pm_message_t state)
{
	return es1869_suspend(pnp_get_drvdata(pdev));
}

static int es1869_pnp_resume(struct pnp_dev *pdev)
{
	return es1869_resume(pnp_get_drvdata(pdev));
}

static struct pnp_driver es1869_pnp_driver = {
	.name = "es1869-pnpbios",
	.id_table = es1869_pnp_ids,
	.probe = es1869_pnp_probe,
	.suspend = es1869_pnp_suspend,
	.resume = es1869_pnp_resume,
};

// --- ISA PnP: the card, its audio device and its control device ---------------

static const struct pnp_card_device_id es1869_pnpc_ids[] = {
	{ .id = "ESS0003", .devs = { { "ESS1869" }, { "ESS0006" } } },
	{ .id = "ESS1869", .devs = { { "ESS1869" }, { "ESS0006" } } },
	{ .id = "" }
};
MODULE_DEVICE_TABLE(pnp_card, es1869_pnpc_ids);

static int es1869_pnpc_probe(struct pnp_card_link *pcard,
			     const struct pnp_card_device_id *pid)
{
	static int dev;
	struct pnp_dev *audio, *ctrl;
	struct snd_card *card;
	struct es1869 *chip;
	int err;

	for (; dev < SNDRV_CARDS; dev++)
		if (enable[dev] && isapnp[dev])
			break;
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	audio = pnp_request_card_device(pcard, pid->devs[0].id, NULL);
	ctrl = pnp_request_card_device(pcard, pid->devs[1].id, NULL);
	if (!audio || !ctrl)
		return -EBUSY;
	if (pnp_activate_dev(ctrl) < 0 || pnp_activate_dev(audio) < 0) {
		dev_err(&audio->dev, "PnP configure failure\n");
		return -EBUSY;
	}
	es1869_pnp_resources(dev, audio);
	// the audio device, which unbinds when the driver goes, where the card
	// device has no bus and would keep the card and its interrupt
	err = es1869_card_new(&audio->dev, dev, &card);
	if (err < 0)
		return err;
	err = es1869_probe(card, dev);
	if (err < 0)
		return err;
	chip = card->private_data;
	chip->pnp_audio = audio;
	chip->pnp_ctrl = ctrl;
	pnp_set_card_drvdata(pcard, card);
	dev++;
	return 0;
}

static int es1869_pnpc_suspend(struct pnp_card_link *pcard,
			       pm_message_t state)
{
	return es1869_suspend(pnp_get_card_drvdata(pcard));
}

// isapnp disables both devices at a suspend, and the card's resume comes
// with the first one's, before the other's
static int es1869_pnpc_resume(struct pnp_card_link *pcard)
{
	struct snd_card *card = pnp_get_card_drvdata(pcard);
	struct es1869 *chip = card->private_data;

	if (!chip->pnp_ctrl->active)
		pnp_start_dev(chip->pnp_ctrl);
	if (!chip->pnp_audio->active)
		pnp_start_dev(chip->pnp_audio);
	return es1869_resume(card);
}

static struct pnp_card_driver es1869_pnpc_driver = {
	.flags = PNP_DRIVER_RES_DISABLE,
	.name = "es1869",
	.id_table = es1869_pnpc_ids,
	.probe = es1869_pnpc_probe,
	.suspend = es1869_pnpc_suspend,
	.resume = es1869_pnpc_resume,
};

static bool pnp_registered, pnpc_registered;
#endif /* CONFIG_PNP */

static bool isa_registered;

static int __init es1869_init(void)
{
	int err;

	err = isa_register_driver(&es1869_isa_driver, SNDRV_CARDS);
	isa_registered = !err;
#ifdef CONFIG_PNP
	pnp_registered = !pnp_register_driver(&es1869_pnp_driver);
	pnpc_registered = !pnp_register_card_driver(&es1869_pnpc_driver);
	// no card yet is no error: the module stays for one to come
	if (isa_registered || pnp_registered || pnpc_registered)
		err = 0;
#endif
	return err;
}

static void __exit es1869_exit(void)
{
#ifdef CONFIG_PNP
	if (pnpc_registered)
		pnp_unregister_card_driver(&es1869_pnpc_driver);
	if (pnp_registered)
		pnp_unregister_driver(&es1869_pnp_driver);
#endif
	if (isa_registered)
		isa_unregister_driver(&es1869_isa_driver);
}

module_init(es1869_init);
module_exit(es1869_exit);
