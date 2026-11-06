/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * es1869test's only way to the machine: hal_alsa.c on the laptop, through
 * alsa-lib, sysfs and the driver's hwdeps, and hal_sim.c on a simulated
 * ES1869 with the cable and a second card, in virtual time.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ET_HAL_H
#define ET_HAL_H

#include "et.h"

// alsamixer's views a simple element shows in
#define ET_VIEW_PLAYBACK 1
#define ET_VIEW_CAPTURE 2

struct et_hal {
	const char *name;
	void *ctx;

	// the streams, run together; 0 or the first one's error
	int (*run)(void *ctx, struct et_stream *s, int n);
	// whether a role's device is there
	int (*has)(void *ctx, enum et_role role);
	// the second card's capture device, if any
	const char *(*rec2)(void *ctx);

	// controls of the ES1869 card, by name and index: the number of values
	// read or written, or a negative errno
	int (*ctl_info)(void *ctx, const char *name, int index,
			struct et_ctl_info *info);
	int (*ctl_get)(void *ctx, const char *name, int index, long *v, int n);
	int (*ctl_set)(void *ctx, const char *name, int index, const long *v,
		       int n);
	int (*ctl_enum_name)(void *ctx, const char *name, int index, int item,
			     char *buf, int len);
	// a value's gain from the control's dB scale, in hundredths of a dB
	// (ET_DB_MUTE for muted)
	int (*ctl_db)(void *ctx, const char *name, int index, long value,
		      long *cdb);
	// an element of the simple mixer, which alsamixer and amixer show: 0
	// and whether alsamixer's Playback view has it, or -ENOENT
	// a simple element's alsamixer views: ET_VIEW_PLAYBACK | ET_VIEW_CAPTURE
	int (*selem)(void *ctx, const char *name, int *views);
	// the card's controls, one at a time: 0 and the name, or -ENOENT
	int (*ctl_nth)(void *ctx, int nth, char *name, int len, int *index);

	// the module's parameters, as sysfs has them
	int (*param_get)(void *ctx, const char *id, char *buf, int len);
	int (*param_set)(void *ctx, const char *id, const char *val);

	// a register through the register hwdep (bank as in uapi/es1869.h)
	int (*reg_read)(void *ctx, int bank, int reg, int *val);
	// the driver's settings hash and interrupt counts (Audio 1, Audio 2,
	// the buttons, the MPU-401), from the register hwdep; irqs may be NULL
	int (*info)(void *ctx, uint32_t *hash, uint32_t *irqs);
	// /proc/asound/cardN/es1869
	int (*proc)(void *ctx, char *buf, int len);
	// the CPU's busy and total time so far, in seconds, and its clock
	int (*cpu)(void *ctx, double *busy, double *total);
	double (*cpu_hz)(void *ctx);
	// a watch over the machine, from on to off: what a thread that gets
	// only idle time counts a second, and the latest a real-time thread
	// woke after a timer, in us. Time in the idle loop can't hide from it.
	// trace gets the count of each quarter of a second, at most max of them
	int (*watch)(void *ctx, int on, double *spins, double *late_us,
		     double *trace, int max);
	// the time, and a wait, in seconds
	double (*now)(void *ctx);
	void (*sleep)(void *ctx, double s);

	// someone at the machine is asked to press a button (ET_BTN_*); the
	// simulated machine presses it itself
	void (*ask_press)(void *ctx, int button);
	// the machine's devices suspended and resumed through the kernel's
	// pm_test, which runs the driver's suspend and resume: 0, or a
	// negative errno (-ENOTSUP without pm_test)
	int (*suspend)(void *ctx);
	// the driver unloaded and loaded again with these parameters, and the
	// card found again: 0, or a negative errno
	int (*reload)(void *ctx, const char *params);
	// the kernel's new log lines since the last call that name the driver,
	// or warn of a fault anywhere, one a line into buf: their number, or a
	// negative errno
	int (*klog)(void *ctx, char *buf, int len);
	// whether a recording would hang the machine: an Intel PIIX4 with
	// Type F timing on the card's capture channel (ISA bridge 76h, 77h): 1
	// or 0, or a negative errno where it can't be read
	int (*type_f)(void *ctx);
};

#define ET_BTN_UP 0
#define ET_BTN_DOWN 1
#define ET_BTN_MUTE 2
#define ET_BTN_SUSPEND 3

#define ET_DB_MUTE (-9999999)

struct et_hal *et_hal_alsa(const char *rec2);
struct et_hal *et_hal_sim(unsigned int faults);
// the command that loads the driver again, its parameters appended; NULL
// for modprobe
void et_hal_alsa_reload_cmd(const char *cmd);
// a suspend and resume of the machine's devices, as the HAL's suspend
int et_pm_test_suspend(void);
// a tone on the PC speaker, as the console's bell rings it: 0, or -1 with
// no console to ask
int et_pc_tone(unsigned int hz, unsigned int ms);
// a kernel log line that warns of a fault: a WARN, a BUG, an oops, a hung
// task or an interrupt that nobody handled
int et_klog_fault(const char *msg);

// hal_sim's faults, for the tests of the suite itself
#define SIM_A1_BYTE_OFF 0x0001 // Audio 1 starts a byte late
#define SIM_A2_SILENT 0x0002 // Audio 2 counts words: silence
#define SIM_A2_SWAP 0x0004 // Audio 2's sides swapped
#define SIM_NO_FIFO 0x0008 // the delay misses the FIFO
#define SIM_CLOCK_DS 0x0010 // the data sheet's clocks
#define SIM_TLV_OFF 0x0020 // a level one step off
#define SIM_B4_SWAP 0x0040 // B4h's sides swapped
#define SIM_FM_SIDES 0x0080 // fm_sides ignored
#define SIM_LOSS 0x0100 // Audio 2 loses bytes
#define SIM_RESUME 0x0200 // a resume leaves the 3-D level's register at 0
#define SIM_RELOAD 0x0400 // a reload ignores its parameters
#define SIM_HWVOL 0x0800 // the master's control misses the buttons
#define SIM_KLOG 0x1000 // the driver logs an error in every run
#define SIM_A1_MUTES 0x2000 // Audio 1's close silences Audio 2 for 100 ms
#define SIM_DRAIN_STALE 0x4000 // a drain plays the ring's old frames to a period's end
#define SIM_FILTER_FIXED 0x8000 // Audio 2's filter corner ignores the rate
#define SIM_TWO_PERIODS 0x10000 // two periods in the buffer lose a buffer
#define SIM_RUNOUT_STALE 0x20000 // the core's silence misses the ring
#define SIM_A1_RESUME 0x40000 // a resume leaves Audio 1 off the mixer
#define SIM_A1_DRAIN 0x80000 // a drain drops what Audio 1's FIFO holds
#define SIM_A1_PAUSEDRAIN 0x100000 // a drain from a pause loses a buffer
#define SIM_ADC_DC 0x200000 // the ADC is off by more than half a step
#define SIM_BEEP 0x400000 // the PC speaker's level is ignored
#define SIM_TRIM 0x800000 // the rate correction leaves the samples alone
#define SIM_CLOCK_BASE 0x1000000 // and takes 768 kHz's offset the wrong way
#define SIM_RS_SEAM 0x2000000 // a ring frame off by 1 at a chunk's seam
#define SIM_RS_IMAGE 0x4000000 // the filter's short: images above 18 kHz
#define SIM_RS_LIVE 0x8000000 // a change under a stream waits for its end
#define SIM_RS_REAL_TIME 0x10000000 // the Clock mode misses the steered clock
#define SIM_RS_TREBLE 0x20000000 // the hold's correction does nothing
#define SIM_RS_OVER 0x40000000 // the ring stays at the stream's rate
#define SIM_MIXER 0x80000000u // the record level's scale moves with the source,
			      // the ADC's list holds its own value, and the
			      // PC speaker doesn't hear the master

#endif
