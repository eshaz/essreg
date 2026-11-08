/*
 * t_wchk checks esswchk's measures and verdicts (src/wchk.c): the rate
 * fit, the sides' levels, the positions a program polls, the count's ring,
 * sb16test's report, each check's verdict, and /sim's card, fixed and old.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <math.h>
#include <string.h>

#include "check.h"
#include "wchk.h"

static char text[WCHK_TEXT];

static void test_fit(void) {
  struct wchk_fit f;
  int i;

  memset(&f, 0, sizeof(f));
  wchk_fit_add(&f, 0, 5);
  wchk_fit_add(&f, 1, 7);
  CHECK(wchk_fit_slope(&f) == 0); // two points: no line yet
  memset(&f, 0, sizeof(f));
  // a block's frames each time it came, the times a few ms late
  for (i = 0; i < 300; i++)
    wchk_fit_add(&f, i * 0.0823 + (i % 7) * 0.001, i * 4096.0);
  CHECK(fabs(wchk_fit_slope(&f) - 4096 / 0.0823) < 4096 / 0.0823 * 2e-4);
}

static void test_sides(void) {
  struct wchk_sides w;
  s16 pcm[2 * 400];
  int i;

  memset(&w, 0, sizeof(w));
  CHECK(wchk_side_db(&w, 0) == -120);
  for (i = 0; i < 400; i++) {
    pcm[2 * i] = (s16)(1000 + 16384 * sin(2 * 3.14159265358979 * i / 40));
    pcm[2 * i + 1] = 1000; // only a mean
  }
  wchk_sides_add(&w, pcm, 400);
  CHECK_EQ(w.frames, 400);
  CHECK(fabs(wchk_side_db(&w, 0) + 6.02) < 0.05); // half of full scale
  CHECK(wchk_side_db(&w, 1) == -120);
}

static void test_pos(void) {
  struct wchk_pos p;
  u32 ms;
  int r;

  wchk_pos_init(&p);
  for (ms = 0; ms <= 1000; ms += 55)
    wchk_pos_add(&p, ms * 176, ms, 176.4);
  r = wchk_position(&p, 1000, text);
  CHECK_EQ(r, WCHK_PASS);
  CHECK(strstr(text, "19 positions over 1000 ms: 0 steps back") != NULL);
  // a step back, and a jump past the rate and the 1024 bytes over it
  wchk_pos_add(&p, 900 * 176, 1100, 176.4);
  wchk_pos_add(&p, 1110 * 176 + 1500, 1110, 176.4);
  r = wchk_position(&p, 1110, text);
  CHECK_EQ(r, WCHK_FAIL);
  CHECK_EQ(p.back, 1);
  CHECK_EQ(p.worst_back, 990 * 176 - 900 * 176);
  CHECK_EQ(p.over, 1);
  CHECK_EQ(p.worst_ms, 10);
  wchk_pos_init(&p);
  wchk_pos_add(&p, 0, 0, 176.4);
  CHECK_EQ(wchk_position(&p, 0, text), WCHK_FAIL); // one position
}

static void test_ring(void) {
  CHECK_EQ(wchk_ring(0x3FFB), 0x4000);
  CHECK_EQ(wchk_ring(0x3FFF), 0x4000);
  CHECK_EQ(wchk_ring(0x4000), 0x8000);
  CHECK_EQ(wchk_ring(0xFFFF), 0x10000L);
  CHECK_EQ(wchk_count_pos(0x3FFF, 0x4000), 0);
  CHECK_EQ(wchk_count_pos(0, 0x4000), 0x3FFF);
  CHECK_EQ(wchk_count_pos(0x4005, 0x4000), 0x3FFA); // past the ring: wraps
}

static void test_sbt(void) {
  struct wchk_sbt s;
  static const char *const report[] = {
      "sb16test 1.6: checks the Sound Blaster 16 that games find",
      "ctl.poll16           PASS  100000 readings, 0 torn",
      "",
      "[summary] done",
      "pass=61 fail=0 known=3 info=40 skip=2",
      NULL};
  int i;

  wchk_sbt_init(&s);
  CHECK_EQ(wchk_sbt_verdict(&s, text), WCHK_FAIL); // no summary
  for (i = 0; report[i]; i++)
    wchk_sbt_line(&s, report[i]);
  CHECK(s.found);
  CHECK_EQ(s.pass, 61);
  CHECK_EQ(s.known, 3);
  CHECK_EQ(wchk_sbt_verdict(&s, text), WCHK_PASS);
  CHECK(strstr(text, "pass=61 fail=0 known=3 info=40; ctl.poll16 ") != NULL);
  wchk_sbt_line(&s, "pass=60 fail=1 known=3 info=40 skip=2 (stopped)");
  CHECK_EQ(wchk_sbt_verdict(&s, text), WCHK_FAIL);
}

// the counts /sim gives over 100,000 readings 10 us apart, through the
// ring found from them, into tears
static void sim_counts(struct tears *t, int old) {
  static u16 pos[250];
  u16 most = 0, c;
  u32 i, ring;
  int j;

  for (i = 0; i < 20000; i++) {
    c = wchk_sim_count(i * 10, 176400.0, 0x2000, i, old);
    if (c > most)
      most = c;
  }
  ring = wchk_ring(most);
  CHECK_EQ(ring, 0x2000);
  tears_init(t, ring, 2, 4096);
  for (i = 0; i < 100000; i += 250) {
    for (j = 0; j < 250; j++)
      pos[j] = wchk_count_pos(
          wchk_sim_count((i + j) * 10, 176400.0, 0x2000, i + j, old), ring);
    tears_add(t, pos, 250);
  }
}

static void test_count(void) {
  struct tears t;

  sim_counts(&t, 0);
  CHECK_EQ(wchk_count(&t, 1, text), WCHK_PASS);
  CHECK(strstr(text, "100000 readings, 0 torn") != NULL);
  sim_counts(&t, 1);
  CHECK(t.torn256 > 300);
  CHECK_EQ(t.torn128 + t.torn_other + t.back, 0);
  CHECK_EQ(wchk_count(&t, 0, text), WCHK_INFO); // ESS's VxD
  CHECK(strstr(text, "; one in 3") != NULL);
  CHECK_EQ(wchk_count(&t, 1, text), WCHK_FAIL); // a fixed VxD that tears
  tears_init(&t, 0x2000, 2, 4096);
  CHECK_EQ(wchk_count(&t, 1, text), WCHK_FAIL); // nothing read
}

static void test_drain(void) {
  u32 runs[3];
  int i;

  for (i = 0; i < 3; i++)
    runs[i] = wchk_sim_drain(i, 0);
  CHECK_EQ(wchk_drain(runs, 3, text), WCHK_PASS);
  CHECK(strstr(text, "back 531, 532, 533 ms after") != NULL);
  for (i = 0; i < 3; i++)
    runs[i] = wchk_sim_drain(i, 1);
  CHECK_EQ(wchk_drain(runs, 3, text), WCHK_FAIL);
  CHECK(strstr(text, "at the DMA's end") != NULL);
  runs[0] = 0, runs[1] = 650, runs[2] = 700; // one never came back
  CHECK_EQ(wchk_drain(runs, 3, text), WCHK_INFO);
  runs[1] = runs[2] = 0;
  CHECK_EQ(wchk_drain(runs, 3, text), WCHK_FAIL);
  CHECK(strstr(text, "never came back") != NULL);
}

static void fm_sides(char side, int old, double *db) {
  static s16 pcm[2 * 2048];
  struct wchk_sides w;

  memset(&w, 0, sizeof(w));
  wchk_sim_fm(pcm, 2048, 0, side, old);
  wchk_sides_add(&w, pcm, 2048);
  wchk_sim_fm(pcm, 2048, 2048, side, old);
  wchk_sides_add(&w, pcm, 2048);
  db[0] = wchk_side_db(&w, 0);
  db[1] = wchk_side_db(&w, 1);
}

static void test_fm(void) {
  double db[2];

  fm_sides('l', 0, db);
  CHECK(fabs(db[0] + 12.3) < 0.2 && db[1] == -120);
  CHECK_EQ(wchk_fm_side(db[0], db[1], 'l', 1, text), WCHK_PASS);
  fm_sides('r', 0, db);
  CHECK_EQ(wchk_fm_side(db[0], db[1], 'r', 1, text), WCHK_PASS);
  fm_sides('l', 1, db); // ESS's ESFM.DRV: on the right
  CHECK_EQ(wchk_fm_side(db[0], db[1], 'l', 0, text), WCHK_KNOWN);
  CHECK(strstr(text, "TrueSides off; on the other side") != NULL);
  CHECK_EQ(wchk_fm_side(db[0], db[1], 'l', 1, text), WCHK_FAIL);
  CHECK_EQ(wchk_fm_side(-120, -120, 'l', 1, text), WCHK_FAIL);
  CHECK(strstr(text, "no FM") != NULL);
  CHECK_EQ(wchk_fm_side(-10, -12, 'r', 1, text), WCHK_FAIL);
  CHECK(strstr(text, "on both sides") != NULL);
  CHECK_EQ(wchk_fm_rate(49819.5, 30, text), WCHK_PASS);
  CHECK(strstr(text, "49819.50 Hz over 30 s, +0 ppm") != NULL);
  CHECK(strstr(text, "49716 Hz is -2077 ppm") != NULL);
  CHECK_EQ(wchk_fm_rate(49716, 30, text), WCHK_FAIL);
  CHECK_EQ(wchk_fm_rate(0, 30, text), WCHK_FAIL);
}

// polls in bursts of 250, 20 us apart, one every 55 ms: a torn count's
// 256 bytes ahead comes back at the next poll, a step back
static void sim_polls(struct wchk_pos *p, int old) {
  u32 i, us;

  wchk_pos_init(p);
  for (i = 0; i < 2000; i++) {
    us = i / 250 * 55000UL + i % 250 * 20;
    wchk_pos_add(p, wchk_sim_pos(us, 176.4, i, old), us / 1000, 176.4);
  }
}

static void test_sim_pos(void) {
  struct wchk_pos p;

  sim_polls(&p, 0);
  CHECK_EQ(wchk_position(&p, 440, text), WCHK_PASS);
  sim_polls(&p, 1);
  CHECK_EQ(wchk_position(&p, 440, text), WCHK_FAIL);
  CHECK_EQ(p.back, 40);
  CHECK(p.worst_back > 250 && p.worst_back <= 256);
}

int main(void) {
  test_fit();
  test_sides();
  test_pos();
  test_ring();
  test_sbt();
  test_count();
  test_drain();
  test_fm();
  test_sim_pos();
  return CHECK_DONE("t_wchk");
}
