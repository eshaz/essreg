/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * draw.h: a picture in memory, 0xRRGGBB a pixel, with lines, boxes and text
 * in a PSF console font, and the rows changed since it last went out.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef DRAW_H
#define DRAW_H

#include <stdint.h>

struct font {
	unsigned int w, h, glyphs, stride; /* stride: bytes a glyph row */
	unsigned char *bits;
};

struct canvas {
	unsigned int w, h;
	uint32_t *px;
	unsigned char *dirty; /* a byte a row */
	const struct font *font;
};

int canvas_init(struct canvas *c, unsigned int w, unsigned int h);
void canvas_free(struct canvas *c);
void canvas_clean(struct canvas *c);

void fill(struct canvas *c, int x, int y, int w, int h, uint32_t rgb);
void dot(struct canvas *c, int x, int y, uint32_t rgb);
void line(struct canvas *c, int x0, int y0, int x1, int y1, uint32_t rgb);
void box(struct canvas *c, int x, int y, int w, int h, uint32_t rgb);

/* text at x, y (its top left); returns the width it took */
int text(struct canvas *c, int x, int y, uint32_t rgb, const char *s);
int textf(struct canvas *c, int x, int y, uint32_t rgb, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));

/* a PSF 1 or 2 font, gzipped or not: 0 or -errno */
int font_load(struct font *f, const char *path);
/* the first of the usual console fonts that loads */
int font_find(struct font *f);

/* the picture as a binary PPM */
int canvas_ppm(const struct canvas *c, const char *path);

#endif /* DRAW_H */
