#include <conio.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "regs.h"

/*
unsigned int inp(int portid);
unsigned int inpw(int portid);
unsigned int outp(int portid, int value);
unsigned int outpw(int portid, unsigned int value);
*/

unsigned int read_audio_reg(unsigned int reg_addr) {
  int config_device_reg_addr = config_base;
  int config_device_reg_data = config_base + 0x01;

  int logical_device_select_port = config_base + config_logical_device_reg;

  // set config register to audio logical device (1)
  outp(config_device_reg_addr, logical_device_select_port);
  outp(config_device_reg_data, audio_config_logical_device);

  // set config register to input register and read data
  outp(config_device_reg_addr, reg_addr);
  return inp(config_device_reg_data);
}

unsigned int read_mixer_reg(unsigned int reg_addr) {
  int mixer_set_port = audio_base + 0x4;
  int mixer_read_port = audio_base + 0x5;

  // set mixer register to be read
  outp(mixer_set_port, reg_addr);
  return inp(mixer_read_port);
}

void write_mixer_reg(unsigned int reg_addr, unsigned char reg_value) {
  int mixer_set_port = audio_base + 0x4;
  int mixer_read_port = audio_base + 0x5;

  // set mixer register to be read
  outp(mixer_set_port, reg_addr);
  outp(mixer_read_port, reg_value);
}

static int wait_for_controller_ready() {
  unsigned int wait = 0;

  // poll for low bit 7 of audiobase + Ch until clear
  while (1) {
    if (!(inp(audio_base + 0x0c) & 0x80)) {
      return 0;
    }

    if (wait == controller_wait_timeout) {
      return -1;
    }

    wait++;
  }
}

static int wait_for_controller_read() {
  unsigned int wait = 0;

  // poll for high bit 6 port audiobase + Ch
  while (1) {
    if ((inp(audio_base + 0x0e) & 0x80)) {
      return 0;
    }

    if (wait == controller_wait_timeout) {
      return -1;
    }

    wait++;
  }
}

int read_controller_reg(unsigned char reg_addr) {
  unsigned int tries = 0;

do_read:
  tries++;

  if (wait_for_controller_ready() == -1) {
    if (tries == controller_retry_count) {
      return -1;
    } else {
      goto do_read;
    }
  }

  // write command to read controller registers 0xC0
  outp(audio_base + 0x0c, 0xc0);
  // write register to read
  outp(audio_base + 0x0c, reg_addr);

  if (wait_for_controller_read() == -1) {
    if (tries == controller_retry_count) {
      return -1;
    } else {
      goto do_read;
    }
  }

  // read byte from audiobase + Ah
  return inp(audio_base + 0x0a);
}

int write_controller_reg(unsigned char reg_addr, unsigned char reg_value) {
  unsigned int tries = 0;

do_read:
  tries++;

  if (wait_for_controller_ready() == -1) {
    if (tries == controller_retry_count) {
      printf("Timeout waiting for controller register ready flag.\n");
      return -1;
    } else {
      goto do_read;
    }
  }

  outp(audio_base + 0x0c, reg_addr);
  outp(audio_base + 0x0c, reg_value);

  return 0;
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
  write_mixer_reg(0x6d, (level << 4) & level);
  get_mono_in_level();
}

void set_mono_in_level_pct(unsigned char level_pct) {
  set_mono_in_level((level_pct + 1) * 0x0f / 100);
}

unsigned char get_digital_power_down() {
  unsigned char value = inp(audio_base + 0x06) & 0x08;

  printf("Digital Power Down ");
  if (value) {
    printf("Disabled\n");
  } else {
    printf("Enabled\n");
  }

  return value;
}

unsigned char get_analog_stays_on() {
  unsigned char value = inp(audio_base + 0x07) & 0x08;

  printf("Analog Stays On ");
  if (value) {
    printf("Enabled\n");
  } else {
    printf("Disabled\n");
  }

  return value;
}

void set_analog_stays_on(unsigned char on_off) {
  unsigned char value = inp(audio_base + 0x07);

  value = (value & 0xf7) | (on_off ? 0x08 : 0x00);

  outp(audio_base + 0x07, value);

  get_analog_stays_on();
}

unsigned char get_fm_reset() {
  unsigned char value = inp(audio_base + 0x07) & 0x20;

  printf("FM Reset ");
  if (value) {
    printf("Held\n");
  } else {
    printf("Released\n");
  }

  return value;
}

void set_fm_reset(unsigned char on_off) {
  unsigned char value = inp(audio_base + 0x07);

  value = (value & 0xdf) | (on_off ? 0x20 : 0x00);

  outp(audio_base + 0x07, value);

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

unsigned int get_audio_1_sample_rate() {
  int rate = read_controller_reg(0xa1);

  if (rate >= 0) {
    rate = rate & 0x80 ? 795500 / (256 - rate)  //  rate > 22kHz
                       : 397700 / (128 - rate); // rate <= 22kHz

    printf("Audio 1 Sample Rate: %u Hz\n", rate);
    return rate;
  }

  return 0;
}

unsigned int get_audio_2_sample_rate() {
  unsigned int rate = read_mixer_reg(0x70);
  unsigned int master_clock = rate & 0x80
                                  ? 768000  //  48kHz, 32kHz, 16kHz, 8kHz, etc.
                                  : 793800; // 44.1kHz, 22.05kHz, etc.
  rate = master_clock / (128 - rate);

  printf("Audio 2 Sample Rate: %u Hz\n", rate);
  return rate;
}

unsigned int calc_filter_rate(unsigned char reg) {
  return (unsigned long)7160000 / (256 - reg);
}

unsigned int get_audio_1_filter_rate() {
  int rate = read_controller_reg(0xa2);

  if (rate >= 0) {
    rate = calc_filter_rate(rate);
    printf("Audio 1 Filter Rate: %u Hz\n", rate);
    return rate;
  }

  return 0;
}

unsigned int get_audio_2_filter_rate() {
  unsigned int rate = read_mixer_reg(0x72);

  rate = calc_filter_rate(rate);

  printf("Audio 2 Filter Rate: %u Hz\n", rate);
  return rate;
}

static int calc_offset_value(unsigned char offset_reg) {
  return offset_reg & 0x10 ? -64 * (offset_reg & 0x0f + 1)
                           : 64 * (offset_reg & 0x0f);
}

static unsigned char calc_offset_reg(int offset_value) {
  return offset_value < 0 ? ((-offset_value / 64) & 0x0f) | 0x10
                          : (offset_value / 64) & 0x0f;
}

int get_adc_offset_left() {
  int offset = calc_offset_value(read_controller_reg(0xba));

  if (offset >= 0)
    printf("ACD Offset Left:  %d samples\n", offset);

  return offset;
}

int get_adc_offset_right() {
  int offset = calc_offset_value(read_controller_reg(0xbb));

  if (offset >= 0)
    printf("ACD Offset Right: %d samples\n", offset);

  return offset;
}

int set_adc_offset_left(int offset) {
  unsigned char offset_value = calc_offset_reg(offset);
  write_controller_reg(0xba, offset_value);

  delay(1);

  get_adc_offset_left();

  return offset;
}

int set_adc_offset_right(int offset) {
  unsigned char offset_value = calc_offset_reg(offset);
  write_controller_reg(0xbb, offset_value);

  delay(1);

  get_adc_offset_right();

  return offset;
}
