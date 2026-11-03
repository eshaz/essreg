/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Open Watcom's i86.h for essreg on Linux: no DOS box to find, and delays
 * that sleep.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ESSREG_LINUX_I86_H
#define ESSREG_LINUX_I86_H

#include <unistd.h>

union REGS {
	struct {
		unsigned short ax, bx, cx, dx;
	} w;
	struct {
		unsigned char al, ah, bl, bh, cl, ch, dl, dh;
	} h;
};

// int 2Fh 1600h, "is Windows running": not here
static inline int int86(int n, union REGS *in, union REGS *out)
{
	(void)n;
	*out = *in;
	out->h.al = 0;
	return 0;
}

#define delay(ms) usleep((ms) * 1000)
#define _disable() ((void)0)
#define _enable() ((void)0)

#endif
