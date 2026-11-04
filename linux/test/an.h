/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * es1869test's analysis: spectra, tones fitted to recordings, distortion,
 * glitches, delays and pitch. Every function takes one channel as doubles
 * in s16 scale (et_unpack) and keeps no state.
 *
 * Notes:
 *
 * The suite builds for the Pentium II with x87 maths, so nothing here
 * needs SSE, and the arrays are the caller's or malloc'ed per call.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ET_AN_H
#define ET_AN_H

// a sine fitted to a recording: x[i] = dc + amp * sin(2 pi f i / rate + phase)
struct an_fit {
	double f; // Hz
	double amp; // peak, s16 scale
	double phase; // radians at sample 0
	double dc;
	double resid; // the rms left after the fit
};

// the in-place FFT of n complex values, n a power of 2
void an_fft(double *re, double *im, long n);

// the rms of x, without its mean when ac
double an_rms(const double *x, long n, int ac);
// dB of a level against a full-scale sine (32767 peak)
double an_dbfs(double rms);

// the amplitude (peak) and phase of frequency f in x, by a Goertzel filter
// over a Hann window
double an_tone(const double *x, long n, double rate, double f, double *phase);

// the strongest frequency in lo-hi Hz, from a windowed spectrum with the
// peak interpolated
double an_peak_hz(const double *x, long n, double rate, double lo, double hi);

// a sine near f0 fitted by least squares; with refine the frequency too;
// 0, or -1 if the frequency ran away (fit all zeros then)
int an_fit_sine(const double *x, long n, double rate, double f0, int refine,
		struct an_fit *fit);

// THD+N in dB: what is left after the fitted sine (and its dc), against
// the sine, in the band lo-hi Hz (0 for no limit)
double an_thdn(const double *x, long n, double rate, double f0, double lo,
	       double hi);

// the strongest thing in the band lo-hi Hz of what's left in r (the fitted
// tones taken off), in dB against amp, with where it is to at (or NULL),
// from a window's spectrum of the largest power of 2 of samples
double an_spur(const double *r, long n, double rate, double lo, double hi,
	       double amp, double *at);

// a sine's samples taken off x, for an_spur: the fit of frequency f, whose
// amplitude comes back
double an_sub_sine(double *x, long n, double rate, double f);

// the pitch near f0 from the zero crossings of a band around it
double an_pitch(const double *x, long n, double rate, double f0);

// glitches in a tone of frequency f: windows of win samples whose residual
// after a local fit stands out by thr dB from the median; their starts go
// to where, at most max; the count
int an_glitches(const double *x, long n, double rate, double f, long win,
		double thr, long *where, int max);

// the lag of b behind a, -maxlag to maxlag samples, by cross-correlation;
// the normalized peak to corr
long an_lag(const double *a, long na, const double *b, long nb, long maxlag,
	    double *corr);

// the first sample of x past thr away from its first sample's level
long an_onset(const double *x, long n, double thr);

// a pseudo-random sequence of 16-bit words from seed (x^16+x^14+x^13+x^11+1)
unsigned int an_lfsr(unsigned int *state);

#endif
