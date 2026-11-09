/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * eq.h: corrections over time, for es1869scope -E. A value per block (the
 * loop's deviation in a band, or the Clock mode's residual against the
 * steered clock) kept with its time, its trend a line by least squares
 * through the blocks kept, predicted ahead by the latency of the correction
 * that follows it; and the DAC correction's side taps, fitted to a target
 * response, in the form the driver's "Audio 2 Treble Correction Taps" takes.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef EQ_H
#define EQ_H

#define EQ_HIST 16 /* blocks a trend goes through */
#define EQ_SIDES 16 /* the DAC correction's taps each side (ES_RS_FIT) */
#define EQ_Q30 1073741824.0

struct eq_series {
	double t[EQ_HIST], v[EQ_HIST];
	int n, next;
};

void eq_reset(struct eq_series *s);
void eq_push(struct eq_series *s, double t, double v);
/*
 * the trend at time t: the line through the blocks kept, or their mean while
 * fewer than three, or 0 for none; the slope a second in *slope if not NULL
 */
double eq_predict(const struct eq_series *s, double t, double *slope);

/* the response of side taps (Q30, a stream frame apart, the centre making
 * their sum 1) at f Hz of a stream at rate */
double eq_sides_gain(const long *sides, double f, double rate);
/*
 * side taps whose response comes nearest target[k] at f[k], weighted by w[k],
 * by least squares with the gain at 0 Hz held at 1; 0, or -1 if the fit has
 * nothing to stand on
 */
int eq_fit_sides(const double *f, const double *target, const double *w, int n,
		 double rate, long *sides);

#endif /* EQ_H */
