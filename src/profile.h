/*
 * Saves and restores ES1869 settings as INI text profiles.
 *
 * Example:
 *   [ESSCTL]
 *   Format=1
 *   Chip=ES1869
 *   [Fields]
 *   fx.3d.enable=on
 *   fx.3d.level=40
 *   rec.source=Line
 *
 * Notes:
 *
 * Only fields marked FF_PERSIST with tier SAFE or CAUTION are written or
 * accepted. Anything else in a file is reported and ignored.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
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
  int saved;      // prof_save: fields written
  int applied;    // prof_load: fields set
  int unknown;    // keys not in the catalog
  int refused;    // keys that can't be restored from a profile
  int invalid;    // values that don't parse
  int failed;     // hardware errors
  int mismatched; // fields that read back differently
  char problem[96]; // first problem, for messages
};

void prof_value_text(int field, u8 value, char *buf, unsigned size);
int prof_save(const struct prof_io *io, struct prof_report *rep);
int prof_load(const struct prof_io *io, struct prof_report *rep);

#endif /* PROFILE_H */
