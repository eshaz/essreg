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
#define RIGHT 0.975      // the right channel's level against the left
#define PEAK 8.2         // the boost's gain for 16.7 dB at its peak
#define MODEL 0.70710678 // the model's S, 3 dB under the boost
#define KEEP (1.0 / 32)  // the input's S that the model keeps

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

void s3dsim_limit(const u8 *reg, double *level, double *fall, double *rise) {
  double v = reg[2];

  *level = v <= 0x8F ? -5.6 + 11.1 * v / 0x8F
                     : 5.5 + 4.3 * (v - 0x8F) / (0xFF - 0x8F);
  *fall = 260 * pow(2.0, -reg[4] / 65.0);
  *rise = reg[4] & 0x80 ? *fall / 16 : *fall;
}

void s3dsim_reset(struct s3dsim *s, u32 rate) {
  static const u8 off[6] = {0x04, 0x3F, 0x8F, 0x95, 0x94, 0x80};

  memset(s, 0, sizeof(*s));
  s->rate = rate;
  s->seed = 12345;
  s->gain = 1;
  s3dsim_regs(s, off);
}

void s3dsim_regs(struct s3dsim *s, const u8 *reg) {
  double level, fall, rise;

  memcpy(s->reg, reg, sizeof(s->reg));
  // each run on the card started with the boost back
  s->gain = 1;
  s->env_m = s->env_s = 0;
  first(200, s->rate, 1, s->hb);
  first(1000, s->rate, 0, s->lb);
  s->g = boost_gain(reg);
  s3dsim_limit(reg, &level, &fall, &rise);
  s->lim = pow(10.0, level / 20);
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
  double up = 1 - exp(-1.0 / (0.005 * s->rate));
  double down = 1 - exp(-1.0 / (0.05 * s->rate));
  double l, r, m, sd, x, y, b;
  u16 i;

  for (i = 0; i < n; i++) {
    l = in[2 * i] / 32768.0;
    r = in[2 * i + 1] / 32768.0 * RIGHT;
    m = (l + r) / 2;
    sd = (l - r) / 2;
    if (on) {
      // the boost: high-pass, then low-pass, of the S, or of the M for
      // the model
      x = model ? m : sd;
      y = s->hb[0] * x + s->hb[1] * s->hx - s->hb[2] * s->hy;
      s->hx = x;
      s->ly = s->lb[0] * y + s->lb[1] * s->hy - s->lb[2] * s->ly;
      s->hy = y;
      b = s->g * s->ly * (model ? MODEL : 1);
      if (s->reg[0] & 1) {
        // the limit: follow the M in and the S out, and move the boost's
        // gain down while the S passes the level, and up otherwise
        b *= s->gain;
        x = (model ? sd * KEEP : sd) + b;
        s->env_m += (fabs(m) - s->env_m) * (fabs(m) > s->env_m ? up : down);
        s->env_s += (fabs(x) - s->env_s) * (fabs(x) > s->env_s ? up : down);
        // (over -80 dBFS, so that silence doesn't count)
        s->gain *= s->env_s > s->lim * s->env_m + 1e-4 ? s->f_down : s->f_up;
        if (s->gain > 1)
          s->gain = 1;
        if (s->gain < 0.001)
          s->gain = 0.001;
      } else {
        s->gain = 1;
      }
      sd = (model ? sd * KEEP : sd) + b;
    }
    // the path's gain and delay, then the recording's noise
    out[2 * i] = s->dl[0][s->dpos];
    out[2 * i + 1] = s->dl[1][s->dpos];
    s->dl[0][s->dpos] = to_s16(GAIN * (m + sd) + noise(s));
    s->dl[1][s->dpos] = to_s16(GAIN * (m - sd) + noise(s));
    s->dpos = (s->dpos + 1) % S3DSIM_DELAY;
  }
}

// b0 + b1 z^-1 over 1 + a1 z^-1 at z = e^(jw)
static void at(const double *c, double w, double *re, double *im) {
  double nr = c[0] + c[1] * cos(w), ni = -c[1] * sin(w);
  double dr = 1 + c[2] * cos(w), di = -c[2] * sin(w), d = dr * dr + di * di;

  *re = (nr * dr + ni * di) / d;
  *im = (ni * dr - nr * di) / d;
}

void s3dsim_response(const u8 *reg, u32 rate, double hz, double *re,
                     double *im) {
  double hb[3], lb[3], w = 2 * PI * hz / rate, hr, hi, lr, li, br, bi;
  double mi = (1 + RIGHT) / 2, cross = (1 - RIGHT) / 2;
  int i;

  // the M and S that reach the effect from an M in and an S in, against
  // the M that an M in brings with the effect off
  for (i = 0; i < 4; i++)
    im[i] = 0;
  re[0] = re[1] = 1;
  re[2] = re[3] = cross / mi;
  if ((reg[0] & 0x0C) != 0x0C)
    return;
  first(200, rate, 1, hb);
  first(1000, rate, 0, lb);
  at(hb, w, &hr, &hi);
  at(lb, w, &lr, &li);
  br = boost_gain(reg) * (hr * lr - hi * li);
  bi = boost_gain(reg) * (hr * li + hi * lr);
  if (reg[0] & 2) {
    // the model: the S from the M, and a 32nd of the input's S
    re[1] = KEEP + MODEL * br * cross / mi;
    im[1] = MODEL * bi * cross / mi;
    re[2] = KEEP * cross / mi + MODEL * br;
    im[2] = MODEL * bi;
  } else {
    // S out is S in plus its boost
    re[1] = 1 + br;
    im[1] = bi;
    re[2] = cross / mi * (1 + br);
    im[2] = cross / mi * bi;
  }
}
