/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Types and helpers for the chip core, so that hw.c and esfm_gm.c build in
 * the kernel and, for essreg's tests, on the host.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ES1869_HW_COMPAT_H
#define ES1869_HW_COMPAT_H

#ifdef __KERNEL__
#include <linux/bits.h>
#include <linux/errno.h>
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define clamp(v, lo, hi) min(max(v, lo), hi)
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_CLOSEST(n, d) (((n) + (d) / 2) / (d))

static inline u64 div_u64(u64 n, u32 d) { return n / d; }
#endif

#endif /* ES1869_HW_COMPAT_H */
