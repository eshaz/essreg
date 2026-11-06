/*
 * Finds the data segment of the running ES1869.DRV, ESS's wave driver,
 * for wavestat (src/wavestat.c): what essctl's Device information page
 * shows about it.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <string.h>

#include "essctl.h"
#include "wavestat.h"

int wave_live(struct wavestat *ws) {
  HMODULE mod = GetModuleHandle("ES1869");
  HGLOBAL dgroup;
  u8 __far *ne;
  const u8 __far *dg;
  unsigned autodata = 0;
  int r;

  memset(ws, 0, sizeof(*ws));
  if (!mod)
    return 1;
  // the data segment's number, from the NE header
  ne = (u8 __far *)GlobalLock((HGLOBAL)mod);
  if (ne) {
    if (ne[0] == 'N' && ne[1] == 'E')
      autodata = *(u16 __far *)(ne + 0x0E);
    GlobalUnlock((HGLOBAL)mod);
  }
  dgroup = autodata ? module_segment(mod, autodata) : 0;
  if (!dgroup)
    return -1;
  dg = (const u8 __far *)GlobalLock(dgroup);
  if (!dg)
    return -1;
  r = wavestat_parse(dg, GlobalSize(dgroup), ws);
  GlobalUnlock(dgroup);
  return r;
}
