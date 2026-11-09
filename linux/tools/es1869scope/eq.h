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
 * by least squares with the gain at 0 Hz held at 1, and nearest prior (or 0
 * for NULL) where the targets leave them free; 0, or -1 if the fit has
 * nothing to stand on or a tap would leave the driver's range of 2^30
 */
int eq_fit_sides(const double *f, const double *target, const double *w, int n,
		 double rate, const long *prior, long *sides);

/*
 * what the music was measured under: the stream's rate, which Audio 1
 * records at, the DAC's at 1, 2 or 3 times it, Audio 1's correction as its
 * last start took it, and the rate correction (0 off, 1 trim, 2 clock)
 */
struct eq_setup {
	unsigned int rate, factor;
	int a1c, mode;
};

#define EQ_REST 1 /* Audio 1 rests: the DAC would stall, or it can't record */
#define EQ_COUNT 2 /* the run goes into the trends and the refit */
#define EQ_TRIM 4 /* the clock's trim may move */
#define EQ_CAL_RATE 44100 /* the rate es1869cal measures the record path at */
#define EQ_A1_MAX 52920 /* the highest rate Audio 1 records (a1c_max_rate) */

/*
 * what music under now may do, against base, the setup the trends started
 * under (NULL before they have one): EQ_ flags, and in why (len bytes, at
 * least 1) the reason for what it may not do, else ""
 */
int eq_policy(const struct eq_setup *now, const struct eq_setup *base,
	      char *why, unsigned int len);

#endif /* EQ_H */
