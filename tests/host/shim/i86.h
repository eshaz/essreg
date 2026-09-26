/*
 * Stand-in for the Open Watcom i86.h in host builds.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef SHIM_I86_H
#define SHIM_I86_H
#define delay(ms) ((void)(ms))
#define _disable() ((void)0)
#define _enable() ((void)0)
#endif
