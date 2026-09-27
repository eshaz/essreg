/*
 * t_regs checks the essreg register functions that the original essreg
 * didn't have, so t_trace can't compare them: the 3-D limit bit (mixer 50h
 * bit 0).
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

  fprintf(stderr, "t_regs: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
