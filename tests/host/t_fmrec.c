/*
 * t_fmrec checks the parts of esfmrec that don't need Windows: the WAV
 * header, peak levels in dB and the /sim test tone.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "fmrec.h"

static u32 get32(const u8 *p) {
  return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static void test_header(void) {
  u8 h[FMREC_HEADER];

  fmrec_wav_header(h, FMREC_RATE, 1000);
  CHECK(!memcmp(h, "RIFF", 4));
  CHECK_EQ(get32(h + 4), 1036);
  CHECK(!memcmp(h + 8, "WAVEfmt ", 8));
  CHECK_EQ(get32(h + 16), 16);
  CHECK_EQ(h[20] | h[21] << 8, 1);
  CHECK_EQ(h[22] | h[23] << 8, 2);
  CHECK_EQ(get32(h + 24), 49716);
  CHECK_EQ(get32(h + 28), 49716 * 4);
  CHECK_EQ(h[32] | h[33] << 8, 4);
  CHECK_EQ(h[34] | h[35] << 8, 16);
  CHECK(!memcmp(h + 36, "data", 4));
  CHECK_EQ(get32(h + 40), 1000);
}

static void test_levels(void) {
  s16 pcm[8] = {100, -200, -32768, 5, 32767, 0, -7, 30000};
  u16 l, r;

  fmrec_peaks(pcm, 4, &l, &r);
  CHECK_EQ(l, 32768);
  CHECK_EQ(r, 30000);
  fmrec_peaks(pcm, 1, &l, &r);
  CHECK_EQ(l, 100);
  CHECK_EQ(r, 200);
  CHECK_EQ(fmrec_db10(0), -999);
  CHECK_EQ(fmrec_db10(32768), 0);
  CHECK_EQ(fmrec_db10(32767), 0);
  CHECK_EQ(fmrec_db10(16384), -60); // 6.0 dB below full scale
  CHECK_EQ(fmrec_db10(3277), -200);
  CHECK_EQ(fmrec_db10(328), -400);
  CHECK_EQ(fmrec_db10(1), -903);
}

// rising zero crossings of one channel
static int crossings(const s16 *pcm, long frames, int ch) {
  long i;
  int n = 0;

  for (i = 1; i < frames; i++)
    n += pcm[2 * (i - 1) + ch] < 0 && pcm[2 * i + ch] >= 0;
  return n;
}

static void test_tone(void) {
  static s16 pcm[2 * FMREC_RATE];
  u32 phase[2] = {0, 0};
  u16 l, r, done;

  // one second, in the pieces a timer would make
  for (done = 0; done < FMREC_RATE; done += 1000)
    fmrec_tone(pcm + 2 * done,
               (u16)(FMREC_RATE - done < 1000 ? FMREC_RATE - done : 1000),
               phase);
  CHECK(abs(crossings(pcm, FMREC_RATE, 0) - 1000) <= 1);
  CHECK(abs(crossings(pcm, FMREC_RATE, 1) - 500) <= 1);
  fmrec_peaks(pcm, 49716, &l, &r);
  CHECK(l >= 16380 && l <= 16384);
  CHECK(r >= 16380 && r <= 16384);
}

int main(void) {
  test_header();
  test_levels();
  test_tone();
  return CHECK_DONE("t_fmrec");
}
