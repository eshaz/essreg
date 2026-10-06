/*
 * Fixed-size types and platform glue shared by essreg, essctl and the host
 * tests.
 *
 * Notes:
 *
 * Builds with Watcom C 11 / Open Watcom (DOS and 16-bit Windows) and with
 * gcc for the host tests (-DESS_HOST). Keep the code C89.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef ESSTYPES_H
#define ESSTYPES_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef signed char s8;
typedef short s16;
typedef long s32;

#ifdef ESS_HOST
#define ESS_FAR
#else
#define ESS_FAR __far
#endif

#define ESS_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#endif /* ESSTYPES_H */
