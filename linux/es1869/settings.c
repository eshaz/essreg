// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The settings of settings.def as module parameters, which can be written
 * at runtime through /sys/module/snd_es1869/parameters, and as ALSA
 * controls, both on the same module-wide values.
 *
 * Notes:
 *
 * A parameter written at load only sets the value, which each card then
 * applies as it starts. The GPO pins and the PC speaker's level stay as
 * the chip has them unless a parameter sets them. Written later, it goes to
 * every card at once,
 * under the card list's lock and each card's state mutex, and the card's
 * control announces it. A control's put sets the module-wide value too,
 * so a parameter reads what the controls last set: on a laptop with one
 * card the two are the same thing. The master's value also follows the
 * hardware volume buttons, which the interrupt reads back, and which its
 * control reads from the chip, since the buttons move the master with
 * their interrupt off the line too. So does the PC speaker's, which a
 * notebook's BIOS may move from its Fn keys.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <sound/control.h>
#include <sound/tlv.h>

#include "es1869.h"
#include "oot_config.h"

// a setting's value at load: its CONFIG_SND_ES1869_DEFAULT_<ID>
#define ES_DEF_S_BOOL(ID) CONFIG_SND_ES1869_DEFAULT_##ID
#define ES_DEF_S_INT(ID) CONFIG_SND_ES1869_DEFAULT_##ID
#define ES_DEF_S_ENUM(ID) CONFIG_SND_ES1869_DEFAULT_##ID
#define ES_DEF_S_STEREO(ID) CONFIG_SND_ES1869_DEFAULT_##ID
#define ES_DEF_S_MASTER(ID) CONFIG_SND_ES1869_DEFAULT_##ID
#define ES_DEF_S_MASTERSW(ID) CONFIG_SND_ES1869_DEFAULT_##ID
#define ES_DEF_S_SMAG(ID) CONFIG_SND_ES1869_DEFAULT_##ID
#define ES_DEF_S_STR(ID) 0

const struct es1869_setting es1869_settings[ES_S_COUNT] = {
#define ES1869_SET(id, ID, kind, bank, reg, shift, width, min, max, def, tlv, \
		   flags, index, ctl, items, desc)                            \
	[ES_S_##id] = { #id,   kind,  bank,		 reg,  shift,         \
			width, tlv,   index,		 flags, min,          \
			max,   ES_DEF_##kind(ID), ctl,	 items, desc },
#include "settings.def"
#undef ES1869_SET
};

int es1869_vals[ES_S_COUNT][2] = {
#define ES1869_SET(id, ID, kind, bank, reg, shift, width, min, max, def, tlv, \
		   flags, index, ctl, items, desc)                            \
	[ES_S_##id] = { ES_DEF_##kind(ID), ES_DEF_##kind(ID) },
#include "settings.def"
#undef ES1869_SET
};
EXPORT_SYMBOL_GPL(es1869_vals);

// the settings a parameter set at load
static bool es1869_given[ES_S_COUNT];

static char es1869_bank[64] = CONFIG_SND_ES1869_DEFAULT_ESFM_BANK;

const char *es1869_bank_name(void)
{
	return es1869_bank;
}
EXPORT_SYMBOL_GPL(es1869_bank_name);

static LIST_HEAD(es1869_cards);
static DEFINE_MUTEX(es1869_cards_lock);

// an enum's texts, split from its "a|b|c" once
#define ES_ENUM_MAX 8
static const char *es1869_items[ES_S_COUNT][ES_ENUM_MAX];
static int es1869_nitems[ES_S_COUNT];
static char es1869_item_buf[512];

static void es1869_split_items(void)
{
	size_t used = 0;
	int id;

	if (es1869_nitems[ES_S_capture_source])
		return;
	for (id = 0; id < ES_S_COUNT; id++) {
		const struct es1869_setting *s = &es1869_settings[id];
		char *p, *tok;
		size_t len;

		if (s->kind != S_ENUM)
			continue;
		len = strlen(s->items) + 1;
		if (used + len > sizeof(es1869_item_buf))
			break;
		p = memcpy(es1869_item_buf + used, s->items, len);
		used += len;
		while ((tok = strsep(&p, "|")) && es1869_nitems[id] < ES_ENUM_MAX)
			es1869_items[id][es1869_nitems[id]++] = tok;
	}
}

u32 es1869_settings_hash(void)
{
	u32 h = 2166136261u;
	const char *p;
	int id;

	for (id = 0; id < ES_S_COUNT; id++) {
		for (p = es1869_settings[id].name; *p; p++)
			h = (h ^ (u8)*p) * 16777619u;
		h = (h ^ ',') * 16777619u;
	}
	return h;
}

// --- the chip ----------------------------------------------------------------

static u8 field_mask(const struct es1869_setting *s)
{
	return ((1U << s->width) - 1) << s->shift;
}

// a sign-and-magnitude ADC offset (BAh/BBh bits 4:0): +64 a step for
// 0-15, -64 times (n + 1) with bit 4
static u8 smag_code(int v)
{
	return v >= 0 ? v : 0x10 | (-v - 1);
}

static int smag_value(u8 code)
{
	return code & 0x10 ? -(int)(code & 0x0F) - 1 : code & 0x0F;
}

static void apply_hook(struct es1869 *chip, int id)
{
	struct es1869_hw *hw = &chip->hw;
	u8 low;

	switch (id) {
	case ES_S_self_power_down:
		// never through the shadow: BDh can't be read back, and goes
		// with the power. A partial power-down only, which any access
		// leaves: a full one would need a wake nothing here sees
		if (es1869_val(id))
			es1869_port7_write(hw, hw->port7 | 0x08);
		es1869_ctrl_write(hw, 0xBD, es1869_val(id));
		break;
	case ES_S_spatial_switch:
		// 50h bit 2 holds the effect in reset (active low): on, it goes
		// enabled in reset, then out of it; off, as Linux has written
		// it (00h, then 04h); bits 1:0 kept
		low = hw->mix[0x50] & 0x03;
		if (es1869_val(id)) {
			es1869_mix_write(hw, 0x50, low | 0x08);
			es1869_mix_write(hw, 0x50, low | 0x0C);
		} else {
			es1869_mix_write(hw, 0x50, low);
			es1869_mix_write(hw, 0x50, low | 0x04);
		}
		break;
	case ES_S_adc_offset:
		es1869_ctrl_update(hw, 0xBA, 0x1F, smag_code(es1869_val2(id, 0)));
		es1869_ctrl_update(hw, 0xBB, 0x1F, smag_code(es1869_val2(id, 1)));
		break;
	case ES_S_irq_sources:
	case ES_S_irq_mode:
		es1869_irq_sources_locked(chip);
		break;
	case ES_S_piix4_type_f:
		es1869_type_f_locked(chip);
		break;
	case ES_S_gpo_power_down: {
		u8 cmd[2] = { 0xCF, es1869_val(id) };

		es1869_dsp_cmd(hw, cmd, 2);
		break;
	}
	}
}

// a setting's value to the chip, under the register lock
static void apply_locked(struct es1869 *chip, int id)
{
	const struct es1869_setting *s = &es1869_settings[id];
	struct es1869_hw *hw = &chip->hw;
	int l = es1869_val2(id, 0), r = es1869_val2(id, 1);
	u8 mask = field_mask(s), v;

	if (s->flags & F_HOOK) {
		apply_hook(chip, id);
		return;
	}
	switch (s->kind) {
	case S_STEREO:
		mask = 0xFF;
		v = s->flags & F_SWAP ? (r << 4) | l : (l << 4) | r;
		break;
	case S_MASTER:
	case S_MASTERSW:
		// 60h and 62h move with the buttons: from the chip, not the shadow
		hw->mix[0x60] = es1869_mix_read(hw, 0x60);
		hw->mix[0x62] = es1869_mix_read(hw, 0x62);
		if (s->kind == S_MASTER) {
			es1869_mix_update(hw, 0x60, 0x3F, l);
			es1869_mix_update(hw, 0x62, 0x3F, r);
		} else {
			es1869_mix_update(hw, 0x60, 0x40, l ? 0 : 0x40);
			es1869_mix_update(hw, 0x62, 0x40, r ? 0 : 0x40);
		}
		return;
	case S_BOOL:
		v = (!!l ^ !!(s->flags & F_INV)) << s->shift;
		break;
	default:
		v = (l << s->shift) & mask;
		break;
	}
	switch (s->bank) {
	case B_MIX:
		es1869_mix_update(hw, s->reg, mask, v);
		break;
	case B_CTRL:
		es1869_ctrl_update(hw, s->reg, mask, v);
		break;
	case B_PORT7:
		es1869_port7_write(hw, (hw->port7 & ~mask) | v);
		break;
	case B_PNPC: {
		u8 old = es1869_pnp_read(hw, 0xFF, s->reg);

		es1869_pnp_write(hw, 0xFF, s->reg, (old & ~mask) | v);
		if ((es1869_pnp_read(hw, 0xFF, s->reg) & mask) != v)
			dev_warn_once(chip->dev, "%s doesn't stick on this card\n",
				      s->name);
		break;
	}
	}
}

void es1869_setting_apply(struct es1869 *chip, int id)
{
	unsigned long flags;

	// a DSP left out of step refuses what follows until it is reset
	if (chip->hw.desync && !chip->recovering && !es1869_a1_busy(chip) &&
	    es1869_settings[id].bank == B_CTRL && es1869_dsp_recover(chip))
		return;
	spin_lock_irqsave(&chip->reg_lock, flags);
	apply_locked(chip, id);
	if (id == ES_S_esfm_delay)
		chip->hw.fm_delay = es1869_val(id);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	switch (id) {
	case ES_S_capture_source:
		// the record level's dB scale follows the source
		if (chip->kctl[ES_S_capture_volume])
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_TLV,
				       &chip->kctl[ES_S_capture_volume]->id);
		break;
	case ES_S_pwr_save:
		es1869_power_save_changed(chip);
		break;
	case ES_S_opl3:
		es1869_opl3_gate(chip);
		break;
	case ES_S_esfm_square:
		es1869_esfm_bank_changed(chip);
		break;
	}
}

void es1869_settings_apply_init(struct es1869 *chip)
{
	int id;

	for (id = 0; id < ES_S_COUNT; id++)
		if (es1869_settings[id].flags & F_INIT)
			es1869_setting_apply(chip, id);
}

// those on controller registers, after a reset the driver had to make
void es1869_settings_apply_ctrl(struct es1869 *chip)
{
	int id;

	for (id = 0; id < ES_S_COUNT; id++)
		if ((es1869_settings[id].flags & F_INIT) &&
		    es1869_settings[id].bank == B_CTRL)
			es1869_setting_apply(chip, id);
}

// a setting that can't change while an engine runs
static bool setting_busy(struct es1869 *chip, int id)
{
	unsigned int flags = es1869_settings[id].flags;

	return ((flags & F_IDLE) && chip->a2_owner != ES_OWN_NONE) ||
	       ((flags & F_IDLE_A1) && chip->a1_owner != ES_OWN_NONE);
}

// the value a register written through the register hwdep gives a setting
static void setting_from_shadow(struct es1869 *chip, int id)
{
	const struct es1869_setting *s = &es1869_settings[id];
	struct es1869_hw *hw = &chip->hw;
	u8 raw;

	if (s->bank == B_MIX)
		raw = hw->mix[s->reg];
	else if (s->bank == B_CTRL)
		raw = hw->ctrl[s->reg - 0xA0];
	else if (s->bank == B_PORT7)
		raw = hw->port7;
	else
		return;
	switch (s->kind) {
	case S_STEREO:
		WRITE_ONCE(es1869_vals[id][0], s->flags & F_SWAP ? raw & 15 : raw >> 4);
		WRITE_ONCE(es1869_vals[id][1], s->flags & F_SWAP ? raw >> 4 : raw & 15);
		break;
	case S_MASTER:
		WRITE_ONCE(es1869_vals[id][0], hw->mix[0x60] & 0x3F);
		WRITE_ONCE(es1869_vals[id][1], hw->mix[0x62] & 0x3F);
		break;
	case S_MASTERSW:
		WRITE_ONCE(es1869_vals[id][0], !(hw->mix[0x60] & 0x40));
		WRITE_ONCE(es1869_vals[id][1], !(hw->mix[0x62] & 0x40));
		break;
	case S_SMAG:
		WRITE_ONCE(es1869_vals[id][0], smag_value(hw->ctrl[0x1A]));
		WRITE_ONCE(es1869_vals[id][1], smag_value(hw->ctrl[0x1B]));
		break;
	case S_BOOL:
		WRITE_ONCE(es1869_vals[id][0],
			   !!(raw & field_mask(s)) ^ !!(s->flags & F_INV));
		break;
	default:
		WRITE_ONCE(es1869_vals[id][0],
			   (raw & field_mask(s)) >> s->shift);
		break;
	}
}

// at load, before the settings are applied: what the chip holds, for those
// that keep it unless a parameter gave another value
void es1869_settings_found(struct es1869 *chip)
{
	int id;

	for (id = 0; id < ES_S_COUNT; id++)
		if ((es1869_settings[id].flags & F_FOUND) && !es1869_given[id])
			setting_from_shadow(chip, id);
}

// what moves without the driver, as the chip has it: the buttons move the
// master by themselves, and their interrupt, which reads it back, may be off
// the line, and the BIOS of the Armada writes the PC speaker's level from
// its Fn keys
void es1869_settings_sync(struct es1869 *chip)
{
	struct es1869_hw *hw = &chip->hw;
	unsigned long flags;

	if (chip->dead || chip->pwr_down == 2)
		return;
	spin_lock_irqsave(&chip->reg_lock, flags);
	hw->mix[0x60] = es1869_mix_read(hw, 0x60);
	hw->mix[0x62] = es1869_mix_read(hw, 0x62);
	hw->mix[0x3C] = es1869_mix_read(hw, 0x3C);
	setting_from_shadow(chip, ES_S_master_volume);
	setting_from_shadow(chip, ES_S_master_switch);
	setting_from_shadow(chip, ES_S_beep_volume);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static bool moves_by_itself(int id)
{
	return es1869_settings[id].kind == S_MASTER ||
	       es1869_settings[id].kind == S_MASTERSW || id == ES_S_beep_volume;
}

// after the register hwdep wrote a register: the settings on it follow
void es1869_settings_notify_reg(struct es1869 *chip, u8 bank, u8 reg)
{
	int id;

	for (id = 0; id < ES_S_COUNT; id++) {
		const struct es1869_setting *s = &es1869_settings[id];
		bool hit = s->bank == bank && s->reg == reg;

		if (bank == B_MIX && (reg == 0x60 || reg == 0x62) &&
		    (s->kind == S_MASTER || s->kind == S_MASTERSW))
			hit = true;
		if (bank == B_CTRL && (reg == 0xBA || reg == 0xBB) &&
		    s->kind == S_SMAG)
			hit = true;
		if (!hit)
			continue;
		setting_from_shadow(chip, id);
		if (chip->kctl[id])
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->kctl[id]->id);
		if (id == ES_S_capture_source && chip->kctl[ES_S_capture_volume])
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_TLV,
				       &chip->kctl[ES_S_capture_volume]->id);
	}
}

// --- the card list -------------------------------------------------------------

void es1869_settings_card_add(struct es1869 *chip)
{
	mutex_lock(&es1869_cards_lock);
	list_add_tail(&chip->list, &es1869_cards);
	chip->listed = true;
	mutex_unlock(&es1869_cards_lock);
}

void es1869_settings_card_del(struct es1869 *chip)
{
	mutex_lock(&es1869_cards_lock);
	if (chip->listed)
		list_del(&chip->list);
	chip->listed = false;
	mutex_unlock(&es1869_cards_lock);
}

// a value for every card: refused if one is busy with it, else set, applied
// and announced
static int es1869_set_all(int id, const int v[2])
{
	struct es1869 *chip;
	int err = 0;

	mutex_lock(&es1869_cards_lock);
	list_for_each_entry(chip, &es1869_cards, list) {
		mutex_lock(&chip->state_mutex);
		if (setting_busy(chip, id))
			err = -EBUSY;
		mutex_unlock(&chip->state_mutex);
	}
	if (!err) {
		WRITE_ONCE(es1869_vals[id][0], v[0]);
		WRITE_ONCE(es1869_vals[id][1], v[1]);
		list_for_each_entry(chip, &es1869_cards, list) {
			mutex_lock(&chip->state_mutex);
			if (!chip->dead)
				es1869_setting_apply(chip, id);
			mutex_unlock(&chip->state_mutex);
			if (chip->kctl[id])
				snd_ctl_notify(chip->card,
					       SNDRV_CTL_EVENT_MASK_VALUE,
					       &chip->kctl[id]->id);
		}
	}
	mutex_unlock(&es1869_cards_lock);
	return err;
}

// --- the parameters ------------------------------------------------------------

static int item_index(int id, const char *text)
{
	int i;

	es1869_split_items();
	for (i = 0; i < es1869_nitems[id]; i++)
		if (sysfs_streq(es1869_items[id][i], text))
			return i;
	return -EINVAL;
}

// "v", or "l,r" for the two-channel kinds; an enum by number or by text,
// a switch as y/n/1/0
static int es1869_parse(int id, const char *val, int v[2])
{
	const struct es1869_setting *s = &es1869_settings[id];
	char buf[32], *p = buf, *tok;
	int n = 0, i;
	bool b;

	if (strscpy(buf, val, sizeof(buf)) < 0)
		return -EINVAL;
	strim(buf);
	while ((tok = strsep(&p, ",")) && n < 2) {
		tok = strim(tok);
		if (s->kind == S_BOOL || s->kind == S_MASTERSW) {
			if (kstrtobool(tok, &b))
				return -EINVAL;
			v[n] = b;
		} else if (kstrtoint(tok, 0, &v[n])) {
			if (s->kind != S_ENUM)
				return -EINVAL;
			v[n] = item_index(id, tok);
			if (v[n] < 0)
				return -EINVAL;
		}
		n++;
	}
	if (tok || !n || (n == 2 && es1869_set_count(s) == 1))
		return -EINVAL;
	if (n == 1)
		v[1] = v[0];
	for (i = 0; i < 2; i++)
		if (v[i] < s->min || v[i] > s->max)
			return -ERANGE;
	return 0;
}

static int es1869_param_set(const char *val, const struct kernel_param *kp)
{
	const struct es1869_setting *s = kp->arg;
	int id = s - es1869_settings;
	int v[2], err;

	if (s->kind == S_STR) {
		struct es1869 *chip;

		if (strscpy(es1869_bank, val, sizeof(es1869_bank)) < 0)
			return -ENAMETOOLONG;
		strim(es1869_bank);
		mutex_lock(&es1869_cards_lock);
		list_for_each_entry(chip, &es1869_cards, list) {
			mutex_lock(&chip->state_mutex);
			es1869_esfm_bank_changed(chip);
			mutex_unlock(&chip->state_mutex);
		}
		mutex_unlock(&es1869_cards_lock);
		return 0;
	}
	err = es1869_parse(id, val, v);
	if (err)
		return err;
	es1869_given[id] = true;
	return es1869_set_all(id, v);
}

int es1869_setting_format(int id, char *buf, size_t size)
{
	const struct es1869_setting *s = &es1869_settings[id];

	if (s->kind == S_STR)
		return scnprintf(buf, size, "%s", es1869_bank);
	if (es1869_set_count(s) == 2)
		return scnprintf(buf, size, "%d,%d", es1869_val2(id, 0),
				 es1869_val2(id, 1));
	return scnprintf(buf, size, "%d", es1869_val(id));
}

static int es1869_param_get(char *buf, const struct kernel_param *kp)
{
	const struct es1869_setting *s = kp->arg;
	int n = es1869_setting_format(s - es1869_settings, buf, PAGE_SIZE - 1);

	buf[n++] = '\n';
	buf[n] = 0;
	return n;
}

static const struct kernel_param_ops es1869_param_ops = {
	.set = es1869_param_set,
	.get = es1869_param_get,
};

#define ES1869_SET(id, ID, kind, bank, reg, shift, width, min, max, def, tlv, \
		   flags, index, ctl, items, desc)                            \
	module_param_cb(id, &es1869_param_ops,                                \
			(void *)&es1869_settings[ES_S_##id], 0644);           \
	MODULE_PARM_DESC(id, desc);
#include "settings.def"
#undef ES1869_SET

// --- the controls ----------------------------------------------------------------

static int es1869_ctl_info(struct snd_kcontrol *kctl,
			   struct snd_ctl_elem_info *uinfo)
{
	int id = kctl->private_value;
	const struct es1869_setting *s = &es1869_settings[id];

	if (s->kind == S_ENUM)
		return snd_ctl_enum_info(uinfo, 1, es1869_nitems[id],
					 es1869_items[id]);
	uinfo->count = es1869_set_count(s);
	if (s->kind == S_BOOL || s->kind == S_MASTERSW) {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = 1;
	} else {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->value.integer.min = s->min;
		uinfo->value.integer.max = s->max;
	}
	return 0;
}

static int es1869_ctl_get(struct snd_kcontrol *kctl,
			  struct snd_ctl_elem_value *uv)
{
	int id = kctl->private_value;
	const struct es1869_setting *s = &es1869_settings[id];
	int i;

	if (moves_by_itself(id))
		es1869_settings_sync(snd_kcontrol_chip(kctl));
	if (s->kind == S_ENUM) {
		uv->value.enumerated.item[0] = es1869_val(id);
		return 0;
	}
	for (i = 0; i < es1869_set_count(s); i++)
		uv->value.integer.value[i] = es1869_val2(id, i);
	return 0;
}

static int es1869_ctl_put(struct snd_kcontrol *kctl,
			  struct snd_ctl_elem_value *uv)
{
	struct es1869 *chip = snd_kcontrol_chip(kctl);
	int id = kctl->private_value;
	const struct es1869_setting *s = &es1869_settings[id];
	int v[2], i, n = es1869_set_count(s);

	if (s->kind == S_ENUM) {
		v[0] = v[1] = uv->value.enumerated.item[0];
	} else {
		for (i = 0; i < n; i++)
			v[i] = uv->value.integer.value[i];
		if (n == 1)
			v[1] = v[0];
	}
	for (i = 0; i < 2; i++)
		if (v[i] < s->min || v[i] > s->max)
			return -EINVAL;
	if (moves_by_itself(id))
		es1869_settings_sync(chip);
	if (v[0] == es1869_val2(id, 0) && v[1] == es1869_val2(id, 1))
		return 0;
	mutex_lock(&chip->state_mutex);
	if (setting_busy(chip, id)) {
		mutex_unlock(&chip->state_mutex);
		return -EBUSY;
	}
	WRITE_ONCE(es1869_vals[id][0], v[0]);
	WRITE_ONCE(es1869_vals[id][1], v[1]);
	if (!chip->dead)
		es1869_setting_apply(chip, id);
	mutex_unlock(&chip->state_mutex);
	return 1;
}

int es1869_settings_controls(struct es1869 *chip)
{
	struct snd_kcontrol_new knew = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.info = es1869_ctl_info,
		.get = es1869_ctl_get,
		.put = es1869_ctl_put,
	};
	int id, err;

	es1869_split_items();
	for (id = 0; id < ES_S_COUNT; id++) {
		const struct es1869_setting *s = &es1869_settings[id];
		struct snd_kcontrol *kctl;

		if (!(s->flags & F_CTL))
			continue;
		knew.name = s->ctl;
		knew.index = s->index;
		knew.private_value = id;
		knew.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
		knew.tlv.p = NULL;
		if (s->tlv == T_REC) {
			knew.access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ |
				       SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK;
			knew.tlv.c = es1869_rec_tlv;
		} else if (s->tlv != T_NONE) {
			knew.access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;
			knew.tlv.p = es1869_tlv(s->tlv);
		}
		kctl = snd_ctl_new1(&knew, chip);
		err = snd_ctl_add(chip->card, kctl);
		if (err < 0)
			return err;
		chip->kctl[id] = kctl;
	}
	return 0;
}
