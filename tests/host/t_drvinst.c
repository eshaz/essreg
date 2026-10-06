/*
 * t_drvinst tests essinst's work (src/drvinst.c) on the real drivers:
 * ESS's from driver/ and the rebuilt ones from build/, in a Windows
 * SYSTEM folder and an install folder made in the work directory. The
 * WININIT.INI entries are kept in a list, and a "restart" carries them
 * out as Windows does.
 *
 * Usage:
 *   `t_drvinst ROOT WORKDIR`
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "check.h"
#include "drvinst.h"

static char root[DI_PATH], src[DI_PATH], sys[DI_PATH];
static char dests[8][DI_PATH], froms[8][DI_PATH];
static int entries, fail_at, calls, takebacks, logged;

static int rename_at_start(void *ctx, const char *dest, const char *from) {
  int i;

  (void)ctx;
  calls++;
  if (!from) {
    takebacks++;
    for (i = 0; i < entries; i++)
      if (!strcmp(dests[i], dest)) {
        memmove(dests[i], dests[i + 1], (entries - i - 1) * DI_PATH);
        memmove(froms[i], froms[i + 1], (entries - i - 1) * DI_PATH);
        entries--;
        break;
      }
    return 0;
  }
  if (fail_at && calls == fail_at)
    return -1;
  for (i = 0; i < entries; i++)
    if (!strcmp(dests[i], dest))
      break;
  strcpy(dests[i], dest);
  strcpy(froms[i], from);
  if (i == entries)
    entries++;
  return 0;
}

static void log_line(void *ctx, const char *text) {
  (void)ctx;
  (void)text;
  logged++;
}

static const struct di_ops ops = {0, rename_at_start, log_line};

static long load(const char *path, unsigned char *out, long max) {
  FILE *f = fopen(path, "rb");
  long n;

  if (!f)
    return -1;
  n = (long)fread(out, 1, max, f);
  fclose(f);
  return n;
}

static void save(const char *path, const unsigned char *data, long n) {
  FILE *f = fopen(path, "wb");

  fwrite(data, 1, n, f);
  fclose(f);
}

// 1 if the files at a and b hold the same bytes
static int same_file(const char *a, const char *b) {
  static unsigned char x[0x20000], y[0x20000];
  long n = load(a, x, sizeof(x)), m = load(b, y, sizeof(y));

  return n >= 0 && n == m && !memcmp(x, y, n);
}

static void copy(const char *from, const char *to) {
  static unsigned char data[0x20000];
  long n = load(from, data, sizeof(data));

  CHECK(n > 0);
  save(to, data, n);
}

static int exists(const char *path) {
  struct stat st;

  return !stat(path, &st);
}

static void in_dir(char *out, const char *dir, const char *name) {
  sprintf(out, "%s/%s", dir, name);
}

static void ref(char *out, const char *which, const char *name) {
  sprintf(out, "%s/%s/%s", root, which, name);
}

// a fresh SYSTEM folder with ESS's drivers and an install folder with the
// rebuilt ones
static void setup(void) {
  char a[DI_PATH], b[DI_PATH];
  int i;

  for (i = 0; i < DI_FILES; i++) {
    ref(a, "driver", di_files[i].name);
    in_dir(b, sys, di_files[i].name);
    copy(a, b);
    ref(a, "build", di_files[i].name);
    in_dir(b, src, di_files[i].name);
    copy(a, b);
    in_dir(b, sys, di_files[i].backup);
    remove(b);
    in_dir(b, sys, di_files[i].staged);
    remove(b);
  }
  entries = calls = takebacks = fail_at = 0;
}

// what Windows does with WININIT.INI at its next start
static void restart(void) {
  int i;

  for (i = 0; i < entries; i++) {
    remove(dests[i]);
    CHECK(rename(froms[i], dests[i]) == 0);
  }
  entries = 0;
}

static void test_scan(void) {
  struct di_info info;
  char path[DI_PATH];
  int i;

  for (i = 0; i < DI_FILES; i++) {
    ref(path, "driver", di_files[i].name);
    di_scan(path, i, &info);
    CHECK_EQ(info.kind, DI_ESS);
    CHECK_EQ(info.size, di_files[i].ess_size);
    ref(path, "build", di_files[i].name);
    di_scan(path, i, &info);
    CHECK_EQ(info.kind, DI_REBUILT);
  }
  // the wave driver isn't a VxD, nor the VxD a 16-bit driver
  ref(path, "driver", "ES1869.DRV");
  di_scan(path, 0, &info);
  CHECK_EQ(info.kind, DI_OTHER);
  ref(path, "build", "ES1869.VXD");
  di_scan(path, 2, &info);
  CHECK_EQ(info.kind, DI_OTHER);
  ref(path, "driver", "OEMSETUP.INF");
  di_scan(path, 1, &info);
  CHECK_EQ(info.kind, DI_OTHER);
  in_dir(path, sys, "NONE.DRV");
  di_scan(path, 1, &info);
  CHECK_EQ(info.kind, DI_MISSING);
}

static void test_install_and_restore(void) {
  char why[DI_WHY], a[DI_PATH], b[DI_PATH];
  int i;

  setup();
  // a check alone changes nothing
  CHECK_EQ(di_install(src, sys, 0, &ops, why), 3);
  CHECK_EQ(entries, 0);
  in_dir(a, sys, di_files[0].backup);
  CHECK(!exists(a));
  CHECK_EQ(di_install(src, sys, 1, &ops, why), 3);
  CHECK_EQ(entries, 3);
  for (i = 0; i < DI_FILES; i++) {
    ref(a, "driver", di_files[i].name);
    in_dir(b, sys, di_files[i].backup);
    CHECK(same_file(a, b)); // ESS's driver kept
    ref(a, "build", di_files[i].name);
    in_dir(b, sys, di_files[i].staged);
    CHECK(same_file(a, b)); // the new one staged
    in_dir(b, sys, di_files[i].temp);
    CHECK(!exists(b));
    in_dir(b, sys, di_files[i].name);
    CHECK(!strcmp(dests[i], b));
    in_dir(b, sys, di_files[i].staged);
    CHECK(!strcmp(froms[i], b));
    ref(a, "driver", di_files[i].name);
    in_dir(b, sys, di_files[i].name);
    CHECK(same_file(a, b)); // nothing in place before a restart
  }
  restart();
  for (i = 0; i < DI_FILES; i++) {
    ref(a, "build", di_files[i].name);
    in_dir(b, sys, di_files[i].name);
    CHECK(same_file(a, b));
  }
  CHECK_EQ(di_install(src, sys, 1, &ops, why), 0);
  CHECK_EQ(entries, 0);

  // and back
  CHECK_EQ(di_restore(sys, 0, &ops, why), 3);
  CHECK_EQ(di_restore(sys, 1, &ops, why), 3);
  CHECK_EQ(entries, 3);
  restart();
  for (i = 0; i < DI_FILES; i++) {
    ref(a, "driver", di_files[i].name);
    in_dir(b, sys, di_files[i].name);
    CHECK(same_file(a, b));
    in_dir(b, sys, di_files[i].backup);
    CHECK(same_file(a, b)); // the copy stays
  }
  CHECK_EQ(di_restore(sys, 1, &ops, why), 0);
  // an update over the installed rebuild keeps the same copies
  CHECK_EQ(di_install(src, sys, 1, &ops, why), 3);
  restart();
  CHECK_EQ(di_install(src, sys, 1, &ops, why), 0);
}

static void test_refusals(void) {
  static unsigned char data[0x20000];
  char why[DI_WHY], a[DI_PATH], b[DI_PATH];
  long n;
  int i;

  // ESS's driver not installed
  setup();
  in_dir(a, sys, "ES1869.VXD");
  remove(a);
  CHECK_EQ(di_install(src, sys, 1, &ops, why), -1);
  CHECK(strstr(why, "isn't installed") != 0);
  // another driver: nothing changes at all
  setup();
  in_dir(a, sys, "ESFM.DRV");
  save(a, (const unsigned char *)"not a driver", 12);
  CHECK_EQ(di_install(src, sys, 1, &ops, why), -1);
  CHECK(strstr(why, "ESFM.DRV isn't ESS's driver") != 0);
  CHECK_EQ(calls, 0);
  for (i = 0; i < DI_FILES; i++) {
    in_dir(b, sys, di_files[i].backup);
    CHECK(!exists(b));
    in_dir(b, sys, di_files[i].staged);
    CHECK(!exists(b));
  }
  // ESS's driver in the install folder instead of a rebuilt one
  setup();
  ref(a, "driver", "ES1869.DRV");
  in_dir(b, src, "ES1869.DRV");
  copy(a, b);
  CHECK_EQ(di_install(src, sys, 1, &ops, why), -1);
  CHECK(strstr(why, "isn't a rebuilt driver") != 0);
  // nothing to install
  setup();
  for (i = 0; i < DI_FILES; i++) {
    in_dir(b, src, di_files[i].name);
    remove(b);
  }
  CHECK_EQ(di_install(src, sys, 1, &ops, why), -1);
  CHECK(strstr(why, "None of the rebuilt drivers") != 0);
  // no copy of ESS's drivers to put back
  setup();
  CHECK_EQ(di_restore(sys, 1, &ops, why), -1);
  CHECK(strstr(why, "No copy of ESS's drivers") != 0);
  // a version of ESS's driver changed by esfmpat (a byte of its bank) is
  // ESS's, and kept as it is
  setup();
  in_dir(a, sys, "ESFM.DRV");
  n = load(a, data, sizeof(data));
  data[n - 100] ^= 0x55;
  save(a, data, n);
  CHECK_EQ(di_install(src, sys, 1, &ops, why), 3);
  in_dir(b, sys, "ESFM.ORG");
  CHECK(same_file(a, b));
}

static void test_wininit_fails(void) {
  char why[DI_WHY], a[DI_PATH], b[DI_PATH];
  int i;

  // the second entry can't be written: the first is taken back, and the
  // staged files go
  setup();
  fail_at = 2;
  CHECK_EQ(di_install(src, sys, 1, &ops, why), -1);
  CHECK(strstr(why, "WININIT.INI") != 0);
  CHECK_EQ(entries, 0);
  CHECK_EQ(takebacks, 1);
  for (i = 0; i < DI_FILES; i++) {
    in_dir(b, sys, di_files[i].staged);
    CHECK(!exists(b));
    ref(a, "driver", di_files[i].name);
    in_dir(b, sys, di_files[i].name);
    CHECK(same_file(a, b));
  }
}

static void test_some_drivers(void) {
  char why[DI_WHY], a[DI_PATH], b[DI_PATH];

  // only ESFM.DRV next to the installer: only it goes in
  setup();
  in_dir(a, src, "ES1869.VXD");
  remove(a);
  in_dir(a, src, "ES1869.DRV");
  remove(a);
  CHECK_EQ(di_install(src, sys, 1, &ops, why), 1);
  CHECK_EQ(entries, 1);
  in_dir(b, sys, "ESFM.DRV");
  CHECK(!strcmp(dests[0], b));
  in_dir(b, sys, "ES1869VX.ORG");
  CHECK(!exists(b));
}

static void test_vxd_kept_by_hand(void) {
  char why[DI_WHY], a[DI_PATH], b[DI_PATH];

  // the rebuilt VxD installed by hand, ESS's kept as ES1869.ORG as the
  // README's steps had it: the copy moves to ES1869VX.ORG before ESS's
  // ES1869.DRV is kept as ES1869.ORG
  setup();
  in_dir(a, sys, "ES1869.VXD");
  in_dir(b, sys, "ES1869.ORG");
  copy(a, b);
  ref(a, "build", "ES1869.VXD");
  in_dir(b, sys, "ES1869.VXD");
  copy(a, b);
  CHECK_EQ(di_install(src, sys, 1, &ops, why), 2);
  ref(a, "driver", "ES1869.VXD");
  in_dir(b, sys, "ES1869VX.ORG");
  CHECK(same_file(a, b));
  ref(a, "driver", "ES1869.DRV");
  in_dir(b, sys, "ES1869.ORG");
  CHECK(same_file(a, b));
  // and a restore finds it there before that
  setup();
  in_dir(a, sys, "ES1869.VXD");
  in_dir(b, sys, "ES1869.ORG");
  copy(a, b);
  ref(a, "build", "ES1869.VXD");
  in_dir(b, sys, "ES1869.VXD");
  copy(a, b);
  CHECK_EQ(di_restore(sys, 1, &ops, why), 1);
  restart();
  ref(a, "driver", "ES1869.VXD");
  in_dir(b, sys, "ES1869.VXD");
  CHECK(same_file(a, b));
}

static void test_device_names(void) {
  int all = DI_NAME_AUDIO1 | DI_NAME_FMREC;
  char out[32];

  // the rebuilt driver's names, and ESS's back
  CHECK(di_device_name("ESS AudioDrive Playback (220)", all, out));
  CHECK(!strcmp(out, "ESS AudioDrive Audio 2 (220)"));
  CHECK(di_device_name("ESS AudioDrive Audio 2 (240)", DI_NAMES_ESS, out));
  CHECK(!strcmp(out, "ESS AudioDrive Playback (240)"));
  // the new devices are gone with ESS's driver: the one that plays or
  // records in their place
  CHECK(di_device_name("ESS AudioDrive Audio 1 (220)", DI_NAMES_ESS, out));
  CHECK(!strcmp(out, "ESS AudioDrive Playback (220)"));
  CHECK(di_device_name("ESS AudioDrive FM Digital (220)", DI_NAMES_ESS, out));
  CHECK(!strcmp(out, "ESS AudioDrive Record (220)"));
  // Audio1Device=0: device 0 has ESS's name
  CHECK(di_device_name("ESS AudioDrive Audio 2 (220)", DI_NAME_FMREC, out));
  CHECK(!strcmp(out, "ESS AudioDrive Playback (220)"));
  // names the devices have with those drivers, and other devices
  CHECK(!di_device_name("ESS AudioDrive Playback (220)", DI_NAME_FMREC, out));
  CHECK(!di_device_name("ESS AudioDrive Audio 2 (220)", all, out));
  CHECK(!di_device_name("ESS AudioDrive Audio 1 (220)", DI_NAME_AUDIO1, out));
  CHECK(!di_device_name("ESS AudioDrive Record (220)", all, out));
  CHECK(!di_device_name("ESS AudioDrive Record (220)", DI_NAMES_ESS, out));
  CHECK(!di_device_name("ESS AudioDrive FM Digital (220)", DI_NAME_FMREC, out));
  CHECK(!di_device_name("Sound Blaster Playback", all, out));
  CHECK(!di_device_name("", all, out));
}

static void test_rebuilt_after(void) {
  char a[DI_PATH], why[DI_WHY];

  // ESS's drivers installed: an install puts the rebuilt ES1869.DRV in
  // place, unless only ESFM.DRV is next to the installer
  setup();
  CHECK(di_rebuilt_after(src, sys));
  CHECK(!di_rebuilt_after(0, sys));
  in_dir(a, src, "ES1869.DRV");
  remove(a);
  CHECK(!di_rebuilt_after(src, sys));
  // the rebuilt drivers installed: they stay, and a restore takes
  // ES1869.DRV back while ESS's is kept
  setup();
  CHECK_EQ(di_install(src, sys, 1, &ops, why), 3);
  restart();
  in_dir(a, src, "ES1869.DRV");
  remove(a);
  CHECK(di_rebuilt_after(src, sys));
  CHECK(!di_rebuilt_after(0, sys));
  in_dir(a, sys, "ES1869.ORG");
  remove(a);
  CHECK(di_rebuilt_after(0, sys));
}

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("usage: t_drvinst ROOT WORKDIR\n");
    return 2;
  }
  strcpy(root, argv[1]);
  in_dir(src, argv[2], "INSTALL");
  in_dir(sys, argv[2], "SYSTEM");
  mkdir(src, 0777);
  mkdir(sys, 0777);
  test_scan();
  test_install_and_restore();
  test_refusals();
  test_wininit_fails();
  test_some_drivers();
  test_vxd_kept_by_hand();
  test_device_names();
  test_rebuilt_after();
  CHECK(logged > 0);
  return CHECK_DONE("t_drvinst");
}
