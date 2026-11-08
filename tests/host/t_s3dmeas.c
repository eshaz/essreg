/*
 * t_s3dmeas runs ess3d's measurement of the 3-D effect (src/s3dmeas.c)
 * against the effect of src/s3dsim.c, made after one card's measurement,
 * and checks that the report finds each part of it again: the boost's
 * shape and phase, its level in 0.75 dB steps, the imbalance of the
 * channels, the model's width made from mono, the registers that do
 * nothing with the limit off, where panned tones come out, and the limit:
 * the levels where it holds the boost and where it lets it rise, which
 * 54h, 56h and 5Ah set, and how fast it falls and rises, which 58h sets.
 * It also checks the measurement with devices that start apart, a
 * recording at another rate, a recording or a playback that skips, and a
 * recording with no signal.
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
#define MAX_LINES 1500
#define PI 3.14159265358979

static struct s3d_meas meas;
static struct s3d_chirp chirps[S3D_MAX_CHIRPS];
static struct s3dsim sim;
static s16 play[2 * BLOCK], rec[2 * BLOCK];
static char report[MAX_LINES][240];
static int nlines;
static long report_bytes;
static int verbose;
static int click;    // a DAC's click and its settling in the first 50 ms
static u32 skip_at;  // a recorded frame where the recording skips, or 0
static u32 stall_at; // a played frame where the playback stalls, or 0
static int allpass;  // the M recorded through an all-pass at 300 Hz
static double ap_x, ap_y;

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

// a plan, with room for every chirp run's results
static void init_meas(int plan, u8 reg, s3d_out_fn fn, void *ctx) {
  s3d_init(&meas, plan, reg, fn, ctx);
  CHECK(s3d_chirps(&meas) <= S3D_MAX_CHIRPS);
  s3d_chirp_mem(&meas, chirps, S3D_MAX_CHIRPS);
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

// the M of n recorded frames through a first-order all-pass at 300 Hz,
// which keeps its level but delays it by 1 ms at the lowest frequencies
// and hardly at all over 5 kHz, as an effect whose M has a delay of its
// own would
static void allpass_m(s16 *pcm, u16 n) {
  double a = (tan(PI * 300 / S3D_RATE) - 1) / (tan(PI * 300 / S3D_RATE) + 1);
  double m, s, y;
  u16 k;

  for (k = 0; k < n; k++) {
    m = (pcm[2 * k] + pcm[2 * k + 1]) / 2.0;
    s = (pcm[2 * k] - pcm[2 * k + 1]) / 2.0;
    y = a * m + ap_x - a * ap_y;
    ap_x = m;
    ap_y = y;
    pcm[2 * k] = (s16)floor(y + s + 0.5);
    pcm[2 * k + 1] = (s16)floor(y - s + 0.5);
  }
}

// run i through the effect: lead frames of silence come first, as a
// recording that starts before the playback
static int run_sim(int i, u32 lead) {
  u32 rec_frames, fed = 0, played = 0;
  u8 reg[S3D_REGS];
  u16 n;
  int full = 0, stalled = 0;

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
    if (stall_at && played == stall_at && !stalled) {
      // the playback stalls: 4000 frames, 83 ms, of silence, and the
      // rest comes that much later
      memset(play, 0, sizeof(play));
      n = 4000;
      stalled = 1;
    } else {
      n = BLOCK;
      if (stall_at > played && stall_at - played < n)
        n = (u16)(stall_at - played);
      s3d_fill(&meas, play, n);
      played += n;
    }
    s3dsim_run(&sim, play, rec, n);
    if (allpass)
      allpass_m(rec, n);
    add_click(rec, n, fed);
    if (skip_at && fed <= skip_at && skip_at < fed + n) {
      // the recording loses 1000 frames, 21 ms
      full = s3d_take(&meas, rec, (u16)(skip_at - fed));
      if (!full && n > skip_at - fed + 1000)
        full = s3d_take(&meas, rec + 2 * (skip_at - fed + 1000),
                        (u16)(n - (skip_at - fed + 1000)));
    } else {
      full = s3d_take(&meas, rec, n);
    }
    fed += n;
  }
  // a steady tone has the same amplitude in both halves of its window
  if (!skip_at && !stall_at)
    CHECK_EQ(s3d_disturbed(&meas), 0);
  return s3d_end(&meas);
}

// the plan through the effect, every run good, then the summary
static void run_plan(int plan, u8 reg, const char *info) {
  int i;

  nlines = 0;
  report_bytes = 0;
  init_meas(plan, reg, out, 0);
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

// what s3d_limit finds in step run i against the effect's own limit: the
// level where it holds the boost, the one under which it rises, and its
// fall and rise where it fell, or no rise where the S out after the step
// down stays over the level it rises under
static void check_limit(int i) {
  struct s3d_lim l;
  double m, g, up_at, down_at, fall, rise, after, stop;
  u8 reg[S3D_REGS];
  int bad = 0, drop = run(i)->drop ? run(i)->drop : 12;

  CHECK_EQ(s3d_limit(&meas, i, &l), 0);
  s3d_regs(&meas, i, reg);
  m = S3D_AMP_M * pow(10.0, -run(i)->att / 20.0);
  // an M away from 400 Hz reaches the limit through its high-pass
  g = run(i)->mhz ? s3dsim_detect_m(S3D_RATE, run(i)->mhz) : 1;
  s3dsim_levels(reg, m * g, &up_at, &down_at);
  up_at += 20 * log10(g);
  down_at += 20 * log10(g);
  s3dsim_speed(reg, &fall, &rise);
  if (has(l.held))
    bad |= fabs(l.held - down_at) > 0.3;
  if (l.fall > 0) {
    bad |= fabs(l.fall - fall) > 0.05 * fall;
    // the S out right after the step down: where it was held, less the
    // drop, or the S alone with the boost off
    after = l.high > -40 ? l.held - drop : -drop;
    if (after < up_at - 0.5)
      bad |= fabs(l.rise - rise) > 0.1 * rise;
    else if (after > up_at + 0.5)
      bad |= l.rise != 0;
    bad |= l.rise > 0 && l.hold >= 0 && l.hold > 50;
  }
  // where it rose to, the lower of the two levels since it falls over
  // the other, or a level under or over that
  stop = up_at < down_at ? up_at : down_at;
  if (!has(l.rises))
    bad = 1;
  else if (l.rises_is == 0)
    bad |= fabs(l.rises - stop) > 0.5;
  else if (l.rises_is < 0)
    bad |= stop > l.rises + 0.5;
  else
    bad |= stop < l.rises - 0.5;
  CHECK(!bad);
  if (bad)
    printf("  run %s: held %.1f fall %.1f rise %.1f hold %d rises %d %.1f, "
           "the effect %.1f %.1f %.1f %.1f\n",
           s3d_name(&meas, i), l.held, l.fall, l.rise, l.hold, l.rises_is,
           l.rises, down_at, fall, rise, up_at);
}

static void check_report_width(void);

static void check_report_lines(void) {
  check_report_width();
  // Notepad opens it on Windows 98 (64 KB at most)
  CHECK(report_bytes < 62000);
}

// every line under 200 characters
static void check_report_width(void) {
  int i;

  for (i = 0; i < nlines; i++) {
    CHECK(strlen(report[i]) < 200);
    if (strlen(report[i]) >= 200)
      printf("  too long: %s\n", report[i]);
  }
}

static void test_quick_plan(void) {
  struct s3d_lim l;
  int k, off, ess, model, limit, ratio, ratlim, band, bandlim, pan, panlim;
  int step, steplim, line;
  double deg, lev;

  init_meas(S3D_PLAN_QUICK, 0, out, 0);
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
  // limit holds it at +5.5 dB re M, falls at 51.5 dB a second and rises
  // ten times slower, from the step down on
  CHECK_EQ(s3d_limit(&meas, step, &l), 0);
  CHECK(l.fall == 0 && fabs(l.high) < 0.2 && !has(l.held));
  check_limit(steplim);
  CHECK_EQ(s3d_limit(&meas, steplim, &l), 0);
  CHECK(l.fall > 50 && l.fall < 53 && l.rise > 5 && l.rise < 5.3);
  CHECK(l.hold >= 0 && l.hold < 30);
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
                  "by -15.3 dB; it holds S out at +5.4 to +5.4 dB re M") > 0);
  CHECK(find_line("pan: 0, 22.5, 45, 67.5 and 90 degrees come out at -") > 0);
  CHECK(find_line("limit: the boost's gain in the step runs") > 0);
  CHECK(find_line(" 22  step limit     0D 3F 8F 95 94 80      51 -14.4  +0.0 "
                  "    +5.4      0    5.2   +0.0    >+3.6") > 0);
  check_report_lines();
}

// the number right after text in the line of the properties that starts
// with start; 0 if there's none
static int num_after(const char *start, const char *text, double *v) {
  int i = find_from(find_line("properties: "), start);
  const char *p = i >= 0 ? strstr(report[i], text) : 0;

  if (p && sscanf(p + strlen(text), "%lf", v) == 1)
    return 1;
  printf("  no \"%s\" after \"%s\"\n", text, start);
  return 0;
}

// a property's value within lo and hi
static void check_prop(const char *start, const char *text, double lo,
                       double hi) {
  double v = 0;

  CHECK(num_after(start, text, &v) && v >= lo && v <= hi);
  if (v < lo || v > hi)
    printf("  %s %s %g, not %g to %g\n", start, text, v, lo, hi);
}

// the full plan: each property of the effect of /sim found again, in the
// summary's last part
static void test_full_plan(void) {
  int i;

  init_meas(S3D_PLAN_FULL, 0, out, 0);
  CHECK_EQ(s3d_count(&meas), 58);
  CHECK_EQ(s3d_chirps(&meas), 12);
  CHECK(s3d_seconds(&meas) > 420 && s3d_seconds(&meas) < 540);
  CHECK(run_named("chirp off") == 1 && run_named("L floor") > 0);
  CHECK(run_named("50=08") > 0 && run_named("M 54-5A=FF") > 0);
  run_plan(S3D_PLAN_FULL, 0, 0);
  // the limit in each step run but the floor's, which the effect's
  // levels don't give
  for (i = 0; i < s3d_count(&meas); i++)
    if (run(i)->kind == S3D_STEP && (run(i)->reg[0] & 1) &&
        i != run_named("L floor"))
      check_limit(i);
  // the glides of the runs by thirds of an octave
  CHECK(strstr(report[find_line("  4  52=20") + 2], "      65  ") != 0);
  CHECK(find_line("properties: ") > 0);
  // the path: 0.22 dB, and the all-pass's 1.7 us that grow to 2.1 over
  // 1 kHz
  check_prop("path: ", "the right channel ", 0.2, 0.24);
  check_prop("path: ", "over the left, and ", 1.9, 2.3);
  check_prop("path: ", "it moved ", 0, 0.05);
  // 50h at 08h: the S's 0.4 dB of bass
  check_prop("on: ", "changes S>S by ", 0.35, 0.45);
  check_prop("on: ", "at most, at ", 40, 60);
  check_prop("on: ", "M>M stays within ", 0, 0.05);
  // the boost: 380 and 540 Hz, 16.8 dB and 4.2 us, as near as a band-pass
  // in s comes to the effect's digital one
  check_prop("boost: ", "Q ", 0.45, 0.55);
  check_prop("boost: ", "peak ", 16.6, 17.0);
  check_prop("boost: ", "lag ", 3.5, 5.5);
  check_prop("52h: ", "the boost moves ", 0.74, 0.76);
  check_prop("52h: ", "it peaks at ", -31, -30);
  check_prop("limit off: ", "more than ", 0, 0.05);
  check_prop("model: M>S", "M>S peaks at ", 13.6, 14.0);
  check_prop("model: M>S", "Hz, ", 2.8, 3.2);
  check_prop("model: M>S", "52h moves M>S ", 0.74, 0.76);
  check_prop("model: 54h", "more than ", 0, 0.05);
  check_prop("model and limit: ", "up to ", 5, 15);
  // the limit: 91.2, 0.00188, the higher of the two, +0.5 dB, the speeds
  // of s3dsim_speed, the detector's 420 Hz on the S and 460 Hz on the M,
  // the floor
  check_prop("levels: ", "value / ", 90.5, 93);
  check_prop("levels: ", "plus 5Ah x ", 0.0018, 0.0020);
  check_prop("levels: ", "runs within ", 0, 0.25);
  check_prop("which: ", "hold within ", 0, 0.15);
  check_prop("rise level: ", "level ", 0.3, 0.8);
  check_prop("fall: ", "fall: ", 250, 265);
  check_prop("fall: ", "runs, ", 0, 2);
  check_prop("rise: ", "rise: ", 250, 265);
  CHECK(strstr(report[find_from(find_line("properties: "), "rise: ")],
               "the fall over h + 1") != 0);
  check_prop("detector: the limit", "high-pass at ", 380, 470);
  check_prop("detector: the S held", "160 Hz ", -5.5, -4.8);
  check_prop("detector: the S held", "high-pass at ", 440, 500);
  check_prop("floor: ", "down to ", -48, -46);
  check_prop("ratio: ", "S out at ", 5, 6);
  check_prop("52h and the limit: ", "dB re M, with 3Fh at ", 5, 6);
  check_prop("52h and the limit: ", "30h the limit holds the S out at ", 5, 6);
  // too big for Notepad: ess3d opens it in WordPad
  check_report_width();
  if (verbose)
    printf("full report: %d lines, %ld bytes\n", nlines, report_bytes);
}

// what ess3d's tray panel plots: the effect by frequency, and the limit's
// step, from the same effect as /sim
static void test_plot(void) {
  static const u8 ess[6] = {0x0C, 0x3F, 0x8F, 0x95, 0x94, 0x80};
  u8 reg[6];
  float out[161];
  double ss, ms, held, rise, fall, f2;
  int i, at94, at00;

  // the boost: about +18 dB at 450 Hz, nothing from the M
  s3dsim_curve(ess, 450, 0, &ss, &ms);
  CHECK(ss > 17.5 && ss < 18.5 && ms < -100);
  // and 10 dB lower, as the limit leaves it
  s3dsim_curve(ess, 450, -10, &f2, &ms);
  CHECK(f2 < ss - 5 && f2 > ss - 11);
  // off: the S as it is
  memcpy(reg, ess, 6);
  reg[0] = 0x04;
  s3dsim_curve(reg, 450, 0, &ss, &ms);
  CHECK(fabs(ss) < 0.01 && ms < -100);
  // held in reset: 0.4 dB of bass
  reg[0] = 0x08;
  s3dsim_curve(reg, 60, 0, &ss, &ms);
  CHECK(ss > 0.3 && ss < 0.5);
  // mono: the S from the M, 3 dB under the boost, and a 32nd of the S
  reg[0] = 0x0E;
  s3dsim_curve(reg, 450, 0, &ss, &ms);
  CHECK(fabs(ss + 30.1) < 0.2 && ms > 13 && ms < 15);

  // the limit with ESS's values: from the full boost down to 56h's level,
  // then, 12 dB narrower, back up to the full boost
  reg[0] = 0x0D;
  s3dsim_step(reg, 2.0, 0.05, 161, out, &held, &rise, &fall);
  CHECK(fall > 5 && fall < 6 && rise > fall);
  CHECK(out[0] > 15 && held < -12 && held > -16);
  CHECK(fabs(out[39] - fall) < 0.3);
  CHECK(out[41] < fall - 8);
  CHECK(fabs(out[160] - (out[0] - 12)) < 0.2);
  for (i = 1, at94 = 0; i < 40; i++) {
    CHECK(out[i] <= out[i - 1] + 0.2);
    if (!at94 && out[i] < fall + 0.5)
      at94 = i;
  }
  // a faster fall gets there sooner
  reg[4] = 0x00;
  s3dsim_step(reg, 2.0, 0.05, 161, out, &held, &rise, &fall);
  for (i = 1, at00 = 0; i < 40 && !at00; i++)
    if (out[i] < fall + 0.5)
      at00 = i;
  CHECK(at00 > 0 && at00 < at94);
  // without the limit, the width just follows the S
  reg[0] = 0x0C;
  reg[4] = 0x94;
  s3dsim_step(reg, 2.0, 0.05, 161, out, &held, &rise, &fall);
  CHECK(held == 0 && fabs(out[39] - out[0]) < 0.01 &&
        fabs(out[41] - out[0] + 12) < 0.05);
  // and its powers are the S out of the S at 1 kHz and of the M at 400 Hz,
  // as s3dsim_curve has them, before the drop and after, for off, reset,
  // stereo and mono
  for (i = 0; i < 4; i++) {
    static const u8 r50[4] = {0x04, 0x08, 0x0C, 0x0E};
    double s1k, m400, before, after;

    reg[0] = r50[i];
    s3dsim_curve(reg, 1000, 0, &s1k, &ms);
    s3dsim_curve(reg, 400, 0, &ss, &m400);
    before = 10 * log10(pow(10, s1k / 10) + pow(10, m400 / 10));
    after = 10 * log10(pow(10, s1k / 10) * 0.0630957 + pow(10, m400 / 10));
    s3dsim_step(reg, 2.0, 0.05, 161, out, &held, &rise, &fall);
    CHECK(fabs(out[0] - before) < 0.01 && fabs(out[39] - before) < 0.01);
    CHECK(fabs(out[41] - after) < 0.01 && fabs(out[160] - after) < 0.01);
    // the step that holds the drop has a fiftieth of it
    CHECK(out[40] < before && out[40] > before - 0.5);
  }
  // with 54h, 56h and 5Ah at 00h there is no level, and the boost falls
  // to its floor
  memcpy(reg, ess, 6);
  reg[0] = 0x0D;
  reg[2] = reg[3] = reg[5] = 0;
  s3dsim_step(reg, 2.0, 0.05, 161, out, &held, &rise, &fall);
  CHECK(rise == -120 && fall == -120 && held < -46);
}

// the panel's curves, worked out from filters cached at the first call, as
// s3dsim_curve gives them point by point, for every state of 50h bits 3:0
static void test_curves(void) {
  float ss[S3DSIM_CURVE_N], ms[S3DSIM_CURVE_N];
  double a, b, worst = 0;
  u8 reg[6] = {0, 0x3F, 0x8F, 0x95, 0x94, 0x80};
  int i, r, k;

  for (r = 0; r < 16; r++)
    for (k = 0; k < 2; k++) {
      reg[0] = (u8)r;
      reg[1] = (u8)(k ? 0x20 : 0x3F);
      s3dsim_curves(reg, k ? -10 : 0, ss, ms);
      for (i = 0; i < S3DSIM_CURVE_N; i++) {
        s3dsim_curve(reg, S3DSIM_CURVE_HZ * pow(2.0, i / 6.0), k ? -10 : 0, &a,
                     &b);
        if (fabs(ss[i] - a) > worst)
          worst = fabs(ss[i] - a);
        if (fabs(ms[i] - b) > worst)
          worst = fabs(ms[i] - b);
      }
    }
  CHECK(worst < 1e-3);
  if (verbose)
    printf("curves: within %.6f dB of s3dsim_curve\n", worst);
}

static void test_limit_plan(void) {
  struct s3d_lim a, b;
  int i, n = 0;

  init_meas(S3D_PLAN_LIMIT, 0, out, 0);
  CHECK_EQ(s3d_count(&meas), 62);
  CHECK(s3d_seconds(&meas) > 420 && s3d_seconds(&meas) < 540);
  CHECK(run_named("L 54=0F b7") > 0 && run_named("L 5A=81 b0") > 0);
  CHECK(run_named("52=20") < run_named("L 52=20"));
  CHECK(run_named("L 54 56 b7") > 0 && run_named("L 54,56=7F") > 0);
  run_plan(S3D_PLAN_LIMIT, 0, 0);
  // every bit of 54h-5Ah, against the effect's limit
  for (i = 0; i < s3d_count(&meas); i++)
    if (run(i)->kind == S3D_STEP && (run(i)->reg[0] & 1)) {
      check_limit(i);
      n++;
    }
  CHECK_EQ(n, 53);
  // the effect's limit holds the S out at K M + D, so with the M 12 dB
  // lower it holds it 2.8 dB higher against the M
  CHECK_EQ(s3d_limit(&meas, run_named("step limit"), &a), 0);
  CHECK_EQ(s3d_limit(&meas, run_named("L -12 dB"), &b), 0);
  CHECK(run(run_named("L -12 dB"))->att == 12);
  CHECK(fabs(b.held - a.held - 2.8) < 0.3 && fabs(a.fall - b.fall) < 2.5);
  // a fast limit gets to the level at each frequency, which its high-pass
  // raises under 1 kHz
  i = run_named("band L 58=00");
  CHECK(i > 0 && fabs(val(i, 1, 6) - a.held) < 0.6);
  CHECK(i > 0 && val(i, 1, 2) > a.held + 4);
  CHECK(find_line("plan: limit, 62 runs") == 1);
  check_report_lines();
  if (verbose)
    printf("limit report: %d lines, %ld bytes\n", nlines, report_bytes);
}

static void test_window_plan(void) {
  struct s3d_lim l;
  int i, n = 0, line;

  init_meas(S3D_PLAN_WINDOW, 0, out, 0);
  CHECK_EQ(s3d_count(&meas), 31);
  CHECK(s3d_seconds(&meas) > 210 && s3d_seconds(&meas) < 280);
  CHECK(run(run_named("L -18 dB"))->att == 18);
  CHECK(run(run_named("W3 8F FF"))->drop == 3 &&
        run(run_named("W18 0F FF"))->drop == 18 &&
        !run(run_named("L 3F 3F"))->drop);
  CHECK(run(run_named("L M 160 Hz"))->mhz == 160 &&
        !run(run_named("L 7F 00"))->mhz);
  run_plan(S3D_PLAN_WINDOW, 0, 0);
  for (i = 0; i < s3d_count(&meas); i++)
    if (run(i)->kind == S3D_STEP && (run(i)->reg[0] & 1)) {
      check_limit(i);
      n++;
    }
  CHECK_EQ(n, 27);
  // the window: with ESS's values, and with 54h's level over 56h's, the
  // boost rises back to where it was held, while with 56h's level over
  // 54h's it stays where it was, unless the S drops under 54h's level, and
  // then it rises to that
  CHECK_EQ(s3d_limit(&meas, run_named("W3 8F 95"), &l), 0);
  CHECK(l.rises_is == 0 && l.rise > 4.5 && fabs(l.rises - l.held) < 0.3);
  CHECK_EQ(s3d_limit(&meas, run_named("W3 FF 8F"), &l), 0);
  CHECK(l.rises_is == 0 && fabs(l.rises - l.held) < 0.3);
  CHECK_EQ(s3d_limit(&meas, run_named("W3 8F FF"), &l), 0);
  CHECK(l.rises_is < 0 && l.rise == 0 && fabs(l.again - l.high) < 0.3);
  CHECK_EQ(s3d_limit(&meas, run_named("W6 8F FF"), &l), 0);
  CHECK(l.rises_is == 0 && fabs(l.rises - 5.7) < 0.2);
  // with 54h at 00h it never rises from 12 dB under
  CHECK_EQ(s3d_limit(&meas, run_named("L 00 7F"), &l), 0);
  CHECK(l.rises_is < 0 && l.rise == 0);
  // with 54h and 56h at 3Fh, the limit holds the S out at -0.6 dB re M,
  // so the boost is off at 0 dB S/M
  CHECK_EQ(s3d_limit(&meas, run_named("L 3F 3F"), &l), 0);
  CHECK(l.high < -40 && has(l.held) && l.held < -0.3 && l.held > -1.0);
  // K M + D: K follows the higher of 54h and 56h, and D only 5Ah
  line = find_line("levels: ");
  CHECK(line > 0);
  CHECK(find_from(line, "  3  step limit     0D 3F 8F 95 94 80     +4.2   "
                        "-36.4  3, 4, 5, 6, 19") > 0);
  CHECK(find_from(line, "  7  L 5A=00        0D 3F 8F 95 94 00     +4.2       "
                        "-  7, 8") > 0);
  CHECK(find_from(line, " 12  L 54=AF        0D 3F AF 95 94 80     +5.6   "
                        "-36.4  12, 13") > 0);
  CHECK(find_from(line, " 14  L 80 80        0D 3F 80 80 94 80     +2.9   "
                        "-36.4  14, 15") > 0);
  // and the one model for all of them, with the rise 0.5 dB over 54h's
  // level, as in the effect
  CHECK(find_line("fit: S out held = M x the higher of 54h and 56h / 91.9 + "
                  "-36.4 dBFS x 5Ah / 80h, from 24 runs: within 0.08 dB") > 0);
  CHECK(find_line("rise level: where the boost stopped rising short of the "
                  "level held, it stopped +0.6 dB over 54h's level of the fit "
                  "(runs 23, 24)") > 0);
  CHECK(find_line("in the runs named with -6, -12 and -18, every tone is that "
                  "many dB lower") > 0);
  CHECK(find_line("the runs named W3, W6 and W18 step the S back to that many "
                  "dB under the M after 2 s, instead of 12") > 0);
  CHECK(find_line("in the runs named L M with a frequency, the M tone is at "
                  "160 Hz and 2500 Hz instead of 400 Hz") > 0);
  CHECK(find_line("plan: window, 31 runs") == 1);
  check_report_lines();
  if (verbose)
    printf("window report: %d lines, %ld bytes\n", nlines, report_bytes);
}

static void test_one_register(void) {
  int i;

  nlines = 0;
  init_meas(S3D_PLAN_REG, 0x58, out, 0);
  CHECK_EQ(s3d_count(&meas), 27);
  CHECK(run_named("58=00") == 2 && run_named("58=FF") == 3);
  CHECK(run_named("M 58=FF") == 6 && run_named("step limit") == 8);
  CHECK(run_named("L 58=00") == 9 && run_named("L 58=FF") == 25);
  run_plan(S3D_PLAN_REG, 0x58, 0);
  for (i = 9; i <= 25; i++)
    check_limit(i);
  check_report_lines();
  // a register that isn't one of 54h-5Ah gives the quick plan
  init_meas(S3D_PLAN_REG, 0x50, out, 0);
  CHECK_EQ(s3d_count(&meas), 25);
}

// run 0 recorded at rate Hz, where the report says rate_rec, of the
// stimulus played at 48 kHz: the stimulus, sampled where the recorder's
// clock puts it
static int record_at(double rate, u32 rate_rec) {
  u32 rec_frames, r;
  double pos, l, rr;
  int full = 0;
  u16 n;

  nlines = 0;
  init_meas(S3D_PLAN_QUICK, 0, out, 0);
  s3d_header(&meas, S3D_RATE, rate_rec, 0);
  s3d_begin(&meas, 0, &rec_frames);
  // the recording starts 70 ms before the playback
  for (r = 0; !full && r < rec_frames; r += n) {
    for (n = 0; n < BLOCK; n++) {
      pos = ((double)(r + n) / rate - 0.070) * S3D_RATE;
      s3d_signal(&meas, pos, &l, &rr);
      rec[2 * n] = (s16)floor(0.4 * l * 32767 + 0.5);
      rec[2 * n + 1] = (s16)floor(0.4 * rr * 32767 + 0.5);
    }
    full = s3d_take(&meas, rec, n);
  }
  return s3d_end(&meas);
}

// a recording at 44,194 Hz, and one whose clock is 30 ppm off the
// playback's, which turns a 10 kHz tone by 5 degrees in half a window
static void test_other_rate(void) {
  int k;

  CHECK_EQ(record_at(48000 * (1 + 30e-6), 48000), S3D_OK);
  CHECK_EQ(s3d_disturbed(&meas), 0);
  CHECK_EQ(record_at(44194, 44194), S3D_OK);
  CHECK_EQ(s3d_disturbed(&meas), 0);
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
  init_meas(S3D_PLAN_QUICK, 0, out, 0);
  s3dsim_reset(&sim, S3D_RATE);
  s3d_header(&meas, S3D_RATE, S3D_RATE, 0);
  for (i = 0; i < 2; i++)
    CHECK_EQ(run_sim(i, 1500), S3D_OK);
  click = 0;
  check_path(1, 1, 0.15);
  check_path(0, 2, 0.3);
}

// the recording skips 21 ms in the window of the 1 kHz S tone: the halves
// of that window, and of the ones after it, differ
static void test_skip(void) {
  int line;

  nlines = 0;
  init_meas(S3D_PLAN_QUICK, 0, out, 0);
  s3dsim_reset(&sim, S3D_RATE);
  s3d_header(&meas, S3D_RATE, S3D_RATE, 0);
  CHECK_EQ(run_sim(0, 2000), S3D_OK);
  // 2000 frames of lead, the delay, 350 ms before the tones, 16 tones of
  // 160 ms, and 80 ms into the next
  skip_at = 2000 + S3DSIM_DELAY + 48 * (350 + 16 * 160 + 80);
  CHECK_EQ(run_sim(1, 2000) & ~S3D_CLIPPED, S3D_OK);
  CHECK(s3d_disturbed(&meas) >= 2);
  line = find_line("     (the recording or the playback skipped in the window "
                   "of the S tone at 1k and ");
  CHECK(line > 0 && strstr(report[line], "the values from there on may be "
                                         "off)"));
  skip_at = 0;
  // the run again is good
  CHECK_EQ(run_sim(1, 2000), S3D_OK);
  CHECK_EQ(s3d_disturbed(&meas), 0);
}

// the playback stalls for 83 ms in the step up: the step down comes late,
// the S jumps further than the limit can move it, and the run counts as
// disturbed; measured again, it's good
static void test_stall(void) {
  int i, line;

  nlines = 0;
  init_meas(S3D_PLAN_QUICK, 0, out, 0);
  s3dsim_reset(&sim, S3D_RATE);
  s3d_header(&meas, S3D_RATE, S3D_RATE, 0);
  CHECK_EQ(run_sim(0, 2000), S3D_OK);
  CHECK_EQ(run_sim(1, 2000), S3D_OK);
  i = run_named("step limit");
  // 350 ms before the tones, 1.5 s at -12 dB S/M, then 1 s at 0 dB
  stall_at = 48 * (350 + 1500 + 1000);
  CHECK_EQ(run_sim(i, 2000), S3D_OK);
  CHECK(s3d_disturbed(&meas) >= 1);
  line = find_line("     (the recording or the playback skipped in the window "
                   "0 ms after the step down");
  CHECK(line > 0 && strstr(report[line], "the values from there on may be "
                                         "off)"));
  stall_at = 0;
  CHECK_EQ(run_sim(i, 2000), S3D_OK);
  CHECK_EQ(s3d_disturbed(&meas), 0);
  check_limit(i);
}

static void test_no_signal(void) {
  u32 rec_frames, r;
  int full = 0;

  nlines = 0;
  init_meas(S3D_PLAN_QUICK, 0, out, 0);
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

// the numbers in Hz after "; poles" or "; zeros" in a report line, up
// to n of them, and before the next ";"
static int hz_list(const char *line, const char *what, double *hz, int n) {
  const char *p = strstr(line, what), *stop;
  int k = 0, len;

  if (!p)
    return 0;
  p += strlen(what);
  stop = strchr(p, ';');
  while (k < n && *p && (!stop || p < stop)) {
    while (*p == ' ' || *p == ',')
      p++;
    if (sscanf(p, "%lf%n", &hz[k], &len) != 1 || strncmp(p + len, " Hz", 3))
      break;
    k++;
    p = strchr(p, ',');
    if (!p)
      break;
  }
  return k;
}

// the line of run i's chosen fit and its roots, after the filters line
static int fit_lines(int i, int *chosen, int *roots) {
  char head[8];
  int at = find_line("filters:");

  sprintf(head, "%3d  ", i);
  at = find_from(at, head);
  *chosen = find_from(at, "     ");
  while (*chosen >= 0 && !strstr(report[*chosen], "(chosen)"))
    (*chosen)++;
  *roots = *chosen >= 0 ? find_from(*chosen, "     peak ") : -1;
  return at >= 0 && *chosen >= 0 && *roots >= 0;
}

// the chirp plan: each twelfth of an octave against the effect's own
// response, and the filters the summary fits: the boost's high-pass at
// 380 Hz and low-pass at 540 Hz, with a zero at 0 Hz
static void test_chirp_plan(void) {
  const struct s3d_chirp *c;
  int i, k, p, q, n = 0, chosen, roots;
  double want, got, worst[S3D_PATHS] = {0}, worst_deg[3] = {0}, hz[4];

  init_meas(S3D_PLAN_CHIRP, 0, out, 0);
  CHECK_EQ(s3d_count(&meas), 5);
  CHECK(s3d_seconds(&meas) >= 40 && s3d_seconds(&meas) <= 70);
  // ess3d holds it in one segment of Win16's memory
  CHECK(sizeof(meas) < 65000);
  run_plan(S3D_PLAN_CHIRP, 0, 0);
  for (i = 1; i < s3d_count(&meas); i++) {
    c = &meas.chirp[run(i)->slot];
    for (k = 0; k < S3D_BINS; k++) {
      for (p = 0; p < S3D_PATHS; p++) {
        want = model_db(i, p, s3d_bin_hz(k));
        got = c->db[p][k];
        if (p >= 2 && want < -15)
          continue;
        CHECK(has(got));
        n++;
        if (has(got) && fabs(got - want) > worst[p])
          worst[p] = fabs(got - want);
      }
      for (q = 0; q < 3; q++) {
        got = c->ph[q][k];
        if (!has(got) || (q >= 1 && model_db(i, q + 1, s3d_bin_hz(k)) < -15))
          continue;
        want = model_deg(i, q, s3d_bin_hz(k));
        if (fabs(wrap(got - want)) > worst_deg[q])
          worst_deg[q] = fabs(wrap(got - want));
      }
    }
  }
  printf("  chirp plan: %d values, within %.3f dB and %.2f degrees\n", n,
         worst[0] > worst[1] ? worst[0] : worst[1],
         worst_deg[0] > worst_deg[1] ? worst_deg[0] : worst_deg[1]);
  CHECK(n > 800);
  for (p = 0; p < S3D_PATHS; p++)
    CHECK(worst[p] < 0.05);
  for (q = 0; q < 3; q++)
    CHECK(worst_deg[q] < 0.5);
  // the chirp runs in the summary, at a frequency of their own
  CHECK(find_line("  2  52=30          0C 30 8F 95 94 80     1  S>S") >= 0);
  // the fits: ESS's boost, the lower level and the model's M>S each come
  // within 0.1 dB, and the boost as a band-pass finds the center of the
  // poles at 380 and 540 Hz, 453 Hz, and the lag of 4.2 us, as near as a
  // band-pass in s comes to the effect's digital one
  for (i = 1; i <= 3; i++) {
    CHECK(fit_lines(i, &chosen, &roots));
    if (roots < 0)
      continue;
    CHECK(strstr(report[chosen], "rms 0.0") != 0);
    CHECK(hz_list(report[roots], "; zeros", hz, 4) >= 1);
    CHECK(hz[0] < 2);
    if (i < 3) {
      CHECK(sscanf(strstr(report[roots + 1], "at ") + 3, "%lf", &hz[0]) == 1);
      CHECK(fabs(hz[0] - 452) < 6);
      CHECK(sscanf(strstr(report[roots + 1], "lag ") + 4, "%lf", &hz[0]) == 1);
      CHECK(hz[0] > 3.5 && hz[0] < 5.5);
    }
  }
  CHECK(fit_lines(1, &chosen, &roots));
  if (roots >= 0)
    CHECK(!strncmp(report[roots], "     peak +16.8 dB at 4", 23));
  // the runs with the effect off have nothing to fit
  CHECK(find_from(find_line("filters:"), "  4  off again") < 0);
}

// set chirp run i's results to a boost B = num(s) / den(s), s = j f / 1
// kHz, each with n + 1 coefficients from s^0 up
static void set_boost(int i, const double *num, int nn, const double *den,
                      int nd) {
  struct s3d_chirp *c = &meas.chirp[meas.run[i].slot];
  double w, pr, pi, t, nr, ni, dr, di, d, hr, hi;
  int k, j;

  meas.run[i].result = S3D_OK;
  for (k = 0; k < S3D_BINS; k++) {
    w = s3d_bin_hz(k) / 1000;
    nr = ni = dr = di = 0;
    pr = 1;
    pi = 0;
    for (j = 0; j <= (nn > nd ? nn : nd); j++) {
      if (j <= nn) {
        nr += num[j] * pr;
        ni += num[j] * pi;
      }
      if (j <= nd) {
        dr += den[j] * pr;
        di += den[j] * pi;
      }
      // times s = j w
      t = -w * pi;
      pi = w * pr;
      pr = t;
    }
    d = dr * dr + di * di;
    hr = 1 + (nr * dr + ni * di) / d;
    hi = (ni * dr - nr * di) / d;
    c->db[0][k] = 0;
    c->db[1][k] = (float)(10 * log10(hr * hr + hi * hi));
    c->db[2][k] = c->db[3][k] = -40;
    c->ph[0][k] = (float)(atan2(hi, hr) * 180 / PI);
    c->ph[1][k] = c->ph[2][k] = 0;
  }
}

// the fit on a boost given exactly: a resonance with its Q, and the
// patent's high-pass of 18 dB an octave at 300 Hz under a low-pass at 2
// kHz
static void test_filter_fit(void) {
  // 3 s / (s^2 + s / 4 + 1 / 4): a pair at 500 Hz with a Q of 2
  static const double num1[2] = {0, 3}, den1[3] = {0.25, 0.25, 1};
  // 3 s^3 / ((s + 0.3) (s^2 + 0.3 s + 0.09) (1 + s / 2)): Butterworth at
  // 300 Hz
  static const double num2[4] = {0, 0, 0, 3};
  static const double den2[5] = {0.027, 0.1935, 0.69, 1.3, 0.5};
  // 6.365 s / ((s + 0.38) (s + 0.54)): ESS's boost, as the card's chirp
  // runs gave it
  static const double num3[2] = {0, 6.365}, den3[3] = {0.2052, 0.92, 1};
  static const double num4[3] = {0, 6.365, -6.365 / 37};
  int chosen, roots, k;
  double hz[4];

  nlines = 0;
  init_meas(S3D_PLAN_CHIRP, 0, out, 0);
  meas.run[0].result = S3D_OK;
  set_boost(1, num1, 1, den1, 2);
  set_boost(2, num2, 3, den2, 4);
  // ESS's boost with the noise of a measurement, 0.05 dB and 0.3 degrees
  meas.run[3].reg[S3D_R50] = 0x0C;
  set_boost(3, num3, 1, den3, 2);
  for (k = 0; k < S3D_BINS; k++) {
    meas.chirp[meas.run[3].slot].db[1][k] += (float)(0.05 * sin(k * 1.7));
    meas.chirp[meas.run[3].slot].ph[0][k] += (float)(0.3 * cos(k * 2.3));
  }
  // the same with a zero in the right half at 37 kHz, as the card's fit
  // has it
  meas.run[4].reg[S3D_R50] = 0x0C;
  set_boost(4, num4, 2, den3, 2);
  s3d_summary(&meas);
  CHECK(fit_lines(1, &chosen, &roots));
  if (roots >= 0) {
    CHECK(strstr(report[chosen], "1 zero, 2 poles: rms 0.00 dB") != 0);
    CHECK(strstr(report[roots], "; poles 500 Hz Q 2.00") != 0);
    CHECK_EQ(hz_list(report[roots], "; zeros", hz, 4), 1);
    CHECK(hz[0] < 1);
  }
  CHECK(fit_lines(2, &chosen, &roots));
  if (roots >= 0) {
    CHECK(strstr(report[chosen], "3 zeros, 4 poles: rms 0.00 dB") != 0);
    CHECK_EQ(hz_list(report[roots], "; zeros", hz, 4), 3);
    CHECK(hz[0] < 1 && hz[1] < 1 && hz[2] < 1);
    CHECK_EQ(hz_list(report[roots], "; poles", hz, 4), 3);
    CHECK(strstr(report[roots], "300 Hz Q 1.00") != 0);
    CHECK(strstr(report[roots], "2000 Hz") != 0);
  }
  // the noise doesn't take a higher order
  CHECK(fit_lines(3, &chosen, &roots));
  if (roots >= 0) {
    CHECK(strstr(report[chosen], "1 zero, 2 poles: rms 0.0") != 0);
    CHECK_EQ(hz_list(report[roots], "; poles", hz, 4), 2);
    CHECK(fabs(hz[0] - 380) < 8 && fabs(hz[1] - 540) < 10);
  }
  // and the zero in the right half as the lag that it makes
  CHECK(fit_lines(4, &chosen, &roots));
  if (roots >= 0) {
    CHECK(strstr(report[chosen], "2 zeros, 2 poles: rms 0.00 dB") != 0);
    CHECK(strstr(report[roots], "; zeros 0 Hz, 37000 Hz right half, as a "
                                "lag of 4.3 us; poles ") != 0 ||
          strstr(report[roots], "; zeros 37000 Hz right half, as a lag of "
                                "4.3 us, 0 Hz; poles ") != 0);
  }
}

// a chirp run whose M has a delay of its own, which changes with the
// frequency and times the glide differently from the reference: no
// window counts as disturbed, and M>M stays at 0 dB
static void test_chirp_m_delay(void) {
  const struct s3d_chirp *c;
  double worst = 0;
  int k;

  nlines = 0;
  init_meas(S3D_PLAN_CHIRP, 0, out, 0);
  s3dsim_reset(&sim, S3D_RATE);
  s3d_header(&meas, S3D_RATE, S3D_RATE, 0);
  CHECK_EQ(run_sim(0, 2000), S3D_OK);
  allpass = 1;
  ap_x = ap_y = 0;
  CHECK_EQ(run_sim(1, 2100), S3D_OK);
  allpass = 0;
  c = &meas.chirp[run(1)->slot];
  for (k = 0; k < S3D_BINS; k++)
    if (fabs(c->db[0][k]) > worst)
      worst = fabs(c->db[0][k]);
  CHECK(worst < 0.05);
  if (worst >= 0.05)
    printf("  chirp with the M delayed: M>M off by %.3f dB\n", worst);
}

// a chirp run whose recording skips, before the glide is timed and after
static void test_chirp_skip(void) {
  static const u32 at[2] = {80000, 175000};
  int i;

  for (i = 0; i < 2; i++) {
    nlines = 0;
    init_meas(S3D_PLAN_CHIRP, 0, out, 0);
    s3dsim_reset(&sim, S3D_RATE);
    s3d_header(&meas, S3D_RATE, S3D_RATE, 0);
    CHECK_EQ(run_sim(0, 2000), S3D_OK);
    skip_at = at[i];
    run_sim(1, 2000);
    CHECK(s3d_disturbed(&meas) > 0);
    skip_at = 0;
  }
}

int main(int argc, char **argv) {
  verbose = argc > 1 && !strcmp(argv[1], "-v");
  test_quick_plan();
  test_full_plan();
  test_plot();
  test_curves();
  test_limit_plan();
  test_window_plan();
  test_one_register();
  test_other_rate();
  test_click();
  test_skip();
  test_stall();
  test_no_signal();
  test_chirp_plan();
  test_filter_fit();
  test_chirp_m_delay();
  test_chirp_skip();
  return CHECK_DONE("t_s3dmeas");
}
