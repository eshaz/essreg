// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The ES1869's FM: the OPL3 emulation through snd-opl3-lib (hwdep 0 and
 * the sequencer's "OPL3 FM" synth), and the native ESFM mode, for the
 * ESFM hwdep (hwdep 1) and the GM synthesizer snd-es1869-esfm.
 *
 * Notes:
 *
 * FM_Base answers at Audio_Base+0-3 and at its alias, 388h on the Armada
 * (PnP LDN 1 62h/63h). The FM is heard only with the music DAC on FM (7Fh
 * bit 0 clear, the "Video Playback Switch" off) and 36h above 0.
 *
 * One user has the FM at a time: the OPL3 hwdep, the OPL3 synth, the ESFM
 * hwdep or the ESFM synth. The token is the OPL3 hwdep's use count, under
 * its open mutex, which the OPL3 library's hwdep and synth take already.
 * The native mode lasts while the ESFM side holds it; its release resets
 * the FM back to the OPL3 emulation the library expects.
 *
 * The chip plays an operator's bit 4 of C0h-C8h (an OPL3's channel A, its
 * left) or of native register 6 on its right output, and bit 5 on its
 * left, as recorded digitally and through the output. With fm_sides the
 * OPL3 library's writes get the two bits swapped, and the ESFM synth pans
 * left with bit 5, so that both pan as on an OPL3. The ESFM hwdep writes
 * what it is given.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/uaccess.h>
#include <sound/hwdep.h>
#include <sound/opl3.h>
#include <sound/seq_device.h>

#include "es1869.h"
#include "uapi/es1869.h"

// a chip fully down wakes before the hwdep is opened: the open waits in the
// hwdep's own loop, with its mutex free for the wake to finish under
static int es1869_opl3_open(struct snd_hwdep *hw, struct file *file)
{
	struct snd_opl3 *opl3 = hw->private_data;
	struct es1869 *chip = opl3->private_data;

	if (READ_ONCE(chip->pwr_down) == 2 && !READ_ONCE(chip->pwr_stopped)) {
		schedule_work(&chip->wake_work);
		return -EAGAIN;
	}
	return chip->opl3_open(hw, file);
}

// the OPL3 library's register writes, with C0h-C8h's bits 4 and 5
// swapped while fm_sides is on
static void es1869_opl3_command(struct snd_opl3 *opl3, unsigned short cmd,
				unsigned char val)
{
	struct es1869 *chip = opl3->private_data;
	u8 reg = cmd & 0xFF, side = val & 0x30;

	// the OPL3 synth takes events without a subscriber, and a write to
	// FM_Base+0 ends the native mode under whoever has it
	if (READ_ONCE(chip->esfm_held))
		return;
	// a chip fully down loses it, and the synth has no open that could
	// wake it; what it writes from then on counts
	if (READ_ONCE(chip->pwr_down) == 2) {
		if (!READ_ONCE(chip->pwr_stopped))
			schedule_work(&chip->wake_work);
		return;
	}
	if (reg >= 0xC0 && reg <= 0xC8 && (side == 0x10 || side == 0x20) &&
	    es1869_val(ES_S_fm_sides))
		val ^= 0x30;
	chip->opl3_command(opl3, cmd, val);
}

int es1869_fm_new(struct es1869 *chip)
{
	int err;

	if (!chip->fm_port)
		return 0;
	if (!devm_request_region(chip->dev, chip->fm_port, 4, "ES1869 FM")) {
		dev_warn(chip->dev, "FM port 0x%lx is in use\n", chip->fm_port);
		return 0;
	}
	err = snd_opl3_create(chip->card, chip->fm_port, chip->fm_port + 2,
			      OPL3_HW_OPL3, 1, &chip->opl3);
	if (err < 0) {
		dev_warn(chip->dev, "no OPL3 at 0x%lx\n", chip->fm_port);
		return 0;
	}
	chip->opl3->private_data = chip;
	chip->opl3_command = chip->opl3->command;
	chip->opl3->command = es1869_opl3_command;
	err = snd_opl3_hwdep_new(chip->opl3, 0, 1, NULL);
	if (err < 0)
		return err;
	chip->opl3_open = chip->opl3->hwdep->ops.open;
	chip->opl3->hwdep->ops.open = es1869_opl3_open;
	return 0;
}

// the token's count that opl3=0 keeps, under state_mutex: the OPL3 hwdep
// and synth find it in use, and the ESFM's claim looks past it
void es1869_opl3_gate(struct es1869 *chip)
{
	struct snd_hwdep *hw = chip->opl3 ? chip->opl3->hwdep : NULL;
	unsigned int off = !es1869_val(ES_S_opl3);

	if (!hw || off == chip->opl3_off)
		return;
	mutex_lock_nested(&hw->open_mutex, SINGLE_DEPTH_NESTING);
	hw->used += off ? 1 : -1;
	chip->opl3_off = off;
	mutex_unlock(&hw->open_mutex);
	wake_up(&hw->open_wait);
}

// the FM as its owner left it at a suspend: back in its OPL3 emulation mode,
// as the OPL3 library expects it, or in the native mode, whose user is told
void es1869_fm_resume(struct es1869 *chip)
{
	unsigned long flags;

	if (READ_ONCE(chip->esfm_held)) {
		es1869_esfm_reset(chip);
		mutex_lock(&chip->state_mutex);
		if (chip->esfm_ops)
			chip->esfm_ops->resume(chip->esfm_data);
		mutex_unlock(&chip->state_mutex);
		return;
	}
	spin_lock_irqsave(&chip->reg_lock, flags);
	es1869_fm_reset(&chip->hw);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (chip->opl3)
		snd_opl3_init(chip->opl3);
}

// --- the native ESFM --------------------------------------------------------

// a reset, the native mode, and every voice keyed off
void es1869_esfm_reset(struct es1869 *chip)
{
	unsigned long flags;
	u16 r;

	spin_lock_irqsave(&chip->reg_lock, flags);
	es1869_fm_reset(&chip->hw);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	spin_lock_irqsave(&chip->fm_lock, flags);
	es1869_fm_native(&chip->hw);
	for (r = 0x240; r <= 0x253; r++)
		es1869_fm_write(&chip->hw, r, 0);
	spin_unlock_irqrestore(&chip->fm_lock, flags);
}
EXPORT_SYMBOL_GPL(es1869_esfm_reset);

int es1869_esfm_claim(struct es1869 *chip)
{
	struct snd_hwdep *hw = chip->opl3 ? chip->opl3->hwdep : NULL;

	if (!hw)
		return -ENODEV;
	if (!es1869_val(ES_S_esfm))
		return -EPERM;
	scoped_guard(mutex, &chip->state_mutex) {
		int err = es1869_power_up(chip);

		if (err)
			return err;
		// inside the ESFM hwdep's own open_mutex, of the same class
		mutex_lock_nested(&hw->open_mutex, SINGLE_DEPTH_NESTING);
		if (hw->used > chip->opl3_off) {
			mutex_unlock(&hw->open_mutex);
			return -EBUSY;
		}
		hw->used++;
		mutex_unlock(&hw->open_mutex);
		WRITE_ONCE(chip->esfm_held, true);
	}
	es1869_esfm_reset(chip);
	return 0;
}
EXPORT_SYMBOL_GPL(es1869_esfm_claim);

void es1869_esfm_release(struct es1869 *chip)
{
	struct snd_hwdep *hw = chip->opl3->hwdep;

	WRITE_ONCE(chip->esfm_held, false);
	es1869_fm_resume(chip);
	mutex_lock_nested(&hw->open_mutex, SINGLE_DEPTH_NESTING);
	if (hw->used > 0)
		hw->used--;
	mutex_unlock(&hw->open_mutex);
	wake_up(&hw->open_wait);
	es1869_power_idle(chip);
}
EXPORT_SYMBOL_GPL(es1869_esfm_release);

// the synth's callbacks, NULL when it goes
void es1869_esfm_set_ops(struct es1869 *chip,
			 const struct es1869_esfm_ops *ops, void *data)
{
	mutex_lock(&chip->state_mutex);
	chip->esfm_ops = ops;
	chip->esfm_data = data;
	mutex_unlock(&chip->state_mutex);
}
EXPORT_SYMBOL_GPL(es1869_esfm_set_ops);

// esfm_bank or esfm_square written, under state_mutex
void es1869_esfm_bank_changed(struct es1869 *chip)
{
	if (chip->esfm_ops)
		chip->esfm_ops->bank(chip->esfm_data);
}

void es1869_esfm_write(struct es1869 *chip, u16 reg, u8 val)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->fm_lock, flags);
	es1869_fm_write(&chip->hw, reg, val);
	spin_unlock_irqrestore(&chip->fm_lock, flags);
}
EXPORT_SYMBOL_GPL(es1869_esfm_write);

u8 es1869_esfm_read(struct es1869 *chip, u16 reg)
{
	unsigned long flags;
	u8 val;

	spin_lock_irqsave(&chip->fm_lock, flags);
	val = es1869_fm_read(&chip->hw, reg);
	spin_unlock_irqrestore(&chip->fm_lock, flags);
	return val;
}
EXPORT_SYMBOL_GPL(es1869_esfm_read);

// --- the ESFM hwdep -----------------------------------------------------------

static int esfm_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
	return es1869_esfm_claim(hw->private_data);
}

static int esfm_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	es1869_esfm_release(hw->private_data);
	return 0;
}

static int esfm_hwdep_write_regs(struct es1869 *chip, void __user *arg)
{
	struct es1869_esfm_regs w;
	struct es1869_esfm_reg r;
	struct es1869_esfm_reg __user *p;
	u32 i;

	if (copy_from_user(&w, arg, sizeof(w)))
		return -EFAULT;
	if (w.count > ES1869_ESFM_MAX)
		return -EINVAL;
	p = u64_to_user_ptr(w.regs);
	for (i = 0; i < w.count; i++) {
		if (copy_from_user(&r, p + i, sizeof(r)))
			return -EFAULT;
		if (r.reg > 0x7FF)
			return -EINVAL;
		es1869_esfm_write(chip, r.reg, r.value);
		cond_resched();
	}
	return 0;
}

static int esfm_hwdep_ioctl(struct snd_hwdep *hw, struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	struct es1869 *chip = hw->private_data;
	void __user *argp = (void __user *)arg;
	struct es1869_esfm_reg r;

	switch (cmd) {
	case ES1869_ESFM_IOCTL_PVERSION:
		return put_user(ES1869_ESFM_VERSION, (int __user *)argp);
	case ES1869_ESFM_IOCTL_RESET:
		es1869_esfm_reset(chip);
		return 0;
	case ES1869_ESFM_IOCTL_WRITE:
		return esfm_hwdep_write_regs(chip, argp);
	case ES1869_ESFM_IOCTL_READ:
		if (copy_from_user(&r, argp, sizeof(r)))
			return -EFAULT;
		if (r.reg > 0x7FF)
			return -EINVAL;
		r.value = es1869_esfm_read(chip, r.reg);
		return copy_to_user(argp, &r, sizeof(r)) ? -EFAULT : 0;
	}
	return -ENOTTY;
}

// hwdep 1, and the sequencer device that snd-es1869-esfm binds to
int es1869_esfm_new(struct es1869 *chip)
{
	struct snd_hwdep *hw;
	int err;

	if (!chip->opl3)
		return 0;
	err = snd_hwdep_new(chip->card, "ES1869 ESFM", 1, &hw);
	if (err < 0)
		return err;
	strscpy(hw->name, "ES1869 ESFM");
	hw->iface = ES1869_HWDEP_IFACE_ESFM;
	hw->exclusive = 1;
	hw->private_data = chip;
	hw->ops.open = esfm_hwdep_open;
	hw->ops.release = esfm_hwdep_release;
	hw->ops.ioctl = esfm_hwdep_ioctl;
	hw->ops.ioctl_compat = esfm_hwdep_ioctl;
	chip->esfm_hwdep = hw;
#if IS_ENABLED(CONFIG_SND_SEQUENCER)
	if (snd_seq_device_new(chip->card, 2, ES1869_ESFM_SEQ_ID,
			       sizeof(struct es1869 *), &chip->esfm_seq) >= 0)
		*(struct es1869 **)SNDRV_SEQ_DEVICE_ARGPTR(chip->esfm_seq) = chip;
#endif
	return 0;
}
