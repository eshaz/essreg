// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Audio 1's recordings through the correction filter of capfilt.c.
 *
 * Notes:
 *
 * The DMA writes a recording straight into the stream's buffer, so the filter
 * runs there in place, on the frames the DMA has written and the program
 * hasn't been given yet: they lie between the stream's position and the
 * DMA's, where neither the DMA nor the program touches them. The pointer
 * gives the program only what the filter has been through, and the filter's
 * own history keeps the raw frames it needs from one pass to the next.
 *
 * The filter takes about 200 cycles a stereo frame in C on the Armada's
 * Celeron, too long for the interrupt, which would hold interrupts off for
 * most of a millisecond at each period. So the interrupt hands the period to
 * a work item, which reads the DMA's position, filters up to it with
 * interrupts on, and then tells the core that the period passed. A stop waits
 * for the work in sync_stop, before the buffer can go.
 *
 * Only Audio 1's own recordings at 16 bits take the filter: the FM recording
 * (PCM device 1) stays exact, as es1869test's dig cases need. The taps come
 * from "Audio 1 Correction Capture Taps", 17 for each channel, the centre
 * first, in units of 1/16384, which a recording takes at its prepare with
 * a1c_filter, and a recording under way at its next pass, its history kept,
 * so that a correction can follow the path as it drifts. es1869scope -C
 * measures the loop, fits them and loads them.
 *
 * ESS's drivers have no such filter: ES1869.DRV and ES1869.VXD record the
 * DMA's bytes as they come. The roll-off it takes back is measured, not from
 * the data sheet, which gives only the switched-capacitor filter's corner as a
 * share of the rate (register A2h); see capfilt.c.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */
#include "es1869.h"

#include <sound/control.h>
#include <sound/info.h>

static void a1c_work_fn(struct work_struct *work)
{
	struct es1869 *chip = container_of(work, struct es1869, a1c_work);
	struct snd_pcm_substream *ss;
	struct snd_pcm_runtime *rt;
	snd_pcm_uframes_t to, n;
	unsigned long flags;
	u32 pos;

	spin_lock_irqsave(&chip->reg_lock, flags);
	ss = chip->a1_sub;
	if (!ss || !chip->a1c_on || (chip->a1_state != ES_A1_RUNNING &&
				     chip->a1_state != ES_A1_SETTLING)) {
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return;
	}
	rt = ss->runtime;
	pos = es1869_dma_pos(&chip->hw, chip->dma1,
			     snd_pcm_lib_buffer_bytes(ss));
	// taps loaded while it records: from this pass on, the history kept
	if (chip->a1c_new) {
		memcpy(chip->a1c_cf.taps, chip->a1c_taps,
		       sizeof(chip->a1c_cf.taps));
		chip->a1c_new = false;
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	to = es1869_forward(&chip->a1_last, bytes_to_frames(rt, pos),
			    rt->buffer_size, bytes_to_frames(rt, 32));
	// the frames since the last pass, in at most two pieces of the ring
	n = (to + rt->buffer_size - chip->a1c_at) % rt->buffer_size;
	while (n) {
		snd_pcm_uframes_t k = min(n, rt->buffer_size - chip->a1c_at);
		s16 *p = (s16 *)(rt->dma_area +
				 frames_to_bytes(rt, chip->a1c_at));

		es1869_cf_run(&chip->a1c_cf, p, k, rt->channels);
		WRITE_ONCE(chip->a1c_at, (chip->a1c_at + k) % rt->buffer_size);
		chip->a1c_frames += k;
		n -= k;
	}
	snd_pcm_period_elapsed(ss);
}

// the period of a recording through the filter, from the interrupt
void es1869_a1c_period(struct es1869 *chip, struct snd_pcm_substream *ss)
{
	if (chip->a1c_on)
		queue_work(system_highpri_wq, &chip->a1c_work);
	else
		snd_pcm_period_elapsed(ss);
}

// a recording's start: through the filter if a1c_filter asks and the frames
// are Audio 1's own at 16 bits, from silence, with the taps as they are now
void es1869_a1c_prepare(struct es1869 *chip, struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *rt = ss->runtime;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->a1c_on = es1869_val(ES_S_a1c_filter) &&
		       chip->a1_owner == ES_OWN_A1C &&
		       rt->format == SNDRV_PCM_FORMAT_S16_LE &&
		       rt->channels <= 2;
	chip->a1c_at = 0;
	memcpy(chip->a1c_cf.taps, chip->a1c_taps, sizeof(chip->a1c_cf.taps));
	chip->a1c_new = false;
	es1869_cf_reset(&chip->a1c_cf);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

snd_pcm_uframes_t es1869_a1c_pointer(struct es1869 *chip)
{
	return READ_ONCE(chip->a1c_at);
}

// no pass runs once a recording has stopped and before its buffer goes
void es1869_a1c_sync(struct es1869 *chip)
{
	cancel_work_sync(&chip->a1c_work);
}

static int taps_info(struct snd_kcontrol *kc, struct snd_ctl_elem_info *ui)
{
	ui->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	ui->count = 2 * (ES_CF_HALF + 1);
	ui->value.integer.min = -32768;
	ui->value.integer.max = 32767;
	ui->value.integer.step = 1;
	return 0;
}

static int taps_get(struct snd_kcontrol *kc, struct snd_ctl_elem_value *uv)
{
	struct es1869 *chip = snd_kcontrol_chip(kc);
	unsigned long flags;
	unsigned int c, i;

	spin_lock_irqsave(&chip->reg_lock, flags);
	for (c = 0; c < 2; c++)
		for (i = 0; i <= ES_CF_HALF; i++)
			uv->value.integer.value[c * (ES_CF_HALF + 1) + i] =
				chip->a1c_taps[c][i];
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int taps_put(struct snd_kcontrol *kc, struct snd_ctl_elem_value *uv)
{
	struct es1869 *chip = snd_kcontrol_chip(kc);
	unsigned long flags;
	unsigned int c, i;
	int changed = 0;

	for (i = 0; i < 2 * (ES_CF_HALF + 1); i++)
		if (uv->value.integer.value[i] < -32768 ||
		    uv->value.integer.value[i] > 32767)
			return -EINVAL;
	// under the lock the filter's work takes them by
	spin_lock_irqsave(&chip->reg_lock, flags);
	for (c = 0; c < 2; c++)
		for (i = 0; i <= ES_CF_HALF; i++) {
			s16 v = uv->value.integer.value[c * (ES_CF_HALF + 1) +
							i];

			changed |= chip->a1c_taps[c][i] != v;
			chip->a1c_taps[c][i] = v;
		}
	if (changed)
		chip->a1c_new = true;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return changed;
}

int es1869_a1c_new(struct es1869 *chip)
{
	static const struct snd_kcontrol_new knew = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Audio 1 Correction Capture Taps",
		.info = taps_info,
		.get = taps_get,
		.put = taps_put,
	};
	INIT_WORK(&chip->a1c_work, a1c_work_fn);
	// until a measurement loads its own: the filter changes nothing
	memset(chip->a1c_taps, 0, sizeof(chip->a1c_taps));
	chip->a1c_taps[0][0] = ES_CF_ONE;
	chip->a1c_taps[1][0] = ES_CF_ONE;
	return snd_ctl_add(chip->card, snd_ctl_new1(&knew, chip));
}

void es1869_a1c_proc(struct es1869 *chip, struct snd_info_buffer *buf)
{
	snd_iprintf(buf, "Audio 1 correction: %s; %llu frames filtered\n",
		    chip->a1c_on ? "on" : "off", chip->a1c_frames);
}
