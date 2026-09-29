/*
 * t_esscat checks the register catalog (src/esscat.tbl) for consistency,
 * then tests its helpers and the essio field writes against the simulated
 * ES1869.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <string.h>

#include "check.h"
#include "esscat.h"
#include "esshw.h"
#include "essio.h"
#include "simhw.h"

static void test_tables(void) {
  int i, j, n;
  unsigned k;
  u8 used[R_COUNT][8];
  int page_fields[PG_COUNT];

  memset(used, 0, sizeof(used));
  memset(page_fields, 0, sizeof(page_fields));
  for (i = 0; i < R_COUNT; i++) {
    const struct ess_reg *r = &ess_regs[i];
    CHECK(r->bank < BK_COUNT);
    CHECK(r->name && r->name[0]);
    CHECK(r->addr < 0x100);
    if (r->bank == BK_CTRL)
      CHECK(r->addr >= 0xA0 && r->addr <= 0xBF && (r->flags & RF_NEEDS_IDLE));
    if (r->bank == BK_APORT)
      CHECK(r->addr <= 0x0F);
    if (r->bank == BK_CPORT)
      CHECK(r->addr <= 0x07);
    // no register appears twice
    for (j = 0; j < i; j++)
      CHECK(!(ess_regs[j].bank == r->bank && ess_regs[j].addr == r->addr &&
              ess_regs[j].ldn == r->ldn));
  }
  for (i = 0; i < F_COUNT; i++) {
    const struct ess_field *f = &ess_fields[i];
    CHECK(f->reg < R_COUNT);
    CHECK(f->width >= 1 && f->shift + f->width <= 8);
    CHECK(f->page < PG_COUNT);
    CHECK(f->tier <= T_EXPERT);
    CHECK(f->key && f->key[0] && f->label && f->label[0] && f->help &&
          f->help[0]);
    CHECK(strlen(f->label) <= 32);
    page_fields[f->page]++;
    // fields of a register do not overlap
    for (j = f->shift; j < f->shift + f->width; j++) {
      CHECK(!used[f->reg][j]);
      used[f->reg][j] = 1;
    }
    // keys are unique
    for (j = 0; j < i; j++)
      CHECK(strcmp(ess_fields[j].key, f->key) != 0);
    CHECK_EQ(cat_find_key(f->key), i);
    // enum fields name an enum with values, other fields none
    if (f->kind == K_ENUM) {
      CHECK(f->enum_id != E_NONE && f->enum_id < E_COUNT);
      for (n = 0, k = 0; k < ess_enumv_count; k++)
        if (ess_enumvs[k].enum_id == f->enum_id) {
          n++;
          CHECK(ess_enumvs[k].value <= cat_max(f));
        }
      CHECK(n >= 2);
    } else {
      CHECK_EQ(f->enum_id, E_NONE);
    }
    // live status is never written, and only settings are persisted
    if (f->kind == K_RO)
      CHECK_EQ(f->tier, T_RO);
    if (f->flags & FF_PERSIST)
      CHECK((f->tier == T_SAFE || f->tier == T_CAUTION) && f->kind != K_RO &&
            f->kind != K_ACTION && f->kind != K_PULSE);
    // actions that reset or reconfigure something need Expert mode, unless
    // they only calibrate
    if (f->kind == K_PULSE)
      CHECK_EQ(f->tier, T_EXPERT);
    // registers reached through the DSP are never read by a timer
    if (ess_regs[f->reg].bank == BK_CTRL)
      CHECK(ess_regs[f->reg].flags & RF_NEEDS_IDLE);
    // PnP resources are never changed below Expert mode
    if (ess_regs[f->reg].bank == BK_PNPCARD ||
        ess_regs[f->reg].bank == BK_PNPLDN)
      CHECK(f->tier == T_RO || f->tier == T_EXPERT);
  }
  for (i = 0; i < R_COUNT; i++) {
    for (n = 0, j = 0; j < F_COUNT; j++)
      n += ess_fields[j].reg == i;
    CHECK(n > 0);
  }
  for (i = 0; i < PG_COUNT; i++)
    CHECK(page_fields[i] > 0 && page_fields[i] <= 96);
  for (k = 0; k < ess_enumv_count; k++)
    CHECK(ess_enumvs[k].text && ess_enumvs[k].text[0]);
}

static void test_get_set(void) {
  int i;
  unsigned v, raw;

  for (i = 0; i < F_COUNT; i++) {
    const struct ess_field *f = &ess_fields[i];
    for (v = 0; v <= cat_max(f); v++)
      for (raw = 0; raw < 256; raw += 0x55) {
        u8 r2 = cat_set(f, (u8)raw, (u8)v);
        CHECK_EQ(cat_get(f, r2), v);
        CHECK_EQ(r2 & ~cat_mask(f) & 0xFF, raw & ~cat_mask(f) & 0xFF);
      }
  }
}

static const struct ess_field *field(const char *key) {
  int i = cat_find_key(key);
  CHECK(i >= 0);
  return i >= 0 ? &ess_fields[i] : &ess_fields[0];
}

static void test_formulas(void) {
  const struct ess_field *off = field("adc.off_l");
  char buf[48];
  int s;

  CHECK_EQ(cat_rate_70(0xF0), 48000);
  CHECK_EQ(cat_rate_70(0x6E), 44100);
  CHECK_EQ(cat_rate_70(0xE0), 24000); // 768000 / 32
  CHECK_EQ(cat_rate_a1(0x6E), 22094); // 397700 / 18
  CHECK_EQ(cat_rate_a1(0xEE), 44194); // 795500 / 18
  // with mixer 71h bit 5 set, A1h works like 70h (DS p.64)
  cat_a1_like_70 = 1;
  CHECK_EQ(cat_rate_a1(0xF0), 48000);
  CHECK_EQ(cat_rate_a1(0x6E), 44100);
  cat_a1_like_70 = 0;
  CHECK_EQ(cat_rate_a1(0xF0), 49718); // 795500 / 16, the old formula
  CHECK_EQ(cat_filter(0xFF), 7160000);
  CHECK_EQ(cat_adc_offset(0x00), 0);
  CHECK_EQ(cat_adc_offset(0x0F), 960);
  CHECK_EQ(cat_adc_offset(0x10), -64);
  CHECK_EQ(cat_adc_offset(0x1F), -1024);
  // K_SMAG: 5 bits, -16..15
  CHECK_EQ(off->width, 5);
  for (s = -16; s <= 15; s++)
    CHECK_EQ(cat_smag(off, cat_smag_code(off, s)), s);
  CHECK_EQ(cat_smag_code(off, -1), 0x10);
  CHECK_EQ(cat_smag_code(off, -16), 0x1F);

  cat_format(field("a2.rate"), 0xF0, buf, sizeof(buf));
  CHECK(strcmp(buf, "48000 Hz (F0h)") == 0);
  cat_format(off, 0x1F, buf, sizeof(buf));
  CHECK(strcmp(buf, "-1024 (1Fh)") == 0);
  cat_format(field("fx.3d.enable"), 0x08, buf, sizeof(buf));
  CHECK(strcmp(buf, "on") == 0);
  cat_format(field("fx.3d.level"), 0x2A, buf, sizeof(buf));
  CHECK(strcmp(buf, "42 / 63") == 0);
  cat_format(field("rec.source"), 0x06, buf, sizeof(buf));
  CHECK(strcmp(buf, "Line") == 0);
}

static void test_parse(void) {
  const struct ess_field *lvl = field("fx.3d.level");
  const struct ess_field *on = field("fx.3d.enable");
  const struct ess_field *src = field("rec.source");
  const struct ess_field *off = field("adc.off_r");
  u8 v = 0;

  CHECK_EQ(cat_parse(lvl, "12", &v), 0);
  CHECK_EQ(v, 12);
  CHECK_EQ(cat_parse(lvl, "0x0c", &v), 0);
  CHECK_EQ(v, 12);
  CHECK_EQ(cat_parse(lvl, "0Ch", &v), 0);
  CHECK_EQ(v, 12);
  CHECK_EQ(cat_parse(lvl, " 63 ", &v), 0);
  CHECK_EQ(v, 63);
  CHECK(cat_parse(lvl, "64", &v) != 0);
  CHECK(cat_parse(lvl, "-1", &v) != 0);
  CHECK(cat_parse(lvl, "", &v) != 0);
  CHECK(cat_parse(lvl, "abc", &v) != 0);
  CHECK(cat_parse(lvl, "12x", &v) != 0);
  CHECK_EQ(cat_parse(on, "ON", &v), 0);
  CHECK_EQ(v, 1);
  CHECK_EQ(cat_parse(on, "off", &v), 0);
  CHECK_EQ(v, 0);
  CHECK_EQ(cat_parse(src, "line", &v), 0);
  CHECK_EQ(v, 6);
  CHECK_EQ(cat_parse(src, "5", &v), 0);
  CHECK_EQ(v, 5);
  CHECK(cat_parse(src, "Lin", &v) != 0);
  CHECK_EQ(cat_parse(off, "-3", &v), 0);
  CHECK_EQ(v, 0x12);
  CHECK_EQ(cat_parse(off, "15", &v), 0);
  CHECK_EQ(v, 0x0F);
  CHECK(cat_parse(off, "-17", &v) != 0);
  CHECK(cat_parse(off, "16", &v) != 0);
}

static void test_tiers(void) {
  CHECK(cat_writable(field("fx.3d.level"), 0));
  CHECK(cat_writable(field("ser.enable"), 0)); // caution
  CHECK(!cat_writable(field("ctl.sw_reset"), 0));
  CHECK(cat_writable(field("ctl.sw_reset"), 1));
  CHECK(!cat_writable(field("stat.dsp_busy"), 1));
  CHECK(!cat_writable(field("stat.chipid"), 1));
}

static int count_port(u16 port, u8 dir) {
  int i, n = 0;
  for (i = 0; i < simhw.nlog; i++)
    n += simhw.log[i].port == port && simhw.log[i].dir == dir;
  return n;
}

static void test_essio(void) {
  int i;

  simhw_reset(0x220, 0x800);
  simhw_attach();
  esshw.flags = ESSHW_SAFE;
  esshw.dsp_timeout = 0x40;

  // read-modify-write keeps the other bits
  simhw.mixer[0x7D] = 0x09;
  CHECK_EQ(ess_field_write(cat_find_key("fx.monoout.source"), 2, 0), 0);
  CHECK_EQ(simhw.mixer[0x7D], 0x0D);

  // refused below the tier: no port access at all
  simhw.nlog = 0;
  CHECK_EQ(ess_field_write(cat_find_key("ctl.fifo_reset"), 1, 0), -ESSIO_ETIER);
  CHECK_EQ(simhw.nlog, 0);

  // Audio_Base+6 pulses: never read first, written 02h then 00h, the
  // delay between them reads +6h (the only port that doesn't wake a
  // partial power-down with +7h, DS p.76)
  simhw.nlog = 0;
  CHECK_EQ(ess_field_write(cat_find_key("ctl.fifo_reset"), 1, 1), 0);
  CHECK(simhw.nlog > 0 && simhw.log[0].port == 0x226 &&
        simhw.log[0].dir == 'o');
  CHECK_EQ(count_port(0x22C, 'i'), 0);
  {
    u8 seq[4];
    int n = 0;
    for (i = 0; i < simhw.nlog; i++)
      if (simhw.log[i].port == 0x226 && simhw.log[i].dir == 'o' && n < 4)
        seq[n++] = simhw.log[i].value;
    CHECK_EQ(n, 2);
    CHECK_EQ(seq[0], 0x02);
    CHECK_EQ(seq[1], 0x00);
  }

  // the stock ES1869.VXD's FM ports: not touched, a read would take FM
  // from DOS programs
  simhw.nlog = 0;
  esshw.flags |= ESSHW_F_FM_TRAPPED;
  CHECK_EQ(ess_read(R_AP_0), -ESSIO_ETRAPPED);
  CHECK_EQ(ess_write(R_AP_0, 0), -ESSIO_ETRAPPED);
  CHECK_EQ(simhw.nlog, 0);
  esshw.flags &= (u8)~ESSHW_F_FM_TRAPPED;
  CHECK(ess_read(R_AP_0) >= 0);

  // power management pulse keeps GPO and Analog_Stays_On
  simhw.port7 = 0x0B;
  CHECK_EQ(ess_field_write(cat_find_key("pwr.pdn_request"), 1, 1), 0);
  CHECK_EQ(simhw.port7 & 0x0B, 0x0B);
  CHECK_EQ(simhw.port7 & 0x04, 0);

  // controller fields go through the DSP channel
  simhw.ext_mode = 1;
  simhw.ctrl[0xBA - 0xA0] = 0x20;
  CHECK_EQ(ess_field_write(cat_find_key("adc.off_l"), 0x12, 0), 0);
  CHECK_EQ(simhw.ctrl[0xBA - 0xA0], 0x32);
  CHECK_EQ(simhw.irq_clears, 0);

  // a DSP reset reads the AAh the DSP answers with, so the next controller
  // read gets its own data; BAh survives the reset (DS p.43, p.70)
  CHECK_EQ(ess_field_write(cat_find_key("ctl.sw_reset"), 1, 1), 0);
  CHECK_EQ(simhw.nrdata, 0);
  CHECK_EQ(ess_read(R_CT_BA), 0x32);

  // optional logical devices follow LDN 2 in 25h order (DS p.30-32)
  CHECK_EQ(cat_opt_ldn(1, 0x80), 1);
  CHECK_EQ(cat_opt_ldn(LDN_MPU, 0x80), -1);
  CHECK_EQ(cat_opt_ldn(LDN_GP, 0x80), -1);
  CHECK_EQ(cat_opt_ldn(LDN_MPU, 0x1F), 3);
  CHECK_EQ(cat_opt_ldn(LDN_CDROM, 0x1F), 4);
  CHECK_EQ(cat_opt_ldn(LDN_MODEM, 0x1F), 5);
  CHECK_EQ(cat_opt_ldn(LDN_GP, 0x1F), 6);
  CHECK_EQ(cat_opt_ldn(LDN_CDROM, 0x02), 3);
  CHECK_EQ(cat_opt_ldn(LDN_MODEM, 0x0C), 3);
  CHECK_EQ(cat_opt_ldn(LDN_GP, 0x0C), 4);
  // a CD-ROM without a separate MPU-401 is LDN 3
  simhw.pnp_card[0x25] = 0x02;
  simhw.pnp_ldn[3][0x60] = 0x01;
  CHECK_EQ(ess_read(R_PCD_60), 0x01);
  CHECK_EQ(ess_read(R_PMPU_30), -ESSIO_EABSENT);
  CHECK_EQ(ess_write(R_PMOD_30, 1), -ESSIO_EABSENT);
  CHECK(strcmp(ess_strerror(-ESSIO_EABSENT), "not on this card") == 0);
}

int main(void) {
  test_tables();
  test_get_set();
  test_formulas();
  test_parse();
  test_tiers();
  test_essio();
  return CHECK_DONE("t_esscat");
}
