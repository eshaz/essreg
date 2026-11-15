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

// ESS's entry (called from) copied to e's key, with e's name and Port 1:
// 0, or -1 with nothing left of it
static int copy_entry(DWORD top, const char *from, const struct mr_entry *e) {
  static char vname[64];
  static BYTE data[256];
  DWORD src, dst, sub, i, nlen, size, type, ptype = REG_DW, one = 1;
  LONG r;
  int err = 0;

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
    if (!lstrcmpi(vname, "Port"))
      ptype = type;
    else if (lstrcmpi(vname, "FriendlyName") &&
             lstrcmpi(vname, "Description") &&
             reg.set(dst, vname, 0, type, data, size))
      err = -1;
  }
  if (reg.set(dst, "FriendlyName", 0, REG_SZ, (const BYTE FAR *)e->name,
              strlen(e->name) + 1) ||
      reg.set(dst, "Description", 0, REG_SZ, (const BYTE FAR *)e->name,
              strlen(e->name) + 1))
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

// the MIDI mapper's CurrentInstrument where it names a removed entry,
// by key or by name: ESS's device in its place, the same way
static void mapper_back(char gone[][MR_KEY], int n, const struct mr_entry *opl3,
                        const struct mr_entry *ess) {
  static const struct {
    DWORD root;
    const char *key;
  } users[] = {{HKCU, KEY_MAP}, {HKU, ".DEFAULT\\" KEY_MAP}};
  char cur[MR_NAME];
  const char *to;
  DWORD key;
  unsigned u;
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
    if (to && to[0] &&
        !reg.set(key, "CurrentInstrument", 0, REG_SZ, (const BYTE FAR *)to,
                 strlen(to) + 1) &&
        reg.flush)
      reg.flush(key);
    reg.close(key);
  }
}

int mr_opl3(int on, int set, struct mr_entry *e) {
  static char gone[GONE][MR_KEY];
  struct mr_entry cur, ess;
  char name[64], *p;
  DWORD top, key, i;
  int n = 0, found = 0, have_ess = 0, err = 0, j;

  memset(e, 0, sizeof(*e));
  memset(&ess, 0, sizeof(ess));
  if (reg_init() || reg.open(HKLM, KEY_MIDI, &top))
    return on ? MR_NO_ESFM : MR_KEEP;
  for (i = 0; !reg.enum_key(top, i, name, sizeof(name)); i++) {
    if (get_entry(top, name, &cur) || lstrcmpi(cur.driver, "esfm.drv"))
      continue;
    if (cur.port <= 0) {
      // ESS's device: Port 0, or none
      if (!have_ess)
        ess = cur;
      have_ess = 1;
      continue;
    }
    if (!found)
      *e = cur;
    found = 1;
    if (n < GONE)
      strcpy(gone[n++], cur.key);
  }
  if (on) {
    if (found || !have_ess) {
      reg.close(top);
      return found ? MR_KEEP : MR_NO_ESFM;
    }
    // ESS's key's driver part, and the first free number
    strcpy(e->key, ess.key);
    p = strchr(e->key, '<');
    if (!p || p - e->key > MR_KEY - 7) {
      strcpy(e->key, "esfm.drv");
      p = e->key + strlen(e->key);
    }
    for (j = 1; j < 10000; j++) {
      sprintf(p, "<%04d>", j);
      if (reg.open(top, e->key, &key))
        break;
      reg.close(key);
    }
    strcpy(e->driver, "esfm.drv");
    opl3_name(ess.name, e->name);
    e->port = 1;
    e->active = ess.active;
    if (set)
      err = copy_entry(top, ess.key, e);
    reg.close(top);
    return err ? MR_FAIL : MR_ADD;
  }
  if (!found) {
    reg.close(top);
    return MR_KEEP;
  }
  if (set) {
    for (j = 0; j < n; j++)
      if (del_tree(top, gone[j], 0))
        err = -1;
    if (reg.flush)
      reg.flush(top);
    if (have_ess)
      mapper_back(gone, n, e, &ess);
  }
  reg.close(top);
  return err ? MR_FAIL : MR_REMOVE;
}
