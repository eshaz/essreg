/*
 * ES1869.VXD V86/PM API wrappers, see vxdapi.h.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <string.h>

#include "esshw.h"
#include "vxdapi.h"

#ifndef ESS_HOST
#include <i86.h>
#endif

struct vxd_state vxd;

// buffers are passed in ES:BX or ES:DI
#ifdef ESS_HOST
#define SET_ESBX(r, p) ((r)->host_buf = (void *)(p))
#define SET_ESDI(r, p) ((r)->host_buf = (void *)(p))
#else
#define SET_ESBX(r, p) ((r)->es = FP_SEG(p), (r)->ebx = FP_OFF(p))
#define SET_ESDI(r, p) ((r)->es = FP_SEG(p), (r)->edi = FP_OFF(p))
#endif

static void regs_init(vxd_regs *r) {
  memset(r, 0, sizeof(*r));
  r->ecx = vxd.devnode;
}

static void put_u32(u8 *p, u32 v) {
  p[0] = (u8)v;
  p[1] = (u8)(v >> 8);
  p[2] = (u8)(v >> 16);
  p[3] = (u8)(v >> 24);
}

int vxd_call(u16 fn, vxd_regs *r) {
  u16 ax;

  if (!vxd.entry)
    return ESSHW_ENODEV;
  r->edx = fn;
  if (!vxd_raw_call(vxd.entry, r))
    return 0;
  // carry set, AX has the error code for most functions but some leave it
  // unchanged or zero
  ax = (u16)r->eax;
  return ax >= ESSHW_ENODEV && ax <= ESSHW_ENOCFG ? ax : ESSHW_EFAIL;
}

int vxd_open(void) {
  vxd_regs r;

  memset(&vxd, 0, sizeof(vxd));
  vxd.entry = vxd_get_entry(VXD_AUDDRV_ID);
  if (!vxd.entry)
    return ESSHW_ENODEV;

  regs_init(&r);
  if (vxd_call(0x0000, &r) == 0)
    vxd.version = (u16)r.eax;
  if (vxd_read_adi() == 0)
    vxd.devnode = vxd_adi_dword(ADI_DEVNODE);

  // the stock driver rejects group 4 with the carry flag
  regs_init(&r);
  if (vxd.devnode && vxd_call(ESSX_INFO, &r) == 0) {
    vxd.ext_version = (u16)r.eax;
    vxd.ext_features = (u16)r.ebx;
    vxd.ext_count = (u16)r.edx;
  }
  return 0;
}

int vxd_read_adi(void) {
  vxd_regs r;
  int err;

  // dword 0 of the buffer is the number of bytes to copy
  // AX = 1 selects the device by the devnode in ECX, AX = 0 the first one
  put_u32(vxd.adi, ADI_COPY_SIZE);
  regs_init(&r);
  r.eax = vxd.devnode ? 1 : 0;
  SET_ESBX(&r, vxd.adi);
  err = vxd_call(0x0001, &r);
  vxd.adi_valid = (u8)!err;
  return err;
}

u16 vxd_adi_word(unsigned offset) {
  return (u16)(vxd.adi[offset] | (vxd.adi[offset + 1] << 8));
}

u32 vxd_adi_dword(unsigned offset) {
  return vxd_adi_word(offset) | ((u32)vxd_adi_word(offset + 2) << 16);
}

int vxd_owner_class(u32 owner) {
  if (!owner)
    return VXD_OWNER_NONE;
  if (!vxd.sys_vm)
    return VXD_OWNER_UNKNOWN;
  return owner == vxd.sys_vm ? VXD_OWNER_SELF : VXD_OWNER_OTHER;
}

int vxd_dma_count(int channel, u16 *count) {
  vxd_regs r;
  int err;

  regs_init(&r);
  r.ebx = channel ? 1 : 0;
  err = vxd_call(0x0004, &r);
  if (!err)
    *count = (u16)r.eax;
  return err;
}

int vxd_gpo_read(u8 *bits) {
  vxd_regs r;
  int err;

  regs_init(&r);
  r.eax = 1; // EAX = 0 would write GPO0/GPO1 from BL
  err = vxd_call(0x0005, &r);
  if (!err)
    *bits = (u8)(r.ebx & 3);
  return err;
}

int vxd_config_port(u16 *port) {
  vxd_regs r;
  int err;

  regs_init(&r);
  err = vxd_call(0x0008, &r);
  if (!err)
    *port = (u16)r.edx;
  return err;
}

int vxd_global_flag(u8 *flag) {
  vxd_regs r;
  int err;

  regs_init(&r);
  err = vxd_call(0x000A, &r);
  if (!err)
    *flag = (u8)r.eax;
  return err;
}

int vxd_fm_info(u16 *port) {
  vxd_regs r;
  u8 buf[0x1C];
  int err;

  memset(buf, 0, sizeof(buf));
  put_u32(buf, sizeof(buf));
  regs_init(&r);
  r.eax = 1;
  SET_ESBX(&r, buf);
  err = vxd_call(0x0101, &r);
  if (!err)
    *port = (u16)(buf[6] | (buf[7] << 8));
  return err;
}

int vxd_mpu_info(u16 *port, u8 *irq) {
  vxd_regs r;
  u8 buf[0x24];
  int err;

  memset(buf, 0, sizeof(buf));
  put_u32(buf, sizeof(buf));
  regs_init(&r);
  r.eax = 1;
  SET_ESBX(&r, buf);
  err = vxd_call(0x0301, &r);
  if (!err) {
    *port = (u16)(buf[6] | (buf[7] << 8));
    *irq = buf[8];
  }
  return err;
}

int vxd_dsp_begin(void) {
  vxd_regs r;
  u32 owner = 0;
  int was_free;
  int err;

  vxd.dsp_taken = 0;
  if (!vxd.entry)
    return 0;
  // the owner field tells if this acquire is the one that takes the DSP,
  // 0002 alone can't since it also succeeds for the current owner
  if (vxd_read_adi() == 0)
    owner = vxd_adi_dword(ADI_DSP_OWNER);
  was_free = vxd.adi_valid && !owner;
  regs_init(&r);
  r.eax = esshw.audio_base; // AX = the device's Audio_Base
  r.ebx = 1;                // BX = 1, the DSP
  err = vxd_call(0x0002, &r);
  if (err == ESSHW_EINUSE)
    return ESSHW_EINUSE; // a DOS box owns it
  if (err)
    return 0; // not a port range of this driver, so nothing is trapped
  vxd.dsp_taken = (u8)was_free;
  // the DSP now belongs to the caller's VM, which is Windows
  if (!vxd.sys_vm) {
    if (owner)
      vxd.sys_vm = owner;
    else if (vxd_read_adi() == 0)
      vxd.sys_vm = vxd_adi_dword(ADI_DSP_OWNER);
  }
  return 0;
}

void vxd_dsp_end(void) {
  vxd_regs r;

  if (!vxd.dsp_taken)
    return;
  vxd.dsp_taken = 0;
  regs_init(&r);
  r.eax = esshw.audio_base;
  r.ebx = 1;
  vxd_call(0x0003, &r);
}

int vxd_ext_call(u16 fn, u8 bl, u8 bh, u8 al, u8 *result) {
  vxd_regs r;
  int err;

  regs_init(&r);
  r.eax = al;
  r.ebx = ((u16)bh << 8) | bl;
  err = vxd_call(fn, &r);
  if (!err && result)
    *result = (u8)r.eax;
  return err;
}

int vxd_ext_mixer_block(u8 ESS_FAR *buf) {
  vxd_regs r;

  regs_init(&r);
  SET_ESDI(&r, buf);
  return vxd_call(ESSX_MIXER_BLOCK, &r);
}

int vxd_ext_owners(struct vxd_owners *o) {
  vxd_regs r;
  int err;

  regs_init(&r);
  err = vxd_call(ESSX_OWNERS, &r);
  if (err)
    return err;
  o->dsp = (u8)r.eax;
  o->fm = (u8)(r.eax >> 8);
  o->mpu = (u8)r.ebx;
  o->status = (u8)(r.ebx >> 8);
  o->adi_flags = (u16)r.edx;
  return 0;
}
