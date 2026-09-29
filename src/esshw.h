/*
 * ES1869 hardware access shared by essreg (DOS) and essctl (16-bit
 * Windows).
 *
 * Notes:
 *
 * Every register access goes through the `esshw` context. It picks the
 * backend: direct port I/O, the essreg API of the extended ES1869.VXD
 * (group 4, see docs/VXD_API.md) or a simulated chip. For direct I/O it
 * also picks the protocol:
 *   - ESSHW_LEGACY: the same port sequences as the original essreg
 *   - ESSHW_SAFE: restores the mixer index, keeps index/data pairs atomic,
 *     sends C6h before controller access, polls Audio_Base+Ch bit 6
 *     instead of Audio_Base+Eh (reading that clears the audio interrupt)
 *     and won't start a DSP transaction while the DSP is busy
 *
 * Functions return the register value (0-255) or a negative ESSHW_E* code.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef ESSHW_H
#define ESSHW_H

#include "esstypes.h"

enum esshw_backend { ESSHW_DIRECT, ESSHW_VXDEXT, ESSHW_SIM };

// protocol flags for direct port I/O
#define ESSHW_F_SAVE_MIXIDX 0x01 // save and restore Audio_Base+4
#define ESSHW_F_CRIT 0x02        // interrupts off around port pairs
#define ESSHW_F_POLL_C 0x04      // poll Audio_Base+Ch bit 6 for read data
#define ESSHW_F_EXT_C6 0x08      // send C6h before controller access
#define ESSHW_F_PRECHECK 0x10    // refuse DSP access unless it's idle
#define ESSHW_F_FM_TRAPPED 0x20  // ES1869.VXD traps the FM ports: leave them
#define ESSHW_LEGACY 0x00
#define ESSHW_SAFE 0x1F

// error codes, returned negated (1-6 match the VxD API)
#define ESSHW_ENODEV 1
#define ESSHW_EINUSE 2
#define ESSHW_EPARAM 3
#define ESSHW_EBUSY 4
#define ESSHW_ETIMEOUT 5
#define ESSHW_ENOCFG 6
#define ESSHW_EFAIL 7   // the driver rejected the call
#define ESSHW_EDESYNC 8 // the DSP took half a command and never the rest

// VxD essreg API function numbers (DX)
#define ESSX_INFO 0x0400
#define ESSX_MIXER_READ 0x0401
#define ESSX_MIXER_WRITE 0x0402
#define ESSX_CTRL_READ 0x0403
#define ESSX_CTRL_WRITE 0x0404
#define ESSX_PORT_READ 0x0405
#define ESSX_PORT_WRITE 0x0406
#define ESSX_CFG_READ 0x0407
#define ESSX_CFG_WRITE 0x0408
#define ESSX_PNP_READ 0x0409
#define ESSX_PNP_WRITE 0x040A
#define ESSX_MIXER_BLOCK 0x040B
#define ESSX_OWNERS 0x040C

// call the VxD API, returns 0 or an ESSHW_E* code with AL in *result
typedef int (*esshw_ext_fn)(u16 fn, u8 bl, u8 bh, u8 al, u8 *result);
typedef u8 (*esshw_in_fn)(u16 port);
typedef void (*esshw_out_fn)(u16 port, u8 value);

typedef struct {
  u16 audio_base;  // Audio_Base, default 220h
  u16 config_base; // Config_Base, 0 until detected
  u16 dsp_timeout; // DSP handshake poll limit
  u16 dsp_retries; // controller access retries (legacy protocol)
  u8 flags;        // ESSHW_F_*
  u8 backend;      // enum esshw_backend
  u8 dsp_desync;   // set when a DSP transaction was abandoned half way
  esshw_ext_fn ext_call;
  esshw_in_fn sim_in;
  esshw_out_fn sim_out;
} esshw_ctx;

extern esshw_ctx esshw;

u8 esshw_inb(u16 port);
void esshw_outb(u16 port, u8 value);

int esshw_mixer_read(u8 reg);
int esshw_mixer_write(u8 reg, u8 value);
int esshw_ctrl_read(u8 reg);
int esshw_ctrl_write(u8 reg, u8 value);
int esshw_port_read(u8 offset); // Audio_Base + offset
int esshw_port_write(u8 offset, u8 value);
int esshw_cfg_read(u8 offset); // Config_Base + offset
int esshw_cfg_write(u8 offset, u8 value);
int esshw_pnp_read(u8 ldn, u8 reg); // ldn FFh for card level
int esshw_pnp_write(u8 ldn, u8 reg, u8 value);

// read the mixer 40h identification sequence: 18h 69h cfg-hi cfg-lo
int esshw_mixer_id(u8 id[4]);

// find Config_Base from the identification sequence, 0 on success
int esshw_detect_config(void);

// 1 when the DSP accepts a command and has no unread data
int esshw_dsp_idle(void);

const char *esshw_strerror(int err);

#endif /* ESSHW_H */
