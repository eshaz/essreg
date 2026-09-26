/*
 * essio.c -- catalog register access (see essio.h).
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#include "essio.h"
#include "esshw.h"

int ess_read(int reg) {
  const struct ess_reg *r = &ess_regs[reg];

  switch (r->bank) {
  case BK_MIXER:
    return esshw_mixer_read((u8)r->addr);
  case BK_CTRL:
    return esshw_ctrl_read((u8)r->addr);
  case BK_APORT:
    return esshw_port_read((u8)r->addr);
  case BK_CPORT:
    return esshw_cfg_read((u8)r->addr);
  case BK_PNPCARD:
    return esshw_pnp_read(0xFF, (u8)r->addr);
  case BK_PNPLDN:
    return esshw_pnp_read(r->ldn, (u8)r->addr);
  }
  return -ESSHW_EPARAM;
}

int ess_write(int reg, u8 value) {
  const struct ess_reg *r = &ess_regs[reg];

  switch (r->bank) {
  case BK_MIXER:
    return esshw_mixer_write((u8)r->addr, value);
  case BK_CTRL:
    return esshw_ctrl_write((u8)r->addr, value);
  case BK_APORT:
    return esshw_port_write((u8)r->addr, value);
  case BK_CPORT:
    return esshw_cfg_write((u8)r->addr, value);
  case BK_PNPCARD:
    return esshw_pnp_write(0xFF, (u8)r->addr, value);
  case BK_PNPLDN:
    return esshw_pnp_write(r->ldn, (u8)r->addr, value);
  }
  return -ESSHW_EPARAM;
}

int ess_field_read(int field, u8 *value, u8 *raw) {
  const struct ess_field *f = &ess_fields[field];
  int v = ess_read(f->reg);

  if (v < 0)
    return v;
  if (raw)
    *raw = (u8)v;
  *value = cat_get(f, (u8)v);
  return 0;
}

/* a few microseconds between the edges of a pulse (the DSP reset needs
 * 3 us): status reads of Audio_Base+Ch take about 1 us each on ISA */
static void pulse_delay(void) {
  int i;
  for (i = 0; i < 8; i++)
    esshw_port_read(0x0C);
}

int ess_field_write(int field, u8 value, int expert) {
  const struct ess_field *f = &ess_fields[field];
  const struct ess_reg *r = &ess_regs[f->reg];
  int raw = 0;
  int err;

  if (!cat_writable(f, expert))
    return -ESSIO_ETIER;
  /* write-only registers have nothing to keep, and reading a register with
   * read side effects would change it (Audio_Base+6h returns status bits
   * that must be written as 0): both are written from 0 */
  if (!(r->flags & (RF_WRITEONLY | RF_READ_SIDEFX))) {
    raw = ess_read(f->reg);
    if (raw < 0)
      return raw;
  }
  switch (f->kind) {
  case K_PULSE:
    err = ess_write(f->reg, cat_set(f, (u8)raw, 1));
    if (err < 0)
      return err;
    pulse_delay();
    return ess_write(f->reg, cat_set(f, (u8)raw, 0));
  case K_ACTION:
    return ess_write(f->reg, cat_set(f, (u8)raw, cat_max(f)));
  }
  return ess_write(f->reg, cat_set(f, (u8)raw, value));
}
