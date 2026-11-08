/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * plot.h: axes on a canvas, linear or logarithmic, with ticks labelled in SI
 * prefixes, and series drawn on them, broken where a value is NaN.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef PLOT_H
#define PLOT_H

#include <stddef.h>
#include <stdint.h>

#include "draw.h"

#define C_BACK 0x101418
#define C_PANEL 0x161b21
#define C_GRID 0x2a323b
#define C_AXIS 0x6b7480
#define C_TEXT 0xd5dbe1
#define C_DIM 0x8a939d

struct axes {
	int x, y, w, h; /* the plotting area in pixels */
	double x0, x1, y0, y1;
	int logx, logy;
	const char *xunit, *yunit; /* NULL: powers of ten */
};

/* the panel, its title, grid, ticks and labels; the area from the box */
void axes_draw(struct canvas *c, struct axes *a, int bx, int by, int bw, int bh,
	       const char *title);
void axes_series(struct canvas *c, const struct axes *a, const double *x,
		 const double *y, size_t n, uint32_t rgb);
void axes_points(struct canvas *c, const struct axes *a, const double *x,
		 const double *y, size_t n, uint32_t rgb);
/* a legend entry in the panel's top right, n from 0 */
void axes_key(struct canvas *c, const struct axes *a, int n, uint32_t rgb,
	      const char *name);

/* the finite range of v, widened a little; 0 if nothing is finite */
int range_of(const double *v, size_t n, double *lo, double *hi, int log);

/* v with an SI prefix and unit, as "3.2 us", or as 1e-09 with no unit */
const char *si(double v, const char *unit, char *buf, size_t size);
const char *si_unit(double v, const char *unit, char *buf, size_t size);

#endif /* PLOT_H */
