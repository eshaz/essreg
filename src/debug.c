#include "debug.h"

#include <stdio.h>

static const unsigned char config_regs[] = {
    0x06, // Interrupt Status Register
    0x07  // Interrupt Mask Register
};

static const unsigned char audio_config_regs[] = {
    0x30, // Activate
    0x31, // IO Range Check
    0x60, // Audio processor IO Base Address 11:8
    0x61, // Audio processor IO Base Address 7:4
    0x62, // FM Alias IO Base Address 11:8
    0x63, // FM Alias IO Base Address 7:2
    0x64, // MPU-401 IO Base Address 11:8
    0x65, // MPU-401 IO Base Address 7:2
    0x70, // Interrupt Request Channel 1 Select
    0x71, // Interrupt Request Type Select 1
    0x72, // Interrupt Request Channel 2 Select
    0x73, // Interrupt Request Type Select 2
    0x74, // DMA Channel 1 Select
    0x75, // DMA Channel 2 Select
};

static const unsigned char audio_regs[] = {
    0x06, // Reset and Status flags
    0x07, // Power Management
};

static const unsigned char mixer_regs[] = {
    0x00,
    // sound blaster mixer
    0x04, 0x0a, 0x0c, 0x0e, 0x22, 0x26, 0x28, 0x2e,
    // ess mixer
    0x14, 0x1a, 0x1c, 0x32, 0x36, 0x38, 0x3a, 0x3c, 0x3e, 0x40, 0x42, 0x44,
    0x46, 0x48, 0x4a, 0x4c, 0x4e, 0x50, 0x52, 0x54, 0x56, 0x58, 0x5a, 0x60,
    0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d,
    0x6e, 0x6f, 0x70, 0x71, 0x72, 0x74, 0x76, 0x78, 0x7a, 0x7c, 0x7d, 0x7f};

static FILE *out;

void print_reg(unsigned int reg_addr, unsigned char reg_value) {
  fprintf(out ? out : stderr, "%02X | %02X | " BIN_PAT " \n", reg_addr,
          reg_value, BIN(reg_value));
}

static void print_result(unsigned int reg_addr, int value) {
  if (value < 0)
    fprintf(out, "%02X | -- | %s\n", reg_addr, esshw_strerror(value));
  else
    print_reg(reg_addr, (unsigned char)value);
}

void dump_regs(const char *path) {
  unsigned int i;

  out = fopen(path, "w");
  if (!out) {
    printf("Could not create %s\n", path);
    return;
  }

  fprintf(out, "Config Regs: \n");
  for (i = 0; i < ESS_ARRAY_SIZE(config_regs); i++)
    print_result(config_regs[i], esshw_cfg_read(config_regs[i]));

  fprintf(out, "Config Audio: \n");
  for (i = 0; i < ESS_ARRAY_SIZE(audio_config_regs); i++)
    print_result(audio_config_regs[i], read_audio_reg(audio_config_regs[i]));

  fprintf(out, "Audio Regs: \n");
  for (i = 0; i < ESS_ARRAY_SIZE(audio_regs); i++)
    print_result(audio_regs[i], esshw_port_read(audio_regs[i]));

  fprintf(out, "Audio Mixer Regs: \n");
  for (i = 0; i < ESS_ARRAY_SIZE(mixer_regs); i++)
    print_result(mixer_regs[i], read_mixer_reg(mixer_regs[i]));

  fclose(out);
  out = 0;

  printf("Saved registers to %s\n", path);
}
