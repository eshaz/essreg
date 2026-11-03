/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The ES1869 driver's state, and what its files share.
 *
 * Notes:
 *
 * Locks, outermost first: es1869_cards_lock (the card list, which runtime
 * parameter writes walk), a card's state_mutex (owners, settings, power),
 * the PCM stream lock, then reg_lock, a spinlock taken with interrupts off
 * around every port sequence and the chip core's shadows, and the 8237's
 * claim_dma_lock inside it.
 *
 * Audio 1 plays, records, or records the music DAC, one at a time
 * (a1_owner). Audio 2 plays (a2_owner). On a 16-bit DMA channel, Audio 2
 * plays from a ring of words that mirror.c fills from the application's
 * buffer, each byte in a word's low byte.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ES1869_H
#define ES1869_H

#include <linux/hrtimer.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/timex.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/pcm.h>

struct pci_dev;

#include "hw.h"
#include "settings.h"

#define ES1869_A1_BUF (64 * 1024) // Audio 1's buffer, one 64 KiB page
#define ES1869_A2_BUF (64 * 1024) // Audio 2's, as the application sees it
#define ES1869_RING (2 * ES1869_A2_BUF) // its words on a 16-bit channel

enum es1869_owner {
	ES_OWN_NONE,
	ES_OWN_A1P, // Audio 1 playback: device 0, playback subdevice 1
	ES_OWN_A1C, // Audio 1 recording: device 0, capture
	ES_OWN_FMC, // the music DAC recorded: device 1
	ES_OWN_A2P, // Audio 2 playback: device 0, playback subdevice 0
	ES_OWN_DUAL, // both DACs as four channels: device 2
};

// dual playback (dual.c): channels 1-2 to Audio 1's buffer, 3-4 to Audio
// 2's ring, both from the application's four-channel buffer
struct es1869_dual {
	struct snd_pcm *pcm; // device 2
	struct hrtimer tick;
	snd_pcm_uframes_t filled; // the absolute frame both halves hold up to
	snd_pcm_uframes_t last; // the last offset the pointer gave
	bool running, backlog, standing;
	bool draining; // as the mirror's
	snd_pcm_uframes_t silent_to;
	struct es1869_stream s2; // Audio 2's half, for its start
};

enum es1869_a1_state {
	ES_A1_IDLE,
	ES_A1_SETTLING, // started, its channel still masked
	ES_A1_RUNNING,
	ES_A1_PAUSED,
	ES_A1_PAUSED_MASKED, // paused before its channel was unmasked
	ES_A1_SETTLING_REC, // a recording waiting for the ADC's input
};

struct es1869_mirror {
	struct snd_dma_buffer ring; // the words the 8237 moves
	struct hrtimer tick;
	snd_pcm_uframes_t rot; // the application's frame at the ring's start
	snd_pcm_uframes_t filled; // up to where the ring holds current data
	snd_pcm_uframes_t pos; // the paused position
	snd_pcm_uframes_t last; // the last offset the DMA was read at
	struct es1869_stream stream; // as prepared, for a pause's restart
	bool spread; // a 16-bit channel: bytes in words, a mirrored buffer
	bool running;
	u32 late; // refills of frames the DMA had already taken
	u32 max_chunk; // the largest refill, in bytes
	u32 ticks; // timer ticks, and the TSC cycles they took
	u64 tick_cycles;
	bool backlog; // the last fill left frames to copy
	bool standing; // the pointer stands, as under dmix
	bool draining; // a drain's silence goes on past the last frame
	snd_pcm_uframes_t silent_to; // the absolute frame it has reached
};

struct es1869 {
	struct snd_card *card;
	struct device *dev;
	int dev_index; // the card's place in the resource parameters
	struct es1869_hw hw;
	struct es1869_io io;
	struct es1869_ident ident;
	spinlock_t reg_lock;
	struct mutex state_mutex;
	struct list_head list;
	bool listed;
	bool dead; // the chip didn't come back from a resume
	struct delayed_work pwr_work; // the power-down after pwr_save seconds
	u8 pwr_down; // 1 partly powered down, 2 fully

	unsigned long port, fm_port, mpu_port;
	int irq, dma1, dma2;
	u32 isr_calls; // interrupt handler calls, and the TSC cycles they took
	u64 isr_cycles;
	struct pci_dev *isa_bridge; // a PIIX4 with Type F on Audio 1's channel
	u8 type_f_reg, type_f_bios; // its MBDMA register and the BIOS's value

	// Audio 1, shared by its playback, its recording and the FM recording
	enum es1869_owner a1_owner;
	struct snd_pcm_substream *a1_sub;
	struct snd_dma_buffer a1_buf;
	struct hrtimer a1_settle;
	unsigned int a1_gen;
	bool adc_on; // the codec on the ADC between recordings (a1c_settle_ms)
	ktime_t adc_since; // since when
	enum es1869_a1_state a1_state;
	snd_pcm_uframes_t a1_last; // the last offset Audio 1's DMA was read at
	bool a1_dac; // D1h sent: Audio 1 into the mixer

	// Audio 2
	enum es1869_owner a2_owner;
	struct snd_pcm_substream *a2_sub;
	struct es1869_mirror mirror;
	bool a2_running;
	struct snd_dma_buffer a2_buf; // the direct buffer of an 8-bit channel

	struct snd_pcm *pcm; // device 0
	struct snd_pcm *pcm_fm; // device 1
	struct es1869_dual dual;
	struct snd_kcontrol *kctl[ES_S_COUNT];
	struct snd_kcontrol *hwvol_kctl[2];
	struct snd_rawmidi *rmidi;
	struct snd_opl3 *opl3;
	void (*opl3_command)(struct snd_opl3 *opl3, unsigned short cmd,
			     unsigned char val); // the library's own
	spinlock_t fm_lock; // a native register's address and data writes
	struct snd_hwdep *esfm_hwdep; // hwdep 1
	struct snd_seq_device *esfm_seq; // for snd-es1869-esfm
	struct snd_hwdep *regs_hwdep;
	unsigned long irq_window; // the storm guard's second, in jiffies
	unsigned int irq_count; // and the interrupts in it
	bool irq_storm; // it took every source off the line
};

// main.c
extern const char *const es1869_owner_names[];
void es1869_irq_sources_locked(struct es1869 *chip);
void es1869_type_f_locked(struct es1869 *chip);

// a DMA offset in frames that never steps back: a reading behind the last
// one by less than half the buffer was read late, or torn, and ALSA would
// take the step back for a wrap and skip a buffer
static inline snd_pcm_uframes_t es1869_forward(snd_pcm_uframes_t *last,
					       snd_pcm_uframes_t off,
					       snd_pcm_uframes_t size)
{
	if (off != *last && (*last + size - off) % size < size / 2)
		return *last;
	*last = off;
	return off;
}

// a stream's silence filler, as snd_pcm_ops.fill_silence
typedef int (*es1869_silence_fn)(struct snd_pcm_substream *ss, int channel,
				 unsigned long pos, unsigned long bytes);

void es1869_drain_silence(struct snd_pcm_substream *ss, snd_pcm_uframes_t hw,
			  unsigned int fifo, snd_pcm_uframes_t *to,
			  es1869_silence_fn fill);

// settings.c
int es1869_settings_card_init(struct es1869 *chip);
void es1869_settings_card_add(struct es1869 *chip);
void es1869_settings_card_del(struct es1869 *chip);
int es1869_settings_controls(struct es1869 *chip);
void es1869_setting_apply(struct es1869 *chip, int id);
void es1869_settings_apply_init(struct es1869 *chip);
void es1869_settings_notify_reg(struct es1869 *chip, u8 bank, u8 reg);
int es1869_setting_format(int id, char *buf, size_t size);

// mixer.c
const unsigned int *es1869_tlv(u8 tlv);
int es1869_rec_tlv(struct snd_kcontrol *kctl, int op_flag, unsigned int size,
		   unsigned int __user *tlv);
int es1869_mixer_extra(struct es1869 *chip);
void es1869_hwvol_notify(struct es1869 *chip);

// pcm.c
struct es1869_limit;
extern const struct es1869_limit es1869_limit_a2;
int es1869_pcm_new(struct es1869 *chip);
int es1869_whole_periods(struct snd_pcm_runtime *runtime);
int es1869_pcm_limits(struct snd_pcm_runtime *runtime,
		      const struct es1869_limit *lim);
void es1869_stream_rate(struct snd_pcm_runtime *runtime, bool a1, u32 *num,
			u32 *den);
void es1869_a1_irq(struct es1869 *chip, struct snd_pcm_substream **sub);
bool es1869_a1_busy(struct es1869 *chip);

// mirror.c
extern const struct snd_pcm_ops es1869_a2_ops;
int es1869_mirror_init(struct es1869 *chip);
void es1869_a2_irq(struct es1869 *chip, struct snd_pcm_substream **sub);

// regs_hwdep.c
int es1869_regs_hwdep_new(struct es1869 *chip);

// proc.c
void es1869_proc_init(struct es1869 *chip);

extern const struct snd_pcm_hw_constraint_ratnums es1869_a1_rates;
void es1869_a1_start_settle(struct es1869 *chip,
			    struct snd_pcm_runtime *runtime);

// main.c: power save
int es1869_power_up(struct es1869 *chip);
void es1869_power_idle(struct es1869 *chip);

// dual.c
int es1869_dual_new(struct es1869 *chip);
void es1869_dual_settled(struct es1869 *chip);

// esfm.c
#define ES1869_ESFM_SEQ_ID "es1869-esfm"
int es1869_fm_new(struct es1869 *chip);
void es1869_fm_resume(struct es1869 *chip);
int es1869_esfm_new(struct es1869 *chip);
int es1869_esfm_claim(struct es1869 *chip);
void es1869_esfm_release(struct es1869 *chip);
void es1869_esfm_reset(struct es1869 *chip);
void es1869_esfm_write(struct es1869 *chip, u16 reg, u8 val);
u8 es1869_esfm_read(struct es1869 *chip, u16 reg);

#endif /* ES1869_H */
