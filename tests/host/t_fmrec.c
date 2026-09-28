/*
 * t_fmrec checks the parts of esfmrec that don't need Windows: the WAV
 * header and its repair, the names of a long recording's files, peak
 * levels in dB and the /sim test tone.
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

// the header of a recording that stopped without its last header
static void test_fix(void) {
  u8 h[FMREC_HEADER];
  u32 data = 1;

  fmrec_wav_header(h, FMREC_RATE, 0);
  CHECK_EQ(fmrec_wav_fix(h, 44 + 4000 + 3, &data), 0);
  CHECK_EQ(data, 4000); // whole frames only
  CHECK_EQ(get32(h + 4), 36 + 4000);
  CHECK_EQ(get32(h + 40), 4000);
  // an old length is replaced too
  CHECK_EQ(fmrec_wav_fix(h, 44 + 8, &data), 0);
  CHECK_EQ(get32(h + 40), 8);
  CHECK_EQ(fmrec_wav_fix(h, 44, &data), 0);
  CHECK_EQ(data, 0);
  CHECK_EQ(fmrec_wav_fix(h, 43, &data), -1);
  // another rate or format isn't esfmrec's
  fmrec_wav_header(h, 44100, 0);
  CHECK_EQ(fmrec_wav_fix(h, 1000, &data), -1);
  fmrec_wav_header(h, FMREC_RATE, 0);
  h[22] = 1;
  CHECK_EQ(fmrec_wav_fix(h, 1000, &data), -1);
  memset(h, 0, sizeof(h)); // raw PCM
  CHECK_EQ(fmrec_wav_fix(h, 1000, &data), -1);
}

static void test_parts(void) {
  char out[32];

  CHECK_EQ(fmrec_part_name("C:\\REC\\GAME.WAV", 1, out, sizeof(out)), 0);
  CHECK(!strcmp(out, "C:\\REC\\GAME.WAV"));
  CHECK_EQ(fmrec_part_name("C:\\REC\\GAME.WAV", 2, out, sizeof(out)), 0);
  CHECK(!strcmp(out, "C:\\REC\\GAME_2.WAV"));
  CHECK_EQ(fmrec_part_name("C:\\REC\\LONGNAME.WAV", 2, out, sizeof(out)), 0);
  CHECK(!strcmp(out, "C:\\REC\\LONGNA_2.WAV"));
  CHECK_EQ(fmrec_part_name("C:\\REC\\LONGNAME.WAV", 12, out, sizeof(out)), 0);
  CHECK(!strcmp(out, "C:\\REC\\LONGN_12.WAV"));
  CHECK_EQ(fmrec_part_name("A:TAKE", 3, out, sizeof(out)), 0);
  CHECK(!strcmp(out, "A:TAKE_3"));
  // a dot in a directory name isn't the extension
  CHECK_EQ(fmrec_part_name("C:\\R.1\\FM", 2, out, sizeof(out)), 0);
  CHECK(!strcmp(out, "C:\\R.1\\FM_2"));
  CHECK_EQ(fmrec_part_name("FM.PCM", 99999, out, sizeof(out)), 0);
  CHECK(!strcmp(out, "FM_99999.PCM"));
  // too long for out
  CHECK_EQ(fmrec_part_name("C:\\REC\\GAME.WAV", 2, out, 17), -1);
  CHECK_EQ(fmrec_part_name("C:\\REC\\GAME.WAV", 2, out, 18), 0);
  CHECK_EQ(fmrec_part_name("C:\\REC\\GAME.WAV", 1, out, 15), -1);
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
  test_fix();
  test_parts();
  test_levels();
  test_tone();
  return CHECK_DONE("t_fmrec");
}
