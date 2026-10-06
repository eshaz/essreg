/*
 * Catalog register and field access, see essio.h.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include "essio.h"
#include "esshw.h"

// the number of a register's logical device: an optional device's
// depends on card register 25h
static int ldn_of(const struct ess_reg *r) {
  int hdr, ldn;

  if (r->ldn < LDN_OPT)
    return r->ldn;
  hdr = esshw_pnp_read(0xFF, 0x25);
  if (hdr < 0)
    return hdr;
  ldn = cat_opt_ldn(r->ldn, (u8)hdr);
  return ldn < 0 ? -ESSIO_EABSENT : ldn;
}

const char *ess_strerror(int err) {
  switch (err < 0 ? -err : err) {
  case ESSIO_ETIER:
    return "needs Expert mode";
  case ESSIO_EABSENT:
    return "not on this card";
  case ESSIO_ETRAPPED:
    return "not read: ES1869.VXD would give the FM to Windows";
  }
  return esshw_strerror(err);
}

int ess_read(int reg) {
  const struct ess_reg *r = &ess_regs[reg];
  int ldn;

  // a DOS program would find no FM until a MIDI program closes
  if ((r->flags & RF_FM_PORT) && (esshw.flags & ESSHW_F_FM_TRAPPED))
    return -ESSIO_ETRAPPED;

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
    ldn = ldn_of(r);
    return ldn < 0 ? ldn : esshw_pnp_read((u8)ldn, (u8)r->addr);
  }
  return -ESSHW_EPARAM;
}

int ess_write(int reg, u8 value) {
  const struct ess_reg *r = &ess_regs[reg];
  int ldn;

  if ((r->flags & RF_FM_PORT) && (esshw.flags & ESSHW_F_FM_TRAPPED))
    return -ESSIO_ETRAPPED;

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
    ldn = ldn_of(r);
    return ldn < 0 ? ldn : esshw_pnp_write((u8)ldn, (u8)r->addr, value);
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

// wait a few microseconds between pulse edges (the DSP reset needs 3 us)
// by reading Audio_Base+6h, about 1 us each on ISA, as the reset recipe
// does (DS p.43): any other port wakes the chip from a partial power-down
// (DS p.76), which would undo a power-down pulse
static void pulse_delay(void) {
  int i;
  for (i = 0; i < 8; i++)
    esshw_port_read(0x06);
}

// after a DSP software reset the DSP puts AAh in its read buffer: read it,
// so the next command doesn't take it as its data (DS p.43)
static int reset_ack(void) {
  int i, v;

  // at least 1 ms: each read of Audio_Base+Eh takes about 1 us on ISA
  for (i = 0; i < 2000; i++) {
    v = esshw_port_read(0x0E);
    if (v < 0)
      return v;
    if (!(v & 0x80))
      continue;
    v = esshw_port_read(0x0A);
    if (v < 0)
      return v;
    if (v == 0xAA)
      return 0;
  }
  return -ESSHW_ETIMEOUT;
}

int ess_field_write(int field, u8 value, int expert) {
  const struct ess_field *f = &ess_fields[field];
  const struct ess_reg *r = &ess_regs[f->reg];
  int raw = 0;
  int err;

  if (!cat_writable(f, expert))
    return -ESSIO_ETIER;
  // start from 0 for write-only registers, which have nothing to keep, and
  // for ones that change when read (Audio_Base+6h returns status bits that
  // must be written as 0)
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
    err = ess_write(f->reg, cat_set(f, (u8)raw, 0));
    if (err == 0 && field == F_AP_SWRST)
      err = reset_ack();
    return err;
  case K_ACTION:
    return ess_write(f->reg, cat_set(f, (u8)raw, cat_max(f)));
  }
  return ess_write(f->reg, cat_set(f, (u8)raw, value));
}
