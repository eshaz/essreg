/*
 * Saves and restores ES1869 settings, see profile.h.
 *
 * (c) 2026 Ethan
 * Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <string.h>

#include "esscat.h"
#include "esshw.h"
#include "essio.h"
#include "profile.h"

#define DSP_RETRIES 50

static u8 want_value[F_COUNT]; // prof_parse
static u8 want[F_COUNT];
static u8 read_value[F_COUNT]; // prof_read
static u8 read_ok[F_COUNT];
static char keybuf[4096];

static int persistable(const struct ess_field *f) {
  return (f->flags & FF_PERSIST) &&
         (f->tier == T_SAFE || f->tier == T_CAUTION) && f->kind != K_RO &&
         f->kind != K_ACTION && f->kind != K_PULSE;
}

static void note(struct prof_report *rep, const char *what, const char *key) {
  if (!rep->problem[0]) {
    strncpy(rep->problem, what, sizeof(rep->problem) - 1);
    strncat(rep->problem, key, sizeof(rep->problem) - 1 - strlen(rep->problem));
  }
}

void prof_value_text(int field, u8 value, char *buf, unsigned size) {
  const struct ess_field *f = &ess_fields[field];
  const char *text = 0;
  char tmp[24];

  if (f->kind == K_BOOL)
    text = value ? "on" : "off";
  else if (f->kind == K_ENUM)
    text = cat_enum_text(f->enum_id, value);
  if (!text) {
    if (f->kind == K_SMAG)
      sprintf(tmp, "%d", cat_smag(f, value));
    else
      sprintf(tmp, "%u", value);
    text = tmp;
  }
  strncpy(buf, text, size - 1);
  buf[size - 1] = 0;
}

int prof_read(struct prof_report *rep) {
  int i, tries, err;
  u8 v;

  memset(rep, 0, sizeof(*rep));
  for (i = 0; i < F_COUNT; i++) {
    read_ok[i] = 0;
    if (!persistable(&ess_fields[i]))
      continue;
    // a DSP register is busy while a sound plays: try it again
    for (tries = 0;; tries++) {
      err = ess_field_read(i, &v, 0);
      if (err != -ESSHW_EBUSY || tries >= DSP_RETRIES)
        break;
    }
    if (err < 0) {
      rep->failed++;
      note(rep, "could not read ", ess_fields[i].key);
      continue;
    }
    read_value[i] = v;
    read_ok[i] = 1;
    rep->read++;
  }
  return rep->failed ? -1 : 0;
}

int prof_write(const struct prof_io *io, const struct prof_io *old,
               struct prof_report *rep) {
  char text[40];
  int i, err = 0;

  if (io->put(io->ctx, PROF_HEADER, "Format", "1") < 0 ||
      io->put(io->ctx, PROF_HEADER, "Chip", "ES1869") < 0)
    err = -1;
  for (i = 0; i < F_COUNT; i++) {
    if (!persistable(&ess_fields[i]))
      continue;
    if (read_ok[i])
      prof_value_text(i, read_value[i], text, sizeof(text));
    else if (!old || old->get(old->ctx, PROF_FIELDS, ess_fields[i].key, text,
                              sizeof(text)) <= 0)
      continue;
    if (io->put(io->ctx, PROF_FIELDS, ess_fields[i].key, text) < 0) {
      err = -1;
      note(rep, "could not store ", ess_fields[i].key);
      continue;
    }
    if (read_ok[i])
      rep->saved++;
    else
      rep->kept++;
  }
  return err;
}

int prof_save(const struct prof_io *io, struct prof_report *rep) {
  prof_read(rep);
  if (prof_write(io, 0, rep) < 0)
    rep->failed++;
  return rep->failed ? -1 : 0;
}

// write all wanted fields of one register with one read-modify-write
static void apply_register(int reg, struct prof_report *rep) {
  int raw, i, tries;
  u8 value;

  for (tries = 0;; tries++) {
    raw = ess_read(reg);
    if (raw != -ESSHW_EBUSY || tries >= DSP_RETRIES)
      break;
  }
  if (raw < 0) {
    rep->failed++;
    note(rep, "could not read register ", ess_regs[reg].name);
    return;
  }
  value = (u8)raw;
  for (i = 0; i < F_COUNT; i++)
    if (want[i] && ess_fields[i].reg == reg)
      value = cat_set(&ess_fields[i], value, want_value[i]);
  for (tries = 0;; tries++) {
    raw = ess_write(reg, value);
    if (raw != -ESSHW_EBUSY || tries >= DSP_RETRIES)
      break;
  }
  if (raw < 0) {
    rep->failed++;
    note(rep, "could not write register ", ess_regs[reg].name);
    return;
  }
  raw = ess_read(reg);
  for (i = 0; i < F_COUNT; i++) {
    if (!want[i] || ess_fields[i].reg != reg)
      continue;
    if (raw < 0 || cat_get(&ess_fields[i], (u8)raw) != want_value[i]) {
      rep->mismatched++;
      note(rep, "did not stick: ", ess_fields[i].key);
    } else {
      rep->applied++;
    }
  }
}

int prof_parse(const struct prof_io *io, struct prof_report *rep) {
  char value[40];
  const char *key;
  int len;
  u8 v;

  memset(rep, 0, sizeof(*rep));
  memset(want, 0, sizeof(want));
  len = io->keys(io->ctx, PROF_FIELDS, keybuf, sizeof(keybuf));
  // a full buffer may have cut keys off
  if (len >= (int)sizeof(keybuf) - 2) {
    rep->invalid++;
    note(rep, "too many settings in ", PROF_FIELDS);
  }
  for (key = keybuf; len > 0 && *key; key += strlen(key) + 1) {
    int f = cat_find_key(key);
    if (f < 0) {
      rep->unknown++;
      note(rep, "unknown setting ", key);
      continue;
    }
    if (!persistable(&ess_fields[f])) {
      rep->refused++;
      note(rep, "not restorable from a profile: ", key);
      continue;
    }
    if (io->get(io->ctx, PROF_FIELDS, key, value, sizeof(value)) <= 0 ||
        cat_parse(&ess_fields[f], value, &v) != 0) {
      rep->invalid++;
      note(rep, "invalid value for ", key);
      continue;
    }
    want[f] = 1;
    want_value[f] = v;
  }
  return rep->unknown || rep->refused || rep->invalid ? -1 : 0;
}

int prof_apply(struct prof_report *rep) {
  int i, pass;

  // go through the registers in catalog order, plain ones before DSP ones
  for (pass = 0; pass < 2; pass++)
    for (i = 0; i < R_COUNT; i++) {
      int j, needed = 0;
      int dsp = (ess_regs[i].flags & RF_NEEDS_IDLE) != 0;
      if (dsp != pass)
        continue;
      for (j = 0; j < F_COUNT && !needed; j++)
        needed = want[j] && ess_fields[j].reg == i;
      if (needed)
        apply_register(i, rep);
    }
  return (rep->failed || rep->mismatched) ? -1 : 0;
}

int prof_load(const struct prof_io *io, struct prof_report *rep) {
  prof_parse(io, rep);
  return prof_apply(rep);
}
