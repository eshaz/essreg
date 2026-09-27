/*
 * The commands of ess3d, which switches the 3-D effect (Spatializer) of
 * the ES1869 from the command line.
 *
 * Notes:
 *
 * The parsing and the register changes are here, so the host tests run
 * them against the simulated ES1869. src/win/ess3dw.c is the program
 * around them: the access path (winio), the display and the messages.
 *
 * The effect is three fields of the catalog: fx.3d.enable (mixer 50h
 * bit 3), fx.3d.run (50h bit 2, active-low reset) and fx.3d.level (52h
 * bits 5:0). Only the fields that change are written, read-modify-write,
 * so the other bits of 50h keep what the driver put there.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef ESS3D_H
#define ESS3D_H

#include "esstypes.h"

#define ESS3D_MAX_ACTIONS 16
#define ESS3D_STEP 4     // up and down without a number
#define ESS3D_TIME 1500  // display time in ms, /t= changes it
#define ESS3D_MISMATCH 1 // ess3d_run: the chip returned other values

// commands, run in the order of the command line
enum ess3d_op {
  ESS3D_SHOW,   // show: change nothing
  ESS3D_ON,     // on: release from reset and enable
  ESS3D_OFF,    // off: bypass
  ESS3D_TOGGLE, // toggle: off if it's on and running, else on
  ESS3D_HOLD,   // hold: keep the effect in reset
  ESS3D_RESET,  // reset: reset, then release, keeping on/off and level
  ESS3D_LEVEL,  // level N
  ESS3D_ADD     // level +N, level -N, up, down
};

struct ess3d_action {
  u8 op;  // enum ess3d_op
  s8 arg; // ESS3D_LEVEL: 0-63, ESS3D_ADD: -63 to 63
};

struct ess3d_cmd {
  struct ess3d_action act[ESS3D_MAX_ACTIONS];
  int nact;
  u16 audio_base;  // /base=, 0 = from the driver
  u16 config_base; // /cfg=
  u16 time_ms;     // /t=, how long the display stays
  u8 sim;          // /sim
  u8 novxd;        // /novxd
  u8 quiet;        // /q
  char log[128];   // /log=, the file every result is appended to
  char err[80];    // what was wrong when parsing failed
};

struct ess3d_state {
  u8 enable; // fx.3d.enable
  u8 run;    // fx.3d.run, 0 = held in reset
  u8 level;  // fx.3d.level
};

// parse the command line (switches and commands, any order)
// returns 0, or -1 with the first problem in c->err
// the whole line is read either way, so the switches are set
int ess3d_parse(const char *line, struct ess3d_cmd *c);

// the highest level, 63
int ess3d_level_max(void);

// read the effect's fields, 0 or a negative ESSHW_E* code
int ess3d_read(struct ess3d_state *s);

// read the state, run the commands in order and read it back
// returns 0, ESS3D_MISMATCH when the chip returned other values than
// were written, or a negative ESSHW_E* code
// *s holds what the chip returned
int ess3d_run(const struct ess3d_cmd *c, struct ess3d_state *s);

// "3-D on, level 40 of 63"
void ess3d_text(const struct ess3d_state *s, char *buf, unsigned size);

#endif /* ESS3D_H */
