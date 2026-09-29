/*
 * t_regs checks the essreg register functions that the original essreg
 * didn't have, or that now work differently, so t_trace can't compare them:
 * the 3-D limit bit (mixer 50h bit 0) and the Audio 1 rate with mixer 71h
 * bit 5 set.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>

#include "esshw.h"
#include "regs.h"
#include "simhw.h"

static int checks, failures;

#define CHECK(cond)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(cond)) {                                                             \
      failures++;                                                              \
      fprintf(stderr, "t_regs: line %d: %s\n", __LINE__, #cond);               \
    }                                                                          \
  } while (0)

int main(void) {
  // essreg prints every value, only the result goes to stderr
  if (!freopen("/dev/null", "w", stdout))
    return 1;
  simhw_reset(0x220, 0x250);
  simhw_attach();
  esshw.flags = ESSHW_LEGACY;

  simhw.mixer[0x50] = 0x0C;
  CHECK(get_3d_limit() == 0);
  set_3d_limit(1);
  CHECK(simhw.mixer[0x50] == 0x0D); // enable and reset bits kept
  CHECK(get_3d_limit() == 1);
  set_3d_limit(0);
  CHECK(simhw.mixer[0x50] == 0x0C);

  // the 3-D mode and level keep the limit bit
  set_3d_limit(1);
  set_3d_mode(0);
  CHECK(simhw.mixer[0x50] == 0x05);
  set_3d_level(20);
  CHECK(simhw.mixer[0x50] == 0x0D);
  CHECK((simhw.mixer[0x52] & 0x3F) == 20);

  // A1h: the original formula, or like 70h with mixer 71h bit 5 (DS p.64)
  simhw.ext_mode = 1;
  simhw.ctrl[0xA1 - 0xA0] = 0xF0;
  simhw.mixer[0x71] = 0x00;
  CHECK(get_audio_1_sample_rate() == 49718);
  simhw.mixer[0x71] = 0x20;
  CHECK(get_audio_1_sample_rate() == 48000);
  simhw.ctrl[0xA1 - 0xA0] = 0x6E;
  CHECK(get_audio_1_sample_rate() == 44100);

  fprintf(stderr, "t_regs: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
