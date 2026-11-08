// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * plot.c: axes and series on a canvas (plot.h).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include "plot.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const char *si(double v, const char *unit, char *buf, size_t size)
{
	if (!unit) {
		snprintf(buf, size, "%.0e", v);
		return buf;
	}
	// a plain number, as for dB and degrees, which take no prefix
	if (!*unit) {
		snprintf(buf, size, "%g", v);
		return buf;
	}
	return si_unit(v, unit, buf, size);
}

const char *si_unit(double v, const char *unit, char *buf, size_t size)
{
	static const char *const pre[] = { "p", "n", "u", "m", "", "k", "M" };
	double a = fabs(v);
	int k = 4;

	if (a == 0 || !isfinite(a)) {
		snprintf(buf, size, "%g %s", v, unit);
		return buf;
	}
	while (a < 0.9995 && k > 0) {
		a *= 1000;
		v *= 1000;
		k--;
	}
	while (a >= 999.5 && k < 6) {
		a /= 1000;
		v /= 1000;
		k++;
	}
	snprintf(buf, size, a < 9.95 ? "%.2g %s%s" : "%.0f %s%s", v, pre[k],
		 unit);
	return buf;
}

int range_of(const double *v, size_t n, double *lo, double *hi, int log)
{
	double a = INFINITY, b = -INFINITY;
	size_t i;

	for (i = 0; i < n; i++)
		if (isfinite(v[i]) && (!log || v[i] > 0)) {
			if (v[i] < a)
				a = v[i];
			if (v[i] > b)
				b = v[i];
		}
	if (a > b)
		return 0;
	if (log) {
		*lo = pow(10, floor(log10(a)));
		*hi = pow(10, ceil(log10(b)));
		if (*hi <= *lo)
			*hi = *lo * 10;
	} else {
		double pad = (b - a) * 0.05;

		if (pad == 0)
			pad = a ? fabs(a) * 0.1 : 1;
		*lo = a - pad;
		*hi = b + pad;
	}
	return 1;
}

static double frac(double v, double v0, double v1, int log)
{
	if (log)
		return (log10(v) - log10(v0)) / (log10(v1) - log10(v0));
	return (v - v0) / (v1 - v0);
}

// a point on the axes in pixels; 0 where it can't be shown
static int px(const struct axes *a, double x, double y, int *ox, int *oy)
{
	double fx, fy;

	if (!isfinite(x) || !isfinite(y) || (a->logx && x <= 0) ||
	    (a->logy && y <= 0))
		return 0;
	fx = frac(x, a->x0, a->x1, a->logx);
	fy = frac(y, a->y0, a->y1, a->logy);
	if (fx < -1 || fx > 2 || fy < -1 || fy > 2)
		return 0;
	*ox = a->x + (int)lrint(fx * (a->w - 1));
	*oy = a->y + a->h - 1 - (int)lrint(fy * (a->h - 1));
	return 1;
}

// ticks at 1, 2 and 5 of each decade on a linear axis, decades on a log one
static int ticks(double v0, double v1, int log, double *t, int max)
{
	int n = 0;

	if (log) {
		double d;

		for (d = pow(10, ceil(log10(v0) - 1e-9));
		     d <= v1 * (1 + 1e-9) && n < max; d *= 10)
			t[n++] = d;
	} else {
		double span = v1 - v0, step = pow(10, floor(log10(span / 5)));
		double s;

		if (span / step > 10)
			step *= span / step > 25 ? 5 : 2;
		for (s = ceil(v0 / step) * step; s <= v1 && n < max; s += step)
			t[n++] = fabs(s) < step * 1e-9 ? 0 : s;
	}
	return n;
}

void axes_draw(struct canvas *c, struct axes *a, int bx, int by, int bw, int bh,
	       const char *title)
{
	int fh = c->font ? (int)c->font->h : 16, fw = c->font ? c->font->w : 8;
	double t[32];
	char buf[32];
	int i, n, x, y;

	fill(c, bx, by, bw, bh, C_PANEL);
	text(c, bx + 6, by + 3, C_TEXT, title);
	a->x = bx + 8 * fw;
	a->y = by + fh + 8;
	a->w = bw - 8 * fw - 8;
	a->h = bh - 2 * fh - 14;
	if (a->w < 10 || a->h < 10)
		return;
	n = ticks(a->x0, a->x1, a->logx, t, 32);
	for (i = 0; i < n; i++)
		if (px(a, t[i], a->logy ? a->y0 : a->y0, &x, &y)) {
			int w;

			fill(c, x, a->y, 1, a->h, C_GRID);
			si(t[i], a->xunit, buf, sizeof(buf));
			w = (int)strlen(buf) * fw;
			if (i % (n > 8 ? 2 : 1) == 0)
				text(c, x - w / 2, a->y + a->h + 3, C_DIM, buf);
		}
	n = ticks(a->y0, a->y1, a->logy, t, 32);
	for (i = 0; i < n; i++)
		if (px(a, a->logx ? a->x0 : a->x0, t[i], &x, &y)) {
			fill(c, a->x, y, a->w, 1, C_GRID);
			si(t[i], a->yunit, buf, sizeof(buf));
			text(c, a->x - (int)strlen(buf) * fw - 4, y - fh / 2,
			     C_DIM, buf);
		}
	box(c, a->x, a->y, a->w, a->h, C_AXIS);
}

void axes_series(struct canvas *c, const struct axes *a, const double *x,
		 const double *y, size_t n, uint32_t rgb)
{
	int have = 0, lx = 0, ly = 0, cx, cy;
	size_t i;

	for (i = 0; i < n; i++) {
		if (!px(a, x[i], y[i], &cx, &cy)) {
			have = 0;
			continue;
		}
		if (cx < a->x)
			cx = a->x;
		if (cx >= a->x + a->w)
			cx = a->x + a->w - 1;
		if (cy < a->y)
			cy = a->y;
		if (cy >= a->y + a->h)
			cy = a->y + a->h - 1;
		if (have && (cx != lx || cy != ly))
			line(c, lx, ly, cx, cy, rgb);
		else if (!have)
			dot(c, cx, cy, rgb);
		lx = cx;
		ly = cy;
		have = 1;
	}
}

void axes_points(struct canvas *c, const struct axes *a, const double *x,
		 const double *y, size_t n, uint32_t rgb)
{
	size_t i;
	int cx, cy;

	for (i = 0; i < n; i++)
		if (px(a, x[i], y[i], &cx, &cy) && cx >= a->x &&
		    cx < a->x + a->w && cy >= a->y && cy < a->y + a->h)
			fill(c, cx - 1, cy - 1, 3, 3, rgb);
}

void axes_key(struct canvas *c, const struct axes *a, int n, uint32_t rgb,
	      const char *name)
{
	int fh = c->font ? (int)c->font->h : 16, fw = c->font ? c->font->w : 8;
	int w = ((int)strlen(name) + 3) * fw;
	int x = a->x + a->w - w - 4, y = a->y + 4 + n * (fh + 2);

	fill(c, x, y + fh / 2 - 1, 2 * fw - 4, 3, rgb);
	text(c, x + 2 * fw, y, C_TEXT, name);
}
