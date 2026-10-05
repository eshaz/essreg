/*
 * t_s3dmeas runs ess3d's measurement of the 3-D effect (src/s3dmeas.c)
 * against the made-up effect of src/s3dsim.c, and checks that the report
 * finds each part of it again: the boost's shape and phase, its sign bit,
 * the width the model makes from mono, the register that does nothing in
 * the sweeps, where panned tones come out, and the limit, with the
 * release that 58h sets. It also checks the measurement with devices that
 * start apart, a recording at another rate, and a recording with no
 * signal.
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

// run i through the made-up effect: lead frames of silence come first, as
// a recording that starts before the playback
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
    full = s3d_take(&meas, rec, n);
    lead -= n;
    fed += n;
  }
  while (!full && fed < rec_frames) {
    n = BLOCK;
    s3d_fill(&meas, play, n);
    s3dsim_run(&sim, play, rec, n);
    full = s3d_take(&meas, rec, n);
    fed += n;
  }
  return s3d_end(&meas);
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

// what the made-up effect does with run i's registers at hz
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

// every frequency of path p of run i matches the made-up effect within
// tol dB
static void check_path(int i, int p, double tol) {
  int k;
  double want, got;

  for (k = 0; k < S3D_FREQS; k++) {
    want = model_db(i, p, hz[k]);
    got = val(i, p, k);
    if (want <= S3D_FLOOR + 1) {
      CHECK(!has(got));
      continue;
    }
    CHECK(fabs(want - got) < tol);
    if (fabs(want - got) >= tol)
      printf("  run %s path %d at %g Hz: %.2f dB, the model %.2f dB\n",
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
      printf("  run %s phase %d at %g Hz: %.2f degrees, the model %.2f\n",
             s3d_name(&meas, i), q, hz[k], got, want);
  }
  return n;
}

// where the made-up effect puts 1 kHz panned to th degrees, and its level
// in dB, as the report works them out
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

// the first step window after which S>S stays within 1 dB of v, in ms, or
// -1
static int settle_ms(const float *traj, double v) {
  int k;

  for (k = S3D_TRAJ; k > 0; k--)
    if (!has(traj[k - 1]) || fabs(traj[k - 1] - v) > 1.0)
      break;
  if (k >= S3D_TRAJ)
    return -1;
  return k < S3D_FINE ? 20 * k : 500 + 100 * (k - S3D_FINE);
}

static void check_report_lines(void) {
  int i;

  for (i = 0; i < nlines; i++) {
    CHECK(strlen(report[i]) < 200);
    if (strlen(report[i]) >= 200)
      printf("  too long: %s\n", report[i]);
  }
}

static void test_quick_plan(void) {
  int i, k, n, res, off, ess, model, limit, r58, r54, r54z, ratio, ratlim;
  int band, bandlim, pan, panlim, step, steplim, vneg, line;
  double deg, lev;

  nlines = 0;
  s3d_init(&meas, S3D_PLAN_QUICK, 0, out, 0);
  s3dsim_reset(&sim, S3D_RATE);
  n = s3d_count(&meas);
  CHECK_EQ(n, 25);
  CHECK(s3d_seconds(&meas) > 100 && s3d_seconds(&meas) < 180);
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
  r54z = run_named("54=00");
  ratio = run_named("ratio");
  ratlim = run_named("ratio limit");
  band = run_named("band");
  bandlim = run_named("band limit");
  pan = run_named("pan");
  panlim = run_named("pan limit");
  step = run_named("step");
  steplim = run_named("step limit");
  vneg = run_named("vec neg");
  CHECK(off == 0 && ess == 1 && model > 0 && limit > 0 && r58 > 0);
  CHECK(r54 > 0 && r54z > 0 && ratio > 0 && ratlim > 0 && band > 0);
  CHECK(bandlim > 0 && pan > 0 && panlim > 0 && step > 0 && steplim > 0);
  CHECK(vneg > 0 && run_named("vec /2") > 0);
  CHECK(run(bandlim)->base == band && run(panlim)->base == pan);
  CHECK(run(steplim)->base == step && run(limit)->slow && !run(ess)->slow);

  // the reference is 0 dB with no phase, and its cross terms are under
  // the floor
  for (k = 0; k < S3D_FREQS; k++) {
    CHECK(fabs(val(off, 0, k)) < 0.01 && fabs(val(off, 1, k)) < 0.01);
    CHECK(!has(val(off, 2, k)) && !has(val(off, 3, k)));
    CHECK(fabs(phase(off, 0, k)) < 0.01);
    CHECK(!has(phase(off, 1, k)) && !has(phase(off, 2, k)));
  }
  // the boost's shape and phase, ESS's setting and the high-pass moved
  // by 54h
  check_path(ess, 0, 0.1);
  check_path(ess, 1, 0.15);
  check_path(r54, 1, 0.15);
  CHECK(check_phase(ess, 0, 1.0) == S3D_FREQS);
  CHECK(check_phase(r54, 0, 1.0) == S3D_FREQS);
  CHECK(val(ess, 1, 7) > 8 && val(ess, 1, 0) < 0);
  CHECK(val(r54, 1, 2) < val(ess, 1, 2) - 3);
  // 54h bit 7 clear takes the boost away: S comes out turned around
  // where the boost is bigger than the direct path
  check_path(r54z, 1, 0.15);
  CHECK(check_phase(r54z, 0, 1.0) == S3D_FREQS);
  CHECK(fabs(phase(r54z, 0, 5)) > 150);
  CHECK(fabs(phase(ess, 0, 5)) < 30);
  // the model's width from mono, and its phase against the M
  check_path(model, 2, 0.15);
  CHECK(check_phase(model, 1, 1.0) == S3D_FREQS);
  CHECK(phase(model, 1, 5) < -80 && phase(model, 1, 5) > -100);
  // 58h does nothing in the sweeps
  for (k = 0; k < S3D_FREQS; k++)
    CHECK(fabs(val(r58, 1, k) - val(ess, 1, k)) < 0.1);
  // 5Ah bit 0 raises M by 1 dB: 5A=FF has it, 5A=00 doesn't
  CHECK(fabs(val(run_named("5A=FF"), 0, 5) - 1.0) < 0.1);
  CHECK(fabs(val(run_named("5A=00"), 0, 5)) < 0.1);
  // the sign bits of 56h and 58h do nothing here, so the vector's
  // negative is 54h's alone
  check_path(vneg, 1, 0.15);
  CHECK(check_phase(vneg, 0, 1.0) == S3D_FREQS);

  // the limit: S alone gets no boost, and the more S, the less boost
  CHECK(fabs(val(limit, 1, 8)) < 0.5);
  CHECK(fabs(run(ratio)->u.ra.ss[0] - run(ratio)->u.ra.ss[4]) < 0.2);
  CHECK(run(ratlim)->u.ra.ss[4] < run(ratlim)->u.ra.ss[0] - 3);
  for (k = 0; k < 5; k++)
    CHECK(fabs(run(ratio)->u.ra.mm[k]) < 0.1);
  // band runs: without the limit S>S is the sweep's, with it the boost is
  // held down where it's big
  for (k = 0; k < S3D_FREQS; k++) {
    CHECK(fabs(val(band, 1, k) - val(ess, 1, k)) < 0.2);
    CHECK(fabs(val(band, 0, k)) < 0.1);
  }
  CHECK(val(bandlim, 1, 8) < val(band, 1, 8) - 3);
  CHECK(fabs(val(bandlim, 1, 0) - val(band, 1, 0)) < 0.5);
  // pan runs: the boost puts the sides past the speakers, the middle
  // stays
  for (k = 0; k < S3D_PANS; k++) {
    model_pan(pan, 22.5 * k, &deg, &lev);
    CHECK(fabs(run(pan)->u.pa.deg[k] - deg) < 1.0);
    CHECK(fabs(run(pan)->u.pa.lev[k] - lev) < 0.2);
    if (fabs(run(pan)->u.pa.deg[k] - deg) >= 1.0 ||
        fabs(run(pan)->u.pa.lev[k] - lev) >= 0.2)
      printf("  pan %g: %.1f degrees %.2f dB, the model %.1f %.2f\n", 22.5 * k,
             run(pan)->u.pa.deg[k], run(pan)->u.pa.lev[k], deg, lev);
  }
  CHECK(run(pan)->u.pa.deg[0] < -10 && run(pan)->u.pa.deg[4] > 100);
  CHECK(fabs(run(pan)->u.pa.deg[2] - 45) < 0.5);
  CHECK(fabs(run(panlim)->u.pa.deg[2] - 45) < 0.5);
  CHECK(run(panlim)->u.pa.deg[0] > run(pan)->u.pa.deg[0] + 5);
  // step runs: without the limit S>S doesn't move; with it, the first
  // window after the step up has the boost before the attack holds it,
  // and the boost comes back with the release after the step down
  for (k = 0; k < S3D_TRAJ; k++) {
    CHECK(fabs(run(step)->u.st.traj[0][k] - run(step)->u.st.steady[1]) < 0.3);
    CHECK(fabs(run(step)->u.st.traj[1][k] - run(step)->u.st.steady[2]) < 0.3);
  }
  CHECK(fabs(run(step)->u.st.steady[0] - val(ess, 1, 5)) < 0.2);
  CHECK(run(steplim)->u.st.traj[0][0] > run(steplim)->u.st.steady[1] + 1);
  CHECK(run(steplim)->u.st.steady[1] < run(steplim)->u.st.steady[0] - 3);
  CHECK(run(steplim)->u.st.traj[1][0] < run(steplim)->u.st.steady[0] - 3);
  CHECK(fabs(run(steplim)->u.st.steady[2] - run(steplim)->u.st.steady[0]) <
        0.3);
  k = settle_ms(run(steplim)->u.st.traj[1], run(steplim)->u.st.steady[0]);
  CHECK(k >= 300 && k <= 900);

  // the report
  CHECK(find_line("ess3d measure: ") == 0);
  CHECK(find_line("host test") == 1);
  CHECK(find_line("plan: quick, 25 runs, about 2 min") == 2);
  CHECK(find_line("  0  off            04 3F 8F 95 94 80  M>M") > 0);
  line = find_line("  1  ess            0C 3F 8F 95 94 80  M>M");
  CHECK(line > 0 && strstr(report[line + 2], "S>S deg"));
  line = find_line("  0  off");
  CHECK(line > 0 && !strstr(report[line + 2], "S>S deg"));
  CHECK(find_line("ratio runs") > 0);
  CHECK(find_line("band runs") > 0);
  CHECK(find_line("pan runs") > 0);
  CHECK(find_line("step runs") > 0);
  CHECK(find_line("summary") > 0);
  line = find_from(find_line("summary"), "  7  54=00 ");
  CHECK(line > 0 && strstr(report[line], ", S>S phase "));
  CHECK(find_line("ratio limit: from S/M -24 to +6 dB") > 0);
  CHECK(find_line("pan: 0, 22.5, 45, 67.5 and 90 degrees come out at -") > 0);
  CHECK(find_line("step: within 1 dB 0 ms after the step up and 0 ms after "
                  "the step down") > 0);
  CHECK(find_line("step limit: within 1 dB ") > 0);
  line = find_line("vector: clearing bit 7 of 54h-58h together");
  CHECK(line > 0);
  CHECK(find_line("        and differs from") < 0);
  check_report_lines();
}

static void test_full_plan(void) {
  int i, n, line, fast, slow, ess;

  nlines = 0;
  report_bytes = 0;
  s3d_init(&meas, S3D_PLAN_FULL, 0, out, 0);
  s3dsim_reset(&sim, S3D_RATE);
  n = s3d_count(&meas);
  CHECK_EQ(n, 95);
  CHECK(n <= S3D_MAX_RUNS);
  CHECK(s3d_seconds(&meas) > 420 && s3d_seconds(&meas) < 600);
  CHECK(!strcmp(s3d_name(&meas, run_named("54=0F b7")), "54=0F b7"));
  CHECK(run_named("M 56=15 b7") > 0 && run_named("vec swap") > 0);
  CHECK(run_named("band L 5A=FF") > 0 && run_named("step L 52=20") > 0);
  s3d_header(&meas, S3D_RATE, S3D_RATE, 0);
  for (i = 0; i < n; i++)
    CHECK_EQ(run_sim(i, 2000 + 37 * i), S3D_OK);
  s3d_summary(&meas);
  ess = run_named("ess");
  // the model's registers: 54h and 56h shape the boost, not the model
  line = find_line("model: with 50h bit 1 set");
  CHECK(line > 0 && strstr(report[line], "by 0.0 dB and 0 degrees at most"));
  check_path(run_named("54=0F b7"), 1, 0.15);
  check_path(run_named("56=85 b4"), 1, 0.15);
  CHECK(check_phase(run_named("56=85 b4"), 0, 1.0) == S3D_FREQS);
  CHECK(check_phase(run_named("vec rot"), 0, 1.0) == S3D_FREQS);
  // the sign bits act apart: clearing all three is 54h's alone
  line = find_line("vector: clearing bit 7 of 54h-58h together");
  CHECK(line > 0 && strstr(report[line + 1], "up to +0.0 dB and +0 degrees"));
  CHECK(find_line("        vec x2: up to ") > 0);
  // 58h sets the limit's release: fast at 00h, too slow for the run at
  // FFh
  fast = settle_ms(run(run_named("step L 58=00"))->u.st.traj[1],
                   run(run_named("step L 58=00"))->u.st.steady[0]);
  slow = settle_ms(run(run_named("step L 58=FF"))->u.st.traj[1],
                   run(run_named("step L 58=FF"))->u.st.steady[0]);
  CHECK(fast >= 0 && fast <= 100);
  CHECK(slow < 0);
  CHECK(find_line("step L 58=FF: within 1 dB ") > 0);
  line = find_line("window: with the limit on, the settling after the step");
  CHECK(line > 0 && strstr(report[line], "to over 1700 ms (run 91, step L "
                                         "58=FF)"));
  CHECK(line > 0 && strstr(report[line], "(run 90, step L 58=00) to"));
  // the bits that do nothing here don't move it
  CHECK(settle_ms(run(run_named("step L 5A=00"))->u.st.traj[1],
                  run(run_named("step L 5A=00"))->u.st.steady[0]) ==
        settle_ms(run(run_named("step limit"))->u.st.traj[1],
                  run(run_named("step limit"))->u.st.steady[0]));
  CHECK(settle_ms(run(run_named("step L 56=FF"))->u.st.traj[1],
                  run(run_named("step L 56=FF"))->u.st.steady[0]) ==
        settle_ms(run(run_named("step limit"))->u.st.traj[1],
                  run(run_named("step limit"))->u.st.steady[0]));
  CHECK(ess == 1);
  check_report_lines();
  // Notepad opens it on Windows 98 (64 KB at most)
  CHECK(report_bytes < 60000);
  if (verbose)
    printf("report: %d lines, %ld bytes\n", nlines, report_bytes);
}

static void test_one_register(void) {
  int n;

  nlines = 0;
  s3d_init(&meas, S3D_PLAN_REG, 0x56, out, 0);
  n = s3d_count(&meas);
  CHECK_EQ(n, 28);
  CHECK(run_named("56=00") == 2 && run_named("56=FF") == 18);
  CHECK(run_named("M 56=FF") == 21);
  CHECK(run_named("band L 56=FF") == 24 && run_named("step L 56=FF") == 27);
  // a register that isn't one of 54h-5Ah gives the quick plan
  s3d_init(&meas, S3D_PLAN_REG, 0x50, out, 0);
  CHECK_EQ(s3d_count(&meas), 25);
}

// a recording at 44,194 Hz of a stimulus played at 48 kHz: the made-up
// effect's output, sampled where the recorder's clock puts it
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
