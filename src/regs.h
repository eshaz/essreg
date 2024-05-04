#include <conio.h>
#include <stdlib.h>

#ifndef ESSREG_H
#define ESSREG_H

#define BIN_PAT "%c%c%c%c%c%c%c%c"
#define BIN(byte) \
	((byte) & 0x80 ? '1' : '0'), \
	((byte) & 0x40 ? '1' : '0'), \
	((byte) & 0x20 ? '1' : '0'), \
	((byte) & 0x10 ? '1' : '0'), \
	((byte) & 0x08 ? '1' : '0'), \
	((byte) & 0x04 ? '1' : '0'), \
	((byte) & 0x02 ? '1' : '0'), \
	((byte) & 0x01 ? '1' : '0')

unsigned int read_mixer_reg(unsigned int reg_addr);
void write_mixer_reg(unsigned int reg_addr, unsigned char reg_value);
unsigned int read_audio_reg(unsigned int reg_addr);

unsigned char get_mono_in();
void set_mono_in(unsigned char on_off);

unsigned char get_mono_in_level();
void set_mono_in_level(unsigned char level);
void set_mono_in_level_pct(unsigned char level);

unsigned char get_telegaming_mode();
void set_telegaming_mode(unsigned char on_off);

unsigned char get_3d_mode();
void set_3d_mode(unsigned char on_off);

unsigned char get_3d_level();
void set_3d_level(unsigned char level);
void set_3d_level_pct(unsigned char level_pct);

unsigned char get_mic_preamp();
void set_mic_preamp(unsigned char on_off);

unsigned char get_digital_record();
void set_digital_record(unsigned char on_off);

void calibrate_op_amp();

unsigned int audio_base = 0x220;
unsigned int config_base = 0x250;
unsigned int config_logical_device_reg = 0x07;
unsigned char audio_config_logical_device = 0x01;

#endif /* ESSREG_H */
