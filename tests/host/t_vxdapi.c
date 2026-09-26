/*
 * t_vxdapi tests the vxdapi wrappers against a scripted stand-in for the
 * ES1869.VXD V86/PM API.
 *
 * The fake models what the wrappers depend on:
 *   - 0001 copies an ADI whose owner fields the test controls
 *   - 0002/0003 acquire and release the DSP like Acquire_Resources and
 *     Release_Resources
 *   - group 4 exists only when `ext` is set
 *
 * Every call is logged, so the test can prove that functions with side
 * effects are called only when intended and that the callback functions
 * are never called at all.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <string.h>

#include "check.h"
#include "esshw.h"
#include "vxdapi.h"

#define SYS_VM 0xC1001000UL
#define DOS_VM 0xC1402000UL
#define DEVNODE 0xC0FFEE11UL

static struct {
  int present, ext;
  u8 adi[ADI_COPY_SIZE];
  u16 calls[64];
  int ncalls;
  u8 last_bl, last_bh, last_al;
} fake;

// every function called during the whole run
static u16 all_calls[512];
static int nall;

static void put32(u8 *p, u32 v) {
  p[0] = (u8)v;
  p[1] = (u8)(v >> 8);
  p[2] = (u8)(v >> 16);
  p[3] = (u8)(v >> 24);
}

static u32 get32(const u8 *p) {
  return p[0] | (p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void fake_reset(int ext) {
  memset(&fake, 0, sizeof(fake));
  fake.present = 1;
  fake.ext = ext;
  put32(fake.adi, 0x12345678);             // overwritten size field
  fake.adi[ADI_AUDIO_BASE] = 0x20;
  fake.adi[ADI_AUDIO_BASE + 1] = 0x02;
  put32(fake.adi + ADI_DEVNODE, DEVNODE);
  esshw.audio_base = 0x220;
}

void *vxd_get_entry(u16 device_id) {
  return fake.present && device_id == VXD_AUDDRV_ID ? (void *)&fake : 0;
}

static int fail(vxd_regs *r, u16 code) {
  r->eax = (r->eax & 0xFFFF0000UL) | code;
  return 1;
}

int vxd_raw_call(void *entry, vxd_regs *r) {
  u16 fn = (u16)r->edx;
  u8 *buf = r->host_buf;
  u32 owner;

  if (entry != (void *)&fake)
    return 1;
  if (fake.ncalls < 64)
    fake.calls[fake.ncalls++] = fn;
  if (nall < 512)
    all_calls[nall++] = fn;
  fake.last_bl = (u8)r->ebx;
  fake.last_bh = (u8)(r->ebx >> 8);
  fake.last_al = (u8)r->eax;

  if ((fn >> 8) == 4) {
    if (!fake.ext)
      return 1; // stock dispatcher: group out of range, AX unchanged
    if (r->ecx != DEVNODE)
      return fail(r, 1);
    switch (fn) {
    case 0x0400:
      r->eax = 0x0100;
      r->ebx = 0x7F;
      r->edx = 13;
      return 0;
    case 0x0401:
      r->eax = 0x5A;
      return 0;
    case 0x040B:
      memset(buf, 0x33, 128);
      return 0;
    case 0x040C:
      r->eax = 0x0201; // DSP: caller's VM, FM: another VM
      r->ebx = 0x8000; // MPU: none, status 80h
      r->edx = ADI_F_MPU_SHARED;
      return 0;
    }
    return 0;
  }

  switch (fn) {
  case 0x0000:
    r->eax = 0x0404;
    return 0;
  case 0x0001:
    if ((r->eax & 0xFFFF) > 1 || get32(buf) > ADI_COPY_SIZE)
      return fail(r, 0);
    if ((r->eax & 1) && r->ecx != DEVNODE)
      return fail(r, 0);
    memcpy(buf, fake.adi, get32(buf));
    r->eax = 1;
    return 0;
  case 0x0002: // the system VM acquires the DSP
    if ((r->eax & 0xFFFF) != 0x220 || (r->ebx & 0xFFFF) != 1)
      return fail(r, 1);
    owner = get32(fake.adi + ADI_DSP_OWNER);
    if (owner && owner != SYS_VM)
      return fail(r, 2);
    put32(fake.adi + ADI_DSP_OWNER, SYS_VM);
    r->eax = 0;
    return 0;
  case 0x0003:
    if (get32(fake.adi + ADI_DSP_OWNER) != SYS_VM)
      return fail(r, 2);
    put32(fake.adi + ADI_DSP_OWNER, 0);
    r->eax = 0;
    return 0;
  case 0x0004:
    r->eax = (r->ebx & 0xFFFF) ? 0x2000 : 0x1000;
    return 0;
  case 0x0005:
    if (r->eax == 0)
      return 0; // would write the GPO pins
    r->ebx = 2;
    return 0;
  case 0x0008:
    r->edx = 0x800;
    return 0;
  case 0x000A:
    r->eax = 1;
    return 0;
  case 0x0101:
    buf[6] = 0x88;
    buf[7] = 0x03;
    r->eax = 1;
    return 0;
  case 0x0301:
    buf[6] = 0x30;
    buf[7] = 0x03;
    buf[8] = 9;
    r->eax = 1;
    return 0;
  }
  return fail(r, 0);
}

static int count_calls(u16 fn) {
  int i, n = 0;
  for (i = 0; i < fake.ncalls; i++)
    n += fake.calls[i] == fn;
  return n;
}

static void test_open_stock(void) {
  u16 port = 0, count = 0;
  u8 v = 0, irq = 0;

  fake_reset(0);
  CHECK_EQ(vxd_open(), 0);
  CHECK_EQ(vxd.version, 0x0404);
  CHECK_EQ(vxd.devnode, DEVNODE);
  CHECK_EQ(vxd.ext_version, 0);
  CHECK(vxd.adi_valid);
  CHECK_EQ(vxd_adi_word(ADI_AUDIO_BASE), 0x220);
  CHECK_EQ(vxd_dma_count(1, &count), 0);
  CHECK_EQ(count, 0x2000);
  CHECK_EQ(vxd_gpo_read(&v), 0);
  CHECK_EQ(v, 2);
  CHECK_EQ(vxd_config_port(&port), 0);
  CHECK_EQ(port, 0x800);
  CHECK_EQ(vxd_global_flag(&v), 0);
  CHECK_EQ(v, 1);
  CHECK_EQ(vxd_fm_info(&port), 0);
  CHECK_EQ(port, 0x388);
  CHECK_EQ(vxd_mpu_info(&port, &irq), 0);
  CHECK_EQ(port, 0x330);
  CHECK_EQ(irq, 9);
  // the stock driver refuses group 4
  CHECK_EQ(vxd_ext_call(ESSX_MIXER_READ, 0x36, 0, 0, &v), ESSHW_EFAIL);
}

static void test_open_missing(void) {
  fake_reset(0);
  fake.present = 0;
  CHECK_EQ(vxd_open(), ESSHW_ENODEV);
  CHECK(!vxd.entry);
  CHECK_EQ(vxd_dsp_begin(), 0);
  vxd_dsp_end();
  CHECK_EQ(fake.ncalls, 0);
}

static void test_open_ext(void) {
  u8 v = 0;
  u8 block[128];
  struct vxd_owners o;

  fake_reset(1);
  CHECK_EQ(vxd_open(), 0);
  CHECK_EQ(vxd.ext_version, 0x0100);
  CHECK_EQ(vxd.ext_features, 0x7F);
  CHECK_EQ(vxd.ext_count, 13);
  CHECK_EQ(vxd_ext_call(ESSX_MIXER_READ, 0x36, 0, 0, &v), 0);
  CHECK_EQ(v, 0x5A);
  CHECK_EQ(fake.last_bl, 0x36);
  CHECK_EQ(vxd_ext_call(ESSX_PNP_WRITE, 0x01, 0x70, 0x05, 0), 0);
  CHECK_EQ(fake.last_bl, 0x01); // LDN
  CHECK_EQ(fake.last_bh, 0x70); // register
  CHECK_EQ(fake.last_al, 0x05); // value
  CHECK_EQ(vxd_ext_mixer_block(block), 0);
  CHECK_EQ(block[127], 0x33);
  CHECK_EQ(vxd_ext_owners(&o), 0);
  CHECK_EQ(o.dsp, VXD_OWNER_SELF);
  CHECK_EQ(o.fm, VXD_OWNER_OTHER);
  CHECK_EQ(o.mpu, VXD_OWNER_NONE);
  CHECK_EQ(o.status, 0x80);
  // group 4 calls never acquire anything
  CHECK_EQ(count_calls(0x0002), 0);
  CHECK_EQ(count_calls(0x0003), 0);
  // direct port access (essctl /novxd) is bracketed like on the stock
  // driver, since the extended driver traps the ports too
  CHECK_EQ(vxd_dsp_begin(), 0);
  vxd_dsp_end();
  CHECK_EQ(count_calls(0x0002), 1);
  CHECK_EQ(count_calls(0x0003), 1);
}

static void test_dsp_bracket(void) {
  // free DSP: acquire, then release afterwards
  fake_reset(0);
  vxd_open();
  CHECK_EQ(vxd_owner_class(DOS_VM), VXD_OWNER_UNKNOWN);
  CHECK_EQ(vxd_dsp_begin(), 0);
  CHECK_EQ(get32(fake.adi + ADI_DSP_OWNER), SYS_VM);
  CHECK_EQ(vxd.sys_vm, SYS_VM); // learned from the acquire
  CHECK_EQ(vxd_owner_class(SYS_VM), VXD_OWNER_SELF);
  CHECK_EQ(vxd_owner_class(DOS_VM), VXD_OWNER_OTHER);
  CHECK_EQ(vxd_owner_class(0), VXD_OWNER_NONE);
  vxd_dsp_end();
  CHECK_EQ(get32(fake.adi + ADI_DSP_OWNER), 0);
  CHECK_EQ(count_calls(0x0003), 1);
  vxd_dsp_end(); // a second end does nothing
  CHECK_EQ(count_calls(0x0003), 1);

  // Windows already owns it (a wave device is open): never release
  fake_reset(0);
  put32(fake.adi + ADI_DSP_OWNER, SYS_VM);
  vxd_open();
  CHECK_EQ(vxd_dsp_begin(), 0);
  CHECK_EQ(vxd.sys_vm, SYS_VM);
  vxd_dsp_end();
  CHECK_EQ(get32(fake.adi + ADI_DSP_OWNER), SYS_VM);
  CHECK_EQ(count_calls(0x0003), 0);

  // a DOS box owns it: refuse before any port access
  fake_reset(0);
  put32(fake.adi + ADI_DSP_OWNER, DOS_VM);
  vxd_open();
  CHECK_EQ(vxd_dsp_begin(), ESSHW_EINUSE);
  CHECK_EQ(vxd.sys_vm, 0); // not learned from a refusal
  vxd_dsp_end();
  CHECK_EQ(get32(fake.adi + ADI_DSP_OWNER), DOS_VM);
  CHECK_EQ(count_calls(0x0003), 0);

  // a base the driver does not own: nothing is trapped, carry on
  fake_reset(0);
  vxd_open();
  esshw.audio_base = 0x240;
  CHECK_EQ(vxd_dsp_begin(), 0);
  vxd_dsp_end();
  CHECK_EQ(count_calls(0x0003), 0);
}

// run after all other tests: no wrapper ever called a function with lasting
// side effects on the driver
static void test_never_called(void) {
  static const u16 forbidden[] = {0x0006, 0x0007, 0x0009, 0x000B,
                                  0x0200, 0x0201, 0x0102, 0x0103,
                                  0x0302, 0x0303};
  unsigned i;
  int j, n;

  for (i = 0; i < ESS_ARRAY_SIZE(forbidden); i++) {
    for (n = j = 0; j < nall; j++)
      n += all_calls[j] == forbidden[i];
    CHECK_EQ(n, 0);
  }
  CHECK(nall > 20);
}

int main(void) {
  test_open_stock();
  test_open_missing();
  test_open_ext();
  test_dsp_bracket();
  test_never_called();
  return CHECK_DONE("t_vxdapi");
}
