/*
 * simhw.c -- a port-level model of the ES1869 (see simhw.h).
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#include <string.h>

#include "esshw.h"
#include "simhw.h"

struct simhw_state simhw;

/* mixer reset values that differ from zero (DS p.57-66) */
static const u8 mixer_defaults[][2] = {
    {0x14, 0x88}, {0x32, 0x88}, {0x36, 0x88}, {0x38, 0x00}, {0x3C, 0x00},
    {0x60, 0x3F}, {0x62, 0x3F}, {0x7C, 0x88}, {0x04, 0x88}, {0x22, 0x88},
    {0x26, 0x88}};

static void log_access(u8 dir, u16 port, u8 value) {
  if (simhw.nlog < SIMHW_LOG_MAX) {
    simhw.log[simhw.nlog].dir = dir;
    simhw.log[simhw.nlog].port = port;
    simhw.log[simhw.nlog].value = value;
    simhw.nlog++;
  }
}

static void dsp_reset(void) {
  u8 ba = simhw.ctrl[0xBA - 0xA0];
  u8 bb = simhw.ctrl[0xBB - 0xA0];

  memset(simhw.ctrl, 0, sizeof(simhw.ctrl));
  simhw.ctrl[0xBA - 0xA0] = ba; /* ADC offsets survive a software reset */
  simhw.ctrl[0xBB - 0xA0] = bb;
  simhw.ext_mode = 0;
  simhw.npending = 0;
  simhw.nrdata = 1;
  simhw.rdata[0] = 0xAA; /* reset acknowledge */
}

void simhw_reset(u16 audio_base, u16 config_base) {
  unsigned i;

  memset(&simhw, 0, sizeof(simhw));
  simhw.audio_base = audio_base;
  simhw.config_base = config_base;
  for (i = 0; i < ESS_ARRAY_SIZE(mixer_defaults); i++)
    simhw.mixer[mixer_defaults[i][0]] = mixer_defaults[i][1];
  simhw.port7 = 0x08; /* analog stays on */
  /* LDN1 (audio) resources: 220h, FM 388h, MPU 330h, IRQ 5, DMA 1 and 0 */
  simhw.pnp_ldn[1][0x30] = 0x01;
  simhw.pnp_ldn[1][0x60] = (u8)(audio_base >> 8);
  simhw.pnp_ldn[1][0x61] = (u8)audio_base;
  simhw.pnp_ldn[1][0x62] = 0x03;
  simhw.pnp_ldn[1][0x63] = 0x88;
  simhw.pnp_ldn[1][0x64] = 0x03;
  simhw.pnp_ldn[1][0x65] = 0x30;
  simhw.pnp_ldn[1][0x70] = 0x05;
  simhw.pnp_ldn[1][0x74] = 0x01;
  simhw.pnp_ldn[1][0x75] = 0x00;
  simhw.pnp_ldn[0][0x60] = (u8)(config_base >> 8);
  simhw.pnp_ldn[0][0x61] = (u8)config_base;
  simhw.pnp_card[0x06] = 0x00;
}

static void dsp_write(u8 value) {
  if (simhw.npending) {
    u8 cmd = simhw.pending[0];
    simhw.npending = 0;
    if (cmd == 0xC0) {
      if (simhw.ext_mode && value >= 0xA0 && value <= 0xBF && !simhw.mute_dsp &&
          simhw.nrdata < sizeof(simhw.rdata))
        simhw.rdata[simhw.nrdata++] = simhw.ctrl[value - 0xA0];
    } else if (cmd >= 0xA0 && cmd <= 0xBF && simhw.ext_mode) {
      simhw.ctrl[cmd - 0xA0] = value;
    }
    return;
  }
  if (value == 0xC6)
    simhw.ext_mode = 1;
  else if (value == 0xC0 || (value >= 0xA0 && value <= 0xBF)) {
    simhw.pending[0] = value;
    simhw.npending = 1;
  }
}

static u8 rdata_pop(void) {
  u8 v;

  if (!simhw.nrdata)
    return 0xFF;
  v = simhw.rdata[0];
  memmove(simhw.rdata, simhw.rdata + 1, --simhw.nrdata);
  return v;
}

static u8 pnp_read(void) {
  u8 idx = simhw.cfg_index;

  if (idx == 0x07)
    return simhw.ldn;
  if (idx < 0x30)
    return simhw.pnp_card[idx];
  return simhw.pnp_ldn[simhw.ldn & 7][idx];
}

static void pnp_write(u8 value) {
  u8 idx = simhw.cfg_index;

  if (idx == 0x07)
    simhw.ldn = value;
  else if (idx < 0x30)
    simhw.pnp_card[idx] = value;
  else
    simhw.pnp_ldn[simhw.ldn & 7][idx] = value;
}

static u8 sim_in(u16 port) {
  u16 off = port - simhw.audio_base;
  u8 id[4];

  if (port >= simhw.config_base && port < simhw.config_base + 8) {
    u16 c = port - simhw.config_base;
    if (c == 0)
      return simhw.cfg_index;
    if (c == 1)
      return pnp_read();
    return simhw.cfg_ports[c];
  }
  switch (off) {
  case 0x04:
    return simhw.mixer_index;
  case 0x05:
    if (simhw.mixer_index == 0x40) {
      id[0] = 0x18;
      id[1] = 0x69;
      id[2] = (u8)(simhw.config_base >> 8);
      id[3] = (u8)simhw.config_base;
      return id[simhw.id_seq++ & 3];
    }
    return simhw.mixer[simhw.mixer_index];
  case 0x06:
    return simhw.port6;
  case 0x07:
    return simhw.port7;
  case 0x0A:
    return rdata_pop();
  case 0x0C:
    return (u8)((simhw.busy_stuck ? 0x80 : 0) | (simhw.nrdata ? 0x40 : 0));
  case 0x0E:
    simhw.irq_clears++;
    return (u8)(simhw.nrdata ? 0x80 : 0);
  }
  return 0xFF;
}

static void sim_out(u16 port, u8 value) {
  u16 off = port - simhw.audio_base;

  if (port >= simhw.config_base && port < simhw.config_base + 8) {
    u16 c = port - simhw.config_base;
    if (c == 0)
      simhw.cfg_index = value;
    else if (c == 1)
      pnp_write(value);
    else
      simhw.cfg_ports[c] = value;
    return;
  }
  switch (off) {
  case 0x04:
    simhw.mixer_index = value;
    simhw.id_seq = 0;
    break;
  case 0x05:
    if (simhw.mixer_index == 0x00)
      simhw_reset(simhw.audio_base, simhw.config_base);
    else
      simhw.mixer[simhw.mixer_index] = value;
    break;
  case 0x06:
    if (value & 0x01)
      simhw.in_reset = 1;
    else if (simhw.in_reset) {
      simhw.in_reset = 0;
      dsp_reset();
    }
    simhw.port6 = value;
    break;
  case 0x07:
    simhw.port7 = (u8)(value & ~0xC4); /* pulses and GPI read back as 0 */
    break;
  case 0x0C:
    dsp_write(value);
    break;
  }
}

u8 simhw_in(u16 port) {
  u8 v = sim_in(port);
  log_access('i', port, v);
  return v;
}

void simhw_out(u16 port, u8 value) {
  log_access('o', port, value);
  sim_out(port, value);
}

void simhw_attach(void) {
  esshw.backend = ESSHW_SIM;
  esshw.sim_in = simhw_in;
  esshw.sim_out = simhw_out;
  esshw.audio_base = simhw.audio_base;
  esshw.config_base = simhw.config_base;
}
