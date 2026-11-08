// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * fb.c: the framebuffer on a virtual terminal of its own (fb.h).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "fb.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <unistd.h>

static struct fb *live;
static struct termios saved_tio;
static int saved_tio_ok;
static volatile sig_atomic_t want_release, want_acquire;

static void on_release(int sig)
{
	(void)sig;
	want_release = 1;
}

static void on_acquire(int sig)
{
	(void)sig;
	want_acquire = 1;
}

int fb_open(struct fb *f, const char *dev)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	struct vt_mode vm;
	struct termios tio;

	memset(f, 0, sizeof(*f));
	f->tty = -1;
	f->fd = open(dev, O_RDWR);
	if (f->fd < 0)
		return -errno;
	if (ioctl(f->fd, FBIOGET_VSCREENINFO, &var) ||
	    ioctl(f->fd, FBIOGET_FSCREENINFO, &fix)) {
		close(f->fd);
		return -errno;
	}
	f->w = var.xres;
	f->h = var.yres;
	f->bpp = var.bits_per_pixel;
	f->line = fix.line_length;
	f->shift[0] = var.red.offset;
	f->bits[0] = var.red.length;
	f->shift[1] = var.green.offset;
	f->bits[1] = var.green.length;
	f->shift[2] = var.blue.offset;
	f->bits[2] = var.blue.length;
	if (f->bpp != 16 && f->bpp != 24 && f->bpp != 32) {
		close(f->fd);
		return -EINVAL;
	}
	f->size = fix.smem_len;
	f->mem = mmap(NULL, f->size, PROT_READ | PROT_WRITE, MAP_SHARED, f->fd,
		      0);
	if (f->mem == MAP_FAILED) {
		close(f->fd);
		return -errno;
	}
	// the terminal: graphics, raw keys, and the switches through signals
	f->tty = open("/dev/tty", O_RDWR | O_NONBLOCK);
	if (f->tty >= 0 && !ioctl(f->tty, VT_GETMODE, &vm)) {
		signal(SIGUSR1, on_release);
		signal(SIGUSR2, on_acquire);
		vm.mode = VT_PROCESS;
		vm.relsig = SIGUSR1;
		vm.acqsig = SIGUSR2;
		ioctl(f->tty, VT_SETMODE, &vm);
		ioctl(f->tty, KDSETMODE, KD_GRAPHICS);
	}
	if (f->tty >= 0 && !tcgetattr(f->tty, &saved_tio)) {
		saved_tio_ok = 1;
		tio = saved_tio;
		tio.c_lflag &= ~(ICANON | ECHO);
		tio.c_cc[VMIN] = 0;
		tio.c_cc[VTIME] = 0;
		tcsetattr(f->tty, TCSANOW, &tio);
	}
	f->shown = 1;
	f->redraw = 1;
	live = f;
	return 0;
}

static uint32_t pixel(const struct fb *f, uint32_t rgb)
{
	uint32_t v = 0;
	int k;

	for (k = 0; k < 3; k++) {
		uint32_t c = rgb >> (16 - 8 * k) & 0xFF;

		v |= (c >> (8 - f->bits[k])) << f->shift[k];
	}
	return v;
}

void fb_present(struct fb *f, struct canvas *c)
{
	unsigned int y, x, w = c->w < f->w ? c->w : f->w;
	unsigned int h = c->h < f->h ? c->h : f->h;

	if (!f->shown)
		return;
	for (y = 0; y < h; y++) {
		unsigned char *row = f->mem + (size_t)y * f->line;
		const uint32_t *src = c->px + (size_t)y * c->w;

		if (!c->dirty[y] && !f->redraw)
			continue;
		if (f->bpp == 16) {
			uint16_t buf[2048];

			for (x = 0; x < w && x < 2048; x++)
				buf[x] = pixel(f, src[x]);
			memcpy(row, buf, x * 2);
		} else if (f->bpp == 32) {
			uint32_t buf[2048];

			for (x = 0; x < w && x < 2048; x++)
				buf[x] = pixel(f, src[x]);
			memcpy(row, buf, x * 4);
		} else {
			for (x = 0; x < w; x++) {
				uint32_t v = pixel(f, src[x]);

				row[3 * x] = v;
				row[3 * x + 1] = v >> 8;
				row[3 * x + 2] = v >> 16;
			}
		}
	}
	f->redraw = 0;
	canvas_clean(c);
}

int fb_key(struct fb *f)
{
	unsigned char k;

	if (f->tty < 0 || read(f->tty, &k, 1) != 1)
		return -1;
	return k;
}

void fb_poll_switch(struct fb *f)
{
	if (want_release) {
		want_release = 0;
		f->shown = 0;
		ioctl(f->tty, VT_RELDISP, 1);
	}
	if (want_acquire) {
		want_acquire = 0;
		ioctl(f->tty, VT_RELDISP, VT_ACKACQ);
		f->shown = 1;
		f->redraw = 1;
	}
}

void fb_restore(void)
{
	struct vt_mode vm;

	if (!live || live->tty < 0)
		return;
	ioctl(live->tty, KDSETMODE, KD_TEXT);
	if (!ioctl(live->tty, VT_GETMODE, &vm)) {
		vm.mode = VT_AUTO;
		ioctl(live->tty, VT_SETMODE, &vm);
	}
	if (saved_tio_ok)
		tcsetattr(live->tty, TCSANOW, &saved_tio);
}

void fb_close(struct fb *f)
{
	if (f->mem && f->shown)
		memset(f->mem, 0, (size_t)f->line * f->h);
	fb_restore();
	if (f->mem)
		munmap(f->mem, f->size);
	if (f->tty >= 0)
		close(f->tty);
	close(f->fd);
	live = NULL;
}
