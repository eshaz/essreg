/*
 * ES1869 hardware access, see esshw.h.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include "esshw.h"

#ifndef ESS_HOST
#include <conio.h>
#include <i86.h>
#endif

esshw_ctx esshw = {
    0x220,          // audio_base
    0x250,          // config_base, the original essreg's default
    0xFFF,          // dsp_timeout
    0xF,            // dsp_retries
    ESSHW_LEGACY,   // flags
    ESSHW_DIRECT,   // backend
    0,              // dsp_desync
    0, 0, 0         // ext_call, sim_in, sim_out
};

// only mask interrupts with ESSHW_F_CRIT (the safe protocol)
// _enable() instead of restoring the saved flags, since at CPL 3 POPF can't
// change IF and would leave interrupts disabled
#ifdef ESS_HOST
#define CRIT_ENTER()
#define CRIT_LEAVE()
#else
#define CRIT_ENTER()                                                           \
  if (esshw.flags & ESSHW_F_CRIT)                                              \
  _disable()
#define CRIT_LEAVE()                                                           \
  if (esshw.flags & ESSHW_F_CRIT)                                              \
  _enable()
#endif

u8 esshw_inb(u16 port) {
  if (esshw.backend == ESSHW_SIM && esshw.sim_in)
    return esshw.sim_in(port);
#ifdef ESS_HOST
  return 0xFF;
#else
  return (u8)inp(port);
#endif
}

void esshw_outb(u16 port, u8 value) {
  if (esshw.backend == ESSHW_SIM && esshw.sim_out) {
    esshw.sim_out(port, value);
    return;
  }
#ifndef ESS_HOST
  outp(port, value);
#endif
}

static int ext(u16 fn, u8 bl, u8 bh, u8 al) {
  u8 result = 0;
  int err;

  if (!esshw.ext_call)
    return -ESSHW_ENODEV;
  err = esshw.ext_call(fn, bl, bh, al, &result);
  return err ? -err : result;
}

// mixer

int esshw_mixer_read(u8 reg) {
  u16 port = esshw.audio_base + 4;
  u8 old = 0;
  u8 value;

  if (esshw.backend == ESSHW_VXDEXT)
    return ext(ESSX_MIXER_READ, reg, 0, 0);

  CRIT_ENTER();
  if (esshw.flags & ESSHW_F_SAVE_MIXIDX)
    old = esshw_inb(port);
  esshw_outb(port, reg);
  value = esshw_inb(port + 1);
  if (esshw.flags & ESSHW_F_SAVE_MIXIDX)
    esshw_outb(port, old);
  CRIT_LEAVE();
  return value;
}

int esshw_mixer_write(u8 reg, u8 value) {
  u16 port = esshw.audio_base + 4;
  u8 old = 0;

  if (esshw.backend == ESSHW_VXDEXT) {
    int r = ext(ESSX_MIXER_WRITE, reg, value, 0);
    return r < 0 ? r : 0;
  }

  CRIT_ENTER();
  if (esshw.flags & ESSHW_F_SAVE_MIXIDX)
    old = esshw_inb(port);
  esshw_outb(port, reg);
  esshw_outb(port + 1, value);
  if (esshw.flags & ESSHW_F_SAVE_MIXIDX)
    esshw_outb(port, old);
  CRIT_LEAVE();
  return 0;
}

int esshw_mixer_id(u8 id[4]) {
  u16 port = esshw.audio_base + 4;
  u8 old = 0;
  int i;

  if (esshw.backend == ESSHW_VXDEXT)
    return -ESSHW_EPARAM; // the API restores the index between reads

  CRIT_ENTER();
  if (esshw.flags & ESSHW_F_SAVE_MIXIDX)
    old = esshw_inb(port);
  esshw_outb(port, 0x40);
  for (i = 0; i < 4; i++)
    id[i] = esshw_inb(port + 1);
  if (esshw.flags & ESSHW_F_SAVE_MIXIDX)
    esshw_outb(port, old);
  CRIT_LEAVE();
  return 0;
}

int esshw_detect_config(void) {
  u8 id[4];
  int err = esshw_mixer_id(id);

  if (err < 0)
    return err;
  if (id[0] != 0x18)
    return -ESSHW_ENODEV;
  esshw.config_base = (u16)((id[2] << 8) | id[3]);
  return esshw.config_base ? 0 : -ESSHW_ENOCFG;
}

// DSP channel

// wait until Audio_Base+Ch bit 7 (busy) is clear
static int dsp_wait_ready(void) {
  u16 port = esshw.audio_base + 0x0C;
  u16 wait = 0;

  for (;;) {
    if (!(esshw_inb(port) & 0x80))
      return 0;
    if (wait == esshw.dsp_timeout)
      return -1;
    wait++;
  }
}

// wait for read data on Audio_Base+Ch bit 6 or Audio_Base+Eh bit 7
static int dsp_wait_data(void) {
  int poll_c = (esshw.flags & ESSHW_F_POLL_C) != 0;
  u16 port = esshw.audio_base + (poll_c ? 0x0C : 0x0E);
  u8 mask = poll_c ? 0x40 : 0x80;
  u16 wait = 0;

  for (;;) {
    if (esshw_inb(port) & mask)
      return 0;
    if (wait == esshw.dsp_timeout)
      return -1;
    wait++;
  }
}

static int dsp_write(u8 value) {
  if (dsp_wait_ready() < 0)
    return -1;
  esshw_outb(esshw.audio_base + 0x0C, value);
  return 0;
}

int esshw_dsp_idle(void) {
  u8 status;

  if (esshw.backend == ESSHW_VXDEXT) {
    int v = ext(ESSX_PORT_READ, 0x0C, 0, 0);
    return v >= 0 && !(v & 0xC0);
  }
  status = esshw_inb(esshw.audio_base + 0x0C);
  return !(status & 0xC0);
}

// the original essreg protocol: wait for ready once, send C0h and the
// register, wait for data on Audio_Base+Eh and retry the whole sequence
static int ctrl_read_legacy(u8 reg) {
  u16 tries = 0;

  for (;;) {
    tries++;
    if (dsp_wait_ready() < 0) {
      if (tries == esshw.dsp_retries)
        return -ESSHW_EBUSY;
      continue;
    }
    esshw_outb(esshw.audio_base + 0x0C, 0xC0);
    esshw_outb(esshw.audio_base + 0x0C, reg);
    if (dsp_wait_data() < 0) {
      if (tries == esshw.dsp_retries)
        return -ESSHW_ETIMEOUT;
      continue;
    }
    return esshw_inb(esshw.audio_base + 0x0A);
  }
}

static int ctrl_write_legacy(u8 reg, u8 value) {
  u16 tries = 0;

  for (;;) {
    tries++;
    if (dsp_wait_ready() < 0) {
      if (tries == esshw.dsp_retries)
        return -ESSHW_EBUSY;
      continue;
    }
    esshw_outb(esshw.audio_base + 0x0C, reg);
    esshw_outb(esshw.audio_base + 0x0C, value);
    return 0;
  }
}

int esshw_ctrl_read(u8 reg) {
  int result;

  if (reg < 0xA0 || reg > 0xBF)
    return -ESSHW_EPARAM;
  if (esshw.backend == ESSHW_VXDEXT)
    return ext(ESSX_CTRL_READ, reg, 0, 0);
  if (!(esshw.flags & (ESSHW_F_EXT_C6 | ESSHW_F_PRECHECK | ESSHW_F_POLL_C)))
    return ctrl_read_legacy(reg);

  if ((esshw.flags & ESSHW_F_PRECHECK) && !esshw_dsp_idle())
    return -ESSHW_EBUSY;
  CRIT_ENTER();
  result = -ESSHW_EBUSY;
  if ((esshw.flags & ESSHW_F_EXT_C6) && dsp_write(0xC6) < 0)
    goto done;
  if (dsp_write(0xC0) < 0 || dsp_write(reg) < 0) {
    esshw.dsp_desync = 1;
    goto done;
  }
  if (dsp_wait_data() < 0) {
    esshw.dsp_desync = 1;
    result = -ESSHW_ETIMEOUT;
    goto done;
  }
  result = esshw_inb(esshw.audio_base + 0x0A);
done:
  CRIT_LEAVE();
  return result;
}

int esshw_ctrl_write(u8 reg, u8 value) {
  int result;

  if (reg < 0xA0 || reg > 0xBF)
    return -ESSHW_EPARAM;
  if (esshw.backend == ESSHW_VXDEXT) {
    int r = ext(ESSX_CTRL_WRITE, reg, value, 0);
    return r < 0 ? r : 0;
  }
  if (!(esshw.flags & (ESSHW_F_EXT_C6 | ESSHW_F_PRECHECK | ESSHW_F_POLL_C)))
    return ctrl_write_legacy(reg, value);

  if ((esshw.flags & ESSHW_F_PRECHECK) && !esshw_dsp_idle())
    return -ESSHW_EBUSY;
  CRIT_ENTER();
  result = -ESSHW_EBUSY;
  if ((esshw.flags & ESSHW_F_EXT_C6) && dsp_write(0xC6) < 0)
    goto done;
  if (dsp_write(reg) < 0 || dsp_write(value) < 0) {
    esshw.dsp_desync = 1;
    goto done;
  }
  result = 0;
done:
  CRIT_LEAVE();
  return result;
}

// ports

int esshw_port_read(u8 offset) {
  if (offset > 0x0F)
    return -ESSHW_EPARAM;
  if (esshw.backend == ESSHW_VXDEXT)
    return ext(ESSX_PORT_READ, offset, 0, 0);
  return esshw_inb(esshw.audio_base + offset);
}

int esshw_port_write(u8 offset, u8 value) {
  if (offset > 0x0F)
    return -ESSHW_EPARAM;
  if (esshw.backend == ESSHW_VXDEXT) {
    int r = ext(ESSX_PORT_WRITE, offset, value, 0);
    return r < 0 ? r : 0;
  }
  esshw_outb(esshw.audio_base + offset, value);
  return 0;
}

int esshw_cfg_read(u8 offset) {
  if (offset > 0x07)
    return -ESSHW_EPARAM;
  if (esshw.backend == ESSHW_VXDEXT)
    return ext(ESSX_CFG_READ, offset, 0, 0);
  if (!esshw.config_base)
    return -ESSHW_ENOCFG;
  return esshw_inb(esshw.config_base + offset);
}

int esshw_cfg_write(u8 offset, u8 value) {
  if (offset > 0x07)
    return -ESSHW_EPARAM;
  if (esshw.backend == ESSHW_VXDEXT) {
    int r = ext(ESSX_CFG_WRITE, offset, value, 0);
    return r < 0 ? r : 0;
  }
  if (!esshw.config_base)
    return -ESSHW_ENOCFG;
  esshw_outb(esshw.config_base + offset, value);
  return 0;
}

// PnP registers

static int pnp_access(u8 ldn, u8 reg, int write, u8 value) {
  u16 cfg = esshw.config_base;
  u8 old_index;
  u8 old_ldn = 0;
  u8 result = 0;

  if (!cfg)
    return -ESSHW_ENOCFG;
  CRIT_ENTER();
  old_index = esshw_inb(cfg);
  if (ldn != 0xFF) {
    esshw_outb(cfg, 0x07);
    old_ldn = esshw_inb(cfg + 1);
    esshw_outb(cfg + 1, ldn);
  }
  esshw_outb(cfg, reg);
  if (write)
    esshw_outb(cfg + 1, value);
  else
    result = esshw_inb(cfg + 1);
  if (ldn != 0xFF) {
    esshw_outb(cfg, 0x07);
    esshw_outb(cfg + 1, old_ldn);
  }
  esshw_outb(cfg, old_index);
  CRIT_LEAVE();
  return result;
}

int esshw_pnp_read(u8 ldn, u8 reg) {
  if (esshw.backend == ESSHW_VXDEXT)
    return ext(ESSX_PNP_READ, ldn, reg, 0);
  return pnp_access(ldn, reg, 0, 0);
}

int esshw_pnp_write(u8 ldn, u8 reg, u8 value) {
  if (esshw.backend == ESSHW_VXDEXT) {
    int r = ext(ESSX_PNP_WRITE, ldn, reg, value);
    return r < 0 ? r : 0;
  }
  return pnp_access(ldn, reg, 1, value) < 0 ? -ESSHW_ENOCFG : 0;
}

const char *esshw_strerror(int err) {
  if (err < 0)
    err = -err;
  switch (err) {
  case ESSHW_ENODEV:
    return "no ES1869 found";
  case ESSHW_EINUSE:
    return "the audio device is in use by another program";
  case ESSHW_EPARAM:
    return "invalid register";
  case ESSHW_EBUSY:
    return "the DSP is busy";
  case ESSHW_ETIMEOUT:
    return "the DSP did not answer";
  case ESSHW_ENOCFG:
    return "configuration port not found";
  case ESSHW_EFAIL:
    return "the driver rejected the request";
  }
  return "error";
}
