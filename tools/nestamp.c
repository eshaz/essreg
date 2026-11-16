/*
 * nestamp sets the "expected Windows version" of a 16-bit
 * Windows executable (NE header offset 3Eh).
 *
 * Notes:
 *
 * Windows 95 draws the dialogs of 16-bit programs marked for
 * version 4.0 with 3-D controls and the gray dialog color,
 * like those of 32-bit programs. The Watcom resource compiler
 * only knows versions 3.0 and 3.1, so the build stamps the
 * program after binding its resources.
 *
 * Builds with Watcom C for DOS (nestamp.mk1) and with any
 * hosted C compiler.
 *
 * Usage:
 *   `nestamp file.exe [major.minor]` (default 4.0)
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <stdlib.h>

static int fail(const char *what, const char *path) {
  fprintf(stderr, "nestamp: %s: %s\n", path, what);
  return 1;
}

int main(int argc, char **argv) {
  unsigned char mz[64], ne[64];
  unsigned major = 4, minor = 0;
  unsigned long ne_off;
  FILE *f;

  if (argc < 2 || argc > 3) {
    fprintf(stderr, "usage: nestamp file.exe [major.minor]\n");
    return 2;
  }
  if (argc == 3 && sscanf(argv[2], "%u.%u", &major, &minor) != 2)
    return fail("bad version", argv[2]);
  f = fopen(argv[1], "r+b");
  if (!f)
    return fail("cannot open", argv[1]);
  if (fread(mz, 1, sizeof(mz), f) != sizeof(mz) || mz[0] != 'M' ||
      mz[1] != 'Z') {
    fclose(f);
    return fail("not an MZ executable", argv[1]);
  }
  ne_off = mz[0x3C] | (mz[0x3D] << 8) | ((unsigned long)mz[0x3E] << 16) |
           ((unsigned long)mz[0x3F] << 24);
  if (fseek(f, (long)ne_off, SEEK_SET) ||
      fread(ne, 1, sizeof(ne), f) != sizeof(ne) || ne[0] != 'N' ||
      ne[1] != 'E') {
    fclose(f);
    return fail("no NE header", argv[1]);
  }
  if (ne[0x36] != 2) {
    fclose(f);
    return fail("not a Windows executable", argv[1]);
  }
  ne[0x3E] = (unsigned char)minor;
  ne[0x3F] = (unsigned char)major;
  if (fseek(f, (long)ne_off + 0x3E, SEEK_SET) ||
      fwrite(ne + 0x3E, 1, 2, f) != 2) {
    fclose(f);
    return fail("write failed", argv[1]);
  }
  fclose(f);
  printf("%s: expected Windows version %u.%u\n", argv[1], major, minor);
  return 0;
}
