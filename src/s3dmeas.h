/*
 * Measures the ES1869's 3-D effect (Spatializer) through its own output:
 * ess3d plays test tones on Audio 2 and records them on Audio 1 from
 * record source 7, which is the effect's output before the master volume
 * (DS p.59). No microphone or speaker is involved.
 *
 * Notes:
 *
 * A measurement is a list of runs, each with its own values of mixer 50h
 * and 52h-5Ah. Run 0 has the effect off and is the reference for the
 * others. Each run plays this at 48 kHz:
 * - 250 ms of silence, then a 40 ms burst of 1 kHz in both channels, which
 *   marks the start in the recording (the onset)
 * - sweep runs: a tone in both channels (M), then in opposite phase (S),
 *   at 11 frequencies from 100 Hz to 10 kHz
 * - ratio runs: 400 Hz in M and 1 kHz in S at five S/M ratios, to see
 *   whether the effect's gain follows the program (a limit)
 * - step runs: the 1 kHz S tone jumps up and back down, to see how fast
 *   the gain follows
 *
 * Each tone is measured over a whole number of its cycles (100 ms, or 20
 * ms in step runs), after time for the effect to settle, as the complex
 * amplitude of each output channel at its frequency (lock-in). The left
 * and right amplitudes give the M and S out of each tone.
 *
 * The engine doesn't touch the chip or Windows: the program around it
 * writes each run's registers, plays what s3d_fill gives, hands over what
 * it records through s3d_take, and writes the report lines that come to
 * the out function. The host tests and ess3d /sim drive it with the
 * made-up effect of s3dsim.c.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef S3DMEAS_H
#define S3DMEAS_H

#include "esstypes.h"

#define S3D_RATE 48000UL // what both devices are opened at
#define S3D_FREQS 11
#define S3D_MAX_RUNS 80
#define S3D_MAX_SEGS 32
#define S3D_MAX_WINS 64
#define S3D_PATHS 4 // M>M, S>S, M>S, S>M
#define S3D_STEP_WINS 25

// the plans
#define S3D_PLAN_FULL 0  // every setting below, about 5 minutes
#define S3D_PLAN_QUICK 1 // each register's ends only, about 2 minutes
#define S3D_PLAN_REG 2   // one register from 00h to FFh in steps of 10h

// kinds of runs
#define S3D_SWEEP 0
#define S3D_RATIO 1
#define S3D_STEP 2

// s3d_end
#define S3D_OK 0
#define S3D_NOSIGNAL 1 // no onset in the recording
#define S3D_SHORT 2    // the recording ended before the last window
#define S3D_CLIPPED 4  // added to OK: a recorded sample reached full scale

// registers of a run, in this order
#define S3D_R50 0
#define S3D_R52 1
#define S3D_R54 2
#define S3D_R56 3
#define S3D_R58 4
#define S3D_R5A 5
#define S3D_REGS 6

#define S3D_FLOOR (-999.0f) // a value under the noise floor

typedef void (*s3d_out_fn)(void *ctx, const char *line);

struct s3d_tone {
  u16 hz;    // 0 for none
  s8 side;   // 1: the same in both channels (M), -1: opposite (S)
  float amp; // of each channel, full scale 1
};

struct s3d_seg {
  u32 start, len; // play frames
  struct s3d_tone t[2];
  u8 ramp; // 5 ms fades at both ends
};

struct s3d_win {
  u32 pstart; // play frame where it starts
  u32 rstart; // recorded frame, set at the onset
  u16 rlen;   // recorded frames
  u16 ms;     // length
  u8 seg;
  float re[2][2], im[2][2]; // [tone][left, right], amplitude at the tone
};

struct s3d_run {
  char name[14];
  u8 reg[S3D_REGS];
  u8 kind;   // S3D_SWEEP, S3D_RATIO or S3D_STEP
  u8 base;   // the run the summary compares it with
  u8 slow;   // the limit is on: longer settling
  u8 result; // s3d_end's, 0xFF before the run
  // sweep: dB relative to run 0, S3D_FLOOR under the floor
  float db[S3D_PATHS][S3D_FREQS];
  // ratio: S>S at 1 kHz for each ratio, step: the end values
  float extra[S3D_STEP_WINS * 2];
};

struct s3d_meas {
  s3d_out_fn out;
  void *ctx;
  int plan, nruns;
  struct s3d_run run[S3D_MAX_RUNS];
  u32 rate_play, rate_rec;
  float ref_mm[S3D_FREQS], ref_ss[S3D_FREQS]; // run 0's gains
  float ref_mm400, ref_ss1k;                  // and at the ratio tones
  float floor_db[S3D_FREQS];                  // run 0's noise floor
  // the run being measured
  int cur;
  struct s3d_seg seg[S3D_MAX_SEGS];
  int nseg;
  struct s3d_win win[S3D_MAX_WINS];
  int nwin;
  u32 play_len, play_pos, psync;
  // the recording
  u32 rpos, rmax;
  int rstate;
  double nsum[2], nsq[2]; // noise window: sums and squares of L and R
  u32 nn;
  double e0;           // its noise power, both channels
  double sigma;        // noise of one channel, full scale 1
  double bl[2], bq[2]; // the onset search: sums and squares of a 1 ms block
  double qsum;         // noise power of the quiet blocks before the burst
  u32 qn;
  u16 bn;
  u32 bstart, cand;
  int confirm;
  u32 ron; // recorded frame of the onset
  int wcur;
  u32 wn;
  double acc[2][4]; // [tone]: I and Q of the left, then of the right
  double pc[2], ps[2], wc[2], ws[2];
  u16 peak;
};

// a plan; reg is the register (54h, 56h, 58h or 5Ah) of S3D_PLAN_REG
// report lines go to out, without line ends
void s3d_init(struct s3d_meas *m, int plan, u8 reg, s3d_out_fn out, void *ctx);

// the runs, the values of run i's registers (S3D_R50 to S3D_R5A) and its
// name
int s3d_count(const struct s3d_meas *m);
void s3d_regs(const struct s3d_meas *m, int i, u8 *reg);
const char *s3d_name(const struct s3d_meas *m, int i);

// the seconds the plan takes, roughly
int s3d_seconds(const struct s3d_meas *m);

// the report's first lines; info says where it ran (a line, or 0)
void s3d_header(struct s3d_meas *m, u32 rate_play, u32 rate_rec,
                const char *info);

// start run i: returns the frames to play, and *rec_frames the most to
// record (the frames played plus time for the devices to start)
u32 s3d_begin(struct s3d_meas *m, int i, u32 *rec_frames);

// the next n frames to play, 16-bit stereo; silence after the end
void s3d_fill(struct s3d_meas *m, s16 *pcm, u16 n);

// the next n recorded frames, 16-bit stereo; returns 1 once the run has
// every window
int s3d_take(struct s3d_meas *m, const s16 *pcm, u16 n);

// the run's results and report lines: S3D_OK, plus S3D_CLIPPED, or
// S3D_NOSIGNAL or S3D_SHORT
int s3d_end(struct s3d_meas *m);

// a run that failed twice goes in the report as such
void s3d_failed(struct s3d_meas *m, const char *why);

// the summary after the last run
void s3d_summary(struct s3d_meas *m);

// the stimulus of the current run at play frame pos (pos may have a
// fraction), full scale 1
void s3d_signal(const struct s3d_meas *m, double pos, double *l, double *r);

#endif /* S3DMEAS_H */
