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
 *   the level at which the limit holds the S out
 * - band runs: an S tone at each frequency over 400 Hz in M, to see the
 *   limit at each frequency
 * - pan runs: 1 kHz panned from left to right, to see where each place
 *   comes out, and whether the effect pushes it past a speaker
 * - step runs: the 1 kHz S tone steps from -12 to 0 dB re the M tone and
 *   back, to see how fast the limit holds the boost down, at which level,
 *   and how fast it lets it back up
 *
 * Each tone is measured over a whole number of its cycles (100 ms, or 20
 * ms early in step runs), after time for the effect to settle, as the
 * complex amplitude of each output channel at its frequency (lock-in).
 * The phases count from the onset, so tones of one run can be compared:
 * the S out of an S tone against the M out of an M tone at the same
 * frequency gives the phase the effect adds to S, which a coefficient
 * with its sign flipped turns by 180 degrees.
 *
 * With the limit on, the S out of a run with the 1 kHz S tone gives the
 * boost's own gain: S>S is 1 + a B, where B is the boost with the limit
 * off, from a sweep run with the same 50h (without bit 0) and 52h, and a
 * is what the limit leaves of it. On the card a moves at a fixed rate in
 * dB, which the summary gives in dB a second.
 *
 * Every plan ends with the effect off again, which shows how much the
 * path from the DAC to the ADC moved during the measurement.
 *
 * The engine doesn't touch the chip or Windows: the program around it
 * writes each run's registers, plays what s3d_fill gives, hands over what
 * it records through s3d_take, and writes the report lines that come to
 * the out function. The host tests and ess3d /sim drive it with the
 * effect of s3dsim.c, which follows one card's measurement.
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
#define S3D_MAX_RUNS 64
#define S3D_MAX_SEGS 32
#define S3D_MAX_WINS 96
#define S3D_PATHS 4      // M>M, S>S, M>S, S>M
#define S3D_PANS 5       // pan runs: 0, 22.5, 45, 67.5 and 90 degrees
#define S3D_FINE 25      // step runs: 20 ms windows over the first 500 ms
#define S3D_COARSE_UP 13 // then 100 ms windows to 1.8 s after the step up
#define S3D_COARSE_DN 25 // and to 3 s after the step down
#define S3D_QUIET 64     // 1 ms blocks the noise floor comes from
#define S3D_UP (S3D_FINE + S3D_COARSE_UP)
#define S3D_DN (S3D_FINE + S3D_COARSE_DN)

// the plans
#define S3D_PLAN_FULL 0  // every kind of run below, about 6 minutes
#define S3D_PLAN_QUICK 1 // each kind once or twice, about 2 minutes
#define S3D_PLAN_REG 2   // one register from 00h to FFh, with the limit
#define S3D_PLAN_LIMIT 3 // each bit of 54h-5Ah with the limit, 7 minutes

// kinds of runs
#define S3D_SWEEP 0
#define S3D_RATIO 1
#define S3D_STEP 2
#define S3D_PAN 3
#define S3D_BAND 4

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
  u16 hz;     // 0 for none
  float l, r; // amplitude in each channel, full scale 1, with its sign
};

struct s3d_seg {
  u32 start, len; // play frames
  struct s3d_tone t[2];
  u8 ramp; // 5 ms fades: bit 0 at the start, bit 1 at the end
};

struct s3d_win {
  u32 pstart; // play frame where it starts
  u32 rstart; // recorded frame, set at the onset
  u16 rlen;   // recorded frames
  u16 ms;     // length
  u8 seg;
  float psi[2];             // [tone] phase of the stimulus at the start
  float zr[2][2], zi[2][2]; // [tone][left, right], A e^(j phase)
};

struct s3d_run {
  char name[14];
  u8 reg[S3D_REGS];
  u8 kind;   // S3D_SWEEP to S3D_BAND
  u8 base;   // the run the summary compares it with
  u8 slow;   // the limit is on: longer settling
  u8 result; // s3d_end's, 0xFF before the run
  union {
    struct {
      // dB relative to run 0, S3D_FLOOR under the floor; band runs only
      // have S>S, and the M>M of the 400 Hz tone
      float db[S3D_PATHS][S3D_FREQS];
      // degrees: the phase the effect adds to S against M, then M>S
      // against M>M and S>M against S>S in the same window
      float ph[3][S3D_FREQS];
    } sw;
    struct {
      float ss[5], mm[5]; // S>S at 1 kHz and M>M at 400 Hz, dB
      float b_re, b_im;   // the boost B at 1 kHz, 0 if unknown
    } ra;
    struct {
      float deg[S3D_PANS], lev[S3D_PANS]; // where it comes out, its level
    } pa;
    struct {
      float up[S3D_UP], dn[S3D_DN]; // S>S after the step up and down
      float steady[3];              // low, high, low again
      float b_re, b_im;             // the boost B at 1 kHz, 0 if unknown
    } st;
  } u;
};

// what the limit does in a step run, from the boost's gain in dB
struct s3d_lim {
  // steady: before the step, at 0 dB S/M, and after
  float low, high, again;
  // the S out where it holds the boost down, re the M in, dB, or
  // S3D_FLOOR
  float held;
  float fall; // dB a second after the step up, 0 if it holds
  float rise; // dB a second after the step down, 0 if none
  int delay;  // ms before it rises 3 dB, -1 if it doesn't
};

struct s3d_meas {
  s3d_out_fn out;
  void *ctx;
  int plan, nruns;
  struct s3d_run run[S3D_MAX_RUNS];
  u32 rate_play, rate_rec;
  float ref_mm[S3D_FREQS], ref_ss[S3D_FREQS]; // run 0's gains
  float ref_ph[S3D_FREQS]; // run 0's phase of S against M, degrees
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
  float quiet[S3D_QUIET]; // power of the last 1 ms blocks before the burst
  int nquiet, quiet_at;
  double e0;           // the noise power of the first 50 ms, both channels
  double sigma;        // noise of one channel, full scale 1
  double bl[2], bq[2]; // sums and squares of the 1 ms block so far
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

// the report's first lines; info says where it ran (lines, or 0)
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

// what the limit did in step run i: 0, or -1 without B or a result
int s3d_limit(const struct s3d_meas *m, int i, struct s3d_lim *l);

// the stimulus of the current run at play frame pos (pos may have a
// fraction), full scale 1
void s3d_signal(const struct s3d_meas *m, double pos, double *l, double *r);

#endif /* S3DMEAS_H */
