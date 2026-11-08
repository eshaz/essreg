/*
 * The work of essinst.exe (drvinst.h): what each driver file is, ESS's
 * drivers kept as backups, the new files staged next to the old ones, and
 * their entries in WININIT.INI.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <string.h>

#include "drvinst.h"

#ifdef ESS_HOST
#define DI_SEP '/'
#define commit_file(f) ((void)(f))
#else
#include <io.h>
#define DI_SEP '\\'
#define commit_file(f) _commit(fileno(f))
#endif

#define CHUNK 4096
#define TAIL 24 // kept from the last chunk: a marker or the version across two
#define LINE (2 * DI_PATH + 100)

// ESS's 4.04.00.1319, as in driver/, and its version resource
const struct di_file di_files[DI_FILES] = {
    {"ES1869.VXD", "ES1869VX.ORG", "ES1869VX.NEW", "ES1869VX.$$$",
     "RegisterAPI", 36028L, 0xD43E73FCUL},
    {"ES1869.DRV", "ES1869.ORG", "ES1869.NEW", "ES1869.$$$", "ESDRVFIX", 77440L,
     0x5AFC7F3AUL},
    {"ESFM.DRV", "ESFM.ORG", "ESFM.NEW", "ESFM.$$$", "ESFMFIX", 20976L,
     0xD19CF23FUL},
};

#define VERSION_MS 0x00040004UL // 4.04
#define VERSION_LS 0x00000527UL // .00.1319

static u8 buf[TAIL + CHUNK];
static char line[LINE];

static u32 crc32(u32 crc, const u8 *p, unsigned n) {
  static const u32 t[16] = {
      0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
      0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
      0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
      0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL};

  crc = ~crc & 0xFFFFFFFFUL;
  while (n--) {
    crc ^= *p++;
    crc = (crc >> 4) ^ t[(unsigned)crc & 15];
    crc = (crc >> 4) ^ t[(unsigned)crc & 15];
  }
  return ~crc & 0xFFFFFFFFUL;
}

static u32 get32(const u8 *p) {
  return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void say(const struct di_ops *ops, const char *text) {
  if (ops->log)
    ops->log(ops->ctx, text);
}

void di_path(char *out, const char *dir, const char *name) {
  unsigned n = (unsigned)strlen(dir);

  if (n > DI_PATH - 14)
    n = DI_PATH - 14;
  memcpy(out, dir, n);
  if (n && out[n - 1] != '\\' && out[n - 1] != '/' && out[n - 1] != ':')
    out[n++] = DI_SEP;
  strcpy(out + n, name);
}

// 1 if path is an executable of the format of file i: LE for the VxD, NE
// for the 16-bit drivers
static int format_ok(const char *path, int i) {
  u8 head[64], sig[2];
  FILE *fp = fopen(path, "rb");
  int ok;

  if (!fp)
    return 0;
  ok = fread(head, 1, sizeof(head), fp) == sizeof(head) && head[0] == 'M' &&
       head[1] == 'Z' && !fseek(fp, (long)get32(head + 0x3C), SEEK_SET) &&
       fread(sig, 1, 2, fp) == 2 && sig[0] == (i ? 'N' : 'L') && sig[1] == 'E';
  fclose(fp);
  return ok;
}

void di_scan(const char *path, int i, struct di_info *info) {
  const struct di_file *f = &di_files[i];
  unsigned mlen = (unsigned)strlen(f->marker), keep = 0, have, n, j;
  int marker = 0, version = 0, bad;
  FILE *fp = fopen(path, "rb");

  info->kind = DI_MISSING;
  info->size = 0;
  info->crc = 0;
  if (!fp)
    return;
  while ((n = (unsigned)fread(buf + keep, 1, CHUNK, fp)) > 0) {
    info->crc = crc32(info->crc, buf + keep, n);
    info->size += n;
    have = keep + n;
    for (j = 0; j + mlen <= have; j++)
      if (buf[j] == (u8)f->marker[0] && !memcmp(buf + j, f->marker, mlen))
        marker = 1;
    // VS_FIXEDFILEINFO: its signature, then the file version
    for (j = 0; j + 16 <= have; j++)
      if (get32(buf + j) == 0xFEEF04BDUL && get32(buf + j + 8) == VERSION_MS &&
          get32(buf + j + 12) == VERSION_LS)
        version = 1;
    keep = have < TAIL ? have : TAIL;
    memmove(buf, buf + have - keep, keep);
  }
  bad = ferror(fp);
  fclose(fp);
  if (bad || !info->size)
    return;
  if (info->size == f->ess_size && info->crc == f->ess_crc)
    info->kind = DI_ESS;
  else if (!format_ok(path, i))
    info->kind = DI_OTHER;
  else if (marker)
    info->kind = DI_REBUILT;
  else if (version)
    info->kind = DI_ESS_MOD;
  else
    info->kind = DI_OTHER;
}

static int same(const struct di_info *a, const struct di_info *b) {
  return a->kind != DI_MISSING && a->size == b->size && a->crc == b->crc;
}

static int ess(const struct di_info *a) {
  return a->kind == DI_ESS || a->kind == DI_ESS_MOD;
}

// from to to, whole or not at all, and read back: 0 or -1
static int copy_checked(const char *from, const char *to, int i,
                        const struct di_info *want) {
  struct di_info got;
  FILE *in, *out;
  unsigned n;
  int ok = 1;

  in = fopen(from, "rb");
  if (!in)
    return -1;
  out = fopen(to, "wb");
  if (!out) {
    fclose(in);
    return -1;
  }
  while ((n = (unsigned)fread(buf, 1, CHUNK, in)) > 0)
    if (fwrite(buf, 1, n, out) != n)
      ok = 0;
  if (ferror(in))
    ok = 0;
  fclose(in);
  if (fflush(out))
    ok = 0;
  commit_file(out);
  if (fclose(out))
    ok = 0;
  if (ok) {
    di_scan(to, i, &got);
    ok = same(&got, want);
  }
  if (!ok)
    remove(to);
  return ok ? 0 : -1;
}

// the file from, which is want, as name in dir: first as file i's .$$$,
// checked, then renamed. 0 or -1, with dir\name as it was
static int write_as(const char *from, const char *dir, const char *name, int i,
                    const struct di_info *want) {
  char tmp[DI_PATH], to[DI_PATH];

  di_path(tmp, dir, di_files[i].temp);
  di_path(to, dir, name);
  remove(tmp);
  if (copy_checked(from, tmp, i, want))
    return -1;
  remove(to);
  if (rename(tmp, to)) {
    remove(tmp);
    return -1;
  }
  return 0;
}

static int fail(const struct di_ops *ops, char *why, const char *text) {
  strncpy(why, text, DI_WHY - 1);
  why[DI_WHY - 1] = 0;
  say(ops, text);
  return -1;
}

// the staged files of the drivers in todo listed in WININIT.INI, all or
// none: 0 or -1
static int list_staged(const char *sysdir, const int *todo,
                       const struct di_ops *ops, char *why) {
  char dest[DI_PATH], from[DI_PATH];
  int i, j;

  for (i = 0; i < DI_FILES; i++) {
    if (!todo[i])
      continue;
    di_path(dest, sysdir, di_files[i].name);
    di_path(from, sysdir, di_files[i].staged);
    if (ops->rename_at_start(ops->ctx, dest, from)) {
      for (j = 0; j < i; j++)
        if (todo[j]) {
          di_path(dest, sysdir, di_files[j].name);
          ops->rename_at_start(ops->ctx, dest, 0);
        }
      return fail(ops, why,
                  "WININIT.INI in the Windows folder can't be "
                  "written, so nothing was changed.");
    }
    sprintf(line, "%s goes in place of %s at the next start.",
            di_files[i].staged, di_files[i].name);
    say(ops, line);
  }
  return 0;
}

// the staged files that were written, removed again
static void unstage(const char *sysdir, const int *todo, int upto) {
  char path[DI_PATH];
  int i;

  for (i = 0; i < upto; i++)
    if (todo[i]) {
      di_path(path, sysdir, di_files[i].staged);
      remove(path);
    }
}

// 1 if path, ES1869.ORG, holds ESS's VxD as ESS made it, info its scan
static int vxd_kept_by_hand(const char *path, struct di_info *info) {
  di_scan(path, 0, info);
  return info->kind == DI_ESS;
}

static const char *kind_text(int kind) {
  switch (kind) {
  case DI_ESS:
    return "ESS's driver";
  case DI_ESS_MOD:
    return "ESS's driver with changes, such as a patch bank";
  case DI_REBUILT:
    return "an earlier rebuilt driver";
  case DI_OTHER:
    return "another driver or version";
  }
  return "not there";
}

int di_install(const char *srcdir, const char *sysdir, int stage,
               const struct di_ops *ops, char *why) {
  struct di_info have[DI_FILES], want[DI_FILES], org;
  char path[DI_PATH], from[DI_PATH];
  int todo[DI_FILES], i, n = 0, offered = 0;

  for (i = 0; i < DI_FILES; i++) {
    todo[i] = 0;
    di_path(path, srcdir, di_files[i].name);
    di_scan(path, i, &want[i]);
    di_path(path, sysdir, di_files[i].name);
    di_scan(path, i, &have[i]);
  }
  if (have[0].kind == DI_MISSING) {
    di_path(path, sysdir, di_files[0].name);
    sprintf(line,
            "%.*s isn't there, so ESS's ES1869 driver isn't installed. "
            "Install it first, and then the rebuilt drivers.",
            DI_PATH, path);
    return fail(ops, why, line);
  }
  for (i = 0; i < DI_FILES; i++) {
    if (want[i].kind == DI_MISSING) {
      sprintf(line, "%s: none in %.*s, so it stays as it is.", di_files[i].name,
              DI_PATH, srcdir);
      say(ops, line);
      continue;
    }
    offered++;
    di_path(path, srcdir, di_files[i].name);
    if (want[i].kind != DI_REBUILT) {
      sprintf(line, "%.*s isn't a rebuilt driver (%s), so nothing was changed.",
              DI_PATH, path, kind_text(want[i].kind));
      return fail(ops, why, line);
    }
    di_path(path, sysdir, di_files[i].name);
    if (have[i].kind == DI_MISSING || have[i].kind == DI_OTHER) {
      sprintf(line,
              "%.*s isn't ESS's driver 4.04.00.1319 (%s), which the rebuilt "
              "drivers replace, so nothing was changed.",
              DI_PATH, path, kind_text(have[i].kind));
      return fail(ops, why, line);
    }
    if (same(&have[i], &want[i])) {
      sprintf(line, "%s: installed already.", di_files[i].name);
      say(ops, line);
      continue;
    }
    todo[i] = 1;
    n++;
    if (ess(&have[i]))
      sprintf(line, "%s: %s, kept as %s.", di_files[i].name,
              kind_text(have[i].kind), di_files[i].backup);
    else
      sprintf(line, "%s: %s, replaced.", di_files[i].name,
              kind_text(have[i].kind));
    say(ops, line);
  }
  if (!offered) {
    sprintf(line, "None of the rebuilt drivers is in %.*s.", DI_PATH, srcdir);
    return fail(ops, why, line);
  }
  if (!n || !stage)
    return n;

  // the README's steps by hand kept ESS's VxD as ES1869.ORG, where ESS's
  // ES1869.DRV is kept: it moves to ES1869VX.ORG first
  di_path(path, sysdir, di_files[0].backup);
  di_scan(path, 0, &org);
  di_path(from, sysdir, di_files[1].backup);
  if (org.kind == DI_MISSING && vxd_kept_by_hand(from, &org)) {
    if (write_as(from, sysdir, di_files[0].backup, 0, &org)) {
      sprintf(line, "%.*s can't be written, so nothing was changed.", DI_PATH,
              path);
      return fail(ops, why, line);
    }
    sprintf(line, "Kept ESS's %s, found as %s, as %s.", di_files[0].name,
            di_files[1].backup, di_files[0].backup);
    say(ops, line);
  }

  // ESS's drivers kept first
  for (i = 0; i < DI_FILES; i++) {
    if (!todo[i])
      continue;
    di_path(path, sysdir, di_files[i].backup);
    di_scan(path, i, &org);
    di_path(from, sysdir, di_files[i].name);
    if (ess(&have[i])) {
      if (same(&org, &have[i]))
        continue;
      if (write_as(from, sysdir, di_files[i].backup, i, &have[i])) {
        sprintf(line, "%.*s can't be written, so nothing was changed.", DI_PATH,
                path);
        return fail(ops, why, line);
      }
      sprintf(line, "Kept %s as %s.", di_files[i].name, di_files[i].backup);
      say(ops, line);
    } else if (ess(&org)) {
      sprintf(line, "ESS's %s is kept as %s already.", di_files[i].name,
              di_files[i].backup);
      say(ops, line);
    } else {
      sprintf(line, "No copy of ESS's %s is kept.", di_files[i].name);
      say(ops, line);
    }
  }
  // then the new files, next to the old ones
  for (i = 0; i < DI_FILES; i++) {
    if (!todo[i])
      continue;
    di_path(from, srcdir, di_files[i].name);
    if (write_as(from, sysdir, di_files[i].staged, i, &want[i])) {
      unstage(sysdir, todo, i);
      di_path(path, sysdir, di_files[i].staged);
      sprintf(line, "%.*s can't be written, so nothing was changed.", DI_PATH,
              path);
      return fail(ops, why, line);
    }
  }
  if (list_staged(sysdir, todo, ops, why)) {
    unstage(sysdir, todo, DI_FILES);
    return -1;
  }
  return n;
}

int di_rebuilt_after(const char *srcdir, const char *sysdir) {
  struct di_info have, other;
  char path[DI_PATH];

  di_path(path, sysdir, di_files[1].name);
  di_scan(path, 1, &have);
  if (srcdir) {
    // di_install puts a rebuilt driver there, or leaves the one there
    di_path(path, srcdir, di_files[1].name);
    di_scan(path, 1, &other);
    return other.kind == DI_REBUILT ||
           (other.kind == DI_MISSING && have.kind == DI_REBUILT);
  }
  // di_restore takes a rebuilt driver back if ESS's is kept
  di_path(path, sysdir, di_files[1].backup);
  di_scan(path, 1, &other);
  return have.kind == DI_REBUILT && !ess(&other);
}

// the device names of ESS's driver and of the rebuilt one: a prefix, then
// Audio_Base in hex and ")"
#define PFX_PLAY "ESS AudioDrive Playback ("
#define PFX_A2 "ESS AudioDrive Audio 2 ("
#define PFX_A1 "ESS AudioDrive Audio 1 ("
#define PFX_REC "ESS AudioDrive Record ("
#define PFX_FM "ESS AudioDrive FM Digital ("

int di_device_name(const char *name, int names, char *out) {
  // a device's name with the names flag set (on 1) or clear (on 0); a
  // device that's gone takes the name of the one that plays in its place
  static const struct {
    const char *from, *to;
    int flag, on;
  } map[] = {
      {PFX_PLAY, PFX_A2, DI_NAME_AUDIO1, 1},
      {PFX_A2, PFX_PLAY, DI_NAME_AUDIO1, 0},
      {PFX_A1, PFX_PLAY, DI_NAME_AUDIO1, 0},
      {PFX_FM, PFX_REC, DI_NAME_FMREC, 0},
  };
  unsigned i, n;

  for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
    n = (unsigned)strlen(map[i].from);
    if (strncmp(name, map[i].from, n) ||
        ((names & map[i].flag) != 0) != map[i].on)
      continue;
    if (strlen(map[i].to) + strlen(name + n) > 31)
      return 0;
    strcpy(out, map[i].to);
    strcat(out, name + n);
    return 1;
  }
  return 0;
}

int di_restore(const char *sysdir, int stage, const struct di_ops *ops,
               char *why) {
  struct di_info have[DI_FILES], org[DI_FILES];
  const char *backup[DI_FILES];
  char path[DI_PATH], from[DI_PATH];
  int todo[DI_FILES], i, n = 0, kept = 0;

  for (i = 0; i < DI_FILES; i++) {
    todo[i] = 0;
    backup[i] = di_files[i].backup;
    di_path(path, sysdir, di_files[i].name);
    di_scan(path, i, &have[i]);
    di_path(path, sysdir, backup[i]);
    di_scan(path, i, &org[i]);
    if (!i && org[i].kind == DI_MISSING) {
      di_path(path, sysdir, di_files[1].backup);
      if (vxd_kept_by_hand(path, &org[i]))
        backup[i] = di_files[1].backup;
    }
    if (!ess(&org[i])) {
      sprintf(line, "%s: no copy of ESS's driver as %s.", di_files[i].name,
              di_files[i].backup);
      say(ops, line);
      continue;
    }
    kept++;
    if (have[i].kind != DI_REBUILT) {
      sprintf(line, "%s: %s, so it stays as it is.", di_files[i].name,
              kind_text(have[i].kind));
      say(ops, line);
      continue;
    }
    todo[i] = 1;
    n++;
    sprintf(line, "%s: %s, back from %s.", di_files[i].name,
            kind_text(org[i].kind), backup[i]);
    say(ops, line);
  }
  if (!kept) {
    sprintf(line,
            "No copy of ESS's drivers is in %.*s, so nothing was changed.",
            DI_PATH, sysdir);
    return fail(ops, why, line);
  }
  if (!n || !stage)
    return n;
  for (i = 0; i < DI_FILES; i++) {
    if (!todo[i])
      continue;
    di_path(from, sysdir, backup[i]);
    if (write_as(from, sysdir, di_files[i].staged, i, &org[i])) {
      unstage(sysdir, todo, i);
      di_path(path, sysdir, di_files[i].staged);
      sprintf(line, "%.*s can't be written, so nothing was changed.", DI_PATH,
              path);
      return fail(ops, why, line);
    }
  }
  if (list_staged(sysdir, todo, ops, why)) {
    unstage(sysdir, todo, DI_FILES);
    return -1;
  }
  return n;
}
