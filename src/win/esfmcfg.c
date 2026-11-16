/*
 * ESFMCFG.DLL: the box where the patch banks of ESFM.DRV's two MIDI
 * devices are chosen. The Settings button in the properties of either
 * driver in the Multimedia control panel opens it (DRV_CONFIGURE, in
 * src/esfm/esfmcfg.asm and src/esfm/esfmopl3.asm), and essinst puts it in
 * Windows' SYSTEM folder.
 *
 * Notes:
 *
 * The ESFM device plays the bank that Bank= in [ESFM.DRV] of SYSTEM.INI
 * names, and the OPL3 device the GENMIDI lump that Opl3Bank= names. Each
 * device reads its key when a program opens it, so a choice plays from the
 * next open, and without the key the driver's own bank plays.
 *
 * Each list holds the driver's own bank, the files in the ESFMBANK folder
 * of the SYSTEM folder that are of the device's kind (bank_kind, as the
 * driver checks them), the file the key names if it is somewhere else, and
 * the files that Browse picks. OK writes the keys that changed.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <commdlg.h>
#include <dos.h>
#include <stdio.h>
#include <string.h>

#include "esfmbank.h"
#include "esfmcfg.h"

#define NDEV 2
#define MAXFILES 40 // a list's files, after the driver's own bank
#define PATHLEN 128 // FIX_PATH of ESFM.DRV, with the 0
#define SECTION "ESFM.DRV"
#define INI "SYSTEM.INI"
#define TITLE "MIDI patch banks"

static HINSTANCE dll_inst;
static UINT first;             // the device whose Settings opened the box
static char bank_dir[PATHLEN]; // SYSTEM\ESFMBANK, "" if it can't be named

static struct dev {
  int kind;               // BANK_KIND_*
  const char *key;        // its key in [ESFM.DRV]
  int ctl, browse, label; // its controls
  int n;                  // entries, [0] the driver's own bank ("")
  int start;              // the entry the key named when the box opened
  int off;                // 1 with Opl3Device=0: the device isn't there
  char path[MAXFILES + 1][PATHLEN];
} devs[NDEV] = {
    {BANK_KIND_ESFM, "Bank", IDC_ESFM, IDC_ESFM_BROWSE, IDC_ESFM_LABEL},
    {BANK_KIND_GENMIDI, "Opl3Bank", IDC_OPL3, IDC_OPL3_BROWSE, IDC_OPL3_LABEL},
};

// the kind of bank the file at path is (bank_kind), 0 if it can't be read
static int file_kind(const char *path) {
  HGLOBAL h;
  u8 FAR *buf;
  HFILE f;
  long size;
  int kind = 0;

  f = _lopen(path, OF_READ | OF_SHARE_DENY_NONE);
  if (f == HFILE_ERROR)
    return 0;
  size = _llseek(f, 0, 2);
  if (size >= BANK_TABLE && size <= GENMIDI_FILE_MAX && !_llseek(f, 0, 0)) {
    h = GlobalAlloc(GMEM_MOVEABLE, (DWORD)size);
    buf = h ? (u8 FAR *)GlobalLock(h) : 0;
    if (buf && _lread(f, buf, (UINT)size) == (UINT)size)
      kind = bank_kind(buf, (u32)size);
    if (buf)
      GlobalUnlock(h);
    if (h)
      GlobalFree(h);
  }
  _lclose(f);
  return kind;
}

// the entry of dev that is path, added if it isn't there yet; -1 when the
// list is full
static int find_or_add(struct dev *dev, const char *path) {
  int i;

  for (i = 1; i < dev->n; i++)
    if (!lstrcmpi(dev->path[i], path))
      return i;
  if (dev->n > MAXFILES || strlen(path) >= PATHLEN)
    return -1;
  strcpy(dev->path[dev->n], path);
  return dev->n++;
}

// the bank files of the ESFMBANK folder, each in the list of its device
static void list_dir(void) {
  static char spec[PATHLEN + 8], path[PATHLEN];
  struct find_t ff;
  unsigned rc;
  int d, i, j, kind;

  _bprintf(spec, sizeof(spec), "%.120s\\*.*", bank_dir);
  for (rc = _dos_findfirst(spec, _A_NORMAL | _A_RDONLY | _A_ARCH, &ff); !rc;
       rc = _dos_findnext(&ff)) {
    if (strlen(bank_dir) + 1 + strlen(ff.name) >= PATHLEN)
      continue;
    sprintf(path, "%s\\%s", bank_dir, ff.name);
    kind = file_kind(path);
    for (d = 0; d < NDEV; d++)
      if (kind == devs[d].kind)
        find_or_add(&devs[d], path);
  }
  // in the order of their names, whatever order the folder has them in
  for (d = 0; d < NDEV; d++)
    for (i = 2; i < devs[d].n; i++)
      for (j = i; j > 1 && lstrcmpi(devs[d].path[j - 1], devs[d].path[j]) > 0;
           j--) {
        strcpy(path, devs[d].path[j]);
        strcpy(devs[d].path[j], devs[d].path[j - 1]);
        strcpy(devs[d].path[j - 1], path);
      }
}

// what the list shows for entry i of device d: the driver's own bank, the
// name of a file of the ESFMBANK folder, or the whole path of another
static const char *entry_name(int d, int i) {
  const char *p = devs[d].path[i];
  unsigned k = strlen(bank_dir);

  if (!i) {
    if (d == CFG_OPL3)
      return "The driver's own: Freedoom's GENMIDI";
    return GetPrivateProfileInt(SECTION, "BetterSquareWave", 1, INI)
               ? "The driver's own: the square-wave bank"
               : "The driver's own: ESS's bank";
  }
  if (k && !strnicmp(p, bank_dir, k) && p[k] == '\\')
    return p + k + 1;
  return p;
}

static void fill(HWND dlg, int d, int sel) {
  HWND cb = GetDlgItem(dlg, devs[d].ctl);
  int i;

  SendMessage(cb, CB_RESETCONTENT, 0, 0);
  for (i = 0; i < devs[d].n; i++)
    SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)entry_name(d, i));
  SendMessage(cb, CB_SETCURSEL, sel, 0);
}

static void init(HWND dlg) {
  static char cur[PATHLEN], note[240];
  UINT len;
  int d;

  len = GetSystemDirectory(bank_dir, sizeof(bank_dir) - 10);
  if (!len || len >= sizeof(bank_dir) - 10)
    bank_dir[0] = 0;
  else
    strcat(bank_dir, "\\ESFMBANK");
  for (d = 0; d < NDEV; d++) {
    devs[d].n = 1;
    devs[d].path[0][0] = 0;
    devs[d].start = 0;
  }
  devs[CFG_ESFM].off = 0;
  devs[CFG_OPL3].off = !GetPrivateProfileInt(SECTION, "Opl3Device", 1, INI);
  if (bank_dir[0])
    list_dir();
  for (d = 0; d < NDEV; d++) {
    GetPrivateProfileString(SECTION, devs[d].key, "", cur, sizeof(cur), INI);
    if (cur[0])
      devs[d].start = find_or_add(&devs[d], cur);
    if (devs[d].start < 0)
      devs[d].start = 0;
    fill(dlg, d, devs[d].start);
  }
  if (devs[CFG_OPL3].off) {
    EnableWindow(GetDlgItem(dlg, IDC_OPL3_LABEL), FALSE);
    EnableWindow(GetDlgItem(dlg, IDC_OPL3), FALSE);
    EnableWindow(GetDlgItem(dlg, IDC_OPL3_BROWSE), FALSE);
  }
  _bprintf(note, sizeof(note),
           "A bank plays from the next time a program opens its device. "
           "Banks go in %.100s.%s",
           bank_dir[0] ? bank_dir : "the ESFMBANK folder",
           devs[CFG_OPL3].off ? " The OPL3 device is off (Opl3Device=0)." : "");
  SetDlgItemText(dlg, IDC_NOTE, note);
}

// Browse for a bank of device d's kind, which goes in its list
static void browse(HWND dlg, int d) {
  static const char esfm_filter[] = "ESFM patch banks (*.bin;*.pat)\0"
                                    "*.bin;*.pat\0All files (*.*)\0*.*\0";
  static const char opl3_filter[] = "GENMIDI banks (*.op2;*.lmp)\0"
                                    "*.op2;*.lmp\0All files (*.*)\0*.*\0";
  static char path[PATHLEN];
  OPENFILENAME ofn;
  int i;

  path[0] = 0;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = dlg;
  ofn.lpstrFilter = d == CFG_ESFM ? esfm_filter : opl3_filter;
  ofn.lpstrFile = path;
  ofn.nMaxFile = sizeof(path);
  ofn.lpstrInitialDir = bank_dir[0] ? bank_dir : NULL;
  ofn.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileName(&ofn))
    return;
  if (file_kind(path) != devs[d].kind) {
    MessageBox(dlg,
               d == CFG_ESFM
                   ? "This file isn't an ESFM patch bank: a raw bank or a "
                     "RIFF Ptch file, as esfmpat and essctl take."
                   : "This file isn't a GENMIDI bank: \"#OPL_II#\" and 175 "
                     "instruments, the format of Doom's GENMIDI lump.",
               TITLE, MB_OK | MB_ICONEXCLAMATION);
    return;
  }
  i = find_or_add(&devs[d], path);
  if (i < 0) {
    MessageBox(dlg, "The list is full, or the file's path is too long.", TITLE,
               MB_OK | MB_ICONEXCLAMATION);
    return;
  }
  fill(dlg, d, i);
}

// OK: the keys whose choice changed; the driver's own bank takes the key
// away
static void apply(HWND dlg) {
  int d, sel;

  for (d = 0; d < NDEV; d++) {
    if (devs[d].off)
      continue;
    sel = (int)SendDlgItemMessage(dlg, devs[d].ctl, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= devs[d].n || sel == devs[d].start)
      continue;
    WritePrivateProfileString(SECTION, devs[d].key,
                              sel ? devs[d].path[sel] : NULL, INI);
  }
}

BOOL FAR PASCAL __export bank_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
  (void)lp;
  switch (msg) {
  case WM_INITDIALOG:
    init(dlg);
    SetFocus(GetDlgItem(
        dlg, first == CFG_OPL3 && !devs[CFG_OPL3].off ? IDC_OPL3 : IDC_ESFM));
    return FALSE; // the focus is set
  case WM_COMMAND:
    switch (wp) {
    case IDOK:
      apply(dlg);
      EndDialog(dlg, DRVCNF_OK);
      return TRUE;
    case IDCANCEL:
      EndDialog(dlg, DRVCNF_CANCEL);
      return TRUE;
    case IDC_ESFM_BROWSE:
      browse(dlg, CFG_ESFM);
      return TRUE;
    case IDC_OPL3_BROWSE:
      browse(dlg, CFG_OPL3);
      return TRUE;
    }
    break;
  }
  return FALSE;
}

UINT FAR PASCAL __export BankDialog(HWND parent, UINT device) {
  first = device;
  return DialogBoxParam(dll_inst, MAKEINTRESOURCE(IDD_BANKS), parent,
                        (DLGPROC)bank_proc, 0) == DRVCNF_OK
             ? DRVCNF_OK
             : DRVCNF_CANCEL;
}

int FAR PASCAL LibMain(HINSTANCE inst, WORD dataseg, WORD heapsize,
                       LPSTR cmdline) {
  (void)dataseg;
  (void)heapsize;
  (void)cmdline;
  dll_inst = inst;
  return 1;
}
