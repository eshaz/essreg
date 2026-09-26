/*
 * winio.c -- access path selection for essctl (see winio.h).
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <string.h>

#include "esshw.h"
#include "simhw.h"
#include "vxdapi.h"
#include "winio.h"

int winio_path = WIO_DIRECT;

static int ext_hook(u16 fn, u8 bl, u8 bh, u8 al, u8 *result) {
  return vxd_ext_call(fn, bl, bh, al, result);
}

int winio_init(const struct winio_opts *opts) {
  u16 port;

  esshw.flags = ESSHW_SAFE;
  esshw.dsp_timeout = 0x2000;
  esshw.dsp_retries = 1;

  if (opts->sim) {
    winio_path = WIO_SIM;
    simhw_reset(opts->audio_base ? opts->audio_base : 0x220,
                opts->config_base ? opts->config_base : 0x800);
    simhw_attach();
    esshw.audio_base = simhw.audio_base;
    esshw.config_base = simhw.config_base;
    return 0;
  }

  esshw.backend = ESSHW_DIRECT;
  esshw.audio_base = 0x220;
  esshw.config_base = 0;
  winio_path = WIO_DIRECT;

  if (vxd_open() == 0) {
    if (vxd.adi_valid)
      esshw.audio_base = vxd_adi_word(ADI_AUDIO_BASE);
    if (vxd.ext_version && !opts->novxd && !opts->audio_base) {
      winio_path = WIO_VXDEXT;
      esshw.backend = ESSHW_VXDEXT;
      esshw.ext_call = ext_hook;
      return 0;
    }
    winio_path = WIO_DIRECT_VXD;
    if (vxd_config_port(&port) == 0)
      esshw.config_base = port;
  }
  if (opts->audio_base)
    esshw.audio_base = opts->audio_base;
  if (opts->config_base)
    esshw.config_base = opts->config_base;
  if (!esshw.config_base) {
    int err;
    if ((err = winio_begin()) < 0)
      return err;
    err = esshw_detect_config();
    winio_end();
    return err;
  }
  return 0;
}

int winio_begin(void) {
  if (winio_path != WIO_DIRECT_VXD)
    return 0;
  return vxd_dsp_begin() ? -ESSHW_EINUSE : 0;
}

void winio_end(void) {
  if (winio_path == WIO_DIRECT_VXD)
    vxd_dsp_end();
}

int winio_can_poll(void) {
  /* on the stock driver every batch takes (and may release) the DSP */
  return winio_path != WIO_DIRECT_VXD;
}

void winio_path_text(char *buf, unsigned size) {
  char tmp[96];

  switch (winio_path) {
  case WIO_VXDEXT:
    sprintf(tmp, "VxD register API %u.%02u, Audio_Base %03Xh",
            vxd.ext_version >> 8, vxd.ext_version & 0xFF, esshw.audio_base);
    break;
  case WIO_DIRECT_VXD:
    sprintf(tmp, "Direct I/O at %03Xh (stock ES1869.VXD %u.%02u)",
            esshw.audio_base, vxd.version >> 8, vxd.version & 0xFF);
    break;
  case WIO_SIM:
    sprintf(tmp, "Simulated ES1869 at %03Xh", esshw.audio_base);
    break;
  default:
    sprintf(tmp, "Direct I/O at %03Xh (no ES1869 driver)", esshw.audio_base);
  }
  strncpy(buf, tmp, size - 1);
  buf[size - 1] = 0;
}

static const char *owner_name(int cls) {
  switch (cls) {
  case VXD_OWNER_NONE:
    return "free";
  case VXD_OWNER_SELF:
    return "Windows";
  case VXD_OWNER_OTHER:
    return "DOS box";
  }
  return "in use";
}

void winio_owner_text(char *buf, unsigned size) {
  struct vxd_owners o;
  char tmp[96];

  tmp[0] = 0;
  if (winio_path == WIO_VXDEXT) {
    if (vxd_ext_owners(&o) == 0)
      sprintf(tmp, "DSP %s, FM %s, MPU %s%s", owner_name(o.dsp),
              owner_name(o.fm), owner_name(o.mpu),
              (o.status & 0x80) ? ", DSP busy" : "");
  } else if (winio_path == WIO_DIRECT_VXD) {
    if (vxd_read_adi() == 0)
      sprintf(tmp, "DSP %s, FM %s, MPU %s",
              owner_name(vxd_owner_class(vxd_adi_dword(ADI_DSP_OWNER))),
              owner_name(vxd_owner_class(vxd_adi_dword(ADI_FM_OWNER))),
              owner_name(vxd_owner_class(vxd_adi_dword(ADI_MPU_OWNER))));
  }
  strncpy(buf, tmp, size - 1);
  buf[size - 1] = 0;
}
