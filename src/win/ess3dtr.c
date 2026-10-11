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
 * The panel is laid out like ESS's own 3-D panel, ES938 Controls of its
 * OS/2 drivers (driver/es1869f/OS2/INSTALL/ES938.EXE): a group box for each
 * stage of the sound, a row of its switches, and rows of a name and a
 * slider with a word at each end. Every bit has its own control, so every
 * value of the registers can be set. The Spatializer group has the effect
 * on (50h bit 3), held in reset (bit 2 clear), widening the stereo (the
 * program's S) or mono (an S made from the mono sum, bit 1), and its
 * effect, 52h. The automatic effect level group has the limit (bit 0,
 * ESS's 3D Limit), which lowers the effect while its S is over a level
 * against the M and raises it again once it is under, and its fields of
 * 54h-5Ah (ess3d_reg_field), named by what each does to the effect. A field
 * added to the catalog gets a row in that group with its catalog name.
 *
 * Each row has a text field and a slider on its right, in steps of one
 * over the field's range: decimal, or hex for a whole register, written on
 * Enter or when the field loses the focus. Right of the slider is what the
 * value does, as one card measured it (cat_unit): the boost in dB, a level
 * against the M, a speed in dB a second or a level in dBFS. It follows the
 * slider as it moves, and the rise follows the fall, since the rise is the
 * fall over h + 1.
 *
 * The panel closes when another window comes to the front, like the
 * tray's menus, or on a second click on the icon. It watches the
 * foreground window instead of its own activation, since Windows may not
 * let it take the focus. With the register interface of the rebuilt
 * ES1869.VXD, the icon also checks the setting every few seconds, so it
 * follows the Windows mixer.
 *
 * Explorer may not be ready when the tray starts from the StartUp folder,
 * and on Windows 98 it restarts after a crash: an icon the shell refused
 * is added again every 2 s until it's there, and again when the new
 * taskbar says "TaskbarCreated".
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
#define ADD_TIMER 3 // the shell refused the icon: try again
#define ADD_MS 2000

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

// panel controls: the switches and buttons, then a row of each slider:
// its name, text field, end words, slider and units
#define IDC_P_ON 101     // 50h bit 3
#define IDC_P_HOLD 102   // bit 2 clear
#define IDC_P_STEREO 103 // bit 1 clear, or set for mono
#define IDC_P_MONO 104
#define IDC_P_LIMIT 105 // bit 0
#define IDC_P_RESET 106
#define IDC_P_DEFAULTS 107
#define IDC_P_CLOSE 108
#define IDC_P_STATUS 109
#define IDC_P_GROUP 110 // + 0 and 1: the group boxes
#define IDC_P_LABEL 200 // + row
#define IDC_P_SLIDER 300
#define IDC_P_EDIT 400
#define IDC_P_RANGE 500
#define IDC_P_LO 600
#define IDC_P_HI 700
#define MAX_SLIDERS (1 + ESS3D_MAX_REGS)

// panel layout in dialog units: a group's radio buttons, then its rows of
// name, text field, end word, slider, end word and units
#define P_W 270
#define P_ROW 14
#define P_RADIO 11 // a group's switches, under its top
#define P_ROWS 26  // and its first row
#define P_NAME 10
#define P_EDIT 58
#define P_LO 82
#define P_SLIDER 104
#define P_HI 186
#define P_UNIT 208

// the rows, named for what they do to the sound, with the words at the
// ends of their sliders as ES938 Controls has Min and Max
static const struct {
  int field;
  const char *name, *lo, *hi;
} names[] = {{F_3D_LEVEL, "Effect:", "Min", "Max"},
             {F_3D_R56, "Lower above:", "Low", "High"},
             {F_3D_R54, "Raise below:", "Low", "High"},
             {F_3D_R5A, "Fixed level:", "None", "High"},
             {F_3D_FALL, "Lower speed:", "Fast", "Slow"},
             {F_3D_RISE, "Raise speed:", "Fast", "Slow"}};

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
static int row_field[MAX_SLIDERS]; // a row's field
static int row_reg[MAX_SLIDERS];   // its ess3d_reg_field index, -1 for 52h
static int row_name[MAX_SLIDERS];  // its entry in names, -1 if none
static int shown;                  // the shell has the icon
static int drag = -1;              // slider held without writing (direct I/O)
static UINT panel_fg;              // the window in front while the panel is up
static DWORD hidden_at;            // GetTickCount() when the panel last hid

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

// the icon and its tooltip follow the setting; one the shell doesn't
// have is added, and one it refuses is tried again on a timer
static void icon_update(void) {
  nid.icon =
      (DWORD)(UINT)(!cur_err && cur.enable && cur.run ? icon_on : icon_off);
  state_text(nid.tip, sizeof(nid.tip));
  if (shown && notify(NIM_MODIFY))
    return;
  // gone without "TaskbarCreated", or never added
  shown = notify(NIM_ADD) != 0;
  if (shown)
    KillTimer(tray, ADD_TIMER);
  else if (!SetTimer(tray, ADD_TIMER, ADD_MS, 0))
    tray_log("no timer left to add the icon again");
}

// --- the panel --------------------------------------------------------------

static int slider_field(int row) { return row_field[row]; }

static int slider_value(int row) {
  return row_reg[row] < 0 ? cur.level : cur.reg[row_reg[row]];
}

static int is_hex(int row) {
  return ess_fields[slider_field(row)].kind == K_HEX;
}

// the rows in the order of names, then the fields that names doesn't have
static void rows_find(void) {
  unsigned j;
  int i, r;

  nsliders = 0;
  for (j = 0; j < sizeof(names) / sizeof(names[0]); j++)
    for (i = -1; i < ess3d_regs(); i++)
      if ((i < 0 ? F_3D_LEVEL : ess3d_reg_field(i)) == names[j].field) {
        row_field[nsliders] = names[j].field;
        row_reg[nsliders] = i;
        row_name[nsliders++] = (int)j;
      }
  for (i = 0; i < ess3d_regs() && nsliders < MAX_SLIDERS; i++) {
    for (r = 0; r < nsliders && row_reg[r] != i; r++)
      ;
    if (r == nsliders) {
      row_field[nsliders] = ess3d_reg_field(i);
      row_reg[nsliders] = i;
      row_name[nsliders++] = -1;
    }
  }
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

// the byte of a row's register with the row's field at value, the other
// fields of it as the chip has them
static u8 row_byte(int row, int value) {
  u8 raw = row_reg[row] < 0 ? 0 : (u8)ess3d_reg_byte(&cur, row_reg[row]);

  return cat_set(&ess_fields[slider_field(row)], raw, (u8)value);
}

// right of the slider: what the value does, or else its range; each row
// of the same register too, as the rise follows the fall of 58h
static void unit_show(int row, int value) {
  const struct ess_field *f;
  char text[24];
  u8 raw = row_byte(row, value);
  int r;

  for (r = 0; r < nsliders; r++) {
    f = &ess_fields[slider_field(r)];
    if (f->reg != ess_fields[slider_field(row)].reg)
      continue;
    if (!cat_unit(f, raw, text, sizeof(text)))
      sprintf(text, f->kind == K_HEX ? "00h-%02Xh" : "of %d", cat_max(f));
    SetDlgItemText(panel, IDC_P_RANGE + r, text);
  }
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
  CheckDlgButton(panel, IDC_P_HOLD, !cur.run);
  CheckRadioButton(panel, IDC_P_STEREO, IDC_P_MONO,
                   cur.mono ? IDC_P_MONO : IDC_P_STEREO);
  CheckDlgButton(panel, IDC_P_LIMIT, cur.limit);
  for (r = 0; r < nsliders; r++) {
    if (r != drag) {
      SetScrollPos(item(IDC_P_SLIDER + r), SB_CTL, slider_value(r), TRUE);
      unit_show(r, slider_value(r));
    }
    edit_show(r, 0);
  }
  state_text(text, sizeof(text));
  SetDlgItemText(panel, IDC_P_STATUS, text);
}

static void changed(int err) {
  char text[96];

  cur_err = err < 0 ? err : 0;
  if (err < 0)
    read_setting();
  icon_update();
  panel_fill();
  state_text(text, sizeof(text));
  if (err == ESS3D_MISMATCH)
    strncat(text, " (not what was written)", sizeof(text) - 1 - strlen(text));
  tray_log(text);
}

// reset or defaults, as ess3d's commands do them
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
  unit_show(row, v);
  if (code == SB_THUMBTRACK && !winio_can_poll()) {
    // direct I/O with ES1869.VXD, only write when the thumb is released
    drag = row;
    return;
  }
  drag = -1;
  if (v != slider_value(row))
    do_field(slider_field(row), v);
}

// a row at y: its name, text field, end words, slider and units
static void row_build(int r, int y) {
  const struct ess_field *f = &ess_fields[slider_field(r)];
  const char *lo = "0", *hi = "Max";

  if (row_name[r] >= 0) {
    add("STATIC", names[row_name[r]].name, SS_LEFTNOWORDWRAP | SS_NOPREFIX,
        P_NAME, y + 2, P_EDIT - 2 - P_NAME, 9, IDC_P_LABEL + r);
    lo = names[row_name[r]].lo;
    hi = names[row_name[r]].hi;
  } else {
    // a field that names doesn't have: its catalog name, without "3-D "
    add("STATIC", strncmp(f->label, "3-D ", 4) ? f->label : f->label + 4,
        SS_LEFTNOWORDWRAP | SS_NOPREFIX, P_NAME, y + 2, P_EDIT - 2 - P_NAME, 9,
        IDC_P_LABEL + r);
  }
  add("EDIT", "",
      WS_GROUP | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_UPPERCASE, P_EDIT,
      y, 22, 12, IDC_P_EDIT + r);
  SendMessage(item(IDC_P_EDIT + r), EM_LIMITTEXT, 4, 0);
  add("STATIC", lo, SS_RIGHT | SS_NOPREFIX, P_LO, y + 2, P_SLIDER - 2 - P_LO, 9,
      IDC_P_LO + r);
  add("SCROLLBAR", "", WS_TABSTOP | SBS_HORZ, P_SLIDER, y + 1,
      P_HI - 2 - P_SLIDER, 10, IDC_P_SLIDER + r);
  SetScrollRange(item(IDC_P_SLIDER + r), SB_CTL, 0, cat_max(f), FALSE);
  add("STATIC", hi, SS_LEFTNOWORDWRAP | SS_NOPREFIX, P_HI, y + 2,
      P_UNIT - 2 - P_HI, 9, IDC_P_HI + r);
  add("STATIC", "", SS_LEFTNOWORDWRAP | SS_NOPREFIX, P_UNIT, y + 2,
      P_W - 8 - P_UNIT, 9, IDC_P_RANGE + r);
}

// a group box from y with its rows from row r0 to r1 - 1, its bottom
static int group_build(int id, const char *title, int y, int r0, int r1) {
  int r, h = P_ROWS + (r1 - r0) * P_ROW + 3;

  add("BUTTON", title, BS_GROUPBOX, 4, y, P_W - 8, h, id);
  for (r = r0; r < r1; r++)
    row_build(r, y + P_ROWS + (r - r0) * P_ROW);
  return y + h;
}

// the controls: the Spatializer, then the automatic level of its effect, as
// the sound goes through them
static void panel_build(void) {
  RECT rc;
  int y, ry;

  rows_find();
  // the effect's bits, and the row of 52h
  ry = 2 + P_RADIO;
  add("BUTTON", "O&n", WS_GROUP | WS_TABSTOP | BS_AUTOCHECKBOX, P_NAME, ry, 30,
      10, IDC_P_ON);
  add("BUTTON", "&Hold in reset", WS_GROUP | WS_TABSTOP | BS_AUTOCHECKBOX,
      P_NAME + 34, ry, 62, 10, IDC_P_HOLD);
  add("BUTTON", "&Stereo", WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON,
      P_NAME + 100, ry, 38, 10, IDC_P_STEREO);
  add("BUTTON", "&Mono", BS_AUTORADIOBUTTON, P_NAME + 140, ry, 36, 10,
      IDC_P_MONO);
  add("BUTTON", "&Reset", WS_GROUP | WS_TABSTOP | BS_PUSHBUTTON, P_W - 62,
      ry - 2, 50, 14, IDC_P_RESET);
  y = group_build(IDC_P_GROUP, "Spatializer", 2, 0, 1);
  // the automatic level: the limit, and the rows of 54h-5Ah
  ry = y + 4 + P_RADIO;
  add("BUTTON", "On", WS_GROUP | WS_TABSTOP | BS_AUTOCHECKBOX, P_NAME, ry, 30,
      10, IDC_P_LIMIT);
  y = group_build(IDC_P_GROUP + 1, "Automatic effect level", y + 4, 1,
                  nsliders);
  y += 3;
  add("STATIC", "", SS_LEFTNOWORDWRAP | SS_NOPREFIX, 6, y, P_W - 12, 9,
      IDC_P_STATUS);
  y += 12;
  add("BUTTON", "Driver &defaults", WS_GROUP | WS_TABSTOP | BS_PUSHBUTTON, 6, y,
      68, 14, IDC_P_DEFAULTS);
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
  icon_update();
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
  if (!SetTimer(tray, PANEL_TIMER, PANEL_MS, 0))
    tray_log("no timer left: the panel closes with Esc or a click on it");
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
      do_field(F_3D_EN, IsDlgButtonChecked(dlg, id));
    } else if (id == IDC_P_HOLD && code == BN_CLICKED) {
      do_field(F_3D_RUN, !IsDlgButtonChecked(dlg, id));
    } else if ((id == IDC_P_STEREO || id == IDC_P_MONO) && code == BN_CLICKED) {
      do_field(F_3D_MONO, id == IDC_P_MONO);
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
      // Esc drops what was typed: hiding takes the focus, which writes it
      focus = GetFocus();
      id = GetDlgCtrlID(focus);
      if (id >= IDC_P_EDIT && id < IDC_P_EDIT + nsliders)
        edit_show(id - IDC_P_EDIT, 1);
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
    // a new taskbar, after Explorer restarted
    shown = 0;
    icon_update();
    tray_log("icon added again");
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
    icon_update();
    panel_fill();
    return 0;
  case WM_TIMER:
    if (wp == PANEL_TIMER) {
      panel_check();
      return 0;
    }
    if (wp == ADD_TIMER) {
      icon_update();
      if (shown)
        tray_log("icon added");
      return 0;
    }
    // with the register interface, reading has no side effects
    old = cur;
    read_setting();
    if (memcmp(&old, &cur, sizeof(cur))) {
      icon_update();
      panel_fill();
    }
    return 0;
  case WM_CLOSE:
    DestroyWindow(w);
    return 0;
  case WM_DESTROY:
    KillTimer(w, POLL_TIMER);
    KillTimer(w, PANEL_TIMER);
    KillTimer(w, ADD_TIMER);
    if (shown)
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

HWND tray_claim(HINSTANCE hinst) {
  WNDCLASS wc;

  inst = hinst;
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = tray_proc;
  wc.hInstance = inst;
  wc.lpszClassName = TRAY_CLASS;
  RegisterClass(&wc);
  tray = CreateWindow(TRAY_CLASS, TRAY_TITLE, WS_OVERLAPPED, 0, 0, 0, 0, 0, 0,
                      inst, 0);
  return tray;
}

int tray_run(const struct ess3d_cmd *c) {
  DLGPROC proc;
  MSG msg;

  cmd = c;
  if (w32_load() < 0) {
    if (!c->quiet)
      MessageBox(0, "The tray icon needs Windows 95 or later.", TRAY_TITLE,
                 MB_OK | MB_ICONEXCLAMATION);
    tray_log("no tray on this Windows");
    DestroyWindow(tray);
    return 2;
  }
  taskbar_created = RegisterWindowMessage("TaskbarCreated");
  icon_on = LoadIcon(inst, "ICON_ON");
  icon_off = LoadIcon(inst, "ICON_OFF");
  proc = (DLGPROC)MakeProcInstance((FARPROC)panel_proc, inst);
  // owned by the tray's window, so the panel gets no taskbar button
  if (!CreateDialog(inst, "PANEL", tray, proc)) {
    tray_log("not enough memory for the panel");
    DestroyWindow(tray);
    FreeProcInstance((FARPROC)proc);
    w32_free();
    return 2;
  }

  memset(&nid, 0, sizeof(nid));
  nid.size = sizeof(nid);
  nid.hwnd = (DWORD)(UINT)tray;
  nid.id = TRAY_ID;
  nid.flags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.message = WM_TRAYICON;
  read_setting();
  icon_update();
  tray_log(shown ? "started" : "started, the icon comes when the taskbar does");
  if (winio_can_poll() && !SetTimer(tray, POLL_TIMER, POLL_MS, 0))
    tray_log("no timer left: the icon won't follow the Windows mixer");

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
