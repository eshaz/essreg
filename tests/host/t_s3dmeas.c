/*
 * t_s3dmeas runs ess3d's measurement of the 3-D effect (src/s3dmeas.c)
 * against the effect of src/s3dsim.c, made after one card's measurement,
 * and checks that the report finds each part of it again: the boost's
 * shape and phase, its level in 0.75 dB steps, the imbalance of the
 * channels, the model's width made from mono, the registers that do
 * nothing with the limit off, where panned tones come out, and the limit:
 * the level it holds, which 54h sets, and how fast it falls and rises,
 * which 58h sets. It also checks the measurement with devices that start
 * apart, a recording at another rate, and a recording with no signal.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "s3dmeas.h"
#include "s3dsim.h"

#define BLOCK 4096
#define MAX_LINES 1000
#define PI 3.14159265358979

static struct s3d_meas meas;
static struct s3dsim sim;
static s16 play[2 * BLOCK], rec[2 * BLOCK];
static char report[MAX_LINES][240];
static int nlines;
static long report_bytes;
static int verbose;
static int click; // a DAC's click and its settling in the first 50 ms

static void out(void *ctx, const char *line) {
  (void)ctx;
  if (verbose)
    printf("%s\n", line);
  report_bytes += (long)strlen(line) + 2;
  if (nlines < MAX_LINES) {
    strncpy(report[nlines], line, sizeof(report[0]) - 1);
    nlines++;
  }
}

// the first line from line from that starts with start, or -1
static int find_from(int from, const char *start) {
  int i;

  for (i = from < 0 ? 0 : from; i < nlines; i++)
    if (!strncmp(report[i], start, strlen(start)))
      return i;
  return -1;
}

static int find_line(const char *start) { return find_from(0, start); }

// what a DAC and the mixer can do as the playback starts: a click and a
// step that settles over 10 ms, 10 ms into the recording, in frames from
// fed on
static void add_click(s16 *pcm, u16 n, u32 fed) {
  u32 at;
  u16 k;
  double v;

  for (k = 0; click && k < n; k++) {
    at = fed + k;
    if (at < 480 || at >= 480 + 2400)
      continue;
    v = 3000 * exp(-((double)at - 480) / 480) + (at < 520 ? 12000 : 0);
    pcm[2 * k] = (s16)(pcm[2 * k] + v);
    pcm[2 * k + 1] = (s16)(pcm[2 * k + 1] + v / 2);
  }
}

// run i through the effect: lead frames of silence come first, as a
// recording that starts before the playback
static int run_sim(int i, u32 lead) {
  u32 rec_frames, fed = 0;
  u8 reg[S3D_REGS];
  u16 n;
  int full = 0;

  s3d_regs(&meas, i, reg);
  s3dsim_regs(&sim, reg);
  s3d_begin(&meas, i, &rec_frames);
  memset(rec, 0, sizeof(rec));
  while (lead && !full) {
    n = lead > BLOCK ? BLOCK : (u16)lead;
    memset(rec, 0, sizeof(rec));
    add_click(rec, n, fed);
    full = s3d_take(&meas, rec, n);
    lead -= n;
    fed += n;
  }
  while (!full && fed < rec_frames) {
    n = BLOCK;
    s3d_fill(&meas, play, n);
    s3dsim_run(&sim, play, rec, n);
    add_click(rec, n, fed);
    full = s3d_take(&meas, rec, n);
    fed += n;
  }
  return s3d_end(&meas);
}

// the plan through the effect, every run good, then the summary
static void run_plan(int plan, u8 reg, const char *info) {
  int i;

  nlines = 0;
  report_bytes = 0;
  s3d_init(&meas, plan, reg, out, 0);
  s3dsim_reset(&sim, S3D_RATE);
  s3d_header(&meas, S3D_RATE, S3D_RATE, info);
  for (i = 0; i < s3d_count(&meas); i++)
    CHECK_EQ(run_sim(i, 2000 + 111 * i), S3D_OK);
  s3d_summary(&meas);
}

static int run_named(const char *name) {
  int i;

  for (i = 0; i < s3d_count(&meas); i++)
    if (!strcmp(s3d_name(&meas, i), name))
      return i;
  return -1;
}

static const struct s3d_run *run(int i) { return &meas.run[i]; }

// the run's value in dB, path p (0 to 3) at frequency index k
static double val(int i, int p, int k) { return run(i)->u.sw.db[p][k]; }

// and its phase q (0 S>S, 1 M>S, 2 S>M) in degrees
static double phase(int i, int q, int k) { return run(i)->u.sw.ph[q][k]; }

static int has(double v) { return v > S3D_FLOOR + 1; }

static double wrap(double deg) {
  while (deg > 180)
    deg -= 360;
  while (deg <= -180)
    deg += 360;
  return deg;
}

// what the effect does with run i's registers at hz
static void response(int i, double hz, double *re, double *im) {
  u8 reg[S3D_REGS];

  s3d_regs(&meas, i, reg);
  s3dsim_response(reg, S3D_RATE, hz, re, im);
}

static double model_db(int i, int p, double hz) {
  double re[4], im[4], g;

  response(i, hz, re, im);
  g = sqrt(re[p] * re[p] + im[p] * im[p]);
  return g > 0 ? 20 * log10(g) : S3D_FLOOR;
}

// the phase the report gives for phase q: S>S and M>S against M>M, S>M
// against S>S
static double model_deg(int i, int q, double hz) {
  static const int path[3] = {1, 2, 3}, ref[3] = {0, 0, 1};
  double re[4], im[4], a, b;

  response(i, hz, re, im);
  a = atan2(im[path[q]], re[path[q]]);
  b = atan2(im[ref[q]], re[ref[q]]);
  return wrap((a - b) * 180 / PI);
}

static const double hz[S3D_FREQS] = {100,  160,  250,  400,  630,  1000,
                                     1600, 2500, 4000, 6300, 10000};

// every frequency of path p of run i matches the effect within tol dB
static void check_path(int i, int p, double tol) {
  int k;
  double want, got;

  for (k = 0; k < S3D_FREQS; k++) {
    want = model_db(i, p, hz[k]);
    got = val(i, p, k);
    CHECK(has(got) && fabs(want - got) < tol);
    if (!has(got) || fabs(want - got) >= tol)
      printf("  run %s path %d at %g Hz: %.2f dB, the effect %.2f dB\n",
             s3d_name(&meas, i), p, hz[k], got, want);
  }
}

// and every phase q there is, within tol degrees; returns how many
static int check_phase(int i, int q, double tol) {
  int k, n = 0;
  double want, got;

  for (k = 0; k < S3D_FREQS; k++) {
    got = phase(i, q, k);
    if (!has(got))
      continue;
    n++;
    want = model_deg(i, q, hz[k]);
    CHECK(fabs(wrap(got - want)) < tol);
    if (fabs(wrap(got - want)) >= tol)
      printf("  run %s phase %d at %g Hz: %.2f degrees, the effect %.2f\n",
             s3d_name(&meas, i), q, hz[k], got, want);
  }
  return n;
}

// paths p of run i the same as run j's within 0.1 dB
static void check_same(int i, int j, int p) {
  int k;

  for (k = 0; k < S3D_FREQS; k++)
    CHECK(fabs(val(i, p, k) - val(j, p, k)) < 0.1);
}

// where the effect puts 1 kHz panned to th degrees, and its level in dB,
// as the report works them out
static void model_pan(int i, double th, double *deg, double *lev) {
  double re[4], im[4], m, s, lr, li, rr, ri, ur, ui, a;

  response(i, 1000, re, im);
  m = (cos(th * PI / 180) + sin(th * PI / 180)) / 2;
  s = (cos(th * PI / 180) - sin(th * PI / 180)) / 2;
  // M out and S out, then left and right
  lr = re[0] * m + re[3] * s + re[1] * s + re[2] * m;
  li = im[0] * m + im[3] * s + im[1] * s + im[2] * m;
  rr = re[0] * m + re[3] * s - re[1] * s - re[2] * m;
  ri = im[0] * m + im[3] * s - im[1] * s - im[2] * m;
  if (lr * lr + li * li >= rr * rr + ri * ri) {
    ur = lr;
    ui = li;
  } else {
    ur = rr;
    ui = ri;
  }
  a = sqrt(ur * ur + ui * ui);
  ur /= a;
  ui /= a;
  *deg = atan2(rr * ur + ri * ui, lr * ur + li * ui) * 180 / PI;
  *lev = 10 * log10(lr * lr + li * li + rr * rr + ri * ri);
}

// what s3d_limit finds in step run i against the effect's own limit: its
// level where it holds the boost, its fall, and its rise where it isn't
// too slow to see
static void check_limit(int i) {
  struct s3d_lim l;
  double level, fall, rise;
  u8 reg[S3D_REGS];
  int bad = 0;

  CHECK_EQ(s3d_limit(&meas, i, &l), 0);
  s3d_regs(&meas, i, reg);
  s3dsim_limit(reg, &level, &fall, &rise);
  if (has(l.held))
    bad |= fabs(l.held - level) > 0.3;
  if (l.fall > 0)
    bad |= fabs(l.fall - fall) > 0.1 * fall;
  if (l.rise > 0 && l.delay >= 0)
    bad |= fabs(l.rise - rise) > 0.2 * rise;
  CHECK(!bad);
  if (bad)
    printf("  run %s: held %.1f fall %.1f rise %.1f, the effect %.1f %.1f "
           "%.1f\n",
           s3d_name(&meas, i), l.held, l.fall, l.rise, level, fall, rise);
}

static void check_report_lines(void) {
  int i;

  for (i = 0; i < nlines; i++) {
    CHECK(strlen(report[i]) < 200);
    if (strlen(report[i]) >= 200)
      printf("  too long: %s\n", report[i]);
  }
  // Notepad opens it on Windows 98 (64 KB at most)
  CHECK(report_bytes < 60000);
}

static void test_quick_plan(void) {
  struct s3d_lim l;
  int k, off, ess, model, limit, ratio, ratlim, band, bandlim, pan, panlim;
  int step, steplim, line;
  double deg, lev;

  s3d_init(&meas, S3D_PLAN_QUICK, 0, out, 0);
  CHECK_EQ(s3d_count(&meas), 25);
  CHECK(s3d_seconds(&meas) > 100 && s3d_seconds(&meas) < 180);
  run_plan(S3D_PLAN_QUICK, 0, "host test");

  off = run_named("off");
  ess = run_named("ess");
  model = run_named("model");
  limit = run_named("limit");
  ratio = run_named("ratio");
  ratlim = run_named("ratio limit");
  band = run_named("band");
  bandlim = run_named("band limit");
  pan = run_named("pan");
  panlim = run_named("pan limit");
  step = run_named("step");
  steplim = run_named("step limit");
  CHECK(off == 0 && ess == 1 && model > 0 && limit > 0 && ratio > 0);
  CHECK(ratlim > 0 && band > 0 && bandlim > 0 && pan > 0 && panlim > 0);
  CHECK(step > 0 && steplim > 0 && run_named("vec neg") > 0);
  CHECK(run_named("off again") == s3d_count(&meas) - 1);
  CHECK(run(bandlim)->base == band && run(panlim)->base == pan);
  CHECK(run(steplim)->base == step && run(limit)->slow && !run(ess)->slow);

  // the reference is 0 dB with no phase, and the imbalance of the two
  // channels puts M into S and S into M at -38 dB
  for (k = 0; k < S3D_FREQS; k++) {
    CHECK(fabs(val(off, 0, k)) < 0.01 && fabs(val(off, 1, k)) < 0.01);
    CHECK(fabs(phase(off, 0, k)) < 0.01);
  }
  check_path(off, 2, 0.3);
  check_path(off, 3, 0.3);
  // the boost's shape and phase with ESS's setting, and its level
  check_path(ess, 0, 0.1);
  check_path(ess, 1, 0.15);
  check_path(ess, 2, 0.3);
  CHECK(check_phase(ess, 0, 1.0) == S3D_FREQS);
  CHECK(check_phase(ess, 1, 2.0) == S3D_FREQS);
  CHECK(fabs(val(ess, 1, 3) - 17.9) < 0.3);
  check_path(run_named("52=20"), 1, 0.15);
  check_path(run_named("52=00"), 1, 0.15);
  // the model makes S from M and drops the input's S
  check_path(model, 1, 0.3);
  check_path(model, 2, 0.15);
  CHECK(check_phase(model, 1, 1.0) == S3D_FREQS);
  CHECK(val(model, 2, 3) > 13 && val(model, 1, 3) < -20);
  // 54h-5Ah do nothing with the limit off
  for (k = 0; k < 8; k++) {
    static const char *const names[8] = {"54=00", "54=FF", "56=00", "56=FF",
                                         "58=00", "58=FF", "5A=00", "5A=FF"};
    CHECK(run_named(names[k]) > 0);
    check_same(run_named(names[k]), ess, 0);
    check_same(run_named(names[k]), ess, 1);
  }

  // the limit: S alone gets no boost, and the more S, the less boost,
  // from where the S out passes +5.5 dB re M
  CHECK(fabs(val(limit, 1, 8)) < 0.5);
  for (k = 0; k < 5; k++)
    CHECK(fabs(run(ratio)->u.ra.ss[k] - run(ratio)->u.ra.ss[0]) < 0.2);
  CHECK(fabs(run(ratlim)->u.ra.ss[0] - run(ratio)->u.ra.ss[0]) < 0.2);
  CHECK(fabs(run(ratlim)->u.ra.ss[1] - run(ratio)->u.ra.ss[1]) < 0.2);
  CHECK(fabs(run(ratlim)->u.ra.ss[2] + -6 - 5.5) < 0.3);
  CHECK(fabs(run(ratlim)->u.ra.ss[3] - 5.5) < 0.3);
  CHECK(fabs(run(ratlim)->u.ra.ss[4]) < 0.5);
  // band runs: without the limit S>S is the sweep's
  for (k = 0; k < S3D_FREQS; k++) {
    CHECK(fabs(val(band, 1, k) - val(ess, 1, k)) < 0.2);
    CHECK(fabs(val(band, 0, k)) < 0.2);
  }
  CHECK(val(bandlim, 1, 3) < val(band, 1, 3) - 6);
  // pan runs: the boost puts the sides past the speakers
  for (k = 0; k < S3D_PANS; k++) {
    model_pan(pan, 22.5 * k, &deg, &lev);
    CHECK(fabs(run(pan)->u.pa.deg[k] - deg) < 1.0);
    CHECK(fabs(run(pan)->u.pa.lev[k] - lev) < 0.2);
    if (fabs(run(pan)->u.pa.deg[k] - deg) >= 1.0 ||
        fabs(run(pan)->u.pa.lev[k] - lev) >= 0.2)
      printf("  pan %g: %.1f degrees %.2f dB, the effect %.1f %.2f\n", 22.5 * k,
             run(pan)->u.pa.deg[k], run(pan)->u.pa.lev[k], deg, lev);
  }
  CHECK(run(pan)->u.pa.deg[0] < -20 && run(pan)->u.pa.deg[4] > 110);
  CHECK(run(panlim)->u.pa.deg[0] > run(pan)->u.pa.deg[0] + 5);
  // step runs: the boost stays with the limit off, and with it on the
  // limit holds it at +5.5 dB re M, falls at 53 dB a second and rises at
  // a 16th of that
  CHECK_EQ(s3d_limit(&meas, step, &l), 0);
  CHECK(l.fall == 0 && fabs(l.high) < 0.2 && !has(l.held));
  check_limit(steplim);
  CHECK_EQ(s3d_limit(&meas, steplim, &l), 0);
  CHECK(l.fall > 45 && l.rise > 2.5 && l.rise < 4.5 && l.delay > 0);
  CHECK_EQ(s3d_limit(&meas, ess, &l), -1);

  // the report
  CHECK(find_line("ess3d measure: ") == 0);
  CHECK(find_line("host test") == 1);
  CHECK(find_line("plan: quick, 25 runs, about 2 min") == 2);
  CHECK(find_line("  0  off            04 3F 8F 95 94 80  M>M") > 0);
  line = find_line("  1  ess            0C 3F 8F 95 94 80  M>M");
  CHECK(line > 0 && strstr(report[line + 2], "S>S deg"));
  CHECK(find_line("ratio runs") > 0 && find_line("band runs") > 0);
  CHECK(find_line("pan runs") > 0 && find_line("step runs") > 0);
  line = find_line("summary");
  CHECK(line > 0);
  // the imbalance that the boost raises to -20 dB stays out of the summary
  CHECK(find_from(line, "  1  ess            0C 3F 8F 95 94 80     0  S>S   "
                        "+17.9  400, S>S phase") > 0);
  CHECK(!strstr(report[find_from(line, "  1  ess")], "M>S"));
  CHECK(strstr(report[find_from(line, "  4  model")], "M>S appears"));
  CHECK(strstr(report[find_from(line, " 24  off again")], "no change"));
  CHECK(find_line("ratio limit: from S/M -24 to +6 dB, S>S at 1 kHz changes "
                  "by -16.1 dB; it holds S out at +5.5 to +5.5 dB re M") > 0);
  CHECK(find_line("pan: 0, 22.5, 45, 67.5 and 90 degrees come out at -") > 0);
  CHECK(find_line("limit: the boost's gain in the step runs") > 0);
  CHECK(find_line(" 22  step limit     0D 3F 8F 95 94 80      54 -15.4  +0.0 "
                  "   +5.5") > 0);
  check_report_lines();
}

static void test_full_plan(void) {
  int i, line;

  s3d_init(&meas, S3D_PLAN_FULL, 0, out, 0);
  CHECK_EQ(s3d_count(&meas), 53);
  CHECK(s3d_seconds(&meas) > 280 && s3d_seconds(&meas) < 400);
  CHECK(run_named("M 56=FF") > 0 && run_named("vec swap") > 0);
  CHECK(run_named("band L 58=00") > 0 && run_named("L 52=20") > 0);
  run_plan(S3D_PLAN_FULL, 0, 0);
  // the model's registers: none of them changes the model
  line = find_line("model: with 50h bit 1 set");
  CHECK(line > 0 && strstr(report[line], "by 0.0 dB and 0 degrees at most"));
  // each 52h level
  for (i = 0; i < 8; i++) {
    char name[8];

    sprintf(name, "52=%02X", i * 8);
    check_path(run_named(name), 1, 0.15);
  }
  // the limit in each step run
  for (i = 0; i < s3d_count(&meas); i++)
    if (run(i)->kind == S3D_STEP && (run(i)->reg[0] & 1))
      check_limit(i);
  CHECK(find_line("fall: from 17 dB/s (run 43, L 58=FF) to 261 dB/s (run 42, "
                  "L 58=00)") > 0);
  CHECK(find_line("held: S out from -5.6 dB re M (run 38, L 54=00) to +9.8 dB "
                  "(run 39, L 54=FF)") > 0);
  check_report_lines();
  if (verbose)
    printf("full report: %d lines, %ld bytes\n", nlines, report_bytes);
}

static void test_limit_plan(void) {
  int i, n = 0;

  s3d_init(&meas, S3D_PLAN_LIMIT, 0, out, 0);
  CHECK_EQ(s3d_count(&meas), 54);
  CHECK(s3d_seconds(&meas) > 360 && s3d_seconds(&meas) < 480);
  CHECK(run_named("L 54=0F b7") > 0 && run_named("L 5A=81 b0") > 0);
  CHECK(run_named("52=20") < run_named("L 52=20"));
  run_plan(S3D_PLAN_LIMIT, 0, 0);
  // every bit of 54h-5Ah, against the effect's limit
  for (i = 0; i < s3d_count(&meas); i++)
    if (run(i)->kind == S3D_STEP && (run(i)->reg[0] & 1)) {
      check_limit(i);
      n++;
    }
  CHECK_EQ(n, 47);
  CHECK(find_line("plan: limit, 54 runs") == 1);
  check_report_lines();
  if (verbose)
    printf("limit report: %d lines, %ld bytes\n", nlines, report_bytes);
}

static void test_one_register(void) {
  int i;

  nlines = 0;
  s3d_init(&meas, S3D_PLAN_REG, 0x58, out, 0);
  CHECK_EQ(s3d_count(&meas), 27);
  CHECK(run_named("58=00") == 2 && run_named("58=FF") == 3);
  CHECK(run_named("M 58=FF") == 6 && run_named("step limit") == 8);
  CHECK(run_named("L 58=00") == 9 && run_named("L 58=FF") == 25);
  run_plan(S3D_PLAN_REG, 0x58, 0);
  for (i = 9; i <= 25; i++)
    check_limit(i);
  check_report_lines();
  // a register that isn't one of 54h-5Ah gives the quick plan
  s3d_init(&meas, S3D_PLAN_REG, 0x50, out, 0);
  CHECK_EQ(s3d_count(&meas), 25);
}

// a recording at 44,194 Hz of a stimulus played at 48 kHz: the stimulus,
// sampled where the recorder's clock puts it
static void test_other_rate(void) {
  u32 rec_frames, r, rate_rec = 44194;
  double pos, l, rr;
  int full = 0, res, k;
  u16 n;

  nlines = 0;
  s3d_init(&meas, S3D_PLAN_QUICK, 0, out, 0);
  s3d_header(&meas, S3D_RATE, rate_rec, 0);
  s3d_begin(&meas, 0, &rec_frames);
  // the recording starts 70 ms before the playback
  for (r = 0; !full && r < rec_frames; r += n) {
    for (n = 0; n < BLOCK; n++) {
      pos = ((double)(r + n) / rate_rec - 0.070) * S3D_RATE;
      s3d_signal(&meas, pos, &l, &rr);
      rec[2 * n] = (s16)floor(0.4 * l * 32767 + 0.5);
      rec[2 * n + 1] = (s16)floor(0.4 * rr * 32767 + 0.5);
    }
    full = s3d_take(&meas, rec, n);
  }
  res = s3d_end(&meas);
  CHECK_EQ(res, S3D_OK);
  for (k = 0; k < S3D_FREQS; k++) {
    CHECK(fabs(meas.ref_mm[k] - 0.4) < 0.004);
    CHECK(fabs(meas.ref_ss[k] - 0.4) < 0.004);
    // the phase of S against M is there, and the same, at another rate
    CHECK(fabs(meas.ref_ph[k]) < 2);
  }
}

// the playback's start in the first 50 ms of the recording, where the
// noise floor comes from: a click and a step that settles mustn't hide
// the burst
static void test_click(void) {
  int i;

  nlines = 0;
  click = 1;
  s3d_init(&meas, S3D_PLAN_QUICK, 0, out, 0);
  s3dsim_reset(&sim, S3D_RATE);
  s3d_header(&meas, S3D_RATE, S3D_RATE, 0);
  for (i = 0; i < 2; i++)
    CHECK_EQ(run_sim(i, 1500), S3D_OK);
  click = 0;
  check_path(1, 1, 0.15);
  check_path(0, 2, 0.3);
}

static void test_no_signal(void) {
  u32 rec_frames, r;
  int full = 0;

  nlines = 0;
  s3d_init(&meas, S3D_PLAN_QUICK, 0, out, 0);
  s3d_header(&meas, S3D_RATE, S3D_RATE, 0);
  s3d_begin(&meas, 0, &rec_frames);
  memset(rec, 0, sizeof(rec));
  for (r = 0; !full && r < rec_frames; r += BLOCK)
    full = s3d_take(&meas, rec, BLOCK);
  CHECK_EQ(s3d_end(&meas), S3D_NOSIGNAL);
  s3d_failed(&meas, "no signal");
  CHECK(strstr(report[nlines - 1], "failed: no signal (noise under -90 "
                                   "dBFS, loudest under -90 dBFS)") != 0);

  // a click alone (a DAC that starts) is no onset either
  s3d_begin(&meas, 0, &rec_frames);
  memset(rec, 0, sizeof(rec));
  rec[2 * 3000] = 20000;
  rec[2 * 3001] = -20000;
  full = 0;
  for (r = 0; !full && r < rec_frames; r += BLOCK) {
    full = s3d_take(&meas, rec, BLOCK);
    memset(rec, 0, sizeof(rec));
  }
  CHECK_EQ(s3d_end(&meas), S3D_NOSIGNAL);
}

int main(int argc, char **argv) {
  verbose = argc > 1 && !strcmp(argv[1], "-v");
  test_quick_plan();
  test_full_plan();
  test_limit_plan();
  test_one_register();
  test_other_rate();
  test_click();
  test_no_signal();
  return CHECK_DONE("t_s3dmeas");
}
