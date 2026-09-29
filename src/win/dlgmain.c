/*
 * The main dialog of essctl: category list, menus, status
 * lines and the
 * refresh timer.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 *
 * Licensed under GPL Version 3.0
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "essctl.h"

#include "drvcfg.h"
#include "esshw.h"
#include "resource.h"
#include "vxdapi.h"
#include "winio.h"
#include <commdlg.h>

#define TIMER_ID 1
#define TIMER_MS 1000

struct page_entry {
  int kind, arg;
};

static struct page_entry entries[PG_COUNT + 3];
static int nentries;
static int auto_refresh = 1;
static char status[160];

void set_help(const char *text) { SetDlgItemText(g_main, IDC_HELPTEXT, text); }

void set_status(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  _vbprintf(status, sizeof(status), fmt, ap);
  va_end(ap);
  SetDlgItemText(g_main, IDC_STATUS, status);
}

void update_owner_status(void) {
  static char shown[96];
  char text[96];

  // a new text only: each one repaints the line
  winio_owner_text(text, sizeof(text));
  if (strcmp(text, shown)) {
    strcpy(shown, text);
    SetDlgItemText(g_main, IDC_OWNERS, text);
  }
}

static void add_entry(HWND list, const char *name, int kind, int arg) {
  SendMessage(list, LB_ADDSTRING, 0, (LPARAM)(LPSTR)name);
  entries[nentries].kind = kind;
  entries[nentries++].arg = arg;
}

static void init_pages(HWND dlg) {
  HWND list = GetDlgItem(dlg, IDC_PAGES);
  int pg, f;

  add_entry(list, "Device information", PK_INFO, 0);
  for (pg = 0; pg < PG_COUNT; pg++) {
    for (f = 0; f < F_COUNT; f++)
      if (ess_fields[f].page == pg)
        break;
    if (f < F_COUNT)
      add_entry(list, ess_page_names[pg], PK_FIELDS, pg);
  }
  add_entry(list, "Raw registers", PK_RAW, 0);
  add_entry(list, "ESFM patch bank", PK_ESFM, 0);
  SendMessage(list, LB_SETCURSEL, 0, 0);
}

static void select_page(HWND dlg) {
  int sel = (int)SendDlgItemMessage(dlg, IDC_PAGES, LB_GETCURSEL, 0, 0);

  if (sel >= 0 && sel < nentries)
    page_show(entries[sel].kind, entries[sel].arg);
}

// --- file dialogs -----------------------------------------------------------

static int file_dialog(HWND owner, int save, const char *filter,
                       const char *ext, char *path, unsigned size) {
  OPENFILENAME ofn;

  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = path;
  ofn.nMaxFile = size;
  ofn.lpstrDefExt = ext;
  ofn.Flags =
      OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  return save ? GetSaveFileName(&ofn) : GetOpenFileName(&ofn);
}

static const char ini_filter[] = "Profiles (*.ini)\0*.ini\0"
                                 "All files (*.*)\0*.*\0";
static const char txt_filter[] = "Text files (*.txt)\0*.txt\0"
                                 "All files (*.*)\0*.*\0";

static void cmd_load(HWND dlg) {
  char path[144], report[256];
  int rc;

  path[0] = 0;
  if (!file_dialog(dlg, 0, ini_filter, "ini", path, sizeof(path)))
    return;
  rc = profile_load_file(path, report, sizeof(report));
  // essctl may have closed while the load waited for the DSP
  if (!IsWindow(dlg))
    return;
  page_refresh(REFRESH_USER);
  if (rc)
    msg_error(dlg, "%s:\n%s", path, report);
  else
    set_status("%s", report);
}

static void cmd_save(HWND dlg) {
  char path[144], report[256];
  int rc;

  strcpy(path, "ESS.INI");
  if (!file_dialog(dlg, 1, ini_filter, "ini", path, sizeof(path)))
    return;
  rc = profile_save_file(path, report, sizeof(report));
  if (rc)
    msg_error(dlg, "%s:\n%s", path, report);
  else
    set_status("%s", report);
}

static void cmd_dump(HWND dlg) {
  char path[144];

  strcpy(path, "ESSDUMP.TXT");
  if (!file_dialog(dlg, 1, txt_filter, "txt", path, sizeof(path)))
    return;
  if (dump_file(path))
    msg_error(dlg, "Could not write %s", path);
  else
    set_status("Registers written to %s", path);
}

static void cmd_expert(HWND dlg) {
  if (!g_expert &&
      !confirm(dlg, "Expert mode unlocks registers that can stop playback, "
                    "hang the DSP until the next reset, or move the card to "
                    "other resources until Windows restarts.\n\n"
                    "Turn on Expert mode for this session?"))
    return;
  g_expert = !g_expert;
  CheckMenuItem(GetMenu(dlg), IDM_EXPERT,
                MF_BYCOMMAND | (g_expert ? MF_CHECKED : MF_UNCHECKED));
  select_page(dlg);
  set_status("Expert mode %s", g_expert ? "on" : "off");
}

// the check mark shows ESSWaveTableChip; grayed without the ES1869's key
static void fmdac_menu(HWND dlg) {
  DWORD v;
  int r = drvcfg_get(DRVCFG_WAVETABLE, &v);

  EnableMenuItem(GetMenu(dlg), IDM_FMDAC,
                 MF_BYCOMMAND | (r < 0 ? MF_GRAYED : MF_ENABLED));
  CheckMenuItem(GetMenu(dlg), IDM_FMDAC,
                MF_BYCOMMAND | (r > 0 && v ? MF_CHECKED : MF_UNCHECKED));
}

static void cmd_fmdac(HWND dlg) {
  char report[200];
  int on = !(GetMenuState(GetMenu(dlg), IDM_FMDAC, MF_BYCOMMAND) & MF_CHECKED);

  if (fmdac_set(on, report, sizeof(report)) == 2)
    msg_error(dlg, "%s.", report);
  set_status("%s", report);
  fmdac_menu(dlg);
  page_refresh(REFRESH_USER);
}

static void cmd_about(HWND dlg) {
  char text[400], path[96];

  winio_path_text(path, sizeof(path));
  sprintf(text,
          "ES1869 Control %s\n\n"
          "Controls the registers of the ESS ES1869 AudioDrive.\n"
          "%s\n\n"
          "(c) 2024 Ethan Halsall, GPL version 3.\n"
          "https://github.com/eshaz/essreg",
          ESSCTL_VERSION, path);
  MessageBox(dlg, text, "About ES1869 Control", MB_OK | MB_ICONINFORMATION);
}

static void on_command(HWND dlg, int id, int code, HWND ctl) {
  switch (id) {
  case IDC_PAGES:
    if (code == LBN_SELCHANGE)
      select_page(dlg);
    return;
  case IDM_LOAD:
    cmd_load(dlg);
    return;
  case IDM_SAVE:
    cmd_save(dlg);
    return;
  case IDM_DUMP:
    cmd_dump(dlg);
    return;
  case IDCANCEL: // Esc
    // in a text field it drops what was typed
    if (page_kind() == PK_FIELDS && fields_cancel_edit())
      return;
    g_closing = 1;
    DestroyWindow(dlg);
    return;
  case IDM_EXIT:
    g_closing = 1;
    DestroyWindow(dlg);
    return;
  case IDM_REFRESH:
    page_refresh(REFRESH_USER);
    update_owner_status();
    return;
  case IDM_READDSP:
    page_refresh(REFRESH_DSP);
    return;
  case IDM_AUTOREFRESH:
    auto_refresh = !auto_refresh;
    CheckMenuItem(GetMenu(dlg), IDM_AUTOREFRESH,
                  MF_BYCOMMAND | (auto_refresh ? MF_CHECKED : MF_UNCHECKED));
    return;
  case IDM_EXPERT:
    cmd_expert(dlg);
    return;
  case IDM_FMDAC:
    cmd_fmdac(dlg);
    return;
  case IDM_ESFM_LOAD:
    esfm_menu_load(dlg);
    return;
  case IDM_ESFM_RESTORE:
    esfm_menu_restore(dlg);
    return;
  case IDM_ABOUT:
    cmd_about(dlg);
    return;
  }
  page_command(id, code, ctl);
}

BOOL CALLBACK __export main_dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
  char title[128], path[96];

  switch (msg) {
  case WM_INITDIALOG:
    g_main = dlg;
    g_font = (HFONT)SendMessage(dlg, WM_GETFONT, 0, 0);
    page_init();
    winio_path_text(path, sizeof(path));
    sprintf(title, "ES1869 Control - %s", path);
    SetWindowText(dlg, title);
    init_pages(dlg);
    if (winio_can_poll()) {
      CheckMenuItem(GetMenu(dlg), IDM_AUTOREFRESH, MF_BYCOMMAND | MF_CHECKED);
    } else {
      // direct I/O with ES1869.VXD takes the DSP on every read, so no polling
      auto_refresh = 0;
      EnableMenuItem(GetMenu(dlg), IDM_AUTOREFRESH, MF_BYCOMMAND | MF_GRAYED);
    }
    fmdac_menu(dlg);
    if (!SetTimer(dlg, TIMER_ID, TIMER_MS, 0)) {
      auto_refresh = 0;
      CheckMenuItem(GetMenu(dlg), IDM_AUTOREFRESH, MF_BYCOMMAND | MF_UNCHECKED);
      EnableMenuItem(GetMenu(dlg), IDM_AUTOREFRESH, MF_BYCOMMAND | MF_GRAYED);
    }
    select_page(dlg);
    if (!auto_refresh && winio_can_poll())
      set_status("No timer left in Windows: F5 refreshes");
    update_owner_status();
    return TRUE;

  case WM_COMMAND:
    on_command(dlg, wp, HIWORD(lp), (HWND)LOWORD(lp));
    return TRUE;

  case WM_HSCROLL:
    page_hscroll(wp, LOWORD(lp), (HWND)HIWORD(lp));
    return TRUE;

  case WM_VSCROLL:
    if ((HWND)HIWORD(lp) == GetDlgItem(dlg, IDC_PGSCROLL))
      page_vscroll(wp, LOWORD(lp));
    return TRUE;

  case WM_LBUTTONDOWN:
    page_click(LOWORD(lp), HIWORD(lp));
    return TRUE;

  case WM_TIMER:
    if (auto_refresh && !IsIconic(dlg)) {
      page_refresh(REFRESH_TIMER);
      update_owner_status();
    }
    return TRUE;

  case WM_CLOSE:
    g_closing = 1;
    DestroyWindow(dlg);
    return TRUE;

  case WM_DESTROY:
    KillTimer(dlg, TIMER_ID);
    PostQuitMessage(0);
    return TRUE;
  }
  return FALSE;
}
