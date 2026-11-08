// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * draw.c: a picture in memory, and a PSF console font to write on it
 * (draw.h).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "draw.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

int canvas_init(struct canvas *c, unsigned int w, unsigned int h)
{
	c->w = w;
	c->h = h;
	c->px = calloc((size_t)w * h, sizeof(*c->px));
	c->dirty = malloc(h);
	if (!c->px || !c->dirty) {
		canvas_free(c);
		return -ENOMEM;
	}
	memset(c->dirty, 1, h);
	return 0;
}

void canvas_free(struct canvas *c)
{
	free(c->px);
	free(c->dirty);
	c->px = NULL;
	c->dirty = NULL;
}

void canvas_clean(struct canvas *c)
{
	memset(c->dirty, 0, c->h);
}

void fill(struct canvas *c, int x, int y, int w, int h, uint32_t rgb)
{
	int i, j;

	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > (int)c->w)
		w = c->w - x;
	if (y + h > (int)c->h)
		h = c->h - y;
	for (j = 0; j < h; j++) {
		uint32_t *p = c->px + (size_t)(y + j) * c->w + x;

		for (i = 0; i < w; i++)
			p[i] = rgb;
		c->dirty[y + j] = 1;
	}
}

void dot(struct canvas *c, int x, int y, uint32_t rgb)
{
	if (x < 0 || y < 0 || x >= (int)c->w || y >= (int)c->h)
		return;
	c->px[(size_t)y * c->w + x] = rgb;
	c->dirty[y] = 1;
}

void line(struct canvas *c, int x0, int y0, int x1, int y1, uint32_t rgb)
{
	int dx = abs(x1 - x0), dy = -abs(y1 - y0);
	int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;

	// far off the picture: nothing worth the steps
	if ((x0 < -10000 && x1 < -10000) || (y0 < -10000 && y1 < -10000) ||
	    (x0 > 20000 && x1 > 20000) || (y0 > 20000 && y1 > 20000))
		return;
	for (;;) {
		int e2 = 2 * err;

		dot(c, x0, y0, rgb);
		if (x0 == x1 && y0 == y1)
			break;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

void box(struct canvas *c, int x, int y, int w, int h, uint32_t rgb)
{
	fill(c, x, y, w, 1, rgb);
	fill(c, x, y + h - 1, w, 1, rgb);
	fill(c, x, y, 1, h, rgb);
	fill(c, x + w - 1, y, 1, h, rgb);
}

int text(struct canvas *c, int x, int y, uint32_t rgb, const char *s)
{
	const struct font *f = c->font;
	int x0 = x;

	if (!f)
		return 0;
	for (; *s; s++, x += f->w) {
		unsigned int g = (unsigned char)*s, r, k;
		const unsigned char *b;

		if (g >= f->glyphs)
			g = '?';
		b = f->bits + (size_t)g * f->h * f->stride;
		for (r = 0; r < f->h; r++)
			for (k = 0; k < f->w; k++)
				if (b[r * f->stride + k / 8] &
				    (0x80 >> (k % 8)))
					dot(c, x + k, y + r, rgb);
	}
	return x - x0;
}

int textf(struct canvas *c, int x, int y, uint32_t rgb, const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return text(c, x, y, rgb, buf);
}

// --- fonts -------------------------------------------------------------------

#define PSF1_MAGIC 0x0436
#define PSF2_MAGIC 0x864ab572

int font_load(struct font *f, const char *path)
{
	unsigned char head[32];
	gzFile z = gzopen(path, "rb");
	size_t size;
	int n;

	if (!z)
		return -errno ? -errno : -ENOENT;
	n = gzread(z, head, 4);
	if (n == 4 && (head[0] | head[1] << 8) == PSF1_MAGIC) {
		f->w = 8;
		f->h = head[3];
		f->glyphs = head[2] & 1 ? 512 : 256;
		f->stride = 1;
	} else if (n == 4 && gzread(z, head + 4, 28) == 28 &&
		   (uint32_t)(head[0] | head[1] << 8 | head[2] << 16 |
			      (uint32_t)head[3] << 24) == PSF2_MAGIC) {
		uint32_t hs = head[8] | head[9] << 8 | head[10] << 16;

		f->glyphs = head[16] | head[17] << 8 | head[18] << 16;
		f->h = head[24] | head[25] << 8;
		f->w = head[28] | head[29] << 8;
		f->stride = (f->w + 7) / 8;
		if (hs > 32)
			gzseek(z, hs, SEEK_SET);
	} else {
		gzclose(z);
		return -EINVAL;
	}
	if (!f->w || !f->h || f->h > 64 || f->w > 32 || f->glyphs < 128) {
		gzclose(z);
		return -EINVAL;
	}
	size = (size_t)f->glyphs * f->h * f->stride;
	f->bits = malloc(size);
	if (!f->bits || gzread(z, f->bits, size) != (int)size) {
		free(f->bits);
		f->bits = NULL;
		gzclose(z);
		return -EINVAL;
	}
	gzclose(z);
	return 0;
}

int font_find(struct font *f)
{
	static const char *const names[] = {
		"default8x16.psfu.gz",	  "default8x16.psf.gz",
		"lat1-16.psfu.gz",	  "Lat15-Fixed16.psf.gz",
		"Uni2-Fixed16.psf.gz",	  "cp850-8x16.psfu.gz",
		"LatGrkCyr-8x16.psfu.gz",
	};
	static const char *const dirs[] = { "/usr/share/consolefonts",
					    "/usr/share/kbd/consolefonts",
					    "/lib/kbd/consolefonts" };
	unsigned int i, j;
	char path[256];

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		for (j = 0; j < sizeof(dirs) / sizeof(dirs[0]); j++) {
			snprintf(path, sizeof(path), "%s/%s", dirs[j],
				 names[i]);
			if (!font_load(f, path))
				return 0;
		}
	return -ENOENT;
}

int canvas_ppm(const struct canvas *c, const char *path)
{
	FILE *f = fopen(path, "wb");
	size_t i, n = (size_t)c->w * c->h;

	if (!f)
		return -errno;
	fprintf(f, "P6\n%u %u\n255\n", c->w, c->h);
	for (i = 0; i < n; i++) {
		unsigned char rgb[3] = { c->px[i] >> 16, c->px[i] >> 8,
					 c->px[i] };

		fwrite(rgb, 1, 3, f);
	}
	if (fclose(f))
		return -errno;
	return 0;
}
