/*
 * essinst puts the rebuilt ES1869.VXD, ES1869.DRV and ESFM.DRV in place
 * of ESS's drivers and restarts Windows, which moves them into place
 * before it loads any driver.
 *
 * Usage:
 *   `essinst [/restore] [/y] [/norestart]`
 *
 * Notes:
 *
 * It installs the rebuilt drivers that are in its own folder, after a
 * check of the installed ones (src/drvinst.c), and keeps ESS's drivers as
 * ES1869VX.ORG, ES1869.ORG and ESFM.ORG in the Windows SYSTEM folder.
 * /restore puts those back the same way. /y asks nothing and shows no
 * box, for a batch file, and /norestart leaves the restart to the user.
 * Each step goes to ESSINST.LOG in the Windows folder.
 *
 * The rebuilt ES1869.DRV names the wave devices by DAC, and Windows keeps
 * its preferred playback and recording devices by name, so essinst gives
 * those the new names too (and ESS's back with /restore), for the current
 * user and for new ones.
 *
 * The rebuilt ESFM.DRV has a second MIDI device, the OPL3 device, which
 * ESS's INF doesn't know. Windows lists a card's MIDI drivers, not their
 * devices, so essinst copies ESFMOPL3.DRV, the OPL3 device's own driver,
 * into the SYSTEM folder, adds it to the card's MIDI drivers with its
 * entry in the MIDI list of the Multimedia control panel
 * (src/win/midireg.c), and sets Opl3Device=2 in [ESFM.DRV] of SYSTEM.INI,
 * so that ESFM.DRV leaves the device to it from the next start. It takes
 * all of that back, with Opl3Device=1, when ESS's driver comes back, and
 * when Opl3Device=0 turns the device off.
 *
 * With the rebuilt ESFM.DRV, the Settings button of the two MIDI drivers
 * in the Multimedia control panel opens ESFMCFG.DLL's box of the patch
 * banks (src/win/esfmcfg.c), so essinst copies the DLL into the SYSTEM
 * folder too, and the bank files of its BANKS folder into the SYSTEM
 * folder's ESFMBANK, where the box lists them. ESS's driver takes the DLL
 * out again, and leaves the banks.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <direct.h>
#include <dos.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

#include "drvinst.h"
#include "midireg.h"

#ifndef WF_WINNT
#define WF_WINNT 0x4000
#endif

#define TITLE "ES1869 Driver Setup"
#define TEXT 1500

// KERNEL's registry functions, as src/win/drvcfg.c finds them
#define HKCU 0x80000001UL
#define HKU 0x80000003UL
#define REG_SZ 1
#define MAPPER "Software\\Microsoft\\Multimedia\\Sound Mapper"

typedef LONG(FAR PASCAL *REGOPENKEY)(DWORD, LPCSTR, DWORD FAR *);
typedef LONG(FAR PASCAL *REGCLOSEKEY)(DWORD);
typedef LONG(FAR PASCAL *REGQUERYVALUEEX)(DWORD, LPCSTR, DWORD FAR *,
                                          DWORD FAR *, BYTE FAR *, DWORD FAR *);
typedef LONG(FAR PASCAL *REGSETVALUEEX)(DWORD, LPCSTR, DWORD, DWORD,
                                        const BYTE FAR *, DWORD);
typedef LONG(FAR PASCAL *REGFLUSHKEY)(DWORD);

static FILE *log_file;
static char ini[DI_PATH]; // WININIT.INI
static char plan[TEXT];   // what the check found, for the question
static int collect, quiet;
static int opl3; // the OPL3 device is in the MIDI list after the install
static char srcdir[DI_PATH], sysdir[DI_PATH];

// the OPL3 device's driver for the Multimedia control panel
static const struct di_extra opl3_drv = {"ESFMOPL3.DRV", "ESFMOPL3.NEW",
                                         "ESFMOPL3.$$$"};
// and the box of the patch banks that the Settings button of the MIDI
// drivers opens
static const struct di_extra cfg_dll = {"ESFMCFG.DLL", "ESFMCFG.NEW",
                                        "ESFMCFG.$$$"};
static int banks; // the box and the banks go in with the drivers

// a line for the log
static void log_only(const char *text) {
  if (log_file) {
    fputs(text, log_file);
    fputs("\n", log_file);
    fflush(log_file);
    _commit(fileno(log_file));
  }
}

// and for the question, while the check collects it
static void plan_add(const char *text) {
  if (collect && strlen(plan) + strlen(text) + 3 < sizeof(plan)) {
    strcat(plan, text);
    strcat(plan, "\n");
  }
}

static void log_line(void *ctx, const char *text) {
  (void)ctx;
  log_only(text);
  plan_add(text);
}

static int rename_at_start(void *ctx, const char *dest, const char *src) {
  (void)ctx;
  return WritePrivateProfileString("rename", dest, src, ini) ? 0 : -1;
}

static const struct di_ops ops = {0, rename_at_start, log_line};

// the preferred devices: the Sound Mapper's values, the user's and those
// that new users start with, which are the same without user profiles
static const struct {
  DWORD root;
  const char *key, *value, *what, *who;
} prefs[] = {
    {HKCU, MAPPER, "Playback", "playback", ""},
    {HKCU, MAPPER, "Record", "recording", ""},
    {HKU, ".DEFAULT\\" MAPPER, "Playback", "playback", " of new users"},
    {HKU, ".DEFAULT\\" MAPPER, "Record", "recording", " of new users"},
};

// the preferred devices that get the names of the drivers that names
// describes (DI_NAME_*): logged, listed once in the question, and with set
// written. Returns how many
static int prefer(int names, int set) {
  static char text[200];
  REGOPENKEY open;
  REGCLOSEKEY close;
  REGQUERYVALUEEX query;
  REGSETVALUEEX setv;
  REGFLUSHKEY flush;
  HMODULE k = GetModuleHandle("KERNEL");
  char old[64], name[32];
  DWORD key, type, size;
  unsigned i;
  int n = 0;

  open = (REGOPENKEY)GetProcAddress(k, MAKEINTRESOURCE(217));
  close = (REGCLOSEKEY)GetProcAddress(k, MAKEINTRESOURCE(220));
  query = (REGQUERYVALUEEX)GetProcAddress(k, MAKEINTRESOURCE(225));
  setv = (REGSETVALUEEX)GetProcAddress(k, MAKEINTRESOURCE(226));
  flush = (REGFLUSHKEY)GetProcAddress(k, MAKEINTRESOURCE(227));
  if (!open || !close || !query || !setv)
    return 0;
  for (i = 0; i < sizeof(prefs) / sizeof(prefs[0]); i++) {
    if (open(prefs[i].root, prefs[i].key, &key))
      continue;
    memset(old, 0, sizeof(old));
    size = sizeof(old) - 1;
    if (query(key, prefs[i].value, 0, &type, (BYTE FAR *)old, &size) ||
        type != REG_SZ || !di_device_name(old, names, name)) {
      close(key);
      continue;
    }
    n++;
    if (!set) {
      _bprintf(text, sizeof(text), "The preferred %s device becomes %s.",
               prefs[i].what, name);
      if (!strstr(plan, text))
        plan_add(text);
      _bprintf(text, sizeof(text),
               "The preferred %s device%s, %.32s, becomes "
               "%s.",
               prefs[i].what, prefs[i].who, old, name);
    } else if (setv(key, prefs[i].value, 0, REG_SZ, (const BYTE FAR *)name,
                    strlen(name) + 1)) {
      _bprintf(text, sizeof(text),
               "The preferred %s device%s can't be set to "
               "%s.",
               prefs[i].what, prefs[i].who, name);
    } else {
      // to the disk now: Windows writes the registry late
      if (flush)
        flush(key);
      _bprintf(text, sizeof(text), "The preferred %s device%s is %s.",
               prefs[i].what, prefs[i].who, name);
    }
    log_only(text);
    close(key);
  }
  return n;
}

// ESFMOPL3.DRV in the SYSTEM folder while the card has it among its
// drivers (put), and out of it otherwise: logged, listed in the question,
// and with set put there or taken out. 1 if it changes, 0 if not, -1 if it
// can't, with the reason in why
static int opl3_file(int put, int set, char *why) {
  int loaded = GetModuleHandle("ESFMOPL3") != 0, r;

  r = put ? di_put_extra(&opl3_drv, srcdir, sysdir, loaded, set, &ops, why)
          : di_drop_extra(&opl3_drv, sysdir, loaded, set, &ops, why);
  if (r > 0 && !set)
    plan_add(put ? "ESFMOPL3.DRV, the OPL3 device's driver, goes in the "
                   "SYSTEM folder."
                 : "ESFMOPL3.DRV leaves the SYSTEM folder.");
  return r;
}

// ESFMCFG.DLL in the SYSTEM folder with the rebuilt ESFM.DRV (put), and
// out of it with ESS's: as opl3_file
static int cfg_file(int put, int set, char *why) {
  int loaded = GetModuleHandle("ESFMCFG") != 0, r;

  r = put ? di_put_extra(&cfg_dll, srcdir, sysdir, loaded, set, &ops, why)
          : di_drop_extra(&cfg_dll, sysdir, loaded, set, &ops, why);
  if (r > 0 && !set)
    plan_add(put ? "ESFMCFG.DLL, the box of the MIDI patch banks, goes in "
                   "the SYSTEM folder."
                 : "ESFMCFG.DLL leaves the SYSTEM folder.");
  return r;
}

// the bank files of essinst's BANKS folder into the SYSTEM folder's
// ESFMBANK, which is made if needed, each as its .$$$ first, checked, then
// renamed. 1 if one goes there (or would go), 0 if none, -1 on failure
static int bank_files(int set, char *why) {
  static char from[DI_PATH], to[DI_PATH], spec[DI_PATH];
  struct find_t ff;
  struct di_extra x;
  int r, any = 0;

  di_path(from, srcdir, "BANKS");
  di_path(to, sysdir, "ESFMBANK");
  di_path(spec, from, "*.*");
  if (_dos_findfirst(spec, _A_NORMAL | _A_RDONLY | _A_ARCH, &ff))
    return 0;
  if (set && access(to, 0) && mkdir(to)) {
    _bprintf(why, DI_WHY, "%.*s can't be made.", DI_PATH, to);
    log_line(0, why);
    return -1;
  }
  do {
    x.name = x.staged = ff.name;
    x.temp = "ESFMBANK.$$$";
    r = di_put_extra(&x, from, to, 0, set, &ops, why);
    if (r < 0)
      return r;
    any |= r;
  } while (!_dos_findnext(&ff));
  if (any && !set)
    plan_add("The patch banks of its BANKS folder go in SYSTEM\\ESFMBANK, "
             "where the box lists them.");
  return any;
}

// the box and the banks with the rebuilt ESFM.DRV (put), the box out with
// ESS's: 1 if something changes (or would), -1 if something can't
static int banks_apply(int put, int set, char *why) {
  int c = cfg_file(put, set, why), b = put && c >= 0 ? bank_files(set, why) : 0;

  return c < 0 || b < 0 ? -1 : c || b;
}

// [ESFM.DRV] Opl3Device=2 in SYSTEM.INI while the card has ESFMOPL3.DRV
// among its drivers (on), 1 in its place when the driver goes; 0, the
// device off, stays. With set written. 1 if it changes
static int opl3_key(int on, int set) {
  static char text[120];
  int now = GetPrivateProfileInt("ESFM.DRV", "Opl3Device", 1, "SYSTEM.INI");
  int want = on ? 2 : now == 2 ? 1 : now;

  if (want == now)
    return 0;
  _bprintf(text, sizeof(text), "SYSTEM.INI: [ESFM.DRV] Opl3Device=%d%s.", want,
           set ? "" : " from the next start");
  if (set && !WritePrivateProfileString("ESFM.DRV", "Opl3Device",
                                        want == 2 ? "2" : "1", "SYSTEM.INI"))
    _bprintf(text, sizeof(text),
             "SYSTEM.INI can't be written: Opl3Device "
             "stays %d.",
             now);
  log_only(text);
  return 1;
}

// ESFMOPL3.DRV among the card's MIDI drivers, and its entry in the MIDI
// list of the Multimedia control panel, as opl3 says: logged, listed in the
// question, and with set written. 1 if it changes; in *has whether the
// card has the driver after it
static int midi_list(int set, int *has) {
  static char text[200], old[48];
  struct mr_entry e;
  int stale, r = mr_opl3(opl3, set, &e, &stale);

  *has = opl3 && (r == MR_KEEP || r == MR_ADD || r == MR_TIDY);
  old[0] = 0;
  if (stale == 1)
    strcpy(old, ", in place of an old entry");
  else if (stale > 1)
    _bprintf(old, sizeof(old), ", in place of %d old entries", stale);
  if (r == MR_ADD && !set) {
    _bprintf(text, sizeof(text),
             "The MIDI list of the Multimedia control panel gets %s at the "
             "next start.",
             e.name);
    plan_add(text);
    _bprintf(text, sizeof(text),
             "The card gets ESFMOPL3.DRV among its MIDI drivers, and the MIDI "
             "list %s, as %s%s.",
             e.name, e.key, old);
  } else if (r == MR_ADD) {
    _bprintf(text, sizeof(text),
             "The card has ESFMOPL3.DRV among its MIDI drivers, and the MIDI "
             "list %s, as %s.",
             e.name, e.key);
  } else if (r == MR_TIDY && !set) {
    plan_add("Old entries of the OPL3 device leave the MIDI list.");
    _bprintf(text, sizeof(text),
             "The MIDI list has the OPL3 device, %s, as %s%s.", e.name, e.key,
             old[0] ? old : "");
  } else if (r == MR_TIDY) {
    _bprintf(text, sizeof(text),
             "The old entries of %s are out of the MIDI "
             "list.",
             e.name);
  } else if (r == MR_REMOVE && !set) {
    _bprintf(text, sizeof(text),
             "The MIDI list of the Multimedia control panel loses %s.", e.name);
    plan_add(text);
    _bprintf(text, sizeof(text),
             "The MIDI list loses the OPL3 device, %s (%s).", e.name, e.key);
  } else if (r == MR_REMOVE) {
    _bprintf(text, sizeof(text), "%s (%s) is out of the MIDI list.", e.name,
             e.key);
  } else if (r == MR_FAIL) {
    *has = 0;
    _bprintf(text, sizeof(text), "The OPL3 device's entry %s can't be %s.",
             e.key, opl3 ? "added to the MIDI list" : "removed");
  } else if (set) {
    return 0;
  } else if (r == MR_NO_ESFM) {
    _bprintf(text, sizeof(text),
             "The MIDI list has no entry of ESFM.DRV for the card, so the "
             "OPL3 device stays ESFM.DRV's device 1, without an entry.");
  } else if (opl3) {
    _bprintf(text, sizeof(text),
             "The MIDI list has the OPL3 device, %s, as %s.", e.name, e.key);
  } else {
    return 0;
  }
  log_only(text);
  return r == MR_ADD || r == MR_REMOVE || r == MR_TIDY;
}

// the OPL3 device's driver, its place among the card's drivers and in the
// MIDI list, and Opl3Device, with put as the check found it: the file
// before the list names it, and without the file the device stays
// ESFM.DRV's device 1
static void opl3_apply(int put) {
  static char why[DI_WHY];
  int has;

  if (put && opl3_file(1, 1, why) < 0)
    opl3 = 0;
  midi_list(1, &has);
  if (!has)
    opl3_file(0, 1, why);
  opl3_key(has, 1);
}

static int box(const char *text, UINT flags) {
  if (quiet)
    return IDOK;
  return MessageBox(NULL, text, TITLE, flags);
}

// a failure: the reason, and where the details are
static int failed(const char *why) {
  static char text[TEXT];

  _bprintf(text, sizeof(text),
           "%s\n\nESSINST.LOG in the Windows folder has the details.", why);
  box(text, MB_OK | MB_ICONSTOP);
  return 1;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
  static char src[DI_PATH], sys[DI_PATH], win[DI_PATH], path[DI_PATH];
  static char why[DI_WHY], text[TEXT], args[128];
  int restore = 0, restart = 1, n, m, o, f, k, c, midi, has = 0;
  int names = DI_NAMES_ESS;
  char *p;

  (void)prev;
  (void)show;
  strncpy(args, cmd, sizeof(args) - 1);
  for (p = strtok(args, " \t"); p; p = strtok(0, " \t")) {
    if (!stricmp(p, "/restore") || !stricmp(p, "-restore"))
      restore = 1;
    else if (!stricmp(p, "/y") || !stricmp(p, "-y"))
      quiet = 1;
    else if (!stricmp(p, "/norestart") || !stricmp(p, "-norestart"))
      restart = 0;
  }
  GetModuleFileName(inst, src, sizeof(src));
  p = strrchr(src, '\\');
  if (p)
    *p = 0;
  GetSystemDirectory(sys, sizeof(sys));
  GetWindowsDirectory(win, sizeof(win));
  strcpy(srcdir, src);
  strcpy(sysdir, sys);
  di_path(ini, win, "WININIT.INI");
  di_path(path, win, "ESSINST.LOG");
  log_file = fopen(path, "w");
  _bprintf(text, sizeof(text), "essinst%s: from %.*s into %.*s",
           restore ? " /restore" : "", DI_PATH, src, DI_PATH, sys);
  log_line(0, text);

  if ((GetWinFlags() & WF_WINNT) || LOBYTE(GetVersion()) < 3 ||
      (LOBYTE(GetVersion()) == 3 && HIBYTE(LOWORD(GetVersion())) < 95)) {
    log_line(0, "Not Windows 95 or 98.");
    return failed("The ES1869 drivers are for Windows 95 and 98.");
  }

  // the names the devices get: by DAC with the rebuilt ES1869.DRV, as its
  // SYSTEM.INI keys say (settings.asm, both on by default)
  if (di_rebuilt_after(restore ? 0 : src, sys)) {
    if (GetPrivateProfileInt("ES1869.DRV", "Audio1Device", 1, "SYSTEM.INI"))
      names |= DI_NAME_AUDIO1;
    if (GetPrivateProfileInt("ES1869.DRV", "FMRecordDevice", 1, "SYSTEM.INI"))
      names |= DI_NAME_FMREC;
  }
  // the OPL3 device of the rebuilt ESFM.DRV, on by default (esfmini.asm)
  opl3 = di_rebuilt_has(2, restore ? 0 : src, sys, "Opl3Device") &&
         GetPrivateProfileInt("ESFM.DRV", "Opl3Device", 1, "SYSTEM.INI");
  // and its driver for the Multimedia control panel, from essinst's folder:
  // without it, the device stays ESFM.DRV's device 1, without an entry
  di_path(path, src, opl3_drv.name);
  if (opl3 && access(path, 0)) {
    log_line(0, "ESFMOPL3.DRV isn't next to essinst: the OPL3 device stays "
                "ESFM.DRV's device 1, without an entry in the MIDI list.");
    opl3 = 0;
  }
  // the box of the patch banks, with a rebuilt ESFM.DRV that opens it
  banks = di_rebuilt_has(2, restore ? 0 : src, sys, "BankSettings");
  di_path(path, src, cfg_dll.name);
  if (banks && access(path, 0)) {
    log_line(0, "ESFMCFG.DLL isn't next to essinst: the Settings button of "
                "the MIDI drivers has no box of the patch banks.");
    banks = 0;
  }

  // what there is to do
  collect = 1;
  n = restore ? di_restore(sys, 0, &ops, why)
              : di_install(src, sys, 0, &ops, why);
  m = n >= 0 ? prefer(names, 0) : 0;
  o = n >= 0 ? midi_list(0, &has) : 0;
  f = n >= 0 ? opl3_file(has, 0, why) : 0;
  k = n >= 0 && f >= 0 ? opl3_key(has, 0) : 0;
  c = n >= 0 && f >= 0 ? banks_apply(banks, 0, why) : 0;
  collect = 0;
  if (n < 0 || f < 0 || c < 0)
    return failed(why);
  midi = o || f || k;
  o = midi || c;
  if (!n && !m && !o) {
    box(restore ? "ESS's drivers are in place already."
                : "The rebuilt drivers are installed already.",
        MB_OK | MB_ICONINFORMATION);
    return 0;
  }
  if (!n) {
    // the drivers are in place, but the devices' names or the MIDI list
    // aren't as they make them
    _bprintf(text, sizeof(text),
             "essinst brings Windows' devices in line with the drivers in "
             "place:\n\n%s\n%s",
             plan,
             midi ? "The MIDI list's change takes effect when Windows starts "
                    "again."
                  : "Windows doesn't restart.");
    if (box(text, MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
      log_line(0, "Cancelled: nothing was changed.");
      return 0;
    }
    if (m) {
      log_line(0, "Naming the preferred devices:");
      prefer(names, 1);
    }
    if (o) {
      log_line(0, "The OPL3 device's driver and the MIDI list:");
      opl3_apply(has);
      banks_apply(banks, 1, why);
    }
    box("Windows' devices are in line with the drivers.",
        MB_OK | MB_ICONINFORMATION);
    return 0;
  }
  _bprintf(text, sizeof(text), "%s\n\n%s\n%s",
           restore ? "essinst puts ESS's drivers back:"
                   : "essinst puts the rebuilt drivers in place:",
           plan,
           restart ? "Windows then restarts, and the drivers go in place. "
                     "Save your work in other programs first."
                   : "They go in place when Windows restarts.");
  if (box(text, MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
    log_line(0, "Cancelled: nothing was changed.");
    return 0;
  }

  // the backups, the new files, and WININIT.INI, then the devices' names
  log_line(0, "Installing:");
  n = restore ? di_restore(sys, 1, &ops, why)
              : di_install(src, sys, 1, &ops, why);
  if (n < 0)
    return failed(why);
  prefer(names, 1);
  opl3_apply(has);
  banks_apply(banks, 1, why);
  WritePrivateProfileString(0, 0, 0, ini);
  if (!restart) {
    log_line(0, "Done: the drivers go in place at the next restart.");
    box("The drivers are ready. Windows puts them in place when it "
        "restarts.",
        MB_OK | MB_ICONINFORMATION);
    return 0;
  }
  log_line(0, "Restarting Windows.");
  if (log_file)
    fclose(log_file);
  log_file = 0;
  if (!ExitWindows(EW_REBOOTSYSTEM, 0)) {
    log_file = fopen(path, "a");
    log_line(0, "A program kept Windows from restarting.");
    box("A program kept Windows from restarting. The drivers go in place "
        "when Windows restarts.",
        MB_OK | MB_ICONINFORMATION);
  }
  if (log_file)
    fclose(log_file);
  return 0;
}
