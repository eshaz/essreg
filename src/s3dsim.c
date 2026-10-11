/*
 * A 3-D effect made after the measurement of one card, for the
 * measurement's tests and for /sim (see s3dsim.h).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <math.h>
#include <string.h>

#include "s3dsim.h"

#define PI 3.14159265358979
#define GAIN 0.5
#define RIGHT 1.0257      // the right channel's level against the left
#define HP_HZ 380.0       // the boost's high-pass
#define LP_HZ 540.0       // and low-pass, both first order
#define PEAK 11.79        // and its gain, for 16.8 dB at its peak
#define LAG 4.2e-6        // and its lag, seconds
#define SKEW 1.7e-6       // the right channel's lag in the ADC, seconds
#define MODEL 0.70710678  // the model's S, 3 dB under the boost
#define KEEP (1.0 / 32)   // the input's S that the model keeps
#define DETECT 420.0      // the limit's high-pass on the S it follows
#define FLOOR 0.0045      // and the least gain it leaves, -47 dB
#define DETECT_1K 1.0844  // and its gain at 1 kHz brought to 0 dB
#define DETECT_M 460.0    // its high-pass on the M, 0 dB at 400 Hz
#define RESET_GAIN 0.047  // 50h at 08h: the S's low-pass part, 0.4 dB
#define RESET_HZ 550.0    // and its corner
#define K_STEP (1 / 91.2) // a level's part re the M, a step of 54h or 56h
#define RISE 1.06         // 54h's level for the rise, 0.5 dB higher
#define ENV 0.002         // each stage of the envelopes, seconds
// the levels' fixed part for each step of 5Ah: 0.00188 of the M the card
// was measured with, -24 dBFS in each channel, as it reaches the effect
#define D_STEP (0.00188 * 0.0625 * (1 + RIGHT) / 2)

// a first-order section by the bilinear transform: b0 b1 a1
static void first(double hz, u32 rate, int high, double *c) {
  double k = tan(PI * hz / rate);

  c[0] = high ? 1 / (1 + k) : k / (1 + k);
  c[1] = high ? -c[0] : c[0];
  c[2] = (k - 1) / (k + 1);
}

static double boost_gain(const u8 *reg) {
  return PEAK * pow(10.0, -0.75 * (63 - (reg[1] & 0x3F)) / 20);
}

void s3dsim_speed(const u8 *reg, double *fall, double *rise) {
  // one step of the gain every bits 3:0 + 1 ticks falling, and bits 7:4
  // + 1 times as many rising
  *fall = 257.5 / ((reg[4] & 0x0F) + 1);
  *rise = *fall / ((reg[4] >> 4) + 1);
}

void s3dsim_levels(const u8 *reg, double m, double *rise, double *fall) {
  double mi = m * (1 + RIGHT) / 2, a, b;

  // both levels where the effect compares them, against the M there: 54h
  // or 56h over 91.2 times the M, and 5Ah's fixed part
  a = K_STEP * reg[2] * mi + D_STEP * reg[5];
  b = K_STEP * reg[3] * mi + D_STEP * reg[5];
  *rise = 20 * log10(a * RISE / mi);
  *fall = 20 * log10((b > a ? b : a) / mi);
}

// a first-order all-pass (a + z^-1) / (1 + a z^-1) that delays by t
// seconds at low frequencies: its a
static double allpass(double t, u32 rate) {
  return (1 - t * rate) / (1 + t * rate);
}

// a first-order high-pass's gain at hz
static double hp_gain(const double *c, u32 rate, double hz) {
  double w = 2 * PI * hz / rate;

  return c[0] * sqrt((2 - 2 * cos(w)) / (1 + c[2] * c[2] + 2 * c[2] * cos(w)));
}

double s3dsim_detect_m(u32 rate, double hz) {
  double c[3];

  first(DETECT_M, rate, 1, c);
  return hp_gain(c, rate, hz) / hp_gain(c, rate, 400);
}

void s3dsim_reset(struct s3dsim *s, u32 rate) {
  static const u8 off[6] = {0x04, 0x3F, 0x8F, 0x95, 0x94, 0x80};

  memset(s, 0, sizeof(*s));
  s->rate = rate;
  s->lag[0] = allpass(LAG, rate);
  s->skew[0] = allpass(SKEW, rate);
  s->seed = 12345;
  s->gain = 1;
  s3dsim_regs(s, off);
}

void s3dsim_regs(struct s3dsim *s, const u8 *reg) {
  double fall, rise;
  int i;

  memcpy(s->reg, reg, sizeof(s->reg));
  // each run on the card started with the boost back
  s->gain = 1;
  s->env_m[0] = s->env_m[1] = s->env_s[0] = s->env_s[1] = 0;
  first(HP_HZ, s->rate, 1, s->hb);
  first(LP_HZ, s->rate, 0, s->lb);
  s->g = boost_gain(reg);
  first(DETECT, s->rate, 1, s->db);
  first(DETECT_M, s->rate, 1, s->mb);
  s->m_400 = 1 / hp_gain(s->mb, s->rate, 400);
  first(RESET_HZ, s->rate, 0, s->rb);
  // the envelopes are means of the absolute value, 2 / pi of a tone's
  // amplitude
  for (i = 0; i < 2; i++) {
    s->k[i] = K_STEP * reg[2 + i];
    s->d[i] = 2 / PI * D_STEP * reg[5];
  }
  s3dsim_speed(reg, &fall, &rise);
  s->f_down = pow(10.0, -fall / s->rate / 20);
  s->f_up = pow(10.0, rise / s->rate / 20);
}

static double noise(struct s3dsim *s) {
  s->seed = s->seed * 1103515245UL + 12345UL;
  return ((double)((s->seed >> 16) & 0x7FFF) / 32768.0 - 0.5) * 4.0 / 32768;
}

static s16 to_s16(double v) {
  long x = (long)floor(v * 32768.0 + 0.5);

  if (x > 32767)
    x = 32767;
  if (x < -32768)
    x = -32768;
  return (s16)x;
}

void s3dsim_run(struct s3dsim *s, const s16 *in, s16 *out, u16 n) {
  int on = (s->reg[0] & 0x0C) == 0x0C, model = s->reg[0] & 2;
  double k = 1 - exp(-1.0 / (ENV * s->rate));
  double l, r, m, sd, x, y, b, lo, hi;
  u16 i;

  for (i = 0; i < n; i++) {
    l = in[2 * i] / 32768.0;
    r = in[2 * i + 1] / 32768.0 * RIGHT;
    m = (l + r) / 2;
    sd = (l - r) / 2;
    if ((s->reg[0] & 0x0C) == 0x08) {
      // bit 3 without bit 2 adds a little of the S through a low-pass
      y = s->rb[0] * sd + s->rb[1] * s->rx - s->rb[2] * s->ry;
      s->rx = sd;
      s->ry = y;
      sd += RESET_GAIN * y;
    }
    if (on) {
      // the boost: high-pass, then low-pass, of the S, or of the M for
      // the model
      x = model ? m : sd;
      y = s->hb[0] * x + s->hb[1] * s->hx - s->hb[2] * s->hy;
      s->hx = x;
      s->ly = s->lb[0] * y + s->lb[1] * s->hy - s->lb[2] * s->ly;
      s->hy = y;
      b = s->g * s->ly * (model ? MODEL : 1);
      // its lag
      y = s->lag[0] * b + s->lag[1] - s->lag[0] * s->lag[2];
      s->lag[1] = b;
      s->lag[2] = y;
      b = y;
      if (s->reg[0] & 1) {
        // the limit: follow the M in and the S out, this through a
        // high-pass with 0 dB at 1 kHz, and move the boost's gain down
        // while the S is over both levels, and up while it is under 54h's
        // made higher
        b *= s->gain;
        x = (model ? sd * KEEP : sd) + b;
        y = s->db[0] * x + s->db[1] * s->dx - s->db[2] * s->dy;
        s->dx = x;
        s->dy = y;
        x = y * DETECT_1K;
        // the M through its own high-pass, 0 dB at 400 Hz
        y = s->mb[0] * m + s->mb[1] * s->mx - s->mb[2] * s->my;
        s->mx = m;
        s->my = y;
        s->env_m[0] += (fabs(y) * s->m_400 - s->env_m[0]) * k;
        s->env_m[1] += (s->env_m[0] - s->env_m[1]) * k;
        s->env_s[0] += (fabs(x) - s->env_s[0]) * k;
        s->env_s[1] += (s->env_s[0] - s->env_s[1]) * k;
        // (each over -80 dBFS, so that silence doesn't count)
        lo = s->k[0] * s->env_m[1] + s->d[0];
        hi = s->k[1] * s->env_m[1] + s->d[1];
        if (hi < lo)
          hi = lo;
        if (s->env_s[1] > hi + 1e-4)
          s->gain *= s->f_down;
        if (s->env_s[1] < lo * RISE + 1e-4)
          s->gain *= s->f_up;
        if (s->gain > 1)
          s->gain = 1;
        if (s->gain < FLOOR)
          s->gain = FLOOR;
      } else {
        s->gain = 1;
      }
      sd = (model ? sd * KEEP : sd) + b;
    }
    // the path's gain, the right channel's lag and the delay, then the
    // recording's noise
    x = GAIN * (m - sd);
    y = s->skew[0] * x + s->skew[1] - s->skew[0] * s->skew[2];
    s->skew[1] = x;
    s->skew[2] = y;
    out[2 * i] = s->dl[0][s->dpos];
    out[2 * i + 1] = s->dl[1][s->dpos];
    s->dl[0][s->dpos] = to_s16(GAIN * (m + sd) + noise(s));
    s->dl[1][s->dpos] = to_s16(y + noise(s));
    s->dpos = (s->dpos + 1) % S3DSIM_DELAY;
  }
}

struct z {
  double re, im;
};

static struct z z_of(double re, double im) {
  struct z v;

  v.re = re;
  v.im = im;
  return v;
}

static struct z z_add(struct z a, struct z b) {
  return z_of(a.re + b.re, a.im + b.im);
}

static struct z z_sub(struct z a, struct z b) {
  return z_of(a.re - b.re, a.im - b.im);
}

static struct z z_mul(struct z a, struct z b) {
  return z_of(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}

static struct z z_div(struct z a, struct z b) {
  double d = b.re * b.re + b.im * b.im;

  return z_of((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}

static struct z z_scale(struct z a, double k) {
  return z_of(a.re * k, a.im * k);
}

// b0 + b1 z^-1 over 1 + a1 z^-1 at z = e^(jw)
static struct z at(const double *c, double w) {
  return z_div(z_of(c[0] + c[1] * cos(w), -c[1] * sin(w)),
               z_of(1 + c[2] * cos(w), -c[2] * sin(w)));
}

// the effect's S out at w of an M and an S in, with the boost at g of its
// gain, as the limit leaves it
static struct z effect(const u8 *reg, u32 rate, double w, struct z m,
                       struct z s, double g) {
  double hb[3], lb[3], ap[3];
  struct z b;
  int model = reg[0] & 2;

  if ((reg[0] & 0x0C) == 0x08) {
    first(RESET_HZ, rate, 0, lb);
    s = z_add(s, z_mul(z_scale(at(lb, w), RESET_GAIN), s));
  }
  if ((reg[0] & 0x0C) == 0x0C) {
    first(HP_HZ, rate, 1, hb);
    first(LP_HZ, rate, 0, lb);
    ap[0] = ap[2] = allpass(LAG, rate);
    ap[1] = 1;
    b = z_mul(z_mul(at(hb, w), at(lb, w)), at(ap, w));
    b = z_mul(z_scale(b, g * boost_gain(reg) * (model ? MODEL : 1)),
              model ? m : s);
    s = z_add(model ? z_scale(s, KEEP) : s, b);
  }
  return s;
}

// the M and S out at w of l and r in, with these registers and the limit
// off
static void path(const u8 *reg, u32 rate, double w, struct z l, struct z r,
                 struct z *mo, struct z *so) {
  double ap[3];
  struct z m, s, lo, ro;

  r = z_scale(r, RIGHT);
  m = z_scale(z_add(l, r), 0.5);
  s = effect(reg, rate, w, m, z_scale(z_sub(l, r), 0.5), 1);
  ap[0] = ap[2] = allpass(SKEW, rate);
  ap[1] = 1;
  lo = z_add(m, s);
  ro = z_mul(z_sub(m, s), at(ap, w));
  *mo = z_scale(z_add(lo, ro), 0.5);
  *so = z_scale(z_sub(lo, ro), 0.5);
}

void s3dsim_response(const u8 *reg, u32 rate, double hz, double *re,
                     double *im) {
  static const u8 off[6] = {0x04, 0x3F, 0x8F, 0x95, 0x94, 0x80};
  struct z one = z_of(1, 0), mm, ms, sm, ss, mm0, ss0, x, v[4];
  double w = 2 * PI * hz / rate;
  int i;

  // the M and S out of an M in and of an S in, with the registers and
  // with the effect off, and M>M, S>S, M>S and S>M as ess3d measures them
  path(reg, rate, w, one, one, &mm, &ms);
  path(reg, rate, w, one, z_scale(one, -1), &sm, &ss);
  path(off, rate, w, one, one, &mm0, &x);
  path(off, rate, w, one, z_scale(one, -1), &x, &ss0);
  v[0] = z_div(mm, mm0);
  v[1] = z_div(ss, ss0);
  v[2] = z_div(ms, mm0);
  v[3] = z_div(sm, ss0);
  for (i = 0; i < 4; i++) {
    re[i] = v[i].re;
    im[i] = v[i].im;
  }
}

// --- what ess3d's tray panel plots ------------------------------------------

#define PLOT_RATE 48000UL
#define STEP_M 0.0630957 // the measurement's M, -24 dBFS
#define STEP_LATE 4      // the limit's detector, about 4 ms late

static double mag(struct z a) { return sqrt(a.re * a.re + a.im * a.im); }

static double db_of(double v) { return v > 1e-6 ? 20 * log10(v) : -120; }

void s3dsim_curve(const u8 *reg, double hz, double gain, double *ss,
                  double *ms) {
  struct z zero = z_of(0, 0), one = z_of(1, 0);
  double w = 2 * PI * hz / PLOT_RATE, g = pow(10.0, gain / 20);

  *ss = db_of(mag(effect(reg, PLOT_RATE, w, zero, one, g)));
  *ms = db_of(mag(effect(reg, PLOT_RATE, w, one, zero, g)));
}

void s3dsim_step(const u8 *reg, double t1, double dt, int n, float *out,
                 double *held, double *rise, double *fall) {
  struct z zero = z_of(0, 0), one = z_of(1, 0), s0, s1, m0, m1;
  double w1 = 2 * PI * 1000 / PLOT_RATE, w4 = 2 * PI * 400 / PLOT_RATE;
  double c[3], hp4, f, r, down, up, lo, hi, g = 1, s, a, b, sum;
  double seen[STEP_LATE];
  int i, k, ms = (int)(dt * 1000 + 0.5), t = 0;
  int limit = (reg[0] & 0x0D) == 0x0D;

  // the S out of the S at 1 kHz and of the M at 400 Hz: a part that the
  // boost's gain doesn't move, and one that it does
  s0 = effect(reg, PLOT_RATE, w1, zero, one, 0);
  s1 = z_sub(effect(reg, PLOT_RATE, w1, zero, one, 1), s0);
  m0 = effect(reg, PLOT_RATE, w4, one, zero, 0);
  m1 = z_sub(effect(reg, PLOT_RATE, w4, one, zero, 1), m0);
  // the limit's detector takes 400 Hz through its high-pass, against
  // 1 kHz, and its levels and speeds, a millisecond's step
  first(DETECT, PLOT_RATE, 1, c);
  hp4 = hp_gain(c, PLOT_RATE, 400) * DETECT_1K;
  s3dsim_levels(reg, STEP_M, rise, fall);
  s3dsim_speed(reg, &f, &r);
  lo = pow(10.0, *rise / 20);
  hi = pow(10.0, *fall / 20);
  down = pow(10.0, -f / 1000 / 20);
  up = pow(10.0, r / 1000 / 20);
  *held = 0;
  for (k = 0; k < STEP_LATE; k++)
    seen[k] = 0;
  for (i = 0; i < n; i++) {
    // the mean of the S out over each step, where a fast limit hunts
    for (k = 0, sum = 0; k < (i ? ms : 1); k++, t++) {
      // the S at 0 dB against the M until t1, then 12 dB lower
      s = t < t1 * 1000 ? 1 : 0.25119;
      a = mag(z_add(s0, z_scale(s1, g))) * s;
      b = mag(z_add(m0, z_scale(m1, g)));
      sum += sqrt(a * a + b * b);
      if (t == (long)(t1 * 1000))
        *held = db_of(g);
      if (!limit)
        continue;
      // the limit follows what it saw a few milliseconds ago
      seen[t % STEP_LATE] = sqrt(a * a + b * b * hp4 * hp4);
      if (seen[(t + 1) % STEP_LATE] > hi)
        g *= down;
      if (seen[(t + 1) % STEP_LATE] < lo)
        g *= up;
      if (g > 1)
        g = 1;
      if (g < FLOOR)
        g = FLOOR;
    }
    out[i] = (float)db_of(sum / (i ? ms : 1));
  }
}
