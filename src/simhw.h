/*
 * simhw.h -- a port-level model of the ES1869 for tests and essctl /sim.
 *
 * Models the mixer (with the 40h identification sequence), the DSP command
 * channel (C6h, C0h, Axh/Bxh, software reset through Audio_Base+6), the
 * power-management port and the configuration device with PnP card and
 * logical-device registers.  Every port access is logged.
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#ifndef SIMHW_H
#define SIMHW_H

#include "esstypes.h"

#define SIMHW_LOG_MAX 512

struct simhw_log {
  u8 dir; /* 'i' or 'o' */
  u8 value;
  u16 port;
};

struct simhw_state {
  u16 audio_base;
  u16 config_base;
  u8 mixer[256];
  u8 mixer_index;
  u8 id_seq;
  u8 ctrl[32];       /* A0h-BFh */
  u8 ext_mode;       /* C6h received since the last reset */
  u8 pending[2];     /* DSP command waiting for its operand */
  u8 npending;
  u8 rdata[4];       /* read buffer */
  u8 nrdata;
  u8 port6, port7;   /* reset/status, power management */
  u8 in_reset;
  u8 cfg_index;
  u8 ldn;
  u8 pnp_card[0x30];
  u8 pnp_ldn[8][0x100];
  u8 cfg_ports[8];
  /* fault injection */
  u8 busy_stuck;     /* Audio_Base+Ch bit 7 stays set */
  u8 mute_dsp;       /* the DSP never returns data */
  /* statistics */
  u16 irq_clears;    /* reads of Audio_Base+Eh */
  u16 nlog;
  struct simhw_log log[SIMHW_LOG_MAX];
};

extern struct simhw_state simhw;

void simhw_reset(u16 audio_base, u16 config_base);
u8 simhw_in(u16 port);
void simhw_out(u16 port, u8 value);

/* route esshw through the simulator */
void simhw_attach(void);

#endif /* SIMHW_H */
