/*
 * The MIDI output devices of Windows' Multimedia control panel (see
 * midireg.h).
 *
 * Notes:
 *
 * The registry functions are KERNEL's, by ordinal, as src/win/drvcfg.c
 * finds them: Windows 3.1 has none, and then there's no list.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "midireg.h"

#define HKCU 0x80000001UL
#define HKLM 0x80000002UL
#define HKU 0x80000003UL
#define REG_SZ 1
#define REG_BIN 3
#define REG_DW 4
#define MORE_DATA 234L
#define KEY_MIDI "System\\CurrentControlSet\\Control\\MediaResources\\midi"
#define KEY_MAP                                                                \
  "Software\\Microsoft\\Windows\\CurrentVersion\\Multimedia\\MIDIMap"
#define KEY_CLASS "System\\CurrentControlSet\\Services\\Class\\"
#define KEY_DRV "\\Drivers\\midi"
#define OPL3_NAME "Internal OPL3 FM Synthesis"
#define OPL3_DEV "OPL3 FM Synthesis (" // the devices' names (szPname)
#define ESFM_DEV "ESFM Synthesis ("
#define GONE 8 // the OPL3 device's entries removed at once

typedef LONG(FAR PASCAL *REGENUMKEY)(DWORD, DWORD, LPSTR, DWORD);
typedef LONG(FAR PASCAL *REGOPENKEY)(DWORD, LPCSTR, DWORD FAR *);
typedef LONG(FAR PASCAL *REGDELETEKEY)(DWORD, LPCSTR);
typedef LONG(FAR PASCAL *REGCLOSEKEY)(DWORD);
typedef LONG(FAR PASCAL *REGENUMVALUE)(DWORD, DWORD, LPSTR, DWORD FAR *,
                                       DWORD FAR *, DWORD FAR *, BYTE FAR *,
                                       DWORD FAR *);
typedef LONG(FAR PASCAL *REGQUERYVALUEEX)(DWORD, LPCSTR, DWORD FAR *,
                                          DWORD FAR *, BYTE FAR *, DWORD FAR *);
typedef LONG(FAR PASCAL *REGSETVALUEEX)(DWORD, LPCSTR, DWORD, DWORD,
                                        const BYTE FAR *, DWORD);
typedef LONG(FAR PASCAL *REGFLUSHKEY)(DWORD);

static struct {
  int tried;
  REGENUMKEY enum_key;
  REGOPENKEY open, create;
  REGDELETEKEY del;
  REGCLOSEKEY close;
  REGENUMVALUE enum_value;
  REGQUERYVALUEEX query;
  REGSETVALUEEX set;
  REGFLUSHKEY flush;
} reg;

// RegEnumKey 216, RegOpenKey 217, RegCreateKey 218, RegDeleteKey 219,
// RegCloseKey 220, RegEnumValue 223, RegQueryValueEx 225, RegSetValueEx 226
// and RegFlushKey 227
static int reg_init(void) {
  HMODULE k;

  if (!reg.tried) {
    reg.tried = 1;
    k = GetModuleHandle("KERNEL");
    reg.enum_key = (REGENUMKEY)GetProcAddress(k, MAKEINTRESOURCE(216));
    reg.open = (REGOPENKEY)GetProcAddress(k, MAKEINTRESOURCE(217));
    reg.create = (REGOPENKEY)GetProcAddress(k, MAKEINTRESOURCE(218));
    reg.del = (REGDELETEKEY)GetProcAddress(k, MAKEINTRESOURCE(219));
    reg.close = (REGCLOSEKEY)GetProcAddress(k, MAKEINTRESOURCE(220));
    reg.enum_value = (REGENUMVALUE)GetProcAddress(k, MAKEINTRESOURCE(223));
    reg.query = (REGQUERYVALUEEX)GetProcAddress(k, MAKEINTRESOURCE(225));
    reg.set = (REGSETVALUEEX)GetProcAddress(k, MAKEINTRESOURCE(226));
    reg.flush = (REGFLUSHKEY)GetProcAddress(k, MAKEINTRESOURCE(227));
  }
  return reg.enum_key && reg.open && reg.create && reg.del && reg.close &&
                 reg.enum_value && reg.query && reg.set
             ? 0
             : -1;
}

// a string value into out (n bytes), "" without one
static void get_text(DWORD key, const char *name, char *out, unsigned n) {
  static char b[128];
  DWORD type, size = sizeof(b) - 1;

  memset(b, 0, sizeof(b));
  out[0] = 0;
  if (!reg.query(key, name, 0, &type, (BYTE FAR *)b, &size) && type == REG_SZ) {
    strncpy(out, b, n - 1);
    out[n - 1] = 0;
  }
}

// a number, kept as a string, a DWORD or 4 bytes; -1 without one
static long get_num(DWORD key, const char *name) {
  BYTE b[16];
  DWORD type, size = sizeof(b) - 1;

  memset(b, 0, sizeof(b));
  if (reg.query(key, name, 0, &type, b, &size))
    return -1;
  if (type == REG_SZ)
    return atol((char *)b);
  return b[0] | ((long)b[1] << 8) | ((long)b[2] << 16) |
         ((long)(b[3] & 0x7F) << 24);
}

// a binary or DWORD value's first 4 bytes, and 1, or 0 without one
static int get_raw(DWORD key, const char *name, unsigned long *out) {
  BYTE b[16];
  DWORD type, size = sizeof(b);

  memset(b, 0, sizeof(b));
  *out = 0;
  if (reg.query(key, name, 0, &type, b, &size) || type == REG_SZ)
    return 0;
  *out = b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16) |
         ((unsigned long)b[3] << 24);
  return 1;
}

// the entry called name of the list: 0, or -1 if it can't be read
static int get_entry(DWORD top, const char *name, struct mr_entry *e) {
  DWORD key;

  memset(e, 0, sizeof(*e));
  if (strlen(name) >= sizeof(e->key) || reg.open(top, name, &key))
    return -1;
  strcpy(e->key, name);
  get_text(key, "Driver", e->driver, sizeof(e->driver));
  get_text(key, "FriendlyName", e->name, sizeof(e->name));
  if (!e->name[0])
    get_text(key, "Description", e->name, sizeof(e->name));
  e->port = get_num(key, "Port");
  e->active = get_num(key, "Active");
  e->has_devnode = get_raw(key, "DevNode", &e->devnode);
  reg.close(key);
  return 0;
}

// the card's software key of the entry e, under HKLM, into e->soft: its
// SoftwareKey where that names a key of the MEDIA class, else the MEDIA key
// of the number in the entry's name, esfm.drv<0018>
static void soft_key(DWORD top, struct mr_entry *e) {
  static char sk[96];
  const char *p;
  DWORD key;
  unsigned n;

  e->soft[0] = 0;
  sk[0] = 0;
  if (!reg.open(top, e->key, &key)) {
    get_text(key, "SoftwareKey", sk, sizeof(sk));
    reg.close(key);
  }
  for (p = sk; *p && strnicmp(p, "MEDIA\\", 6); p++)
    ;
  if (*p && strlen(KEY_CLASS) + strlen(p) < MR_SOFT) {
    strcpy(e->soft, KEY_CLASS);
    strcat(e->soft, p);
    return;
  }
  p = strchr(e->key, '<');
  n = p ? (unsigned)strcspn(p + 1, ">") : 0;
  if (n && n < 9 && p[1 + n] == '>' && strlen(KEY_CLASS) + 6 + n < MR_SOFT) {
    strcpy(e->soft, KEY_CLASS "MEDIA\\");
    strncat(e->soft, p + 1, n);
  }
}

int mr_list(struct mr_entry *list, int max) {
  char name[64];
  DWORD top, i;
  int n = 0;

  if (reg_init() || reg.open(HKLM, KEY_MIDI, &top))
    return -1;
  for (i = 0; n < max && !reg.enum_key(top, i, name, sizeof(name)); i++)
    if (!get_entry(top, name, &list[n])) {
      soft_key(top, &list[n]);
      n++;
    }
  reg.close(top);
  return n;
}

void mr_mapper(char *out) {
  DWORD key;

  out[0] = 0;
  if (!reg_init() && !reg.open(HKCU, KEY_MAP, &key)) {
    get_text(key, "CurrentInstrument", out, MR_NAME);
    reg.close(key);
  }
}

// ESS's device's entry on the card that is there: Driver esfm.drv, Port 0
// or none, Active 1. 0, or -1 without one
static int find_ess(DWORD top, struct mr_entry *e) {
  char name[64];
  DWORD i;

  for (i = 0; !reg.enum_key(top, i, name, sizeof(name)); i++)
    if (!get_entry(top, name, e) && !lstrcmpi(e->driver, "esfm.drv") &&
        e->port <= 0 && e->active == 1) {
      soft_key(top, e);
      return 0;
    }
  memset(e, 0, sizeof(*e));
  return -1;
}

int mr_ess(struct mr_entry *e) {
  DWORD top;
  int r;

  memset(e, 0, sizeof(*e));
  if (reg_init() || reg.open(HKLM, KEY_MIDI, &top))
    return -1;
  r = find_ess(top, e);
  reg.close(top);
  return r;
}

int mr_drivers(const char *soft, char *out, unsigned size) {
  static char path[MR_SOFT + 16];
  char name[32];
  DWORD key, i;
  int n = 0;

  out[0] = 0;
  if (reg_init() || !soft[0])
    return -1;
  strcpy(path, soft);
  strcat(path, KEY_DRV);
  if (reg.open(HKLM, path, &key))
    return -1;
  for (i = 0; !reg.enum_key(key, i, name, sizeof(name)); i++) {
    if (strlen(out) + strlen(name) + 3 >= size)
      break;
    if (n++)
      strcat(out, ", ");
    strcat(out, name);
  }
  reg.close(key);
  return n;
}

// the OPL3 device's name from ESS's device's: ESFM becomes OPL3 FM
static void opl3_name(const char *ess, char *out) {
  const char *p = strstr(ess, "ESFM");

  if (!p || strlen(ess) + 3 >= MR_NAME) {
    strcpy(out, OPL3_NAME);
    return;
  }
  memcpy(out, ess, (unsigned)(p - ess));
  strcpy(out + (p - ess), "OPL3 FM");
  strcat(out, p + 4);
}

// the key called name under parent, with its keys: 0 or -1
static int del_tree(DWORD parent, const char *name, int depth) {
  char sub[64];
  DWORD key;

  if (!reg.open(parent, name, &key)) {
    while (depth < 4 && !reg.enum_key(key, 0, sub, sizeof(sub)))
      if (del_tree(key, sub, depth + 1))
        break;
    reg.close(key);
  }
  return reg.del(parent, name) ? -1 : 0;
}

// the values of the key src into the key dst: the names (Description and
// FriendlyName) with ESFM made OPL3 FM, Driver as driver, and without a
// Port. 0, or -1 if one of them can't be written
static int copy_values(DWORD src, DWORD dst, const char *driver) {
  static char vname[64], name[MR_NAME];
  static BYTE data[256];
  DWORD i, nlen, size, type;
  LONG r;
  int err = 0;

  for (i = 0; i < 64; i++) {
    nlen = sizeof(vname);
    size = sizeof(data);
    r = reg.enum_value(src, i, vname, &nlen, 0, &type, data, &size);
    if (r == MORE_DATA)
      continue;
    if (r)
      break;
    if (!lstrcmpi(vname, "Port") || !lstrcmpi(vname, "Driver"))
      continue;
    if (type == REG_SZ &&
        (!lstrcmpi(vname, "FriendlyName") || !lstrcmpi(vname, "Description"))) {
      data[sizeof(data) - 1] = 0;
      opl3_name((char *)data, name);
      if (reg.set(dst, vname, 0, REG_SZ, (const BYTE FAR *)name,
                  strlen(name) + 1))
        err = -1;
      continue;
    }
    if (reg.set(dst, vname, 0, type, data, size))
      err = -1;
  }
  if (reg.set(dst, "Driver", 0, REG_SZ, (const BYTE FAR *)driver,
              strlen(driver) + 1))
    err = -1;
  return err;
}

// a key of HKLM made from the key from, under the name to (the parent
// there already): copy_values, with the name where from has none, and its
// empty Instruments key if from has one. 0, or -1 with nothing left of it
static int make_key(DWORD fromtop, const char *from, DWORD totop,
                    const char *to, const char *name) {
  DWORD src, dst, sub;
  BYTE b[4];
  DWORD type, size = sizeof(b);
  int err;

  if (reg.open(fromtop, from, &src))
    return -1;
  if (reg.create(totop, to, &dst)) {
    reg.close(src);
    return -1;
  }
  err = copy_values(src, dst, MR_OPL3_DRIVER);
  if (reg.query(dst, "Description", 0, &type, b, &size) == 2L &&
      reg.set(dst, "Description", 0, REG_SZ, (const BYTE FAR *)name,
              strlen(name) + 1))
    err = -1;
  // a synthesizer has no instruments
  if (!reg.open(src, "Instruments", &sub)) {
    reg.close(sub);
    if (reg.create(dst, "Instruments", &sub))
      err = -1;
    else
      reg.close(sub);
  }
  // to the disk now: Windows writes the registry late
  if (reg.flush)
    reg.flush(dst);
  reg.close(dst);
  reg.close(src);
  if (err)
    del_tree(totop, to, 0);
  return err;
}

// 1 if the card's software key soft has ESFMOPL3.DRV among its MIDI
// drivers, which= 1 for ESFM.DRV's key instead
static int has_driver(const char *soft, int which) {
  static char path[MR_SOFT + 32];
  char drv[16];
  DWORD key;

  if (!soft[0])
    return 0;
  strcpy(path, soft);
  strcat(path, KEY_DRV "\\");
  strcat(path, which ? "esfm.drv" : MR_OPL3_DRIVER);
  if (reg.open(HKLM, path, &key))
    return 0;
  get_text(key, "Driver", drv, sizeof(drv));
  reg.close(key);
  return which || !lstrcmpi(drv, MR_OPL3_DRIVER);
}

// ESFMOPL3.DRV out of the MIDI drivers of the card's software key soft: 0
// if it isn't there or is gone, -1 if it stays
static int drop_driver(const char *soft) {
  static char path[MR_SOFT + 16];
  DWORD key;
  int r;

  if (!soft[0])
    return 0;
  strcpy(path, soft);
  strcat(path, KEY_DRV);
  if (reg.open(HKLM, path, &key))
    return 0;
  r = 0;
  if (has_driver(soft, 0))
    r = del_tree(key, MR_OPL3_DRIVER, 0);
  if (reg.flush)
    reg.flush(key);
  reg.close(key);
  return r;
}

// ESFMOPL3.DRV among the MIDI drivers of the card's software key soft,
// made from ESS's key for ESFM.DRV: 0 or -1
static int add_driver(const char *soft) {
  static char from[MR_SOFT + 32], to[MR_SOFT + 32];

  strcpy(from, soft);
  strcat(from, KEY_DRV "\\esfm.drv");
  strcpy(to, soft);
  strcat(to, KEY_DRV "\\" MR_OPL3_DRIVER);
  return make_key(HKLM, from, HKLM, to, OPL3_NAME);
}

// the MIDI mapper's CurrentInstrument where it names a removed entry, by
// key, by its name in the list or by the device's own name, as Windows 98
// keeps it: ESS's device in its place, the same way
static void mapper_back(char gone[][MR_KEY], int n, const struct mr_entry *opl3,
                        const struct mr_entry *ess) {
  static const struct {
    DWORD root;
    const char *key;
  } users[] = {{HKCU, KEY_MAP}, {HKU, ".DEFAULT\\" KEY_MAP}};
  char cur[MR_NAME], dev[MR_NAME];
  const char *to;
  DWORD key;
  unsigned u, k = strlen(OPL3_DEV);
  int j;

  for (u = 0; u < sizeof(users) / sizeof(users[0]); u++) {
    if (reg.open(users[u].root, users[u].key, &key))
      continue;
    get_text(key, "CurrentInstrument", cur, sizeof(cur));
    to = 0;
    for (j = 0; j < n; j++)
      if (!lstrcmpi(cur, gone[j]))
        to = ess->key;
    if (cur[0] && !lstrcmpi(cur, opl3->name))
      to = ess->name;
    if (!strnicmp(cur, OPL3_DEV, k) &&
        strlen(cur) - k + strlen(ESFM_DEV) < sizeof(dev)) {
      strcpy(dev, ESFM_DEV);
      strcat(dev, cur + k);
      to = dev;
    }
    if (to && to[0] &&
        !reg.set(key, "CurrentInstrument", 0, REG_SZ, (const BYTE FAR *)to,
                 strlen(to) + 1) &&
        reg.flush)
      reg.flush(key);
    reg.close(key);
  }
}

int mr_opl3(int on, int set, struct mr_entry *e, int *stale) {
  static char gone[GONE][MR_KEY], soft[GONE][MR_SOFT];
  static char want[MR_KEY], name[64];
  struct mr_entry cur, ess;
  DWORD top, i;
  int n = 0, have_ess, keep = -1, have_drv, err = 0, j;
  char *p;

  memset(e, 0, sizeof(*e));
  *stale = 0;
  if (reg_init() || reg.open(HKLM, KEY_MIDI, &top))
    return on ? MR_NO_ESFM : MR_KEEP;
  have_ess = !find_ess(top, &ess) && has_driver(ess.soft, 1);
  // the entry the card's ESFMOPL3.DRV has: esfmopl3.drv<0018>
  want[0] = 0;
  p = have_ess ? strchr(ess.key, '<') : 0;
  if (p && strlen(MR_OPL3_DRIVER) + strlen(p) < sizeof(want)) {
    strcpy(want, MR_OPL3_DRIVER);
    strcat(want, p);
  }
  if (on && !want[0]) {
    reg.close(top);
    return MR_NO_ESFM;
  }
  // the OPL3 device's entries: ESFMOPL3.DRV's, and an earlier essinst's
  // copies of ESS's entry with a Port above 0
  for (i = 0; !reg.enum_key(top, i, name, sizeof(name)); i++) {
    if (get_entry(top, name, &cur) ||
        (lstrcmpi(cur.driver, MR_OPL3_DRIVER) &&
         (lstrcmpi(cur.driver, "esfm.drv") || cur.port <= 0)))
      continue;
    soft_key(top, &cur);
    if (on && keep < 0 && !lstrcmpi(cur.key, want) &&
        !lstrcmpi(cur.driver, MR_OPL3_DRIVER)) {
      keep = 1;
      *e = cur;
      continue;
    }
    if (n < GONE) {
      if (!n && !on)
        *e = cur;
      strcpy(gone[n], cur.key);
      // a copy of ESS's entry has no driver of its own to remove
      strcpy(soft[n], lstrcmpi(cur.driver, MR_OPL3_DRIVER) ? "" : cur.soft);
      n++;
    }
  }
  have_drv = have_ess && has_driver(ess.soft, 0);
  *stale = n;
  if (on && keep > 0 && have_drv && !n) {
    reg.close(top);
    return MR_KEEP;
  }
  if (!on && !n && !have_drv) {
    reg.close(top);
    return MR_KEEP;
  }
  // what goes: the stale entries and their drivers, or all of it
  for (j = 0; set && j < n; j++)
    if (del_tree(top, gone[j], 0) || drop_driver(soft[j]))
      err = -1;
  if (!on) {
    if (set && have_drv && drop_driver(ess.soft))
      err = -1;
    if (!n) {
      strcpy(e->key, want);
      opl3_name(ess.name, e->name);
    }
    if (set && reg.flush)
      reg.flush(top);
    if (set && have_ess)
      mapper_back(gone, n, e, &ess);
    reg.close(top);
    return err ? MR_FAIL : MR_REMOVE;
  }
  if (keep > 0 && have_drv) {
    if (set && reg.flush)
      reg.flush(top);
    reg.close(top);
    return err ? MR_FAIL : MR_TIDY;
  }
  // the card's ESFMOPL3.DRV and its entry, as ESS's
  if (keep < 0) {
    strcpy(e->key, want);
    strcpy(e->driver, MR_OPL3_DRIVER);
    opl3_name(ess.name, e->name);
    e->port = -1;
    e->active = ess.active;
    e->devnode = ess.devnode;
    e->has_devnode = ess.has_devnode;
    strcpy(e->soft, ess.soft);
  }
  if (set && !err && !have_drv && add_driver(ess.soft))
    err = -1;
  if (set && !err && keep < 0 && make_key(top, ess.key, top, want, e->name))
    err = -1;
  if (set && reg.flush)
    reg.flush(top);
  reg.close(top);
  return err ? MR_FAIL : MR_ADD;
}
