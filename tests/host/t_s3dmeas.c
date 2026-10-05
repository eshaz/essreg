/*
 * t_s3dmeas runs ess3d's measurement of the 3-D effect (src/s3dmeas.c)
 * against the made-up effect of src/s3dsim.c, and checks that the report
 * finds each part of it again: the boost's shape, the width the model
 * makes from mono, the register that does nothing and the limit. It also
 * checks the measurement with devices that start apart, a recording at
 * another rate, and a recording with no signal.
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

static struct s3d_meas meas;
static struct s3dsim sim;
static s16 play[2 * BLOCK], rec[2 * BLOCK];
static char report[400][240];
static int nlines;
static int verbose;

static void out(void *ctx, const char *line) {
  (void)ctx;
  if (verbose)
    printf("%s\n", line);
  if (nlines < 400) {
    strncpy(report[nlines], line, sizeof(report[0]) - 1);
    nlines++;
  }
}

static int find_line(const char *start) {
  int i;

  for (i = 0; i < nlines; i++)
    if (!strncmp(report[i], start, strlen(start)))
      return i;
  return -1;
}

// run i through the model: lead frames of silence come first, as a
// recording that starts before the playback
static int run_sim(int i, u32 lead) {
  u32 frames, rec_frames, done = 0, fed = 0;
  u8 reg[S3D_REGS];
  u16 n;
  int full = 0;

  s3d_regs(&meas, i, reg);
  s3dsim_regs(&sim, reg);
  frames = s3d_begin(&meas, i, &rec_frames);
  memset(rec, 0, sizeof(rec));
  while (lead && !full) {
    n = lead > BLOCK ? BLOCK : (u16)lead;
    full = s3d_take(&meas, rec, n);
    lead -= n;
    fed += n;
  }
  while (!full && fed < rec_frames) {
    n = BLOCK;
    s3d_fill(&meas, play, n);
    s3dsim_run(&sim, play, rec, n);
    full = s3d_take(&meas, rec, n);
    done += n;
    fed += n;
  }
  (void)frames;
  return s3d_end(&meas);
}

// the run's value in dB, path p (0 to 3) at frequency index k
static double val(int i, int p, int k) { return meas.run[i].db[p][k]; }

static double model_db(int i, int p, double hz) {
  double g[4];
  u8 reg[S3D_REGS];

  s3d_regs(&meas, i, reg);
  s3dsim_response(reg, S3D_RATE, hz, g);
  return g[p] > 0 ? 20 * log10(g[p]) : S3D_FLOOR;
}

static int run_named(const char *name) {
  int i;

  for (i = 0; i < s3d_count(&meas); i++)
    if (!strcmp(s3d_name(&meas, i), name))
      return i;
  return -1;
}

static const double hz[S3D_FREQS] = {100,  160,  250,  400,  630,  1000,
                                     1600, 2500, 4000, 6300, 10000};

// every frequency of path p of run i matches the model within tol dB
static void check_path(int i, int p, double tol) {
  int k;
  double want, got;

  for (k = 0; k < S3D_FREQS; k++) {
    want = model_db(i, p, hz[k]);
    got = val(i, p, k);
    if (want <= S3D_FLOOR + 1) {
      CHECK(got <= S3D_FLOOR + 1);
      continue;
    }
    CHECK(fabs(want - got) < tol);
    if (fabs(want - got) >= tol)
      printf("  run %s path %d at %g Hz: %.2f dB, the model %.2f dB\n",
             s3d_name(&meas, i), p, hz[k], got, want);
  }
}

static void test_quick_plan(void) {
  int i, n, res, off, ess, model, limit, r58, r54, ratio, ratlim, steplim;

  nlines = 0;
  s3d_init(&meas, S3D_PLAN_QUICK, 0, out, 0);
  s3dsim_reset(&sim, S3D_RATE);
  n = s3d_count(&meas);
  CHECK(n == 19);
  CHECK(s3d_seconds(&meas) > 60 && s3d_seconds(&meas) < 180);
  s3d_header(&meas, S3D_RATE, S3D_RATE, "host test");
  for (i = 0; i < n; i++) {
    res = run_sim(i, 3000 + 111 * i);
    CHECK_EQ(res, S3D_OK);
  }
  s3d_summary(&meas);

  off = run_named("off");
  ess = run_named("ess");
  model = run_named("model");
  limit = run_named("limit");
  r58 = run_named("58=00");
  r54 = run_named("54=FF");
  ratio = run_named("ratio");
  ratlim = run_named("ratio limit");
  steplim = run_named("step limit");
  CHECK(off == 0 && ess == 1 && model > 0 && limit > 0 && r58 > 0);
  CHECK(r54 > 0 && ratio > 0 && ratlim > 0 && steplim > 0);

  // the reference is 0 dB, and its cross terms are under the floor
  for (i = 0; i < S3D_FREQS; i++) {
    CHECK(fabs(val(off, 0, i)) < 0.01 && fabs(val(off, 1, i)) < 0.01);
    CHECK(val(off, 2, i) <= S3D_FLOOR + 1);
    CHECK(val(off, 3, i) <= S3D_FLOOR + 1);
  }
  // the boost's shape, ESS's setting and the high-pass moved by 54h
  check_path(ess, 0, 0.1);
  check_path(ess, 1, 0.15);
  check_path(r54, 1, 0.15);
  CHECK(val(ess, 1, 7) > 8 && val(ess, 1, 0) < 0);
  CHECK(val(r54, 1, 2) < val(ess, 1, 2) - 3);
  // the model's width from mono
  check_path(model, 2, 0.15);
  // 58h does nothing
  for (i = 0; i < S3D_FREQS; i++)
    CHECK(fabs(val(r58, 1, i) - val(ess, 1, i)) < 0.1);
  // 5Ah bit 0 raises M by 1 dB: 5A=FF has it, 5A=00 doesn't
  CHECK(fabs(val(run_named("5A=FF"), 0, 5) - 1.0) < 0.1);
  CHECK(fabs(val(run_named("5A=00"), 0, 5)) < 0.1);
  // the limit: S alone gets no boost, and the more S, the less boost
  CHECK(fabs(val(limit, 1, 8)) < 0.5);
  CHECK(fabs(meas.run[ratio].extra[0] - meas.run[ratio].extra[4]) < 0.2);
  CHECK(meas.run[ratlim].extra[4] < meas.run[ratlim].extra[0] - 3);
  // the step with the limit: it takes time to settle after the step up
  CHECK(meas.run[steplim].extra[0] > meas.run[steplim].db[0][1] + 1);

  // the report
  CHECK(find_line("ess3d measure: ") == 0);
  CHECK(find_line("host test") == 1);
  CHECK(find_line("  0  off            04 3F 8F 95 94 80  M>M") > 0);
  CHECK(find_line("  1  ess            0C 3F 8F 95 94 80  M>M") > 0);
  CHECK(find_line("ratio runs") > 0);
  CHECK(find_line("step runs") > 0);
  CHECK(find_line("summary") > 0);
  CHECK(find_line("ratio limit: from S/M -24 to +6 dB") > 0);
  CHECK(find_line("step limit: S>S is within 1 dB") > 0);
  for (i = 0; i < nlines; i++)
    CHECK(strlen(report[i]) < 200);
}

static void test_full_plan(void) {
  int i, n, model_line;

  nlines = 0;
  s3d_init(&meas, S3D_PLAN_FULL, 0, out, 0);
  s3dsim_reset(&sim, S3D_RATE);
  n = s3d_count(&meas);
  CHECK(n == 69);
  CHECK(n <= S3D_MAX_RUNS);
  CHECK(!strcmp(s3d_name(&meas, run_named("54=0F b7")), "54=0F b7"));
  CHECK(run_named("M 56=15 b7") > 0);
  s3d_header(&meas, S3D_RATE, S3D_RATE, 0);
  for (i = 0; i < n; i++)
    CHECK_EQ(run_sim(i, 2000), S3D_OK);
  s3d_summary(&meas);
  // the model's registers: 54h and 56h shape the boost, not the model,
  // but the boost is in the output with the model on too
  model_line = find_line("model: with 50h bit 1 set");
  CHECK(model_line > 0);
  CHECK(model_line > 0 && strstr(report[model_line], "by 0.0 dB at most"));
  check_path(run_named("54=0F b7"), 1, 0.15);
  check_path(run_named("56=85 b4"), 1, 0.15);
}

static void test_one_register(void) {
  int n;

  nlines = 0;
  s3d_init(&meas, S3D_PLAN_REG, 0x56, out, 0);
  n = s3d_count(&meas);
  CHECK(n == 22);
  CHECK(run_named("56=00") == 2 && run_named("56=FF") == 18);
  CHECK(run_named("M 56=FF") == 21);
  // a register that isn't one of 54h-5Ah gives the quick plan
  s3d_init(&meas, S3D_PLAN_REG, 0x50, out, 0);
  CHECK(s3d_count(&meas) == 19);
}

// a recording at 44,194 Hz of a stimulus played at 48 kHz: the model's
// output, sampled where the recorder's clock puts it
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
  CHECK(meas.ref_mm[5] > 0.39 && meas.ref_mm[5] < 0.41);
  for (k = 0; k < S3D_FREQS; k++) {
    CHECK(fabs(meas.ref_mm[k] - 0.4) < 0.004);
    CHECK(fabs(meas.ref_ss[k] - 0.4) < 0.004);
  }
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
  CHECK(strstr(report[nlines - 1], "failed: no signal") != 0);

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
  test_one_register();
  test_other_rate();
  test_no_signal();
  return CHECK_DONE("t_s3dmeas");
}
