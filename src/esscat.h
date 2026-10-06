/*
 * The ES1869 register and field catalog.
 *
 * Notes:
 *
 * The catalog is written once, in esscat.tbl, as X-macro lines:
 *
 *   REG(id, bank, ldn, addr, rflags, "name", dspage)
 *   FLD(id, reg, shift, width, kind, tier, fflags, enum, page, fmt,
 *       "key", "label", "help")
 *   ENUM(id)
 *   ENUMV(enum, value, "text")
 *
 * esscat.c builds its tables from it and tools/regdoc.py builds the
 * register tables of docs/REGISTERS.md. Every line holds exactly one macro
 * call so both C and Python can read it.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef ESSCAT_H
#define ESSCAT_H

#include "esstypes.h"

// how a register is reached
enum ess_bank {
  BK_MIXER,   // mixer index/data at Audio_Base+4/+5
  BK_CTRL,    // controller register (A0h-BFh) through the DSP channel
  BK_APORT,   // I/O port Audio_Base+addr
  BK_CPORT,   // I/O port Config_Base+addr
  BK_PNPCARD, // PnP card-level register
  BK_PNPLDN,  // PnP register of logical device ldn
  BK_COUNT
};

// optional logical devices, whose number depends on which of them card
// register 25h says the card has: they follow LDN 2 in this order
#define LDN_OPT 0x10
#define LDN_MPU (LDN_OPT + 0)   // MPU-401, 25h bit 0
#define LDN_CDROM (LDN_OPT + 1) // CD-ROM, 25h bit 1
#define LDN_MODEM (LDN_OPT + 2) // modem, 25h bit 2
#define LDN_GP (LDN_OPT + 3)    // general-purpose, 25h bits 4:3

// how a field is shown and edited
enum ess_kind {
  K_BOOL,   // one bit
  K_UINT,   // unsigned level or number
  K_ENUM,   // named values (see ENUM/ENUMV)
  K_RO,     // read-only status
  K_ACTION, // write the field's maximum value to trigger an action
  K_PULSE,  // write 1 then 0
  K_SMAG,   // sign (top bit) and magnitude
  K_HEX     // raw value
};

// who can write it
enum ess_tier {
  T_RO,      // never written
  T_SAFE,    // ordinary settings
  T_CAUTION, // can mute, distort or confuse the driver, but not hang
  T_EXPERT   // can stop playback, hang the DSP or reconfigure the card
};

// register flags
#define RF_READ_SIDEFX 0x01 // reading changes state, only read on request
#define RF_WRITEONLY 0x02   // no meaningful read-back
#define RF_NEEDS_IDLE 0x04  // goes through the DSP command channel
#define RF_ALIAS 0x08       // Sound Blaster compatible view of another reg
#define RF_FM_PORT                                                             \
  0x10 // an FM port: a read through the stock
       // ES1869.VXD makes the reader FM's owner

// field flags
#define FF_PERSIST 0x01  // stored in profiles
#define FF_DRVOWNED 0x02 // the Windows driver rewrites it
#define FF_VOLATILE 0x04 // a DSP software reset reinitializes it

// user interface pages
enum ess_page {
  PG_OUTPUT,  // playback mixer
  PG_MASTER,  // master volume and hardware volume control
  PG_RECORD,  // record source and record mixer
  PG_EFFECTS, // 3-D, microphone, MONO_IN/OUT, I2S
  PG_SERIAL,  // serial / telegaming / ES689 interface
  PG_DAC,     // both audio channels: the two DACs, and the ADC
  PG_POWER,   // power management, GPO
  PG_STATUS,  // status and interrupt registers
  PG_PNP,     // Plug and Play configuration
  PG_LEGACY,  // Sound Blaster compatible mixer registers
  PG_COUNT
};

// value formatting
enum ess_fmt {
  FMT_NONE,
  FMT_RATE_A1, // Audio 1 sample rate from register A1h
  FMT_RATE_70, // Audio 2 sample rate from mixer 70h
  FMT_FILTER,  // filter clock from A2h / 72h
  FMT_ADCOFF,  // ADC offset code (sign-magnitude, 64 per step)
  FMT_PORTHI,  // I/O base bits 11:8
  FMT_PORTLO   // I/O base bits 7:0
};

// register, field and enum ids
#define REG(id, bank, ldn, addr, rflags, name, dspage) id,
#define FLD(id, reg, shift, width, kind, tier, fflags, en, page, fmt, key,     \
            label, help)
#define ENUM(id)
#define ENUMV(en, value, text)
enum ess_reg_id {
#include "esscat.tbl"
  R_COUNT
};
#undef REG
#undef FLD

#define REG(id, bank, ldn, addr, rflags, name, dspage)
#define FLD(id, reg, shift, width, kind, tier, fflags, en, page, fmt, key,     \
            label, help)                                                       \
  id,
enum ess_field_id {
#include "esscat.tbl"
  F_COUNT
};
#undef FLD
#undef ENUM

#define FLD(id, reg, shift, width, kind, tier, fflags, en, page, fmt, key,     \
            label, help)
#define ENUM(id) id,
enum ess_enum_id {
  E_NONE,
#include "esscat.tbl"
  E_COUNT
};
#undef REG
#undef FLD
#undef ENUM
#undef ENUMV

struct ess_reg {
  u8 bank;
  u8 ldn;
  u16 addr;
  u8 flags;
  u8 dspage;
  const char *name;
};

struct ess_field {
  u8 reg;
  u8 shift;
  u8 width;
  u8 kind;
  u8 tier;
  u8 flags;
  u8 enum_id;
  u8 page;
  u8 fmt;
  const char *key;
  const char *label;
  const char *help;
};

struct ess_enumv {
  u8 enum_id;
  u8 value;
  const char *text;
};

extern const struct ess_reg ess_regs[R_COUNT];
extern const struct ess_field ess_fields[F_COUNT];
extern const struct ess_enumv ess_enumvs[];
extern const unsigned ess_enumv_count;
extern const char *const ess_page_names[PG_COUNT];
extern const char *const ess_tier_names[4];
extern const char *const ess_bank_names[BK_COUNT];

// field value to register byte and back
u8 cat_mask(const struct ess_field *f);
u8 cat_get(const struct ess_field *f, u8 raw);
u8 cat_set(const struct ess_field *f, u8 raw, u8 value);
u8 cat_max(const struct ess_field *f);

// signed value of a K_SMAG field, and the field value of a signed value
int cat_smag(const struct ess_field *f, u8 value);
u8 cat_smag_code(const struct ess_field *f, int step);

// text of an enum value, or NULL
const char *cat_enum_text(u8 enum_id, u8 value);

// format a field value (of register byte raw) for display
void cat_format(const struct ess_field *f, u8 raw, char *buf, unsigned size);

// parse "12", "0x0c", "-128" (K_SMAG) or an enum text, 0 on success
int cat_parse(const struct ess_field *f, const char *text, u8 *value);

// field id by its profile key, or -1
int cat_find_key(const char *key);

// 1 if the field can be written (expert: Expert mode is on)
int cat_writable(const struct ess_field *f, int expert);

// derived values used by the formats
u32 cat_rate_a1(u8 raw);
u32 cat_rate_70(u8 raw);
u32 cat_filter(u8 raw);
int cat_adc_offset(u8 raw);

// mixer 71h bit 5: A1h works like 70h (DS p.64); cat_rate_a1 and
// FMT_RATE_A1 follow it, so programs set it after reading 71h
extern int cat_a1_like_70;

// the logical device number of an optional device (LDN_MPU to LDN_GP) on a
// card whose register 25h is hdr, -1 if the card doesn't have it; other
// numbers come back as they are
int cat_opt_ldn(int ldn, u8 hdr);

#endif /* ESSCAT_H */
