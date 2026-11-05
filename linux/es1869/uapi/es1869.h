/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * The ES1869 driver's hwdep ioctls: the register hwdep, "ES1869 regs"
 * (hwdep 2), which reads and writes the chip's registers through the
 * driver's lock, and the ESFM hwdep, "ES1869 ESFM" (hwdep 1).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef _UAPI_SOUND_ES1869_H
#define _UAPI_SOUND_ES1869_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define ES1869_REGS_VERSION 1

// the hwdeps' interface numbers, past ALSA's list (SNDRV_HWDEP_IFACE_*),
// so that no OPL tool takes them for its own
#define ES1869_HWDEP_IFACE_REGS 0x40
#define ES1869_HWDEP_IFACE_ESFM 0x41

// how a register is reached, as essreg's register catalog numbers it
enum {
	ES1869_BANK_MIXER, // Audio_Base+4/+5
	ES1869_BANK_CTRL, // A0h-BFh through the DSP
	ES1869_BANK_APORT, // Audio_Base+0 to +Fh
	ES1869_BANK_CPORT, // Config_Base+0 to +7
	ES1869_BANK_PNPCARD, // card-level PnP register
	ES1869_BANK_PNPLDN, // a logical device's PnP register
};

#define ES1869_REG_LIVE 0x01 // a controller register from the chip
#define ES1869_REG_FORCE 0x02 // a read that changes the chip's state

struct es1869_reg {
	__u8 bank;
	__u8 ldn;
	__u8 reg;
	__u8 flags;
	__u8 value;
	__u8 pad[3];
};

struct es1869_reg_block {
	__u8 bank;
	__u8 ldn;
	__u8 first;
	__u8 flags;
	__u16 count; // registers first to first + count - 1, 256 at most
	__u16 pad;
	__u8 values[256];
};

// what the driver found and counted
struct es1869_regs_info {
	__u32 version;
	__u16 port, cfg, fm, mpu;
	__s16 irq, dma1, dma2;
	__u8 dsp[2]; // E7h's answer
	__u8 mixer_id[2]; // mixer 40h's
	__u8 a1_owner, a2_owner;
	__u32 settings_hash; // of settings.def's names, in order
	__u32 irqs[4]; // Audio 1, Audio 2, hardware volume, MPU-401
	__u32 irq_none;
	__u32 dsp_busy;
	__u32 dsp_desync;
	__u32 pos_retries;
	__u32 settles;
	__u32 mirror_late; // ring refills that came after the DMA
	__u32 mirror_max; // the largest refill, in bytes
	__u32 pad2;
};

// a table of registers written at even times, one entry a step, and over
// again: what a rate or a filter clock does when it moves under a stream
struct es1869_reg_pattern {
	__u64 table; // a pointer to len __u16, register << 8 | value
	__u64 base; // in cycles of the CPU, the first step; 0 for now, and the
		    // step after the last on return, to go on in phase
	__u64 step; // cycles between entries, with 16 bits of fraction
	__u32 count; // the steps of this call
	__u32 offset; // the table's entry at the first step, then after the last
	__u16 len; // ES1869_PATTERN_MAX at most
	__u8 bank; // ES1869_BANK_MIXER (70h, 72h) or _CTRL (A1h, A2h)
	__u8 flags; // ES1869_PATTERN_ALONE
	__u32 writes; // out: the writes made, an entry the same as the last none
	__u32 skipped; // out: steps that the machine's other work took
	__u32 late; // out: the latest write, in cycles
	__u32 longest; // out: the longest write, in cycles
};

#define ES1869_PATTERN_MAX 4096
#define ES1869_PATTERN_ALONE 0x01 // no other task runs, a third of a second at most

#define ES1869_IOCTL_PVERSION _IOR('H', 0x60, int)
#define ES1869_IOCTL_INFO _IOR('H', 0x61, struct es1869_regs_info)
#define ES1869_IOCTL_READ _IOWR('H', 0x62, struct es1869_reg)
#define ES1869_IOCTL_WRITE _IOW('H', 0x63, struct es1869_reg)
#define ES1869_IOCTL_BLOCK_READ _IOWR('H', 0x64, struct es1869_reg_block)
#define ES1869_IOCTL_PATTERN _IOWR('H', 0x65, struct es1869_reg_pattern)

#define ES1869_ESFM_VERSION 1

// a native ESFM register, 000h-7FFh: the operators from 000h (a voice's
// four in 20h bytes, eight registers each), the key-ons from 240h
struct es1869_esfm_reg {
	__u16 reg;
	__u8 value;
	__u8 pad;
};

struct es1869_esfm_regs {
	__u32 count; // records at regs, ES1869_ESFM_MAX at most
	__u32 pad;
	__u64 regs; // a pointer to count struct es1869_esfm_reg
};

#define ES1869_ESFM_MAX 4096

// the ESFM hwdep takes the FM from the OPL3 hwdep and synth while it is
// open, in the native mode, every voice keyed off; a reset does that again
#define ES1869_ESFM_IOCTL_PVERSION _IOR('H', 0x70, int)
#define ES1869_ESFM_IOCTL_RESET _IO('H', 0x71)
#define ES1869_ESFM_IOCTL_WRITE _IOW('H', 0x72, struct es1869_esfm_regs)
#define ES1869_ESFM_IOCTL_READ _IOWR('H', 0x73, struct es1869_esfm_reg)

#endif /* _UAPI_SOUND_ES1869_H */
