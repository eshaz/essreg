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
 * Usage:
 *   `esfmpat "c:\path\to\esfm.drv" "c:\path\to\patch.bin"`
 *   `esfmpat "c:\path\to\esfm.drv"` (show the driver's patch bank)
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// the driver's name with the extension replaced by .BAK
static void backup_name(const char *path, char *out, unsigned size) {
  char *dot, *slash;

  strncpy(out, path, size - 5);
  out[size - 5] = 0;
  dot = strrchr(out, '.');
  slash = strrchr(out, '\\');
  if (!slash || strrchr(out, '/') > slash)
    slash = strrchr(out, '/');
  if (dot && (!slash || dot > slash))
    *dot = 0;
  strcat(out, ".BAK");
}

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
  fclose(in);
  if (fclose(out) != 0)
    ok = 0;
  return ok;
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
  printf("Patch bank: %u bytes, %u patches (%u with two voices)\n",
         info.size, info.patches, info.two);
  *size = info.size;
  return file;
}

int main(int argc, char **argv) {
  struct esfm_drv drv;
  char bak[160];
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

  bank = read_bank(argv[2], &size);
  if (!bank)
    return 1;

  backup_name(argv[1], bak, sizeof(bak));
  f = fopen(bak, "rb");
  if (f) {
    fclose(f);
    printf("Keeping the existing backup %s\n", bak);
  } else if (!copy_file(argv[1], bak)) {
    printf("Could not back up %s to %s; not changed\n", argv[1], bak);
    free(bank);
    return 1;
  } else {
    printf("Original driver saved as %s\n", bak);
  }

  f = fopen(argv[1], "r+b");
  if (!f) {
    printf("Failed to open %s for writing\n", argv[1]);
    free(bank);
    return 1;
  }
  old_off = drv.bank_off;
  if (esfm_drv_inspect(f, &drv) != 0 ||
      esfm_drv_patch(f, &drv, bank, size) != 0) {
    printf("%s: %s\n", argv[1], drv.why);
    printf("Restore the driver from %s if Windows fails to play MIDI.\n",
           bak);
    fclose(f);
    free(bank);
    return 1;
  }
  fclose(f);
  free(bank);

  if (drv.bank_off != old_off)
    printf("Bank moved to offset 0x%04lX; ", (unsigned long)drv.bank_off);
  printf("Successfully patched %s with %s (%u bytes)\n", argv[1], argv[2],
         drv.bank_size);
  printf("Restart Windows to use the new patches.\n");
  return 0;
}
