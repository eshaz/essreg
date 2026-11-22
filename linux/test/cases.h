/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * What the case groups (c_*.c) share: test signals, the paths through the
 * chip set up on its mixer, and the recordings' samples.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef ET_CASES_H
#define ET_CASES_H

#include "an.h"
#include "et.h"

#define ET_FM_RATE 49613 // PCM 1's, the music DAC's 49612.5 Hz
#define ET_FM_HZ 49612.5 // the FM's clock, the master over 480 (clock.fm)
#define ET_FM_IDLE 128 // what PCM 1 reads with the FM silent
#define ET_HZ 441.0 // the tones', as sb16test's
#define ET_HZ_R 661.5 // the right side's, where the sides differ

// signals: a sine of amplitude amp (s16 scale) on each side (0 Hz for
// none); a pattern of LFSR words; silence
void *et_tone(const struct et_fmt *f, unsigned long frames, double hz_l,
	      double hz_r, double amp);
void *et_pattern(const struct et_fmt *f, unsigned long frames,
		 unsigned int seed);
// a frame of f as the music DAC records it with Audio 2 slaved to the FM:
// s16 on both sides, swapped, plus the FM's idle level
void et_dig_frame(const struct et_fmt *f, const void *buf, unsigned long i,
		  int out[2]);

// a recording's noise floor, the rms of the quieter of its first and last
// 0.2 s at 48 kHz: a capture that starts late holds the signal's start
double et_noise_floor(const double *x, unsigned long n);

// channel ch of a recording as doubles (malloc'ed)
double *et_chan(const struct et_fmt *f, const void *buf, unsigned long frames,
		unsigned int ch);

// paths: every playback input of the mixer muted, the music DAC on FM, the
// 3-D effect off, and the master at a safe level
int et_mix_quiet(struct et_ctx *c);
// Audio 2 slaved to the FM (71h bit 0), for PCM 1's exact recording
int et_mix_dig(struct et_ctx *c, int on);
// Audio 1 records the record mixer (source 5) with Audio 2 at level
int et_mix_rec_a2(struct et_ctx *c, int level);
// the output to the second card: master and the DACs' levels
int et_mix_out(struct et_ctx *c, int a1, int a2);

// a stream of role r, format f, frames frames, at start seconds
struct et_stream et_play(enum et_role r, const struct et_fmt *f, void *data,
			 unsigned long frames, double start);
struct et_stream et_rec(enum et_role r, const struct et_fmt *f,
			unsigned long frames, double start);

// the dig path: Audio 2's pattern of frames frames recorded exactly by
// PCM 1, with another stream in the same run (or NULL), whose results come
// back in it
enum et_result et_dig_run(struct et_ctx *c, const struct et_fmt *f,
			  unsigned long frames, int seed,
			  struct et_stream *extra);
// its pattern, and how many of the recording's last n frames follow the
// pattern in order from wherever they begin in it, or -1 for none
void *et_dig_pattern(const struct et_fmt *f, unsigned long frames, int seed);
long et_dig_tail(const struct et_fmt *f, const void *pat, unsigned long frames,
		 const short *rec, unsigned long nrec, unsigned long n);

// the driver's cycles so far, by its proc file's cost line, with its
// interrupts and ring ticks: 0, or -1 with no such line
int et_cost(struct et_ctx *c, double *cycles, unsigned long *irqs,
	    unsigned long *ticks);

// the DAC that plays to the second card: Audio 1 on snd-es18xx, whose
// Audio 2 is silent on a 16-bit channel
enum et_role et_out_role(const struct et_ctx *c);

extern const struct et_fmt et_fm_fmt, et_rec2_fmt;

#endif
