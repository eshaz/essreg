/*
 * esfmlive.c -- replace the FM patch bank of the running ESFM.DRV (see
 * esfmlive.h and docs/ESFM_BANK.md).
 *
 * The driver's data segment holds a far pointer to its bank at 0012h
 * (offset, always 0) and 0014h (the GlobalAlloc handle, used directly as a
 * selector).  Before touching anything, the driver's file is checked to be
 * the build whose loader is known (esfm_drv_inspect) and the data segment
 * to have the known layout.  A larger bank grows the driver's own block
 * with GlobalReAlloc, so it stays owned by the driver and is freed by it.
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <toolhelp.h>

#include "esfmbank.h"
#include "esfmlive.h"

#define DG_BANK_OFF 0x12
#define DG_BANK_SEL 0x14
#define DG_NAME1 0x47 /* "Undefined": file name of the dormant RIFF loader */
#define DG_NAME2 0x51 /* "Undefined": what it is compared with */

char esfm_last_bank[144];

struct live {
  HMODULE mod;
  HGLOBAL dgroup;
  u8 __far *dg;
  struct esfm_drv drv;
  char path[144];
};

static HGLOBAL bank_handle(const struct live *lv) {
  return (HGLOBAL)*(u16 __far *)(lv->dg + DG_BANK_SEL);
}

static int open_live(struct live *lv, char *why) {
  GLOBALENTRY ge;
  FILE *f;

  memset(lv, 0, sizeof(*lv));
  lv->mod = GetModuleHandle("ESFM");
  if (!lv->mod) {
    strcpy(why, "ESFM.DRV is not loaded");
    return -1;
  }
  GetModuleFileName(lv->mod, lv->path, sizeof(lv->path));
  f = fopen(lv->path, "rb");
  if (!f) {
    strcpy(why, "cannot read the driver file");
    return -1;
  }
  if (esfm_drv_inspect(f, &lv->drv) != 0) {
    fclose(f);
    strcpy(why, lv->drv.why);
    return -1;
  }
  fclose(f);

  memset(&ge, 0, sizeof(ge));
  ge.dwSize = sizeof(ge);
  if (!GlobalEntryModule(&ge, lv->mod, lv->drv.autodata) || !ge.hBlock) {
    strcpy(why, "cannot find the driver's data segment");
    return -1;
  }
  lv->dgroup = ge.hBlock;
  lv->dg = (u8 __far *)GlobalLock(lv->dgroup);
  if (!lv->dg) {
    strcpy(why, "cannot lock the driver's data segment");
    return -1;
  }
  if (_fmemcmp(lv->dg + DG_NAME1, "Undefined", 10) ||
      _fmemcmp(lv->dg + DG_NAME2, "Undefined", 10)) {
    GlobalUnlock(lv->dgroup);
    strcpy(why, "the driver's data segment is not the known layout");
    return -1;
  }
  if (*(u16 __far *)(lv->dg + DG_BANK_OFF) != 0 || !bank_handle(lv)) {
    GlobalUnlock(lv->dgroup);
    strcpy(why, "the driver is loaded but not enabled (no bank in memory)");
    return -1;
  }
  return 0;
}

static void close_live(struct live *lv) { GlobalUnlock(lv->dgroup); }

/* copy n bytes into the driver's bank, growing its block if needed */
static int put_bank(struct live *lv, const u8 __far *src, u16 n, char *why) {
  HGLOBAL h = bank_handle(lv), h2;
  u8 __far *dst;

  if (GlobalSize(h) < n) {
    h2 = GlobalReAlloc(h, n, GMEM_MOVEABLE | GMEM_ZEROINIT);
    if (!h2) {
      strcpy(why, "not enough memory for the bank");
      return -1;
    }
    if (h2 != h) {
      _disable();
      *(u16 __far *)(lv->dg + DG_BANK_SEL) = (u16)h2;
      _enable();
      h = h2;
    }
  }
  dst = (u8 __far *)GlobalLock(h);
  if (!dst) {
    strcpy(why, "cannot lock the bank");
    return -1;
  }
  /* no MIDI callback may see half a patch */
  _disable();
  _fmemcpy(dst, src, n);
  _enable();
  GlobalUnlock(h);
  return 0;
}

void esfm_get_status(struct esfm_status *st) {
  struct live lv;

  memset(st, 0, sizeof(*st));
  if (!GetModuleHandle("ESFM"))
    return;
  st->loaded = 1;
  if (open_live(&lv, st->why) != 0) {
    GetModuleFileName(GetModuleHandle("ESFM"), st->path, sizeof(st->path));
    return;
  }
  strcpy(st->path, lv.path);
  st->known = 1;
  st->bank_size = GlobalSize(bank_handle(&lv));
  close_live(&lv);
}

static void message(char *msg, unsigned size, const char *text) {
  strncpy(msg, text, size - 1);
  msg[size - 1] = 0;
}

int esfm_live_load(const char *path, char *msg, unsigned size) {
  struct bank_info info;
  struct live lv;
  char why[96], text[240];
  u8 *file;
  long len;
  u32 off, blen;
  FILE *f;
  int rc = -1;

  f = fopen(path, "rb");
  if (!f) {
    sprintf(text, "Cannot open %.120s", path);
    message(msg, size, text);
    return -1;
  }
  fseek(f, 0, SEEK_END);
  len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0 || len > BANK_MAX + 64L) {
    fclose(f);
    sprintf(text, "%.120s is not a patch bank", path);
    message(msg, size, text);
    return -1;
  }
  file = (u8 *)malloc((size_t)len);
  if (!file || fread(file, 1, (size_t)len, f) != (size_t)len) {
    fclose(f);
    free(file);
    message(msg, size, "Cannot read the bank file");
    return -1;
  }
  fclose(f);

  if (bank_unwrap(file, (u32)len, &off, &blen) != 0) {
    strcpy(why, "RIFF file without an \"fm4 \" chunk");
  } else if (bank_check(file + off, blen, &info) != 0) {
    strcpy(why, info.why);
  } else if (open_live(&lv, why) == 0) {
    if (put_bank(&lv, file + off, info.size, why) == 0) {
      strncpy(esfm_last_bank, path, sizeof(esfm_last_bank) - 1);
      esfm_last_bank[sizeof(esfm_last_bank) - 1] = 0;
      sprintf(text, "ESFM bank loaded: %u patches, %u bytes", info.patches,
              info.size);
      message(msg, size, text);
      rc = 0;
    }
    close_live(&lv);
  }
  if (rc) {
    sprintf(text, "Cannot load %.100s: %s", path, why);
    message(msg, size, text);
  }
  free(file);
  return rc;
}

int esfm_live_restore(char *msg, unsigned size) {
  struct live lv;
  char why[96], text[160];
  HRSRC res;
  HGLOBAL mem;
  u8 __far *src;
  int rc = -1;

  if (open_live(&lv, why) != 0) {
    sprintf(text, "Cannot restore the bank: %s", why);
    message(msg, size, text);
    return -1;
  }
  res = FindResource(lv.mod, MAKEINTRESOURCE(BANK_RES_ID),
                     MAKEINTRESOURCE(BANK_RES_TYPE));
  mem = res ? LoadResource(lv.mod, res) : 0;
  src = mem ? (u8 __far *)LockResource(mem) : 0;
  if (!src) {
    strcpy(why, "cannot load the driver's bank resource");
  } else if (put_bank(&lv, src, lv.drv.bank_size, why) == 0) {
    esfm_last_bank[0] = 0;
    message(msg, size, "ESFM bank restored from the driver");
    rc = 0;
  }
  if (src)
    UnlockResource(mem);
  if (mem)
    FreeResource(mem);
  close_live(&lv);
  if (rc) {
    sprintf(text, "Cannot restore the bank: %s", why);
    message(msg, size, text);
  }
  return rc;
}
