/*
 * essctl is a control panel for the ES1869 in Windows 95,
 * written as a 16-bit Windows program.
 *
 * Usage:
 *   `essctl [options]`
 *
 *   /load file   apply a profile saved with File > Save profile
 *   /save file   save the current settings as a profile
 *   /dump file   write every readable register to a text file
 *   /ui          open the window after /load, /save or /dump
 *   /q           no message boxes, problems only go to ESSCTL.LOG
 *   /sim         use a simulated ES1869 (no hardware access)
 *   /base=220    Audio_Base (hex)
 *   /cfg=800     Config_Base (hex)
 *   /novxd       direct port I/O even with the extended ES1869.VXD
 *
 * Notes:
 *
 * Relative file names are taken from essctl's own directory.
 * Put `essctl /load C:\ESS\MY.INI` in the StartUp group to
 * restore the settings every time Windows starts.
 *
 * Exit codes of the batch actions: 0 done, 1 done with
 * problems, 2 failed, 3 bad command line.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esfmlive.h"
#include "essctl.h"
#include "esshw.h"
#include "essio.h"
#include "profile.h"
#include "resource.h"
#include "vxdapi.h"
#include "winio.h"

HINSTANCE g_inst;
HWND g_main;
HFONT g_font;
int g_expert;

static int quiet;
static char log_path[144];

// --- messages and log -------------------------------------------------------

static void log_line(const char *text) {
  FILE *f;
  char stamp[32];
  time_t now = time(0);

  if (!log_path[0])
    app_dir_file("ESSCTL.LOG", log_path, sizeof(log_path));
  f = fopen(log_path, "a");
  if (!f)
    return;
  strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
  fprintf(f, "%s %s\n", stamp, text);
  fclose(f);
}

void msg_error(HWND owner, const char *fmt, ...) {
  char text[256];
  va_list ap;

  va_start(ap, fmt);
  vsprintf(text, fmt, ap);
  va_end(ap);
  if (quiet)
    log_line(text);
  else
    MessageBox(owner, text, "ES1869 Control", MB_OK | MB_ICONEXCLAMATION);
}

int confirm(HWND owner, const char *text) {
  return MessageBox(owner, text, "ES1869 Control",
                    MB_YESNO | MB_ICONEXCLAMATION | MB_DEFBUTTON2) == IDYES;
}

// path to name in essctl.exe's directory
void app_dir_file(const char *name, char *path, unsigned size) {
  char *slash;

  GetModuleFileName(g_inst, path, size - 13);
  slash = strrchr(path, '\\');
  if (slash)
    slash[1] = 0;
  else
    path[0] = 0;
  strcat(path, name);
}

// --- profiles in INI files --------------------------------------------------

static int ini_get(void *ctx, const char *section, const char *key, char *buf,
                   unsigned size) {
  return GetPrivateProfileString(section, key, "", buf, size,
                                 (const char *)ctx);
}

static int ini_put(void *ctx, const char *section, const char *key,
                   const char *value) {
  return WritePrivateProfileString(section, key, value, (const char *)ctx)
             ? 0
             : -1;
}

static int ini_keys(void *ctx, const char *section, char *buf,
                    unsigned size) {
  return GetPrivateProfileString(section, 0, "", buf, size,
                                 (const char *)ctx);
}

// the profile functions look up a bare file name in the Windows directory,
// so make the path absolute
static void full_path(const char *in, char *out, unsigned size) {
  if (in[0] && (in[1] == ':' || in[0] == '\\')) {
    strncpy(out, in, size - 1);
    out[size - 1] = 0;
    return;
  }
  app_dir_file("", out, size);
  strncat(out, in, size - 1 - strlen(out));
}

static void report_text(const struct prof_report *r, int load, char *buf,
                        unsigned size) {
  char tmp[256];

  if (load)
    sprintf(tmp, "%d settings applied", r->applied);
  else
    sprintf(tmp, "%d settings saved", r->saved);
  if (r->unknown)
    sprintf(tmp + strlen(tmp), ", %d unknown", r->unknown);
  if (r->refused)
    sprintf(tmp + strlen(tmp), ", %d not restorable", r->refused);
  if (r->invalid)
    sprintf(tmp + strlen(tmp), ", %d invalid", r->invalid);
  if (r->failed)
    sprintf(tmp + strlen(tmp), ", %d failed", r->failed);
  if (r->mismatched)
    sprintf(tmp + strlen(tmp), ", %d did not stick", r->mismatched);
  if (r->problem[0])
    sprintf(tmp + strlen(tmp), " (%s)", r->problem);
  strncpy(buf, tmp, size - 1);
  buf[size - 1] = 0;
}

int profile_save_file(const char *path, char *report, unsigned size) {
  struct prof_io io;
  struct prof_report rep;
  char full[144];
  int err;

  full_path(path, full, sizeof(full));
  io.get = ini_get;
  io.put = ini_put;
  io.keys = ini_keys;
  io.ctx = full;
  WritePrivateProfileString(PROF_FIELDS, 0, 0, full); // drop old keys
  if ((err = winio_begin()) < 0) {
    strncpy(report, esshw_strerror(err), size - 1);
    report[size - 1] = 0;
    return 2;
  }
  prof_save(&io, &rep);
  winio_end();
  // save the ESFM bank loaded in this session, if any
  WritePrivateProfileString("ESFM", 0, 0, full);
  if (esfm_last_bank[0])
    WritePrivateProfileString("ESFM", "Bank", esfm_last_bank, full);
  WritePrivateProfileString(0, 0, 0, full); // flush the profile cache
  report_text(&rep, 0, report, size);
  if (!rep.saved)
    return 2;
  return rep.failed ? 1 : 0;
}

// wait without blocking Windows (never inside a winio bracket)
static void pause_ms(DWORD ms) {
  DWORD start = GetTickCount();
  MSG msg;

  while (GetTickCount() - start < ms)
    if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      Yield();
    }
}

// load the profile's [ESFM] Bank=file into the running ESFM.DRV and add
// the result to the report, returns 1 if that failed
static int load_esfm(const char *profile, char *report, unsigned size) {
  char bank[144], msg[160];
  int rc;

  if (GetPrivateProfileString("ESFM", "Bank", "", bank, sizeof(bank),
                              profile) <= 0)
    return 0;
  rc = esfm_live_load(bank, msg, sizeof(msg));
  if (strlen(report) + strlen(msg) + 3 < size) {
    strcat(report, "; ");
    strcat(report, msg);
  }
  return rc != 0;
}

int profile_load_file(const char *path, char *report, unsigned size) {
  struct prof_io io;
  struct prof_report rep;
  char full[144];
  OFSTRUCT of;
  int err, attempt, esfm_failed;

  full_path(path, full, sizeof(full));
  if (OpenFile(full, &of, OF_EXIST) == HFILE_ERROR) {
    sprintf(report, "%.100s not found", full);
    return 2;
  }
  io.get = ini_get;
  io.put = ini_put;
  io.keys = ini_keys;
  io.ctx = full;
  // DSP registers are refused while Windows plays a sound (at startup, the
  // startup sound), so try again a little later
  for (attempt = 0;; attempt++) {
    if ((err = winio_begin()) < 0) {
      memset(&rep, 0, sizeof(rep));
      rep.failed = 1;
      strcpy(rep.problem, esshw_strerror(err));
    } else {
      prof_load(&io, &rep);
      winio_end();
    }
    if (!rep.failed || attempt == 4)
      break;
    pause_ms(1000);
  }
  report_text(&rep, 1, report, size);
  esfm_failed = load_esfm(full, report, size);
  if (!rep.applied && (rep.failed || rep.invalid || rep.unknown))
    return 2;
  return (rep.failed || rep.mismatched || rep.invalid || rep.unknown ||
          rep.refused || esfm_failed)
             ? 1
             : 0;
}

// --- register dump ----------------------------------------------------------

int dump_file(const char *path) {
  static char info[2048];
  char full[144];
  FILE *f;
  int i, j, v, err;
  char text[48];

  full_path(path, full, sizeof(full));
  f = fopen(full, "w");
  if (!f)
    return 2;
  info_text(info, sizeof(info));
  fprintf(f, "ES1869 Control %s register dump\n\n%s\n", ESSCTL_VERSION, info);
  if ((err = winio_begin()) < 0) {
    fprintf(f, "registers: %s\n", esshw_strerror(err));
    fclose(f);
    return 1;
  }
  for (i = 0; i < R_COUNT; i++) {
    const struct ess_reg *r = &ess_regs[i];
    fprintf(f, "\n%-11s %02Xh  %s", ess_bank_names[r->bank], r->addr,
            r->name);
    if (r->flags & (RF_READ_SIDEFX | RF_WRITEONLY)) {
      fprintf(f, "  (not read)\n");
      continue;
    }
    v = ess_read(i);
    if (v < 0) {
      fprintf(f, "  %s\n", esshw_strerror(v));
      continue;
    }
    fprintf(f, "  = %02Xh\n", v);
    for (j = 0; j < F_COUNT; j++) {
      if (ess_fields[j].reg != i)
        continue;
      cat_format(&ess_fields[j], (u8)v, text, sizeof(text));
      fprintf(f, "    %-34s %s\n", ess_fields[j].label, text);
    }
  }
  winio_end();
  if (GetModuleHandle("ESFM")) {
    struct esfm_diag d;
    int rc = esfm_diag_read(&d, 1);
    esfm_diag_text(&d, rc, "\n", info, sizeof(info));
    fprintf(f, "\nESFM.DRV\n\n%s", info);
  }
  fclose(f);
  return 0;
}

// --- command line -----------------------------------------------------------

struct cmdline {
  struct winio_opts io;
  char load[128], save[128], dump[128];
  int ui, bad;
};

// next word of the command line (quoted text is one word), 0 at the end
static LPSTR next_word(LPSTR p, char *out, unsigned size) {
  unsigned n = 0;
  int quoted = 0;

  while (*p == ' ' || *p == '\t')
    p++;
  if (!*p)
    return 0;
  while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
    if (*p == '"')
      quoted = !quoted;
    else if (n + 1 < size)
      out[n++] = *p;
    p++;
  }
  out[n] = 0;
  return p;
}

static void parse_cmdline(LPSTR p, struct cmdline *c) {
  char word[128];
  char *arg;

  memset(c, 0, sizeof(*c));
  while ((p = next_word(p, word, sizeof(word))) != 0) {
    char *dest = 0;
    if (word[0] != '/' && word[0] != '-') {
      c->bad = 1;
      continue;
    }
    arg = strchr(word, '=');
    if (arg)
      *arg++ = 0;
    strlwr(word);
    if (!strcmp(word + 1, "load"))
      dest = c->load;
    else if (!strcmp(word + 1, "save"))
      dest = c->save;
    else if (!strcmp(word + 1, "dump"))
      dest = c->dump;
    else if (!strcmp(word + 1, "ui"))
      c->ui = 1;
    else if (!strcmp(word + 1, "q"))
      quiet = 1;
    else if (!strcmp(word + 1, "sim"))
      c->io.sim = 1;
    else if (!strcmp(word + 1, "novxd"))
      c->io.novxd = 1;
    else if (!strcmp(word + 1, "base") && arg)
      c->io.audio_base = (u16)strtoul(arg, 0, 16);
    else if (!strcmp(word + 1, "cfg") && arg)
      c->io.config_base = (u16)strtoul(arg, 0, 16);
    else
      c->bad = 1;
    if (dest) {
      if (!arg) {
        p = next_word(p, word, sizeof(word));
        arg = p ? word : 0;
      }
      if (arg) {
        strncpy(dest, arg, 127);
        dest[127] = 0;
      } else {
        c->bad = 1;
      }
    }
  }
}

// log every batch action, problems also show a message unless /q
static void batch_result(const char *line, int r) {
  log_line(line);
  if (r && !quiet)
    MessageBox(0, line, "ES1869 Control", MB_OK | MB_ICONEXCLAMATION);
}

static int run_batch(const struct cmdline *c) {
  char report[256], line[400];
  int rc = 0, r;

  if (c->load[0]) {
    r = profile_load_file(c->load, report, sizeof(report));
    sprintf(line, "/load %.120s: %s", c->load, report);
    batch_result(line, r);
    rc = r > rc ? r : rc;
  }
  if (c->save[0]) {
    r = profile_save_file(c->save, report, sizeof(report));
    sprintf(line, "/save %.120s: %s", c->save, report);
    batch_result(line, r);
    rc = r > rc ? r : rc;
  }
  if (c->dump[0]) {
    r = dump_file(c->dump);
    sprintf(line, "/dump %.120s: %s", c->dump, r ? "failed" : "written");
    batch_result(line, r);
    rc = r > rc ? r : rc;
  }
  return rc;
}

// --- main -------------------------------------------------------------------

static BOOL register_classes(void) {
  WNDCLASS wc;

  // the main dialog gets its own class so it has an icon
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = DefDlgProc;
  wc.cbWndExtra = DLGWINDOWEXTRA;
  wc.hInstance = g_inst;
  wc.hIcon = LoadIcon(g_inst, "ESSCTL");
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = "EssCtlDlg";
  return RegisterClass(&wc);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
  struct cmdline c;
  DLGPROC proc;
  HACCEL accel;
  HWND focus = 0;
  MSG msg;
  int err, rc;

  g_inst = inst;
  parse_cmdline(cmd, &c);
  if (c.bad) {
    MessageBox(0,
               "essctl [/load file] [/save file] [/dump file] [/ui] [/q]\n"
               "       [/sim] [/base=220] [/cfg=800] [/novxd]",
               "ES1869 Control", MB_OK | MB_ICONINFORMATION);
    return 3;
  }

  err = winio_init(&c.io);
  if (c.load[0] || c.save[0] || c.dump[0]) {
    rc = run_batch(&c);
    if (!c.ui)
      return rc;
  }
  if (prev) {
    // only one window, bring the running one to the front
    HWND other = FindWindow("EssCtlDlg", 0);
    if (other) {
      BringWindowToTop(other);
      if (IsIconic(other))
        ShowWindow(other, SW_RESTORE);
    }
    return 0;
  }

  if (!register_classes())
    return 2;
  proc = (DLGPROC)MakeProcInstance((FARPROC)main_dlg_proc, inst);
  g_main = CreateDialog(inst, "ESSCTL", 0, proc);
  if (!g_main)
    return 2;
  if (err == -ESSHW_ENODEV)
    set_status("No ES1869 answered at %03Xh: use /base=, or /sim to try "
               "essctl without the card", esshw.audio_base);
  else if (err < 0)
    set_status("%s", esshw_strerror(err));
  ShowWindow(g_main, show);
  accel = LoadAccelerators(inst, "ESSCTL");
  while (GetMessage(&msg, 0, 0, 0)) {
    if (!(accel && TranslateAccelerator(g_main, accel, &msg)) &&
        !IsDialogMessage(g_main, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    // the help line follows the keyboard focus
    if (GetFocus() != focus) {
      focus = GetFocus();
      page_focus(focus);
    }
  }
  FreeProcInstance((FARPROC)proc);
  return msg.wParam;
}
