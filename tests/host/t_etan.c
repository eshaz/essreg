// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * t_etan checks es1869test's analysis (linux/test/an.c) on signals made
 * here from fixed seeds: the transform against a plain DFT, levels, a tone
 * between bins, the strongest frequency, a sine fitted in noise and one
 * over a million samples, THD+N with a known harmonic, the pitch, a zeroed
 * sample found as a glitch, delays of noise, an onset and the words of the
 * pseudo-random sequence.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "an.h"
#include "check.h"

#define PI 3.14159265358979323846
#define RATE 48000.0
#define N1 48000 // a second
#define NBIG 1000000

static double x[NBIG], y[N1];
static unsigned long long seed;

// got within tol of want, or a line with both
static void near(const char *what, double got, double want, double tol)
{
	check_count++;
	if (!(fabs(got - want) <= tol)) {
		check_failures++;
		printf("%s: %.12g, not %.12g within %g\n", what, got, want,
		       tol);
	}
}

// uniform in 0-1
static double uni(void)
{
	seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
	return ((seed >> 11) + 0.5) / 9007199254740992.0;
}

// normal, with a standard deviation of 1
static double gauss(void)
{
	double u = uni(), v = uni();

	return sqrt(-2 * log(u)) * cos(2 * PI * v);
}

static void sine(double *v, long n, double f, double amp, double phase,
		 double dc)
{
	long i;

	for (i = 0; i < n; i++)
		v[i] = dc + amp * sin(2 * PI * f * i / RATE + phase);
}

static void noise(double *v, long n, double rms)
{
	long i;

	for (i = 0; i < n; i++)
		v[i] += rms * gauss();
}

// the largest difference of an_fft from a DFT summed in long doubles
static double fft_err(long n)
{
	double *re = malloc(4 * n * sizeof(*re)), *im, *xr, *xi, err = 0;
	long i, k;

	if (!re)
		return INFINITY;
	im = re + n;
	xr = re + 2 * n;
	xi = re + 3 * n;
	for (i = 0; i < n; i++) {
		re[i] = 2 * uni() - 1;
		im[i] = 2 * uni() - 1;
		xr[i] = re[i];
		xi[i] = im[i];
	}
	an_fft(re, im, n);
	for (k = 0; k < n; k++) {
		long double sr = 0, si = 0;

		for (i = 0; i < n; i++) {
			long double a = -2 * PI * ((k * i) % n) / n;

			sr += xr[i] * cosl(a) - xi[i] * sinl(a);
			si += xr[i] * sinl(a) + xi[i] * cosl(a);
		}
		err = fmax(err, hypot(re[k] - sr, im[k] - si));
	}
	free(re);
	return err;
}

static void test_fft(void)
{
	double re[4] = { 1, 2, 3, 4 }, im[4] = { 0 };
	double r6[6] = { 1, 2, 3, 4, 5, 6 }, i6[6] = { 0 };

	seed = 1;
	CHECK(fft_err(64) < 1e-9);
	CHECK(fft_err(1024) < 1e-9);
	// 4 points by hand: 10, -2+2i, -2, -2-2i
	an_fft(re, im, 4);
	near("fft4 re0", re[0], 10, 1e-12);
	near("fft4 re1", re[1], -2, 1e-12);
	near("fft4 im1", im[1], 2, 1e-12);
	near("fft4 re2", re[2], -2, 1e-12);
	near("fft4 im3", im[3], -2, 1e-12);
	// not a power of 2: left alone
	an_fft(r6, i6, 6);
	CHECK(r6[5] == 6 && i6[0] == 0);
}

static void test_levels(void)
{
	double fs = 32767 / sqrt(2);

	sine(x, N1, 1000, 32767, 0.3, 0); // whole cycles
	near("rms", an_rms(x, N1, 0), fs, 1e-6);
	near("dbfs", an_dbfs(an_rms(x, N1, 0)), 0, 1e-9);
	near("dbfs half", an_dbfs(fs / 2), -6.0206, 1e-4);
	sine(x, N1, 1000, 32767, 0.3, 100);
	near("rms ac", an_rms(x, N1, 1), fs, 1e-6);
	near("rms dc", an_rms(x, N1, 0), sqrt(fs * fs + 100 * 100), 1e-6);
	CHECK(an_dbfs(0) == -200);
	CHECK(an_dbfs(-1) == -200);
	CHECK(an_rms(x, 0, 0) == 0);
}

static void test_tone(void)
{
	double ph;
	long i;

	// 1234.5 Hz is 123.45 bins of 4800 samples
	sine(x, 4800, 1234.5, 10000, 0.7, 0);
	near("tone amp", an_tone(x, 4800, RATE, 1234.5, &ph), 10000, 10);
	near("tone phase", ph, 0.7, 0.01);
	near("tone no phase", an_tone(x, 4800, RATE, 1234.5, NULL), 10000, 10);
	sine(x, 4800, 1234.5, 10000, -2.9, 0);
	an_tone(x, 4800, RATE, 1234.5, &ph);
	near("tone phase -2.9", ph, -2.9, 0.01);
	// with a dc and a louder tone elsewhere, the tone alone
	sine(x, 4800, 1234.5, 3000, 2.0, 500);
	sine(y, 4800, 5000, 20000, 0, 0);
	for (i = 0; i < 4800; i++)
		x[i] += y[i];
	near("tone among", an_tone(x, 4800, RATE, 1234.5, &ph), 3000, 3);
	near("tone among phase", ph, 2.0, 0.01);
	CHECK(an_tone(x, 4800, RATE, 3000, NULL) < 5);
	CHECK(an_tone(x, 1, RATE, 1000, &ph) == 0 && ph == 0);
}

static void test_peak(void)
{
	long i;

	seed = 2;
	sine(x, N1, 441, 10000, 0.3, 20);
	noise(x, N1, 10);
	near("peak 441", an_peak_hz(x, N1, RATE, 300, 600), 441, 0.01);
	// between bins, next to a louder tone outside the band
	sine(x, 4800, 441.37, 10000, 1.2, 0);
	sine(y, 4800, 1000, 30000, 0, 0);
	for (i = 0; i < 4800; i++)
		x[i] += y[i];
	near("peak 441.37", an_peak_hz(x, 4800, RATE, 300, 600), 441.37, 0.01);
	near("peak 1000", an_peak_hz(x, 4800, RATE, 0, RATE / 2), 1000, 0.01);
	CHECK(an_peak_hz(x, 4800, RATE, 600, 500) == 0);
	CHECK(an_peak_hz(x, 4800, RATE, 30000, 40000) == 0);
	memset(x, 0, 4800 * sizeof(*x));
	CHECK(an_peak_hz(x, 4800, RATE, 300, 600) == 0);
}

static void test_fit(void)
{
	struct an_fit fit;

	seed = 3;
	sine(x, N1, 441.37, 8000, 1.0, 37);
	noise(x, N1, 3);
	CHECK_EQ(an_fit_sine(x, N1, RATE, 441, 1, &fit), 0);
	near("fit f", fit.f, 441.37, 1e-4);
	near("fit amp", fit.amp / 8000, 1, 1e-3);
	near("fit phase", fit.phase, 1.0, 1e-3);
	near("fit dc", fit.dc, 37, 0.1);
	near("fit resid", fit.resid, 3, 0.05);
	// without refine at the frequency: the same sine
	CHECK_EQ(an_fit_sine(x, N1, RATE, 441.37, 0, &fit), 0);
	near("fit3 f", fit.f, 441.37, 1e-9);
	near("fit3 amp", fit.amp / 8000, 1, 1e-3);
	near("fit3 phase", fit.phase, 1.0, 1e-3);
	// started 15 % off, and a phase that wraps
	sine(x, N1, 1003.9, 12000, -3.0, -80);
	CHECK_EQ(an_fit_sine(x, N1, RATE, 870, 1, &fit), 0);
	near("fit off f", fit.f, 1003.9, 1e-6);
	near("fit off phase", fit.phase, -3.0, 1e-6);
	near("fit off dc", fit.dc, -80, 1e-6);
	CHECK(fit.resid < 1e-6);
	// silence, a frequency past Nyquist, too few samples
	memset(x, 0, N1 * sizeof(*x));
	CHECK_EQ(an_fit_sine(x, N1, RATE, 441, 1, &fit), -1);
	CHECK(fit.f == 0 && fit.amp == 0);
	CHECK_EQ(an_fit_sine(x, N1, RATE, 30000, 0, &fit), -1);
	CHECK_EQ(an_fit_sine(x, 2, RATE, 441, 0, &fit), -1);
}

// a million samples of a clean sine: the sums keep their precision
static void test_long_fit(void)
{
	struct an_fit fit;

	sine(x, NBIG, 997.13, 20000, -2.0, -5);
	CHECK_EQ(an_fit_sine(x, NBIG, RATE, 1000, 1, &fit), 0);
	near("long f", fit.f, 997.13, 1e-8);
	near("long amp", fit.amp, 20000, 1e-6);
	near("long phase", fit.phase, -2.0, 1e-8);
	near("long dc", fit.dc, -5, 1e-6);
	CHECK(fit.resid < 1e-5);
	CHECK(an_thdn(x, NBIG, RATE, 1000, 0, 0) < -150);
}

static void test_thdn(void)
{
	long i;

	sine(x, N1, 1000.3, 20000, 0.2, 10);
	CHECK(an_thdn(x, N1, RATE, 1000, 0, 0) < -120);
	CHECK(an_thdn(x, N1, RATE, 1000, 20, 20000) < -120);
	// a second harmonic 60 dB down
	for (i = 0; i < N1; i++)
		x[i] += 20 * sin(2 * PI * 2000.6 * i / RATE + 1.1);
	near("thdn", an_thdn(x, N1, RATE, 1000, 0, 0), -60, 0.5);
	near("thdn band", an_thdn(x, N1, RATE, 1000, 20, 20000), -60, 0.5);
	// the harmonic outside the band, below and above it
	CHECK(an_thdn(x, N1, RATE, 1000, 20, 1500) < -95);
	CHECK(an_thdn(x, N1, RATE, 1000, 3000, 0) < -95);
	// noise of 1/1000 the sine's rms instead
	seed = 4;
	sine(x, N1, 1000.3, 20000, 0.2, 10);
	noise(x, N1, 20000 / sqrt(2) / 1000);
	near("thdn noise", an_thdn(x, N1, RATE, 1000, 0, 0), -60, 0.2);
	// half of it in 0-12 kHz
	near("thdn noise half", an_thdn(x, N1, RATE, 1000, 0, 12000), -63.01,
	     0.2);
	memset(x, 0, N1 * sizeof(*x));
	CHECK(an_thdn(x, N1, RATE, 1000, 0, 0) == 0);
}

static void test_pitch(void)
{
	seed = 5;
	sine(x, N1, 259.82, 10000, 0.4, 300);
	near("pitch", an_pitch(x, N1, RATE, 260), 259.82, 0.01);
	noise(x, N1, 100);
	near("pitch noise", an_pitch(x, N1, RATE, 260), 259.82, 0.01);
	// a tenth of a second, where the band's edges ring into the crossings
	sine(x, 4800, 1000, 10000, 0.1, 0);
	near("pitch short", an_pitch(x, 4800, RATE, 1000), 1000, 0.05);
	// ten cycles, every crossing used
	near("pitch 10", an_pitch(x, 480, RATE, 1000), 1000, 10);
	// fewer than 3 crossings
	CHECK(an_pitch(x, 60, RATE, 1000) == 0);
	CHECK(an_pitch(x, N1, RATE, 0) == 0);
}

static void test_glitches(void)
{
	enum { WIN = 480 };
	long where[4] = { -1, -1, -1, -1 }, z = N1 / 2 + 17, i;

	sine(x, N1, 441, 10000, 0.3, 0);
	CHECK_EQ(an_glitches(x, N1, RATE, 441, WIN, 20, where, 4), 0);
	CHECK(fabs(x[z]) > 5000);
	x[z] = 0;
	CHECK_EQ(an_glitches(x, N1, RATE, 441, WIN, 20, where, 4), 1);
	CHECK(where[0] > z - WIN && where[0] <= z);
	CHECK(where[0] % (WIN / 2) == 0);
	CHECK_EQ(where[1], -1);
	CHECK_EQ(an_glitches(x, N1, RATE, 441, WIN, 20, NULL, 0), 1);
	// three, but room for two
	x[1000] += 3000;
	x[40000] = -x[40000];
	CHECK_EQ(an_glitches(x, N1, RATE, 441, WIN, 20, where, 2), 3);
	CHECK(where[0] > 1000 - WIN && where[0] <= 1000);
	CHECK(where[1] > z - WIN && where[1] <= z);
	// as 16-bit samples, clean and then with the zeroed sample
	sine(x, N1, 441, 10000, 0.3, 0);
	for (i = 0; i < N1; i++)
		x[i] = floor(x[i] + 0.5);
	CHECK_EQ(an_glitches(x, N1, RATE, 441, WIN, 20, where, 4), 0);
	x[z] = 0;
	CHECK_EQ(an_glitches(x, N1, RATE, 441, WIN, 20, where, 4), 1);
	CHECK_EQ(an_glitches(x, WIN - 1, RATE, 441, WIN, 20, where, 4), 0);
}

static void test_lag(void)
{
	enum { N = 8192 };
	double corr;
	long i;

	seed = 6;
	for (i = 0; i < N; i++)
		x[i] = 1000 * gauss() + 50;
	// b behind a by 123: b[i + 123] = a[i]
	for (i = 0; i < N; i++)
		y[i] = i >= 123 ? x[i - 123] : 1000 * gauss();
	CHECK_EQ(an_lag(x, N, y, N, 1000, &corr), 123);
	CHECK(corr > 0.99);
	near("lag corr", corr, 1, 1e-9);
	// ahead by 57, shorter, with another dc
	for (i = 0; i < 6000; i++)
		y[i] = x[i + 57] - 300;
	CHECK_EQ(an_lag(x, N, y, 6000, 1000, &corr), -57);
	CHECK(corr > 0.99);
	CHECK_EQ(an_lag(y, 6000, x, N, 1000, NULL), 57);
	// out of reach
	CHECK(an_lag(x, N, y, 6000, 50, &corr) != -57 && corr < 0.5);
	// half the noise its own
	for (i = 0; i < 6000; i++)
		y[i] = x[i + 57] + 1000 * gauss();
	CHECK_EQ(an_lag(x, N, y, 6000, 1000, &corr), -57);
	near("lag corr noisy", corr, sqrt(0.5), 0.03);
	CHECK(an_lag(x, 0, y, 6000, 1000, &corr) == 0 && corr == 0);
}

static void test_onset(void)
{
	long i;

	for (i = 0; i < 1000; i++)
		x[i] = 1000 + (i >= 500 ? 100 : 0) + (i & 1 ? 10 : -10);
	CHECK_EQ(an_onset(x, 1000, 50), 500);
	CHECK_EQ(an_onset(x, 500, 50), -1);
	CHECK_EQ(an_onset(x, 1000, 150), -1);
	CHECK_EQ(an_onset(x, 1000, 5), 1);
	CHECK_EQ(an_onset(x, 0, 50), -1);
	x[700] = 0;
	CHECK_EQ(an_onset(x + 600, 400, 200), 100);
}

// one step of x^16 + x^14 + x^13 + x^11 + 1
static unsigned int lfsr_step(unsigned int s)
{
	unsigned int bit = (s ^ (s >> 2) ^ (s >> 3) ^ (s >> 5)) & 1;

	return (s >> 1) | (bit << 15);
}

static void test_lfsr(void)
{
	unsigned int st = 0, ref = 0xACE1, first, w;
	int i, j, bad = 0;
	long n;

	// from 0 as from 0xACE1, the words of other runs
	CHECK_EQ(an_lfsr(&st), 0x4722);
	CHECK_EQ(st, 0x4722);
	CHECK_EQ(an_lfsr(&st), 0xC437);
	CHECK_EQ(an_lfsr(&st), 0xE39D);
	st = 0x10000;
	CHECK_EQ(an_lfsr(&st), 0x4722);
	// 16 single steps a word
	st = 0;
	for (i = 0; i < 5000; i++) {
		for (j = 0; j < 16; j++)
			ref = lfsr_step(ref);
		w = an_lfsr(&st);
		bad += w != ref || w > 0xFFFF || !w;
	}
	CHECK_EQ(bad, 0);
	// 16 is prime to 65535, so the words repeat after 65535 of them
	st = 12345;
	first = an_lfsr(&st);
	for (n = 1; n < 70000 && an_lfsr(&st) != first; n++)
		;
	CHECK(n > 4096);
	CHECK_EQ(n, 65535);
}

int main(void)
{
	test_fft();
	test_levels();
	test_tone();
	test_peak();
	test_fit();
	test_long_fit();
	test_thdn();
	test_pitch();
	test_glitches();
	test_lag();
	test_onset();
	test_lfsr();
	return CHECK_DONE("t_etan");
}
