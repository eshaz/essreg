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
 * The effect is the fx.3d fields of the catalog: fx.3d.enable (mixer 50h
 * bit 3), fx.3d.run (50h bit 2, active-low reset), fx.3d.mono and
 * fx.3d.limit (50h bits 1 and 0, undocumented: the model and the limit,
 * docs/SPATIALIZER.md), fx.3d.level (52h bits
 * 5:0), and the Spatializer's undocumented registers (54h-5Ah). Every
 * other fx.3d field is one of those registers, so a register added to the
 * catalog is in the reg command and the tray panel too. Only the fields
 * that change are written, read-modify-write, so the other bits keep what
 * the driver put there.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef ESS3D_H
#define ESS3D_H

#include "esstypes.h"

#define ESS3D_MAX_ACTIONS 16
#define ESS3D_MAX_REGS 16 // Spatializer registers, see ess3d_reg_field
#define ESS3D_STEP 4      // up and down without a number
#define ESS3D_TIME 1500   // display time in ms, /t= changes it
#define ESS3D_MISMATCH 1  // ess3d_run: the chip returned other values

// commands, run in the order of the command line
enum ess3d_op {
  ESS3D_SHOW,    // show: change nothing
  ESS3D_ON,      // on: release from reset and enable
  ESS3D_OFF,     // off: bypass
  ESS3D_TOGGLE,  // toggle: off if it's on and running, else on
  ESS3D_HOLD,    // hold: keep the effect in reset
  ESS3D_RESET,   // reset: reset, then release, keeping on/off and level
  ESS3D_LEVEL,   // level N
  ESS3D_ADD,     // level +N, level -N, up, down
  ESS3D_LIMIT,   // limit on (arg 1), off (0) or toggle (2)
  ESS3D_MONO,    // model (or mono) on (arg 1), off (0) or toggle (2)
  ESS3D_REG,     // reg XX YY: register reg (ess3d_reg_field) to value
  ESS3D_DEFAULTS // what ESS's driver sets when Windows starts
};

struct ess3d_action {
  u8 op;    // enum ess3d_op
  s8 arg;   // ESS3D_LEVEL: 0-63, ESS3D_ADD: -63 to 63, ESS3D_LIMIT/MONO
  u8 reg;   // ESS3D_REG: index for ess3d_reg_field
  u8 value; // ESS3D_REG: the value
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
  u8 tray;         // tray: stay in the taskbar's tray
  u8 exit;         // exit: close the tray icon
  u8 measure;      // measure: the measurement of ess3dms.c, alone
  u8 plan;         // its plan, S3D_PLAN_FULL to _LIMIT (s3dmeas.h)
  u8 plan_reg;     // the register of S3D_PLAN_REG
  char out[128];   // /out=, the measurement's report
  char log[128];   // /log=, the file every result is appended to
  char err[80];    // what was wrong when parsing failed
};

struct ess3d_state {
  u8 enable;              // fx.3d.enable
  u8 run;                 // fx.3d.run, 0 = held in reset
  u8 level;               // fx.3d.level
  u8 limit;               // fx.3d.limit
  u8 mono;                // fx.3d.mono
  u8 reg[ESS3D_MAX_REGS]; // the Spatializer registers, ess3d_reg_field
};

// parse the command line (switches and commands, any order)
// returns 0, or -1 with the first problem in c->err
// the whole line is read either way, so the switches are set
int ess3d_parse(const char *line, struct ess3d_cmd *c);

// the highest level, 63
int ess3d_level_max(void);

// the Spatializer registers: every fx.3d field of the catalog other than
// enable, run, mono, limit and level, in catalog order
// ess3d_regs is how many, ess3d_reg_field the field of register i
int ess3d_regs(void);
int ess3d_reg_field(int i);

// register i's value if the chip had it, as ESS's driver sets it when
// Windows starts, or -1 if the driver doesn't set it
int ess3d_reg_default(int i);

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
