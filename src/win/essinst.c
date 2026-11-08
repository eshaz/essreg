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
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <io.h>
#include <stdio.h>
#include <string.h>

#include "drvinst.h"

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
  int restore = 0, restart = 1, n, m, names = DI_NAMES_ESS;
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

  // what there is to do
  collect = 1;
  n = restore ? di_restore(sys, 0, &ops, why)
              : di_install(src, sys, 0, &ops, why);
  m = n >= 0 ? prefer(names, 0) : 0;
  collect = 0;
  if (n < 0)
    return failed(why);
  if (!n && !m) {
    box(restore ? "ESS's drivers are in place already."
                : "The rebuilt drivers are installed already.",
        MB_OK | MB_ICONINFORMATION);
    return 0;
  }
  if (!n) {
    // the drivers are in place, but the devices' names aren't
    _bprintf(text, sizeof(text),
             "essinst gives Windows' preferred devices the names that the "
             "drivers in place give them:\n\n%s\nWindows doesn't restart.",
             plan);
    if (box(text, MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
      log_line(0, "Cancelled: nothing was changed.");
      return 0;
    }
    log_line(0, "Naming the preferred devices:");
    prefer(names, 1);
    box("The preferred devices have their names.", MB_OK | MB_ICONINFORMATION);
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
  WritePrivateProfileString(0, 0, 0, ini);
  prefer(names, 1);
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
