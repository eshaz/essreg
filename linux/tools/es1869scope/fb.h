/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * fb.h: the framebuffer on a virtual terminal of its own: the terminal in
 * graphics mode while the program runs, handed back at a switch away and
 * redrawn at a switch back, and text again at the end, also after a signal.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef FB_H
#define FB_H

#include "draw.h"

struct fb {
	int fd, tty;
	unsigned int w, h, bpp, line;
	unsigned char *mem;
	unsigned long size;
	unsigned int shift[3], bits[3]; /* red, green, blue */
	int shown; /* the terminal is ours to draw on */
	int redraw; /* everything again, after a switch back */
};

/* the framebuffer dev on the controlling terminal: 0 or -errno */
int fb_open(struct fb *f, const char *dev);
/* the canvas's dirty rows, converted to the screen's pixels */
void fb_present(struct fb *f, struct canvas *c);
/* a key, or -1 if none waits */
int fb_key(struct fb *f);
/* the terminal back as it was */
void fb_close(struct fb *f);
/* what a signal handler calls */
void fb_restore(void);
/* the switches the terminal asks for, at a safe place in the loop */
void fb_poll_switch(struct fb *f);

#endif /* FB_H */
