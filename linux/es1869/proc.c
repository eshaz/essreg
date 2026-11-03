// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * /proc/asound/cardN/es1869: what the driver found, who has the engines,
 * the counters, every setting with its default, and the registers.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <sound/info.h>

#include "es1869.h"

static void es1869_proc_read(struct snd_info_entry *entry,
			     struct snd_info_buffer *buf)
{
	struct es1869 *chip = entry->private_data;
	struct es1869_hw *hw = &chip->hw;
	struct es1869_stats st;
	unsigned long flags;
	u8 mix[0x80], ctrl[0x20], card[0x30], ldn1[0x80];
	char val[64];
	int i;

	snd_iprintf(buf, "ES1869 at 0x%lx, Config_Base 0x%x, FM 0x%lx, MPU-401 0x%lx\n",
		    chip->port, hw->cfg, chip->fm_port, chip->mpu_port);
	snd_iprintf(buf, "IRQ %d, Audio 1 on DMA %d, Audio 2 on DMA %d%s\n",
		    chip->irq, chip->dma1, chip->dma2,
		    chip->mirror.spread ? " (bytes spread over words)" : "");
	snd_iprintf(buf, "DSP %02X %02X, mixer %02X %02X%s\n",
		    chip->ident.dsp[0], chip->ident.dsp[1],
		    chip->ident.mixer[0], chip->ident.mixer[1],
		    chip->dead ? ", lost at a resume" : "");
	snd_iprintf(buf, "Audio 1: %s; Audio 2: %s; power: %s\n",
		    es1869_owner_names[chip->a1_owner],
		    es1869_owner_names[chip->a2_owner],
		    chip->pwr_down == 2 ? "fully down" :
		    chip->pwr_down ? "partly down" : "up");

	spin_lock_irqsave(&chip->reg_lock, flags);
	st = hw->stats;
	for (i = 0; i < 0x80; i++)
		mix[i] = i == 0x40 || i == 0x66 || i == 0x00 ? hw->mix[i] :
			 es1869_mix_read(hw, i);
	memcpy(ctrl, hw->ctrl, sizeof(ctrl));
	for (i = 0; i < 0x30; i++)
		card[i] = es1869_pnp_read(hw, 0xFF, i);
	for (i = 0x30; i < 0x80; i++)
		ldn1[i] = es1869_pnp_read(hw, 1, i);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	snd_iprintf(buf, "\ninterrupts: Audio 1 %u, Audio 2 %u, buttons %u, MPU-401 %u, none %u\n",
		    st.irq[0], st.irq[1], st.irq[2], st.irq[3], st.irq_none);
	snd_iprintf(buf, "DSP busy %u, out of step %u; position rereads %u; settles %u\n",
		    st.dsp_busy, st.dsp_desync, st.pos_retries, st.settles);
	snd_iprintf(buf, "stuck sources %u, taken off the line %02x%s\n",
		    st.irq_stuck, st.irq_dropped,
		    chip->irq_storm ? "; a storm took them all off" : "");
	snd_iprintf(buf, "Audio 2 ring: late refills %u, largest %u bytes\n",
		    chip->mirror.late, chip->mirror.max_chunk);
	// what the driver costs: TSC cycles a call, and the 8237's readings
	snd_iprintf(buf, "cost: interrupts %u, %llu cycles each; ring ticks %u, %llu cycles each; 8237 readings %u\n",
		    chip->isr_calls,
		    chip->isr_calls ? div_u64(chip->isr_cycles, chip->isr_calls) : 0,
		    chip->mirror.ticks,
		    chip->mirror.ticks ? div_u64(chip->mirror.tick_cycles,
						 chip->mirror.ticks) : 0,
		    st.pos_reads);
	snd_iprintf(buf, "Config_Base+6 missed a polled source %u times\n",
		    st.status_miss);

	snd_iprintf(buf, "\nsettings (parameter = value, default):\n");
	for (i = 0; i < ES_S_COUNT; i++) {
		const struct es1869_setting *s = &es1869_settings[i];

		es1869_setting_format(i, val, sizeof(val));
		if (s->kind == S_STR)
			snd_iprintf(buf, "  %s = %s\n", s->name, val);
		else
			snd_iprintf(buf, "  %s = %s, %d\n", s->name, val, s->def);
	}

	snd_iprintf(buf, "\nmixer:");
	for (i = 0; i < 0x80; i++)
		snd_iprintf(buf, "%s%02X", i % 16 ? " " : "\n  ", mix[i]);
	snd_iprintf(buf, "\ncontroller A0h-BFh (as written):");
	for (i = 0; i < 0x20; i++)
		snd_iprintf(buf, "%s%02X", i % 16 ? " " : "\n  ", ctrl[i]);
	snd_iprintf(buf, "\nAudio_Base+7 %02X, Config_Base+7 %02X\n", hw->port7,
		    hw->imr);
	snd_iprintf(buf, "PnP card registers 00h-2Fh:");
	for (i = 0; i < 0x30; i++)
		snd_iprintf(buf, "%s%02X", i % 16 ? " " : "\n  ", card[i]);
	snd_iprintf(buf, "\nPnP LDN 1 registers 30h-7Fh:");
	for (i = 0x30; i < 0x80; i++)
		snd_iprintf(buf, "%s%02X", i % 16 ? " " : "\n  ", ldn1[i]);
	snd_iprintf(buf, "\n");
}

void es1869_proc_init(struct es1869 *chip)
{
	snd_card_ro_proc_new(chip->card, "es1869", chip, es1869_proc_read);
}
