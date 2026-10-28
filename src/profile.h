/*
 * Saves and restores ES1869 settings as INI text profiles.
 *
 * Example:
 *
 * [ESSCTL]
 *   Format=1
 *   Chip=ES1869
 *   [Fields]
 *   fx.3d.enable=on
 *
 * fx.3d.level=40
 *   rec.source=Line
 *
 * Notes:
 *
 * Only fields marked
 * FF_PERSIST with tier SAFE or CAUTION are written or
 * accepted. Anything
 * else in a file is reported and ignored.
 *
 * Reading the chip and the file
 * are separate steps, so a program can do
 * the file work outside its hardware
 * bracket: prof_read, then prof_write;
 * prof_parse, then prof_apply.
 * prof_save and prof_load do both.
 *
 * (c) 2026 Ethan Halsall
 * <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef PROFILE_H
#define PROFILE_H

#include "esstypes.h"

#define PROF_HEADER "ESSCTL"
#define PROF_FIELDS "Fields"

struct prof_io {
  // copy a value into buf, return its length or 0 if missing
  int (*get)(void *ctx, const char *section, const char *key, char *buf,
             unsigned size);
  int (*put)(void *ctx, const char *section, const char *key,
             const char *value);
  // all keys of a section as "k1\0k2\0\0", return the length used
  int (*keys)(void *ctx, const char *section, char *buf, unsigned size);
  void *ctx;
};

struct prof_report {
  int read;         // prof_read: fields read
  int saved;        // prof_save: fields written
  int kept;         // prof_write: old values kept for fields that failed
  int applied;      // prof_load: fields set
  int unknown;      // keys not in the catalog
  int refused;      // keys that can't be restored from a profile
  int invalid;      // values that don't parse
  int failed;       // hardware errors
  int mismatched;   // fields that read back differently
  char problem[96]; // first problem, for messages
};

void prof_value_text(int field, u8 value, char *buf, unsigned size);

// every persistable field into memory, a busy DSP register tried again
// -1 if some couldn't be read (rep->failed)
int prof_read(struct prof_report *rep);

// what prof_read got to io; a field it couldn't read keeps its value in
// old, the profile being replaced, if there is one; -1 if io failed
int prof_write(const struct prof_io *io, const struct prof_io *old,
               struct prof_report *rep);

// the profile's settings, checked against the catalog, into memory
int prof_parse(const struct prof_io *io, struct prof_report *rep);

// the settings prof_parse got, to the chip
int prof_apply(struct prof_report *rep);

int prof_save(const struct prof_io *io, struct prof_report *rep);
int prof_load(const struct prof_io *io, struct prof_report *rep);

#endif /* PROFILE_H */
