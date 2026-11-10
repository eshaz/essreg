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
 * buffer, each byte in a word's low byte. The monitor (monitor.c) reads back
 * what Audio 2's DMA took and owns no engine; its lock comes after a stream's
 * lock and before reg_lock.
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
#include "resample.h"
#include "capfilt.h"
#include "settings.h"

#define ES1869_A1_BUF (64 * 1024) // Audio 1's buffer, one 64 KiB page
#define ES1869_A2_BUF (64 * 1024) // Audio 2's, as the application sees it
#define ES1869_RING (2 * ES1869_A2_BUF) // its words on a 16-bit channel

struct file;
struct pnp_dev;
struct snd_hwdep;
struct snd_info_buffer;

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
	struct es1869_fill fill; // what both halves hold
	struct work_struct copy_work; // the rest of what .ack left
	unsigned long last; // the last offset the pointer gave
	bool running;
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
	struct hrtimer period; // period boundaries while the filter runs
	snd_pcm_uframes_t rot; // the application's frame at the ring's start
	snd_pcm_uframes_t pos; // the paused position
	unsigned long last; // the last offset the DMA was read at
	struct es1869_fill fill; // what the ring holds, and what comes next
	struct work_struct copy_work; // the rest of what .ack left
	struct es1869_stream stream; // as prepared, for a pause's restart
	bool spread; // a 16-bit channel: bytes in words, a mirrored buffer
	bool running;
	u32 max_chunk; // the largest refill, in bytes
	u32 ticks; // timer ticks, and the TSC cycles they took
	u64 tick_cycles;
	u32 acks; // copies after the application's writes, and theirs
	u64 ack_cycles;
	u32 fill_max; // the most cycles a fill held the lock, this stream
	u32 lead_low; // the fewest ring frames the fill stayed ahead, this stream
	u32 lead_skip; // readings of the position before that counts
	bool draining; // a drain's silence goes on past the last frame
	snd_pcm_uframes_t silent_to; // the absolute frame it has reached
	struct es1869_rs rs;
	// the DAC's correction at each rate that a2_treble's tables take:
	// treble.h's fit until "Audio 2 Treble Correction Taps" and "Phase
	// Taps" load others
	s32 fit[ES_RS_OVER][ES_RS_FIT];
	s32 phase[ES_RS_OVER][ES_RS_FIT]; // and its phase taps
	// a load of them made into spare tables and swapped in between runs,
	// so that a playing stream takes them with no break
	s16 *spare[ES_RS_OVER];
	struct work_struct fit_work;
	u32 over; // the ring frames to a stream frame
	u32 lap; // the ring's frames, as the 8237 runs through them
	u32 rs_quiet; // where in the ring its silence has reached, in bytes
	unsigned long rs_to; // the stream frame the last whole run came to
	u32 clk_period; // the master's ticks to a period of this stream
};

enum es1869_mon_state {
	ES_MON_OFF,
	ES_MON_WAIT, // started, for Audio 2's next start at its rate
	ES_MON_ON, // taking what Audio 2's DMA takes
	ES_MON_GAP, // the frames broke off: an xrun to come from the timer
};

// PCM device 3: what Audio 2's DAC took, read back from what its DMA reads
// (monitor.c), all of it under lock but pcm and tick
struct es1869_mon {
	struct snd_pcm *pcm;
	struct hrtimer tick;
	spinlock_t lock;
	u8 state; // enum es1869_mon_state
	// what Audio 2's DMA runs through, while a2_on
	bool a2_on;
	const u8 *src; // the ring, or the application's buffer on an 8-bit channel
	u8 word; // the bytes a chip byte takes there, 2 in the ring's words
	u8 fb; // a frame's bytes in the stream's format
	u8 bits;
	bool is_signed, stereo;
	u32 lap; // the frames the DMA runs through
	u32 num, den; // the ring's rate, num / den Hz, as prepared; 0 for none
	unsigned long at; // the ring's frame the monitor has taken up to
	// the monitor's stream, from its start
	struct snd_pcm_runtime *rt;
	unsigned long pos; // the frames taken, as hw_ptr counts them
	unsigned long next; // where its next period ends
	struct snd_ratnum rat; // its rate as this open allows it
	struct snd_pcm_hw_constraint_ratnums rats;
	u64 frames; // taken since the load, for /proc
	u32 gaps, overruns; // the frames broken off by Audio 2, by the reader
};

// an interrupt of a stream whose clock counts, as the handler stamped it
#define ES_IRQLOG 512
struct es1869_irqlog {
	u64 raw; // ns on the raw clock at the handler's start, 0 for a later pass
	u32 ticks; // the master's ticks the period took
	u8 src; // ES_IRQ_A1 or ES_IRQ_A2
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
	bool recovering; // es1869_dsp_recover puts the settings back
	struct delayed_work pwr_work; // the power-down after pwr_save seconds
	struct work_struct wake_work; // a wake the FM asked for
	u8 pwr_down; // 1 partly powered down, 2 fully
	bool pwr_stopped; // the card is going: no more power-downs
	struct pnp_dev *pnp_audio, *pnp_ctrl; // an ISA PnP card's two devices

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
	unsigned long a1_last; // the last offset Audio 1's DMA was read at
	bool a1_dac; // D1h sent: Audio 1 into the mixer
	bool a1_draining; // a drain's silence goes on past the last frame
	snd_pcm_uframes_t a1_silent_to; // the absolute frame it has reached
	// the correction filter on Audio 1's recordings (a1filter.c)
	struct es1869_cf a1c_cf;
	s16 a1c_taps[2][ES_CF_HALF + 1]; // what the next recording takes
	s16 a1c_ptaps[2][ES_CF_HALF]; // and its phase taps
	bool a1c_new; // and the one recording now takes them at its next pass
	struct work_struct a1c_work;
	bool a1c_on; // this recording goes through it
	bool a1c_fir; // with the taps, else with a FIR that only delays
	struct es1869_cl a1c_cl; // the linearity tables the next one takes
	bool a1c_cl_new; // and the one recording now takes at its next pass
	snd_pcm_uframes_t a1c_at; // the next frame of the buffer to filter
	u64 a1c_frames; // filtered since the driver loaded
	u64 a1c_longest; // ns, the longest piece of this recording's work

	// the chip's clock, from the interrupts of a running stream (clock.c)
	struct es1869_clock clk;
	u32 clk_a1, clk_a2; // the master's ticks to a period; 0 doesn't count
	u8 clk_src; // the source that feeds it, 0 to begin a new stretch
	s32 clk_offset; // what the step takes for it, 32 bits
	// every interrupt that counts, for es1869_irqs in /proc
	struct es1869_irqlog irqlog[ES_IRQLOG];
	u32 irqlog_seq; // entries written; the newest at (seq - 1) % ES_IRQLOG

	// Audio 2
	enum es1869_owner a2_owner;
	struct snd_pcm_substream *a2_sub;
	struct es1869_mirror mirror;
	bool a2_running;
	struct snd_dma_buffer a2_buf; // the direct buffer of an 8-bit channel

	struct snd_pcm *pcm; // device 0
	struct snd_pcm *pcm_fm; // device 1
	struct es1869_dual dual;
	struct es1869_mon mon; // device 3
	struct snd_kcontrol *kctl[ES_S_COUNT];
	struct snd_kcontrol *hwvol_kctl[2];
	struct snd_kcontrol *adc_kctl; // the ADC's input as a plain list
	struct snd_rawmidi *rmidi;
	struct snd_opl3 *opl3;
	void (*opl3_command)(struct snd_opl3 *opl3, unsigned short cmd,
			     unsigned char val); // the library's own
	int (*opl3_open)(struct snd_hwdep *hw, struct file *file); // and hwdep's
	unsigned int opl3_off; // 1 while opl3=0 holds a count of the FM token
	bool esfm_held; // the native side has the FM
	const struct es1869_esfm_ops *esfm_ops; // the synth's, while it's there
	void *esfm_data;
	spinlock_t fm_lock; // a native register's address and data writes
	struct snd_hwdep *esfm_hwdep; // hwdep 1
	struct snd_seq_device *esfm_seq; // for snd-es1869-esfm
	struct snd_hwdep *regs_hwdep;
	// last, so that a synth built before it still finds the rest: Audio
	// 2's DAC's gap at its sign taken back, 1/256 LSB, or 0
	u32 a2_zstep[2];
};

// main.c
extern const char *const es1869_owner_names[];
void es1869_irq_sources_locked(struct es1869 *chip);
void es1869_type_f_locked(struct es1869 *chip);

// a stream's silence filler, as snd_pcm_ops.fill_silence
typedef int (*es1869_silence_fn)(struct snd_pcm_substream *ss, int channel,
				 unsigned long pos, unsigned long bytes);

// a DMA offset in the buffer as an absolute frame: the last hw_ptr and how
// far the DMA went on from it
static inline snd_pcm_uframes_t es1869_abs_frame(struct snd_pcm_runtime *rt,
						 snd_pcm_uframes_t off)
{
	snd_pcm_uframes_t b = rt->buffer_size, known = rt->status->hw_ptr;

	return (known + (off + b - known % b) % b) % rt->boundary;
}

void es1869_fill_init(struct es1869_fill *f, struct snd_pcm_runtime *rt,
		      unsigned int guard, unsigned long slack);
u32 es1869_standing_us(struct snd_pcm_runtime *rt, u32 budget);

// a playback stream's pointer from the DMA's offset, in frames, and fifo
// bytes of FIFO in front of the DAC
snd_pcm_uframes_t es1869_play_pointer(struct snd_pcm_runtime *rt,
				      unsigned long off, unsigned int fifo,
				      bool draining, unsigned long *last);

void es1869_drain_silence(struct snd_pcm_substream *ss, snd_pcm_uframes_t hw,
			  unsigned int fifo, snd_pcm_uframes_t *to,
			  es1869_silence_fn fill);

// settings.c
void es1869_settings_found(struct es1869 *chip);
void es1869_settings_sync(struct es1869 *chip);
void es1869_settings_card_add(struct es1869 *chip);
void es1869_settings_card_del(struct es1869 *chip);
int es1869_settings_controls(struct es1869 *chip);
void es1869_setting_apply(struct es1869 *chip, int id);
void es1869_settings_apply_init(struct es1869 *chip);
void es1869_settings_apply_ctrl(struct es1869 *chip);
void es1869_settings_notify_reg(struct es1869 *chip, u8 bank, u8 reg);
int es1869_setting_format(int id, char *buf, size_t size);

// mixer.c
const unsigned int *es1869_tlv(u8 tlv);
int es1869_mixer_extra(struct es1869 *chip);
void es1869_hwvol_notify(struct es1869 *chip);
void es1869_beep_out(struct es1869 *chip);
void es1869_beep_leave(struct es1869 *chip);

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
int es1869_dsp_recover(struct es1869 *chip);

// clock.c
void es1869_clock_setup(struct es1869 *chip);
u32 es1869_clock_period(u32 num, u32 den, u32 frames);
void es1869_clock_tick(struct es1869 *chip, u8 source, u64 raw);
void es1869_clock_proc(struct es1869 *chip, struct snd_info_buffer *buf);
void es1869_irqlog_proc(struct snd_info_entry *entry,
			struct snd_info_buffer *buf);

// mirror.c
extern const struct snd_pcm_ops es1869_a2_ops;
int es1869_mirror_init(struct es1869 *chip);
void es1869_a2_irq(struct es1869 *chip, struct snd_pcm_substream **sub);
unsigned int es1869_a2_tied_rate(struct es1869 *chip);
void es1869_a2_rate_changed(struct es1869 *chip);
void es1869_a2_over_changed(struct es1869 *chip);

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
void es1869_power_save_changed(struct es1869 *chip);

// dual.c
int es1869_dual_new(struct es1869 *chip);
void es1869_dual_settled(struct es1869 *chip);

// monitor.c: Audio 2's side calls these under its stream's lock
int es1869_mon_new(struct es1869 *chip);
void es1869_mon_take(struct es1869 *chip, u32 frame);
void es1869_mon_a2_rate(struct es1869 *chip, u32 num, u32 den);
void es1869_mon_a2_go(struct es1869 *chip, struct snd_pcm_runtime *rt,
		      bool fresh);
void es1869_mon_a2_stop(struct es1869 *chip);
void es1869_mon_proc(struct es1869 *chip, struct snd_info_buffer *buf);

int es1869_treble_ctl_new(struct es1869 *chip);

// a1filter.c
int es1869_a1c_new(struct es1869 *chip);
void es1869_a1c_prepare(struct es1869 *chip, struct snd_pcm_substream *ss);
void es1869_a1c_period(struct es1869 *chip, struct snd_pcm_substream *ss);
snd_pcm_uframes_t es1869_a1c_pointer(struct es1869 *chip);
void es1869_a1c_sync(struct es1869 *chip);
void es1869_a1c_proc(struct es1869 *chip, struct snd_info_buffer *buf);

// whether the monitor takes what Audio 2's DMA takes
static inline bool es1869_mon_taking(struct es1869 *chip)
{
	return READ_ONCE(chip->mon.state) == ES_MON_ON;
}

// esfm.c
#define ES1869_ESFM_SEQ_ID "es1869-esfm"

// what snd-es1869-esfm wants to hear, with its private pointer
struct es1869_esfm_ops {
	void (*resume)(void *data); // the chip lost what the synth wrote
	void (*bank)(void *data); // esfm_bank or esfm_square was written
};

int es1869_fm_new(struct es1869 *chip);
void es1869_fm_resume(struct es1869 *chip);
void es1869_esfm_set_ops(struct es1869 *chip,
			 const struct es1869_esfm_ops *ops, void *data);
void es1869_esfm_bank_changed(struct es1869 *chip);
void es1869_opl3_gate(struct es1869 *chip);
int es1869_esfm_new(struct es1869 *chip);
int es1869_esfm_claim(struct es1869 *chip);
void es1869_esfm_release(struct es1869 *chip);
void es1869_esfm_reset(struct es1869 *chip);
void es1869_esfm_write(struct es1869 *chip, u16 reg, u8 val);
u8 es1869_esfm_read(struct es1869 *chip, u16 reg);

#endif /* ES1869_H */
