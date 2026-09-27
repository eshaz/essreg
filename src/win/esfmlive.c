/*
 * Replaces the FM patch bank of the running ESFM.DRV (see
 * esfmlive.h and docs/ESFM_BANK.md).
 *
 * Notes:
 *
 * The driver's data segment holds a far pointer to its bank
 * at 0012h (offset, always 0) and 0014h (the GlobalAlloc
 * handle, used directly as a selector).
 *
 * Before touching anything, check that the driver file is the
 * build with the known loader (esfm_drv_inspect) and that its
 * data segment has the known layout.
 *
 * A larger bank grows the driver's own block with
 * GlobalReAlloc, so the driver still owns and frees it.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <conio.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <toolhelp.h>

#include "esfmbank.h"
#include "esfmlive.h"

#define DG_BANK_OFF 0x12
#define DG_BANK_SEL 0x14
#define DG_NAME1 0x47 // "Undefined", file name of the dormant RIFF loader
#define DG_NAME2 0x51 // "Undefined", what it is compared with
#define DG_DEVICES 0x3C // first device structure

// device structure (src/esfm/esfmdev.inc)
#define DEV_FM_PORT 0x00A
#define DEV_ACTIVE 0x014
#define DEV_OPEN 0x016
#define DEV_CLOCK 0x01C
#define DEV_CHAN_FLAGS 0x040
#define DEV_VOICES 0x070
#define DEV_FLAGS 0x30E
#define DEV_SIZE 0x311
#define VOICE_SIZE 0x21
#define FM_KEYON 0x240 // key-on registers, 0x250-0x253 for voices 16, 17

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

// handle of segment seg of a loaded module, from ToolHelp or else from the
// module database (ToolHelp's GlobalEntryModule is a stub in Wine)
// the module database is the NE header in memory, its 10-byte segment table
// entries end in the segment's handle
static HGLOBAL module_segment(HMODULE mod, unsigned seg) {
  GLOBALENTRY ge;
  u8 __far *ne;
  HGLOBAL h = 0;

  memset(&ge, 0, sizeof(ge));
  ge.dwSize = sizeof(ge);
  if (GlobalEntryModule(&ge, mod, seg) && ge.hBlock)
    return ge.hBlock;
  ne = (u8 __far *)GlobalLock((HGLOBAL)mod);
  if (!ne)
    return 0;
  if (ne[0] == 'N' && ne[1] == 'E' && seg >= 1 &&
      seg <= *(u16 __far *)(ne + 0x1C))
    h = (HGLOBAL) * (u16 __far *)(ne + *(u16 __far *)(ne + 0x22) +
                                   (seg - 1) * 10 + 8);
  GlobalUnlock((HGLOBAL)mod);
  return h;
}

static int open_live(struct live *lv, char *why) {
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

  lv->dgroup = module_segment(lv->mod, lv->drv.autodata);
  if (!lv->dgroup) {
    strcpy(why, "cannot find the driver's data segment");
    return -1;
  }
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

// the counters of the fixed driver (src/esfm/esfmfixd.asm) after the
// static data of DGROUP, 0 for ESS's driver
static const u8 __far *find_fix(const struct live *lv) {
  u32 dgsize = GlobalSize(lv->dgroup);
  u16 i;

  for (i = 0x1B2; i + 32 < 0x800 && i + 32 < dgsize; i++)
    if (!_fmemcmp(lv->dg + i, "ESFMFIX", 8))
      return lv->dg + i;
  return 0;
}

// version of the fixed driver, 2 and up plays SYSTEM.INI's bank file
static u16 fix_version(const struct live *lv) {
  const u8 __far *fix = find_fix(lv);

  return fix ? *(u16 __far *)(fix + 8) : 0;
}

// copy n bytes into the driver's bank, growing its block if needed
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
  // interrupts off so no MIDI callback sees half a patch
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
      // the fixed driver keeps playing the file, also after a restart
      if (fix_version(&lv) >= 2 &&
          WritePrivateProfileString(ESFM_INI_SECTION, ESFM_INI_KEY, path,
                                    ESFM_INI_FILE))
        strcat(text, ", ESFM.DRV plays it from the file now");
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
    // and the fixed driver stops playing a bank file
    if (fix_version(&lv) >= 2)
      WritePrivateProfileString(ESFM_INI_SECTION, ESFM_INI_KEY, 0,
                                ESFM_INI_FILE);
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

static u32 rd32(const u8 *p) {
  return p[0] | ((u16)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

// read an ESFM register in native mode, FM_Base+2/+3 select it and +1
// reads it
// interrupts are off so the driver cannot move the address in between, and
// since the driver always writes both address bytes it is not disturbed
static u8 fm_read(u16 port, u16 reg) {
  u8 v;

  _disable();
  outp(port + 2, reg & 0xFF);
  outp(port + 3, reg >> 8);
  v = (u8)inp(port + 1);
  _enable();
  return v;
}

int esfm_diag_read(struct esfm_diag *d, int read_chip) {
  static u8 dev[DEV_SIZE];
  struct live lv;
  u16 devoff, i;
  u32 clock;
  const u8 __far *fix;
  u32 dgsize;

  memset(d, 0, sizeof(*d));
  if (open_live(&lv, d->why) != 0)
    return -1;
  devoff = *(u16 __far *)(lv.dg + DG_DEVICES);
  dgsize = GlobalSize(lv.dgroup);
  if (!devoff || devoff + (u32)DEV_SIZE > dgsize) {
    close_live(&lv);
    strcpy(d->why, "the driver has no ES1869 device");
    return -1;
  }
  // copy with interrupts off, MIDI callbacks change it at interrupt time
  _disable();
  _fmemcpy(dev, lv.dg + devoff, DEV_SIZE);
  _enable();
  fix = find_fix(&lv);
  if (fix) {
    _disable();
    d->fixed = 1;
    d->queued = *(u32 __far *)(fix + 16);
    d->overflow = *(u32 __far *)(fix + 20);
    d->maxdepth = *(u16 __far *)(fix + 24);
    d->purged = *(u16 __far *)(fix + 26);
    d->version = *(u16 __far *)(fix + 8);
    _enable();
  }
  // version 2: the bank file (src/esfm/esfmfile.asm)
  if (fix && d->version >= 2 &&
      (u32)(FP_OFF(fix) - FP_OFF(lv.dg)) + 44 + 128 <= dgsize) {
    d->file_state = *(u16 __far *)(fix + 30);
    d->file_used = *(u16 __far *)(fix + 32);
    d->file_size = *(u16 __far *)(fix + 34);
    d->file_loads = *(u16 __far *)(fix + 36);
    d->file_checks = *(u16 __far *)(fix + 38);
    d->file_date = *(u16 __far *)(fix + 40);
    d->file_time = *(u16 __far *)(fix + 42);
    _fmemcpy(d->file, fix + 44, sizeof(d->file) - 1);
  }
  close_live(&lv);

  d->device = 1;
  d->fm_port = *(u16 *)(dev + DEV_FM_PORT);
  d->open = *(u16 *)(dev + DEV_OPEN) != 0;
  d->suspended = (dev[DEV_FLAGS] & 4) != 0;
  for (i = 0; i < 16; i++)
    if (dev[DEV_CHAN_FLAGS + i] & 1)
      d->pedal |= 1 << i;
  clock = rd32(dev + DEV_CLOCK);
  for (i = 0; i < ESFM_VOICES; i++) {
    const u8 *v = dev + DEV_VOICES + i * VOICE_SIZE;
    d->v[i].flags = v[0];
    d->v[i].age = clock - rd32(v + 1);
    d->v[i].channel = v[5];
    d->v[i].note = v[6];
    d->v[i].chip = 0xFF;
  }
  if (read_chip && d->open && *(u16 *)(dev + DEV_ACTIVE) && !d->suspended &&
      d->fm_port) {
    for (i = 0; i < 16; i++)
      d->v[i].chip = fm_read(d->fm_port, FM_KEYON + i) & 1;
    d->v[16].chip = (fm_read(d->fm_port, FM_KEYON + 16) |
                     fm_read(d->fm_port, FM_KEYON + 17)) & 1;
    d->v[17].chip = (fm_read(d->fm_port, FM_KEYON + 18) |
                     fm_read(d->fm_port, FM_KEYON + 19)) & 1;
    d->chip_read = 1;
  }
  return 0;
}

// the bank file of the fixed driver, as the last MODM_OPEN found it
static void file_text(const struct esfm_diag *d, const char *nl, char *buf,
                      unsigned size) {
  static char line[240];
  const char *plays = d->file_used ? "its last good version plays"
                                   : "the driver's own bank plays";

  switch (d->file_state) {
  case ESFM_FILE_NONE:
    sprintf(line, "Bank file: none, %s%s",
            d->file_used ? "the last file's bank still plays"
                         : "the driver's own bank plays",
            nl);
    break;
  case ESFM_FILE_LOADED:
    // DOS date and time: year-1980:7 month:4 day:5, hour:5 minute:6
    sprintf(line, "Bank file: %.110s, %u bytes, dated %u-%02u-%02u %02u:%02u%s",
            d->file, d->file_size, (d->file_date >> 9) + 1980,
            (d->file_date >> 5) & 15, d->file_date & 31, d->file_time >> 11,
            (d->file_time >> 5) & 63, nl);
    break;
  case ESFM_FILE_MISSING:
    sprintf(line, "Bank file: cannot read %.110s, %s%s", d->file, plays, nl);
    break;
  case ESFM_FILE_BAD:
    sprintf(line, "Bank file: %.110s is not a patch bank, %s%s", d->file,
            plays, nl);
    break;
  default:
    sprintf(line, "Bank file: not enough memory for %.110s, %s%s", d->file,
            plays, nl);
  }
  strncat(buf, line, size - strlen(buf) - 1);
  sprintf(line, "Read when a program opens the device, if its date or time "
                "changed: %u checks, %u load%s%s",
          d->file_checks, d->file_loads, d->file_loads == 1 ? "" : "s", nl);
  strncat(buf, line, size - strlen(buf) - 1);
}

static const char *note_name(u8 note, char *buf) {
  static const char names[] = "C C#D D#E F F#G G#A A#B ";
  buf[0] = names[(note % 12) * 2];
  buf[1] = names[(note % 12) * 2 + 1];
  sprintf(buf + (buf[1] == ' ' ? 1 : 2), "%d", note / 12 - 1);
  return buf;
}

void esfm_diag_text(const struct esfm_diag *d, int rc, const char *nl,
                    char *buf, unsigned size) {
  static char line[120];
  char nb[8];
  int i;

  buf[0] = 0;
  if (rc != 0) {
    sprintf(line, "Voices: %.90s%s", d->why, nl);
    strncat(buf, line, size - strlen(buf) - 1);
    return;
  }
  sprintf(line, "%s%s%s", d->open ? "A program has the MIDI device open"
                                  : "The MIDI device is closed",
          d->suspended ? " (suspended)" : "", nl);
  strncat(buf, line, size - strlen(buf) - 1);
  if (d->fixed)
    sprintf(line, "Fixed driver: %lu messages queued while busy, %lu "
                  "refused%s",
            (unsigned long)d->queued, (unsigned long)d->overflow, nl);
  else
    sprintf(line, "ESS driver: drops messages that come while it is "
                  "busy%s", nl);
  strncat(buf, line, size - strlen(buf) - 1);
  if (d->fixed && d->version >= 2)
    file_text(d, nl, buf, size);
  sprintf(line, "%sVoice  Channel  Note   State          Age  Chip%s", nl, nl);
  strncat(buf, line, size - strlen(buf) - 1);
  for (i = 0; i < ESFM_VOICES; i++) {
    const struct esfm_voice *v = &d->v[i];
    const char *state = "free";
    int on = v->flags & 1, n;
    if (on && (v->flags & 4))
      state = "held by pedal";
    else if (on)
      state = "playing";
    else if (v->flags & 2)
      state = "released";
    n = sprintf(line, "%5d  ", i + 1);
    if (v->flags)
      n += sprintf(line + n, "%7d  %-5s  ", v->channel + 1,
                   note_name(v->note, nb));
    else
      n += sprintf(line + n, "%7s  %-5s  ", "", "");
    n += sprintf(line + n, "%-13s  ", state);
    if (on)
      n += sprintf(line + n, "%4lu  ", (unsigned long)v->age);
    else
      n += sprintf(line + n, "%4s  ", "");
    if (v->chip == 0xFF)
      n += sprintf(line + n, "  -");
    else
      n += sprintf(line + n, "%s%s", v->chip ? " on" : "off",
                   v->chip && !on ? "  STUCK" : "");
    sprintf(line + n, "%s", nl);
    strncat(buf, line, size - strlen(buf) - 1);
  }
  if (d->pedal) {
    strcpy(line, "Sustain pedal down on channel");
    for (i = 0; i < 16; i++)
      if (d->pedal & (1 << i))
        sprintf(line + strlen(line), " %d", i + 1);
    strcat(line, nl);
    strncat(buf, line, size - strlen(buf) - 1);
  }
}
