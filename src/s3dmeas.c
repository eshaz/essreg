/*
 * The 3-D effect's measurement: the plans, the stimulus, the analysis of
 * the recording and the report (see s3dmeas.h).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "s3dmeas.h"

#define PI 3.14159265358979
#define AMP 0.125     // sweep and pan tones, -18 dBFS in each channel
#define AMP_SYNC 0.25 // the burst that marks the start
#define AMP_M 0.0625  // the tones of ratio, band and step runs
#define FADE 5        // ms of the fades at the ends of a tone
#define F400 3        // freq[] index of 400 Hz
#define F1K 5         // and of 1 kHz
#define CROSS_MIN -15 // the summary leaves out cross paths under this, dB
#define OFF_DB -40    // a boost under this is off, dB

// a step run: the S tone at -12 dB re the M tone, at 0 dB, then at -12 dB
// again, in ms that are whole cycles of both tones
#define STEP_LOW 1500
#define STEP_HIGH 2000
#define STEP_DOWN 3200

// the windows of a step run
#define W_LOW 0                 // steady, before the step up
#define W_UP 1                  // after the step up
#define W_HIGH (W_UP + S3D_UP)  // steady, before the step down
#define W_DOWN (W_HIGH + 1)     // after the step down
#define W_END (W_DOWN + S3D_DN) // steady, at the end

// recording states
#define R_NOISE 0   // the first 50 ms: the noise floor
#define R_ONSET 1   // looking for the burst
#define R_ANALYZE 2 // the windows
#define R_DONE 3
#define R_FAIL 4 // no burst

static const u16 freq[S3D_FREQS] = {100,  160,  250,  400,  630,  1000,
                                    1600, 2500, 4000, 6300, 10000};
static const char *const freq_name[S3D_FREQS] = {"100", "160",  "250",  "400",
                                                 "630", "1k",   "1.6k", "2.5k",
                                                 "4k",  "6.3k", "10k"};
static const char *const path_name[S3D_PATHS] = {"M>M", "S>S", "M>S", "S>M"};
static const char *const plan_name[4] = {"full", "quick", "one register",
                                         "limit"};

// what ESS's driver sets when Windows starts (docs/SPATIALIZER.md)
static const u8 ess_set[S3D_REGS] = {0x0C, 0x3F, 0x8F, 0x95, 0x94, 0x80};
static const u8 reg_addr[S3D_REGS] = {0x50, 0x52, 0x54, 0x56, 0x58, 0x5A};

// ratio runs: the S tone relative to the M tone, in dB
static const s8 ratio_db[5] = {-24, -12, -6, 0, 6};

struct cx {
  double re, im;
};

// play frames of ms milliseconds (the stimulus is made at S3D_RATE)
static u32 fr(u32 ms) { return ms * (S3D_RATE / 1000); }

static double db(double ratio) {
  return ratio > 1e-9 ? 20.0 * log10(ratio) : -180.0;
}

// a value to print with one decimal, without -0.0
static double tidy(double v) { return fabs(v) < 0.05 ? 0 : v; }

// degrees into -180 to 180
static double wrap(double deg) {
  while (deg > 180)
    deg -= 360;
  while (deg <= -180)
    deg += 360;
  return deg;
}

static int has(double v) { return v > S3D_FLOOR + 1; }

// --- the plans ------------------------------------------------------------

static int add(struct s3d_meas *m, const char *name, const u8 *reg, int kind,
               int base) {
  struct s3d_run *r;

  if (m->nruns >= S3D_MAX_RUNS)
    return m->nruns - 1;
  r = &m->run[m->nruns];
  memset(r, 0, sizeof(*r));
  strncpy(r->name, name, sizeof(r->name) - 1);
  memcpy(r->reg, reg, S3D_REGS);
  r->kind = (u8)kind;
  r->base = (u8)base;
  r->slow = (u8)(reg[S3D_R50] & 1); // the limit on
  r->result = 0xFF;
  return m->nruns++;
}

// a run with one register of a setting changed, named "54=0F" or with a
// prefix
static int add_reg(struct s3d_meas *m, const char *prefix, const u8 *from,
                   int i, u8 v, const char *more, int kind, int base) {
  u8 reg[S3D_REGS];
  char name[24];

  memcpy(reg, from, S3D_REGS);
  reg[i] = v;
  sprintf(name, "%s%02X=%02X%s", prefix, reg_addr[i], v, more);
  return add(m, name, reg, kind, base);
}

// a setting with 54h, 56h and 58h changed together, as one vector
static void add_vec(struct s3d_meas *m, const char *name, const u8 *from,
                    u8 v54, u8 v56, u8 v58, int base) {
  u8 reg[S3D_REGS];

  memcpy(reg, from, S3D_REGS);
  reg[S3D_R54] = v54;
  reg[S3D_R56] = v56;
  reg[S3D_R58] = v58;
  add(m, name, reg, S3D_STEP, base);
}

// bits 6:0 halved or doubled, bit 7 kept
static u8 half(u8 v) { return (u8)(0x80 | ((v & 0x7F) >> 1)); }

static u8 twice(u8 v) {
  int x = (v & 0x7F) * 2;

  return (u8)(0x80 | (x > 0x7F ? 0x7F : x));
}

// 54h-58h as one vector, with the limit on, where they act: the sign bits
// cleared, the length halved and doubled, the coordinates turned and two
// of them swapped
static void add_vecs(struct s3d_meas *m, const u8 *from, int all, int base) {
  add_vec(m, "vec neg", from, 0x0F, 0x15, 0x14, base);
  if (!all)
    return;
  add_vec(m, "vec /2", from, half(0x8F), half(0x95), half(0x94), base);
  add_vec(m, "vec x2", from, twice(0x8F), twice(0x95), twice(0x94), base);
  add_vec(m, "vec rot", from, 0x95, 0x94, 0x8F, base);
  add_vec(m, "vec swap", from, 0x95, 0x8F, 0x94, base);
}

// each bit of ESS's value of 54h-5Ah flipped, and each register at its
// ends, in a step run with the limit on
static void add_limit_bits(struct s3d_meas *m, const u8 *lim, int base) {
  char more[8];
  int i, b;

  for (i = S3D_R54; i <= S3D_R5A; i++) {
    for (b = 7; b >= 0; b--) {
      sprintf(more, " b%d", b);
      add_reg(m, "L ", lim, i, (u8)(ess_set[i] ^ (1 << b)), more, S3D_STEP,
              base);
    }
    add_reg(m, "L ", lim, i, 0x00, "", S3D_STEP, base);
    add_reg(m, "L ", lim, i, 0xFF, "", S3D_STEP, base);
  }
}

void s3d_init(struct s3d_meas *m, int plan, u8 reg, s3d_out_fn out, void *ctx) {
  static const u8 levels[8] = {0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38};
  u8 r[S3D_REGS], model[S3D_REGS], lim[S3D_REGS];
  int i, ess, mod, band, blim, step, slim, pan, ri = -1;
  int full = plan == S3D_PLAN_FULL;

  memset(m, 0, sizeof(*m));
  m->out = out;
  m->ctx = ctx;
  m->plan = plan;
  m->rate_play = m->rate_rec = S3D_RATE;
  for (i = S3D_R54; i <= S3D_R5A; i++)
    if (reg_addr[i] == reg)
      ri = i;
  if (plan == S3D_PLAN_REG && ri < 0)
    m->plan = plan = S3D_PLAN_QUICK;

  // the reference, the effect off but out of reset, then ESS's setting
  memcpy(r, ess_set, S3D_REGS);
  r[S3D_R50] = 0x04;
  add(m, "off", r, S3D_SWEEP, 0);
  ess = add(m, "ess", ess_set, S3D_SWEEP, 0);
  // the model (50h bit 1) and the limit (50h bit 0)
  memcpy(model, ess_set, S3D_REGS);
  model[S3D_R50] = 0x0E;
  memcpy(lim, ess_set, S3D_REGS);
  lim[S3D_R50] = 0x0D;

  if (plan == S3D_PLAN_LIMIT) {
    // the limit, where 54h-5Ah act on the card; 52=20 gives the B of its
    // step run
    add_reg(m, "", ess_set, S3D_R52, 0x20, "", S3D_SWEEP, ess);
    add(m, "ratio", ess_set, S3D_RATIO, ess);
    add(m, "ratio limit", lim, S3D_RATIO, ess);
    step = add(m, "step", ess_set, S3D_STEP, ess);
    slim = add(m, "step limit", lim, S3D_STEP, step);
    add_limit_bits(m, lim, slim);
    add_reg(m, "L ", lim, S3D_R52, 0x20, "", S3D_STEP, slim);
    add_vecs(m, lim, 1, slim);
  } else if (plan == S3D_PLAN_REG) {
    // one register: its ends with the limit off and with the model, then
    // every 10h with the limit on
    add_reg(m, "", ess_set, ri, 0x00, "", S3D_SWEEP, ess);
    add_reg(m, "", ess_set, ri, 0xFF, "", S3D_SWEEP, ess);
    mod = add(m, "model", model, S3D_SWEEP, ess);
    add_reg(m, "M ", model, ri, 0x00, "", S3D_SWEEP, mod);
    add_reg(m, "M ", model, ri, 0xFF, "", S3D_SWEEP, mod);
    add(m, "ratio limit", lim, S3D_RATIO, ess);
    slim = add(m, "step limit", lim, S3D_STEP, ess);
    for (i = 0; i <= 0x100; i += 0x10)
      add_reg(m, "L ", lim, ri, (u8)(i > 0xFF ? 0xFF : i), "", S3D_STEP, slim);
  } else {
    for (i = 0; i < 8; i++)
      if (full || levels[i] == 0x00 || levels[i] == 0x20)
        add_reg(m, "", ess_set, S3D_R52, levels[i], "", S3D_SWEEP, ess);
    mod = add(m, "model", model, S3D_SWEEP, ess);
    add(m, "limit", lim, S3D_SWEEP, ess);
    r[S3D_R50] = 0x0F;
    add(m, "model+limit", r, S3D_SWEEP, ess);
    // 54h-5Ah at their ends, with the limit off and with the model
    for (i = S3D_R54; i <= S3D_R5A; i++) {
      add_reg(m, "", ess_set, i, 0x00, "", S3D_SWEEP, ess);
      add_reg(m, "", ess_set, i, 0xFF, "", S3D_SWEEP, ess);
    }
    for (i = S3D_R54; full && i <= S3D_R5A; i++) {
      add_reg(m, "M ", model, i, 0x00, "", S3D_SWEEP, mod);
      add_reg(m, "M ", model, i, 0xFF, "", S3D_SWEEP, mod);
    }
    // whether the gain follows the program, in which band, where a panned
    // tone comes out, and how the limit moves, with each register at its
    // ends
    add(m, "ratio", ess_set, S3D_RATIO, ess);
    add(m, "ratio limit", lim, S3D_RATIO, ess);
    band = add(m, "band", ess_set, S3D_BAND, ess);
    blim = add(m, "band limit", lim, S3D_BAND, band);
    if (full)
      add_reg(m, "band L ", lim, S3D_R58, 0x00, "", S3D_BAND, blim);
    pan = add(m, "pan", ess_set, S3D_PAN, ess);
    add(m, "pan limit", lim, S3D_PAN, pan);
    step = add(m, "step", ess_set, S3D_STEP, ess);
    slim = add(m, "step limit", lim, S3D_STEP, step);
    for (i = S3D_R54; full && i <= S3D_R5A; i++) {
      add_reg(m, "L ", lim, i, 0x00, "", S3D_STEP, slim);
      add_reg(m, "L ", lim, i, 0xFF, "", S3D_STEP, slim);
    }
    if (full)
      add_reg(m, "L ", lim, S3D_R52, 0x20, "", S3D_STEP, slim);
    add_vecs(m, lim, full, slim);
  }
  // the path again, for how much it moved
  memcpy(r, ess_set, S3D_REGS);
  r[S3D_R50] = 0x04;
  add(m, "off again", r, S3D_SWEEP, 0);
}

int s3d_count(const struct s3d_meas *m) { return m->nruns; }

void s3d_regs(const struct s3d_meas *m, int i, u8 *reg) {
  memcpy(reg, m->run[i].reg, S3D_REGS);
}

const char *s3d_name(const struct s3d_meas *m, int i) { return m->run[i].name; }

// --- the stimulus ----------------------------------------------------------

static u32 seg_add(struct s3d_meas *m, u32 start, u32 len, u16 hz0, double l0,
                   double r0, u16 hz1, double l1, double r1, int ramp) {
  struct s3d_seg *s;

  if (m->nseg >= S3D_MAX_SEGS)
    return start;
  s = &m->seg[m->nseg++];
  memset(s, 0, sizeof(*s));
  s->start = start;
  s->len = len;
  s->t[0].hz = hz0;
  s->t[0].l = (float)l0;
  s->t[0].r = (float)r0;
  s->t[1].hz = hz1;
  s->t[1].l = (float)l1;
  s->t[1].r = (float)r1;
  s->ramp = (u8)ramp;
  return start + len;
}

static void win_add(struct s3d_meas *m, u32 pstart, u16 ms) {
  struct s3d_win *w;

  if (m->nwin >= S3D_MAX_WINS)
    return;
  w = &m->win[m->nwin++];
  memset(w, 0, sizeof(*w));
  w->pstart = pstart;
  w->ms = ms;
  w->seg = (u8)(m->nseg - 1);
}

// the windows after a step: 20 ms over the first 500 ms, then n of 100 ms
static void step_windows(struct s3d_meas *m, u32 s, int n) {
  int k;

  for (k = 0; k < S3D_FINE; k++)
    win_add(m, s + fr(20 * k), 20);
  for (k = 0; k < n; k++)
    win_add(m, s + fr(500 + 100 * k), 100);
}

int s3d_seconds(const struct s3d_meas *m) {
  long ms = 0;
  int i, tone;

  // each run's stimulus, and about a second to start the devices and
  // write the registers
  for (i = 0; i < m->nruns; i++) {
    tone = m->run[i].slow ? 410 : 160;
    switch (m->run[i].kind) {
    case S3D_RATIO:
      ms += 5 * 620;
      break;
    case S3D_BAND:
      ms += S3D_FREQS * tone;
      break;
    case S3D_PAN:
      ms += S3D_PANS * tone;
      break;
    case S3D_STEP:
      ms += STEP_LOW + STEP_HIGH + STEP_DOWN;
      break;
    default:
      ms += 2 * S3D_FREQS * tone;
    }
    ms += 450 + 1000;
  }
  return (int)(ms / 1000);
}

u32 s3d_begin(struct s3d_meas *m, int i, u32 *rec_frames) {
  struct s3d_run *r = &m->run[i];
  u32 t = 0, s;
  int k, side, settle = r->slow ? 300 : 50;
  double a, th;

  m->cur = i;
  m->nseg = m->nwin = 0;
  m->play_pos = 0;
  m->rpos = 0;
  m->rstate = R_NOISE;
  m->nsum[0] = m->nsum[1] = m->nsq[0] = m->nsq[1] = 0;
  m->bl[0] = m->bl[1] = m->bq[0] = m->bq[1] = 0;
  m->qsum = 0;
  m->qn = 0;
  m->nn = 0;
  m->wcur = 0;
  m->wn = 0;
  m->peak = 0;
  m->confirm = 0;

  // silence, the burst and a gap before the tones
  t = seg_add(m, t, fr(250), 0, 0, 0, 0, 0, 0, 0);
  m->psync = t;
  t = seg_add(m, t, fr(40), 1000, AMP_SYNC, AMP_SYNC, 0, 0, 0, 3);
  t = seg_add(m, t, fr(60), 0, 0, 0, 0, 0, 0, 0);
  switch (r->kind) {
  case S3D_RATIO:
    for (k = 0; k < 5; k++) {
      a = AMP_M * pow(10.0, ratio_db[k] / 20.0);
      s = t;
      t = seg_add(m, t, fr(500 + 100 + 20), 400, AMP_M, AMP_M, 1000, a, -a, 3);
      win_add(m, s + fr(500), 100);
    }
    break;
  case S3D_BAND:
    for (k = 0; k < S3D_FREQS; k++) {
      s = t;
      t = seg_add(m, t, fr(settle + 110), 400, AMP_M, AMP_M, freq[k], AMP_M,
                  -AMP_M, 3);
      win_add(m, s + fr(settle), 100);
    }
    break;
  case S3D_PAN:
    for (k = 0; k < S3D_PANS; k++) {
      th = PI / 8 * k; // 22.5 degrees a step
      s = t;
      t = seg_add(m, t, fr(settle + 110), 1000, AMP * cos(th), AMP * sin(th), 0,
                  0, 0, 3);
      win_add(m, s + fr(settle), 100);
    }
    break;
  case S3D_STEP:
    // the S tone's level steps where both tones are back at the start of
    // a cycle, so neither jumps in phase
    a = AMP_M * pow(10.0, -12 / 20.0);
    s = t;
    t = seg_add(m, t, fr(STEP_LOW), 400, AMP_M, AMP_M, 1000, a, -a, 1);
    win_add(m, s + fr(STEP_LOW - 120), 100);
    s = t;
    t = seg_add(m, t, fr(STEP_HIGH), 400, AMP_M, AMP_M, 1000, AMP_M, -AMP_M, 0);
    step_windows(m, s, S3D_COARSE_UP);
    win_add(m, s + fr(STEP_HIGH - 120), 100);
    s = t;
    t = seg_add(m, t, fr(STEP_DOWN), 400, AMP_M, AMP_M, 1000, a, -a, 2);
    step_windows(m, s, S3D_COARSE_DN);
    win_add(m, s + fr(STEP_DOWN - 120), 100);
    break;
  default:
    for (side = 1; side >= -1; side -= 2)
      for (k = 0; k < S3D_FREQS; k++) {
        s = t;
        t = seg_add(m, t, fr(settle + 110), freq[k], AMP, side * AMP, 0, 0, 0,
                    3);
        win_add(m, s + fr(settle), 100);
      }
  }
  t = seg_add(m, t, fr(100), 0, 0, 0, 0, 0, 0, 0);
  m->play_len = t;
  // the devices may start up to a second apart
  m->rmax = (u32)((double)t * m->rate_rec / m->rate_play) + m->rate_rec * 3 / 2;
  *rec_frames = m->rmax;
  return t;
}

// the fade of a segment's tones k frames into it
static double fade(const struct s3d_seg *s, u32 k) {
  u32 n = fr(FADE);

  if ((s->ramp & 1) && k < n)
    return 0.5 - 0.5 * cos(PI * k / n);
  if ((s->ramp & 2) && s->len - k < n)
    return 0.5 - 0.5 * cos(PI * (s->len - k) / n);
  return 1.0;
}

static const struct s3d_seg *seg_at(const struct s3d_meas *m, u32 pos) {
  int i;

  for (i = 0; i < m->nseg; i++)
    if (pos >= m->seg[i].start && pos - m->seg[i].start < m->seg[i].len)
      return &m->seg[i];
  return 0;
}

void s3d_signal(const struct s3d_meas *m, double pos, double *l, double *r) {
  const struct s3d_seg *s;
  double k, v, f;
  int j;

  *l = *r = 0;
  if (pos < 0)
    return;
  s = seg_at(m, (u32)pos);
  if (!s)
    return;
  k = pos - s->start;
  f = fade(s, (u32)k);
  for (j = 0; j < 2; j++)
    if (s->t[j].hz) {
      v = f * sin(2 * PI * s->t[j].hz * k / S3D_RATE);
      *l += s->t[j].l * v;
      *r += s->t[j].r * v;
    }
}

static s16 to_s16(double v) {
  long x = (long)floor(v * 32767.0 + 0.5);

  if (x > 32767)
    x = 32767;
  if (x < -32768)
    x = -32768;
  return (s16)x;
}

void s3d_fill(struct s3d_meas *m, s16 *pcm, u16 n) {
  const struct s3d_seg *s = 0;
  double pc[2], ps[2], wc[2], ws[2], t, l, r, f;
  u32 k = 0;
  u16 i;
  int j;

  for (i = 0; i < n; i++, m->play_pos++) {
    if (!s || m->play_pos - s->start >= s->len) {
      // a new segment, or the first frame of this call: the tones'
      // phases from the segment's start
      s = seg_at(m, m->play_pos);
      if (s) {
        k = m->play_pos - s->start;
        for (j = 0; j < 2; j++) {
          t = 2 * PI * s->t[j].hz / S3D_RATE;
          wc[j] = cos(t);
          ws[j] = sin(t);
          pc[j] = cos(t * k);
          ps[j] = sin(t * k);
        }
      }
    }
    l = r = 0;
    if (s) {
      k = m->play_pos - s->start;
      f = fade(s, k);
      for (j = 0; j < 2; j++) {
        if (s->t[j].hz) {
          l += s->t[j].l * f * ps[j];
          r += s->t[j].r * f * ps[j];
        }
        // the next sample's phase
        t = pc[j] * wc[j] - ps[j] * ws[j];
        ps[j] = ps[j] * wc[j] + pc[j] * ws[j];
        pc[j] = t;
        if ((k & 1023) == 1023) {
          t = 1.0 / sqrt(pc[j] * pc[j] + ps[j] * ps[j]);
          pc[j] *= t;
          ps[j] *= t;
        }
      }
    }
    pcm[2 * i] = to_s16(l);
    pcm[2 * i + 1] = to_s16(r);
  }
}

// --- the analysis ----------------------------------------------------------

// the recorded frames of each window, once the onset is known, and the
// phase the stimulus has where the window starts: its place in the
// segment, and the part of a frame lost in rounding the start
static void place_windows(struct s3d_meas *m) {
  double q = (double)m->rate_rec / m->rate_play, x, rho, hz;
  const struct s3d_seg *s;
  struct s3d_win *w;
  int i, j;

  for (i = 0; i < m->nwin; i++) {
    w = &m->win[i];
    s = &m->seg[w->seg];
    x = (w->pstart - m->psync) * q;
    w->rstart = m->ron + (u32)floor(x + 0.5);
    rho = floor(x + 0.5) - x;
    w->rlen = (u16)((u32)w->ms * m->rate_rec / 1000);
    for (j = 0; j < 2; j++) {
      hz = s->t[j].hz * ((double)m->rate_play / S3D_RATE);
      w->psi[j] =
          (float)fmod(2 * PI * s->t[j].hz * (w->pstart - s->start) / S3D_RATE +
                          2 * PI * hz * rho / m->rate_rec,
                      2 * PI);
    }
  }
}

static void window_start(struct s3d_meas *m) {
  const struct s3d_seg *s = &m->seg[m->win[m->wcur].seg];
  double t;
  int j;

  memset(m->acc, 0, sizeof(m->acc));
  for (j = 0; j < 2; j++) {
    // the tone's frequency in the recording
    t = 2 * PI * s->t[j].hz * ((double)m->rate_play / S3D_RATE) / m->rate_rec;
    m->wc[j] = cos(t);
    m->ws[j] = sin(t);
    m->pc[j] = 1;
    m->ps[j] = 0;
  }
}

// A sin(wn + p) gives sums of A n/2 cos(p) over sin(wn) and A n/2 sin(p)
// over cos(wn): A e^(jp), turned back by the stimulus's own phase
static void window_end(struct s3d_meas *m) {
  struct s3d_win *w = &m->win[m->wcur];
  double g = 2.0 / w->rlen, a, b, c, s;
  int j, ch;

  for (j = 0; j < 2; j++) {
    c = cos(w->psi[j]);
    s = sin(w->psi[j]);
    for (ch = 0; ch < 2; ch++) {
      a = m->acc[j][2 * ch + 1] * g;
      b = m->acc[j][2 * ch] * g;
      w->zr[j][ch] = (float)(a * c + b * s);
      w->zi[j][ch] = (float)(b * c - a * s);
    }
  }
}

int s3d_take(struct s3d_meas *m, const s16 *pcm, u16 n) {
  const struct s3d_seg *s;
  u32 blk = m->rate_rec / 1000;
  double l, r, e, t, thr;
  u16 i, a;
  int j;

  for (i = 0; i < n; i++, m->rpos++) {
    l = pcm[2 * i] / 32768.0;
    r = pcm[2 * i + 1] / 32768.0;
    a = (u16)(pcm[2 * i] < 0 ? -(long)pcm[2 * i] : pcm[2 * i]);
    if (a > m->peak)
      m->peak = a;
    a = (u16)(pcm[2 * i + 1] < 0 ? -(long)pcm[2 * i + 1] : pcm[2 * i + 1]);
    if (a > m->peak)
      m->peak = a;
    switch (m->rstate) {
    case R_NOISE:
      // the run starts with 250 ms of silence, the first 50 are the floor
      m->nsum[0] += l;
      m->nsum[1] += r;
      m->nsq[0] += l * l;
      m->nsq[1] += r * r;
      if (++m->nn >= blk * 50) {
        m->e0 = 0;
        for (j = 0; j < 2; j++) {
          t = m->nsum[j] / m->nn;
          m->e0 += m->nsq[j] / m->nn - t * t;
        }
        m->rstate = R_ONSET;
        m->bn = 0;
        m->bstart = m->rpos + 1;
      }
      break;
    case R_ONSET:
      // the burst: a millisecond well over the floor, and nine more after
      // it, where a click from a DAC starting would die away
      m->bl[0] += l;
      m->bl[1] += r;
      m->bq[0] += l * l;
      m->bq[1] += r * r;
      if (++m->bn < blk)
        break;
      e = 0;
      for (j = 0; j < 2; j++) {
        t = m->bl[j] / m->bn;
        e += m->bq[j] / m->bn - t * t;
        m->bl[j] = m->bq[j] = 0;
      }
      thr = 100 * m->e0;
      if (thr < 1e-5)
        thr = 1e-5;
      if (!m->confirm) {
        if (e > thr) {
          m->cand = m->bstart;
          m->confirm = 1;
        } else {
          m->qsum += e;
          m->qn++;
        }
      } else if (e > thr / 4) {
        if (++m->confirm >= 10) {
          // the floor from the quieter of the first 50 ms and the rest of
          // the silence, which a recording of digital zeros can't fake
          e = m->qn && m->qsum / m->qn > m->e0 ? m->qsum / m->qn : m->e0;
          m->sigma = sqrt(e / 2);
          if (m->sigma < 0.29 / 32768)
            m->sigma = 0.29 / 32768; // 16-bit samples have at least this
          m->ron = m->cand;
          place_windows(m);
          m->rstate = R_ANALYZE;
        }
      } else {
        m->confirm = 0;
      }
      m->bn = 0;
      m->bstart = m->rpos + 1;
      if (m->rpos > m->rate_rec * 5)
        m->rstate = R_FAIL;
      break;
    case R_ANALYZE:
      if (m->rpos < m->win[m->wcur].rstart)
        break;
      if (!m->wn)
        window_start(m);
      s = &m->seg[m->win[m->wcur].seg];
      for (j = 0; j < 2; j++) {
        if (!s->t[j].hz)
          continue;
        m->acc[j][0] += l * m->pc[j];
        m->acc[j][1] += l * m->ps[j];
        m->acc[j][2] += r * m->pc[j];
        m->acc[j][3] += r * m->ps[j];
        t = m->pc[j] * m->wc[j] - m->ps[j] * m->ws[j];
        m->ps[j] = m->ps[j] * m->wc[j] + m->pc[j] * m->ws[j];
        m->pc[j] = t;
        if ((m->wn & 1023) == 1023) {
          t = 1.0 / sqrt(m->pc[j] * m->pc[j] + m->ps[j] * m->ps[j]);
          m->pc[j] *= t;
          m->ps[j] *= t;
        }
      }
      if (++m->wn >= m->win[m->wcur].rlen) {
        window_end(m);
        m->wn = 0;
        if (++m->wcur >= m->nwin)
          m->rstate = R_DONE;
      }
      break;
    }
  }
  return m->rstate == R_DONE || m->rstate == R_FAIL;
}

// tone j of window w in channel ch, and its M, (L + R) / 2, and S,
// (L - R) / 2
static struct cx z_ch(const struct s3d_win *w, int j, int ch) {
  struct cx z;

  z.re = w->zr[j][ch];
  z.im = w->zi[j][ch];
  return z;
}

static struct cx z_mid(const struct s3d_win *w, int j) {
  struct cx z;

  z.re = (w->zr[j][0] + w->zr[j][1]) / 2;
  z.im = (w->zi[j][0] + w->zi[j][1]) / 2;
  return z;
}

static struct cx z_side(const struct s3d_win *w, int j) {
  struct cx z;

  z.re = (w->zr[j][0] - w->zr[j][1]) / 2;
  z.im = (w->zi[j][0] - w->zi[j][1]) / 2;
  return z;
}

static double z_abs(struct cx z) { return sqrt(z.re * z.re + z.im * z.im); }

static double z_deg(struct cx z) { return atan2(z.im, z.re) * 180 / PI; }

// --- the boost ---------------------------------------------------------------

static int ok(const struct s3d_run *r) {
  return r->result != 0xFF && !(r->result & (S3D_NOSIGNAL | S3D_SHORT));
}

// the boost at 1 kHz with the limit off, B in S>S = 1 + B, from the sweep
// run with the same registers but 50h bit 0, or else one with the same
// 50h and 52h, since on the card 54h-5Ah don't change it with the limit
// off; 0 if there's none
static int boost_ref(const struct s3d_meas *m, const struct s3d_run *r,
                     float *re, float *im) {
  const struct s3d_run *s;
  u8 r50 = (u8)(r->reg[S3D_R50] & ~1);
  double g, ph;
  int i, best = -1;

  *re = *im = 0;
  if ((r50 & 0x0C) != 0x0C)
    return 0;
  for (i = 0; i < m->nruns; i++) {
    s = &m->run[i];
    if (s->kind != S3D_SWEEP || !ok(s) || s->reg[S3D_R50] != r50 ||
        s->reg[S3D_R52] != r->reg[S3D_R52] || !has(s->u.sw.db[1][F1K]) ||
        !has(s->u.sw.ph[0][F1K]))
      continue;
    if (!memcmp(s->reg + S3D_R54, r->reg + S3D_R54, S3D_REGS - S3D_R54)) {
      best = i;
      break;
    }
    if (best < 0)
      best = i;
  }
  if (best < 0)
    return 0;
  s = &m->run[best];
  g = pow(10.0, s->u.sw.db[1][F1K] / 20);
  ph = s->u.sw.ph[0][F1K] * PI / 180;
  *re = (float)(g * cos(ph) - 1);
  *im = (float)(g * sin(ph));
  return 1;
}

// the boost's own gain in dB, a in |1 + a B| = S>S with a at least 0,
// from S>S in dB; -60 for a boost that is off
static float boost_db(double ss, double bre, double bim) {
  double b2 = bre * bre + bim * bim, g, d, a;

  if (!has(ss) || b2 < 1e-6)
    return S3D_FLOOR;
  g = pow(10.0, ss / 20);
  d = bre * bre - b2 * (1 - g * g);
  a = d > 0 ? (-bre + sqrt(d)) / b2 : 0;
  return (float)(a > 1e-3 ? db(a) : -60.0);
}

// n values of S>S as the boost's gain, into out
static void to_boost(const float *ss, int n, double bre, double bim,
                     float *out) {
  int k;

  for (k = 0; k < n; k++)
    out[k] = boost_db(ss[k], bre, bim);
}

// --- the report -------------------------------------------------------------

static void say(struct s3d_meas *m, const char *line) {
  if (m->out)
    m->out(m->ctx, line);
}

// "  4  54=0F b7        0C 3F 0F 95 94 80  " for run i, or blanks
static void run_head(const struct s3d_meas *m, int i, char *out) {
  const struct s3d_run *r = &m->run[i];

  if (i < 0) {
    sprintf(out, "%39s", "");
    return;
  }
  sprintf(out, "%3d  %-13s  %02X %02X %02X %02X %02X %02X  ", i, r->name,
          r->reg[0], r->reg[1], r->reg[2], r->reg[3], r->reg[4], r->reg[5]);
}

// dB in six columns, without -0.0
static void cell(char *out, double v) {
  if (!has(v)) {
    strcat(out, "     .");
    return;
  }
  if (v < -99.9)
    v = -99.9;
  sprintf(out + strlen(out), "%+6.1f", tidy(v));
}

// degrees in six columns
static void cell_deg(char *out, double v) {
  if (!has(v))
    strcat(out, "     .");
  else
    sprintf(out + strlen(out), "%+6.0f", fabs(v) < 0.5 ? 0.0 : v);
}

// the boost's gain in six columns, off under OFF_DB
static void cell_boost(char *out, double v) {
  if (has(v) && v < OFF_DB)
    strcat(out, "   off");
  else
    cell(out, v);
}

static void table_head(struct s3d_meas *m, int kind) {
  char line[256];
  int k;

  say(m, "");
  strcpy(line, "run  name           50 52 54 56 58 5A  ");
  switch (kind) {
  case S3D_RATIO:
    say(m, "ratio runs: 400 Hz in M at -24 dBFS, and 1 kHz in S at a ratio "
           "to it");
    strcat(line, "S/M dB  ");
    for (k = 0; k < 5; k++)
      sprintf(line + strlen(line), "%+6d", ratio_db[k]);
    break;
  case S3D_BAND:
    say(m, "band runs: S at each frequency over 400 Hz in M, both at -24 "
           "dBFS; S>S, and the M>M of the 400 Hz tone");
    strcat(line, "path    ");
    for (k = 0; k < S3D_FREQS; k++)
      sprintf(line + strlen(line), "%6s", freq_name[k]);
    break;
  case S3D_PAN:
    say(m, "pan runs: 1 kHz at -18 dBFS panned from the left (0) to the "
           "right (90 degrees): where it comes out, under 0 or over 90 past a "
           "speaker, and its level");
    strcat(line, "pan in  ");
    for (k = 0; k < S3D_PANS; k++)
      sprintf(line + strlen(line), "%6.1f", 22.5 * k);
    break;
  case S3D_STEP:
    say(m, "step runs: 1 kHz in S steps from -12 to 0 dB re 400 Hz in M at 0 "
           "ms, and back after 2 s; the boost's own gain in dB (0 is as with "
           "the limit off), or S>S where the boost isn't known");
    say(m, "in 20 ms windows to 500 ms, then in 100 ms windows to 1.8 s after "
           "the step up and to 3 s after the step down");
    strcat(line, "ms      ");
    for (k = 0; k < S3D_FINE; k++)
      sprintf(line + strlen(line), "%6d", 20 * k);
    say(m, line);
    sprintf(line, "%39s%-8s", "", "ms");
    for (k = 0; k < S3D_COARSE_DN; k++)
      sprintf(line + strlen(line), "%6d", 500 + 100 * k);
    break;
  default:
    strcat(line, "path    ");
    for (k = 0; k < S3D_FREQS; k++)
      sprintf(line + strlen(line), "%6s", freq_name[k]);
  }
  say(m, line);
}

void s3d_header(struct s3d_meas *m, u32 rate_play, u32 rate_rec,
                const char *info) {
  char line[256];
  int s = s3d_seconds(m);

  m->rate_play = rate_play;
  m->rate_rec = rate_rec;
  say(m, "ess3d measure: the 3-D effect, recorded from its own output "
         "(record source 7)");
  if (info && info[0])
    say(m, info);
  sprintf(line, "plan: %s, %d runs, about %d min %02d s", plan_name[m->plan],
          m->nruns, s / 60, s % 60);
  say(m, line);
  sprintf(line, "play %lu Hz on Audio 2, record %lu Hz on Audio 1",
          (unsigned long)rate_play, (unsigned long)rate_rec);
  say(m, line);
  say(m, "gains are in dB relative to run 0, which has the effect off");
  say(m, "M: a tone the same in both channels, S: a tone in opposite phase "
         "in the two");
  say(m, "M>S is the S out of an M tone relative to run 0's M>M, S>M the M "
         "out of an S tone relative to run 0's S>S");
  say(m, "S>S deg is the phase the effect adds to S against M; M>S deg and "
         "S>M deg are against the direct path of the same tone");
  say(m, ". is under the noise floor; M>S and S>M are left out where every "
         "value is");
  table_head(m, S3D_SWEEP);
}

// a row of n values: dB, degrees (ROW_DEG) or the boost (ROW_BOOST)
#define ROW_DB 0
#define ROW_DEG 1
#define ROW_BOOST 2

static void row(struct s3d_meas *m, int i, const char *label, const float *v,
                int n, int how) {
  char line[256];
  int k;

  run_head(m, i, line);
  sprintf(line + strlen(line), "%-8s", label);
  for (k = 0; k < n; k++)
    if (how == ROW_DEG)
      cell_deg(line, v[k]);
    else if (how == ROW_BOOST)
      cell_boost(line, v[k]);
    else
      cell(line, v[k]);
  say(m, line);
}

static void sweep_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];
  int p, k, any, turned;
  static const char *const deg_name[3] = {"S>S deg", "M>S deg", "S>M deg"};

  for (p = 0; p < S3D_PATHS; p++) {
    any = turned = 0;
    for (k = 0; k < S3D_FREQS; k++) {
      any |= has(r->u.sw.db[p][k]);
      if (p >= 1)
        turned |= has(r->u.sw.ph[p - 1][k]) && fabs(r->u.sw.ph[p - 1][k]) >= 1;
    }
    if (p >= 2 && !any)
      continue;
    row(m, p ? -1 : i, path_name[p], r->u.sw.db[p], S3D_FREQS, ROW_DB);
    // the phase of S in every run but the reference, where it's the
    // zero, and of each cross term that is there
    if ((p == 1 && i && turned) || p >= 2)
      row(m, -1, deg_name[p - 1], r->u.sw.ph[p - 1], S3D_FREQS, ROW_DEG);
  }
}

static void band_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];

  row(m, i, "S>S", r->u.sw.db[1], S3D_FREQS, ROW_DB);
  row(m, -1, "M>M 400", r->u.sw.db[0], S3D_FREQS, ROW_DB);
}

static void pan_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];

  row(m, i, "out deg", r->u.pa.deg, S3D_PANS, ROW_DEG);
  row(m, -1, "level", r->u.pa.lev, S3D_PANS, ROW_DB);
}

// the boost's gain where B is known, or else S>S
static void step_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];
  float up[S3D_UP], dn[S3D_DN], st[3];
  int b = r->u.st.b_re != 0 || r->u.st.b_im != 0;
  int how = b ? ROW_BOOST : ROW_DB;
  char line[256];

  memcpy(up, r->u.st.up, sizeof(up));
  memcpy(dn, r->u.st.dn, sizeof(dn));
  memcpy(st, r->u.st.steady, sizeof(st));
  if (b) {
    to_boost(r->u.st.up, S3D_UP, r->u.st.b_re, r->u.st.b_im, up);
    to_boost(r->u.st.dn, S3D_DN, r->u.st.b_re, r->u.st.b_im, dn);
    to_boost(r->u.st.steady, 3, r->u.st.b_re, r->u.st.b_im, st);
  }
  row(m, i, b ? "boost up" : "S>S up", up, S3D_FINE, how);
  row(m, -1, "", up + S3D_FINE, S3D_COARSE_UP, how);
  row(m, -1, b ? "boost dn" : "S>S dn", dn, S3D_FINE, how);
  row(m, -1, "", dn + S3D_FINE, S3D_COARSE_DN, how);
  run_head(m, -1, line);
  sprintf(line + strlen(line), "steady: low %+.1f, high %+.1f, low again %+.1f",
          tidy(st[0]), tidy(st[1]), tidy(st[2]));
  if (b)
    sprintf(line + strlen(line), "; S>S at the high level %+.1f",
            tidy(r->u.st.steady[1]));
  say(m, line);
}

static void ratio_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];
  float b[5];

  row(m, i, "S>S 1k", r->u.ra.ss, 5, ROW_DB);
  row(m, -1, "M>M 400", r->u.ra.mm, 5, ROW_DB);
  if (r->u.ra.b_re != 0 || r->u.ra.b_im != 0) {
    to_boost(r->u.ra.ss, 5, r->u.ra.b_re, r->u.ra.b_im, b);
    row(m, -1, "boost", b, 5, ROW_BOOST);
  }
}

// noise of the M or S of one window of n frames, against an amplitude
static double floor_db(const struct s3d_meas *m, u16 n, double amp) {
  // (L + R) / 2 has half the noise power of one channel, and the
  // amplitude over n frames picks up 2 / sqrt(n) of it
  return db(m->sigma * sqrt(0.5) * 2.0 / sqrt((double)n) / amp);
}

// a gain against run 0's, or S3D_FLOOR under the floor: 10 dB over it, so
// noise passes for a value about once in 20,000 values
static float rel(double g, double ref, double fl) {
  double v = db(g / ref);

  return v < fl + 10 ? S3D_FLOOR : (float)v;
}

static int sweep_end(struct s3d_meas *m, struct s3d_run *r) {
  const struct s3d_win *wm, *ws;
  struct cx mo_m, so_m, mo_s, so_s;
  double fl, turn;
  int k;

  for (k = 0; k < S3D_FREQS; k++) {
    wm = &m->win[k];
    ws = &m->win[S3D_FREQS + k];
    mo_m = z_mid(wm, 0);
    so_m = z_side(wm, 0);
    mo_s = z_mid(ws, 0);
    so_s = z_side(ws, 0);
    // the S of the S tone against the M of the M tone: the onset's error
    // is the same for both, and drops out
    turn = wrap(z_deg(so_s) - z_deg(mo_m));
    if (m->cur == 0) {
      // the reference: no signal through the chip is no measurement
      if (z_abs(mo_m) / AMP < 1e-3 || z_abs(so_s) / AMP < 1e-3)
        return S3D_NOSIGNAL;
      m->ref_mm[k] = (float)(z_abs(mo_m) / AMP);
      m->ref_ss[k] = (float)(z_abs(so_s) / AMP);
      m->ref_ph[k] = (float)turn;
    }
    fl = floor_db(m, wm->rlen, AMP * m->ref_mm[k]);
    r->u.sw.db[0][k] = rel(z_abs(mo_m) / AMP, m->ref_mm[k], fl);
    r->u.sw.db[2][k] = rel(z_abs(so_m) / AMP, m->ref_mm[k], fl);
    fl = floor_db(m, ws->rlen, AMP * m->ref_ss[k]);
    r->u.sw.db[1][k] = rel(z_abs(so_s) / AMP, m->ref_ss[k], fl);
    r->u.sw.db[3][k] = rel(z_abs(mo_s) / AMP, m->ref_ss[k], fl);
    r->u.sw.ph[0][k] = has(r->u.sw.db[1][k]) && has(r->u.sw.db[0][k])
                           ? (float)wrap(turn - m->ref_ph[k])
                           : S3D_FLOOR;
    r->u.sw.ph[1][k] = has(r->u.sw.db[2][k]) && has(r->u.sw.db[0][k])
                           ? (float)wrap(z_deg(so_m) - z_deg(mo_m))
                           : S3D_FLOOR;
    r->u.sw.ph[2][k] = has(r->u.sw.db[3][k]) && has(r->u.sw.db[1][k])
                           ? (float)wrap(z_deg(mo_s) - z_deg(so_s))
                           : S3D_FLOOR;
  }
  return S3D_OK;
}

static void band_end(struct s3d_meas *m, struct s3d_run *r) {
  const struct s3d_win *w;
  double fl;
  int k, p;

  for (k = 0; k < S3D_FREQS; k++) {
    w = &m->win[k];
    fl = floor_db(m, w->rlen, AMP_M * m->ref_ss[k]);
    r->u.sw.db[1][k] = rel(z_abs(z_side(w, 1)) / AMP_M, m->ref_ss[k], fl);
    fl = floor_db(m, w->rlen, AMP_M * m->ref_mm[F400]);
    r->u.sw.db[0][k] = rel(z_abs(z_mid(w, 0)) / AMP_M, m->ref_mm[F400], fl);
    r->u.sw.db[2][k] = r->u.sw.db[3][k] = S3D_FLOOR;
    for (p = 0; p < 3; p++)
      r->u.sw.ph[p][k] = S3D_FLOOR;
  }
}

// where a panned tone comes out: the in-phase parts of both channels
// against the stronger one, so a channel in opposite phase puts it past
// the speaker on the other side
static void pan_end(struct s3d_meas *m, struct s3d_run *r) {
  const struct s3d_win *w;
  struct cx zl, zr, u;
  double al, ar, xl, xr, g = (m->ref_mm[F1K] + m->ref_ss[F1K]) / 2;
  int k;

  for (k = 0; k < S3D_PANS; k++) {
    w = &m->win[k];
    zl = z_ch(w, 0, 0);
    zr = z_ch(w, 0, 1);
    al = z_abs(zl);
    ar = z_abs(zr);
    u = al >= ar ? zl : zr;
    if (al < ar)
      al = ar;
    u.re /= al > 0 ? al : 1;
    u.im /= al > 0 ? al : 1;
    xl = zl.re * u.re + zl.im * u.im;
    xr = zr.re * u.re + zr.im * u.im;
    r->u.pa.lev[k] =
        rel(sqrt(z_abs(zl) * z_abs(zl) + z_abs(zr) * z_abs(zr)) / AMP, g,
            floor_db(m, w->rlen, AMP * g));
    r->u.pa.deg[k] =
        has(r->u.pa.lev[k]) ? (float)(atan2(xr, xl) * 180 / PI) : S3D_FLOOR;
  }
}

// S>S at 1 kHz in window w, where the S tone has amplitude a
static float step_db(const struct s3d_meas *m, int w, double a) {
  double fl = floor_db(m, m->win[w].rlen, a * m->ref_ss[F1K]);

  return rel(z_abs(z_side(&m->win[w], 1)) / a, m->ref_ss[F1K], fl);
}

static void step_end(struct s3d_meas *m, struct s3d_run *r) {
  double a = AMP_M * pow(10.0, -12 / 20.0);
  int k;

  for (k = 0; k < S3D_UP; k++)
    r->u.st.up[k] = step_db(m, W_UP + k, AMP_M);
  for (k = 0; k < S3D_DN; k++)
    r->u.st.dn[k] = step_db(m, W_DOWN + k, a);
  r->u.st.steady[0] = step_db(m, W_LOW, a);
  r->u.st.steady[1] = step_db(m, W_HIGH, AMP_M);
  r->u.st.steady[2] = step_db(m, W_END, a);
  boost_ref(m, r, &r->u.st.b_re, &r->u.st.b_im);
}

int s3d_end(struct s3d_meas *m) {
  struct s3d_run *r = &m->run[m->cur];
  double a, fl;
  int k, res = S3D_OK;

  if (m->rstate == R_DONE) {
    switch (r->kind) {
    case S3D_RATIO:
      for (k = 0; k < 5; k++) {
        a = AMP_M * pow(10.0, ratio_db[k] / 20.0);
        fl = floor_db(m, m->win[k].rlen, a * m->ref_ss[F1K]);
        r->u.ra.ss[k] =
            rel(z_abs(z_side(&m->win[k], 1)) / a, m->ref_ss[F1K], fl);
        fl = floor_db(m, m->win[k].rlen, AMP_M * m->ref_mm[F400]);
        r->u.ra.mm[k] =
            rel(z_abs(z_mid(&m->win[k], 0)) / AMP_M, m->ref_mm[F400], fl);
      }
      boost_ref(m, r, &r->u.ra.b_re, &r->u.ra.b_im);
      break;
    case S3D_BAND:
      band_end(m, r);
      break;
    case S3D_PAN:
      pan_end(m, r);
      break;
    case S3D_STEP:
      step_end(m, r);
      break;
    default:
      res = sweep_end(m, r);
    }
  } else {
    res = m->rstate == R_ANALYZE ? S3D_SHORT : S3D_NOSIGNAL;
  }
  if (res == S3D_OK && m->peak >= 32700)
    res |= S3D_CLIPPED;
  r->result = (u8)res;
  if (res & (S3D_NOSIGNAL | S3D_SHORT))
    return res;

  // the report: a new table where the kind of run changes
  if (r->kind != S3D_SWEEP &&
      (m->cur == 0 || m->run[m->cur - 1].kind != r->kind))
    table_head(m, r->kind);
  switch (r->kind) {
  case S3D_RATIO:
    ratio_lines(m, m->cur);
    break;
  case S3D_BAND:
    band_lines(m, m->cur);
    break;
  case S3D_PAN:
    pan_lines(m, m->cur);
    break;
  case S3D_STEP:
    step_lines(m, m->cur);
    break;
  default:
    sweep_lines(m, m->cur);
  }
  if (res & S3D_CLIPPED)
    say(m, "     (a recorded sample reached full scale: the values may be "
           "too low)");
  return res;
}

void s3d_failed(struct s3d_meas *m, const char *why) {
  char line[256];

  run_head(m, m->cur, line);
  strcat(line, "failed: ");
  strncat(line, why, sizeof(line) - strlen(line) - 1);
  say(m, line);
}

// --- the summary ------------------------------------------------------------

// a value of path p that the summary counts: over the floor, and for a
// cross path over CROSS_MIN, where it isn't the imbalance of the DAC and
// the ADC that the boost raises
static int counts(double v, int p) {
  return has(v) && (p < 2 || v > CROSS_MIN);
}

// the largest change of sweep or band run a from run b where both have a
// value, on the paths in mask (bit p for path p), its path and frequency;
// 0 if there's none
static int biggest(const struct s3d_run *a, const struct s3d_run *b, int mask,
                   double *change, int *path, int *f) {
  int p, k, found = 0;
  double d;

  for (p = 0; p < S3D_PATHS; p++)
    for (k = 0; k < S3D_FREQS; k++) {
      if (!(mask & (1 << p)) || !counts(a->u.sw.db[p][k], p) ||
          !counts(b->u.sw.db[p][k], p))
        continue;
      d = a->u.sw.db[p][k] - b->u.sw.db[p][k];
      if (!found || fabs(d) > fabs(*change)) {
        *change = d;
        *path = p;
        *f = k;
        found = 1;
      }
    }
  return found;
}

// the largest change of phase q (0 S>S, 1 M>S, 2 S>M) of run a from run
// b, and its frequency; 0 if there's none
static int biggest_turn(const struct s3d_run *a, const struct s3d_run *b, int q,
                        double *turn, int *f) {
  int k, found = 0;
  double d;

  for (k = 0; k < S3D_FREQS; k++) {
    if (!has(a->u.sw.ph[q][k]) || !has(b->u.sw.ph[q][k]) ||
        !counts(a->u.sw.db[q + 1][k], q + 1) ||
        !counts(b->u.sw.db[q + 1][k], q + 1))
      continue;
    d = wrap(a->u.sw.ph[q][k] - b->u.sw.ph[q][k]);
    if (!found || fabs(d) > fabs(*turn)) {
      *turn = d;
      *f = k;
      found = 1;
    }
  }
  return found;
}

// a path that comes up from the floor in run a (appears), or goes under
// it: ", M>S appears, up to +3.5" or nothing
static void new_path(const struct s3d_run *a, const struct s3d_run *b,
                     char *out) {
  int p, k, up, down;
  double most;

  for (p = 0; p < S3D_PATHS; p++) {
    up = down = 0;
    most = S3D_FLOOR;
    for (k = 0; k < S3D_FREQS; k++) {
      if (counts(a->u.sw.db[p][k], p) && !counts(b->u.sw.db[p][k], p)) {
        up = 1;
        if (a->u.sw.db[p][k] > most)
          most = a->u.sw.db[p][k];
      }
      down |= !counts(a->u.sw.db[p][k], p) && counts(b->u.sw.db[p][k], p);
    }
    if (up)
      sprintf(out + strlen(out), ", %s appears, up to %+.1f", path_name[p],
              tidy(most));
    else if (down)
      sprintf(out + strlen(out), ", %s goes under %s", path_name[p],
              p < 2 ? "the floor" : "-15 dB");
  }
}

// the sweep and band runs against the run each varies
static void summary_runs(struct s3d_meas *m) {
  char line[256];
  double change, turn;
  int i, p, k;
  const struct s3d_run *r, *b;

  say(m, "");
  say(m, "summary: the biggest change of each run from the run it varies, "
         "in dB, and of the phase of S, in degrees; M>S and S>M only over "
         "-15 dB");
  say(m, "run  name           50 52 54 56 58 5A  from  path  change  at");
  for (i = 1; i < m->nruns; i++) {
    r = &m->run[i];
    b = &m->run[r->base];
    if ((r->kind != S3D_SWEEP && r->kind != S3D_BAND) || r->base == i ||
        b->kind != r->kind || !ok(r) || !ok(b))
      continue;
    run_head(m, i, line);
    sprintf(line + strlen(line), "%4d  ", r->base);
    if (!biggest(r, b, 15, &change, &p, &k))
      strcat(line, "no value over the floor in both");
    else if (fabs(change) < 0.1)
      strcat(line, "no change over 0.1 dB");
    else
      sprintf(line + strlen(line), "%s  %+6.1f  %s", path_name[p], tidy(change),
              freq_name[k]);
    if (biggest_turn(r, b, 0, &turn, &k) && fabs(turn) >= 5)
      sprintf(line + strlen(line), ", S>S phase %+.0f at %s", turn,
              freq_name[k]);
    if (r->kind == S3D_SWEEP)
      new_path(r, b, line);
    say(m, line);
  }
}

// with the model on, how much the registers change the width it makes
static void summary_model(struct s3d_meas *m) {
  char line[256];
  double change, turn, most = 0, most_turn = 0;
  int i, p, k, first = -1, last = -1;
  const struct s3d_run *r, *b;

  for (i = 1; i < m->nruns; i++) {
    r = &m->run[i];
    b = &m->run[r->base];
    if (strncmp(r->name, "M ", 2) || !ok(r) || !ok(b) ||
        !biggest(r, b, 4, &change, &p, &k))
      continue;
    if (fabs(change) > most)
      most = fabs(change);
    if (biggest_turn(r, b, 1, &turn, &k) && fabs(turn) > most_turn)
      most_turn = fabs(turn);
    if (first < 0)
      first = i;
    last = i;
  }
  if (first < 0)
    return;
  sprintf(line,
          "model: with 50h bit 1 set, the registers change M>S, the width "
          "made from mono, by %.1f dB and %.0f degrees at most (runs %d to "
          "%d)",
          most, most_turn, first, last);
  say(m, line);
}

// the ratio and pan runs
static void summary_ratio_pan(struct s3d_meas *m) {
  char line[256];
  float b[5];
  double v, lo = 0, hi = 0;
  int i, k, held;
  const struct s3d_run *r;

  for (i = 0; i < m->nruns; i++) {
    r = &m->run[i];
    if (!ok(r))
      continue;
    if (r->kind == S3D_RATIO && has(r->u.ra.ss[0]) && has(r->u.ra.ss[4])) {
      sprintf(line,
              "%s: from S/M -24 to +6 dB, S>S at 1 kHz changes by %+.1f dB",
              r->name, tidy(r->u.ra.ss[4] - r->u.ra.ss[0]));
      // where the limit holds the boost down but not off, the S out
      // against the M in
      to_boost(r->u.ra.ss, 5, r->u.ra.b_re, r->u.ra.b_im, b);
      for (k = held = 0; k < 5; k++) {
        if (!has(b[k]) || b[k] > -1 || b[k] < OFF_DB)
          continue;
        v = r->u.ra.ss[k] + ratio_db[k];
        if (!held++ || v < lo)
          lo = v;
        if (held == 1 || v > hi)
          hi = v;
      }
      if (held)
        sprintf(line + strlen(line),
                "; it holds S out at %+.1f to %+.1f dB re M", tidy(lo),
                tidy(hi));
      say(m, line);
    } else if (r->kind == S3D_PAN) {
      sprintf(line, "%s: 0, 22.5, 45, 67.5 and 90 degrees come out at",
              r->name);
      for (k = 0; k < S3D_PANS; k++)
        if (has(r->u.pa.deg[k]))
          sprintf(line + strlen(line), "%s %.0f", k ? "," : "",
                  fabs(r->u.pa.deg[k]) < 0.5 ? 0.0 : r->u.pa.deg[k]);
        else
          strcat(line, k ? ", ." : " .");
      say(m, line);
    }
  }
}

// a step run's window k after the step, in ms, and its middle
static int traj_ms(int k) {
  return k < S3D_FINE ? 20 * k : 500 + 100 * (k - S3D_FINE);
}

static double traj_mid(int k) { return traj_ms(k) + (k < S3D_FINE ? 10 : 50); }

// the slope of the boost's gain over windows k0 to k1, in dB a second, by
// least squares over the values that aren't off
static double slope(const float *v, int k0, int k1) {
  double n = 0, st = 0, sv = 0, stt = 0, stv = 0, t, d;
  int k;

  for (k = k0; k <= k1; k++) {
    if (!has(v[k]) || v[k] < OFF_DB)
      continue;
    t = traj_mid(k) / 1000;
    n++;
    st += t;
    sv += v[k];
    stt += t * t;
    stv += t * v[k];
  }
  d = n * stt - st * st;
  return n >= 2 && d > 0 ? (n * stv - st * sv) / d : 0;
}

int s3d_limit(const struct s3d_meas *m, int i, struct s3d_lim *l) {
  const struct s3d_run *r = &m->run[i];
  float up[S3D_UP], dn[S3D_DN], st[3];
  int k0, k1;

  memset(l, 0, sizeof(*l));
  l->delay = -1;
  if (r->kind != S3D_STEP || !ok(r) || (r->u.st.b_re == 0 && r->u.st.b_im == 0))
    return -1;
  to_boost(r->u.st.up, S3D_UP, r->u.st.b_re, r->u.st.b_im, up);
  to_boost(r->u.st.dn, S3D_DN, r->u.st.b_re, r->u.st.b_im, dn);
  to_boost(r->u.st.steady, 3, r->u.st.b_re, r->u.st.b_im, st);
  l->low = st[0];
  l->high = st[1];
  l->again = st[2];
  // the S out against the M in where the limit holds the boost down but
  // not off: at 0 dB S/M, or else at -12 dB
  l->held = S3D_FLOOR;
  if (st[1] < -1 && st[1] > OFF_DB)
    l->held = r->u.st.steady[1];
  else if (st[0] < -1 && st[0] > OFF_DB)
    l->held = r->u.st.steady[0] - 12;
  // the fall: from the first window under the low level to the last one
  // over the level it ends at, plus 3 dB
  if (st[1] < st[0] - 6) {
    for (k0 = 0; k0 < S3D_UP - 1 && up[k0] > st[0] - 1; k0++)
      ;
    for (k1 = k0; k1 < S3D_UP - 1 && up[k1 + 1] > st[1] + 3; k1++)
      ;
    l->fall = (float)-slope(up, k0 > 0 ? k0 - 1 : 0, k1);
  }
  // the rise: from the first window 3 dB over the level right after the
  // step down, to the last one under the low level minus 3 dB; or else
  // over the last second, where it may only just start
  for (k0 = 1; k0 < S3D_DN && !(dn[k0] > dn[0] + 3); k0++)
    ;
  if (k0 < S3D_DN) {
    l->delay = traj_ms(k0);
    for (k1 = k0; k1 < S3D_DN - 1 && dn[k1 + 1] < st[0] - 3; k1++)
      ;
    l->rise = (float)slope(dn, k0 - 1, k1 > k0 ? k1 : k0 + 1);
  } else {
    l->rise = (float)slope(dn, S3D_DN - 10, S3D_DN - 1);
  }
  if (l->rise < 0.1)
    l->rise = 0;
  return 0;
}

// a level in w columns: off under OFF_DB
static void col_db(char *out, double v, int w) {
  if (!has(v))
    sprintf(out + strlen(out), "%*s", w, ".");
  else if (v < OFF_DB)
    sprintf(out + strlen(out), "%*s", w, "off");
  else
    sprintf(out + strlen(out), "%+*.1f", w, tidy(v));
}

// the step runs: how the limit moves the boost, and the runs where it
// falls and rises fastest and slowest, and holds S highest and lowest
static void summary_steps(struct s3d_meas *m) {
  struct s3d_lim l;
  char line[256];
  int i, j, n = 0, at[6];
  float v[6], val[6];

  for (j = 0; j < 6; j++)
    at[j] = -1;
  for (i = 0; i < m->nruns; i++) {
    if (s3d_limit(m, i, &l) < 0)
      continue;
    if (!n++) {
      say(m, "");
      say(m, "limit: the boost's gain in the step runs, from S/M -12 to 0 dB "
             "and back; fall and rise in dB a second, held the S out it holds "
             "against the M in");
      say(m, "run  name           50 52 54 56 58 5A    fall  high   low    "
             "held  delay   rise  again");
      say(m, "                                         dB/s    dB    dB  dB re "
             "M     ms   dB/s     dB");
    }
    run_head(m, i, line);
    if (l.fall > 0)
      sprintf(line + strlen(line), "%6.0f", l.fall);
    else
      strcat(line, "     -");
    col_db(line, l.high, 6);
    col_db(line, l.low, 6);
    if (has(l.held))
      sprintf(line + strlen(line), "%+8.1f", tidy(l.held));
    else
      strcat(line, "       -");
    if (l.delay >= 0)
      sprintf(line + strlen(line), "%7d", l.delay);
    else
      strcat(line, "      -");
    if (l.rise > 0)
      sprintf(line + strlen(line), "%7.1f", l.rise);
    else
      strcat(line, "      -");
    col_db(line, l.again, 7);
    say(m, line);
    // among the runs with the limit on: the slowest and fastest fall and
    // rise, the lowest and highest S held
    if (!(m->run[i].reg[S3D_R50] & 1))
      continue;
    v[0] = v[1] = l.fall;
    v[2] = v[3] = l.rise;
    v[4] = v[5] = l.held;
    for (j = 0; j < 6; j++)
      if ((j < 4 && v[j] <= 0) || !has(v[j]))
        continue;
      else if (at[j] < 0 || (j & 1 ? v[j] > val[j] : v[j] < val[j])) {
        at[j] = i;
        val[j] = v[j];
      }
  }
  if (at[0] >= 0) {
    sprintf(line, "fall: from %.0f dB/s (run %d, %s) to %.0f dB/s (run %d, %s)",
            val[0], at[0], m->run[at[0]].name, val[1], at[1],
            m->run[at[1]].name);
    say(m, line);
  }
  if (at[2] >= 0) {
    sprintf(line, "rise: from %.1f dB/s (run %d, %s) to %.1f dB/s (run %d, %s)",
            val[2], at[2], m->run[at[2]].name, val[3], at[3],
            m->run[at[3]].name);
    say(m, line);
  }
  if (at[4] >= 0) {
    sprintf(line,
            "held: S out from %+.1f dB re M (run %d, %s) to %+.1f dB (run %d, "
            "%s)",
            tidy(val[4]), at[4], m->run[at[4]].name, tidy(val[5]), at[5],
            m->run[at[5]].name);
    say(m, line);
  }
}

void s3d_summary(struct s3d_meas *m) {
  summary_runs(m);
  say(m, "");
  summary_model(m);
  summary_ratio_pan(m);
  summary_steps(m);
}
