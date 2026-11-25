/*
 * Torn readings of a DMA count (tears.h).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include "tears.h"

// d into the ring's half-open range around 0: (-ring/2, ring/2]
static s32 tears_wrap(const struct tears *t, s32 d) {
  s32 r = (s32)t->ring;

  d %= r;
  if (d < 0)
    d += r;
  if (d > r / 2)
    d -= r;
  return d;
}

void tears_init(struct tears *t, u32 ring, u16 slack, u16 jump) {
  t->ring = ring;
  t->slack = slack;
  t->jump = jump;
  t->prev[0] = t->prev[1] = 0;
  t->have = 0;
  t->prev_torn = 0;
  t->readings = 0;
  t->torn128 = t->torn256 = t->torn_other = 0;
  t->back = t->jumps = 0;
  t->worst = 0;
}

void tears_break(struct tears *t) {
  t->have = 0;
  t->prev_torn = 0;
}

u32 tears_torn(const struct tears *t) {
  return t->torn128 + t->torn256 + t->torn_other;
}

void tears_add(struct tears *t, const u16 *pos, unsigned n) {
  unsigned i;

  for (i = 0; i < n; i++) {
    u16 a, b, c = pos[i];
    s32 ac, ab, dev, mag;
    int torn = 0;

    t->readings++;
    if (t->have < 2) {
      t->prev[t->have++] = c;
      continue;
    }
    a = t->prev[0];
    b = t->prev[1];
    ac = tears_wrap(t, (s32)c - (s32)a);
    ab = tears_wrap(t, (s32)b - (s32)a);
    dev = ab - ac / 2; // b against its neighbours' mean
    mag = dev < 0 ? -dev : dev;
    if (ac >= -TEARS_NEAR && ac <= TEARS_NEAR && mag >= TEARS_NEAR) {
      torn = 1;
      if (mag < 2 * TEARS_NEAR)
        t->torn128++;
      else if (mag < 4 * TEARS_NEAR)
        t->torn256++;
      else
        t->torn_other++;
      if (mag > (t->worst < 0 ? -t->worst : t->worst))
        t->worst = dev;
    }
    if (!t->prev_torn && !torn) {
      if (ab < -(s32)t->slack)
        t->back++;
      else if (ab > (s32)t->jump)
        t->jumps++;
    }
    t->prev_torn = (u8)torn;
    t->prev[0] = b;
    t->prev[1] = c;
  }
}
