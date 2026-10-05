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
#define STEP_MS 2000  // each level of a step run: whole cycles of both tones

// the windows of a step run
#define W_LOW 0                   // steady, before the step up
#define W_UP 1                    // after the step up
#define W_HIGH (W_UP + S3D_TRAJ)  // steady, before the step down
#define W_DOWN (W_HIGH + 1)       // after the step down
#define W_END (W_DOWN + S3D_TRAJ) // steady, at the end

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
static const char *const plan_name[3] = {"full", "quick", "one register"};

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

// ESS's setting with 54h, 56h and 58h changed together, as one vector
static void add_vec(struct s3d_meas *m, const char *name, u8 v54, u8 v56,
                    u8 v58, int base) {
  u8 reg[S3D_REGS];

  memcpy(reg, ess_set, S3D_REGS);
  reg[S3D_R54] = v54;
  reg[S3D_R56] = v56;
  reg[S3D_R58] = v58;
  add(m, name, reg, S3D_SWEEP, base);
}

// bits 6:0 halved or doubled, bit 7 kept
static u8 half(u8 v) { return (u8)(0x80 | ((v & 0x7F) >> 1)); }

static u8 twice(u8 v) {
  int x = (v & 0x7F) * 2;

  return (u8)(0x80 | (x > 0x7F ? 0x7F : x));
}

void s3d_init(struct s3d_meas *m, int plan, u8 reg, s3d_out_fn out, void *ctx) {
  static const u8 levels[8] = {0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38};
  u8 r[S3D_REGS], model[S3D_REGS], lim[S3D_REGS];
  char more[8];
  int i, b, ess, mod, band, blim, step, slim, pan, ri = -1;
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

  if (plan == S3D_PLAN_REG) {
    for (i = 0; i <= 0x100; i += 0x10)
      add_reg(m, "", ess_set, ri, (u8)(i > 0xFF ? 0xFF : i), "", S3D_SWEEP,
              ess);
    mod = add(m, "model", model, S3D_SWEEP, ess);
    add_reg(m, "M ", model, ri, 0x00, "", S3D_SWEEP, mod);
    add_reg(m, "M ", model, ri, 0xFF, "", S3D_SWEEP, mod);
    blim = add(m, "band limit", lim, S3D_BAND, ess);
    add_reg(m, "band L ", lim, ri, 0x00, "", S3D_BAND, blim);
    add_reg(m, "band L ", lim, ri, 0xFF, "", S3D_BAND, blim);
    slim = add(m, "step limit", lim, S3D_STEP, ess);
    add_reg(m, "step L ", lim, ri, 0x00, "", S3D_STEP, slim);
    add_reg(m, "step L ", lim, ri, 0xFF, "", S3D_STEP, slim);
    return;
  }
  for (i = 0; i < 8; i++)
    if (full || levels[i] == 0x00 || levels[i] == 0x20)
      add_reg(m, "", ess_set, S3D_R52, levels[i], "", S3D_SWEEP, ess);
  mod = add(m, "model", model, S3D_SWEEP, ess);
  add(m, "limit", lim, S3D_SWEEP, ess);
  r[S3D_R50] = 0x0F;
  memcpy(r + 1, ess_set + 1, S3D_REGS - 1);
  add(m, "model+limit", r, S3D_SWEEP, ess);

  // each register around ESS's value, one bit at a time, and at its ends
  for (i = S3D_R54; i <= S3D_R5A; i++) {
    if (full)
      for (b = 7; b >= 0; b--) {
        sprintf(more, " b%d", b);
        add_reg(m, "", ess_set, i, (u8)(ess_set[i] ^ (1 << b)), more, S3D_SWEEP,
                ess);
      }
    add_reg(m, "", ess_set, i, 0x00, "", S3D_SWEEP, ess);
    add_reg(m, "", ess_set, i, 0xFF, "", S3D_SWEEP, ess);
  }
  // 54h-58h as one vector: its sign bits cleared, its length halved and
  // doubled, its coordinates turned and two of them swapped
  add_vec(m, "vec neg", 0x0F, 0x15, 0x14, ess);
  add_vec(m, "vec /2", half(0x8F), half(0x95), half(0x94), ess);
  if (full) {
    add_vec(m, "vec x2", twice(0x8F), twice(0x95), twice(0x94), ess);
    add_vec(m, "vec rot", 0x95, 0x94, 0x8F, ess);
    add_vec(m, "vec swap", 0x95, 0x8F, 0x94, ess);
    // the registers with the model on, to see whether they shape it
    for (i = S3D_R54; i <= S3D_R5A; i++) {
      add_reg(m, "M ", model, i, 0x00, "", S3D_SWEEP, mod);
      add_reg(m, "M ", model, i, 0xFF, "", S3D_SWEEP, mod);
      add_reg(m, "M ", model, i, (u8)(ess_set[i] ^ 0x80), " b7", S3D_SWEEP,
              mod);
    }
  }

  // whether the gain follows the program, in which band, where a panned
  // tone comes out, and how fast the gain follows; with the limit off and
  // on, and on with each register at its ends
  add(m, "ratio", ess_set, S3D_RATIO, ess);
  add(m, "ratio limit", lim, S3D_RATIO, ess);
  band = add(m, "band", ess_set, S3D_BAND, ess);
  blim = add(m, "band limit", lim, S3D_BAND, band);
  for (i = S3D_R54; full && i <= S3D_R5A; i++) {
    add_reg(m, "band L ", lim, i, 0x00, "", S3D_BAND, blim);
    add_reg(m, "band L ", lim, i, 0xFF, "", S3D_BAND, blim);
  }
  pan = add(m, "pan", ess_set, S3D_PAN, ess);
  add(m, "pan limit", lim, S3D_PAN, pan);
  step = add(m, "step", ess_set, S3D_STEP, ess);
  slim = add(m, "step limit", lim, S3D_STEP, step);
  for (i = S3D_R54; full && i <= S3D_R5A; i++) {
    add_reg(m, "step L ", lim, i, 0x00, "", S3D_STEP, slim);
    add_reg(m, "step L ", lim, i, 0xFF, "", S3D_STEP, slim);
  }
  if (full)
    add_reg(m, "step L ", lim, S3D_R52, 0x20, "", S3D_STEP, slim);
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

// the windows after a step: 20 ms over the first 500 ms, then 100 ms
static void step_windows(struct s3d_meas *m, u32 s) {
  int k;

  for (k = 0; k < S3D_FINE; k++)
    win_add(m, s + fr(20 * k), 20);
  for (k = 0; k < S3D_COARSE; k++)
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
      ms += 3 * STEP_MS;
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
    t = seg_add(m, t, fr(STEP_MS), 400, AMP_M, AMP_M, 1000, a, -a, 1);
    win_add(m, s + fr(STEP_MS - 120), 100);
    s = t;
    t = seg_add(m, t, fr(STEP_MS), 400, AMP_M, AMP_M, 1000, 2 * AMP_M,
                -2 * AMP_M, 0);
    step_windows(m, s);
    win_add(m, s + fr(STEP_MS - 120), 100);
    s = t;
    t = seg_add(m, t, fr(STEP_MS), 400, AMP_M, AMP_M, 1000, a, -a, 2);
    step_windows(m, s);
    win_add(m, s + fr(STEP_MS - 120), 100);
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
    say(m, "step runs: 1 kHz in S jumps from -12 to +6 dB re 400 Hz in M at "
           "0 ms, and back after 2 s; S>S in 20 ms windows to 500 ms, then "
           "in 100 ms windows to 1.8 s");
    strcat(line, "ms      ");
    for (k = 0; k < S3D_FINE; k++)
      sprintf(line + strlen(line), "%6d", 20 * k);
    say(m, line);
    sprintf(line, "%39s%-8s", "", "ms");
    for (k = 0; k < S3D_COARSE; k++)
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

static void row(struct s3d_meas *m, int i, const char *label, const float *v,
                int n, int deg) {
  char line[256];
  int k;

  run_head(m, i, line);
  sprintf(line + strlen(line), "%-8s", label);
  for (k = 0; k < n; k++)
    if (deg)
      cell_deg(line, v[k]);
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
    row(m, p ? -1 : i, path_name[p], r->u.sw.db[p], S3D_FREQS, 0);
    // the phase of S in every run but the reference, where it's the
    // zero, and of each cross term that is there
    if ((p == 1 && i && turned) || p >= 2)
      row(m, -1, deg_name[p - 1], r->u.sw.ph[p - 1], S3D_FREQS, 1);
  }
}

static void band_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];

  row(m, i, "S>S", r->u.sw.db[1], S3D_FREQS, 0);
  row(m, -1, "M>M 400", r->u.sw.db[0], S3D_FREQS, 0);
}

static void pan_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];

  row(m, i, "out deg", r->u.pa.deg, S3D_PANS, 1);
  row(m, -1, "level", r->u.pa.lev, S3D_PANS, 0);
}

static void step_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];
  char line[256];

  row(m, i, "up", r->u.st.traj[0], S3D_FINE, 0);
  row(m, -1, "", r->u.st.traj[0] + S3D_FINE, S3D_COARSE, 0);
  row(m, -1, "down", r->u.st.traj[1], S3D_FINE, 0);
  row(m, -1, "", r->u.st.traj[1] + S3D_FINE, S3D_COARSE, 0);
  run_head(m, -1, line);
  sprintf(line + strlen(line), "steady: low %+.1f, high %+.1f, low again %+.1f",
          tidy(r->u.st.steady[0]), tidy(r->u.st.steady[1]),
          tidy(r->u.st.steady[2]));
  say(m, line);
}

static void ratio_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];

  row(m, i, "S>S 1k", r->u.ra.ss, 5, 0);
  row(m, -1, "M>M 400", r->u.ra.mm, 5, 0);
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

  for (k = 0; k < S3D_TRAJ; k++) {
    r->u.st.traj[0][k] = step_db(m, W_UP + k, AMP_M * 2);
    r->u.st.traj[1][k] = step_db(m, W_DOWN + k, a);
  }
  r->u.st.steady[0] = step_db(m, W_LOW, a);
  r->u.st.steady[1] = step_db(m, W_HIGH, AMP_M * 2);
  r->u.st.steady[2] = step_db(m, W_END, a);
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

static int ok(const struct s3d_run *r) {
  return r->result != 0xFF && !(r->result & (S3D_NOSIGNAL | S3D_SHORT));
}

static int run_named(const struct s3d_meas *m, const char *name) {
  int i;

  for (i = 0; i < m->nruns; i++)
    if (!strcmp(m->run[i].name, name) && ok(&m->run[i]))
      return i;
  return -1;
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
      if (!(mask & (1 << p)) || !has(a->u.sw.db[p][k]) ||
          !has(b->u.sw.db[p][k]))
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
    if (!has(a->u.sw.ph[q][k]) || !has(b->u.sw.ph[q][k]))
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
      if (has(a->u.sw.db[p][k]) && !has(b->u.sw.db[p][k])) {
        up = 1;
        if (a->u.sw.db[p][k] > most)
          most = a->u.sw.db[p][k];
      }
      down |= !has(a->u.sw.db[p][k]) && has(b->u.sw.db[p][k]);
    }
    if (up)
      sprintf(out + strlen(out), ", %s appears, up to %+.1f", path_name[p],
              tidy(most));
    else if (down)
      sprintf(out + strlen(out), ", %s goes under the floor", path_name[p]);
  }
}

// a step run's window k after the step, in ms
static int traj_ms(int k) {
  return k < S3D_FINE ? 20 * k : 500 + 100 * (k - S3D_FINE);
}

// the first window after which a step run stays within 1 dB of v, in ms,
// or -1 if it doesn't settle
static int settle_ms(const float *traj, double v) {
  int k;

  for (k = S3D_TRAJ; k > 0; k--)
    if (!has(traj[k - 1]) || fabs(traj[k - 1] - v) > 1.0)
      break;
  return k < S3D_TRAJ ? traj_ms(k) : -1;
}

static void settled(char *out, const float *traj, double v, const char *step) {
  int ms = settle_ms(traj, v);

  if (ms >= 0)
    sprintf(out + strlen(out), "%d ms after the step %s", ms, step);
  else
    sprintf(out + strlen(out), "not by %d ms after the step %s",
            traj_ms(S3D_TRAJ - 1), step);
}

// a settling time of the window line, 30000 for none
static void window_ms(char *out, int ms) {
  if (ms >= 30000)
    sprintf(out + strlen(out), "over %d ms", traj_ms(S3D_TRAJ - 1));
  else
    sprintf(out + strlen(out), "%d ms", ms);
}

// the sweep and band runs against the run each varies
static void summary_runs(struct s3d_meas *m) {
  char line[256];
  double change, turn;
  int i, p, k;
  const struct s3d_run *r, *b;

  say(m, "");
  say(m, "summary: the biggest change of each run from the run it varies, "
         "in dB, and of the phase of S, in degrees");
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

// the change of S>S from run b to run a at frequency k, in dB and degrees;
// 0 where either is under the floor
static int ss_change(const struct s3d_run *a, const struct s3d_run *b, int k,
                     double *d, double *turn) {
  if (!has(a->u.sw.db[1][k]) || !has(b->u.sw.db[1][k]) ||
      !has(a->u.sw.ph[0][k]) || !has(b->u.sw.ph[0][k]))
    return 0;
  *d = a->u.sw.db[1][k] - b->u.sw.db[1][k];
  *turn = wrap(a->u.sw.ph[0][k] - b->u.sw.ph[0][k]);
  return 1;
}

// whether 54h-58h act as one vector: their sign bits cleared together,
// against the sum of each one cleared alone, which is the same where
// each acts by itself, one after another
static void summary_vector(struct s3d_meas *m) {
  static const char *const alone[3] = {"54=0F b7", "56=15 b7", "58=14 b7"};
  char line[256];
  double d, t, dj, tj, most = 0, most_t = 0, joint = 0, joint_t = 0;
  int ess = run_named(m, "ess"), neg = run_named(m, "vec neg");
  int i, j, k, all = 1, one[3];

  for (j = 0; j < 3; j++) {
    one[j] = run_named(m, alone[j]);
    all &= one[j] >= 0;
  }
  if (ess < 0 || neg < 0)
    return;
  for (k = 0; k < S3D_FREQS; k++) {
    if (!ss_change(&m->run[neg], &m->run[ess], k, &dj, &tj))
      continue;
    if (fabs(dj) > fabs(joint))
      joint = dj;
    if (fabs(tj) > fabs(joint_t))
      joint_t = tj;
    for (j = 0; all && j < 3; j++) {
      if (!ss_change(&m->run[one[j]], &m->run[ess], k, &d, &t))
        break;
      dj -= d;
      tj = wrap(tj - t);
    }
    if (all && j == 3) {
      if (fabs(dj) > fabs(most))
        most = dj;
      if (fabs(tj) > fabs(most_t))
        most_t = tj;
    }
  }
  sprintf(line,
          "vector: clearing bit 7 of 54h-58h together changes S>S by up to "
          "%+.1f dB and its phase by up to %+.0f degrees",
          tidy(joint), fabs(joint_t) < 0.5 ? 0.0 : joint_t);
  say(m, line);
  if (all) {
    sprintf(line,
            "        and differs from the sum of the three cleared alone by "
            "up to %+.1f dB and %+.0f degrees (near 0 if each acts by itself)",
            tidy(most), fabs(most_t) < 0.5 ? 0.0 : most_t);
    say(m, line);
  }
  for (i = 0; i < m->nruns; i++)
    if (!strncmp(m->run[i].name, "vec ", 4) && i != neg && ok(&m->run[i]) &&
        biggest(&m->run[i], &m->run[ess], 15, &d, &j, &k)) {
      sprintf(line, "        %s: up to %+.1f dB (%s at %s)", m->run[i].name,
              tidy(d), path_name[j], freq_name[k]);
      if (biggest_turn(&m->run[i], &m->run[ess], 0, &t, &k))
        sprintf(line + strlen(line), ", S>S phase up to %+.0f degrees",
                fabs(t) < 0.5 ? 0.0 : t);
      say(m, line);
    }
}

// the ratio, pan and step runs, and how the registers move the time the
// limit takes
static void summary_limit(struct s3d_meas *m) {
  char line[256];
  int i, k, ms, fast = -1, slow = -1, fast_i = -1, slow_i = -1;
  const struct s3d_run *r;

  for (i = 0; i < m->nruns; i++) {
    r = &m->run[i];
    if (!ok(r))
      continue;
    if (r->kind == S3D_RATIO && has(r->u.ra.ss[0]) && has(r->u.ra.ss[4])) {
      sprintf(line,
              "%s: from S/M -24 to +6 dB, S>S at 1 kHz changes by %+.1f dB",
              r->name, tidy(r->u.ra.ss[4] - r->u.ra.ss[0]));
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
    } else if (r->kind == S3D_STEP) {
      // up: to the level it reaches, down: back to the level before the
      // step up, which a limit that learns wouldn't come back to
      sprintf(line, "%s: within 1 dB ", r->name);
      settled(line, r->u.st.traj[0], r->u.st.steady[1], "up");
      strcat(line, " and ");
      settled(line, r->u.st.traj[1], r->u.st.steady[0], "down");
      sprintf(line + strlen(line), "; low %+.1f dB before, %+.1f after",
              tidy(r->u.st.steady[0]), tidy(r->u.st.steady[2]));
      say(m, line);
      // the window of the limit: the runs that vary the step with it on,
      // where one that doesn't settle counts as the slowest
      if (strncmp(r->name, "step L ", 7))
        continue;
      ms = settle_ms(r->u.st.traj[1], r->u.st.steady[0]);
      if (ms < 0)
        ms = 30000;
      if (fast_i < 0 || ms < fast) {
        fast = ms;
        fast_i = i;
      }
      if (slow_i < 0 || ms > slow) {
        slow = ms;
        slow_i = i;
      }
    }
  }
  if (fast_i < 0)
    return;
  strcpy(line, "window: with the limit on, the settling after the step down "
               "goes from ");
  window_ms(line, fast);
  sprintf(line + strlen(line), " (run %d, %s) to ", fast_i,
          m->run[fast_i].name);
  window_ms(line, slow);
  sprintf(line + strlen(line), " (run %d, %s)", slow_i, m->run[slow_i].name);
  say(m, line);
}

void s3d_summary(struct s3d_meas *m) {
  summary_runs(m);
  say(m, "");
  summary_model(m);
  summary_vector(m);
  summary_limit(m);
}
