#include "debug.h"
#include "regs.h"

void print_reg(unsigned int reg_addr, unsigned char reg_value) {
  fprintf(stderr, "%02X | %02X | "BIN_PAT" \n", reg_addr, reg_value, BIN(reg_value));
}

void dump_regs(const char* path) {
  unsigned int i;
  unsigned int reg_addr;
  unsigned int reg_value;
  FILE* fd;
  
  fd = freopen(path, "w", stderr);

  fprintf(stderr, "Config Regs: \n");
  for (i = 0; i < config_reg_count; i++) {
	reg_addr = config_regs[i];
    reg_value = inp(config_base + reg_addr);
	print_reg(reg_addr, reg_value);
  }
  
  fprintf(stderr, "Config Audio: \n");
  for (i = 0; i < audio_config_reg_count; i++) {
	reg_addr = audio_config_regs[i];
    reg_value = read_audio_reg(reg_addr);
	print_reg(reg_addr, reg_value);
  }

  fprintf(stderr, "Audio Regs: \n");
  for (i = 0; i < audio_reg_count; i++) {
	reg_addr = audio_regs[i];
    reg_value = inp(audio_base + reg_addr);
	print_reg(reg_addr, reg_value);
  }
  
  fprintf(stderr, "Audio Mixer Regs: \n");
  for (i = 0; i < mixer_reg_count; i++) {
	reg_addr = mixer_regs[i];
    reg_value = read_mixer_reg(reg_addr);
	print_reg(reg_addr, reg_value);
  }
  
  fflush(fd);
  fclose(fd);
  
  printf("Saved registers to %s\n", path);
}