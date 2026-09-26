/* t_profile.c -- save a profile from the simulated ES1869, reset the chip,
 * load it back and compare; unknown, refused and invalid entries */

#include <stdio.h>
#include <string.h>

#include "check.h"
#include "esscat.h"
#include "esshw.h"
#include "essio.h"
#include "profile.h"
#include "simhw.h"

/* --- a tiny in-memory INI ------------------------------------------------ */

#define MAX_ENTRIES 128

static struct {
  char section[16], key[40], value[40];
} ini[MAX_ENTRIES];
static int nini;

static int find(const char *section, const char *key) {
  int i;
  for (i = 0; i < nini; i++)
    if (!strcmp(ini[i].section, section) && !strcmp(ini[i].key, key))
      return i;
  return -1;
}

static int mem_get(void *ctx, const char *section, const char *key,
                   char *buf, unsigned size) {
  int i = find(section, key);
  (void)ctx;
  if (i < 0)
    return 0;
  strncpy(buf, ini[i].value, size - 1);
  buf[size - 1] = 0;
  return (int)strlen(buf);
}

static int mem_put(void *ctx, const char *section, const char *key,
                   const char *value) {
  int i = find(section, key);
  (void)ctx;
  if (i < 0) {
    if (nini == MAX_ENTRIES)
      return -1;
    i = nini++;
    strcpy(ini[i].section, section);
    strcpy(ini[i].key, key);
  }
  strcpy(ini[i].value, value);
  return 0;
}

static int mem_keys(void *ctx, const char *section, char *buf,
                    unsigned size) {
  unsigned n = 0;
  int i;
  (void)ctx;
  for (i = 0; i < nini; i++)
    if (!strcmp(ini[i].section, section) &&
        n + strlen(ini[i].key) + 2 < size) {
      strcpy(buf + n, ini[i].key);
      n += (unsigned)strlen(ini[i].key) + 1;
    }
  buf[n] = 0;
  return (int)n;
}

static const struct prof_io io = {mem_get, mem_put, mem_keys, 0};

static const char *value_of(const char *key) {
  int i = find(PROF_FIELDS, key);
  return i < 0 ? "" : ini[i].value;
}

static void chip(void) {
  simhw_reset(0x220, 0x800);
  simhw_attach();
  esshw.flags = ESSHW_SAFE;
  esshw.dsp_timeout = 0x40;
  simhw.ext_mode = 1;
}

static int persistable_count(void) {
  int i, n = 0;
  for (i = 0; i < F_COUNT; i++)
    n += (ess_fields[i].flags & FF_PERSIST) != 0;
  return n;
}

static void test_round_trip(void) {
  struct prof_report rep;
  u8 mixer[256], ctrl[32], port7;
  int n = persistable_count();

  chip();
  simhw.mixer[0x50] = 0x0C;       /* 3-D on */
  simhw.mixer[0x52] = 0x2A;       /* 3-D level 42 */
  simhw.mixer[0x1C] = 0x06;       /* record source: line */
  simhw.mixer[0x7D] = 0x0D;       /* MONO_OUT source 2, preamp on, MONO_IN */
  simhw.mixer[0x60] = 0x2F;
  simhw.mixer[0x62] = 0x6F;       /* right master muted */
  simhw.ctrl[0xBA - 0xA0] = 0x33; /* no wake delay, offset -256 */
  simhw.ctrl[0xBB - 0xA0] = 0x05; /* +320 */
  simhw.port7 = 0x09;
  memcpy(mixer, simhw.mixer, sizeof(mixer));
  memcpy(ctrl, simhw.ctrl, sizeof(ctrl));
  port7 = simhw.port7;

  nini = 0;
  CHECK_EQ(prof_save(&io, &rep), 0);
  CHECK_EQ(rep.saved, n);
  CHECK_EQ(rep.failed, 0);
  CHECK(n >= 40);
  CHECK(!strcmp(value_of("fx.3d.enable"), "on"));
  CHECK(!strcmp(value_of("fx.3d.level"), "42"));
  CHECK(!strcmp(value_of("rec.source"), "Line"));
  CHECK(!strcmp(value_of("master.mute.r"), "on"));
  CHECK(!strcmp(value_of("adc.off_l"), "-4"));
  CHECK(!strcmp(value_of("adc.off_r"), "5"));
  CHECK(!strcmp(value_of("pwr.analog_on"), "on"));
  /* never saved: expert settings, status, actions */
  CHECK_EQ(find(PROF_FIELDS, "a2.rate"), -1);
  CHECK_EQ(find(PROF_FIELDS, "stat.dsp_busy"), -1);
  CHECK_EQ(find(PROF_FIELDS, "mix.reset"), -1);

  chip(); /* power cycle: registers back to their defaults */
  CHECK(simhw.mixer[0x52] != 0x2A);
  CHECK_EQ(prof_load(&io, &rep), 0);
  CHECK_EQ(rep.applied, n);
  CHECK_EQ(rep.unknown + rep.refused + rep.invalid, 0);
  CHECK_EQ(rep.failed + rep.mismatched, 0);
  CHECK_EQ(simhw.mixer[0x50] & 0x08, 0x08);
  CHECK_EQ(simhw.mixer[0x52], 0x2A);
  CHECK_EQ(simhw.mixer[0x1C] & 0x17, 0x06);
  CHECK_EQ(simhw.mixer[0x7D], 0x0D);
  CHECK_EQ(simhw.mixer[0x60], mixer[0x60]);
  CHECK_EQ(simhw.mixer[0x62], mixer[0x62]);
  CHECK_EQ(simhw.ctrl[0xBA - 0xA0] & 0x3F, ctrl[0xBA - 0xA0] & 0x3F);
  CHECK_EQ(simhw.ctrl[0xBB - 0xA0] & 0x1F, 0x05);
  CHECK_EQ(simhw.port7 & 0x0B, port7 & 0x0B);
  CHECK_EQ(simhw.irq_clears, 0);
}

static void test_bad_entries(void) {
  struct prof_report rep;

  chip();
  nini = 0;
  mem_put(0, PROF_FIELDS, "fx.3d.level", "20");
  mem_put(0, PROF_FIELDS, "no.such.setting", "1");
  mem_put(0, PROF_FIELDS, "a2.rate", "F0h");       /* expert */
  mem_put(0, PROF_FIELDS, "stat.dsp_busy", "0");   /* status */
  mem_put(0, PROF_FIELDS, "fx.mic.boost", "maybe"); /* bad value */
  mem_put(0, PROF_FIELDS, "adc.off_l", "-17");     /* out of range */
  CHECK(prof_load(&io, &rep) != 0 || rep.applied == 1);
  CHECK_EQ(rep.applied, 1);
  CHECK_EQ(rep.unknown, 1);
  CHECK_EQ(rep.refused, 2);
  CHECK_EQ(rep.invalid, 2);
  CHECK_EQ(simhw.mixer[0x52] & 0x3F, 20);
  CHECK(rep.problem[0] != 0);
  CHECK_EQ(simhw.mixer[0x70], 0); /* the expert key was not applied */
}

static void test_dsp_busy(void) {
  struct prof_report rep;

  chip();
  nini = 0;
  mem_put(0, PROF_FIELDS, "adc.off_r", "3");
  mem_put(0, PROF_FIELDS, "fx.3d.level", "7");
  simhw.busy_stuck = 1; /* the DSP never becomes ready */
  CHECK(prof_load(&io, &rep) != 0);
  CHECK_EQ(rep.applied, 1); /* the mixer setting still went through */
  CHECK_EQ(rep.failed, 1);
  CHECK_EQ(simhw.ctrl[0xBB - 0xA0] & 0x1F, 0);
  CHECK_EQ(esshw.dsp_desync, 0); /* refused before the first byte */
}

int main(void) {
  test_round_trip();
  test_bad_entries();
  test_dsp_busy();
  return CHECK_DONE("t_profile");
}
