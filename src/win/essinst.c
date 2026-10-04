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

static FILE *log_file;
static char ini[DI_PATH]; // WININIT.INI
static char plan[TEXT];   // what the check found, for the question
static int collect, quiet;

static void log_line(void *ctx, const char *text) {
  unsigned n;

  (void)ctx;
  if (log_file) {
    fputs(text, log_file);
    fputs("\n", log_file);
    fflush(log_file);
    _commit(fileno(log_file));
  }
  n = strlen(plan);
  if (collect && n + strlen(text) + 3 < sizeof(plan)) {
    strcat(plan, text);
    strcat(plan, "\n");
  }
}

static int rename_at_start(void *ctx, const char *dest, const char *src) {
  (void)ctx;
  return WritePrivateProfileString("rename", dest, src, ini) ? 0 : -1;
}

static const struct di_ops ops = {0, rename_at_start, log_line};

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
  int restore = 0, restart = 1, n;
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

  // what there is to do
  collect = 1;
  n = restore ? di_restore(sys, 0, &ops, why)
              : di_install(src, sys, 0, &ops, why);
  collect = 0;
  if (n < 0)
    return failed(why);
  if (!n) {
    box(restore ? "ESS's drivers are in place already."
                : "The rebuilt drivers are installed already.",
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

  // the backups, the new files, and WININIT.INI
  log_line(0, "Installing:");
  n = restore ? di_restore(sys, 1, &ops, why)
              : di_install(src, sys, 1, &ops, why);
  if (n < 0)
    return failed(why);
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
