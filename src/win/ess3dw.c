/*
 * ess3d switches the 3-D effect (Spatializer) of the ES1869 from the
 * command line, shows the new setting for a moment and exits. It's made
 * for keys: a shortcut's Shortcut key, or keyboard software that runs a
 * command.
 *
 * Usage:
 *   `ess3d [options] command [command...]`
 *
 *   on, off, toggle   switch the effect, on also releases it from reset
 *   level N           0 to 63, or N% of 63
 *   level +N, -N      up or down N steps, stopping at 0 and 63
 *   up [N], down [N]  up or down N steps, 4 without N
 *   reset             reset the effect, keeping on/off and the level
 *   hold              hold the effect in reset
 *   limit on, off, toggle  the undocumented 3-D limit (50h bit 0)
 *   model on, off, toggle  the undocumented 3-D model, or mono mode (50h
 *                     bit 1); "mono" works too
 *   reg XX YY         Spatializer register XX (54, 56, 58, 5A) to YY, hex
 *   defaults          what ESS's driver sets when Windows starts
 *   show              change nothing, show the setting
 *   tray              an icon in the taskbar's tray with a panel of every
 *                     3-D setting (ess3dtr.c)
 *   exit              close the tray icon
 *   measure [quick | 54 | 56 | 58 | 5A]  measure what each setting does,
 *                     alone on the command line (ess3dms.c)
 *
 *   /q                no display and no message boxes, problems go to
 *                     ESS3D.LOG (or the /log= file)
 *   /t=1500           how long the display stays, in ms
 *   /log=file         append each result to a file, problems too
 *   /out=file         the measurement's report, ESS3D.TXT if left out
 *   /sim, /base=220, /cfg=800, /novxd  as for essctl
 *
 * Notes:
 *
 * The commands run in order and the display shows the result. It's a
 * topmost popup that never takes the focus. When the key is pressed again
 * while it's up, the new ess3d hands it the new setting and exits.
 *
 * A problem shows in the display too, and goes away by itself: a message
 * box would wait behind a full-screen game, and each press of the key
 * would leave one more ess3d waiting. Only a bad command line gets a box.
 *
 * Relative file names are taken from ess3d's own directory.
 *
 * Exit codes: 0 done, 1 the chip returned other values than were
 * written, 2 failed, 3 bad command line.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ess3d.h"
#include "ess3dms.h"
#include "ess3dtr.h"
#include "esshw.h"
#include "winio.h"

// missing from the Windows 3.1 headers
#ifndef WS_EX_TOOLWINDOW
#define WS_EX_TOOLWINDOW 0x00000080L
#endif
#ifndef WM_SETHOTKEY
#define WM_SETHOTKEY 0x0032
#endif

#define TITLE "ES1869 3-D"
#define OSD_CLASS "Ess3dOsd"
#define OSD_SET (WM_USER + 1)  // wParam the state (see pack), lParam the ms
#define OSD_TEXT (WM_USER + 2) // wParam the ms, lParam the text of a problem
#define OSD_DONE 0x3D          // the display's answer to both
#define OSD_TIMER 1

static const char usage[] =
    "ess3d [options] command [command...]\n\n"
    "Commands: on, off, toggle, level N (0 to 63, or N%), level +N, "
    "level -N, up [N], down [N], reset, hold, limit on|off|toggle, "
    "model on|off|toggle, reg XX YY (hex), defaults, show, tray, exit, "
    "measure [quick|54|56|58|5A]\n\n"
    "Options: /q, /t=1500 (ms), /log=file, /out=file, /sim, /base=220, "
    "/cfg=800, /novxd";

static HINSTANCE inst;

// the display of this instance
static struct ess3d_state osd;
static char osd_text[160];
static int osd_bar; // the level bar, not for a problem
static HFONT osd_font;
static int osd_h;          // a line's height in pixels
static int osd_tw, osd_th; // the text's size, wrapped

static void display(const struct ess3d_state *s, const char *text, u16 ms);

// --- log and messages -------------------------------------------------------

// relative names are in ess3d.exe's directory
void ess3d_path(const char *name, char *path, unsigned size) {
  char *slash;

  if (name[0] && (name[1] == ':' || name[0] == '\\')) {
    strncpy(path, name, size - 1);
    path[size - 1] = 0;
    return;
  }
  GetModuleFileName(inst, path, size - 13);
  slash = strrchr(path, '\\');
  if (slash)
    slash[1] = 0;
  else
    path[0] = 0;
  strncat(path, name, size - 1 - strlen(path));
}

void ess3d_log(const char *name, const char *text) {
  char path[144], stamp[32];
  time_t now = time(0);
  FILE *f;

  ess3d_path(name, path, sizeof(path));
  f = fopen(path, "a");
  if (!f)
    return;
  strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
  fprintf(f, "%s %s\n", stamp, text);
  fclose(f);
}

// a problem goes to the log, and unless /q to the display, or to a
// message box with the usage for a bad command line
static void problem(const struct ess3d_cmd *c, const char *text,
                    int with_usage) {
  char box[512];

  if (c->log[0])
    ess3d_log(c->log, text);
  else if (c->quiet)
    ess3d_log("ESS3D.LOG", text);
  if (c->quiet)
    return;
  if (!with_usage) {
    display(0, text, c->time_ms > 3000 ? c->time_ms : 3000);
    return;
  }
  sprintf(box, "%.150s\n\n%s", text, usage);
  MessageBox(0, box, TITLE, MB_OK | MB_ICONEXCLAMATION);
}

// --- display ----------------------------------------------------------------

// the state in the wParam of OSD_SET
static WPARAM pack(const struct ess3d_state *s) {
  return (WPARAM)(s->level | (s->enable ? 0x100 : 0) | (s->run ? 0x200 : 0));
}

static void unpack(WPARAM w, struct ess3d_state *s) {
  s->level = (u8)w;
  s->enable = (u8)((w & 0x100) != 0);
  s->run = (u8)((w & 0x200) != 0);
}

// size the display to its text and show it at the bottom of the screen,
// above every window but without taking the focus
static void osd_place(HWND w) {
  HDC dc = GetDC(w);
  HFONT old = (HFONT)SelectObject(dc, osd_font);
  int sw = GetSystemMetrics(SM_CXSCREEN);
  int sh = GetSystemMetrics(SM_CYSCREEN);
  int cx, cy;
  RECT t;

  osd_h = HIWORD(GetTextExtent(dc, "M", 1));
  // a long text wraps at 3/4 of the screen
  SetRect(&t, 0, 0, sw * 3 / 4, 0);
  DrawText(dc, osd_text, -1, &t, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
  SelectObject(dc, old);
  ReleaseDC(w, dc);
  osd_tw = t.right;
  osd_th = t.bottom;
  cx = osd_tw + 2 * osd_h;
  // margins of osd_h / 2, and the bar under the text
  cy = osd_th + osd_h + (osd_bar ? osd_h * 3 / 4 : 0);
  SetWindowPos(w, HWND_TOPMOST, (sw - cx) / 2, sh - cy - sh / 8, cx, cy,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void osd_paint(HWND w) {
  PAINTSTRUCT ps;
  RECT r, bar;
  HDC dc = BeginPaint(w, &ps);
  HBRUSH gray = (HBRUSH)GetStockObject(GRAY_BRUSH);
  HBRUSH fill;
  HFONT old;
  int m = osd_h / 2;

  GetClientRect(w, &r);
  FrameRect(dc, &r, gray);
  old = (HFONT)SelectObject(dc, osd_font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  SetRect(&bar, osd_h, m, osd_h + osd_tw, m + osd_th);
  DrawText(dc, osd_text, -1, &bar, DT_WORDBREAK | DT_NOPREFIX);
  SelectObject(dc, old);
  if (!osd_bar) {
    EndPaint(w, &ps);
    return;
  }
  // the level as a bar, green while the effect is heard
  SetRect(&bar, osd_h, m + osd_th + m / 2, r.right - osd_h,
          2 * m + osd_th + m / 2);
  FrameRect(dc, &bar, gray);
  InflateRect(&bar, -2, -2);
  bar.right = bar.left + (int)((long)(bar.right - bar.left) * osd.level /
                               ess3d_level_max());
  fill = CreateSolidBrush(osd.enable && osd.run ? RGB(0, 255, 0)
                                                : RGB(128, 128, 128));
  FillRect(dc, &bar, fill);
  DeleteObject(fill);
  EndPaint(w, &ps);
}

LRESULT CALLBACK __export osd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case OSD_SET:
  case OSD_TEXT:
    // a new setting or a problem, from this ess3d or from the next one
    if (msg == OSD_SET) {
      unpack(wp, &osd);
      ess3d_text(&osd, osd_text, sizeof(osd_text));
      osd_bar = 1;
    } else {
      // the sender waits in SendMessage, so its text is there
      strncpy(osd_text, (const char *)lp, sizeof(osd_text) - 1);
      osd_text[sizeof(osd_text) - 1] = 0;
      osd_bar = 0;
      lp = wp;
    }
    KillTimer(w, OSD_TIMER);
    if (!SetTimer(w, OSD_TIMER, (UINT)lp, 0)) {
      DestroyWindow(w); // no timer left, so no display
      return OSD_DONE;
    }
    osd_place(w);
    InvalidateRect(w, 0, TRUE);
    return OSD_DONE;
  case WM_PAINT:
    osd_paint(w);
    return 0;
  case WM_MOUSEACTIVATE:
    return MA_NOACTIVATE;
  case WM_SETHOTKEY:
    // a shortcut's key isn't tied to the display, so pressing it again
    // starts ess3d again instead of activating this window
    return 0;
  case WM_TIMER:
  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
    DestroyWindow(w);
    return 0;
  case WM_DESTROY:
    KillTimer(w, OSD_TIMER);
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(w, msg, wp, lp);
}

// the state s, or the text of a problem, shown for ms; returns when the
// display is gone
static void display(const struct ess3d_state *s, const char *text, u16 ms) {
  WNDCLASS wc;
  HWND w;
  HDC dc;
  MSG msg;
  UINT set = s ? OSD_SET : OSD_TEXT;
  WPARAM wp = s ? pack(s) : ms;
  LPARAM lp = s ? (LPARAM)ms : (LPARAM)(const char FAR *)text;

  // an ess3d started a moment ago still shows its display: update that one
  w = FindWindow(OSD_CLASS, 0);
  if (w && SendMessage(w, set, wp, lp) == OSD_DONE)
    return;
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = osd_proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wc.lpszClassName = OSD_CLASS;
  RegisterClass(&wc); // fails if another ess3d registered it, that's fine
  dc = GetDC(0);
  osd_font = CreateFont(-MulDiv(12, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0,
                        FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                        VARIABLE_PITCH | FF_SWISS, "Arial");
  ReleaseDC(0, dc);
  // a tool window gets no taskbar button
  w = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, OSD_CLASS, TITLE,
                     WS_POPUP, 0, 0, 0, 0, 0, 0, inst, 0);
  if (w) {
    SendMessage(w, set, wp, lp);
    // take a shortcut's key off the display, in case Windows tied it on
    // without sending WM_SETHOTKEY
    DefWindowProc(w, WM_SETHOTKEY, 0, 0);
    while (GetMessage(&msg, 0, 0, 0)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  if (osd_font)
    DeleteObject(osd_font);
}

// --- main -------------------------------------------------------------------

// a problem ends ess3d, and the tray's window it took goes with it
static int quit(HWND mine, const struct ess3d_cmd *c, const char *text,
                int code) {
  if (mine)
    DestroyWindow(mine);
  problem(c, text, 0);
  return code;
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE prev, LPSTR cmdline, int show) {
  struct ess3d_cmd c;
  struct winio_opts io;
  struct ess3d_state s;
  char text[64], line[160];
  HWND tray, mine = 0;
  int err;

  (void)prev;
  (void)show;
  inst = hinst;
  if (ess3d_parse(cmdline, &c) < 0) {
    problem(&c, c.err, 1);
    return 3;
  }
  if (c.measure)
    return measure_main(&c, hinst);
  // no commands for the chip: a running tray icon shows its panel, or
  // closes
  tray = tray_window();
  if (!c.nact && tray && (c.tray || c.exit)) {
    PostMessage(tray, c.exit ? WM_CLOSE : TRAY_PANEL, 0, 0);
    return 0;
  }
  if (!c.nact && c.exit)
    return 0;
  // a new tray takes its window now, before anything can yield to a
  // second "ess3d tray" started at the same time
  if (c.tray && !tray) {
    mine = tray_claim(hinst);
    if (!mine) {
      problem(&c, "Not enough memory for the tray icon", 0);
      return 2;
    }
  }

  memset(&io, 0, sizeof(io));
  io.audio_base = c.audio_base;
  io.config_base = c.config_base;
  io.sim = c.sim;
  io.novxd = c.novxd;
  err = winio_init(&io);
  // a measurement ended by force left the mixer changed
  if (err == 0)
    measure_put_back(c.sim);
  // with "tray" alone this only reads, so a card that doesn't answer is
  // reported before the icon shows
  if (err == 0)
    err = winio_begin();
  if (err == 0) {
    err = ess3d_run(&c, &s);
    winio_end();
  }
  if (err == -ESSHW_ENODEV) {
    sprintf(line,
            "No ES1869 answered at %03Xh: use /base=, or /sim to try ess3d "
            "without the card",
            esshw.audio_base);
    return quit(mine, &c, line, 2);
  }
  if (err < 0) {
    sprintf(line, "ess3d could not reach the ES1869: %s", esshw_strerror(err));
    return quit(mine, &c, line, 2);
  }

  ess3d_text(&s, text, sizeof(text));
  if (err == ESS3D_MISMATCH) {
    sprintf(line, "The ES1869 did not keep the setting, it returns: %s", text);
    return quit(mine, &c, line, 1);
  }
  if (c.log[0] && c.nact)
    ess3d_log(c.log, text);
  // a new tray reads the setting when it starts
  if (c.nact && !mine)
    tray_changed();
  if (c.exit && tray)
    PostMessage(tray, WM_CLOSE, 0, 0);
  if (c.tray) {
    if (tray) {
      PostMessage(tray, TRAY_PANEL, 0, 0);
      return 0;
    }
    return tray_run(&c);
  }
  if (!c.quiet)
    display(&s, 0, c.time_ms);
  return 0;
}
