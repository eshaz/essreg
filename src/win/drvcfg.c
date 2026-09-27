/*
 * Reads and writes the settings of ESS's drivers in the registry (see
 * drvcfg.h).
 *
 * Notes:
 *
 * Windows 95's KERNEL has registry functions for 16-bit programs that
 * reach every key. They're looked up by ordinal, the way ES1869.DRV
 * imports them, so on Windows 3.1 the settings are just not found.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <string.h>

#include "drvcfg.h"

#define HKLM 0x80000002UL
#define KEY_MEDIA "System\\CurrentControlSet\\Services\\Class\\Media"
#define REG_BIN 3

typedef LONG(FAR PASCAL *REGENUMKEY)(DWORD, DWORD, LPSTR, DWORD);
typedef LONG(FAR PASCAL *REGOPENKEY)(DWORD, LPCSTR, DWORD FAR *);
typedef LONG(FAR PASCAL *REGCLOSEKEY)(DWORD);
typedef LONG(FAR PASCAL *REGQUERYVALUEEX)(DWORD, LPCSTR, DWORD FAR *,
                                          DWORD FAR *, BYTE FAR *, DWORD FAR *);
typedef LONG(FAR PASCAL *REGSETVALUEEX)(DWORD, LPCSTR, DWORD, DWORD,
                                        const BYTE FAR *, DWORD);

static struct {
  int tried;
  REGENUMKEY enum_key;
  REGOPENKEY open;
  REGCLOSEKEY close;
  REGQUERYVALUEEX query;
  REGSETVALUEEX set;
} reg;

// KERNEL's registry functions: RegEnumKey 216, RegOpenKey 217,
// RegCloseKey 220, RegQueryValueEx 225, RegSetValueEx 226
static int reg_init(void) {
  HMODULE k;

  if (!reg.tried) {
    reg.tried = 1;
    k = GetModuleHandle("KERNEL");
    reg.enum_key = (REGENUMKEY)GetProcAddress(k, MAKEINTRESOURCE(216));
    reg.open = (REGOPENKEY)GetProcAddress(k, MAKEINTRESOURCE(217));
    reg.close = (REGCLOSEKEY)GetProcAddress(k, MAKEINTRESOURCE(220));
    reg.query = (REGQUERYVALUEEX)GetProcAddress(k, MAKEINTRESOURCE(225));
    reg.set = (REGSETVALUEEX)GetProcAddress(k, MAKEINTRESOURCE(226));
  }
  return reg.enum_key && reg.open && reg.close && reg.query && reg.set ? 0 : -1;
}

// the Config key of the first Media device whose Driver is es1869.vxd
static int open_config(DWORD *key) {
  char name[16], path[80], driver[32];
  DWORD media, dev, type, size, i;
  int found = 0;

  if (reg_init() || reg.open(HKLM, KEY_MEDIA, &media))
    return -1;
  for (i = 0; !found && !reg.enum_key(media, i, name, sizeof(name)); i++) {
    if (reg.open(media, name, &dev))
      continue;
    size = sizeof(driver) - 1;
    memset(driver, 0, sizeof(driver));
    if (!reg.query(dev, "Driver", 0, &type, (BYTE FAR *)driver, &size) &&
        !lstrcmpi(driver, "es1869.vxd")) {
      strcpy(path, name);
      strcat(path, "\\Config");
      found = !reg.open(media, path, key);
    }
    reg.close(dev);
  }
  reg.close(media);
  return found ? 0 : -1;
}

int drvcfg_get(const char *name, DWORD *value) {
  DWORD key, type, size = sizeof(*value);
  long err;

  if (open_config(&key))
    return -1;
  // a shorter value only replaces the low bytes, as in ES1869.DRV
  *value = 0;
  err = reg.query(key, name, 0, &type, (BYTE FAR *)value, &size);
  reg.close(key);
  return err ? 0 : 1;
}

int drvcfg_set(const char *name, DWORD value) {
  DWORD key;
  long err;

  if (open_config(&key))
    return -1;
  err = reg.set(key, name, 0, REG_BIN, (const BYTE FAR *)&value, sizeof(value));
  reg.close(key);
  return err ? -1 : 0;
}
