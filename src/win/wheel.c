/*
 * The mouse wheel on the sliders of essctl and ess3d (see wheel.h).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdlib.h>

#include "wheel.h"

#define WM_MOUSEWHEEL 0x020A
#define WHEEL_DELTA 120 // a notch
#define KEYS_MAX 0x1F   // the MK_* keys, in a 16-bit wParam

// the generic thunks: CallProc32W(args..., proc, pointer mask, count)
typedef DWORD(FAR PASCAL *LOADLIB32)(LPCSTR, DWORD, DWORD);
typedef DWORD(FAR PASCAL *GETPROC32)(DWORD, LPCSTR);
typedef BOOL(FAR PASCAL *FREELIB32)(DWORD);
typedef DWORD(FAR PASCAL *CALL32_1)(DWORD, DWORD, DWORD, DWORD);
typedef DWORD(FAR PASCAL *CALL32_5)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                    DWORD, DWORD);

// a MSG as USER32 has it
struct msg32 {
  DWORD hwnd, message, wparam, lparam, time;
  long x, y;
};

int (*wheel_other)(POINT pt, int steps);

static struct {
  FREELIB32 free;
  FARPROC call;
  DWORD user32;
  DWORD peek;     // PeekMessageA
  DWORD dispatch; // DispatchMessageA
} w32;
static struct msg32 m32;
static UINT roll_msg; // MSWHEEL_ROLLMSG
static int native;    // a WM_MOUSEWHEEL came, so MSWHEEL_ROLLMSG repeats it
static int rest;      // a turn's part under a notch, for the next turn
static HWND rest_on;  // and the window it was turned on
static int modal_done, modal_result;

void wheel_init(void) {
  HMODULE k = GetModuleHandle("KERNEL");
  LOADLIB32 load;
  GETPROC32 proc;

  roll_msg = RegisterWindowMessage("MSWHEEL_ROLLMSG");
  // Windows 95 and later, where 16-bit programs see version 3.95
  if (LOBYTE(LOWORD(GetVersion())) < 3 ||
      (LOBYTE(LOWORD(GetVersion())) == 3 && HIBYTE(LOWORD(GetVersion())) < 95))
    return;
  load = (LOADLIB32)GetProcAddress(k, MAKEINTRESOURCE(513));
  w32.free = (FREELIB32)GetProcAddress(k, MAKEINTRESOURCE(514));
  proc = (GETPROC32)GetProcAddress(k, MAKEINTRESOURCE(515));
  w32.call = GetProcAddress(k, MAKEINTRESOURCE(517));
  if (!load || !w32.free || !proc || !w32.call)
    return;
  w32.user32 = load("USER32.DLL", 0, 0);
  if (!w32.user32)
    return;
  w32.peek = proc(w32.user32, "PeekMessageA");
  w32.dispatch = proc(w32.user32, "DispatchMessageA");
  if (!w32.peek || !w32.dispatch)
    wheel_free();
}

void wheel_free(void) {
  if (w32.user32)
    w32.free(w32.user32);
  w32.user32 = w32.peek = w32.dispatch = 0;
}

// a window of this program that takes input: it and the windows it is in
// are enabled, up to its top-level window, which a modal dialog disables
static int usable(HWND w) {
  if (!w || GetWindowTask(w) != GetCurrentTask())
    return 0;
  for (;;) {
    if (!IsWindowEnabled(w))
      return 0;
    if (!(GetWindowLong(w, GWL_STYLE) & WS_CHILD))
      return 1;
    w = GetParent(w);
  }
}

// a slider: a horizontal scroll bar control, shown
static int is_slider(HWND w) {
  char cls[16];

  if (!IsWindowVisible(w))
    return 0;
  GetClassName(w, cls, sizeof(cls));
  return !lstrcmpi(cls, "ScrollBar") &&
         !(GetWindowLong(w, GWL_STYLE) & SBS_VERT);
}

// steps clicks on a slider's arrows, then the end of the scroll, as the
// slider sends them to its window
static void scroll(HWND sb, int steps) {
  HWND to = GetParent(sb);
  UINT code = steps > 0 ? SB_LINEDOWN : SB_LINEUP;

  if (steps < 0)
    steps = -steps;
  while (steps-- > 0)
    SendMessage(to, WM_HSCROLL, code,
                MAKELPARAM(GetScrollPos(sb, SB_CTL), (UINT)sb));
  SendMessage(to, WM_HSCROLL, SB_ENDSCROLL,
              MAKELPARAM(GetScrollPos(sb, SB_CTL), (UINT)sb));
}

// a turn of delta at pt on the screen: on the slider under the pointer,
// else wheel_other's; nonzero if used
static int turn(POINT pt, int delta) {
  HWND w = WindowFromPoint(pt);
  int steps;

  if (!usable(w))
    return 0;
  if (w != rest_on)
    rest = 0;
  rest_on = w;
  rest += delta;
  steps = rest / WHEEL_DELTA;
  rest -= steps * WHEEL_DELTA;
  if (is_slider(w)) {
    if (steps)
      scroll(w, steps);
    return 1;
  }
  return steps && wheel_other && wheel_other(pt, steps);
}

// a WM_MOUSEWHEEL taken whole through USER32, used, or else sent to its
// window as GetMessage would have it; nonzero if there was one
static int take(void) {
  POINT pt;
  int delta;

  if (!((CALL32_5)w32.call)((DWORD)(struct msg32 FAR *)&m32, 0, WM_MOUSEWHEEL,
                            WM_MOUSEWHEEL, PM_REMOVE, w32.peek, 0x10, 5))
    return 0;
  native = 1;
  // the turn, in the high word, or in the low word if the queue kept a
  // 16-bit wParam
  delta = (short)HIWORD(m32.wparam);
  if (!delta && abs((short)LOWORD(m32.wparam)) > KEYS_MAX)
    delta = (short)LOWORD(m32.wparam);
  pt.x = (short)LOWORD(m32.lparam);
  pt.y = (short)HIWORD(m32.lparam);
  if (!delta || !turn(pt, delta))
    ((CALL32_1)w32.call)((DWORD)(struct msg32 FAR *)&m32, w32.dispatch, 1, 1);
  return 1;
}

// a wheel message as a 16-bit program gets it: MSWHEEL_ROLLMSG, whose
// wParam is the turn, or a WM_MOUSEWHEEL with the turn in place of the
// keys; nonzero if used
static int read16(const MSG FAR *msg) {
  POINT pt;
  int delta = (short)msg->wParam;

  if (msg->message == WM_MOUSEWHEEL)
    native = 1;
  else if (!roll_msg || msg->message != roll_msg || native)
    return 0;
  if (abs(delta) <= KEYS_MAX)
    return 0;
  pt.x = (short)LOWORD(msg->lParam);
  pt.y = (short)HIWORD(msg->lParam);
  return turn(pt, delta);
}

BOOL wheel_get(MSG FAR *msg) {
  MSG next;

  for (;;) {
    // with the thunks, wait for a message without taking it, so that a
    // WM_MOUSEWHEEL is taken through USER32
    if (w32.peek) {
      if (!PeekMessage(&next, 0, 0, 0, PM_NOREMOVE)) {
        WaitMessage();
        continue;
      }
      if (next.message == WM_MOUSEWHEEL && take())
        continue;
    }
    if (!GetMessage(msg, 0, 0, 0))
      return FALSE;
    if (!read16(msg))
      return TRUE;
  }
}

int wheel_dialog(HINSTANCE inst, LPCSTR name, HWND owner, DLGPROC proc) {
  HWND dlg;
  MSG msg;
  int quit = 0;

  modal_done = 0;
  modal_result = -1;
  dlg = CreateDialog(inst, name, owner, proc);
  if (!dlg)
    return -1;
  if (owner)
    EnableWindow(owner, FALSE);
  ShowWindow(dlg, SW_SHOW);
  while (!modal_done && IsWindow(dlg)) {
    if (!wheel_get(&msg)) {
      quit = 1;
      break;
    }
    if (!IsDialogMessage(dlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  // the owner first, so that it comes to the front when the dialog goes
  if (owner)
    EnableWindow(owner, TRUE);
  if (IsWindow(dlg))
    DestroyWindow(dlg);
  // the program's own loop ends with it
  if (quit)
    PostQuitMessage(msg.wParam);
  return modal_result;
}

void wheel_end(int result) {
  modal_result = result;
  modal_done = 1;
}
