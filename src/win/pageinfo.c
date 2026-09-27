/*
 * The device information page: the driver, resources and
 * owners essctl found.
 *
 * Notes:
 *
 * Only uses the VxD functions without side effects (0000,
 * 0001, 0004, 0005 read, 0008, 000A, 0101, 0301, and
 * 0400/040C of the extension).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <string.h>

#include "drvcfg.h"
#include "essctl.h"
#include "esshw.h"
#include "resource.h"
#include "vxdapi.h"
#include "winio.h"

static HWND edit;

#define ADD (p + strlen(p))

static const char *owner_name(u32 handle) {
  switch (vxd_owner_class(handle)) {
  case VXD_OWNER_NONE:
    return "free";
  case VXD_OWNER_SELF:
    return "Windows";
  case VXD_OWNER_OTHER:
    return "a DOS box";
  }
  return "in use";
}

void info_text(char *buf, unsigned size) {
  static char p[1600];
  char path[96];
  u16 port, count;
  u8 v, irq, id[4];
  u16 flags;
  DWORD wt;
  int err;

  winio_path_text(path, sizeof(path));
  sprintf(p, "Access path:\t%s\r\n", path);
  err = drvcfg_get(DRVCFG_WAVETABLE, &wt);
  if (err >= 0)
    sprintf(ADD, "Music DAC:\t%s\r\n",
            err && wt ? "FM keeps it (ESSWaveTableChip)"
                      : "I2S has it while no MIDI program is open "
                        "(ESS's default)");
  if (!vxd.entry) {
    strcat(p, "ES1869.VXD:\tnot loaded\r\n");
  } else {
    sprintf(ADD, "ES1869.VXD:\tversion %u.%02u (AUDDRV, device ID 3B07h)\r\n",
            vxd.version >> 8, vxd.version & 0xFF);
    if (vxd.ext_version)
      sprintf(ADD, "Register API:\tversion %u.%02u, %u functions, "
                   "features %04Xh\r\n",
              vxd.ext_version >> 8, vxd.ext_version & 0xFF, vxd.ext_count,
              vxd.ext_features);
    else
      strcat(p, "Register API:\tnot present (stock driver)\r\n");
    if (vxd_read_adi() == 0) {
      flags = vxd_adi_word(ADI_FLAGS);
      sprintf(ADD, "Devnode:\t%08lXh\r\n",
              (unsigned long)vxd_adi_dword(ADI_DEVNODE));
      sprintf(ADD, "Audio_Base:\t%03Xh\r\n", vxd_adi_word(ADI_AUDIO_BASE));
      sprintf(ADD, "IRQ:\t\t%u\r\n", vxd.adi[ADI_IRQ]);
      sprintf(ADD, "DMA:\t\t%u (audio 1), %u (audio 2)\r\n", vxd.adi[ADI_DMA1],
              vxd.adi[ADI_DMA2]);
      if (vxd_adi_word(ADI_FM_BASE) != 0xFFFF)
        sprintf(ADD, "FM:\t\t%03Xh\r\n", vxd_adi_word(ADI_FM_BASE));
      if (vxd_adi_word(ADI_MPU_BASE) != 0xFFFF)
        sprintf(ADD, "MPU-401:\t%03Xh, IRQ %u%s\r\n",
                vxd_adi_word(ADI_MPU_BASE), vxd.adi[ADI_MPU_IRQ],
                (flags & ADI_F_MPU_SHARED) ? " (shared with audio)" : "");
      sprintf(ADD, "Driver flags:\t%04Xh %04Xh%s\r\n", flags,
              vxd_adi_word(ADI_FLAGS_HI),
              (flags & ADI_F_NODMA) ? " (no-DMA emulation)" : "");
      sprintf(ADD, "Owners:\t\tDSP %s, FM %s, MPU-401 %s\r\n",
              owner_name(vxd_adi_dword(ADI_DSP_OWNER)),
              owner_name(vxd_adi_dword(ADI_FM_OWNER)),
              owner_name(vxd_adi_dword(ADI_MPU_OWNER)));
    }
    if (vxd_config_port(&port) == 0)
      sprintf(ADD, "Config port:\t%03Xh\r\n", port);
    if (vxd_gpo_read(&v) == 0)
      sprintf(ADD, "GPO pins:\tGPO0 %u, GPO1 %u\r\n", v & 1, (v >> 1) & 1);
    if (vxd_dma_count(0, &count) == 0)
      sprintf(ADD, "DMA count:\taudio 1 %04Xh", count);
    if (vxd_dma_count(1, &count) == 0)
      sprintf(ADD, ", audio 2 %04Xh", count);
    strcat(p, "\r\n");
    if (vxd_global_flag(&v) == 0)
      sprintf(ADD, "Function 000A:\t%u\r\n", v);
    if (vxd_fm_info(&port) == 0)
      sprintf(ADD, "FM info (0101):\tport %03Xh\r\n", port);
    if (vxd_mpu_info(&port, &irq) == 0)
      sprintf(ADD, "MPU info (0301):\tport %03Xh, IRQ %u\r\n", port, irq);
  }
  if (winio_path != WIO_VXDEXT) {
    sprintf(ADD, "Config_Base:\t%03Xh\r\n", esshw.config_base);
    err = winio_begin();
    if (err == 0)
      err = esshw_mixer_id(id);
    winio_end();
    if (err == 0)
      sprintf(ADD, "Mixer 40h ID:\t%02Xh %02Xh %02Xh %02Xh%s\r\n", id[0],
              id[1], id[2], id[3],
              id[0] == 0x18 && id[1] == 0x69 ? " (ES1869)" : "");
    else
      sprintf(ADD, "Mixer 40h ID:\t%s\r\n", esshw_strerror(err));
  }
  strncpy(buf, p, size - 1);
  buf[size - 1] = 0;
}

void info_create(void) {
  edit = page_control("EDIT", "",
                      WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL |
                          ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                      0, 0, area_w, area_h, IDC_PG_EDIT);
}

void info_refresh(int how) {
  static char text[1600];
  int tabs = 64;

  if (how == REFRESH_TIMER)
    return;
  info_text(text, sizeof(text));
  SendMessage(edit, EM_SETTABSTOPS, 1, (LPARAM)(int FAR *)&tabs);
  SetWindowText(edit, text);
}
