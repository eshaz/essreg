// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The register hwdep, "ES1869 regs" (hwdep 2): every register of the chip
 * through the driver's lock, for essreg's register utility and es1869test.
 *
 * Notes:
 *
 * Reads are free but for those that change the chip's state, which need
 * ES1869_REG_FORCE: mixer 40h (its identification sequence moves on),
 * Audio_Base+6 and +Eh (a read acknowledges Audio 1) and +Ah (the DSP's
 * data). Controller registers come from the driver's shadows, unless
 * ES1869_REG_LIVE asks for the chip and Audio 1 is idle, since a read goes
 * through the DSP. Writes need the regs_write parameter and CAP_SYS_RAWIO,
 * update the shadows and announce the controls on the register.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/capability.h>
#include <linux/sched/signal.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/timex.h>
#include <linux/uaccess.h>
#include <sound/hwdep.h>

#include "es1869.h"
#include "uapi/es1869.h"

static bool read_changes_state(u8 bank, u8 reg)
{
	if (bank == ES1869_BANK_MIXER)
		return reg == 0x40;
	if (bank == ES1869_BANK_APORT)
		return reg == 0x06 || reg == 0x0A || reg == 0x0E ||
		       reg == 0x0F || reg < 0x04 || reg == 0x08 || reg == 0x09;
	return false;
}

static int reg_read(struct es1869 *chip, struct es1869_reg *r)
{
	struct es1869_hw *hw = &chip->hw;
	unsigned long flags;
	int v = 0;

	// such a read takes an acknowledge or moves a sequence from the driver
	if (read_changes_state(r->bank, r->reg) &&
	    (!(r->flags & ES1869_REG_FORCE) || !capable(CAP_SYS_RAWIO)))
		return -EPERM;
	spin_lock_irqsave(&chip->reg_lock, flags);
	switch (r->bank) {
	case ES1869_BANK_MIXER:
		v = es1869_mix_read(hw, r->reg);
		break;
	case ES1869_BANK_CTRL:
		if (r->reg < 0xA0 || r->reg > 0xBF)
			v = -EINVAL;
		else if ((r->flags & ES1869_REG_LIVE) && !es1869_a1_busy(chip))
			v = es1869_ctrl_read(hw, r->reg);
		else
			v = hw->ctrl[r->reg - 0xA0];
		break;
	case ES1869_BANK_APORT:
		v = r->reg < 16 ? chip->io.inb(chip, hw->port + r->reg) : -EINVAL;
		break;
	case ES1869_BANK_CPORT:
		v = r->reg < 8 ? chip->io.inb(chip, hw->cfg + r->reg) : -EINVAL;
		break;
	case ES1869_BANK_PNPCARD:
		v = es1869_pnp_read(hw, 0xFF, r->reg);
		break;
	case ES1869_BANK_PNPLDN:
		v = es1869_ldn(hw, r->ldn);
		if (v >= 0)
			v = es1869_pnp_read(hw, v, r->reg);
		break;
	default:
		v = -EINVAL;
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (v < 0)
		return v;
	r->value = v;
	return 0;
}

static int reg_write(struct es1869 *chip, const struct es1869_reg *r)
{
	struct es1869_hw *hw = &chip->hw;
	unsigned long flags;
	int err = 0, ldn;
	u8 bank = 0xFF;

	if (!es1869_val(ES_S_regs_write) || !capable(CAP_SYS_RAWIO))
		return -EPERM;
	if (security_locked_down(LOCKDOWN_IOPORT))
		return -EPERM;
	spin_lock_irqsave(&chip->reg_lock, flags);
	switch (r->bank) {
	case ES1869_BANK_MIXER:
		es1869_mix_write(hw, r->reg, r->value);
		bank = B_MIX;
		break;
	case ES1869_BANK_CTRL:
		if (r->reg < 0xA0 || r->reg > 0xBF)
			err = -EINVAL;
		else
			err = es1869_ctrl_write(hw, r->reg, r->value);
		bank = B_CTRL;
		break;
	case ES1869_BANK_APORT:
		if (r->reg == 0x07)
			es1869_port7_write(hw, r->value);
		else if (r->reg < 16)
			chip->io.outb(chip, hw->port + r->reg, r->value);
		else
			err = -EINVAL;
		bank = B_PORT7;
		break;
	case ES1869_BANK_CPORT:
		if (r->reg < 8)
			chip->io.outb(chip, hw->cfg + r->reg, r->value);
		else
			err = -EINVAL;
		break;
	case ES1869_BANK_PNPCARD:
		es1869_pnp_write(hw, 0xFF, r->reg, r->value);
		break;
	case ES1869_BANK_PNPLDN:
		ldn = es1869_ldn(hw, r->ldn);
		if (ldn < 0)
			err = ldn;
		else
			es1869_pnp_write(hw, ldn, r->reg, r->value);
		break;
	default:
		err = -EINVAL;
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (!err && bank != 0xFF)
		es1869_settings_notify_reg(chip, bank,
					   bank == B_PORT7 ? 0x07 : r->reg);
	return err;
}

// only the rates and the filter clocks move: the rest of the chip keeps still
static bool pattern_reg(u8 bank, u8 reg)
{
	if (bank == ES1869_BANK_MIXER)
		return reg == 0x70 || reg == 0x72;
	return bank == ES1869_BANK_CTRL && (reg == 0xA1 || reg == 0xA2);
}

/*
 * The CPU spins between the writes with interrupts on, so the streams go on;
 * a step that the machine's other work took is dropped, and the entry that
 * time calls for is written, which keeps the pattern in phase with the
 * cycle counter, the crystal of the chip's own clocks.
 */
static int reg_pattern(struct es1869 *chip, struct es1869_reg_pattern *p,
		       const u16 *tab)
{
	struct es1869_hw *hw = &chip->hw;
	u64 at = p->base ? p->base : get_cycles();
	u32 frac = 0, step_frac = p->step & 0xFFFF;
	u64 step = p->step >> 16;
	bool alone = p->flags & ES1869_PATTERN_ALONE;
	unsigned long flags;
	u32 i = 0, off = p->offset % p->len;
	int last = -1, err = 0;

	p->writes = p->skipped = p->late = p->longest = 0;
	while (i < p->count && !err) {
		u64 now = get_cycles(), t;
		int e;

		if ((s64)(now - at) < 0) {
			if (!alone) {
				cond_resched();
				if (signal_pending(current))
					err = -EINTR;
			}
			cpu_relax();
			continue;
		}
		if (now - at > p->late)
			p->late = now - at;
		// the entry of this moment, then the next step
		for (;;) {
			e = tab[off];
			if (++off == p->len)
				off = 0;
			i++;
			frac += step_frac;
			at += step + (frac >> 16);
			frac &= 0xFFFF;
			if ((s64)(now - at) < 0 || i >= p->count)
				break;
			p->skipped++;
		}
		if (e == last)
			continue;
		last = e;
		t = get_cycles();
		spin_lock_irqsave(&chip->reg_lock, flags);
		if (p->bank == ES1869_BANK_MIXER)
			es1869_mix_write(hw, e >> 8, e & 0xFF);
		else
			err = es1869_ctrl_write(hw, e >> 8, e & 0xFF);
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		t = get_cycles() - t;
		if (t > p->longest)
			p->longest = t;
		p->writes++;
		if (!alone)
			cond_resched();
	}
	p->base = at;
	p->offset = off;
	return err;
}

static int regs_pattern(struct es1869 *chip, void __user *argp)
{
	struct es1869_reg_pattern p;
	u16 *tab;
	int err, i;

	if (!es1869_val(ES_S_regs_write) || !capable(CAP_SYS_RAWIO))
		return -EPERM;
	if (security_locked_down(LOCKDOWN_IOPORT))
		return -EPERM;
	if (copy_from_user(&p, argp, sizeof(p)))
		return -EFAULT;
	if (!p.len || p.len > ES1869_PATTERN_MAX || !p.count ||
	    p.step >> 16 < 16)
		return -EINVAL;
	// alone it holds the machine, for what the streams' buffers last on a
	// machine of 400 MHz
	if ((p.flags & ES1869_PATTERN_ALONE) &&
	    (p.step >> 16) * p.count > 1ULL << 27)
		return -EINVAL;
	tab = memdup_array_user(u64_to_user_ptr(p.table), p.len, sizeof(*tab));
	if (IS_ERR(tab))
		return PTR_ERR(tab);
	err = 0;
	for (i = 0; i < p.len && !err; i++)
		if (!pattern_reg(p.bank, tab[i] >> 8))
			err = -EINVAL;
	if (!err)
		err = reg_pattern(chip, &p, tab);
	kfree(tab);
	if (copy_to_user(argp, &p, sizeof(p)))
		return -EFAULT;
	return err;
}

static void fill_info(struct es1869 *chip, struct es1869_regs_info *info)
{
	struct es1869_hw *hw = &chip->hw;
	unsigned long flags;
	int i;

	memset(info, 0, sizeof(*info));
	info->version = ES1869_REGS_VERSION;
	info->port = chip->port;
	info->cfg = hw->cfg;
	info->fm = chip->fm_port;
	info->mpu = chip->mpu_port;
	info->irq = chip->irq;
	info->dma1 = chip->dma1;
	info->dma2 = chip->dma2;
	memcpy(info->dsp, chip->ident.dsp, 2);
	memcpy(info->mixer_id, chip->ident.mixer, 2);
	info->a1_owner = chip->a1_owner;
	info->a2_owner = chip->a2_owner;
	info->settings_hash = es1869_settings_hash();
	spin_lock_irqsave(&chip->reg_lock, flags);
	for (i = 0; i < 4; i++)
		info->irqs[i] = hw->stats.irq[i];
	info->irq_none = hw->stats.irq_none;
	info->dsp_busy = hw->stats.dsp_busy;
	info->dsp_desync = hw->stats.dsp_desync;
	info->pos_retries = hw->stats.pos_retries;
	info->settles = hw->stats.settles;
	info->mirror_late = chip->mirror.fill.late;
	info->mirror_max = chip->mirror.max_chunk;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static int regs_ioctl(struct snd_hwdep *hw, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	struct es1869 *chip = hw->private_data;
	void __user *argp = (void __user *)arg;
	struct es1869_regs_info info;
	struct es1869_reg_block *blk;
	struct es1869_reg r;
	int err, i;

	switch (cmd) {
	case ES1869_IOCTL_PVERSION:
		return put_user(ES1869_REGS_VERSION, (int __user *)argp);
	case ES1869_IOCTL_INFO:
		fill_info(chip, &info);
		return copy_to_user(argp, &info, sizeof(info)) ? -EFAULT : 0;
	case ES1869_IOCTL_READ:
		if (copy_from_user(&r, argp, sizeof(r)))
			return -EFAULT;
		err = reg_read(chip, &r);
		if (err)
			return err;
		return copy_to_user(argp, &r, sizeof(r)) ? -EFAULT : 0;
	case ES1869_IOCTL_WRITE:
		if (copy_from_user(&r, argp, sizeof(r)))
			return -EFAULT;
		return reg_write(chip, &r);
	case ES1869_IOCTL_BLOCK_READ:
		blk = memdup_user(argp, sizeof(*blk));
		if (IS_ERR(blk))
			return PTR_ERR(blk);
		err = blk->count > 256 || blk->first + blk->count > 256 ?
			      -EINVAL : 0;
		for (i = 0; !err && i < blk->count; i++) {
			r = (struct es1869_reg){ .bank = blk->bank,
						 .ldn = blk->ldn,
						 .reg = blk->first + i,
						 .flags = blk->flags };
			err = reg_read(chip, &r);
			// a register that may not be read reads as FFh
			if (err == -EPERM)
				r.value = 0xFF, err = 0;
			blk->values[i] = r.value;
		}
		if (!err && copy_to_user(argp, blk, sizeof(*blk)))
			err = -EFAULT;
		kfree(blk);
		return err;
	case ES1869_IOCTL_PATTERN:
		return regs_pattern(chip, argp);
	}
	return -ENOIOCTLCMD;
}

int es1869_regs_hwdep_new(struct es1869 *chip)
{
	struct snd_hwdep *hw;
	int err;

	err = snd_hwdep_new(chip->card, "ES1869 regs", 2, &hw);
	if (err < 0)
		return err;
	strscpy(hw->name, "ES1869 registers");
	hw->iface = ES1869_HWDEP_IFACE_REGS;
	hw->private_data = chip;
	hw->ops.ioctl = regs_ioctl;
	hw->ops.ioctl_compat = regs_ioctl;
	chip->regs_hwdep = hw;
	return 0;
}
