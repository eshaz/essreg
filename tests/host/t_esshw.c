/*
 * t_esshw tests the esshw protocols against the simulated ES1869.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <string.h>

#include "check.h"
#include "esshw.h"
#include "simhw.h"

static void setup(u8 flags) {
  simhw_reset(0x220, 0x250);
  simhw_attach();
  esshw.flags = flags;
  esshw.dsp_timeout = 0x40;
  esshw.dsp_retries = 3;
  esshw.dsp_desync = 0;
}

// get the values written to a port, in order
static int writes(u16 port, u8 *out, int max) {
  int i, n = 0;
  for (i = 0; i < simhw.nlog; i++)
    if (simhw.log[i].dir == 'o' && simhw.log[i].port == port && n < max)
      out[n++] = simhw.log[i].value;
  return n;
}

static int reads(u16 port) {
  int i, n = 0;
  for (i = 0; i < simhw.nlog; i++)
    if (simhw.log[i].dir == 'i' && simhw.log[i].port == port)
      n++;
  return n;
}

static void test_mixer(void) {
  setup(ESSHW_SAFE);
  simhw.mixer[0x52] = 0x2A;
  simhw.mixer_index = 0x36;
  CHECK_EQ(esshw_mixer_read(0x52), 0x2A);
  CHECK_EQ(simhw.mixer_index, 0x36);
  CHECK_EQ(esshw_mixer_write(0x50, 0x0C), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);
  CHECK_EQ(simhw.mixer_index, 0x36);

  setup(ESSHW_LEGACY); // the original essreg leaves the index selected
  simhw.mixer[0x52] = 0x11;
  simhw.mixer_index = 0x36;
  CHECK_EQ(esshw_mixer_read(0x52), 0x11);
  CHECK_EQ(simhw.mixer_index, 0x52);
}

static void test_controller_safe(void) {
  u8 w[8];
  int n;

  setup(ESSHW_SAFE);
  simhw.ctrl[0xBA - 0xA0] = 0x13;
  CHECK_EQ(esshw_ctrl_read(0xBA), 0x13);
  n = writes(0x22C, w, 8);
  CHECK_EQ(n, 3);
  CHECK_EQ(w[0], 0xC6);
  CHECK_EQ(w[1], 0xC0);
  CHECK_EQ(w[2], 0xBA);
  CHECK_EQ(simhw.irq_clears, 0); // never touches Audio_Base+Eh

  simhw.nlog = 0;
  CHECK_EQ(esshw_ctrl_write(0xBB, 0x05), 0);
  CHECK_EQ(simhw.ctrl[0xBB - 0xA0], 0x05);
  n = writes(0x22C, w, 8);
  CHECK_EQ(n, 3);
  CHECK_EQ(w[0], 0xC6);
  CHECK_EQ(w[1], 0xBB);
  CHECK_EQ(w[2], 0x05);
  CHECK_EQ(esshw.dsp_desync, 0);
}

static void test_controller_legacy(void) {
  u8 w[8];

  setup(ESSHW_LEGACY);
  simhw.ext_mode = 1; // as left by the Windows driver
  simhw.ctrl[0xA1 - 0xA0] = 0xF0;
  CHECK_EQ(esshw_ctrl_read(0xA1), 0xF0);
  CHECK_EQ(writes(0x22C, w, 8), 2); // C0h, A1h: no C6h
  CHECK_EQ(w[0], 0xC0);
  CHECK_EQ(w[1], 0xA1);
  CHECK(simhw.irq_clears > 0); // polls Audio_Base+Eh like essreg did
}

static void test_controller_errors(void) {
  setup(ESSHW_SAFE);
  simhw.busy_stuck = 1;
  CHECK_EQ(esshw_ctrl_read(0xA1), -ESSHW_EBUSY);
  CHECK_EQ(reads(0x22A), 0);
  {
    u8 w[4];
    CHECK_EQ(writes(0x22C, w, 4), 0); // precheck: nothing written
  }
  CHECK_EQ(esshw.dsp_desync, 0);

  setup(ESSHW_SAFE);
  simhw.mute_dsp = 1;
  CHECK_EQ(esshw_ctrl_read(0xA1), -ESSHW_ETIMEOUT);
  // the command went in whole: a late answer goes at the next precheck
  CHECK_EQ(esshw.dsp_desync, 0);

  // the DSP takes C0h or the register, then stays busy: the rest of the
  // command never goes in, and nothing retries it
  setup(ESSHW_SAFE);
  simhw.busy_mid = 1;
  CHECK_EQ(esshw_ctrl_read(0xA1), -ESSHW_EDESYNC);
  CHECK_EQ(esshw.dsp_desync, 1);
  setup(ESSHW_SAFE);
  simhw.busy_mid = 1;
  CHECK_EQ(esshw_ctrl_write(0xB1, 0x12), -ESSHW_EDESYNC);
  CHECK_EQ(esshw.dsp_desync, 1);

  // a byte left from an answer nobody read is dropped before a read
  setup(ESSHW_SAFE);
  simhw.ctrl[0xA1 - 0xA0] = 0x55;
  simhw.rdata[simhw.nrdata++] = 0x99;
  CHECK_EQ(esshw_ctrl_read(0xA1), 0x55);

  setup(ESSHW_SAFE);
  CHECK_EQ(esshw_ctrl_read(0x40), -ESSHW_EPARAM);
  CHECK_EQ(esshw_port_read(0x10), -ESSHW_EPARAM);
  CHECK_EQ(esshw_cfg_read(0x08), -ESSHW_EPARAM);
}

static void test_ports_and_pnp(void) {
  setup(ESSHW_SAFE);
  simhw.port7 = 0x0B;
  CHECK_EQ(esshw_port_read(7), 0x0B);
  CHECK_EQ(esshw_port_write(7, 0x08), 0);
  CHECK_EQ(simhw.port7, 0x08);

  simhw.cfg_ports[6] = 0x21;
  CHECK_EQ(esshw_cfg_read(6), 0x21);

  simhw.pnp_ldn[1][0x60] = 0x02;
  simhw.pnp_card[0x22] = 0x44;
  simhw.cfg_index = 0x25;
  simhw.ldn = 3;
  CHECK_EQ(esshw_pnp_read(1, 0x60), 0x02);
  CHECK_EQ(simhw.cfg_index, 0x25);
  CHECK_EQ(simhw.ldn, 3);
  CHECK_EQ(esshw_pnp_read(0xFF, 0x22), 0x44);
  CHECK_EQ(simhw.ldn, 3);
  CHECK_EQ(esshw_pnp_write(1, 0x70, 0x07), 0);
  CHECK_EQ(simhw.pnp_ldn[1][0x70], 0x07);
  CHECK_EQ(simhw.cfg_index, 0x25);
  CHECK_EQ(simhw.ldn, 3);

  esshw.config_base = 0;
  CHECK_EQ(esshw_pnp_read(1, 0x60), -ESSHW_ENOCFG);
  CHECK_EQ(esshw_detect_config(), 0);
  CHECK_EQ(esshw.config_base, 0x250);
}

// VxD extension backend with a fake API
static u16 last_fn;
static u8 last_bl, last_bh, last_al;
static int fake_err;

static int fake_ext(u16 fn, u8 bl, u8 bh, u8 al, u8 *result) {
  last_fn = fn;
  last_bl = bl;
  last_bh = bh;
  last_al = al;
  *result = 0x5A;
  return fake_err;
}

static void test_vxd_backend(void) {
  setup(ESSHW_SAFE);
  esshw.backend = ESSHW_VXDEXT;
  esshw.ext_call = fake_ext;
  fake_err = 0;
  CHECK_EQ(esshw_mixer_read(0x52), 0x5A);
  CHECK_EQ(last_fn, ESSX_MIXER_READ);
  CHECK_EQ(last_bl, 0x52);
  CHECK_EQ(esshw_ctrl_write(0xBA, 0x07), 0);
  CHECK_EQ(last_fn, ESSX_CTRL_WRITE);
  CHECK_EQ(last_bh, 0x07);
  CHECK_EQ(esshw_pnp_write(1, 0x70, 5), 0);
  CHECK_EQ(last_fn, ESSX_PNP_WRITE);
  CHECK_EQ(last_bl, 1);
  CHECK_EQ(last_bh, 0x70);
  CHECK_EQ(last_al, 5);
  fake_err = ESSHW_EINUSE;
  CHECK_EQ(esshw_ctrl_read(0xBA), -ESSHW_EINUSE);
  CHECK_EQ(esshw_mixer_write(0x50, 0), -ESSHW_EINUSE);
  CHECK_EQ(simhw.nlog, 0); // no direct port access
}

int main(void) {
  test_mixer();
  test_controller_safe();
  test_controller_legacy();
  test_controller_errors();
  test_ports_and_pnp();
  test_vxd_backend();
  return CHECK_DONE("t_esshw");
}
