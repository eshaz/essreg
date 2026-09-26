/*
 * page.c -- the page area of the main dialog (see essctl.h).
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#include <string.h>

#include "essctl.h"
#include "resource.h"

#define MAX_CTLS 420

int area_w, area_h;
static int area_x, area_y; /* page area origin in dialog units */
static HWND ctls[MAX_CTLS];
static int nctls;
static int cur_kind = -1, cur_arg;

void dlu_to_px(int *x, int *y) {
  RECT rc;

  rc.left = rc.top = 0;
  rc.right = *x;
  rc.bottom = *y;
  MapDialogRect(g_main, &rc);
  *x = rc.right;
  *y = rc.bottom;
}

/* the placeholder IDC_PAGEAREA in the template marks the area; its size in
 * dialog units comes from converting its pixel size back */
void page_init(void) {
  RECT rc;
  POINT p;
  int unit_x = 100, unit_y = 100;
  HWND frame = GetDlgItem(g_main, IDC_PAGEAREA);

  GetWindowRect(frame, &rc);
  p.x = rc.left;
  p.y = rc.top;
  ScreenToClient(g_main, &p);
  dlu_to_px(&unit_x, &unit_y);
  area_x = (int)((long)p.x * 100 / unit_x);
  area_y = (int)((long)p.y * 100 / unit_y);
  area_w = (int)((long)(rc.right - rc.left) * 100 / unit_x);
  area_h = (int)((long)(rc.bottom - rc.top) * 100 / unit_y);
}

HWND page_control(const char *cls, const char *text, DWORD style, int x,
                  int y, int w, int h, int id) {
  HWND ctl;
  int px = area_x + x, py = area_y + y;

  dlu_to_px(&px, &py);
  dlu_to_px(&w, &h);
  ctl = CreateWindow(cls, text, WS_CHILD | style, px, py, w, h, g_main,
                     (HMENU)id, g_inst, 0);
  if (!ctl)
    return 0;
  SendMessage(ctl, WM_SETFONT, (WPARAM)g_font, 0);
  if (nctls < MAX_CTLS)
    ctls[nctls++] = ctl;
  return ctl;
}

void page_move(HWND ctl, int x, int y, int w, int h, int show) {
  int px = area_x + x, py = area_y + y;

  if (!ctl)
    return;
  if (!show) {
    ShowWindow(ctl, SW_HIDE);
    return;
  }
  dlu_to_px(&px, &py);
  dlu_to_px(&w, &h);
  SetWindowPos(ctl, 0, px, py, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  ShowWindow(ctl, SW_SHOWNA);
}

/* the page area's scroll bar: rows first..first+visible of count */
void page_scrollbar(int first, int count, int visible) {
  HWND sb = GetDlgItem(g_main, IDC_PGSCROLL);

  if (count <= visible) {
    ShowWindow(sb, SW_HIDE);
    return;
  }
  SetScrollRange(sb, SB_CTL, 0, count - visible, FALSE);
  SetScrollPos(sb, SB_CTL, first, TRUE);
  ShowWindow(sb, SW_SHOWNA);
}

static void destroy_controls(void) {
  int i;

  /* hide first: destroying a focused control moves the focus through the
   * remaining ones */
  if (GetFocus() && GetParent(GetFocus()) == g_main &&
      GetDlgCtrlID(GetFocus()) >= IDC_PG_EDIT)
    SetFocus(GetDlgItem(g_main, IDC_PAGES));
  for (i = 0; i < nctls; i++)
    ShowWindow(ctls[i], SW_HIDE);
  for (i = 0; i < nctls; i++)
    DestroyWindow(ctls[i]);
  nctls = 0;
  ShowWindow(GetDlgItem(g_main, IDC_PGSCROLL), SW_HIDE);
}

void page_show(int kind, int arg) {
  HCURSOR old = SetCursor(LoadCursor(0, IDC_WAIT));

  SendMessage(g_main, WM_SETREDRAW, FALSE, 0);
  destroy_controls();
  cur_kind = kind;
  cur_arg = arg;
  set_help("");
  switch (kind) {
  case PK_INFO:
    info_create();
    break;
  case PK_FIELDS:
    fields_create(arg);
    break;
  case PK_RAW:
    raw_create();
    break;
  case PK_ESFM:
    esfm_create();
    break;
  }
  SendMessage(g_main, WM_SETREDRAW, TRUE, 0);
  RedrawWindow(g_main, 0, 0,
               RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
  page_refresh(REFRESH_USER);
  SetCursor(old);
}

void page_refresh(int how) {
  switch (cur_kind) {
  case PK_INFO:
    info_refresh(how);
    break;
  case PK_FIELDS:
    fields_refresh(how);
    break;
  case PK_RAW:
    raw_refresh(how);
    break;
  case PK_ESFM:
    esfm_refresh(how);
    break;
  }
}

int page_kind(void) { return cur_kind; }

void page_command(int id, int code, HWND ctl) {
  switch (cur_kind) {
  case PK_FIELDS:
    fields_command(id, code, ctl);
    break;
  case PK_RAW:
    raw_command(id, code, ctl);
    break;
  case PK_ESFM:
    esfm_command(id, code, ctl);
    break;
  }
}

void page_hscroll(int code, int pos, HWND ctl) {
  if (cur_kind == PK_FIELDS)
    fields_hscroll(code, pos, ctl);
}

void page_vscroll(int code, int pos) {
  if (cur_kind == PK_FIELDS)
    fields_vscroll(code, pos);
}

/* a click on the dialog itself (statics let clicks through) */
void page_click(int x, int y) {
  int ux = 100, uy = 100;

  dlu_to_px(&ux, &uy);
  x = (int)((long)x * 100 / ux) - area_x;
  y = (int)((long)y * 100 / uy) - area_y;
  if (cur_kind == PK_FIELDS && x >= 0 && x < area_w && y >= 0 && y < area_h)
    fields_click(y);
}

void page_focus(HWND ctl) {
  if (cur_kind == PK_FIELDS && ctl && GetParent(ctl) == g_main)
    fields_focus(ctl);
}
