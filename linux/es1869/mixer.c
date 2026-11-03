// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The mixer's dB scales and the controls that aren't settings: the
 * hardware volume counters the buttons move in split mode.
 *
 * Notes:
 *
 * The 4-bit levels mute at 0, then rise 3 dB a step up to level 8 and
 * 1.5 dB a step from there, each register with its own 0 dB point (DS
 * p.55, Table 21): 14 for Audio 1, 15 for Audio 2, 8 for the microphone
 * and the music DAC, 13 for the auxiliary inputs. The master is 0.75 dB a
 * step down from 0 dB at 63. The record level (B4h) goes 1.5 dB a step
 * from 0 dB for the microphone and from -6 dB for the other sources.
 *
 * Measured on the Armada through the chip's own record paths (es1869test's
 * mix group): Audio 2's playback level (7Ch) and its record level (69h)
 * follow Audio 2's table, 69h muting only at 0, the music DAC's record level
 * (6Bh) the microphone's, and the record level (B4h) its 1.5 dB steps all
 * the way down to 0, without a mute, within 0.6 dB.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/uaccess.h>
#include <sound/control.h>
#include <sound/tlv.h>

#include "es1869.h"

static const DECLARE_TLV_DB_RANGE(tlv_a1,
	0, 7, TLV_DB_SCALE_ITEM(-3300, 300, 1),
	8, 15, TLV_DB_SCALE_ITEM(-900, 150, 0));
static const DECLARE_TLV_DB_RANGE(tlv_a2,
	0, 7, TLV_DB_SCALE_ITEM(-3450, 300, 1),
	8, 15, TLV_DB_SCALE_ITEM(-1050, 150, 0));
static const DECLARE_TLV_DB_RANGE(tlv_mic,
	0, 7, TLV_DB_SCALE_ITEM(-2400, 300, 1),
	8, 15, TLV_DB_SCALE_ITEM(0, 150, 0));
static const DECLARE_TLV_DB_RANGE(tlv_aux,
	0, 7, TLV_DB_SCALE_ITEM(-3150, 300, 1),
	8, 15, TLV_DB_SCALE_ITEM(-750, 150, 0));
static const DECLARE_TLV_DB_SCALE(tlv_master, -4725, 75, 0);
// the PC speaker: mute, then -24 to -6 dB (DS p.26)
static const DECLARE_TLV_DB_SCALE(tlv_beep, -2700, 300, 1);
// the 3-D effect's boost near 450 Hz, as a card measured it
// (docs/SPATIALIZER.md): +16.8 dB at 3Fh
static const DECLARE_TLV_DB_SCALE(tlv_3d, -3045, 75, 0);
static const DECLARE_TLV_DB_SCALE(tlv_rec_mic, 0, 150, 0);
static const DECLARE_TLV_DB_SCALE(tlv_rec_line, -600, 150, 0);

const unsigned int *es1869_tlv(u8 tlv)
{
	switch (tlv) {
	case T_A1:
		return tlv_a1;
	case T_A2:
		return tlv_a2;
	case T_MIC:
		return tlv_mic;
	case T_AUX:
		return tlv_aux;
	case T_MASTER:
		return tlv_master;
	case T_BEEP:
		return tlv_beep;
	case T_3D:
		return tlv_3d;
	}
	return NULL;
}

// the record level's scale for the source: the microphone's (sources 0
// and 4) or the others'
int es1869_rec_tlv(struct snd_kcontrol *kctl, int op_flag, unsigned int size,
		   unsigned int __user *tlv)
{
	int src = es1869_val(ES_S_capture_source);
	const unsigned int *t = src == 0 || src == 4 ? tlv_rec_mic :
				tlv_rec_line;
	unsigned int len = t[SNDRV_CTL_TLVO_LEN] + 2 * sizeof(unsigned int);

	if (op_flag != SNDRV_CTL_TLV_OP_READ)
		return -ENXIO;
	if (size < len)
		return -ENOMEM;
	return copy_to_user(tlv, t, len) ? -EFAULT : 0;
}

// --- the hardware volume counters (61h/63h), which the buttons move -----

static int hwvol_info(struct snd_kcontrol *kctl,
		      struct snd_ctl_elem_info *uinfo)
{
	bool vol = kctl->private_value == 0;

	uinfo->type = vol ? SNDRV_CTL_ELEM_TYPE_INTEGER :
			    SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = vol ? 63 : 1;
	return 0;
}

static int hwvol_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uv)
{
	struct es1869 *chip = snd_kcontrol_chip(kctl);
	unsigned long flags;
	u8 l, r;

	spin_lock_irqsave(&chip->reg_lock, flags);
	l = es1869_mix_read(&chip->hw, 0x61);
	r = es1869_mix_read(&chip->hw, 0x63);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (kctl->private_value == 0) {
		uv->value.integer.value[0] = l & 0x3F;
		uv->value.integer.value[1] = r & 0x3F;
	} else {
		uv->value.integer.value[0] = !(l & 0x40);
		uv->value.integer.value[1] = !(r & 0x40);
	}
	return 0;
}

int es1869_mixer_extra(struct es1869 *chip)
{
	static const char *const names[2] = {
		"Hardware Master Playback Volume",
		"Hardware Master Playback Switch",
	};
	struct snd_kcontrol_new knew = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = hwvol_info,
		.get = hwvol_get,
	};
	int i, err;

	for (i = 0; i < 2; i++) {
		knew.name = names[i];
		knew.private_value = i;
		chip->hwvol_kctl[i] = snd_ctl_new1(&knew, chip);
		err = snd_ctl_add(chip->card, chip->hwvol_kctl[i]);
		if (err < 0)
			return err;
	}
	return 0;
}

// a button moved the master (or, split, the counters): the interrupt has
// read 60h-63h back into the shadows
void es1869_hwvol_notify(struct es1869 *chip)
{
	struct es1869_hw *hw = &chip->hw;
	int i;

	WRITE_ONCE(es1869_vals[ES_S_master_volume][0], hw->mix[0x60] & 0x3F);
	WRITE_ONCE(es1869_vals[ES_S_master_volume][1], hw->mix[0x62] & 0x3F);
	WRITE_ONCE(es1869_vals[ES_S_master_switch][0], !(hw->mix[0x60] & 0x40));
	WRITE_ONCE(es1869_vals[ES_S_master_switch][1], !(hw->mix[0x62] & 0x40));
	if (chip->kctl[ES_S_master_volume])
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &chip->kctl[ES_S_master_volume]->id);
	if (chip->kctl[ES_S_master_switch])
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &chip->kctl[ES_S_master_switch]->id);
	for (i = 0; i < 2; i++)
		if (chip->hwvol_kctl[i])
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->hwvol_kctl[i]->id);
}
