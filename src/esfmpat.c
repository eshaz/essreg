/*
 * ESFM Patch is a program that writes custom patch
 * banks into esfm.drv.
 *
 * Usage:
 *   `esfmpat "c:\path\to\esfm.drv" "c:\path\to\patch.bin"`
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <stdlib.h>

static const long patch_offset = 0x2400;

int main(int argc, char **argv) {
  FILE *esfm_drv;
  FILE *patch_bin;
  long esfm_drv_size;
  long patch_bin_size;
  char *esfm_drv_new;

  if (argc != 3) {
    printf("ESFM Patch Utility (c) 2024 Ethan Halsall "
           "<ethan.s.halsall@gmail.com>\n\n");
    printf("This utility writes your custom patch set to an existing ESFM.DRV "
           "file.\n");
    printf("Compatible with the VxD Windows driver only.\n\n");
    printf("** WARNING: BACKUP YOUR ORIGINAL ESFM.DRV BEFORE RUNNING THIS "
           "UTILITY! **\n\n");
    printf("Usage: esfmpat \"c:\\path\\to\\esfm.drv\" "
           "\"c:\\path\\to\\patch.bin\"");
    return 1;
  }

  esfm_drv = fopen(argv[1], "rb");
  if (esfm_drv == NULL) {
    printf("Failed to open the ESFM driver at %s", argv[1]);
    return 1;
  }

  patch_bin = fopen(argv[2], "rb");
  if (patch_bin == NULL) {
    printf("Failed to open the patch file at %s", argv[2]);
    return 1;
  }

  // get the size of the ESFM driver file
  fseek(esfm_drv, 0, SEEK_END);
  esfm_drv_size = ftell(esfm_drv);
  fseek(esfm_drv, 0, SEEK_SET);

  // read the original
  esfm_drv_new = malloc(esfm_drv_size);
  if (esfm_drv_new == NULL) {
    printf("Failed to allocate %lu bytes of memory\n", esfm_drv_size);
    return 1;
  }
  fread(esfm_drv_new, 1, esfm_drv_size, esfm_drv);
  fclose(esfm_drv);

  // get the size of the patch file
  fseek(patch_bin, 0, SEEK_END);
  patch_bin_size = ftell(patch_bin);
  fseek(patch_bin, 0, SEEK_SET);

  // write the patch at the offset
  if (patch_bin_size + patch_offset > esfm_drv_size) {
    printf("Patch file is too big to fit in %s!", argv[1]);
  }
  fread(esfm_drv_new + patch_offset, 1, patch_bin_size, patch_bin);
  fclose(patch_bin);

  // write the patched file
  esfm_drv = fopen(argv[1], "wb");
  fwrite(esfm_drv_new, 1, esfm_drv_size, esfm_drv);
  fclose(esfm_drv);

  printf("Successfully patched %s with %s at offset 0x%04lx", argv[1], argv[2],
         patch_offset);
  free(esfm_drv_new);

  return 0;
}
