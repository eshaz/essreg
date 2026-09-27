/*
 * ess3d's tray icon. "ess3d tray" puts an icon in the taskbar's
 * notification area: green "3D" while the effect is heard, gray when it's
 * off. A click on it, right or left, opens a small panel with every 3-D
 * setting.
 *
 * Notes:
 *
 * Shell_NotifyIcon is only in the 32-bit SHELL32.DLL. A 16-bit program
 * reaches it through the generic thunks of Windows 95's KERNEL:
 * LoadLibraryEx32W (513), GetProcAddress32W (515) and CallProc32W (517).
 * On Windows 9x a 16-bit window or icon handle is also the 32-bit one.
 *
 * The panel is built from the catalog like essctl's pages: a check box for
 * each 3-D bit (on, run, and the undocumented mono and limit), and a text
 * field with a slider on its right for the level
 * and for each Spatializer register (ess3d_reg_field). A register added to
 * the catalog shows up here too. The sliders move in steps of one over the
 * field's range, the text field takes decimal for the level and hex for
 * the registers, and a value is written on Enter or when the field loses
 * the focus.
 *
 * The panel closes when another window comes to the front, like the
 * tray's menus, or on a second click on the icon. It watches the
 * foreground window instead of its own activation, since Windows may not
 * let it take the focus. With the register interface of the rebuilt
 * ES1869.VXD, the icon also checks the setting every few seconds, so it
 * follows the Windows mixer.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ess3d.h"
#include "ess3dtr.h"
#include "esscat.h"
#include "esshw.h"
#include "essio.h"
#include "winio.h"

#define TRAY_TITLE "ES1869 3-D"
#define WM_TRAYICON (WM_USER + 20) // lParam: the mouse message on the icon
#define TRAY_ID 1
#define POLL_TIMER 1
#define POLL_MS 3000
#define PANEL_TIMER 2 // while the panel is up: is it still in front
#define PANEL_MS 250

// Shell_NotifyIcon
#define NIM_ADD 0
#define NIM_MODIFY 1
#define NIM_DELETE 2
#define NIF_MESSAGE 1
#define NIF_ICON 2
#define NIF_TIP 4

// NOTIFYICONDATA of Windows 95, as the 32-bit shell reads it
struct nid32 {
  DWORD size;
  DWORD hwnd;
  DWORD id;
  DWORD flags;
  DWORD message;
  DWORD icon;
  char tip[64];
};

// the generic thunks: CallProc32W(args..., proc, pointer mask, count)
typedef DWORD(FAR PASCAL *LOADLIB32)(LPCSTR, DWORD, DWORD);
typedef DWORD(FAR PASCAL *GETPROC32)(DWORD, LPCSTR);
typedef BOOL(FAR PASCAL *FREELIB32)(DWORD);
typedef DWORD(FAR PASCAL *CALL32_0)(DWORD, DWORD, DWORD);
typedef DWORD(FAR PASCAL *CALL32_1)(DWORD, DWORD, DWORD, DWORD);
typedef DWORD(FAR PASCAL *CALL32_2)(DWORD, DWORD, DWORD, DWORD, DWORD);

// panel controls: the check boxes and buttons, then a row of each slider
#define IDC_P_ON 101
#define IDC_P_RUN 102
#define IDC_P_LIMIT 103
#define IDC_P_RESET 104
#define IDC_P_DEFAULTS 105
#define IDC_P_CLOSE 106
#define IDC_P_STATUS 107
#define IDC_P_MONO 108
#define IDC_P_LABEL 200 // + row
#define IDC_P_SLIDER 300
#define IDC_P_EDIT 400
#define IDC_P_RANGE 500
#define MAX_SLIDERS (1 + ESS3D_MAX_REGS)

// panel layout in dialog units
#define P_W 222
#define P_ROW 14
#define P_TOP 59 // first slider row

static struct {
  LOADLIB32 load;
  GETPROC32 proc;
  FREELIB32 free;
  FARPROC call;
  DWORD shell32, user32;
  DWORD notify;     // Shell_NotifyIconA
  DWORD foreground; // SetForegroundWindow
  DWORD getfg;      // GetForegroundWindow
} w32;

static HINSTANCE inst;
static const struct ess3d_cmd *cmd;
static HWND tray, panel;
static HICON icon_on, icon_off;
static struct nid32 nid;
static struct ess3d_state cur; // the setting as the chip returned it
static int cur_err;            // reading or writing it failed
static UINT taskbar_created;   // Explorer restarted: add the icon again
static int nsliders;
static int drag = -1;   // slider held without writing (direct I/O)
static UINT panel_fg;   // the window in front while the panel is up
static DWORD hidden_at; // GetTickCount() when the panel last hid

HWND tray_window(void) { return FindWindow(TRAY_CLASS, 0); }

void tray_changed(void) {
  HWND w = tray_window();

  if (w)
    PostMessage(w, TRAY_CHANGED, 0, 0);
}

static void tray_log(const char *text) {
  char line[96];

  if (!cmd->log[0])
    return;
  sprintf(line, "tray: %.80s", text);
  ess3d_log(cmd->log, line);
}

// --- the 32-bit shell -------------------------------------------------------

static int w32_load(void) {
  HMODULE k = GetModuleHandle("KERNEL");

  // Windows 95 and later, where 16-bit programs see version 3.95
  if (LOBYTE(LOWORD(GetVersion())) < 3 ||
      (LOBYTE(LOWORD(GetVersion())) == 3 && HIBYTE(LOWORD(GetVersion())) < 95))
    return -1;
  w32.load = (LOADLIB32)GetProcAddress(k, MAKEINTRESOURCE(513));
  w32.free = (FREELIB32)GetProcAddress(k, MAKEINTRESOURCE(514));
  w32.proc = (GETPROC32)GetProcAddress(k, MAKEINTRESOURCE(515));
  w32.call = GetProcAddress(k, MAKEINTRESOURCE(517));
  if (!w32.load || !w32.free || !w32.proc || !w32.call)
    return -1;
  w32.shell32 = w32.load("SHELL32.DLL", 0, 0);
  w32.user32 = w32.load("USER32.DLL", 0, 0);
  if (w32.shell32)
    w32.notify = w32.proc(w32.shell32, "Shell_NotifyIconA");
  if (w32.user32) {
    w32.foreground = w32.proc(w32.user32, "SetForegroundWindow");
    w32.getfg = w32.proc(w32.user32, "GetForegroundWindow");
  }
  return w32.notify ? 0 : -1;
}

static void w32_free(void) {
  if (w32.shell32)
    w32.free(w32.shell32);
  if (w32.user32)
    w32.free(w32.user32);
}

// Shell_NotifyIconA(msg, &nid), the pointer converted to a flat one
static BOOL notify(DWORD msg) {
  return (BOOL)((CALL32_2)w32.call)(msg, (DWORD)(void FAR *)&nid, w32.notify, 1,
                                    2);
}

// bring the panel to the front, so a click elsewhere closes it
static void to_front(HWND w) {
  if (w32.foreground)
    ((CALL32_1)w32.call)((DWORD)(UINT)w, w32.foreground, 0, 1);
  else
    SetActiveWindow(w);
}

// the window in front, as the low word of its handle: the 16-bit one
static UINT foreground(void) {
  return w32.getfg ? LOWORD(((CALL32_0)w32.call)(w32.getfg, 0, 0)) : 0;
}

// --- the setting ------------------------------------------------------------

static void read_setting(void) {
  int err = winio_begin();

  if (err == 0) {
    err = ess3d_read(&cur);
    winio_end();
  }
  cur_err = err < 0 ? err : 0;
}

static void state_text(char *buf, unsigned size) {
  if (cur_err)
    sprintf(buf, "3-D: %.50s", esshw_strerror(cur_err));
  else
    ess3d_text(&cur, buf, size);
}

// the icon and its tooltip follow the setting
static void icon_update(DWORD msg) {
  nid.icon =
      (DWORD)(UINT)(!cur_err && cur.enable && cur.run ? icon_on : icon_off);
  state_text(nid.tip, sizeof(nid.tip));
  notify(msg);
}

// --- the panel --------------------------------------------------------------

static int slider_field(int row) {
  return row == 0 ? F_3D_LEVEL : ess3d_reg_field(row - 1);
}

static int slider_value(int row) {
  return row == 0 ? cur.level : cur.reg[row - 1];
}

static int is_hex(int row) {
  return ess_fields[slider_field(row)].kind == K_HEX;
}

static HWND item(int id) { return GetDlgItem(panel, id); }

// a control of the panel, at dialog units
static HWND add(const char *cls, const char *text, DWORD style, int x, int y,
                int w, int h, int id) {
  RECT rc;
  HWND ctl;

  SetRect(&rc, x, y, x + w, y + h);
  MapDialogRect(panel, &rc);
  ctl = CreateWindow(cls, text, WS_CHILD | WS_VISIBLE | style, rc.left, rc.top,
                     rc.right - rc.left, rc.bottom - rc.top, panel, (HMENU)id,
                     inst, 0);
  if (ctl)
    SendMessage(ctl, WM_SETFONT, SendMessage(panel, WM_GETFONT, 0, 0), 0);
  return ctl;
}

static void edit_show(int row, int force) {
  char text[8];
  HWND e = item(IDC_P_EDIT + row);

  if (!force && GetFocus() == e)
    return; // don't change what's being typed
  sprintf(text, is_hex(row) ? "%02X" : "%d", slider_value(row));
  SetWindowText(e, text);
}

// the panel shows the setting
static void panel_fill(void) {
  char text[64];
  int r;

  if (!panel)
    return;
  CheckDlgButton(panel, IDC_P_ON, cur.enable);
  CheckDlgButton(panel, IDC_P_RUN, cur.run);
  CheckDlgButton(panel, IDC_P_MONO, cur.mono);
  CheckDlgButton(panel, IDC_P_LIMIT, cur.limit);
  for (r = 0; r < nsliders; r++) {
    if (r != drag)
      SetScrollPos(item(IDC_P_SLIDER + r), SB_CTL, slider_value(r), TRUE);
    edit_show(r, 0);
  }
  state_text(text, sizeof(text));
  SetDlgItemText(panel, IDC_P_STATUS, text);
}

static void changed(int err) {
  char text[64];

  cur_err = err < 0 ? err : 0;
  if (err < 0)
    read_setting();
  icon_update(NIM_MODIFY);
  panel_fill();
  state_text(text, sizeof(text));
  if (err == ESS3D_MISMATCH)
    strcat(text, " (not what was written)");
  tray_log(text);
}

// on, off, reset or defaults, as ess3d's commands do them
static void do_action(int op) {
  struct ess3d_cmd c;
  int err;

  memset(&c, 0, sizeof(c));
  c.act[0].op = (u8)op;
  c.nact = 1;
  err = winio_begin();
  if (err == 0) {
    err = ess3d_run(&c, &cur);
    winio_end();
  }
  changed(err);
}

// one field, the others kept
static void do_field(int field, int value) {
  int err = winio_begin();

  if (err == 0) {
    err = ess_field_write(field, (u8)value, 0);
    if (err == 0)
      err = ess3d_read(&cur);
    winio_end();
  }
  changed(err);
}

// what was typed in a slider's text field
static void edit_apply(int row) {
  const struct ess_field *f = &ess_fields[slider_field(row)];
  char text[16], msg[80], *end;
  long v;

  GetDlgItemText(panel, IDC_P_EDIT + row, text, sizeof(text));
  v = strtol(text, &end, is_hex(row) ? 16 : 10);
  if (is_hex(row) && (*end == 'h' || *end == 'H'))
    end++;
  if (!text[0] || *end || v < 0 || v > cat_max(f)) {
    MessageBeep(0);
    if (is_hex(row))
      sprintf(msg, "%.40s: %s is not 00h to %02Xh", f->label, text, cat_max(f));
    else
      sprintf(msg, "%.40s: %s is not 0 to %d", f->label, text, cat_max(f));
    SetDlgItemText(panel, IDC_P_STATUS, msg);
    edit_show(row, 1);
    return;
  }
  if (v != slider_value(row))
    do_field(slider_field(row), (int)v);
  edit_show(row, 1);
}

static void panel_scroll(HWND sb, int code, int pos) {
  int row = GetDlgCtrlID(sb) - IDC_P_SLIDER;
  int v, max;
  char text[8];

  if (row < 0 || row >= nsliders)
    return;
  v = GetScrollPos(sb, SB_CTL);
  max = cat_max(&ess_fields[slider_field(row)]);
  // steps of one: arrows, keys and clicks beside the thumb
  switch (code) {
  case SB_LINEUP:
  case SB_PAGEUP:
    v--;
    break;
  case SB_LINEDOWN:
  case SB_PAGEDOWN:
    v++;
    break;
  case SB_TOP:
    v = 0;
    break;
  case SB_BOTTOM:
    v = max;
    break;
  case SB_THUMBTRACK:
  case SB_THUMBPOSITION:
    v = pos;
    break;
  case SB_ENDSCROLL:
    if (drag == row) {
      drag = -1;
      do_field(slider_field(row), GetScrollPos(sb, SB_CTL));
    }
    return;
  default:
    return;
  }
  if (v < 0)
    v = 0;
  if (v > max)
    v = max;
  SetScrollPos(sb, SB_CTL, v, TRUE);
  sprintf(text, is_hex(row) ? "%02X" : "%d", v);
  SetDlgItemText(panel, IDC_P_EDIT + row, text);
  if (code == SB_THUMBTRACK && !winio_can_poll()) {
    // direct I/O with ES1869.VXD, only write when the thumb is released
    drag = row;
    return;
  }
  drag = -1;
  if (v != slider_value(row))
    do_field(slider_field(row), v);
}

// the controls, from the catalog
static void panel_build(void) {
  const struct ess_field *f;
  char text[24];
  RECT rc;
  int r, y;

  add("BUTTON", ess_fields[F_3D_EN].label, WS_TABSTOP | BS_AUTOCHECKBOX, 6, 4,
      130, 10, IDC_P_ON);
  add("BUTTON", ess_fields[F_3D_RUN].label, WS_TABSTOP | BS_AUTOCHECKBOX, 6, 17,
      130, 10, IDC_P_RUN);
  add("BUTTON", ess_fields[F_3D_MONO].label, WS_TABSTOP | BS_AUTOCHECKBOX, 6,
      30, 130, 10, IDC_P_MONO);
  add("BUTTON", ess_fields[F_3D_LIMIT].label, WS_TABSTOP | BS_AUTOCHECKBOX, 6,
      43, 130, 10, IDC_P_LIMIT);
  add("BUTTON", "&Reset", WS_TABSTOP | BS_PUSHBUTTON, P_W - 56, 4, 50, 14,
      IDC_P_RESET);
  nsliders = 1 + ess3d_regs();
  for (r = 0; r < nsliders; r++) {
    f = &ess_fields[slider_field(r)];
    y = P_TOP + r * P_ROW;
    add("STATIC", f->label, SS_LEFTNOWORDWRAP | SS_NOPREFIX, 6, y + 2, 70, 9,
        IDC_P_LABEL + r);
    // the text field, then the slider on its right
    add("EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_UPPERCASE, 78,
        y, 22, 12, IDC_P_EDIT + r);
    SendMessage(item(IDC_P_EDIT + r), EM_LIMITTEXT, 4, 0);
    add("SCROLLBAR", "", WS_TABSTOP | SBS_HORZ, 103, y + 1, 77, 10,
        IDC_P_SLIDER + r);
    SetScrollRange(item(IDC_P_SLIDER + r), SB_CTL, 0, cat_max(f), FALSE);
    if (f->kind == K_HEX)
      sprintf(text, "00h-%02Xh", cat_max(f));
    else
      sprintf(text, "of %d", cat_max(f));
    add("STATIC", text, SS_LEFTNOWORDWRAP, 183, y + 2, P_W - 186, 9,
        IDC_P_RANGE + r);
  }
  y = P_TOP + nsliders * P_ROW + 2;
  add("STATIC", "", SS_LEFTNOWORDWRAP | SS_NOPREFIX, 6, y, P_W - 12, 9,
      IDC_P_STATUS);
  y += 12;
  add("BUTTON", "Driver &defaults", WS_TABSTOP | BS_PUSHBUTTON, 6, y, 68, 14,
      IDC_P_DEFAULTS);
  add("BUTTON", "&Close tray icon", WS_TABSTOP | BS_PUSHBUTTON, P_W - 74, y, 68,
      14, IDC_P_CLOSE);
  // fit the panel to its rows
  SetRect(&rc, 0, 0, P_W, y + 18);
  MapDialogRect(panel, &rc);
  AdjustWindowRect(&rc, GetWindowLong(panel, GWL_STYLE), FALSE);
  SetWindowPos(panel, 0, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void panel_hide(void) {
  KillTimer(tray, PANEL_TIMER);
  if (panel && IsWindowVisible(panel)) {
    ShowWindow(panel, SW_HIDE);
    hidden_at = GetTickCount();
  }
}

// close the panel when another window comes to the front: first note
// the one in front when it opened (or the panel), then watch for a change
static void panel_check(void) {
  UINT fg = foreground();

  if (!fg)
    return;
  if (fg == (UINT)panel || !panel_fg)
    panel_fg = fg;
  else if (fg != panel_fg)
    panel_hide();
}

// the panel above the mouse, where the icon was clicked
static void panel_show(void) {
  RECT rc;
  POINT pt;
  int w, h, sw, sh, x, y;

  if (!panel)
    return;
  read_setting();
  icon_update(NIM_MODIFY);
  panel_fill();
  GetWindowRect(panel, &rc);
  w = rc.right - rc.left;
  h = rc.bottom - rc.top;
  sw = GetSystemMetrics(SM_CXSCREEN);
  sh = GetSystemMetrics(SM_CYSCREEN);
  GetCursorPos(&pt);
  x = pt.x - w;
  y = pt.y - h;
  if (x > sw - w)
    x = sw - w;
  if (y > sh - h)
    y = sh - h;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  SetWindowPos(panel, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
  to_front(panel);
  panel_fg = 0;
  SetTimer(tray, PANEL_TIMER, PANEL_MS, 0);
  SetFocus(item(IDC_P_ON));
  tray_log("panel");
}

BOOL CALLBACK __export panel_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
  HWND focus;
  int id, code;

  switch (msg) {
  case WM_INITDIALOG:
    panel = dlg;
    panel_build();
    return TRUE;
  case WM_HSCROLL:
    panel_scroll((HWND)HIWORD(lp), wp, LOWORD(lp));
    return TRUE;
  case WM_COMMAND:
    id = wp;
    code = HIWORD(lp);
    if (id == IDC_P_ON && code == BN_CLICKED) {
      do_action(IsDlgButtonChecked(dlg, id) ? ESS3D_ON : ESS3D_OFF);
    } else if (id == IDC_P_RUN && code == BN_CLICKED) {
      do_field(F_3D_RUN, IsDlgButtonChecked(dlg, id));
    } else if (id == IDC_P_MONO && code == BN_CLICKED) {
      do_field(F_3D_MONO, IsDlgButtonChecked(dlg, id));
    } else if (id == IDC_P_LIMIT && code == BN_CLICKED) {
      do_field(F_3D_LIMIT, IsDlgButtonChecked(dlg, id));
    } else if (id == IDC_P_RESET) {
      do_action(ESS3D_RESET);
    } else if (id == IDC_P_DEFAULTS) {
      do_action(ESS3D_DEFAULTS);
    } else if (id == IDC_P_CLOSE) {
      DestroyWindow(tray);
    } else if (id >= IDC_P_EDIT && id < IDC_P_EDIT + nsliders &&
               code == EN_KILLFOCUS) {
      edit_apply(id - IDC_P_EDIT);
    } else if (id == IDOK) {
      // Enter in a text field
      focus = GetFocus();
      id = GetDlgCtrlID(focus);
      if (id >= IDC_P_EDIT && id < IDC_P_EDIT + nsliders)
        edit_apply(id - IDC_P_EDIT);
    } else if (id == IDCANCEL) {
      panel_hide();
    }
    return TRUE;
  }
  return FALSE;
}

// --- the tray window --------------------------------------------------------

LRESULT CALLBACK __export tray_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  struct ess3d_state old;

  if (msg == taskbar_created && msg) {
    notify(NIM_ADD);
    return 0;
  }
  switch (msg) {
  case WM_TRAYICON:
    if (lp != WM_RBUTTONUP && lp != WM_LBUTTONUP)
      return 0;
    // a second click closes it, also when pressing the button on the
    // taskbar just closed it
    if (panel && IsWindowVisible(panel))
      panel_hide();
    else if (GetTickCount() - hidden_at > 500)
      panel_show();
    return 0;
  case TRAY_PANEL:
    panel_show();
    return 0;
  case TRAY_CHANGED:
    read_setting();
    icon_update(NIM_MODIFY);
    panel_fill();
    return 0;
  case WM_TIMER:
    if (wp == PANEL_TIMER) {
      panel_check();
      return 0;
    }
    // with the register interface, reading has no side effects
    old = cur;
    read_setting();
    if (memcmp(&old, &cur, sizeof(cur))) {
      icon_update(NIM_MODIFY);
      panel_fill();
    }
    return 0;
  case WM_CLOSE:
    DestroyWindow(w);
    return 0;
  case WM_DESTROY:
    KillTimer(w, POLL_TIMER);
    KillTimer(w, PANEL_TIMER);
    notify(NIM_DELETE);
    if (panel)
      DestroyWindow(panel);
    panel = 0; // the message loop runs until WM_QUIT
    tray_log("closed");
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(w, msg, wp, lp);
}

int tray_run(HINSTANCE hinst, const struct ess3d_cmd *c) {
  WNDCLASS wc;
  DLGPROC proc;
  MSG msg;

  inst = hinst;
  cmd = c;
  if (w32_load() < 0) {
    if (!c->quiet)
      MessageBox(0, "The tray icon needs Windows 95 or later.", TRAY_TITLE,
                 MB_OK | MB_ICONEXCLAMATION);
    tray_log("no tray on this Windows");
    return 2;
  }
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = tray_proc;
  wc.hInstance = inst;
  wc.lpszClassName = TRAY_CLASS;
  RegisterClass(&wc);
  tray = CreateWindow(TRAY_CLASS, TRAY_TITLE, WS_OVERLAPPED, 0, 0, 0, 0, 0, 0,
                      inst, 0);
  if (!tray) {
    w32_free();
    return 2;
  }
  taskbar_created = RegisterWindowMessage("TaskbarCreated");
  icon_on = LoadIcon(inst, "ICON_ON");
  icon_off = LoadIcon(inst, "ICON_OFF");
  proc = (DLGPROC)MakeProcInstance((FARPROC)panel_proc, inst);
  CreateDialog(inst, "PANEL", 0, proc);

  memset(&nid, 0, sizeof(nid));
  nid.size = sizeof(nid);
  nid.hwnd = (DWORD)(UINT)tray;
  nid.id = TRAY_ID;
  nid.flags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.message = WM_TRAYICON;
  read_setting();
  icon_update(NIM_ADD);
  tray_log("started");
  if (winio_can_poll())
    SetTimer(tray, POLL_TIMER, POLL_MS, 0);

  while (GetMessage(&msg, 0, 0, 0)) {
    // the panel's keys: Tab, Enter and Esc
    if (panel && IsDialogMessage(panel, &msg))
      continue;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  FreeProcInstance((FARPROC)proc);
  w32_free();
  return 0;
}
