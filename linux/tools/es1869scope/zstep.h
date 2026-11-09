/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * zstep.h: the gap of Audio 2's DAC at its sign, for es1869scope -E. The
 * DAC puts its codes from 0 up a little higher than those below. Each side
 * of the loop's output is fitted per bin to both sides of what the DAC took
 * and to the sign of its own side's codes, a third input; that input's
 * transfer over the side's own path is the gap, as the driver's a2_zstep_l
 * and a2_zstep_r take it, measured from the music alone.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ZSTEP_H
#define ZSTEP_H

struct zstep {
	unsigned int bins;
	/*
	 * per bin: the inputs' powers and their cross (L times the conjugate
	 * of R); for each side the sign's power, its crosses with L and R,
	 * and the output's with L, R and the sign, re and im interleaved
	 */
	double *xl, *xr, *lr;
	double *qq[2], *ql[2], *qr[2], *yl[2], *yr[2], *yq[2];
	unsigned long segments;
};

/* 0, or -1 without memory */
int zstep_init(struct zstep *z, unsigned int bins);
void zstep_free(struct zstep *z);
void zstep_reset(struct zstep *z);

/*
 * a segment's spectra, bins of re and im each: the inputs (xl, xr), the
 * sign of each side's codes less a half (ql, qr) and the outputs (yl, yr)
 */
void zstep_add(struct zstep *z, const double *xl, const double *xr,
	       const double *ql, const double *qr, const double *yl,
	       const double *yr);

/*
 * side c's gap in the inputs' units, the median over bins lo to hi of the
 * sign's transfer over the side's own path, with its spread (the median
 * deviation over the root of the bins) in *spread if not NULL; 0 with
 * nothing to stand on
 */
double zstep_gap(const struct zstep *z, int c, unsigned int lo, unsigned int hi,
		 double *spread);

#endif /* ZSTEP_H */
