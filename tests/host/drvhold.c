/*
 * drvhold is a 16-bit Windows helper for tests/test_wine.py.
 *
 * Usage:
 *   drvhold <driver> <outdir> <command line>
 *
 * It loads the MIDI driver and sends DRV_LOAD and DRV_ENABLE to its
 * DriverProc, as OpenDriver does (ESFM.DRV copies its bank into memory on
 * DRV_ENABLE), then DRVM_INIT to its modMessage, which gives the driver a
 * device structure as when Windows finds the ES1869, and MODM_OPEN. The
 * open fails without ES1869.VXD, but the fixed driver reads the bank file
 * named in SYSTEM.INI before it (src/esfm/esfmfile.asm). It writes the bank
 * the driver holds to <outdir>\BEFORE.BIN, runs the command, waits for it
 * to finish, writes the bank again to AFTER.BIN and closes the driver.
 * WinExec keeps the command in this Win16 process, so it sees the driver.
 * Progress goes to <outdir>\DRVHOLD.LOG.
 *
 * The bank is found the way ESFM.DRV finds it, from the far pointer at
 * 0012h of its data segment.
 *
 * A wave driver (ES1869.DRV, with a WODMESSAGE export) is only loaded: its
 * DRV_LOAD looks for ES1869.VXD. essctl then reads its data segment. Its
 * LibMain asks for the VDS version (INT 4Bh), which Wine doesn't have, so
 * drvhold answers it while the driver loads.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <dos.h>

#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

static char outdir[128];

static void note(const char *text) {
  char path[160];
  FILE *f;
  sprintf(path, "%s\\DRVHOLD.LOG", outdir);
  f = fopen(path, "a");
  if (f) {
    fprintf(f, "%s\n", text);
    fclose(f);
  }
}

// get the data segment (4) of ESFM.DRV from the module database: in the
// in-memory NE header, each segment table entry ends in its handle
static HGLOBAL esfm_dgroup(void) {
  HMODULE mod = GetModuleHandle("ESFM");
  BYTE FAR *ne;
  HGLOBAL h;

  if (!mod)
    return 0;
  ne = (BYTE FAR *)GlobalLock((HGLOBAL)mod);
  h = (HGLOBAL) * (WORD FAR *)(ne + *(WORD FAR *)(ne + 0x22) + 3 * 10 + 8);
  GlobalUnlock((HGLOBAL)mod);
  return h;
}

static void dump(const char *name) {
  char path[160], text[96];
  HGLOBAL dgroup = esfm_dgroup();
  BYTE FAR *dg;
  BYTE FAR *bank;
  HGLOBAL h;
  DWORD size;
  FILE *f;

  if (!dgroup) {
    note("no ESFM data segment");
    return;
  }
  dg = (BYTE FAR *)GlobalLock(dgroup);
  h = (HGLOBAL) * (WORD FAR *)(dg + 0x14);
  size = GlobalSize(h);
  bank = (BYTE FAR *)GlobalLock(h);
  sprintf(text, "%s: handle %04X, %lu bytes", name, h, size);
  note(text);
  sprintf(path, "%s\\%s", outdir, name);
  f = fopen(path, "wb");
  if (f && bank) {
    fwrite(bank, 1, (size_t)size, f);
    fclose(f);
  }
  if (bank)
    GlobalUnlock(h);
  GlobalUnlock(dgroup);
}

// VDS 2.00 to ES1869.DRV's LibMain, which refuses to load without it
static void __interrupt __far vds(union INTPACK r) {
  if (r.w.ax == 0x8102) {
    r.w.cx = 0x0200;
    r.w.dx = 0;
    r.w.flags &= ~INTR_CF;
  } else {
    r.w.flags |= INTR_CF;
  }
}

typedef LRESULT(FAR PASCAL *DRIVERPROC)(DWORD id, HDRVR drv, UINT msg,
                                        LPARAM p1, LPARAM p2);
typedef DWORD(FAR PASCAL *MODMESSAGE)(UINT id, UINT msg, DWORD user, DWORD p1,
                                      DWORD p2);

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
  char driver[128], command[300], text[400];
  HINSTANCE lib, child;
  DRIVERPROC proc;
  MODMESSAGE mod;
  LRESULT r1, r2;
  DWORD start;
  MSG msg;
  int wave;
  void(__interrupt __far * old4b)();

  (void)inst;
  (void)prev;
  (void)show;
  if (sscanf(cmd, "%127s %127s %299[^\n]", driver, outdir, command) != 3)
    return 3;
  // the command may arrive quoted as one argument
  if (command[0] == '"') {
    memmove(command, command + 1, strlen(command));
    if (strchr(command, '"'))
      *strchr(command, '"') = 0;
  }
  old4b = _dos_getvect(0x4B);
  _dos_setvect(0x4B, vds);
  lib = LoadLibrary(driver);
  _dos_setvect(0x4B, old4b);
  proc = (UINT)lib > 32 ? (DRIVERPROC)GetProcAddress(lib, "DriverProc") : 0;
  if (!proc) {
    sprintf(text, "LoadLibrary(%s): %04X, no DriverProc", driver, lib);
    note(text);
    return 1;
  }
  wave = GetProcAddress(lib, "WODMESSAGE") != 0;
  if (wave) {
    note("a wave driver: loaded only");
    goto run;
  }
  r1 = proc(1, (HDRVR)1, DRV_LOAD, 0, 0);
  r2 = proc(1, (HDRVR)1, DRV_ENABLE, 0, 0);
  sprintf(text, "DRV_LOAD %ld, DRV_ENABLE %ld", r1, r2);
  note(text);
  // DRVM_INIT makes the driver build its device structure for a devnode
  // (essctl shows its voices on the ESFM page and in /dump)
  mod = (MODMESSAGE)GetProcAddress(lib, "MODMESSAGE");
  if (mod) {
    static BYTE desc[32]; // MIDIOPENDESC, not used before the open fails
    DWORD user = 0;

    sprintf(text, "DRVM_INIT %lu", mod(0, 0x64, 0, 0, 0x1234));
    note(text);
    sprintf(text, "MODM_OPEN %lu",
            mod(0, 3, (DWORD)(LPVOID)&user, (DWORD)(LPVOID)desc, 0));
    note(text);
  }
  dump("BEFORE.BIN");
run:
  child = WinExec(command, SW_SHOWNORMAL);
  sprintf(text, "WinExec(%.300s): %04X", command, child);
  note(text);
  // wait for the child to exit (at most 30 s)
  start = GetTickCount();
  while ((UINT)child > 32 && GetModuleUsage(child) > 0 &&
         GetTickCount() - start < 30000) {
    if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      Yield();
    }
  }
  if (!wave) {
    dump("AFTER.BIN");
    proc(1, (HDRVR)1, DRV_DISABLE, 0, 0);
    proc(1, (HDRVR)1, DRV_FREE, 0, 0);
  }
  FreeLibrary(lib);
  note("done");
  return 0;
}
