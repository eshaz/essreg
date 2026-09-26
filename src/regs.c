#include <i86.h>
#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "regs.h"

int read_audio_reg(unsigned int reg_addr) {
  // PnP register of logical device 1 (audio); restores the index and LDN
  return esshw_pnp_read(1, (u8)reg_addr);
}

int read_mixer_reg(unsigned int reg_addr) {
  return esshw_mixer_read((u8)reg_addr);
}

void write_mixer_reg(unsigned int reg_addr, unsigned char reg_value) {
  esshw_mixer_write((u8)reg_addr, reg_value);
}

int read_controller_reg(unsigned char reg_addr) {
  return esshw_ctrl_read(reg_addr);
}

int write_controller_reg(unsigned char reg_addr, unsigned char reg_value) {
  int err = esshw_ctrl_write(reg_addr, reg_value);

  if (err < 0) {
    printf("Timeout waiting for controller register ready flag.\n");
    return -1;
  }
  return 0;
}

void set_safe_protocol(unsigned char on_off) {
  esshw.flags = on_off ? ESSHW_SAFE : ESSHW_LEGACY;
  printf("Safe DSP protocol %s\n", on_off ? "Enabled" : "Disabled");
}

unsigned char get_mono_in() {
  unsigned char on_off = read_mixer_reg(0x7d) & 0x01;
  if (on_off) {
    printf("Mono-In Enabled\n");
  } else {
    printf("Mono-In Disabled\n");
  }
  return on_off;
}

void set_mono_in(unsigned char on_off) {
  unsigned char original_value = read_mixer_reg(0x7d);
  write_mixer_reg(0x7d, (original_value & 0xfe) | (on_off ? 0x01 : 0x00));
  get_mono_in();
}

unsigned char get_mono_in_level() {
  unsigned char level = read_mixer_reg(0x6d);
  unsigned char left = level >> 4;
  unsigned char right = level & 0x0f;
  printf("Mono-In Level: L: %u (%d%%) R: %u (%d%%) \n", left, left * 100 / 0x0f,
         right, right * 100 / 0x0f);
  return level;
}

void set_mono_in_level(unsigned char level) {
  if (level > 0x0f)
    level = 0x0f;
  write_mixer_reg(0x6d, (level << 4) | level);
  get_mono_in_level();
}

void set_mono_in_level_pct(unsigned char level_pct) {
  set_mono_in_level((level_pct + 1) * 0x0f / 100);
}

unsigned char get_digital_power_down() {
  unsigned char value = esshw_port_read(0x06) & 0x08;

  printf("Digital Power Down ");
  if (value) {
    printf("Disabled\n");
  } else {
    printf("Enabled\n");
  }

  return value;
}

unsigned char get_analog_stays_on() {
  unsigned char value = esshw_port_read(0x07) & 0x08;

  printf("Analog Stays On ");
  if (value) {
    printf("Enabled\n");
  } else {
    printf("Disabled\n");
  }

  return value;
}

void set_analog_stays_on(unsigned char on_off) {
  unsigned char value = esshw_port_read(0x07);

  value = (value & 0xf7) | (on_off ? 0x08 : 0x00);

  esshw_port_write(0x07, value);

  get_analog_stays_on();
}

unsigned char get_fm_reset() {
  unsigned char value = esshw_port_read(0x07) & 0x20;

  printf("FM Reset ");
  if (value) {
    printf("Held\n");
  } else {
    printf("Released\n");
  }

  return value;
}

void set_fm_reset(unsigned char on_off) {
  unsigned char value = esshw_port_read(0x07);

  value = (value & 0xdf) | (on_off ? 0x20 : 0x00);

  esshw_port_write(0x07, value);

  get_fm_reset();
}

unsigned char get_telegaming_mode() {
  unsigned char on_off = read_mixer_reg(0x48) & 0x02;
  if (on_off) {
    printf("Telegaming Enabled\n");
  } else {
    printf("Telegaming Disabled\n");
  }
  return on_off;
}

void set_telegaming_mode(unsigned char on_off) {
  unsigned char original_value = read_mixer_reg(0x48);
  write_mixer_reg(0x48, (original_value & 0xfd) | (on_off ? 0x02 : 0x00));
  get_telegaming_mode();
}

unsigned char get_3d_mode() {
  unsigned char on_off = read_mixer_reg(0x50) & 0x08;
  if (on_off) {
    printf("3D Enabled\n");
  } else {
    printf("3D Disabled\n");
  }
  return on_off;
}

void set_3d_mode(unsigned char on_off) {
  unsigned char original_value = read_mixer_reg(0x50);
  // 0x00 3d disable, hold on reset
  // 0x04 3d disable, release from reset
  // 0x08 3d enable, hold on reset
  // 0x0c 3d enable, release from reset
  write_mixer_reg(0x50, (original_value & 0xf3) | (on_off ? 0x0c : 0x04));
  get_3d_mode();
}

unsigned char get_3d_level() {
  unsigned char level = read_mixer_reg(0x52) & 0x3f;
  printf("3D Level: %u (%d%%)\n", level, level * 100 / 0x3f);
  return level;
}

void set_3d_level(unsigned char level) {
  unsigned char original_value = read_mixer_reg(0x52);

  if (level > 0) {
    set_3d_mode(1);
  } else {
    set_3d_mode(0);
  }
  if (level > 0x3f)
    level = 0x3f;
  write_mixer_reg(0x52, (original_value & 0xc0) | (level & 0x3f));
  get_3d_level();
}

void set_3d_level_pct(unsigned char level_pct) {
  set_3d_level((level_pct + 1) * 0x3f / 100);
}

void calibrate_op_amp() {
  write_mixer_reg(0x65, 0x01);
  delay(200);
  printf("Op-amp calibrated\n");
}

unsigned char get_mic_preamp() {
  unsigned char on_off = read_mixer_reg(0x7d) & 0x08;
  if (on_off) {
    printf("Mic Preamp Enabled\n");
  } else {
    printf("Mic Preamp Disabled\n");
  }
  return on_off;
}

void set_mic_preamp(unsigned char on_off) {
  unsigned char original_value = read_mixer_reg(0x7d);
  write_mixer_reg(0x7d, (original_value & 0xf7) | (on_off ? 0x08 : 0x00));
  get_mic_preamp();
}

unsigned char get_digital_record() {
  unsigned char on_off = read_mixer_reg(0x7f) & 0x10;
  if (on_off) {
    printf("FM,IIS,ES689 Digital Record Enabled\n");
  } else {
    printf("FM,IIS,ES689 Digital Record Disabled\n");
  }
  return on_off;
}

void set_digital_record(unsigned char on_off) {
  unsigned char original_value = read_mixer_reg(0x7f);
  write_mixer_reg(0x7f, (original_value & 0xef) | (on_off ? 0x10 : 0x00));

  get_digital_record();
}

unsigned char get_fm_sync_audio_2() {
  unsigned char on_off = read_mixer_reg(0x71) & 0x01;
  if (on_off) {
    printf("FM,IIS,ES689 Digital Sync Enabled\n");
  } else {
    printf("FM,IIS,ES689 Digital Sync Disabled\n");
  }
  return on_off;
}

void set_fm_sync_audio_2(unsigned char on_off) {
  unsigned char original_value = read_mixer_reg(0x71);
  write_mixer_reg(0x71, (original_value & 0xfe) | (on_off ? 0x01 : 0x00));

  get_fm_sync_audio_2();
}

unsigned long get_audio_1_sample_rate() {
  int reg = read_controller_reg(0xa1);
  unsigned long rate;

  if (reg < 0) {
    printf("Audio 1 Sample Rate: %s\n", esshw_strerror(reg));
    return 0;
  }
  rate = reg & 0x80 ? 795500UL / (256 - reg)  //  rate > 22kHz
                    : 397700UL / (128 - reg); // rate <= 22kHz
  printf("Audio 1 Sample Rate: %lu Hz\n", rate);
  return rate;
}

unsigned long get_audio_2_sample_rate() {
  int reg = read_mixer_reg(0x70);
  unsigned long master_clock = reg & 0x80
                                   ? 768000UL  //  48kHz, 32kHz, 16kHz, 8kHz
                                   : 793800UL; // 44.1kHz, 22.05kHz, etc.
  unsigned long rate = master_clock / (128 - (reg & 0x7f));

  printf("Audio 2 Sample Rate: %lu Hz\n", rate);
  return rate;
}

static unsigned long calc_filter_rate(unsigned char reg) {
  return 7160000UL / (256 - reg);
}

unsigned long get_audio_1_filter_rate() {
  int reg = read_controller_reg(0xa2);
  unsigned long rate;

  if (reg < 0) {
    printf("Audio 1 Filter Rate: %s\n", esshw_strerror(reg));
    return 0;
  }
  rate = calc_filter_rate((unsigned char)reg);
  printf("Audio 1 Filter Rate: %lu Hz\n", rate);
  return rate;
}

unsigned long get_audio_2_filter_rate() {
  unsigned long rate = calc_filter_rate((unsigned char)read_mixer_reg(0x72));

  printf("Audio 2 Filter Rate: %lu Hz\n", rate);
  return rate;
}

// ADC offset adjust (BAh/BBh bits 4:0, DS p.71):
// bit 4 = 0: offset = 64 * bits[3:0]; bit 4 = 1: -64 * (bits[3:0] + 1)
static int calc_offset_value(unsigned char offset_reg) {
  return offset_reg & 0x10 ? -64 * ((offset_reg & 0x0f) + 1)
                           : 64 * (offset_reg & 0x0f);
}

static unsigned char calc_offset_reg(int offset_value) {
  if (offset_value < 0) {
    int steps = (-offset_value + 63) / 64; // -1..-64 -> 1
    if (steps > 16)
      steps = 16;
    return 0x10 | ((steps - 1) & 0x0f);
  }
  if (offset_value > 960)
    offset_value = 960;
  return (offset_value / 64) & 0x0f;
}

static int get_adc_offset(unsigned char reg, const char *label) {
  int value = read_controller_reg(reg);

  if (value < 0) {
    printf("ADC Offset %s: %s\n", label, esshw_strerror(value));
    return 0;
  }
  // bit 5 of BAh (disable wake-up delay) is not part of the offset
  value = calc_offset_value((unsigned char)(value & 0x1f));
  printf("ADC Offset %s %d samples\n", label, value);
  return value;
}

int get_adc_offset_left() { return get_adc_offset(0xba, "Left: "); }

int get_adc_offset_right() { return get_adc_offset(0xbb, "Right:"); }

static int set_adc_offset(unsigned char reg, int offset) {
  int old = read_controller_reg(reg);
  unsigned char value = calc_offset_reg(offset);

  if (old > 0)
    value |= (unsigned char)(old & 0xe0); // keep BAh bit 5
  write_controller_reg(reg, value);
  delay(1);
  return offset;
}

int set_adc_offset_left(int offset) {
  set_adc_offset(0xba, offset);
  get_adc_offset_left();
  return offset;
}

int set_adc_offset_right(int offset) {
  set_adc_offset(0xbb, offset);
  get_adc_offset_right();
  return offset;
}
