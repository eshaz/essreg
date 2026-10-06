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
#define DETECT 420.0     // the limit's high-pass on the S it follows
#define FLOOR 0.0045     // and the least gain it leaves, -47 dB
#define DETECT_1K 1.0844 // and its gain at 1 kHz brought to 0 dB
#define K_SET 1.643      // a level's part re the M, bit 7 set, +4.3 dB
#define K_CLEAR 0.228    // and bit 7 clear, -12.8 dB
#define RISE 1.08        // 54h's level for the rise, 0.67 dB higher
#define ENV 0.002        // each stage of the envelopes, seconds
// a level's fixed part for each step of its bits 6:0 with 5Ah at 80h:
// 0.01116 of the M the card was measured with, -24 dBFS in each channel,
// as it reaches the effect
#define D_STEP (0.01116 * 0.0625 * (1 + RIGHT) / 2)

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

// the part of the level of 54h or 56h (v) re the M, and its fixed part,
// which 5Ah scales
static double level_k(u8 v) { return v & 0x80 ? K_SET : K_CLEAR; }

static double level_d(u8 v, u8 r5a) { return D_STEP * (v & 0x7F) * r5a / 128; }

void s3dsim_levels(const u8 *reg, double m, double *rise, double *fall) {
  double mi = m * (1 + RIGHT) / 2, a, b;

  // both levels where the effect compares them, against the M there
  a = level_k(reg[2]) * mi + level_d(reg[2], reg[5]);
  b = level_k(reg[3]) * mi + level_d(reg[3], reg[5]);
  *rise = 20 * log10(a * RISE / mi);
  *fall = 20 * log10((b > a ? b : a) / mi);
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
  double fall, rise;
  int i;

  memcpy(s->reg, reg, sizeof(s->reg));
  // each run on the card started with the boost back
  s->gain = 1;
  s->env_m[0] = s->env_m[1] = s->env_s[0] = s->env_s[1] = 0;
  first(200, s->rate, 1, s->hb);
  first(1000, s->rate, 0, s->lb);
  s->g = boost_gain(reg);
  first(DETECT, s->rate, 1, s->db);
  // the envelopes are means of the absolute value, 2 / pi of a tone's
  // amplitude
  for (i = 0; i < 2; i++) {
    s->k[i] = level_k(reg[2 + i]);
    s->d[i] = 2 / PI * level_d(reg[2 + i], reg[5]);
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
        s->env_m[0] += (fabs(m) - s->env_m[0]) * k;
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
