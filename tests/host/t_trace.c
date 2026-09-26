/*
 * t_trace prints the port accesses of essreg's register functions.
 *
 * tests/run_tests.py builds it twice, against the original src/regs.c
 * (from git history, -DTRACE_OLD) and against the refactored regs.c on
 * esshw, and requires the two port logs to be identical. That proves the
 * refactor kept the legacy protocol. Functions whose behavior was fixed on
 * purpose are not traced here (they have their own tests).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>

#include "regs.h"
#include "simhw.h"

#ifndef TRACE_OLD
#include "esshw.h"
#endif

static void ops(void) {
  get_mono_in();
  set_mono_in(1);
  set_mono_in(0);
  get_mono_in_level();
  get_3d_mode();
  set_3d_mode(1);
  get_3d_level();
  set_3d_level(20);
  set_3d_level(0);
  get_telegaming_mode();
  set_telegaming_mode(1);
  get_mic_preamp();
  set_mic_preamp(1);
  get_digital_record();
  set_digital_record(1);
  get_fm_sync_audio_2();
  set_fm_sync_audio_2(1);
  get_analog_stays_on();
  set_analog_stays_on(0);
  set_analog_stays_on(1);
  get_fm_reset();
  set_fm_reset(1);
  set_fm_reset(0);
  get_digital_power_down();
  calibrate_op_amp();
  simhw.ext_mode = 1;
  simhw.ctrl[0xA1 - 0xA0] = 0xF0;
  simhw.ctrl[0xA2 - 0xA0] = 0xF8;
  simhw.ctrl[0xBA - 0xA0] = 0x03;
  get_audio_1_sample_rate();
  get_audio_1_filter_rate();
  get_audio_2_filter_rate();
  get_adc_offset_left();
}

int main(void) {
  int i;

  simhw_reset(0x220, 0x250);
#ifndef TRACE_OLD
  simhw_attach();
  esshw.flags = ESSHW_LEGACY;
#endif
  simhw.mixer[0x7d] = 0x04;
  simhw.mixer[0x6d] = 0x5a;
  simhw.mixer[0x52] = 0xc5;
  simhw.mixer[0x72] = 0xf0;
  ops();
  fflush(stdout);
  for (i = 0; i < simhw.nlog; i++)
    fprintf(stderr, "%c %03X %02X\n", simhw.log[i].dir, simhw.log[i].port,
            simhw.log[i].value);
  return 0;
}
