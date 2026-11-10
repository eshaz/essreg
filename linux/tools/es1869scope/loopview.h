/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * loopview.h: the loop view: a path's response from what went into it and
 * what came back, by loop.c. Live, it plays its own noise on Audio 2 and
 * records Audio 1 through the record mixer, the ES1869's internal loop, with
 * the mixer and Audio 2's rate settings set for that and put back at the end.
 * From files, it takes a WAV of what was played and one of what came back.
 * With a monitor, it takes what Audio 2 plays anyway, the music, as what went
 * in, and leaves Audio 2 and the output alone.
 * Drawn: each channel's magnitude against 1 kHz, its phase past the delay,
 * what isn't the path (the incoherent part and the crosstalk), and the
 * impulse response.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 */

#ifndef LOOPVIEW_H
#define LOOPVIEW_H

#include <stdio.h>

#include "draw.h"

struct loopview_cfg {
	const char *play, *capture, *ctl; /* live: the devices, the mixer */
	/*
	 * or what Audio 2 plays anyway, from the monitor (PCM device 3), in
	 * place of the view's own noise
	 */
	const char *monitor;
	const char *ref, *rec; /* or files: what was played, what came back */
	unsigned int rate;
	double level; /* the noise's rms, dBFS */
	int gain[2]; /* "PCM Capture Volume" (69h) and "Capture Volume" */
	/*
	 * the calibration of Audio 1's correction filter: the loop at 2x with
	 * Audio 2's treble correction on, whose DAC is then flat to 20 kHz,
	 * measured for secs; taps fitted to take back the record path's
	 * response, loaded and switched on; and the loop measured again
	 */
	int calibrate;
	double secs;
	/*
	 * corrections over time, beside the music (with monitor): in blocks of
	 * secs, the loop's deviation in bands and the Clock mode's residual
	 * against the steered clock, each a trend predicted a block ahead; the
	 * DAC's correction refitted to the drift since the first blocks and
	 * loaded live, and a2_trim_ppb trimmed (as root); lines a block to
	 * log, or stdout
	 */
	int equalize;
	const char *log;
	/*
	 * where the engine shares what it knows with a viewer, eqstate.h's
	 * file, with the commands' FIFO eq.cmd beside it; NULL for none
	 */
	const char *state;
	/*
	 * no screen: the picture only at the end, or every few blocks with
	 * equalize, since a drawing takes longer on the Armada's Celeron than
	 * Audio 1's buffer lasts
	 */
	int headless;
};

struct loopview;

/* NULL on failure, with the reason on stderr, and nothing left changed */
struct loopview *loopview_open(const struct loopview_cfg *cfg);
/* the streams stopped and the mixer put back */
void loopview_close(struct loopview *v);

/* the whole view again */
void loopview_frame(struct loopview *v, struct canvas *c);
/*
 * what has come since, redrawn at most twice a second; 1 when the picture
 * changed, 2 when it shows a block of the corrections over time that just
 * ended, 0 if not, -1 when the streams failed for good
 */
int loopview_step(struct loopview *v, struct canvas *c);
/* c starts again, t turns Audio 2's treble correction over, o its rate */
void loopview_key(struct loopview *v, int key);
/* the seconds measured since the last start */
double loopview_seconds(const struct loopview *v);
// whether nothing plays: with the monitor, after LV_IDLE_S seconds with no
// frame from it, Audio 1 rests and a step only looks for the music, so that
// the caller can wait longer between steps
int loopview_idle(const struct loopview *v);
/* the measurement over: secs of it, or the calibration through; never while
 * it equalizes */
int loopview_done(const struct loopview *v);
/* the figures as lines of text, for tests and logs */
void loopview_report(struct loopview *v, FILE *f);

#endif /* LOOPVIEW_H */
