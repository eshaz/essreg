/*
 * Puts the rebuilt drivers in place of ESS's on Windows 95 and 98, and
 * ESS's back, for essinst.exe.
 *
 * Notes:
 *
 * Windows has ES1869.VXD, ES1869.DRV and ESFM.DRV open while it runs, and
 * it reads parts of the 16-bit drivers from their files again later, so a
 * driver file can't change under it. Each new file goes next to the old
 * one instead (ES1869VX.NEW, ES1869.NEW, ESFM.NEW), and the [rename]
 * section of WININIT.INI has Windows move it into place at the next start,
 * before any driver loads. Nothing is listed there until every file is
 * written and checked, so a failure changes nothing.
 *
 * An installed driver must be ESS's 4.04.00.1319, which the rebuilt ones
 * are made from (its version resource, also with a bank that esfmpat
 * wrote), or an earlier rebuilt driver (a string only it has). ESS's
 * driver is kept as ES1869VX.ORG, ES1869.ORG or ESFM.ORG, and a restore
 * puts those back the same way.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef DRVINST_H
#define DRVINST_H

#include "esstypes.h"

#define DI_FILES 3
#define DI_PATH 160 // a path
#define DI_WHY 300  // why nothing was changed

// what a driver file is (di_scan)
#define DI_MISSING 0 // not there, or unreadable
#define DI_ESS 1     // ESS's 4.04.00.1319, as ESS made it
#define DI_ESS_MOD 2 // ESS's 4.04.00.1319 with other bytes, such as a bank
#define DI_REBUILT 3 // a rebuilt driver
#define DI_OTHER 4   // another driver or version

struct di_file {
  const char *name;   // ES1869.VXD
  const char *backup; // ES1869VX.ORG: ESS's driver
  const char *staged; // ES1869VX.NEW: moved into place at the next start
  const char *temp;   // ES1869VX.$$$: a copy until it is checked
  const char *marker; // a string only the rebuilt driver has
  long ess_size;      // ESS's file
  u32 ess_crc;
};

extern const struct di_file di_files[DI_FILES];

struct di_info {
  int kind; // DI_*
  long size;
  u32 crc; // CRC-32, as zlib computes it
};

struct di_ops {
  void *ctx;
  // WININIT.INI: dest becomes src at the next start, or with src 0 the
  // entry is taken back; 0 or -1
  int (*rename_at_start)(void *ctx, const char *dest, const char *src);
  // a line for the log
  void (*log)(void *ctx, const char *text);
};

// what file i of di_files is at path
void di_scan(const char *path, int i, struct di_info *info);

// dir and name joined into out (DI_PATH bytes)
void di_path(char *out, const char *dir, const char *name);

// the installer: the rebuilt drivers in srcdir in place of the installed
// ones in sysdir. With stage 0, it only checks and logs what it would do.
// Returns the number of drivers staged (or to stage), 0 when each one is
// installed already, or -1 when it changes nothing, with the reason in the
// log and in why (DI_WHY bytes)
int di_install(const char *srcdir, const char *sysdir, int stage,
               const struct di_ops *ops, char *why);

// the same in the other direction: ESS's drivers from the backups in
// sysdir in place of the rebuilt ones
int di_restore(const char *sysdir, int stage, const struct di_ops *ops,
               char *why);

#endif /* DRVINST_H */
