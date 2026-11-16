/*
 * cfgcall is a 16-bit Windows helper for tests/test_wine.py: it opens
 * ESFMCFG.DLL's box of the MIDI patch banks, as the Multimedia control
 * panel's Settings button does through the drivers, and works it from a
 * timer while it's open.
 *
 * Usage:
 *   cfgcall <outdir> <device> <esfm> <opl3>
 *
 * It calls BankDialog(0, device). Once the box is open, it writes to
 * <outdir>\CFGCALL.TXT the title, the entries of both lists, the selected
 * ones, and whether the OPL3 list is enabled, selects entry <esfm> of the
 * ESFM list and <opl3> of the OPL3 list (-1 keeps the selection), and
 * presses OK, or Cancel when <esfm> is "cancel". Then it writes what
 * BankDialog returned.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IDC_ESFM 102 // src/win/esfmcfg.h
#define IDC_OPL3 105

typedef UINT(FAR PASCAL *BANKDIALOG)(HWND, UINT);

static char out_path[160];
static int sel_esfm, sel_opl3, cancel, done;

static void put(const char *line) {
  FILE *f = fopen(out_path, "a");

  if (f) {
    fprintf(f, "%s\n", line);
    fclose(f);
  }
}

static void list(HWND dlg, int id, const char *name) {
  static char text[200], line[240];
  HWND cb = GetDlgItem(dlg, id);
  int i, n = (int)SendMessage(cb, CB_GETCOUNT, 0, 0);

  for (i = 0; i < n; i++) {
    SendMessage(cb, CB_GETLBTEXT, i, (LPARAM)(LPSTR)text);
    sprintf(line, "%s %d %s", name, i, text);
    put(line);
  }
  sprintf(line, "%s selected %d", name,
          (int)SendMessage(cb, CB_GETCURSEL, 0, 0));
  put(line);
  sprintf(line, "%s enabled %d", name, IsWindowEnabled(cb) ? 1 : 0);
  put(line);
}

void FAR PASCAL __export tick(HWND hwnd, UINT msg, UINT id, DWORD time) {
  static char title[80], line[120];
  HWND dlg = FindWindow(NULL, "MIDI patch banks");

  (void)hwnd;
  (void)msg;
  (void)time;
  if (!dlg || done)
    return;
  done = 1;
  KillTimer(NULL, id);
  GetWindowText(dlg, title, sizeof(title));
  sprintf(line, "title %s", title);
  put(line);
  list(dlg, IDC_ESFM, "esfm");
  list(dlg, IDC_OPL3, "opl3");
  sprintf(line, "focus %d", GetDlgCtrlID(GetFocus()));
  put(line);
  if (sel_esfm >= 0)
    SendDlgItemMessage(dlg, IDC_ESFM, CB_SETCURSEL, sel_esfm, 0);
  if (sel_opl3 >= 0)
    SendDlgItemMessage(dlg, IDC_OPL3, CB_SETCURSEL, sel_opl3, 0);
  PostMessage(dlg, WM_COMMAND, cancel ? IDCANCEL : IDOK, 0);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
  static char args[200], *argv[4], line[80];
  HINSTANCE lib;
  BANKDIALOG dialog;
  UINT device, r;
  int argc = 0;
  char *p;

  (void)inst;
  (void)prev;
  (void)show;
  lstrcpyn(args, cmdline, sizeof(args));
  for (p = strtok(args, " "); p && argc < 4; p = strtok(NULL, " "))
    argv[argc++] = p;
  if (argc != 4)
    return 2;
  sprintf(out_path, "%.140s\\CFGCALL.TXT", argv[0]);
  remove(out_path);
  device = atoi(argv[1]);
  cancel = !strcmp(argv[2], "cancel");
  sel_esfm = cancel ? -1 : atoi(argv[2]);
  sel_opl3 = atoi(argv[3]);
  lib = LoadLibrary("ESFMCFG.DLL");
  if ((UINT)lib < 32) {
    sprintf(line, "load failed %u", (UINT)lib);
    put(line);
    return 1;
  }
  dialog = (BANKDIALOG)GetProcAddress(lib, "BANKDIALOG");
  if (!dialog) {
    put("proc missing");
    FreeLibrary(lib);
    return 1;
  }
  SetTimer(NULL, 1, 300, (TIMERPROC)tick);
  r = dialog(NULL, device);
  sprintf(line, "result %u", r);
  put(line);
  FreeLibrary(lib);
  return 0;
}
