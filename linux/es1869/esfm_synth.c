// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * snd-es1869-esfm: a sequencer synthesizer on the ES1869's FM in its native
 * ESFM mode, playing General MIDI as ESS's fixed ESFM.DRV does (esfm_gm.c,
 * docs/ESFM_ENGINE.md), with ESS's GM bank read as firmware.
 *
 * Notes:
 *
 * The synth takes the FM (the token in esfm.c) when its port is first
 * used, and gives it back when the last user leaves, so that the OPL3
 * synth and the ESFM hwdep can have it in between. The bank is read at
 * that first use: the esfm_bank parameter's file (es1869/esfm-gm.bin), or
 * es1869/esfm-gm-square.bin with esfm_square. Without one, every program
 * plays a sine.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/seq_device.h>
#include <sound/seq_kernel.h>

#include "es1869.h"
#include "esfm_gm.h"

struct esfm_synth {
	struct es1869 *chip;
	int client, port;
	struct esfm_gm gm;
	struct esfm_gm_io io;
	const struct firmware *fw;
	spinlock_t lock; // the engine, against concurrent events
	struct mutex use_mutex;
	int users;
};

// the sine every program and drum plays without a bank: one voice, its
// first operator alone heard
static u8 esfm_fallback[512 + 36];

static void esfm_fallback_init(void)
{
	static const u8 sine[8] = { 0x01, 0x00, 0xF2, 0x24, 0x00, 0x00, 0x30, 0xE0 };
	int i;

	for (i = 0; i < 256; i++) {
		esfm_fallback[2 * i] = 0x00;
		esfm_fallback[2 * i + 1] = 0x02;
	}
	memcpy(esfm_fallback + 512 + 4, sine, 8);
	for (i = 1; i < 4; i++)
		esfm_fallback[512 + 4 + 8 * i + 1] = 0x3F;
}

static void esfm_write(void *ctx, u16 reg, u8 val)
{
	es1869_esfm_write(ctx, reg, val);
}

static void esfm_opts(struct esfm_gm_opts *o)
{
	o->pedal_release = es1869_val(ES_S_esfm_pedal_release);
	o->vibrato = es1869_val(ES_S_esfm_vibrato);
	o->tuning = es1869_val(ES_S_esfm_tuning);
	o->reset_controllers = es1869_val(ES_S_esfm_reset_controllers);
	o->live_pan = es1869_val(ES_S_esfm_live_pan);
	o->sysex = es1869_val(ES_S_esfm_sysex);
	o->silence = es1869_val(ES_S_esfm_silence_on_close);
	o->sides = es1869_val(ES_S_fm_sides);
}

// the bank file, checked; the fallback when it is missing or bad
static void esfm_load(struct esfm_synth *s, const u8 **bank, u32 *len)
{
	const char *name = es1869_val(ES_S_esfm_square) ?
				   "es1869/esfm-gm-square.bin" :
				   es1869_bank_name();
	int err;

	*bank = esfm_fallback;
	*len = sizeof(esfm_fallback);
	err = request_firmware(&s->fw, name, s->chip->dev);
	if (err) {
		dev_warn(s->chip->dev, "ESFM bank %s: %d, every program a sine\n",
			 name, err);
		s->fw = NULL;
		return;
	}
	if (esfm_bank_check(s->fw->data, s->fw->size)) {
		dev_warn(s->chip->dev, "ESFM bank %s isn't one, every program a sine\n",
			 name);
		release_firmware(s->fw);
		s->fw = NULL;
		return;
	}
	*bank = s->fw->data;
	*len = s->fw->size;
}

static int esfm_use(void *private_data, struct snd_seq_port_subscribe *info)
{
	struct esfm_synth *s = private_data;
	struct esfm_gm_opts opts;
	const u8 *bank;
	unsigned long flags;
	u32 len;
	int err = 0;

	if (!try_module_get(s->chip->card->module))
		return -EFAULT;
	mutex_lock(&s->use_mutex);
	if (!s->users) {
		err = es1869_esfm_claim(s->chip);
		if (!err) {
			// the reset's 596 writes before the events see the
			// engine, outside the event lock
			esfm_load(s, &bank, &len);
			esfm_opts(&opts);
			esfm_gm_init(&s->gm, &s->io, NULL, 0, &opts);
			spin_lock_irqsave(&s->lock, flags);
			s->gm.bank = bank;
			s->gm.bank_len = len;
			spin_unlock_irqrestore(&s->lock, flags);
		}
	}
	if (!err)
		s->users++;
	mutex_unlock(&s->use_mutex);
	if (err)
		module_put(s->chip->card->module);
	return err;
}

static int esfm_unuse(void *private_data, struct snd_seq_port_subscribe *info)
{
	struct esfm_synth *s = private_data;
	unsigned long flags;

	mutex_lock(&s->use_mutex);
	if (s->users && !--s->users) {
		spin_lock_irqsave(&s->lock, flags);
		esfm_gm_close(&s->gm);
		s->gm.bank = NULL;
		spin_unlock_irqrestore(&s->lock, flags);
		es1869_esfm_release(s->chip);
		release_firmware(s->fw);
		s->fw = NULL;
	}
	mutex_unlock(&s->use_mutex);
	module_put(s->chip->card->module);
	return 0;
}

// a 14-bit controller or parameter as the MIDI messages it stands for
static void esfm_cc14(struct esfm_gm *g, u8 ch, u8 msb_cc, u8 lsb_cc,
		      unsigned int val)
{
	esfm_gm_short(g, 0xB0 | ch, msb_cc, (val >> 7) & 0x7F);
	esfm_gm_short(g, 0xB0 | ch, lsb_cc, val & 0x7F);
}

static int esfm_event(struct snd_seq_event *ev, int direct, void *private_data,
		      int atomic, int hop)
{
	struct esfm_synth *s = private_data;
	struct esfm_gm *g = &s->gm;
	u8 ch, sysex[128];
	unsigned int param, val;
	unsigned long flags;
	long len = 0;

	if (ev->type == SNDRV_SEQ_EVENT_SYSEX)
		len = snd_seq_expand_var_event(ev, sizeof(sysex), sysex, 1, 0);
	spin_lock_irqsave(&s->lock, flags);
	if (!g->bank) {
		spin_unlock_irqrestore(&s->lock, flags);
		return 0;
	}
	ch = ev->data.control.channel & 0x0F;
	param = ev->data.control.param;
	val = ev->data.control.value;
	switch (ev->type) {
	case SNDRV_SEQ_EVENT_NOTEON:
		esfm_gm_short(g, 0x90 | (ev->data.note.channel & 0x0F),
			      ev->data.note.note, ev->data.note.velocity);
		break;
	case SNDRV_SEQ_EVENT_NOTEOFF:
		esfm_gm_short(g, 0x80 | (ev->data.note.channel & 0x0F),
			      ev->data.note.note, 0);
		break;
	case SNDRV_SEQ_EVENT_CONTROLLER:
		esfm_gm_short(g, 0xB0 | ch, param, val);
		break;
	case SNDRV_SEQ_EVENT_CONTROL14:
		if (param < 32)
			esfm_cc14(g, ch, param, param + 32, val);
		else
			esfm_gm_short(g, 0xB0 | ch, param, val);
		break;
	case SNDRV_SEQ_EVENT_REGPARAM:
		esfm_cc14(g, ch, MIDI_CTL_REGIST_PARM_NUM_MSB,
			  MIDI_CTL_REGIST_PARM_NUM_LSB, param);
		esfm_cc14(g, ch, MIDI_CTL_MSB_DATA_ENTRY, MIDI_CTL_LSB_DATA_ENTRY,
			  val);
		break;
	case SNDRV_SEQ_EVENT_NONREGPARAM:
		esfm_cc14(g, ch, MIDI_CTL_NONREG_PARM_NUM_MSB,
			  MIDI_CTL_NONREG_PARM_NUM_LSB, param);
		esfm_cc14(g, ch, MIDI_CTL_MSB_DATA_ENTRY, MIDI_CTL_LSB_DATA_ENTRY,
			  val);
		break;
	case SNDRV_SEQ_EVENT_PGMCHANGE:
		esfm_gm_short(g, 0xC0 | ch, val, 0);
		break;
	case SNDRV_SEQ_EVENT_CHANPRESS:
		esfm_gm_short(g, 0xD0 | ch, val, 0);
		break;
	case SNDRV_SEQ_EVENT_PITCHBEND:
		// -8192 to 8191, the 14 bits less 2000h
		val = clamp(ev->data.control.value + 0x2000, 0, 0x3FFF);
		esfm_gm_short(g, 0xE0 | ch, val & 0x7F, val >> 7);
		break;
	case SNDRV_SEQ_EVENT_SYSEX:
		if (len > 0)
			esfm_gm_sysex(g, sysex, len);
		break;
	case SNDRV_SEQ_EVENT_RESET:
		esfm_gm_gm_reset(g);
		break;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return 0;
}

static int esfm_probe(struct device *_dev)
{
	struct snd_seq_device *dev = to_seq_dev(_dev);
	struct es1869 *chip = *(struct es1869 **)SNDRV_SEQ_DEVICE_ARGPTR(dev);
	struct snd_seq_port_callback cb = {
		.owner = THIS_MODULE,
		.use = esfm_use,
		.unuse = esfm_unuse,
		.event_input = esfm_event,
	};
	struct esfm_synth *s;

	if (!chip)
		return -EINVAL;
	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->chip = chip;
	s->io.ctx = chip;
	s->io.write = esfm_write;
	spin_lock_init(&s->lock);
	mutex_init(&s->use_mutex);
	s->client = snd_seq_create_kernel_client(chip->card, dev->device,
						 "ES1869 ESFM synth");
	if (s->client < 0) {
		int err = s->client;

		kfree(s);
		return err;
	}
	cb.private_data = s;
	s->port = snd_seq_event_port_attach(s->client, &cb,
					    SNDRV_SEQ_PORT_CAP_WRITE |
					    SNDRV_SEQ_PORT_CAP_SUBS_WRITE,
					    SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC |
					    SNDRV_SEQ_PORT_TYPE_MIDI_GM |
					    SNDRV_SEQ_PORT_TYPE_HARDWARE |
					    SNDRV_SEQ_PORT_TYPE_SYNTHESIZER,
					    ESFM_CHANNELS, ESFM_VOICES,
					    "ESFM GM Port");
	if (s->port < 0) {
		int err = s->port;

		snd_seq_delete_kernel_client(s->client);
		kfree(s);
		return err;
	}
	dev_set_drvdata(_dev, s);
	return 0;
}

static int esfm_remove(struct device *_dev)
{
	struct esfm_synth *s = dev_get_drvdata(_dev);

	if (!s)
		return 0;
	snd_seq_delete_kernel_client(s->client);
	release_firmware(s->fw);
	kfree(s);
	return 0;
}

static struct snd_seq_driver esfm_seq_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.probe = esfm_probe,
		.remove = esfm_remove,
	},
	.id = ES1869_ESFM_SEQ_ID,
	.argsize = sizeof(struct es1869 *),
};

static int __init esfm_init(void)
{
	esfm_fallback_init();
	return snd_seq_driver_register(&esfm_seq_driver);
}

static void __exit esfm_exit(void)
{
	snd_seq_driver_unregister(&esfm_seq_driver);
}

module_init(esfm_init);
module_exit(esfm_exit);

MODULE_AUTHOR("Ethan Halsall <ethan.s.halsall@gmail.com>");
MODULE_DESCRIPTION("ESS ES1869 native ESFM General MIDI synthesizer");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("es1869/esfm-gm.bin");
MODULE_FIRMWARE("es1869/esfm-gm-square.bin");
