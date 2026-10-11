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
 * Left of the controls, a graph shows what they do, from the effect of
 * /sim, which follows one card's measurements (s3dsim.c). Its top pane is
 * the width by frequency: what the effect does to the S, the difference of
 * the channels (S>S), or with mono the S that it makes from the M (M>S),
 * and dotted, the same where the automatic level holds it. Its bottom pane
 * is the width over time in a step of the measurement: an M at -24 dBFS
 * and an S as loud, which drops 12 dB after 2 s. It shows the S out
 * against the M as the automatic level lowers the effect to its level and
 * raises it again, with the two levels dashed. Its text is in the panel's
 * font. The graph follows the setting, and a slider while it moves: at
 * most every G_EVERY ms, and on a slow PC less often, so that drawing it
 * takes at most a quarter of the time, and what it skipped when the slider
 * rests (GRAPH_TIMER). It is drawn into a bitmap and then onto the screen
 * at once, and drawn right away, as a slider's tracking lets no WM_PAINT
 * through. Hide graph hides it, and ESS3D.INI keeps that.
 *
 * While a slider moves, only its field is written, and its row and the
 * graph follow it. The rest of the panel, the icon's tooltip and the log
 * follow once it stops, from what the chip returns.
 *
 * Each setting has a tip, which shows when the mouse rests on it. The
 * main loop watches the mouse over the panel (tip_watch), since a 16-bit
 * program has no tooltip control of its own. A value typed out of range
 * is said in the tip, under its text field (tip_say), as the panel has no
 * status line. The mouse wheel moves the slider under the pointer
 * (wheel.c). Under the groups are Hide graph, Defaults and Close.
 *
 * The panel closes when another window comes to the front, like the
 * tray's menus, or on a second click on the icon. It watches the
 * foreground window instead of its own activation, since Windows may not
 * let it take the focus. It takes the focus when the mouse comes over it,
 * which Windows allows after a mouse event, as the wheel's messages go
 * to the focus. If ess3d.exe was replaced while the tray runs, the panel
 * says so, as Windows starts every ess3d from the running copy. With the
 * register interface of the rebuilt ES1869.VXD, the icon also checks the
 * setting every few seconds, so it follows the Windows mixer.
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

#include <math.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ess3d.h"
#include "ess3dtr.h"
#include "esscat.h"
#include "esshw.h"
#include "essio.h"
#include "s3dsim.h"
#include "wheel.h"
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
#define TIP_TIMER 4 // the mouse rests on a setting: its tip
#define TIP_MS 600
#define GRAPH_TIMER 5   // a slider rests: the graph that was skipped
#define TIP_SAY_MS 4000 // a message in the tip
#define TIP_REPLACED_MS 15000
#define TIP_W 240 // a tip's width in pixels
#define TIP_CLASS "ess3dTip"
#define INI_NAME "ESS3D.INI" // the graph shown or not, [Tray] Graph=

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
#define IDC_P_GROUP 110 // + 0 and 1: the group boxes
#define IDC_P_GRAPH 112
#define IDC_P_SHOWGRAPH 113
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

// the graph left of the controls, its width in dialog units; its points
// over time, 50 ms each over 8 s, with the S 12 dB lower after 2 s (by
// frequency, they are s3dsim's S3DSIM_CURVE_N)
#define GRAPH_CLASS "ess3dGraph"
#define G_W 150
#define G_NT 161
#define G_T1 2.0
#define G_EVERY 40  // ms between drawings while a slider moves, at least
#define G_SHARE 3   // and 3 times as long as the last one took
#define G_MOST 1000 // and 1 s at most

// the rows, named for what they do to the sound, with the words at the
// ends of their sliders as ES938 Controls has Min and Max, and their tips
static const struct {
  int field;
  const char *name, *lo, *hi, *tip;
} names[] = {
    {F_3D_LEVEL, "Effect:", "Min", "Max",
     "Effect strength: the gain of the band-pass copy of the side signal "
     "(L-R) that widens the image, peaking near 450 Hz. +16.8 dB at 63, "
     "0.75 dB less per step below. (Mixer 52h)"},
    {F_3D_R56, "Lower above:", "Low", "High",
     "Upper threshold. While the processed side signal is above it, the "
     "effect is reduced at the lower speed. Threshold: the mid level (M) "
     "times value / 91.2, plus the fixed level; 95h is M +4.3 dB. (56h)"},
    {F_3D_R54, "Raise below:", "Low", "High",
     "Lower threshold. While the processed side signal is below it, the "
     "effect is restored at the raise speed. Computed as for Lower above, "
     "then raised 0.5 dB; 8Fh is M +3.9 dB. Set above Lower above, it is "
     "where the side signal settles. (54h)"},
    {F_3D_R5A, "Fixed level:", "None", "High",
     "Absolute level added to both thresholds, in proportion to the value: "
     "-36.3 dBFS at 80h, none at 00h. Keeps quiet passages at the full "
     "effect. (5Ah)"},
    {F_3D_FALL, "Lower speed:", "Fast", "Slow",
     "Attack rate: how fast the effect is reduced above the upper "
     "threshold. 257.5 / (n + 1) dB/s: 257.5 at 0, 16.1 at 15. (58h bits "
     "3:0)"},
    {F_3D_RISE, "Raise speed:", "Fast", "Slow",
     "Release rate: how fast the effect is restored below the lower "
     "threshold, as the lower speed / (h + 1): equal at 0, 16 times slower "
     "at 15. (58h bits 7:4)"}};

// the other controls' tips
static const struct {
  int id;
  const char *tip;
} tips[] = {
    {IDC_P_ON, "Turns the 3-D effect on. When off, the audio passes through "
               "unchanged. (Mixer 50h bit 3)"},
    {IDC_P_HOLD, "Holds the effect in reset. The audio passes almost "
                 "unchanged: only a +0.4 dB bass shelf remains on the side "
                 "signal (L-R). (50h bit 2 clear)"},
    {IDC_P_STEREO, "Widens existing stereo: adds a band-pass copy of the "
                   "side signal (L-R), the difference between the two "
                   "channels. (50h bit 1 clear)"},
    {IDC_P_MONO, "Creates width from mono: derives the side signal from the "
                 "mid signal (L+R) through the same band-pass, 3 dB lower, "
                 "and cuts the original side signal by 30 dB. (50h bit 1 "
                 "set)"},
    {IDC_P_RESET, "Pulses the effect's reset to clear its internal state, "
                  "then resumes with the same settings. (50h bit 2)"},
    {IDC_P_LIMIT, "Turns on the automatic level, a compressor on the "
                  "effect: the boost is reduced while the processed side "
                  "signal is above the upper threshold and restored below "
                  "the lower one, so wide material gets less effect. (50h "
                  "bit 0, ESS 3D Limit)"},
    {IDC_P_GROUP, "The 3-D stage widens the stereo image by boosting the "
                  "side signal (L-R) through a band-pass filter near "
                  "450 Hz."},
    {IDC_P_GROUP + 1, "A compressor on the effect: holds the processed side "
                      "signal between two thresholds set against the mid "
                      "signal (L+R)."},
    {IDC_P_SHOWGRAPH, "Shows or hides the graph. ESS3D.INI keeps the "
                      "choice."},
    {IDC_P_DEFAULTS, "Restores the settings that ESS's driver applies when "
                     "Windows starts: effect on at 63, automatic level off, "
                     "54h-5Ah at 8Fh, 95h, 94h and 80h."},
    {IDC_P_CLOSE, "Removes the tray icon and closes ess3d, as ess3d exit "
                  "does."},
    {IDC_P_GRAPH, "Simulated response of the current settings, modeled on "
                  "one card's measurements. Top: side-signal gain by "
                  "frequency, dotted while the automatic level reduces it. "
                  "Bottom: side level relative to the mid over 8 s, with "
                  "the side signal 12 dB lower after 2 s; dashed, the two "
                  "thresholds."}};

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
static int moving = -1;            // a slider written alone while it moves
static HWND graph;
static int graph_on = 1;           // Hide graph and Show graph
static int graph_laid;             // the controls are right of the graph
static int graph_ok;               // plot holds the curves of plot.reg
static DWORD graph_at;             // timeGetTime() when it was last drawn
static DWORD graph_wait = G_EVERY; // and the ms to the next while moving
static int graph_skip;             // a slider's value it hasn't drawn
static int skip_row, skip_value;   // and which
static HWND tip;
static int tip_id;          // the control the mouse rests on
static const char *tip_now; // and its tip
static int tip_said;        // the tip holds a message instead
static long exe_time;       // ess3d.exe's time and size when the tray started
static long exe_size;

static void tip_hide(void);
static void tip_say(HWND ctl, const char *text, UINT ms);

// what the graph plots: the registers 50h, 52h, 54h, 56h, 58h and 5Ah,
// S>S and M>S by frequency at the full boost and where the limit holds
// it, and the S out over time, with the boost's gain where it held and
// the two levels, in dB
static struct {
  u8 reg[6];
  float ss[S3DSIM_CURVE_N], ms[S3DSIM_CURVE_N];
  float ss_held[S3DSIM_CURVE_N], ms_held[S3DSIM_CURVE_N];
  float width[G_NT];
  double held, rise, fall;
} plot;

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

  // right of the graph
  SetRect(&rc, G_W + x, y, G_W + x + w, y + h);
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

// a control's text, unless it has it already, which saves a repaint
static void set_text(int id, const char *text) {
  char old[100];

  GetDlgItemText(panel, id, old, sizeof(old));
  if (strcmp(old, text))
    SetDlgItemText(panel, id, text);
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
    set_text(IDC_P_RANGE + r, text);
  }
}

static void edit_show(int row, int force) {
  char text[8];
  HWND e = item(IDC_P_EDIT + row);

  if (!force && GetFocus() == e)
    return; // don't change what's being typed
  sprintf(text, is_hex(row) ? "%02X" : "%d", slider_value(row));
  set_text(IDC_P_EDIT + row, text);
}

// --- the graph ----------------------------------------------------------

// the graph of the setting, with a row's field at value while its slider
// moves (row -1 for none); while it moves, after graph_wait ms, and what
// it skipped once the slider rests
static void graph_update(int row, int value, int moving_now) {
  static const u8 addr[4] = {0x54, 0x56, 0x58, 0x5A};
  DWORD now;
  u8 reg[6];
  int i, j;

  if (!graph || !graph_on)
    return;
  reg[0] =
      (u8)((cur.enable << 3) | (cur.run << 2) | (cur.mono << 1) | cur.limit);
  reg[1] = cur.level;
  for (j = 0; j < 4; j++) {
    reg[2 + j] = plot.reg[2 + j];
    for (i = 0; i < ess3d_regs(); i++)
      if (ess3d_reg_addr(i) == addr[j])
        reg[2 + j] = (u8)ess3d_reg_byte(&cur, i);
  }
  if (row >= 0 && row_reg[row] < 0)
    reg[1] = (u8)value;
  for (j = 0; row >= 0 && row_reg[row] >= 0 && j < 4; j++)
    if (ess3d_reg_addr(row_reg[row]) == addr[j])
      reg[2 + j] = row_byte(row, value);
  if (graph_ok && !memcmp(reg, plot.reg, sizeof(reg))) {
    graph_skip = 0;
    return;
  }
  now = timeGetTime();
  if (moving_now && now - graph_at < graph_wait) {
    // drawn at a later step, or when the slider rests
    if (!graph_skip && !SetTimer(tray, GRAPH_TIMER, (UINT)graph_wait, 0))
      tray_log("no timer left for the graph");
    graph_skip = 1;
    skip_row = row;
    skip_value = value;
    return;
  }
  graph_skip = 0;
  memcpy(plot.reg, reg, sizeof(reg));
  s3dsim_step(plot.reg, G_T1, 0.05, G_NT, plot.width, &plot.held, &plot.rise,
              &plot.fall);
  s3dsim_curves(plot.reg, 0, plot.ss, plot.ms);
  // the curve where the limit holds the effect, drawn only with the limit
  if ((plot.reg[0] & 0x0D) == 0x0D)
    s3dsim_curves(plot.reg, plot.held, plot.ss_held, plot.ms_held);
  graph_ok = 1;
  // drawn now, as a slider's tracking lets no WM_PAINT through
  InvalidateRect(graph, 0, FALSE);
  UpdateWindow(graph);
  // while a slider moves, the slider keeps most of the time
  graph_at = timeGetTime();
  graph_wait = (graph_at - now) * G_SHARE;
  if (graph_wait < G_EVERY)
    graph_wait = G_EVERY;
  if (graph_wait > G_MOST)
    graph_wait = G_MOST;
}

// a pane of the graph: the plot's rectangle in pixels, x from x0 at its
// left to x1, and y from y0 at its bottom to y1
struct pane {
  RECT rc;
  double x0, x1, y0, y1;
};

static int px(const struct pane *p, double x) {
  return p->rc.left +
         (int)((x - p->x0) / (p->x1 - p->x0) * (p->rc.right - p->rc.left) +
               0.5);
}

// y in the pane, at its edge where y is out of it
static int py(const struct pane *p, double y) {
  if (y > p->y1)
    y = p->y1;
  if (y < p->y0)
    y = p->y0;
  return p->rc.top +
         (int)((p->y1 - y) / (p->y1 - p->y0) * (p->rc.bottom - p->rc.top) +
               0.5);
}

static void text(HDC dc, int x, int y, UINT align, COLORREF color,
                 const char *s) {
  SetTextAlign(dc, align);
  SetTextColor(dc, color);
  TextOut(dc, x, y, s, strlen(s));
}

// n values from x0 in steps of dx
static void curve(HDC dc, const struct pane *p, const float *v, int n,
                  double x0, double dx, HPEN pen) {
  static POINT pt[G_NT];
  int i;

  for (i = 0; i < n; i++) {
    pt[i].x = px(p, x0 + i * dx);
    pt[i].y = py(p, v[i]);
  }
  SelectObject(dc, pen);
  Polyline(dc, pt, n);
}

static int text_w(HDC dc, const char *s) {
  return LOWORD(GetTextExtent(dc, s, strlen(s)));
}

// the pane between top and bottom, with room for its title, and for its
// labels lw pixels wide on the left and th high: its grid every ystep
// with a label every other one, the lines at the x of xs labeled xl, and
// its title
static void pane_grid(HDC dc, struct pane *p, const RECT *client, int top,
                      int bottom, int th, int lw, double ystep,
                      const double *xs, const char *const *xl, int nx,
                      const char *title, HPEN light, HPEN dark) {
  COLORREF gray = RGB(96, 96, 96);
  char s[8];
  double y;
  int i, k, x, w;

  // under the title, by half of the top label
  SetRect(&p->rc, client->left + lw + 6, top + th + th / 2 + 3,
          client->right - 4, bottom - th - 4);
  for (y = p->y0, k = 0; y <= p->y1 + 0.01; y += ystep, k++) {
    SelectObject(dc, y > -0.01 && y < 0.01 ? dark : light);
    MoveTo(dc, p->rc.left, py(p, y));
    LineTo(dc, p->rc.right, py(p, y));
    if (k % 2 == 0) {
      sprintf(s, y > 0.01 ? "+%.0f" : "%.0f", y);
      text(dc, p->rc.left - 4, py(p, y) - th / 2, TA_RIGHT | TA_TOP, gray, s);
    }
  }
  for (i = 0; i < nx; i++) {
    x = px(p, xs[i]);
    SelectObject(dc, light);
    MoveTo(dc, x, p->rc.top);
    LineTo(dc, x, p->rc.bottom);
    // under its line, clear of the labels on the left, inside the graph
    w = text_w(dc, xl[i]);
    x -= w / 2;
    if (x < p->rc.left - 1)
      x = p->rc.left - 1;
    if (x + w > client->right - 2)
      x = client->right - 2 - w;
    text(dc, x, p->rc.bottom + 2, TA_LEFT | TA_TOP, gray, xl[i]);
  }
  SelectObject(dc, dark);
  MoveTo(dc, p->rc.left, p->rc.top);
  LineTo(dc, p->rc.left, p->rc.bottom);
  text(dc, client->left + 4, top + 2, TA_LEFT | TA_TOP, RGB(0, 0, 0), title);
}

// a key at the right of a pane's title row, right to left from *x
static void key(HDC dc, int *x, int y, COLORREF color, const char *s) {
  text(dc, *x, y, TA_RIGHT | TA_TOP, color, s);
  *x -= text_w(dc, s) + 8;
}

// a level across the pane in dashes, 4 pixels of every 8 from x0, so that
// two levels at the same height show both colors
static void level(HDC dc, const struct pane *p, double y, int x0, HPEN pen) {
  int x, at = py(p, y);

  SelectObject(dc, pen);
  for (x = p->rc.left + x0; x < p->rc.right; x += 8) {
    MoveTo(dc, x, at);
    LineTo(dc, x + 4 < p->rc.right ? x + 4 : p->rc.right, at);
  }
}

static void graph_paint(HDC dc, const RECT *client) {
  static const double fx[3] = {1, 4.3219, 7.6439}; // 100 Hz, 1k, 10k
  static const char *const fl[3] = {"100", "1k", "10k"};
  static const double tx[5] = {0, 2, 4, 6, 8};
  static const char *const tl[5] = {"0", "2", "4", "6", "8 s"};
  COLORREF blue = RGB(0, 0, 192), red = RGB(192, 0, 0);
  COLORREF green = RGB(0, 128, 0), main;
  HPEN light, dark, solid, dotted, fall, rise, old;
  HFONT font = (HFONT)SendMessage(panel, WM_GETFONT, 0, 0);
  HGDIOBJ oldf = 0;
  TEXTMETRIC tm;
  struct pane p;
  RECT rc = *client;
  int on = (plot.reg[0] & 0x0C) == 0x0C, mono = on && (plot.reg[0] & 2);
  int limit = on && (plot.reg[0] & 1), th, lw, h, x;

  FillRect(dc, &rc, GetStockObject(WHITE_BRUSH));
  // the panel's font, which the raster fonts of Windows 98 draw clearly
  if (font)
    oldf = SelectObject(dc, font);
  GetTextMetrics(dc, &tm);
  th = tm.tmHeight;
  lw = text_w(dc, "+40"); // as wide as the widest label
  h = (rc.bottom - rc.top) / 2;
  SetBkMode(dc, TRANSPARENT);
  main = mono ? red : blue;
  light = CreatePen(PS_SOLID, 1, RGB(216, 216, 216));
  dark = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
  solid = CreatePen(PS_SOLID, 1, main);
  dotted = CreatePen(PS_DOT, 1, main);
  fall = CreatePen(PS_SOLID, 1, red);
  rise = CreatePen(PS_SOLID, 1, green);
  old = SelectObject(dc, light);

  // the width by frequency: S>S, or M>S with mono, over 8 octaves
  p.x0 = 0;
  p.x1 = 8;
  p.y0 = -40;
  p.y1 = 20;
  pane_grid(dc, &p, &rc, rc.top, rc.top + h, th, lw, 10, fx, fl, 3,
            "Width by frequency, dB", light, dark);
  x = rc.right - 4;
  if (limit) {
    curve(dc, &p, mono ? plot.ms_held : plot.ss_held, S3DSIM_CURVE_N, 0,
          1.0 / 6, dotted);
    key(dc, &x, rc.top + 2, main, "held");
  }
  curve(dc, &p, mono ? plot.ms : plot.ss, S3DSIM_CURVE_N, 0, 1.0 / 6, solid);
  key(dc, &x, rc.top + 2, main, mono ? "from mono" : "stereo");

  // the width over time: the S out against the M, with the levels
  p.x0 = 0;
  p.x1 = 8;
  p.y0 = -20;
  p.y1 = 20;
  pane_grid(dc, &p, &rc, rc.top + h, rc.bottom, th, lw, 10, tx, tl, 5,
            "Width over time, dB", light, dark);
  x = rc.right - 4;
  if (limit) {
    level(dc, &p, plot.fall, 0, fall);
    level(dc, &p, plot.rise, 4, rise);
    key(dc, &x, rc.top + h + 2, green, "raise");
    key(dc, &x, rc.top + h + 2, red, "lower");
  }
  curve(dc, &p, plot.width, G_NT, 0, 0.05, solid);
  // where the S drops 12 dB, at the top, which the drop leaves clear
  text(dc, px(&p, G_T1) + 3, p.rc.top + 1, TA_LEFT | TA_TOP, RGB(96, 96, 96),
       "S -12 dB");

  SelectObject(dc, old);
  DeleteObject(light);
  DeleteObject(dark);
  DeleteObject(solid);
  DeleteObject(dotted);
  DeleteObject(fall);
  DeleteObject(rise);
  if (oldf)
    SelectObject(dc, oldf);
}

LRESULT CALLBACK __export graph_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  PAINTSTRUCT ps;
  HBITMAP bmp;
  HGDIOBJ old;
  RECT rc;
  HDC dc, mem;

  switch (msg) {
  case WM_ERASEBKGND:
    return 1; // the painting covers it all
  case WM_PAINT:
    dc = BeginPaint(w, &ps);
    GetClientRect(w, &rc);
    // into a bitmap, then onto the screen at once, so that it doesn't
    // flicker; straight onto the screen without the memory for it
    mem = CreateCompatibleDC(dc);
    bmp = mem ? CreateCompatibleBitmap(dc, rc.right, rc.bottom) : 0;
    if (bmp) {
      old = SelectObject(mem, bmp);
      graph_paint(mem, &rc);
      BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
      DeleteObject(bmp);
    } else {
      graph_paint(dc, &rc);
    }
    if (mem)
      DeleteDC(mem);
    EndPaint(w, &ps);
    return 0;
  case WM_DESTROY:
    graph = 0;
    return 0;
  }
  return DefWindowProc(w, msg, wp, lp);
}

// the panel shows the setting
static void panel_fill(void) {
  int r;

  if (!panel)
    return;
  CheckDlgButton(panel, IDC_P_ON, cur.enable);
  CheckDlgButton(panel, IDC_P_HOLD, !cur.run);
  CheckRadioButton(panel, IDC_P_STEREO, IDC_P_MONO,
                   cur.mono ? IDC_P_MONO : IDC_P_STEREO);
  CheckDlgButton(panel, IDC_P_LIMIT, cur.limit);
  for (r = 0; r < nsliders; r++) {
    if (r != drag &&
        GetScrollPos(item(IDC_P_SLIDER + r), SB_CTL) != slider_value(r))
      SetScrollPos(item(IDC_P_SLIDER + r), SB_CTL, slider_value(r), TRUE);
    if (r != drag)
      unit_show(r, slider_value(r));
    edit_show(r, 0);
  }
  if (drag < 0)
    graph_update(-1, 0, 0);
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

// a slider's field written alone while the slider moves, without reading
// the setting back; refresh() follows when it stops
static int quick_write(int row, int v) {
  int err = winio_begin();

  if (err == 0) {
    err = ess_field_write(slider_field(row), (u8)v, 0);
    winio_end();
  }
  if (err < 0) {
    changed(err);
    return err;
  }
  if (row_reg[row] < 0)
    cur.level = (u8)v;
  else
    cur.reg[row_reg[row]] = (u8)v;
  return 0;
}

// the whole panel, the icon and the log, from what the chip returns
static void refresh(void) {
  int err = winio_begin();

  moving = -1;
  if (err == 0) {
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
      sprintf(msg, "Enter a value from 00h to %02Xh.", cat_max(f));
    else
      sprintf(msg, "Enter a value from 0 to %d.", cat_max(f));
    tip_say(item(IDC_P_EDIT + row), msg, TIP_SAY_MS);
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
  // a step of the wheel or the keys hides the tip, as a click does
  tip_hide();
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
    } else if (moving == row) {
      refresh();
    }
    return;
  default:
    return;
  }
  if (v < 0)
    v = 0;
  if (v > max)
    v = max;
  if (GetScrollPos(sb, SB_CTL) != v)
    SetScrollPos(sb, SB_CTL, v, TRUE);
  sprintf(text, is_hex(row) ? "%02X" : "%d", v);
  set_text(IDC_P_EDIT + row, text);
  unit_show(row, v);
  if (code == SB_THUMBTRACK && !winio_can_poll()) {
    // direct I/O with ES1869.VXD, only write when the thumb is released
    drag = row;
    graph_update(row, v, 1);
    return;
  }
  drag = -1;
  // the field alone while it moves, the rest when it stops
  if (v != slider_value(row) && quick_write(row, v) == 0)
    moving = row;
  graph_update(row, v, code != SB_THUMBPOSITION);
}

// the controls right of the graph, or moved left over its place, and the
// panel as wide as that, its right edge where it was; and the button
static void graph_layout(int show) {
  RECT rc;
  HWND c;
  int dx;

  SetDlgItemText(panel, IDC_P_SHOWGRAPH, show ? "Hide &graph" : "Show &graph");
  if (show == graph_laid)
    return;
  SetRect(&rc, 0, 0, G_W, 0);
  MapDialogRect(panel, &rc);
  dx = show ? rc.right : -rc.right;
  for (c = GetWindow(panel, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
    if (c == graph)
      continue;
    GetWindowRect(c, &rc);
    ScreenToClient(panel, (POINT FAR *)&rc);
    SetWindowPos(c, 0, rc.left + dx, rc.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (graph)
    ShowWindow(graph, show ? SW_SHOWNA : SW_HIDE);
  GetWindowRect(panel, &rc);
  SetWindowPos(panel, 0, rc.left - dx < 0 ? 0 : rc.left - dx, rc.top,
               rc.right - rc.left + dx, rc.bottom - rc.top,
               SWP_NOZORDER | SWP_NOACTIVATE);
  graph_laid = show;
}

// Hide graph and Show graph: the layout, the graph drawn again, and
// ESS3D.INI
static void graph_toggle(int show) {
  char ini[144];

  graph_on = show;
  graph_layout(show && graph);
  graph_ok = 0;
  graph_update(-1, 0, 0);
  ess3d_path(INI_NAME, ini, sizeof(ini));
  if (!WritePrivateProfileString("Tray", "Graph", show ? "1" : "0", ini))
    tray_log("the graph's setting wasn't kept in ESS3D.INI");
}

// --- the tips -------------------------------------------------------------

// the tip of the control with this id: a row's, or one of tips, or 0
static const char *tip_text(int id) {
  unsigned i;
  int r = -1;

  if (id >= IDC_P_LABEL && id < IDC_P_HI + MAX_SLIDERS)
    r = id % 100;
  if (r >= 0 && r < nsliders)
    return row_name[r] >= 0 ? names[row_name[r]].tip
                            : ess_fields[slider_field(r)].help;
  for (i = 0; i < sizeof(tips) / sizeof(tips[0]); i++)
    if (tips[i].id == id)
      return tips[i].tip;
  return 0;
}

// the smallest control under the mouse, so not a group box around it: its
// id, or 0
static int tip_at(void) {
  POINT pt;
  RECT rc;
  HWND c, best = 0;
  long area, least = 0;

  GetCursorPos(&pt);
  for (c = GetWindow(panel, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
    if (!IsWindowVisible(c))
      continue;
    GetWindowRect(c, &rc);
    if (!PtInRect(&rc, pt))
      continue;
    area = (long)(rc.right - rc.left) * (rc.bottom - rc.top);
    if (!best || area < least) {
      best = c;
      least = area;
    }
  }
  return best ? GetDlgCtrlID(best) : 0;
}

static void tip_hide(void) {
  KillTimer(tray, TIP_TIMER);
  tip_said = 0;
  if (tip && IsWindowVisible(tip))
    ShowWindow(tip, SW_HIDE);
}

// the tip at x, y on the screen, sized for its text and kept on the
// screen: above y0 where it doesn't fit under
static void tip_place(int x, int y, int y0) {
  RECT rc;
  HFONT font;
  HGDIOBJ old = 0;
  HDC dc;
  int w, h;

  dc = GetDC(tip);
  font = (HFONT)SendMessage(panel, WM_GETFONT, 0, 0);
  if (font)
    old = SelectObject(dc, font);
  SetRect(&rc, 0, 0, TIP_W, 0);
  DrawText(dc, tip_now, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
  if (old)
    SelectObject(dc, old);
  ReleaseDC(tip, dc);
  w = rc.right + 10;
  h = rc.bottom + 6;
  if (x + w > GetSystemMetrics(SM_CXSCREEN))
    x = GetSystemMetrics(SM_CXSCREEN) - w;
  if (y + h > GetSystemMetrics(SM_CYSCREEN))
    y = y0 - h;
  SetWindowPos(tip, HWND_TOPMOST, x < 0 ? 0 : x, y < 0 ? 0 : y, w, h,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(tip, 0, TRUE);
}

// the tip of the control under the mouse, under the pointer
static void tip_show(void) {
  POINT pt;

  if (!tip || !tip_now || !panel || !IsWindowVisible(panel) ||
      tip_at() != tip_id)
    return;
  GetCursorPos(&pt);
  tip_place(pt.x + 2, pt.y + 20, pt.y - 4);
}

// a message in the tip, under a control, until a click, a key or ms
static void tip_say(HWND ctl, const char *text, UINT ms) {
  static char said[96];
  RECT rc;

  if (!tip || !ctl)
    return;
  tip_hide();
  strncpy(said, text, sizeof(said) - 1);
  tip_now = said;
  tip_id = GetDlgCtrlID(ctl);
  GetWindowRect(ctl, &rc);
  tip_place(rc.left, rc.bottom + 2, rc.top - 2);
  tip_said = 1;
  if (!SetTimer(tray, TIP_TIMER, ms, 0))
    tray_log("no timer left for the tips");
}

// the mouse over the panel, before the dialog has the message: a new
// control under it starts the timer of its tip, and a click or a key
// hides the tip
static void tip_watch(const MSG *m) {
  int id;

  switch (m->message) {
  case WM_MOUSEMOVE:
    if (m->hwnd != panel && GetParent(m->hwnd) != panel)
      return;
    id = tip_at();
    if (id == tip_id)
      return;
    // the wheel's messages go to the window with the focus: the panel
    // takes it if Windows didn't let it when it opened, which it does
    // after a mouse event over the panel
    if (foreground() != (UINT)panel)
      to_front(panel);
    // a message stays while the mouse moves
    if (tip_said)
      return;
    tip_hide();
    tip_id = id;
    tip_now = tip_text(id);
    if (tip_now && !SetTimer(tray, TIP_TIMER, TIP_MS, 0))
      tray_log("no timer left for the tips");
    return;
  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    tip_hide();
    return;
  }
}

LRESULT CALLBACK __export tip_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  PAINTSTRUCT ps;
  HBRUSH back;
  HFONT font;
  HGDIOBJ old = 0;
  RECT rc;
  HDC dc;

  switch (msg) {
  case WM_PAINT:
    dc = BeginPaint(w, &ps);
    GetClientRect(w, &rc);
    back = CreateSolidBrush(RGB(255, 255, 225));
    if (back) {
      FillRect(dc, &rc, back);
      DeleteObject(back);
    }
    font = panel ? (HFONT)SendMessage(panel, WM_GETFONT, 0, 0) : 0;
    if (font)
      old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    InflateRect(&rc, -4, -2);
    if (tip_now)
      DrawText(dc, tip_now, -1, &rc, DT_WORDBREAK | DT_NOPREFIX);
    if (old)
      SelectObject(dc, old);
    EndPaint(w, &ps);
    return 0;
  case WM_MOUSEACTIVATE:
    return MA_NOACTIVATE;
  }
  return DefWindowProc(w, msg, wp, lp);
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
  char ini[144];
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
  y += 5;
  add("BUTTON", "Hide &graph", WS_GROUP | WS_TABSTOP | BS_PUSHBUTTON, 6, y, 68,
      14, IDC_P_SHOWGRAPH);
  add("BUTTON", "&Defaults", WS_TABSTOP | BS_PUSHBUTTON, (P_W - 68) / 2, y, 68,
      14, IDC_P_DEFAULTS);
  add("BUTTON", "&Close", WS_TABSTOP | BS_PUSHBUTTON, P_W - 74, y, 68, 14,
      IDC_P_CLOSE);
  // the graph left of the controls, as tall as they are
  SetRect(&rc, 4, 4, G_W, y + 14);
  MapDialogRect(panel, &rc);
  graph = CreateWindow(GRAPH_CLASS, "", WS_CHILD | WS_VISIBLE | WS_BORDER,
                       rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                       panel, (HMENU)IDC_P_GRAPH, inst, 0);
  if (!graph)
    tray_log("no memory for the graph");
  // fit the panel to its rows
  SetRect(&rc, 0, 0, G_W + P_W, y + 18);
  MapDialogRect(panel, &rc);
  AdjustWindowRect(&rc, GetWindowLong(panel, GWL_STYLE), FALSE);
  SetWindowPos(panel, 0, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  // the graph as ESS3D.INI keeps it, shown unless it says 0
  graph_laid = 1;
  ess3d_path(INI_NAME, ini, sizeof(ini));
  graph_on = GetPrivateProfileInt("Tray", "Graph", 1, ini) != 0;
  if (!graph)
    EnableWindow(item(IDC_P_SHOWGRAPH), FALSE);
  graph_layout(graph_on && graph);
}

static void panel_hide(void) {
  KillTimer(tray, PANEL_TIMER);
  tip_hide();
  tip_id = 0;
  if (moving >= 0)
    refresh();
  if (panel && IsWindowVisible(panel)) {
    ShowWindow(panel, SW_HIDE);
    hidden_at = GetTickCount();
  }
}

// close the panel when another window comes to the front: first note
// the one in front when it opened (or the panel), then watch for a change
static void panel_check(void) {
  UINT fg = foreground();
  POINT pt;
  RECT rc;

  // the mouse left the panel: no tip
  GetCursorPos(&pt);
  GetWindowRect(panel, &rc);
  if (tip_id && !PtInRect(&rc, pt)) {
    tip_hide();
    tip_id = 0;
  }

  if (!fg)
    return;
  if (fg == (UINT)panel || !panel_fg)
    panel_fg = fg;
  else if (fg != panel_fg)
    panel_hide();
}

// ess3d.exe's time and size, 0 if it can't be read
static void exe_stat(long *t, long *size) {
  char path[144];
  struct stat st;

  *t = *size = 0;
  if (GetModuleFileName(inst, path, sizeof(path)) && stat(path, &st) == 0) {
    *t = (long)st.st_mtime;
    *size = (long)st.st_size;
  }
}

// a different ess3d.exe was copied over the one this copy started from
static int exe_replaced(void) {
  long t, size;

  exe_stat(&t, &size);
  return exe_time && t && (t != exe_time || size != exe_size);
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
  // while this copy runs, every ess3d starts from it, also after a newer
  // ess3d.exe was copied over it
  if (exe_replaced()) {
    tip_say(item(IDC_P_CLOSE),
            "ESS3D.EXE was replaced while this copy runs. Click Close, then "
            "start ess3d tray again to run the new version.",
            TIP_REPLACED_MS);
    tray_log("ess3d.exe was replaced: this is still the old copy");
  }
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
    } else if (id == IDC_P_SHOWGRAPH && code == BN_CLICKED) {
      graph_toggle(!graph_on);
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
    if (wp == TIP_TIMER) {
      KillTimer(w, TIP_TIMER);
      if (tip_said) {
        tip_hide();
        tip_id = 0;
      } else {
        tip_show();
      }
      return 0;
    }
    if (wp == GRAPH_TIMER) {
      KillTimer(w, GRAPH_TIMER);
      if (graph_skip)
        graph_update(skip_row, skip_value, 0);
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
    KillTimer(w, TIP_TIMER);
    KillTimer(w, GRAPH_TIMER);
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
  WNDCLASS wc;
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
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = graph_proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  wc.hbrBackground = GetStockObject(WHITE_BRUSH);
  wc.lpszClassName = GRAPH_CLASS;
  if (!RegisterClass(&wc))
    tray_log("the graph's window class wasn't registered");
  wc.lpfnWndProc = tip_proc;
  wc.hbrBackground = 0;
  wc.lpszClassName = TIP_CLASS;
  if (!RegisterClass(&wc))
    tray_log("the tips' window class wasn't registered");
  proc = (DLGPROC)MakeProcInstance((FARPROC)panel_proc, inst);
  // owned by the tray's window, so the panel gets no taskbar button
  if (!CreateDialog(inst, "PANEL", tray, proc)) {
    tray_log("not enough memory for the panel");
    DestroyWindow(tray);
    FreeProcInstance((FARPROC)proc);
    w32_free();
    return 2;
  }
  // the tips, owned by the panel so that they stay over it
  tip = CreateWindow(TIP_CLASS, "", WS_POPUP | WS_BORDER, 0, 0, 10, 10, panel,
                     0, inst, 0);
  if (!tip)
    tray_log("no memory for the tips");

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

  // the mouse wheel moves the slider under the pointer
  wheel_init();
  exe_stat(&exe_time, &exe_size);
  while (wheel_get(&msg)) {
    // the tips, then the panel's keys: Tab, Enter and Esc
    if (panel)
      tip_watch(&msg);
    if (panel && IsDialogMessage(panel, &msg))
      continue;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  wheel_free();
  FreeProcInstance((FARPROC)proc);
  w32_free();
  return 0;
}
