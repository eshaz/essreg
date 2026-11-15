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
#define OPL3_NAME "Internal OPL3 FM Synthesis"
#define OPL3_DEV "OPL3 FM Synthesis (" // the devices' names (szPname)
#define ESFM_DEV "ESFM Synthesis ("
#define FIRST_NO 9001 // the OPL3 entry's number, past any card's
#define GONE 8        // the OPL3 device's entries removed at once

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

int mr_list(struct mr_entry *list, int max) {
  char name[64];
  DWORD top, i;
  int n = 0;

  if (reg_init() || reg.open(HKLM, KEY_MIDI, &top))
    return -1;
  for (i = 0; n < max && !reg.enum_key(top, i, name, sizeof(name)); i++)
    if (!get_entry(top, name, &list[n]))
      n++;
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

// 1 if the entry called opl has every value of the entry called ess, as it
// is there, but for the names and Port
static int same_values(DWORD top, const char *ess, const char *opl) {
  static char vname[64];
  static BYTE data[256], have[256];
  DWORD src, dst, i, nlen, size, type, htype, hsize;
  LONG r;
  int same = 1;

  if (reg.open(top, ess, &src))
    return 0;
  if (reg.open(top, opl, &dst)) {
    reg.close(src);
    return 0;
  }
  for (i = 0; same && i < 64; i++) {
    nlen = sizeof(vname);
    size = sizeof(data);
    r = reg.enum_value(src, i, vname, &nlen, 0, &type, data, &size);
    if (r == MORE_DATA)
      continue;
    if (r)
      break;
    if (!lstrcmpi(vname, "Port") || !lstrcmpi(vname, "FriendlyName") ||
        !lstrcmpi(vname, "Description"))
      continue;
    hsize = sizeof(have);
    same = !reg.query(dst, vname, 0, &htype, have, &hsize) && htype == type &&
           hsize == size && !memcmp(have, data, (unsigned)size);
  }
  reg.close(dst);
  reg.close(src);
  return same;
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

// ESS's entry (called from) copied to e's key, with Port 1, and its names
// with ESFM made OPL3 FM (e's name where it has none): 0, or -1 with
// nothing left of it
static int copy_entry(DWORD top, const char *from, const struct mr_entry *e) {
  static char vname[64], name[MR_NAME];
  static BYTE data[256];
  DWORD src, dst, sub, i, nlen, size, type, ptype = REG_DW, one = 1;
  LONG r;
  int err = 0, names = 0;

  if (reg.open(top, from, &src))
    return -1;
  if (reg.create(top, e->key, &dst)) {
    reg.close(src);
    return -1;
  }
  for (i = 0; i < 64; i++) {
    nlen = sizeof(vname);
    size = sizeof(data);
    r = reg.enum_value(src, i, vname, &nlen, 0, &type, data, &size);
    if (r == MORE_DATA)
      continue;
    if (r)
      break;
    if (!lstrcmpi(vname, "Port")) {
      ptype = type;
      continue;
    }
    if (type == REG_SZ &&
        (!lstrcmpi(vname, "FriendlyName") || !lstrcmpi(vname, "Description"))) {
      data[sizeof(data) - 1] = 0;
      opl3_name((char *)data, name);
      names++;
      if (reg.set(dst, vname, 0, REG_SZ, (const BYTE FAR *)name,
                  strlen(name) + 1))
        err = -1;
      continue;
    }
    if (reg.set(dst, vname, 0, type, data, size))
      err = -1;
  }
  if (!names && (reg.set(dst, "FriendlyName", 0, REG_SZ,
                         (const BYTE FAR *)e->name, strlen(e->name) + 1) ||
                 reg.set(dst, "Description", 0, REG_SZ,
                         (const BYTE FAR *)e->name, strlen(e->name) + 1)))
    err = -1;
  // Port as ESS's entry keeps it, a DWORD where it has none
  if (ptype == REG_SZ)
    r = reg.set(dst, "Port", 0, REG_SZ, (const BYTE FAR *)"1", 2);
  else
    r = reg.set(dst, "Port", 0, ptype == REG_BIN ? REG_BIN : REG_DW,
                (const BYTE FAR *)&one, 4);
  if (r)
    err = -1;
  // ESS's entry's Instruments key, empty: a synthesizer has no instruments
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
    del_tree(top, e->key, 0);
  return err;
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
  static char gone[GONE][MR_KEY];
  struct mr_entry cur, ess;
  char name[64], *p;
  DWORD top, key, i;
  int n = 0, have_ess = 0, keep = -1, err = 0, j;

  memset(e, 0, sizeof(*e));
  memset(&ess, 0, sizeof(ess));
  *stale = 0;
  if (reg_init() || reg.open(HKLM, KEY_MIDI, &top))
    return on ? MR_NO_ESFM : MR_KEEP;
  for (i = 0; !reg.enum_key(top, i, name, sizeof(name)); i++) {
    if (get_entry(top, name, &cur) || lstrcmpi(cur.driver, "esfm.drv"))
      continue;
    if (cur.port <= 0) {
      // ESS's device: Port 0, or none; the card's, not an earlier one's
      if (cur.active == 1 && !have_ess) {
        ess = cur;
        have_ess = 1;
      }
      continue;
    }
    if (n < GONE) {
      if (!n)
        *e = cur;
      strcpy(gone[n++], cur.key);
    }
  }
  if (on && !have_ess) {
    reg.close(top);
    return MR_NO_ESFM;
  }
  // an entry that is ESS's but for its names and Port stays
  for (j = 0; on && keep < 0 && j < n; j++)
    if (same_values(top, ess.key, gone[j]))
      keep = j;
  if (on && keep >= 0) {
    get_entry(top, gone[keep], e);
    *stale = n - 1;
    for (j = 0; set && j < n; j++)
      if (j != keep && del_tree(top, gone[j], 0))
        err = -1;
    reg.close(top);
    return err ? MR_FAIL : n > 1 ? MR_TIDY : MR_KEEP;
  }
  if (!on && !n) {
    reg.close(top);
    return MR_KEEP;
  }
  // the others go: an earlier card's copy, or the device is off
  *stale = n;
  for (j = 0; set && j < n; j++)
    if (del_tree(top, gone[j], 0))
      err = -1;
  if (set && reg.flush)
    reg.flush(top);
  if (!on) {
    if (set && have_ess)
      mapper_back(gone, n, e, &ess);
    reg.close(top);
    return err ? MR_FAIL : MR_REMOVE;
  }
  // ESS's key's driver part, and a number that no card's install takes
  strcpy(e->key, ess.key);
  p = strchr(e->key, '<');
  if (!p || p - e->key > MR_KEY - 7) {
    strcpy(e->key, "esfm.drv");
    p = e->key + strlen(e->key);
  }
  for (j = FIRST_NO; j < 10000; j++) {
    sprintf(p, "<%04d>", j);
    if (reg.open(top, e->key, &key))
      break;
    reg.close(key);
  }
  strcpy(e->driver, "esfm.drv");
  opl3_name(ess.name, e->name);
  e->port = 1;
  e->active = ess.active;
  e->devnode = ess.devnode;
  e->has_devnode = ess.has_devnode;
  if (set && !err)
    err = copy_entry(top, ess.key, e);
  reg.close(top);
  return err ? MR_FAIL : MR_ADD;
}
