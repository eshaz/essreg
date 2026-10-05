/*
 * A made-up 3-D effect for the measurement's tests and for /sim (see
 * s3dsim.h).
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
#define LIMIT 0.7 // the boost stays under this times the M

// the coefficients for the registers: high-pass b0 b1 b2 a1 a2 (biquad,
// Q 0.707), low-pass b0 b1 a1 and all-pass c (first order)
static void design(const u8 *reg, u32 rate, double *hb, double *lb,
                   double *ab) {
  double fc = 150.0 + 10.0 * (reg[2] & 0x7F);
  double w = 2 * PI * fc / rate, cw = cos(w), al = sin(w) / (2 * 0.70710678);
  double a0 = 1 + al, k;

  hb[0] = (1 + cw) / 2 / a0;
  hb[1] = -(1 + cw) / a0;
  hb[2] = (1 + cw) / 2 / a0;
  hb[3] = -2 * cw / a0;
  hb[4] = (1 - al) / a0;
  k = tan(PI * 4000.0 / rate);
  lb[0] = lb[1] = k / (1 + k);
  lb[2] = (k - 1) / (k + 1);
  k = tan(PI * 1000.0 / rate);
  ab[0] = (k - 1) / (k + 1);
}

void s3dsim_reset(struct s3dsim *s, u32 rate) {
  static const u8 off[6] = {0x04, 0x3F, 0x8F, 0x95, 0x94, 0x80};

  memset(s, 0, sizeof(*s));
  s->rate = rate;
  s->seed = 12345;
  s3dsim_regs(s, off);
}

void s3dsim_regs(struct s3dsim *s, const u8 *reg) {
  memcpy(s->reg, reg, sizeof(s->reg));
  design(reg, s->rate, s->hb, s->lb, s->ab);
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
  int on = (s->reg[0] & 0x0C) == 0x0C;
  double k = (s->reg[1] & 0x3F) / 63.0;
  double up = 1 - exp(-1.0 / (0.005 * s->rate));
  double down = 1 - exp(-1.0 / ((0.02 + 0.01 * (s->reg[4] & 0x7F)) * s->rate));
  double m, sd, b, y, a, l, r;
  u16 i;

  for (i = 0; i < n; i++) {
    l = in[2 * i] / 32768.0;
    r = in[2 * i + 1] / 32768.0;
    m = (l + r) / 2;
    sd = (l - r) / 2;
    if (on) {
      // the boost: high-pass, then the low-pass while 56h bit 4 is set
      y = s->hb[0] * sd + s->hb[1] * s->hx[0] + s->hb[2] * s->hx[1] -
          s->hb[3] * s->hy[0] - s->hb[4] * s->hy[1];
      s->hx[1] = s->hx[0];
      s->hx[0] = sd;
      s->hy[1] = s->hy[0];
      s->hy[0] = y;
      b = y;
      if (s->reg[3] & 0x10) {
        y = s->lb[0] * b + s->lb[1] * s->lx - s->lb[2] * s->ly;
        s->lx = b;
        s->ly = y;
        b = y;
      }
      b *= s->reg[2] & 0x80 ? 2 * k : -2 * k; // 54h bit 7, its sign
      if (s->reg[0] & 1) {
        // the limit: follow both levels, with the release that 58h sets,
        // and scale the boost down when it passes 0.7 times the M
        s->env_m += (fabs(m) - s->env_m) * (fabs(m) > s->env_m ? up : down);
        s->env_b += (fabs(b) - s->env_b) * (fabs(b) > s->env_b ? up : down);
        a = s->env_b > LIMIT * s->env_m && s->env_b > 1e-9
                ? LIMIT * s->env_m / s->env_b
                : 1.0;
        b *= a;
      }
      sd += b;
      if (s->reg[0] & 2) {
        // the model: the M through an all-pass, as width from mono
        y = s->ab[0] * m + s->ax - s->ab[0] * s->ay;
        s->ax = m;
        s->ay = y;
        sd += 1.5 * k * y;
      }
      if (s->reg[5] & 1)
        m *= 1.12201845; // +1 dB
    }
    // the path's gain and delay, then the recording's noise
    out[2 * i] = s->dl[0][s->dpos];
    out[2 * i + 1] = s->dl[1][s->dpos];
    s->dl[0][s->dpos] = to_s16(GAIN * (m + sd) + noise(s));
    s->dl[1][s->dpos] = to_s16(GAIN * (m - sd) + noise(s));
    s->dpos = (s->dpos + 1) % S3DSIM_DELAY;
  }
}

// num / den of a section of up to second order at z = e^(jw)
static void at(double b0, double b1, double b2, double a1, double a2, double w,
               double *re, double *im) {
  double c1 = cos(w), s1 = sin(w), c2 = cos(2 * w), s2 = sin(2 * w);
  double nr = b0 + b1 * c1 + b2 * c2, ni = -b1 * s1 - b2 * s2;
  double dr = 1 + a1 * c1 + a2 * c2, di = -a1 * s1 - a2 * s2;
  double d = dr * dr + di * di;

  *re = (nr * dr + ni * di) / d;
  *im = (ni * dr - nr * di) / d;
}

void s3dsim_response(const u8 *reg, u32 rate, double hz, double *re,
                     double *im) {
  double hb[5], lb[3], ab[3], w = 2 * PI * hz / rate;
  double k = (reg[1] & 0x3F) / 63.0, g, hr, hi, lr, li, br, bi;
  int i;

  for (i = 0; i < 4; i++)
    re[i] = im[i] = 0;
  re[0] = re[1] = 1;
  if ((reg[0] & 0x0C) != 0x0C)
    return;
  design(reg, rate, hb, lb, ab);
  at(hb[0], hb[1], hb[2], hb[3], hb[4], w, &hr, &hi);
  br = hr;
  bi = hi;
  if (reg[3] & 0x10) {
    at(lb[0], lb[1], 0, lb[2], 0, w, &lr, &li);
    br = hr * lr - hi * li;
    bi = hr * li + hi * lr;
  }
  // the boost, taken away while 54h bit 7 is clear
  g = reg[2] & 0x80 ? 2 * k : -2 * k;
  re[1] = 1 + g * br;
  im[1] = g * bi;
  if (reg[0] & 2) {
    // the model: the M through the all-pass
    at(ab[0], 1, 0, ab[0], 0, w, &re[2], &im[2]);
    re[2] *= 1.5 * k;
    im[2] *= 1.5 * k;
  }
  if (reg[5] & 1)
    re[0] = 1.12201845;
}
