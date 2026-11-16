/*
 * t_tears checks the torn-reading finder of sb16test, essprobe and esswchk
 * (src/tears.c): torn readings by size, steps back and forward between the
 * others, the ring's wrap, readings fed in pieces, and pauses.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include "check.h"
#include "tears.h"

#define RING 8192

// n readings rising by step from start, wrapped at the ring
static void ramp(u16 *p, unsigned n, u32 start, unsigned step) {
  unsigned i;
  for (i = 0; i < n; i++)
    p[i] = (u16)((start + (u32)i * step) % RING);
}

static void test_clean(void) {
  struct tears t;
  u16 p[400];

  ramp(p, 400, RING - 300, 3); // across the wrap
  tears_init(&t, RING, 4, 64);
  tears_add(&t, p, 400);
  CHECK_EQ(t.readings, 400);
  CHECK_EQ(tears_torn(&t), 0);
  CHECK_EQ(t.back, 0);
  CHECK_EQ(t.jumps, 0);
  CHECK_EQ(t.worst, 0);
}

static void test_sizes(void) {
  struct tears t;
  u16 p[100];

  ramp(p, 100, 1000, 2);
  p[20] = (u16)(p[20] + 256); // the 8237's own tear
  p[50] = (u16)(p[50] + 128); // a front's, halved into words
  p[80] = (u16)(p[80] + 1000);
  tears_init(&t, RING, 4, 64);
  tears_add(&t, p, 100);
  CHECK_EQ(t.torn256, 1);
  CHECK_EQ(t.torn128, 1);
  CHECK_EQ(t.torn_other, 1);
  CHECK_EQ(tears_torn(&t), 3);
  CHECK_EQ(t.worst, 1000);
  CHECK_EQ(t.back, 0); // the way back from a torn one isn't a step back
  CHECK_EQ(t.jumps, 0);
  // a reading under the threshold isn't torn, and is a step instead
  ramp(p, 100, 1000, 2);
  p[40] = (u16)(p[40] + 90);
  tears_init(&t, RING, 4, 64);
  tears_add(&t, p, 100);
  CHECK_EQ(tears_torn(&t), 0);
  CHECK_EQ(t.jumps, 1);
  CHECK_EQ(t.back, 1);
}

static void test_wrap(void) {
  struct tears t;
  u16 p[60];

  // a tear just before the ring wraps, landing past the wrap
  ramp(p, 60, RING - 60, 2);
  p[20] = (u16)((p[20] + 256) % RING);
  tears_init(&t, RING, 4, 64);
  tears_add(&t, p, 60);
  CHECK_EQ(t.torn256, 1);
  CHECK_EQ(t.back + t.jumps, 0);
  // and a tear behind, at the start of the ring
  ramp(p, 60, 10, 2);
  p[30] = (u16)((p[30] + RING - 256) % RING);
  tears_init(&t, RING, 4, 64);
  tears_add(&t, p, 60);
  CHECK_EQ(t.torn256, 1);
  CHECK_EQ(t.worst, -256);
}

static void test_steps(void) {
  struct tears t;
  u16 p[100];

  // the position going back for good, and a jump that stays
  ramp(p, 50, 3000, 2);
  ramp(p + 50, 50, 3000 + 98 - 20, 2);
  tears_init(&t, RING, 4, 64);
  tears_add(&t, p, 100);
  CHECK_EQ(t.back, 1);
  CHECK_EQ(tears_torn(&t), 0);
  ramp(p, 50, 3000, 2);
  ramp(p + 50, 50, 3000 + 98 + 500, 2);
  tears_init(&t, RING, 4, 64);
  tears_add(&t, p, 100);
  CHECK_EQ(t.jumps, 1);
  CHECK_EQ(tears_torn(&t), 0);
  // a small step back within the slack
  ramp(p, 100, 3000, 2);
  p[60] = (u16)(p[59] - 3);
  p[61] = (u16)(p[59] - 1);
  tears_init(&t, RING, 4, 64);
  tears_add(&t, p, 100);
  CHECK_EQ(t.back, 0);
}

static void test_pieces(void) {
  struct tears one, many;
  u16 p[300];
  unsigned i;

  ramp(p, 300, 5000, 3);
  p[99] = (u16)(p[99] + 256); // on the edge of a piece
  p[100] = (u16)(p[100]);
  p[200] = (u16)(p[200] + 128);
  tears_init(&one, RING, 4, 64);
  tears_add(&one, p, 300);
  tears_init(&many, RING, 4, 64);
  for (i = 0; i < 300; i += 7)
    tears_add(&many, p + i, i + 7 <= 300 ? 7 : 300 - i);
  CHECK_EQ(many.readings, one.readings);
  CHECK_EQ(many.torn256, one.torn256);
  CHECK_EQ(many.torn128, one.torn128);
  CHECK_EQ(many.back, one.back);
  CHECK_EQ(many.jumps, one.jumps);
  CHECK_EQ(one.torn256, 1);
  CHECK_EQ(one.torn128, 1);
}

static void test_break(void) {
  struct tears t;
  u16 p[100];

  // a pause while the transfer goes on: no step, no tear
  ramp(p, 50, 100, 2);
  ramp(p + 50, 50, 2100, 2);
  tears_init(&t, RING, 4, 64);
  tears_add(&t, p, 50);
  tears_break(&t);
  tears_add(&t, p + 50, 50);
  CHECK_EQ(t.jumps, 0);
  CHECK_EQ(tears_torn(&t), 0);
  // neighbours far apart, a fast transfer or slow readings: nothing torn
  ramp(p, 100, 100, 200);
  p[50] = (u16)(p[50] + 256);
  tears_init(&t, RING, 4, 1000);
  tears_add(&t, p, 100);
  CHECK_EQ(tears_torn(&t), 0);
}

int main(void) {
  test_clean();
  test_sizes();
  test_wrap();
  test_steps();
  test_pieces();
  test_break();
  return CHECK_DONE("t_tears");
}
