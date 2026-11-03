/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The values of settings.def's columns: kinds, banks, dB scales and flags,
 * with no kernel header, so that the test suite reads the same table.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ES1869_SETTINGS_KINDS_H
#define ES1869_SETTINGS_KINDS_H

// kinds
#define S_BOOL 0
#define S_INT 1
#define S_ENUM 2
#define S_STEREO 3
#define S_MASTER 4
#define S_MASTERSW 5
#define S_SMAG 6
#define S_STR 7

// banks
#define B_NONE 0
#define B_MIX 1
#define B_CTRL 2
#define B_PORT7 3
#define B_PNPC 4

// dB scales
#define T_NONE 0
#define T_A1 1
#define T_A2 2
#define T_MIC 3
#define T_AUX 4
#define T_MASTER 5
#define T_BEEP 6
#define T_REC 7
#define T_3D 8

// flags
#define F_CTL 0x001
#define F_PARAM 0x002
#define F_INIT 0x004
#define F_INV 0x008
#define F_SWAP 0x010
#define F_HOOK 0x020
#define F_RO 0x040
#define F_IDLE 0x080
#define F_NEXT 0x100
#define F_IDLE_A1 0x200

#endif /* ES1869_SETTINGS_KINDS_H */
