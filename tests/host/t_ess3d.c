/*
 * t_ess3d checks ess3d's command line and its 3-D register changes against
 * the simulated ES1869: on, off and toggle, hold and reset on the run bit,
 * absolute and relative levels with clamping, several commands in a row,
 * the limit and mono bits, the Spatializer registers, the driver's
 * defaults, and bad input.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <string.h>

#include "check.h"
#include "ess3d.h"
#include "esscat.h"
#include "esshw.h"
#include "simhw.h"

// faults of a broken chip, see fault_out
static int drop_52;         // writes to 52h are lost
static int reset_clears_52; // resetting the effect clears 52h

static void fault_out(u16 port, u8 value) {
  if (port == 0x225 && simhw.mixer_index == 0x52 && drop_52)
    return;
  simhw_out(port, value);
  if (port == 0x225 && simhw.mixer_index == 0x50 && !(value & 0x04) &&
      reset_clears_52)
    simhw.mixer[0x52] = 0;
}

static int ext_in_use(u16 fn, u8 bl, u8 bh, u8 al, u8 *result) {
  (void)fn;
  (void)bl;
  (void)bh;
  (void)al;
  (void)result;
  return ESSHW_EINUSE;
}

// a chip with 50h and 52h set, and an empty port log
static void chip(u8 mx50, u8 mx52) {
  simhw_reset(0x220, 0x800);
  simhw_attach();
  esshw.sim_out = fault_out;
  esshw.flags = ESSHW_SAFE;
  esshw.dsp_timeout = 0x40;
  simhw.mixer[0x50] = mx50;
  simhw.mixer[0x52] = mx52;
  simhw.nlog = 0;
  drop_52 = reset_clears_52 = 0;
}

// the values written to a mixer register since the log was emptied
static int writes(u8 reg, u8 *out, int max) {
  int i, n = 0;
  u8 index = 0;

  for (i = 0; i < simhw.nlog; i++) {
    const struct simhw_log *l = &simhw.log[i];
    if (l->dir != 'o')
      continue;
    if (l->port == 0x224)
      index = l->value;
    else if (l->port == 0x225 && index == reg && n < max)
      out[n++] = l->value;
  }
  return n;
}

// parse a command line and run it on the chip, with a fresh port log
static int run(const char *line, struct ess3d_state *s) {
  struct ess3d_cmd c;

  simhw.nlog = 0;
  if (ess3d_parse(line, &c) < 0) {
    printf("t_ess3d: \"%s\" did not parse: %s\n", line, c.err);
    return -100;
  }
  return ess3d_run(&c, s);
}

static int text_is(const struct ess3d_state *s, const char *want) {
  char buf[64];

  ess3d_text(s, buf, sizeof(buf));
  if (strcmp(buf, want)) {
    printf("t_ess3d: \"%s\", expected \"%s\"\n", buf, want);
    return 0;
  }
  return 1;
}

static void test_parse(void) {
  struct ess3d_cmd c;

  CHECK_EQ(ess3d_parse("on", &c), 0);
  CHECK_EQ(c.nact, 1);
  CHECK_EQ(c.act[0].op, ESS3D_ON);
  CHECK_EQ(c.time_ms, ESS3D_TIME);
  CHECK(!c.quiet && !c.sim && !c.novxd && !c.log[0]);
  CHECK_EQ(c.audio_base, 0);

  CHECK_EQ(ess3d_parse("  ON\tLevel 40 ", &c), 0);
  CHECK_EQ(c.nact, 2);
  CHECK_EQ(c.act[0].op, ESS3D_ON);
  CHECK_EQ(c.act[1].op, ESS3D_LEVEL);
  CHECK_EQ(c.act[1].arg, 40);

  CHECK_EQ(ess3d_parse("toggle hold reset show off", &c), 0);
  CHECK_EQ(c.nact, 5);
  CHECK_EQ(c.act[0].op, ESS3D_TOGGLE);
  CHECK_EQ(c.act[1].op, ESS3D_HOLD);
  CHECK_EQ(c.act[2].op, ESS3D_RESET);
  CHECK_EQ(c.act[3].op, ESS3D_SHOW);
  CHECK_EQ(c.act[4].op, ESS3D_OFF);

  // relative levels and percentages of 63
  CHECK_EQ(ess3d_parse("level +5 level -5 level 0 level 63", &c), 0);
  CHECK_EQ(c.act[0].op, ESS3D_ADD);
  CHECK_EQ(c.act[0].arg, 5);
  CHECK_EQ(c.act[1].op, ESS3D_ADD);
  CHECK_EQ(c.act[1].arg, -5);
  CHECK_EQ(c.act[2].op, ESS3D_LEVEL);
  CHECK_EQ(c.act[2].arg, 0);
  CHECK_EQ(c.act[3].arg, 63);
  CHECK_EQ(ess3d_parse("level 50% level 100% level 1% level +10% "
                       "level -100%",
                       &c),
           0);
  CHECK_EQ(c.act[0].op, ESS3D_LEVEL);
  CHECK_EQ(c.act[0].arg, 32);
  CHECK_EQ(c.act[1].arg, 63);
  CHECK_EQ(c.act[2].arg, 1);
  CHECK_EQ(c.act[3].op, ESS3D_ADD);
  CHECK_EQ(c.act[3].arg, 6);
  CHECK_EQ(c.act[4].arg, -63);

  // up and down, with and without a number
  CHECK_EQ(ess3d_parse("up down up 8 down 2 on up 10% up", &c), 0);
  CHECK_EQ(c.nact, 7);
  CHECK_EQ(c.act[0].op, ESS3D_ADD);
  CHECK_EQ(c.act[0].arg, ESS3D_STEP);
  CHECK_EQ(c.act[1].arg, -ESS3D_STEP);
  CHECK_EQ(c.act[2].arg, 8);
  CHECK_EQ(c.act[3].arg, -2);
  CHECK_EQ(c.act[4].op, ESS3D_ON);
  CHECK_EQ(c.act[5].arg, 6);
  CHECK_EQ(c.act[6].arg, ESS3D_STEP);

  // switches anywhere, with / or -
  CHECK_EQ(ess3d_parse("/sim /Q toggle /novxd /base=240 /cfg=800 /t=3000 "
                       "/log=c:\\ess\\3d.log",
                       &c),
           0);
  CHECK(c.sim && c.quiet && c.novxd);
  CHECK_EQ(c.audio_base, 0x240);
  CHECK_EQ(c.config_base, 0x800);
  CHECK_EQ(c.time_ms, 3000);
  CHECK(!strcmp(c.log, "c:\\ess\\3d.log"));
  CHECK_EQ(c.nact, 1);
  CHECK_EQ(ess3d_parse("-q -SIM level -4", &c), 0);
  CHECK(c.quiet && c.sim);
  CHECK_EQ(c.act[0].arg, -4);
  CHECK_EQ(ess3d_parse("/log=\"C:\\My Logs\\3d.log\" on", &c), 0);
  CHECK(!strcmp(c.log, "C:\\My Logs\\3d.log"));

  // bad input
  CHECK_EQ(ess3d_parse("", &c), -1);
  CHECK(!strcmp(c.err, "no command"));
  CHECK_EQ(ess3d_parse("/q /sim", &c), -1);
  CHECK_EQ(ess3d_parse("on bogus", &c), -1);
  CHECK(!strcmp(c.err, "unknown command: bogus"));
  CHECK_EQ(ess3d_parse("level", &c), -1);
  CHECK(strstr(c.err, "0 to 63") != 0);
  CHECK_EQ(ess3d_parse("level 64", &c), -1);
  CHECK_EQ(ess3d_parse("level +64", &c), -1);
  CHECK_EQ(ess3d_parse("level x", &c), -1);
  CHECK_EQ(ess3d_parse("level 101%", &c), -1);
  CHECK_EQ(ess3d_parse("level 4x", &c), -1);
  CHECK_EQ(ess3d_parse("level 4294967336", &c), -1); // 2^32 + 40
  CHECK_EQ(ess3d_parse("up 64", &c), -1);
  CHECK(!strcmp(c.err, "up 64: the level is 0 to 63, or 0% to 100%"));
  CHECK_EQ(ess3d_parse("up -3", &c), -1);
  CHECK(!strcmp(c.err, "unknown command: -3"));
  CHECK_EQ(ess3d_parse("/x on", &c), -1);
  CHECK(!strcmp(c.err, "unknown or bad option: /x"));
  CHECK_EQ(ess3d_parse("/base on", &c), -1);
  CHECK_EQ(ess3d_parse("/base=xyz on", &c), -1);
  CHECK(!strcmp(c.err, "/base= needs a port in hex"));
  CHECK_EQ(ess3d_parse("/cfg=10000 on", &c), -1);
  CHECK_EQ(ess3d_parse("/t=50 on", &c), -1);
  CHECK_EQ(ess3d_parse("/t=abc on", &c), -1);
  CHECK_EQ(ess3d_parse("/log= on", &c), -1);
  CHECK_EQ(ess3d_parse("level", &c), -1);
  CHECK(!strcmp(c.err, "level: the level is 0 to 63, or 0% to 100%"));

  // the first problem is kept, and switches after it still count
  CHECK_EQ(ess3d_parse("bogus level 99 /q /log=a.log", &c), -1);
  CHECK(!strcmp(c.err, "unknown command: bogus"));
  CHECK(c.quiet);
  CHECK(!strcmp(c.log, "a.log"));
  CHECK_EQ(ess3d_parse("level 99 on", &c), -1);
  CHECK(!strcmp(c.err, "level 99: the level is 0 to 63, or 0% to 100%"));
  CHECK_EQ(c.nact, 1); // on, after the bad level
  CHECK_EQ(ess3d_parse("on on on on on on on on on on on on on on on on", &c),
           0);
  CHECK_EQ(c.nact, ESS3D_MAX_ACTIONS);
  CHECK_EQ(
      ess3d_parse("on on on on on on on on on on on on on on on on on", &c),
      -1);
  CHECK(!strcmp(c.err, "too many commands"));
}

static void test_on_off(void) {
  struct ess3d_state s;
  u8 w[8];

  // from the power-on state: released from reset first, as the driver does
  chip(0x00, 0);
  CHECK_EQ(run("on", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);
  CHECK_EQ(writes(0x50, w, 8), 2);
  CHECK_EQ(w[0], 0x04);
  CHECK_EQ(w[1], 0x0C);
  CHECK(s.enable && s.run && s.level == 0);
  CHECK(text_is(&s, "3-D on, level 0 of 63"));

  // nothing written when nothing changes
  CHECK_EQ(run("on", &s), 0);
  CHECK_EQ(writes(0x50, w, 8), 0);
  CHECK_EQ(writes(0x52, w, 8), 0);

  // off bypasses the effect and leaves it running, like the driver's 04h
  CHECK_EQ(run("off", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x04);
  CHECK(text_is(&s, "3-D off, level 0 of 63"));

  // bit 0 (the driver's 3D Limit) stays
  chip(0x05, 9);
  CHECK_EQ(run("on", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0D);
  CHECK_EQ(run("off", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x05);
  CHECK_EQ(simhw.mixer[0x52], 9);
}

static void test_toggle(void) {
  struct ess3d_state s;
  u8 w[8];

  chip(0x04, 20);
  CHECK_EQ(run("toggle", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);
  CHECK(text_is(&s, "3-D on, level 20 of 63"));
  CHECK_EQ(run("toggle", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x04);
  CHECK(text_is(&s, "3-D off, level 20 of 63"));

  // off and held in reset: on and released
  chip(0x00, 0);
  CHECK_EQ(run("toggle", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);

  // on but held in reset is silent, so toggle releases it
  chip(0x08, 0);
  CHECK_EQ(run("toggle", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);
  CHECK_EQ(writes(0x50, w, 8), 1);

  // twice is back where it was, both writes made
  chip(0x04, 0);
  CHECK_EQ(run("toggle toggle", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x04);
  CHECK_EQ(writes(0x50, w, 8), 2);
  CHECK_EQ(w[0], 0x0C);
  CHECK_EQ(w[1], 0x04);
}

static void test_run_bit(void) {
  struct ess3d_state s;
  u8 w[8];

  chip(0x0C, 30);
  CHECK_EQ(run("hold", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x08);
  CHECK(!s.run && s.enable);
  CHECK(text_is(&s, "3-D on, held in reset, level 30 of 63"));
  CHECK_EQ(run("on", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);

  // reset: 0 then 1, keeping on/off, bit 0 and the level
  chip(0x0C, 30);
  CHECK_EQ(run("reset", &s), 0);
  CHECK_EQ(writes(0x50, w, 8), 2);
  CHECK_EQ(w[0], 0x08);
  CHECK_EQ(w[1], 0x0C);
  CHECK_EQ(writes(0x52, w, 8), 0);
  CHECK_EQ(simhw.mixer[0x52], 30);
  CHECK(text_is(&s, "3-D on, level 30 of 63"));
  chip(0x05, 0);
  CHECK_EQ(run("reset", &s), 0);
  CHECK_EQ(writes(0x50, w, 8), 2);
  CHECK_EQ(w[0], 0x01);
  CHECK_EQ(w[1], 0x05);
  CHECK(text_is(&s, "3-D off, level 0 of 63, limit on"));

  // reset releases an effect that was held
  chip(0x08, 12);
  CHECK_EQ(run("reset", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);
  CHECK(s.run);

  // the level comes back if the effect's reset clears it
  chip(0x0C, 30);
  reset_clears_52 = 1;
  CHECK_EQ(run("reset", &s), 0);
  CHECK_EQ(simhw.mixer[0x52], 30);
  CHECK_EQ(writes(0x52, w, 8), 1);
  CHECK(text_is(&s, "3-D on, level 30 of 63"));
}

static void test_levels(void) {
  struct ess3d_state s;
  u8 w[8];

  chip(0x0C, 0);
  CHECK_EQ(run("level 40", &s), 0);
  CHECK_EQ(simhw.mixer[0x52], 40);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);
  CHECK(text_is(&s, "3-D on, level 40 of 63"));
  // relative, clamped at 63 and 0
  CHECK_EQ(run("level +30", &s), 0);
  CHECK_EQ(simhw.mixer[0x52], 63);
  CHECK_EQ(run("up", &s), 0);
  CHECK_EQ(writes(0x52, w, 8), 0);
  CHECK_EQ(run("level -10", &s), 0);
  CHECK_EQ(simhw.mixer[0x52], 53);
  CHECK_EQ(run("down 60", &s), 0);
  CHECK_EQ(simhw.mixer[0x52], 0);
  CHECK_EQ(run("level -5", &s), 0);
  CHECK_EQ(writes(0x52, w, 8), 0);
  CHECK_EQ(run("up", &s), 0);
  CHECK_EQ(simhw.mixer[0x52], ESS3D_STEP);
  CHECK_EQ(run("up 8", &s), 0);
  CHECK_EQ(simhw.mixer[0x52], ESS3D_STEP + 8);
  CHECK_EQ(run("down", &s), 0);
  CHECK_EQ(simhw.mixer[0x52], 8);
  CHECK_EQ(run("level 50%", &s), 0);
  CHECK_EQ(simhw.mixer[0x52], 32);
  CHECK_EQ(run("level 32", &s), 0);
  CHECK_EQ(writes(0x52, w, 8), 0);
  CHECK_EQ(run("level +100%", &s), 0);
  CHECK_EQ(simhw.mixer[0x52], 63);
  CHECK_EQ(simhw.mixer[0x50], 0x0C); // the levels leave on/off alone
}

static void test_sequence(void) {
  struct ess3d_state s;
  u8 w[8];

  chip(0x00, 0);
  CHECK_EQ(run("on level 40 level +30 down 8 toggle", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x04);
  CHECK_EQ(simhw.mixer[0x52], 55);
  CHECK_EQ(writes(0x50, w, 8), 3);
  CHECK_EQ(w[0], 0x04);
  CHECK_EQ(w[1], 0x0C);
  CHECK_EQ(w[2], 0x04);
  CHECK_EQ(writes(0x52, w, 8), 3);
  CHECK_EQ(w[0], 40);
  CHECK_EQ(w[1], 63);
  CHECK_EQ(w[2], 55);
  CHECK(text_is(&s, "3-D off, level 55 of 63"));

  // show changes nothing
  CHECK_EQ(run("show", &s), 0);
  CHECK_EQ(writes(0x50, w, 8) + writes(0x52, w, 8), 0);
  CHECK(text_is(&s, "3-D off, level 55 of 63"));
}

static void test_errors(void) {
  struct ess3d_state s;

  // the chip doesn't keep the level: reported, with what it returned
  chip(0x0C, 0);
  drop_52 = 1;
  CHECK_EQ(run("level 40", &s), ESS3D_MISMATCH);
  CHECK_EQ(s.level, 0);
  CHECK(text_is(&s, "3-D on, level 0 of 63"));

  // a refused access stops before anything is written
  chip(0x04, 0);
  esshw.backend = ESSHW_VXDEXT;
  esshw.ext_call = ext_in_use;
  CHECK_EQ(run("on", &s), -ESSHW_EINUSE);
  esshw.backend = ESSHW_SIM;
  CHECK_EQ(simhw.mixer[0x50], 0x04);
}

static void test_limit_and_regs(void) {
  struct ess3d_state s;
  struct ess3d_cmd c;
  u8 w[8];

  // the Spatializer registers come from the catalog
  CHECK_EQ(ess3d_regs(), 4);
  CHECK_EQ(ess3d_reg_field(0), F_3D_R54);
  CHECK_EQ(ess3d_reg_field(3), F_3D_R5A);
  CHECK_EQ(ess3d_reg_field(4), -1);
  CHECK_EQ(ess3d_reg_default(0), 0x8F);
  CHECK_EQ(ess3d_reg_default(1), 0x95);
  CHECK_EQ(ess3d_reg_default(2), 0x94);
  CHECK_EQ(ess3d_reg_default(3), 0x80);

  // limit: bit 0 of 50h, the other bits kept
  chip(0x0C, 40);
  CHECK_EQ(run("limit on", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0D);
  CHECK(text_is(&s, "3-D on, level 40 of 63, limit on"));
  CHECK_EQ(run("limit toggle", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);
  CHECK_EQ(run("LIMIT Toggle limit off", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);
  // on, off and toggle keep it
  CHECK_EQ(run("limit on off", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x05);
  CHECK_EQ(run("toggle", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0D);

  // mono: bit 1 of 50h, the other bits kept
  CHECK_EQ(run("mono on", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0F);
  CHECK(text_is(&s, "3-D on, level 40 of 63, limit on, mono on"));
  CHECK_EQ(run("MONO toggle limit off", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);
  CHECK(!s.mono && !s.limit);

  // reg: a register and a value in hex, written only if it changes
  CHECK_EQ(run("reg 54 8f reg 5Ah 7Fh", &s), 0);
  CHECK_EQ(simhw.mixer[0x54], 0x8F);
  CHECK_EQ(simhw.mixer[0x5A], 0x7F);
  CHECK_EQ(s.reg[0], 0x8F);
  CHECK_EQ(s.reg[3], 0x7F);
  CHECK_EQ(run("reg 54 8f", &s), 0);
  CHECK_EQ(writes(0x54, w, 8), 0);
  CHECK_EQ(run("reg 54 0", &s), 0);
  CHECK_EQ(writes(0x54, w, 8), 1);
  CHECK_EQ(w[0], 0x00);

  // defaults: what ESS's driver sets when Windows starts
  chip(0x03, 5);
  CHECK_EQ(run("defaults", &s), 0);
  CHECK_EQ(simhw.mixer[0x50], 0x0C);
  CHECK_EQ(simhw.mixer[0x52], 63);
  CHECK_EQ(simhw.mixer[0x54], 0x8F);
  CHECK_EQ(simhw.mixer[0x56], 0x95);
  CHECK_EQ(simhw.mixer[0x58], 0x94);
  CHECK_EQ(simhw.mixer[0x5A], 0x80);
  CHECK(text_is(&s, "3-D on, level 63 of 63"));

  // tray and exit aren't commands for the chip
  CHECK_EQ(ess3d_parse("tray", &c), 0);
  CHECK(c.tray && !c.exit && !c.nact);
  CHECK_EQ(ess3d_parse("/q exit", &c), 0);
  CHECK(c.exit && c.quiet && !c.nact);
  CHECK_EQ(ess3d_parse("on tray", &c), 0);
  CHECK(c.tray && c.nact == 1);

  // bad ones
  CHECK_EQ(ess3d_parse("limit", &c), -1);
  CHECK(!strcmp(c.err, "limit needs on, off or toggle"));
  CHECK_EQ(ess3d_parse("limit maybe", &c), -1);
  CHECK_EQ(ess3d_parse("mono", &c), -1);
  CHECK(!strcmp(c.err, "mono needs on, off or toggle"));
  CHECK_EQ(ess3d_parse("reg 54", &c), -1);
  CHECK(!strcmp(c.err,
                "reg needs a register and a value in hex, like reg 54 8F"));
  CHECK_EQ(ess3d_parse("reg 52 10", &c), -1); // the level has its command
  CHECK(strstr(c.err, "not a Spatializer register") != 0);
  CHECK_EQ(ess3d_parse("reg 54 100", &c), -1);
  CHECK_EQ(ess3d_parse("reg zz 10", &c), -1);
  CHECK(strstr(c.err, "zz") != 0);
}

int main(void) {
  test_parse();
  test_on_off();
  test_toggle();
  test_run_bit();
  test_levels();
  test_sequence();
  test_errors();
  test_limit_and_regs();
  return CHECK_DONE("t_ess3d");
}
