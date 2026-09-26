/* t_esfm.c -- patch banks and ESFM.DRV patching on a copy of the driver.
 *
 * usage: t_esfm ESFM.DRV bank.bin workdir */

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "esfmbank.h"

static u8 *load(const char *path, long *size) {
  FILE *f = fopen(path, "rb");
  u8 *buf;
  if (!f)
    return 0;
  fseek(f, 0, SEEK_END);
  *size = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = malloc((size_t)*size + 1);
  if (fread(buf, 1, (size_t)*size, f) != (size_t)*size) {
    free(buf);
    buf = 0;
  }
  fclose(f);
  return buf;
}

static void save(const char *path, const u8 *buf, long size) {
  FILE *f = fopen(path, "wb");
  CHECK(f != 0);
  if (f) {
    CHECK(fwrite(buf, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
  }
}

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }

static void test_bank_check(const u8 *bank, long size) {
  struct bank_info info;
  u8 *bad = malloc((size_t)size);

  CHECK_EQ(bank_check(bank, (u32)size, &info), 0);
  CHECK_EQ(info.size, BANK_ORIG_SIZE);
  CHECK_EQ(info.patches, 175);
  CHECK_EQ(info.two, 41);

  CHECK(bank_check(bank, 100, &info) != 0); /* shorter than the table */
  CHECK(bank_check(bank, BANK_MAX + 2, &info) != 0);

  memcpy(bad, bank, (size_t)size);
  bad[2 * 5] = 0x10; /* entry 5 inside the table */
  bad[2 * 5 + 1] = 0x00;
  CHECK(bank_check(bad, (u32)size, &info) != 0);

  memcpy(bad, bank, (size_t)size);
  /* last patch cut short */
  CHECK(bank_check(bad, (u32)size - 10, &info) != 0);

  memset(bad, 0, BANK_TABLE);
  CHECK(bank_check(bad, BANK_TABLE, &info) != 0); /* no patches */
  free(bad);
}

static void test_riff(const u8 *bank, long size) {
  u8 *file = malloc((size_t)size + 21);
  u32 off = 0, len = 0;

  bank_riff_header(file, (u16)size);
  memcpy(file + 20, bank, (size_t)size);
  CHECK(!memcmp(file, "RIFF", 4) && !memcmp(file + 8, "Ptchfm4 ", 8));
  CHECK_EQ(bank_unwrap(file, (u32)size + 20, &off, &len), 0);
  CHECK_EQ(off, 20);
  CHECK_EQ(len, size);
  CHECK_EQ(bank_unwrap(bank, (u32)size, &off, &len), 0); /* raw bank */
  CHECK_EQ(off, 0);
  CHECK_EQ(len, size);
  memcpy(file + 12, "abcd", 4); /* no fm4 chunk */
  CHECK(bank_unwrap(file, (u32)size + 20, &off, &len) != 0);
  free(file);
}

/* bytes that differ between two images, outside [skip0, skip1) */
static int diffs(const u8 *a, const u8 *b, long n, long skip0, long skip1) {
  long i;
  int d = 0;
  for (i = 0; i < n; i++)
    if ((i < skip0 || i >= skip1) && a[i] != b[i])
      d++;
  return d;
}

static void test_driver(const char *drvpath, const u8 *bank, long bsize,
                        const char *work) {
  char path[512];
  struct esfm_drv d, d2;
  long osize, nsize;
  u8 *orig = load(drvpath, &osize), *now, *big;
  FILE *f;
  u16 bigsize;
  long i;

  CHECK(orig != 0);
  if (!orig)
    return;
  sprintf(path, "%s/ESFM.DRV", work);
  save(path, orig, osize);

  f = fopen(path, "rb");
  CHECK_EQ(esfm_drv_inspect(f, &d), 0);
  fclose(f);
  CHECK_EQ(d.bank_off, 0x2400);
  CHECK_EQ(d.bank_len, BANK_ORIG_SIZE);
  CHECK_EQ(d.bank_size, BANK_ORIG_SIZE);
  CHECK_EQ(d.seg3, 0x44A0);
  CHECK_EQ(d.autodata, 4);
  CHECK(!memcmp(orig + d.bank_off, bank, (size_t)bsize)); /* bnk_com.bin */

  /* same size: in place, only the bank bytes change */
  big = malloc(BANK_MAX);
  memcpy(big, bank, (size_t)bsize);
  big[0x200 + 10] ^= 0x5A;
  f = fopen(path, "r+b");
  CHECK_EQ(esfm_drv_inspect(f, &d), 0);
  CHECK_EQ(esfm_drv_patch(f, &d, big, (u16)bsize), 0);
  fclose(f);
  now = load(path, &nsize);
  CHECK_EQ(nsize, osize);
  CHECK_EQ(diffs(orig, now, osize, 0, 0), 1);
  CHECK(!memcmp(now + 0x2400, big, (size_t)bsize));
  free(now);

  /* larger: 40 more patches appended to the bank */
  bigsize = (u16)(bsize + 40 * BANK_VOICE);
  memcpy(big, bank, (size_t)bsize);
  for (i = 0; i < 40; i++) {
    memcpy(big + bsize + i * BANK_VOICE, bank + 0x200, BANK_VOICE);
    big[bsize + i * BANK_VOICE] &= (u8)~6; /* one voice */
    big[2 * (216 + i)] = (u8)(bsize + i * BANK_VOICE);
    big[2 * (216 + i) + 1] = (u8)((bsize + i * BANK_VOICE) >> 8);
  }
  {
    struct bank_info info;
    CHECK_EQ(bank_check(big, bigsize, &info), 0);
  }
  save(path, orig, osize);
  f = fopen(path, "r+b");
  CHECK_EQ(esfm_drv_inspect(f, &d), 0);
  CHECK_EQ(esfm_drv_patch(f, &d, big, bigsize), 0);
  fclose(f);
  CHECK_EQ(d.bank_off, (u32)((osize + 15) & ~15L));
  CHECK_EQ(d.bank_size, bigsize);

  now = load(path, &nsize);
  CHECK_EQ(nsize, (long)(d.bank_off + d.bank_len));
  CHECK(!memcmp(now + d.bank_off, big, bigsize));
  /* the old image is unchanged except the resource entry (4 bytes) and
   * the four size constants (8 bytes) */
  CHECK_EQ(diffs(orig, now, osize, 0, 0) <= 12, 1);
  CHECK_EQ(rd16(now + d.seg3 + ESFM_K_ALLOC), bigsize);
  CHECK_EQ(rd16(now + d.seg3 + ESFM_K_WORDS), bigsize / 2);
  CHECK_EQ(rd16(now + d.seg3 + ESFM_K_RIFF1), bigsize);
  CHECK_EQ(rd16(now + d.seg3 + ESFM_K_RIFF2), bigsize);
  CHECK_EQ(rd16(now + d.res_entry), d.bank_off >> 4);
  CHECK_EQ(rd16(now + d.res_entry + 2), d.bank_len >> 4);
  free(now);

  /* the patched driver is still recognized, with the new bank */
  f = fopen(path, "rb");
  CHECK_EQ(esfm_drv_inspect(f, &d2), 0);
  fclose(f);
  CHECK_EQ(d2.bank_off, d.bank_off);
  CHECK_EQ(d2.bank_size, bigsize);

  /* patching again (smaller) reuses the moved resource in place */
  f = fopen(path, "r+b");
  CHECK_EQ(esfm_drv_inspect(f, &d2), 0);
  CHECK_EQ(esfm_drv_patch(f, &d2, bank, (u16)bsize), 0);
  fclose(f);
  CHECK_EQ(d2.bank_off, d.bank_off);
  CHECK_EQ(d2.bank_size, bsize);

  /* something else is refused untouched */
  memcpy(big, orig, (size_t)osize);
  big[0x44A0 + 0x06FE] = 0x90; /* rep movsw replaced */
  save(path, big, osize);
  f = fopen(path, "rb");
  CHECK(esfm_drv_inspect(f, &d) != 0);
  fclose(f);
  CHECK(strstr(d.why, "build") != 0);

  free(big);
  free(orig);
}

int main(int argc, char **argv) {
  long bsize;
  u8 *bank;

  if (argc != 4) {
    printf("usage: t_esfm ESFM.DRV bank.bin workdir\n");
    return 2;
  }
  bank = load(argv[2], &bsize);
  CHECK(bank != 0);
  if (bank) {
    test_bank_check(bank, bsize);
    test_riff(bank, bsize);
    test_driver(argv[1], bank, bsize, argv[3]);
    free(bank);
  }
  return CHECK_DONE("t_esfm");
}
