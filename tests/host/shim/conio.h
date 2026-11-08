/*
 * Stand-in for the Open Watcom conio.h in host builds: port I/O goes to
 * the simulated ES1869.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef SHIM_CONIO_H
#define SHIM_CONIO_H
#include "simhw.h"
#define inp(p) simhw_in((u16)(p))
#define outp(p, v) simhw_out((u16)(p), (u8)(v))
#endif
