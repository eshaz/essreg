/*
 * ESFM Patch is a program that writes custom patch
 * banks into esfm.drv.
 *
 * Notes:
 *
 * esfm.drv is the Windows 95 FM MIDI driver of the ES1869, found in
 * c:\windows\system. The bank can be a raw bank (256 offsets, then the
 * patches) or a RIFF "Ptch" file. A bank that fits in the driver's resource
 * is written in place. A larger one (up to 32752 bytes) is appended to the
 * file, and the resource entry and the bank loader's size constants are
 * updated. The first run keeps a copy of the original driver as ESFM.BAK.
 *
 * Nothing is changed in place: the backup and the patched driver are
 * written to ESFM.$$$ first, checked, then renamed. A disk error, Ctrl+C
 * or a power cut leaves the old files as they were. A backup that isn't
 * a whole ESFM.DRV is replaced.
 *
 * In a Windows DOS box it doesn't patch the ESFM.DRV that Windows has
 * loaded: its bank loader is discardable and would be read again from
 * the changed file while the resource table in memory is the old one.
 *
 * Usage:
 *   `esfmpat "c:\path\to\esfm.drv" "c:\path\to\patch.bin"`
 *   `esfmpat "c:\path\to\esfm.drv"` (show the driver's patch bank)
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ESS_HOST
#include <i86.h>
#endif

#include "esfmbank.h"

static void usage(void) {
  printf("ESFM Patch Utility (c) 2024 Ethan Halsall "
         "<ethan.s.halsall@gmail.com>\n\n");
  printf("This utility writes your custom patch set to an existing ESFM.DRV "
         "file.\n");
  printf("Compatible with the VxD Windows driver only.  The original driver "
         "is kept\nas ESFM.BAK next to it.\n\n");
  printf("Usage: esfmpat \"c:\\path\\to\\esfm.drv\" "
         "\"c:\\path\\to\\patch.bin\"\n");
  printf("       esfmpat \"c:\\path\\to\\esfm.drv\"   (show its patch bank)\n");
}

// the driver's name with the extension replaced by ext
static void sibling(const char *path, const char *ext, char *out,
                    unsigned size) {
  char *dot, *slash;

  strncpy(out, path, size - 5);
  out[size - 5] = 0;
  dot = strrchr(out, '.');
  slash = strrchr(out, '\\');
  if (!slash || strrchr(out, '/') > slash)
    slash = strrchr(out, '/');
  if (dot && (!slash || dot > slash))
    *dot = 0;
  strcat(out, ".");
  strcat(out, ext);
}

// a whole copy or none: a read or write error removes it
static int copy_file(const char *from, const char *to) {
  static char buf[1024];
  FILE *in, *out;
  size_t n;
  int ok = 1;

  in = fopen(from, "rb");
  if (!in)
    return 0;
  out = fopen(to, "wb");
  if (!out) {
    fclose(in);
    return 0;
  }
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    if (fwrite(buf, 1, n, out) != n)
      ok = 0;
  if (ferror(in))
    ok = 0;
  fclose(in);
  if (fclose(out) != 0)
    ok = 0;
  if (!ok)
    remove(to);
  return ok;
}

// 1 if path is an ESFM.DRV that esfmpat can read and patch
static int usable_driver(const char *path) {
  static struct esfm_drv d;
  FILE *f = fopen(path, "rb");
  int ok;

  if (!f)
    return 0;
  ok = esfm_drv_inspect(f, &d) == 0;
  fclose(f);
  return ok;
}

// tmp becomes path, whose old file goes through old; 0 or -1 with path
// as it was. Ctrl+C can't stop it half way
static int replace_file(const char *path, const char *tmp, const char *old) {
  int err = 0;

  signal(SIGINT, SIG_IGN);
  remove(old);
  if (rename(path, old)) {
    err = -1;
  } else if (rename(tmp, path)) {
    rename(old, path);
    err = -1;
  } else {
    remove(old);
  }
  signal(SIGINT, SIG_DFL);
  return err;
}

// 1 if path is the ESFM.DRV of a running Windows: %windir%\SYSTEM
static int driver_in_use(const char *path) {
#ifdef ESS_HOST
  (void)path;
  return 0;
#else
  static char full[160], live[160];
  const char *windir = getenv("windir");
  union REGS r;

  // Windows 95/98 in enhanced mode, which sets windir in its DOS boxes
  r.w.ax = 0x1600;
  int86(0x2F, &r, &r);
  if (r.h.al == 0 || r.h.al == 0x80 || !windir)
    return 0;
  if (!_fullpath(full, path, sizeof(full)))
    return 0;
  sprintf(live, "%.140s\\SYSTEM\\ESFM.DRV", windir);
  return !stricmp(full, live);
#endif
}

static u8 *read_bank(const char *path, u16 *size) {
  struct bank_info info;
  FILE *f;
  long len;
  u8 *file;
  u32 off, blen;

  f = fopen(path, "rb");
  if (!f) {
    printf("Failed to open the patch file at %s\n", path);
    return 0;
  }
  fseek(f, 0, SEEK_END);
  len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0 || len > BANK_MAX + 64L) {
    printf("%s is not a patch bank (%ld bytes; at most %u)\n", path, len,
           BANK_MAX);
    fclose(f);
    return 0;
  }
  file = (u8 *)malloc((size_t)len);
  if (!file) {
    printf("Failed to allocate %ld bytes of memory\n", len);
    fclose(f);
    return 0;
  }
  if (fread(file, 1, (size_t)len, f) != (size_t)len) {
    printf("Failed to read %s\n", path);
    fclose(f);
    free(file);
    return 0;
  }
  fclose(f);
  if (bank_unwrap(file, (u32)len, &off, &blen) != 0) {
    printf("%s: RIFF file without an \"fm4 \" bank chunk\n", path);
    free(file);
    return 0;
  }
  if (off)
    memmove(file, file + off, (size_t)blen);
  if (bank_check(file, blen, &info) != 0) {
    printf("%s is not a usable patch bank: %s\n", path, info.why);
    free(file);
    return 0;
  }
  printf("Patch bank: %u bytes, %u patches (%u with two voices)\n", info.size,
         info.patches, info.two);
  *size = info.size;
  return file;
}

int main(int argc, char **argv) {
  // static: DOS programs have a small stack
  static struct esfm_drv drv;
  static char bak[160], tmp[160], old[160];
  FILE *f;
  u8 *bank;
  u16 size = 0;
  u32 old_off;

  if (argc != 2 && argc != 3) {
    usage();
    return 1;
  }

  f = fopen(argv[1], "rb");
  if (!f) {
    printf("Failed to open the ESFM driver at %s\n", argv[1]);
    return 1;
  }
  if (esfm_drv_inspect(f, &drv) != 0) {
    printf("%s: %s; not changed\n", argv[1], drv.why);
    fclose(f);
    return 1;
  }
  fclose(f);
  printf("%s: patch bank of %u bytes at offset 0x%04lX (resource of %lu "
         "bytes)\n",
         argv[1], drv.bank_size, (unsigned long)drv.bank_off,
         (unsigned long)drv.bank_len);
  if (argc == 2)
    return 0;
  if (driver_in_use(argv[1])) {
    printf("Windows is running with this driver: patch it from MS-DOS mode\n"
           "(Start > Shut Down > Restart in MS-DOS mode), or load the bank\n"
           "with essctl's ESFM > Load patch bank.\n");
    return 1;
  }

  bank = read_bank(argv[2], &size);
  if (!bank)
    return 1;

  sibling(argv[1], "BAK", bak, sizeof(bak));
  sibling(argv[1], "$$$", tmp, sizeof(tmp));
  sibling(argv[1], "$$O", old, sizeof(old));
  // the backup is the driver before esfmpat first changed it
  f = fopen(bak, "rb");
  if (f)
    fclose(f);
  if (f && usable_driver(bak)) {
    printf("Keeping the existing backup %s\n", bak);
  } else {
    if (f)
      printf("The backup %s isn't a whole ESFM.DRV: making a new one\n", bak);
    if (!copy_file(argv[1], tmp) || !usable_driver(tmp) ||
        (remove(bak), rename(tmp, bak))) {
      remove(tmp);
      printf("Could not back up %s to %s; not changed\n", argv[1], bak);
      free(bank);
      return 1;
    }
    printf("Original driver saved as %s\n", bak);
  }

  // patched on a copy, which replaces the driver once it checks out
  if (!copy_file(argv[1], tmp) || (f = fopen(tmp, "r+b")) == 0) {
    remove(tmp);
    printf("Could not copy %s to %s; not changed\n", argv[1], tmp);
    free(bank);
    return 1;
  }
  old_off = drv.bank_off;
  if (esfm_drv_inspect(f, &drv) != 0 ||
      esfm_drv_patch(f, &drv, bank, size) != 0) {
    printf("%s: %s; not changed\n", argv[1], drv.why);
    fclose(f);
    remove(tmp);
    free(bank);
    return 1;
  }
  free(bank);
  if (fclose(f) != 0 || !usable_driver(tmp) ||
      replace_file(argv[1], tmp, old)) {
    remove(tmp);
    printf("Could not write %s (is the disk full?); not changed\n", argv[1]);
    return 1;
  }

  if (drv.bank_off != old_off)
    printf("Bank moved to offset 0x%04lX; ", (unsigned long)drv.bank_off);
  printf("Successfully patched %s with %s (%u bytes)\n", argv[1], argv[2],
         drv.bank_size);
  printf("Restart Windows to use the new patches.\n");
  return 0;
}
