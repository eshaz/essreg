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
	
	int logical_device_select_port = 
		config_base + config_logical_device_reg;
	
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
	write_mixer_reg(
		0x7d,
	  (original_value & 0xfe) | (on_off ? 0x01 : 0x00)
	);
	get_mono_in();
}

unsigned char get_mono_in_level() {
	unsigned char level = read_mixer_reg(0x6d);
	unsigned char left = level >> 4;
	unsigned char right = level & 0x0f;
	printf("Mono-In Level: L: %u (%d%%) R: %u (%d%%) \n", left, left * 100 / 0x0f, right, right * 100 / 0x0f);
	return level;
}

void set_mono_in_level(unsigned char level) {  
	if (level > 0x0f) level = 0x0f;
	write_mixer_reg(
		0x6d,
	  (level << 4) & level
	);
	get_mono_in_level();
}

void set_mono_in_level_pct(unsigned char level_pct) {
	set_mono_in_level((level_pct + 1) * 0x0f / 100);
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
	write_mixer_reg(
		0x48,
	  (original_value & 0xfd) | (on_off ? 0x02 : 0x00)
	);
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
	write_mixer_reg(
		0x50,
	  (original_value & 0xf7) | (on_off ? 0x0c : 0x00)
	);
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
	if (level > 0x3f) level = 0x3f;
	write_mixer_reg(
		0x52,
	  (original_value & 0xc0) | (level & 0x3f)
	);
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
	write_mixer_reg(
		0x7d,
	  (original_value & 0xf7) | (on_off ? 0x08 : 0x00)
	);
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
	write_mixer_reg(
		0x7f,
	  (original_value & 0xef) | (on_off ? 0x10 : 0x00)
	);
	get_digital_record();
}
