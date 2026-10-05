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
#define AMP 0.125     // sweep tones, -18 dBFS in each channel
#define AMP_SYNC 0.25 // the burst that marks the start
#define AMP_M 0.0625  // the M tone of ratio and step runs
#define FADE 5        // ms of the fades at the ends of a tone
#define F400 3        // freq[] index of 400 Hz
#define F1K 5         // and of 1 kHz

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

// play frames of ms milliseconds (the stimulus is made at S3D_RATE)
static u32 fr(u32 ms) { return ms * (S3D_RATE / 1000); }

static double db(double ratio) {
  return ratio > 1e-9 ? 20.0 * log10(ratio) : -180.0;
}

// a value to print with one decimal, without -0.0
static double tidy(double v) { return fabs(v) < 0.05 ? 0 : v; }

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
                   int i, u8 v, const char *more, int base) {
  u8 reg[S3D_REGS];
  char name[24];

  memcpy(reg, from, S3D_REGS);
  reg[i] = v;
  sprintf(name, "%s%02X=%02X%s", prefix, reg_addr[i], v, more);
  return add(m, name, reg, S3D_SWEEP, base);
}

void s3d_init(struct s3d_meas *m, int plan, u8 reg, s3d_out_fn out, void *ctx) {
  static const u8 levels[8] = {0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38};
  u8 r[S3D_REGS], model[S3D_REGS];
  char more[8];
  int i, b, ess, mod, ri = -1;

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
  if (plan == S3D_PLAN_REG) {
    for (i = 0; i <= 0x100; i += 0x10)
      add_reg(m, "", ess_set, ri, (u8)(i > 0xFF ? 0xFF : i), "", ess);
    mod = add(m, "model", model, S3D_SWEEP, ess);
    add_reg(m, "M ", model, ri, 0x00, "", mod);
    add_reg(m, "M ", model, ri, 0xFF, "", mod);
    return;
  }
  for (i = 0; i < 8; i++)
    if (plan == S3D_PLAN_FULL || levels[i] == 0x00 || levels[i] == 0x20)
      add_reg(m, "", ess_set, S3D_R52, levels[i], "", ess);
  mod = add(m, "model", model, S3D_SWEEP, ess);
  memcpy(r, ess_set, S3D_REGS);
  r[S3D_R50] = 0x0D;
  add(m, "limit", r, S3D_SWEEP, ess);
  r[S3D_R50] = 0x0F;
  add(m, "model+limit", r, S3D_SWEEP, ess);

  // each register around ESS's value, one bit at a time, and at its ends
  for (i = S3D_R54; i <= S3D_R5A; i++) {
    if (plan == S3D_PLAN_FULL)
      for (b = 7; b >= 0; b--) {
        sprintf(more, " b%d", b);
        add_reg(m, "", ess_set, i, (u8)(ess_set[i] ^ (1 << b)), more, ess);
      }
    add_reg(m, "", ess_set, i, 0x00, "", ess);
    add_reg(m, "", ess_set, i, 0xFF, "", ess);
  }
  // the same registers with the model on, to see whether they shape it
  if (plan == S3D_PLAN_FULL)
    for (i = S3D_R54; i <= S3D_R5A; i++) {
      add_reg(m, "M ", model, i, 0x00, "", mod);
      add_reg(m, "M ", model, i, 0xFF, "", mod);
      add_reg(m, "M ", model, i, (u8)(ess_set[i] ^ 0x80), " b7", mod);
    }

  // whether the gain follows the program, with the limit off and on
  memcpy(r, ess_set, S3D_REGS);
  add(m, "ratio", r, S3D_RATIO, ess);
  r[S3D_R50] = 0x0D;
  add(m, "ratio limit", r, S3D_RATIO, ess);
  r[S3D_R50] = 0x0C;
  add(m, "step", r, S3D_STEP, ess);
  r[S3D_R50] = 0x0D;
  add(m, "step limit", r, S3D_STEP, ess);
}

int s3d_count(const struct s3d_meas *m) { return m->nruns; }

void s3d_regs(const struct s3d_meas *m, int i, u8 *reg) {
  memcpy(reg, m->run[i].reg, S3D_REGS);
}

const char *s3d_name(const struct s3d_meas *m, int i) { return m->run[i].name; }

// --- the stimulus ----------------------------------------------------------

static u32 seg_add(struct s3d_meas *m, u32 start, u32 len, u16 hz0, int side0,
                   double amp0, u16 hz1, int side1, double amp1, int ramp) {
  struct s3d_seg *s;

  if (m->nseg >= S3D_MAX_SEGS)
    return start;
  s = &m->seg[m->nseg++];
  memset(s, 0, sizeof(*s));
  s->start = start;
  s->len = len;
  s->t[0].hz = hz0;
  s->t[0].side = (s8)side0;
  s->t[0].amp = (float)amp0;
  s->t[1].hz = hz1;
  s->t[1].side = (s8)side1;
  s->t[1].amp = (float)amp1;
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

int s3d_seconds(const struct s3d_meas *m) {
  long ms = 0;
  int i;

  // each run's stimulus, and about a second to start the devices and
  // write the registers
  for (i = 0; i < m->nruns; i++)
    if (m->run[i].kind == S3D_RATIO)
      ms += 450 + 5 * 620 + 1000;
    else if (m->run[i].kind == S3D_STEP)
      ms += 450 + 3000 + 1000;
    else
      ms += 450 + 22 * (m->run[i].slow ? 410 : 160) + 1000;
  return (int)(ms / 1000);
}

u32 s3d_begin(struct s3d_meas *m, int i, u32 *rec_frames) {
  struct s3d_run *r = &m->run[i];
  u32 t = 0, s;
  int k, side, settle = r->slow ? 300 : 50;
  double a;

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
  t = seg_add(m, t, fr(40), 1000, 1, AMP_SYNC, 0, 0, 0, 3);
  t = seg_add(m, t, fr(60), 0, 0, 0, 0, 0, 0, 0);
  switch (r->kind) {
  case S3D_RATIO:
    for (k = 0; k < 5; k++) {
      a = AMP_M * pow(10.0, ratio_db[k] / 20.0);
      s = t;
      t = seg_add(m, t, fr(500 + 100 + 20), 400, 1, AMP_M, 1000, -1, a, 3);
      win_add(m, s + fr(500), 100);
    }
    break;
  case S3D_STEP:
    // 1000 ms holds whole cycles of both tones, so they go on without a
    // jump in phase where the S tone's level steps
    a = AMP_M * pow(10.0, -12 / 20.0);
    s = t;
    t = seg_add(m, t, fr(1000), 400, 1, AMP_M, 1000, -1, a, 1);
    win_add(m, s + fr(880), 100);
    s = t;
    t = seg_add(m, t, fr(1000), 400, 1, AMP_M, 1000, -1, AMP_M * 2, 0);
    for (k = 0; k < S3D_STEP_WINS; k++)
      win_add(m, s + fr(20 * k), 20);
    win_add(m, s + fr(880), 100);
    s = t;
    t = seg_add(m, t, fr(1000), 400, 1, AMP_M, 1000, -1, a, 2);
    for (k = 0; k < S3D_STEP_WINS; k++)
      win_add(m, s + fr(20 * k), 20);
    win_add(m, s + fr(880), 100);
    break;
  default:
    for (side = 1; side >= -1; side -= 2)
      for (k = 0; k < S3D_FREQS; k++) {
        s = t;
        t = seg_add(m, t, fr(settle + 100 + 10), freq[k], side, AMP, 0, 0, 0,
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
      v = s->t[j].amp * f * sin(2 * PI * s->t[j].hz * k / S3D_RATE);
      *l += v;
      *r += s->t[j].side * v;
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
  double pc[2], ps[2], wc[2], ws[2], t, l, r, v, f;
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
          v = s->t[j].amp * f * ps[j];
          l += v;
          r += s->t[j].side * v;
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

// the recorded frames of each window, once the onset is known
static void place_windows(struct s3d_meas *m) {
  double q = (double)m->rate_rec / m->rate_play;
  int i;

  for (i = 0; i < m->nwin; i++) {
    struct s3d_win *w = &m->win[i];
    w->rstart = m->ron + (u32)floor((w->pstart - m->psync) * q + 0.5);
    w->rlen = (u16)((u32)w->ms * m->rate_rec / 1000);
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

static void window_end(struct s3d_meas *m) {
  struct s3d_win *w = &m->win[m->wcur];
  double g = 2.0 / w->rlen;
  int j;

  for (j = 0; j < 2; j++) {
    w->re[j][0] = (float)(m->acc[j][0] * g);
    w->im[j][0] = (float)(m->acc[j][1] * g);
    w->re[j][1] = (float)(m->acc[j][2] * g);
    w->im[j][1] = (float)(m->acc[j][3] * g);
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

// |(L + R) / 2| or |(L - R) / 2| of tone j in window w
static double mid(const struct s3d_win *w, int j) {
  double re = (w->re[j][0] + w->re[j][1]) / 2;
  double im = (w->im[j][0] + w->im[j][1]) / 2;
  return sqrt(re * re + im * im);
}

static double side(const struct s3d_win *w, int j) {
  double re = (w->re[j][0] - w->re[j][1]) / 2;
  double im = (w->im[j][0] - w->im[j][1]) / 2;
  return sqrt(re * re + im * im);
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

static void cell(char *out, double v) {
  if (v <= S3D_FLOOR + 1) {
    strcat(out, "     .");
    return;
  }
  // six columns, and no -0.0
  if (v < -99.9)
    v = -99.9;
  if (fabs(v) < 0.05)
    v = 0;
  sprintf(out + strlen(out), "%+6.1f", v);
}

static void table_head(struct s3d_meas *m, int kind) {
  char line[200];
  int k;

  say(m, "");
  if (kind == S3D_SWEEP) {
    strcpy(line, "run  name           50 52 54 56 58 5A  path");
    for (k = 0; k < S3D_FREQS; k++)
      sprintf(line + strlen(line), "%6s", freq_name[k]);
  } else if (kind == S3D_RATIO) {
    say(m, "ratio runs: 400 Hz in M at -24 dBFS, and 1 kHz in S at a ratio "
           "to it");
    strcpy(line, "run  name           50 52 54 56 58 5A  S/M dB ");
    for (k = 0; k < 5; k++)
      sprintf(line + strlen(line), "%+6d", ratio_db[k]);
  } else {
    say(m, "step runs: 1 kHz in S jumps from -12 to +6 dB re 400 Hz in M at "
           "0 ms, and back after 1 s; S>S in 20 ms windows");
    strcpy(line, "run  name           50 52 54 56 58 5A  ms     ");
    for (k = 0; k < S3D_STEP_WINS; k++)
      sprintf(line + strlen(line), "%6d", 20 * k);
  }
  say(m, line);
}

void s3d_header(struct s3d_meas *m, u32 rate_play, u32 rate_rec,
                const char *info) {
  char line[200];
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
  say(m, ". is under the noise floor; M>S and S>M are left out where every "
         "value is");
  table_head(m, S3D_SWEEP);
}

static void sweep_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];
  char line[200];
  int p, k, any;

  for (p = 0; p < S3D_PATHS; p++) {
    any = 0;
    for (k = 0; k < S3D_FREQS; k++)
      any |= r->db[p][k] > S3D_FLOOR + 1;
    if (p >= 2 && !any)
      continue;
    run_head(m, p ? -1 : i, line);
    strcat(line, path_name[p]);
    strcat(line, " ");
    for (k = 0; k < S3D_FREQS; k++)
      cell(line, r->db[p][k]);
    say(m, line);
  }
}

static void ratio_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];
  char line[200];
  int k;

  run_head(m, i, line);
  strcat(line, "S>S 1k  ");
  for (k = 0; k < 5; k++)
    cell(line, r->extra[k]);
  say(m, line);
  run_head(m, -1, line);
  strcat(line, "M>M 400 ");
  for (k = 0; k < 5; k++)
    cell(line, r->extra[5 + k]);
  say(m, line);
}

static void step_lines(struct s3d_meas *m, int i) {
  struct s3d_run *r = &m->run[i];
  char line[220];
  int k, d;

  for (d = 0; d < 2; d++) {
    run_head(m, d ? -1 : i, line);
    strcat(line, d ? "down   " : "up     ");
    for (k = 0; k < S3D_STEP_WINS; k++)
      cell(line, r->extra[d * S3D_STEP_WINS + k]);
    say(m, line);
  }
  run_head(m, -1, line);
  sprintf(line + strlen(line), "steady: low %+.1f, high %+.1f, low again %+.1f",
          tidy(r->db[0][0]), tidy(r->db[0][1]), tidy(r->db[0][2]));
  say(m, line);
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
  double mm, ss, ms, sm, fl;
  int k;

  for (k = 0; k < S3D_FREQS; k++) {
    wm = &m->win[k];
    ws = &m->win[S3D_FREQS + k];
    mm = mid(wm, 0) / AMP;
    ms = side(wm, 0) / AMP;
    ss = side(ws, 0) / AMP;
    sm = mid(ws, 0) / AMP;
    if (m->cur == 0) {
      // the reference: no signal through the chip is no measurement
      if (mm < 1e-3 || ss < 1e-3)
        return S3D_NOSIGNAL;
      m->ref_mm[k] = (float)mm;
      m->ref_ss[k] = (float)ss;
    }
    fl = floor_db(m, wm->rlen, AMP * m->ref_mm[k]);
    r->db[0][k] = rel(mm, m->ref_mm[k], fl);
    r->db[2][k] = rel(ms, m->ref_mm[k], fl);
    fl = floor_db(m, ws->rlen, AMP * m->ref_ss[k]);
    r->db[1][k] = rel(ss, m->ref_ss[k], fl);
    r->db[3][k] = rel(sm, m->ref_ss[k], fl);
  }
  return S3D_OK;
}

// S>S at 1 kHz in window w, where the S tone has amplitude a
static float step_db(const struct s3d_meas *m, int w, double a) {
  double fl = floor_db(m, m->win[w].rlen, a * m->ref_ss[F1K]);

  return rel(side(&m->win[w], 1) / a, m->ref_ss[F1K], fl);
}

int s3d_end(struct s3d_meas *m) {
  struct s3d_run *r = &m->run[m->cur];
  double a, fl;
  int k, res;

  if (m->rstate == R_DONE) {
    if (r->kind == S3D_RATIO) {
      for (k = 0; k < 5; k++) {
        a = AMP_M * pow(10.0, ratio_db[k] / 20.0);
        fl = floor_db(m, m->win[k].rlen, a * m->ref_ss[F1K]);
        r->extra[k] = rel(side(&m->win[k], 1) / a, m->ref_ss[F1K], fl);
        fl = floor_db(m, m->win[k].rlen, AMP_M * m->ref_mm[F400]);
        r->extra[5 + k] = rel(mid(&m->win[k], 0) / AMP_M, m->ref_mm[F400], fl);
      }
      res = S3D_OK;
    } else if (r->kind == S3D_STEP) {
      a = AMP_M * pow(10.0, -12 / 20.0);
      for (k = 0; k < S3D_STEP_WINS; k++) {
        r->extra[k] = step_db(m, 1 + k, AMP_M * 2);
        r->extra[S3D_STEP_WINS + k] = step_db(m, 2 + S3D_STEP_WINS + k, a);
      }
      r->db[0][0] = step_db(m, 0, a);
      r->db[0][1] = step_db(m, 1 + S3D_STEP_WINS, AMP_M * 2);
      r->db[0][2] = step_db(m, 2 * S3D_STEP_WINS + 2, a);
      res = S3D_OK;
    } else {
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
  if (r->kind == S3D_RATIO)
    ratio_lines(m, m->cur);
  else if (r->kind == S3D_STEP)
    step_lines(m, m->cur);
  else
    sweep_lines(m, m->cur);
  if (res & S3D_CLIPPED)
    say(m, "     (a recorded sample reached full scale: the values may be "
           "too low)");
  return res;
}

void s3d_failed(struct s3d_meas *m, const char *why) {
  char line[200];

  run_head(m, m->cur, line);
  strcat(line, "failed: ");
  strncat(line, why, sizeof(line) - strlen(line) - 1);
  say(m, line);
}

// --- the summary ------------------------------------------------------------

static int ok(const struct s3d_run *r) {
  return r->result != 0xFF && !(r->result & (S3D_NOSIGNAL | S3D_SHORT));
}

// the largest change of a sweep run a from run b where both have a value,
// on the paths in mask (bit p for path p), its path and frequency; 0 if
// there's none
static int biggest(const struct s3d_run *a, const struct s3d_run *b, int mask,
                   double *change, int *path, int *f) {
  int p, k, found = 0;
  double d;

  for (p = 0; p < S3D_PATHS; p++)
    for (k = 0; k < S3D_FREQS; k++) {
      if (!(mask & (1 << p)) || a->db[p][k] <= S3D_FLOOR + 1 ||
          b->db[p][k] <= S3D_FLOOR + 1)
        continue;
      d = a->db[p][k] - b->db[p][k];
      if (!found || fabs(d) > fabs(*change)) {
        *change = d;
        *path = p;
        *f = k;
        found = 1;
      }
    }
  return found;
}

// a path that comes up from the floor in run a (appears), or goes under
// it (disappears): "M>S appears at +3.5 dB" or ""
static void new_path(const struct s3d_run *a, const struct s3d_run *b,
                     char *out) {
  int p, k, up, down;
  double most;

  for (p = 0; p < S3D_PATHS; p++) {
    up = down = 0;
    most = S3D_FLOOR;
    for (k = 0; k < S3D_FREQS; k++) {
      if (a->db[p][k] > S3D_FLOOR + 1 && b->db[p][k] <= S3D_FLOOR + 1) {
        up = 1;
        if (a->db[p][k] > most)
          most = a->db[p][k];
      }
      down |= a->db[p][k] <= S3D_FLOOR + 1 && b->db[p][k] > S3D_FLOOR + 1;
    }
    if (up)
      sprintf(out + strlen(out), ", %s appears, up to %+.1f", path_name[p],
              tidy(most));
    else if (down)
      sprintf(out + strlen(out), ", %s goes under the floor", path_name[p]);
  }
}

// "within 1 dB of its steady value 40 ms after", from the first window
// after which a step run stays within 1 dB of v
static void settled(char *out, const float *traj, double v, const char *step) {
  int k;

  for (k = S3D_STEP_WINS; k > 0; k--)
    if (traj[k - 1] <= S3D_FLOOR + 1 || fabs(traj[k - 1] - v) > 1.0)
      break;
  if (k < S3D_STEP_WINS)
    sprintf(out + strlen(out), "%d ms after the step %s", 20 * k, step);
  else
    sprintf(out + strlen(out), "not yet %d ms after the step %s",
            20 * (S3D_STEP_WINS - 1), step);
}

void s3d_summary(struct s3d_meas *m) {
  char line[200];
  double change, most = 0;
  int i, p, k, first = -1, last = -1, seen = 0;
  const struct s3d_run *r, *b;

  say(m, "");
  say(m, "summary: the biggest change of each run from the run it varies, "
         "in dB");
  say(m, "run  name           50 52 54 56 58 5A  from  path  change  at");
  for (i = 1; i < m->nruns; i++) {
    r = &m->run[i];
    b = &m->run[r->base];
    if (r->kind != S3D_SWEEP || r->base == i || !ok(r) || !ok(b))
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
    new_path(r, b, line);
    say(m, line);
    // runs that vary the model with a register: what they do to its M>S
    if (strncmp(r->name, "M ", 2) == 0 && biggest(r, b, 4, &change, &p, &k)) {
      if (!seen || fabs(change) > most)
        most = fabs(change);
      seen = 1;
      if (first < 0)
        first = i;
      last = i;
    }
  }
  say(m, "");
  if (seen) {
    sprintf(line,
            "model: with 50h bit 1 set, the registers change M>S, the width "
            "made from mono, by %.1f dB at most (runs %d to %d)",
            most, first, last);
    say(m, line);
  }
  for (i = 0; i < m->nruns; i++) {
    r = &m->run[i];
    if (!ok(r) || r->kind == S3D_SWEEP)
      continue;
    if (r->kind == S3D_RATIO && r->extra[0] > S3D_FLOOR + 1 &&
        r->extra[4] > S3D_FLOOR + 1) {
      sprintf(line,
              "%s: from S/M -24 to +6 dB, S>S at 1 kHz changes by %+.1f dB",
              r->name, tidy(r->extra[4] - r->extra[0]));
      say(m, line);
    } else if (r->kind == S3D_STEP) {
      sprintf(line, "%s: S>S is within 1 dB of its steady value ", r->name);
      settled(line, r->extra, r->db[0][1], "up");
      strcat(line, ", and ");
      settled(line, r->extra + S3D_STEP_WINS, r->db[0][2], "down");
      say(m, line);
    }
  }
}
