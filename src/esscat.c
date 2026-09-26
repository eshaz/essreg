/*
 * Tables and helpers for the ES1869 register catalog.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esscat.h"

#define REG(id, bank, ldn, addr, rflags, name, dspage)                         \
  {bank, ldn, addr, rflags, dspage, name},
#define FLD(id, reg, shift, width, kind, tier, fflags, en, page, fmt, key,    \
            label, help)
#define ENUM(id)
#define ENUMV(en, value, text)
const struct ess_reg ess_regs[R_COUNT] = {
#include "esscat.tbl"
};
#undef REG
#undef FLD

#define REG(id, bank, ldn, addr, rflags, name, dspage)
#define FLD(id, reg, shift, width, kind, tier, fflags, en, page, fmt, key,    \
            label, help)                                                       \
  {reg, shift, width, kind, tier, fflags, en, page, fmt, key, label, help},
const struct ess_field ess_fields[F_COUNT] = {
#include "esscat.tbl"
};
#undef FLD
#undef ENUMV

#define FLD(id, reg, shift, width, kind, tier, fflags, en, page, fmt, key,    \
            label, help)
#define ENUMV(en, value, text) {en, value, text},
const struct ess_enumv ess_enumvs[] = {
#include "esscat.tbl"
    {E_NONE, 0, 0}};
#undef REG
#undef FLD
#undef ENUM
#undef ENUMV

const unsigned ess_enumv_count =
    sizeof(ess_enumvs) / sizeof(ess_enumvs[0]) - 1;

const char *const ess_page_names[PG_COUNT] = {
    "Output mixer",       "Master volume",
    "Record",             "3-D, mic, MONO, I2S",
    "Serial / telegaming", "Audio 2 channel",
    "Audio 1 controller", "ADC offset & power",
    "Status & interrupts", "Plug and Play",
    "SB compatible mixer"};

const char *const ess_tier_names[4] = {"read-only", "safe", "caution",
                                       "expert"};

const char *const ess_bank_names[BK_COUNT] = {"mixer", "controller",
                                              "audio port", "config port",
                                              "PnP card", "PnP device"};

u8 cat_max(const struct ess_field *f) {
  return (u8)((1u << f->width) - 1);
}

u8 cat_mask(const struct ess_field *f) {
  return (u8)(cat_max(f) << f->shift);
}

u8 cat_get(const struct ess_field *f, u8 raw) {
  return (u8)((raw >> f->shift) & cat_max(f));
}

u8 cat_set(const struct ess_field *f, u8 raw, u8 value) {
  u8 mask = cat_mask(f);
  return (u8)((raw & ~mask) | ((value << f->shift) & mask));
}

// sign in the top bit, and negative codes mean -(magnitude + 1) as in the
// ADC offset registers (DS p.71)
int cat_smag(const struct ess_field *f, u8 value) {
  u8 sign = (u8)(1u << (f->width - 1));
  int mag = value & (sign - 1);
  return (value & sign) ? -(mag + 1) : mag;
}

u8 cat_smag_code(const struct ess_field *f, int step) {
  u8 sign = (u8)(1u << (f->width - 1));
  if (step < 0)
    return (u8)(sign | ((-step - 1) & (sign - 1)));
  return (u8)(step & (sign - 1));
}

const char *cat_enum_text(u8 enum_id, u8 value) {
  unsigned i;
  for (i = 0; i < ess_enumv_count; i++)
    if (ess_enumvs[i].enum_id == enum_id && ess_enumvs[i].value == value)
      return ess_enumvs[i].text;
  return 0;
}

u32 cat_rate_a1(u8 raw) {
  // DS p.67: 397.7 kHz / (128 - x) or 795.5 kHz / (256 - x)
  if (raw & 0x80)
    return 795500UL / (u32)(256 - raw);
  return 397700UL / (u32)(128 - raw);
}

u32 cat_rate_70(u8 raw) {
  // DS p.64: bit 7 picks the 768 kHz (48 kHz family) or 793.8 kHz
  // (44.1 kHz family) master clock, bits 6:0 are the divider
  u32 clock = (raw & 0x80) ? 768000UL : 793800UL;
  return clock / (u32)(128 - (raw & 0x7F));
}

u32 cat_filter(u8 raw) {
  // filter clock divider: 7.16 MHz / (256 - x)
  return 7160000UL / (u32)(256 - raw);
}

int cat_adc_offset(u8 raw) {
  // DS p.71: 64 * bits[3:0], or -64 * (bits[3:0] + 1) when bit 4 is set
  if (raw & 0x10)
    return -64 * ((raw & 0x0F) + 1);
  return 64 * (raw & 0x0F);
}

void cat_format(const struct ess_field *f, u8 raw, char *buf,
                unsigned size) {
  char tmp[64];
  u8 v = cat_get(f, raw);
  const char *text;

  switch (f->fmt) {
  case FMT_RATE_A1:
    sprintf(tmp, "%lu Hz (%02Xh)", (unsigned long)cat_rate_a1(v), v);
    break;
  case FMT_RATE_70:
    sprintf(tmp, "%lu Hz (%02Xh)", (unsigned long)cat_rate_70(v), v);
    break;
  case FMT_FILTER:
    sprintf(tmp, "%lu Hz (%02Xh)", (unsigned long)cat_filter(v), v);
    break;
  case FMT_ADCOFF:
    sprintf(tmp, "%+d (%02Xh)", cat_adc_offset(v), v);
    break;
  default:
    switch (f->kind) {
    case K_BOOL:
      strcpy(tmp, v ? "on" : "off");
      break;
    case K_ENUM:
      text = cat_enum_text(f->enum_id, v);
      if (text)
        sprintf(tmp, "%s", text);
      else
        sprintf(tmp, "%u", v);
      break;
    case K_SMAG:
      sprintf(tmp, "%+d", cat_smag(f, v));
      break;
    case K_HEX:
      sprintf(tmp, "%02Xh", v);
      break;
    default:
      if (f->width == 1)
        strcpy(tmp, v ? "1" : "0");
      else
        sprintf(tmp, "%u / %u", v, cat_max(f));
    }
  }
  strncpy(buf, tmp, size - 1);
  buf[size - 1] = 0;
}

static int nocase_eq(const char *a, const char *b) {
  while (*a && *b) {
    char x = *a++, y = *b++;
    if (x >= 'A' && x <= 'Z')
      x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z')
      y = (char)(y - 'A' + 'a');
    if (x != y)
      return 0;
  }
  return *a == *b;
}

int cat_parse(const struct ess_field *f, const char *text, u8 *value) {
  char *end;
  long n;
  unsigned i, len;

  while (*text == ' ')
    text++;
  if (f->kind == K_ENUM)
    for (i = 0; i < ess_enumv_count; i++)
      if (ess_enumvs[i].enum_id == f->enum_id &&
          nocase_eq(ess_enumvs[i].text, text)) {
        *value = ess_enumvs[i].value;
        return 0;
      }
  if (f->kind == K_BOOL) {
    if (nocase_eq(text, "on") || nocase_eq(text, "true")) {
      *value = 1;
      return 0;
    }
    if (nocase_eq(text, "off") || nocase_eq(text, "false")) {
      *value = 0;
      return 0;
    }
  }
  len = strlen(text);
  while (len && text[len - 1] == ' ')
    len--;
  if (!len)
    return -1;
  if (text[len - 1] == 'h' || text[len - 1] == 'H') {
    n = strtol(text, &end, 16); // "0Ch"
    if (end != text + len - 1)
      return -1;
  } else {
    n = strtol(text, &end, 0); // "12", "0x0c", "-3"
    if (end != text + len)
      return -1;
  }
  if (f->kind == K_SMAG) {
    // signed values from -2^(w-1) to 2^(w-1)-1
    int lim = 1 << (f->width - 1);
    if (n < -lim || n >= lim)
      return -1;
    *value = cat_smag_code(f, (int)n);
    return 0;
  }
  if (n < 0 || n > cat_max(f))
    return -1;
  *value = (u8)n;
  return 0;
}

int cat_find_key(const char *key) {
  int i;
  for (i = 0; i < F_COUNT; i++)
    if (strcmp(ess_fields[i].key, key) == 0)
      return i;
  return -1;
}

int cat_writable(const struct ess_field *f, int expert) {
  if (f->tier == T_RO || f->kind == K_RO)
    return 0;
  if (f->tier == T_EXPERT)
    return expert;
  return 1;
}
