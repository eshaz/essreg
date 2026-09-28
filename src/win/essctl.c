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
 *   /i2s=off     ESS's driver never gives the music DAC to the I2S
 *                input, so FM keeps it (/i2s=on: ESS's default)
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
 * A profile is written to NAME.$$$ and then renamed over the old one, so
 * a crash can't leave half a profile. The chip is read first: a busy or
 * missing chip leaves the old file as it was, and a setting the chip
 * didn't answer for keeps its old value.
 *
 * Exit codes of the batch actions: 0 done, 1 done with
 * problems, 2 failed, 3 bad command line.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "drvcfg.h"
#include "esfmlive.h"
#include "ess3dtr.h"
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
int g_closing;

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
  // committed, so the line is there after a crash
  fflush(f);
  _commit(fileno(f));
  fclose(f);
}

void msg_error(HWND owner, const char *fmt, ...) {
  char text[256];
  va_list ap;

  va_start(ap, fmt);
  _vbprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  if (quiet)
    log_line(text);
  else
    MessageBox(owner, text, "ES1869 Control", MB_OK | MB_ICONEXCLAMATION);
}

// the box that shows even when memory is low
void msg_no_memory(const char *text) {
  if (quiet)
    log_line(text);
  else
    MessageBox(0, text, "ES1869 Control", MB_OK | MB_ICONHAND | MB_SYSTEMMODAL);
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
  strncat(path, name, size - 1 - strlen(path));
}

int wait_ms(DWORD ms) {
  UINT timer = SetTimer(0, 0, (UINT)ms, 0);
  DWORD start = GetTickCount();
  MSG msg;
  int quit = 0;

  while (GetTickCount() - start < ms) {
    // GetMessage sleeps until the timer's message comes; without a
    // timer, poll and let the other programs run
    if (timer) {
      if (!GetMessage(&msg, 0, 0, 0)) {
        quit = 1;
        break;
      }
    } else if (!PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
      Yield();
      continue;
    } else if (msg.message == WM_QUIT) {
      quit = 1;
      break;
    }
    if (msg.message == WM_TIMER && !msg.hwnd && msg.wParam == timer)
      break;
    if (!g_main || !IsDialogMessage(g_main, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  if (timer)
    KillTimer(0, timer);
  // the main loop ends with it
  if (quit)
    PostQuitMessage(msg.wParam);
  return quit;
}

// --- profiles in INI files --------------------------------------------------

static int ini_get(void *ctx, const char *section, const char *key, char *buf,
                   unsigned size) {
  return GetPrivateProfileString(section, key, "", buf, size,
                                 (const char *)ctx);
}

static int ini_put(void *ctx, const char *section, const char *key,
                   const char *value) {
  return WritePrivateProfileString(section, key, value, (const char *)ctx) ? 0
                                                                           : -1;
}

static int ini_keys(void *ctx, const char *section, char *buf, unsigned size) {
  return GetPrivateProfileString(section, 0, "", buf, size, (const char *)ctx);
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
  if (r->kept)
    sprintf(tmp + strlen(tmp), ", %d kept from the old profile", r->kept);
  if (r->problem[0])
    sprintf(tmp + strlen(tmp), " (%s)", r->problem);
  strncpy(buf, tmp, size - 1);
  buf[size - 1] = 0;
}

// path with the extension ext instead of its own
static void sibling(const char *path, const char *ext, char *out,
                    unsigned size) {
  char *dot, *slash;

  strncpy(out, path, size - 5);
  out[size - 5] = 0;
  dot = strrchr(out, '.');
  slash = strrchr(out, '\\');
  if (dot && (!slash || dot > slash))
    *dot = 0;
  strcat(out, ".");
  strcat(out, ext);
}

// a profile written with plain file I/O, one section after the other
struct ini_out {
  FILE *f;
  char section[16];
  int err;
};

static int out_put(void *ctx, const char *section, const char *key,
                   const char *value) {
  struct ini_out *w = (struct ini_out *)ctx;

  if (stricmp(w->section, section)) {
    if (fprintf(w->f, "%s[%s]\n", w->section[0] ? "\n" : "", section) < 0)
      w->err = 1;
    strncpy(w->section, section, sizeof(w->section) - 1);
  }
  if (fprintf(w->f, "%s=%s\n", key, value) < 0)
    w->err = 1;
  return w->err ? -1 : 0;
}

// 1 if line starts section name
static int opens(const char *line, const char *name) {
  size_t n = strlen(name);

  return line[0] == '[' && !strnicmp(line + 1, name, n) && line[n + 1] == ']';
}

// the old profile's sections that essctl doesn't write itself, [ESFM] too
// unless this session changed the bank
static void copy_others(struct ini_out *w, const char *old) {
  char line[256];
  int copy = 0;
  FILE *f = fopen(old, "r");

  if (!f)
    return;
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '[') {
      copy = !opens(line, PROF_HEADER) && !opens(line, PROF_FIELDS) &&
             !(esfm_bank_touched && opens(line, "ESFM"));
      if (copy && fputs("\n", w->f) < 0)
        w->err = 1;
    }
    if (copy && line[strspn(line, " \t\r\n")] && fputs(line, w->f) < 0)
      w->err = 1;
  }
  // better the old file than a new one without its other sections
  if (ferror(f))
    w->err = 1;
  fclose(f);
}

// tmp becomes path; 0, or -1 with the old file still there
static int replace_file(const char *path, const char *tmp, const char *bak) {
  OFSTRUCT of;
  int had = OpenFile(path, &of, OF_EXIST) != HFILE_ERROR;

  remove(bak);
  if (had && rename(path, bak))
    return -1;
  if (rename(tmp, path)) {
    if (had)
      rename(bak, path);
    return -1;
  }
  if (had)
    remove(bak);
  return 0;
}

int profile_save_file(const char *path, char *report, unsigned size) {
  struct prof_io out, old;
  struct prof_report rep;
  struct ini_out w;
  char full[144], tmp[144], bak[144];
  OFSTRUCT of;
  int err, bad;

  full_path(path, full, sizeof(full));
  // the chip first: when it's busy or missing, the old file stays as it is
  if ((err = winio_begin()) < 0) {
    strncpy(report, esshw_strerror(err), size - 1);
    report[size - 1] = 0;
    return 2;
  }
  prof_read(&rep);
  winio_end();
  if (!rep.read) {
    _bprintf(report, size, "nothing saved, %s", rep.problem);
    return 2;
  }
  // a new file, renamed over the old one once it's all written
  sibling(full, "$$$", tmp, sizeof(tmp));
  sibling(full, "$$B", bak, sizeof(bak));
  memset(&w, 0, sizeof(w));
  w.f = fopen(tmp, "w");
  if (!w.f) {
    _bprintf(report, size, "can't create %.120s", tmp);
    return 2;
  }
  out.put = out_put;
  out.get = ini_get;
  out.keys = ini_keys;
  out.ctx = &w;
  old = out;
  old.put = ini_put;
  old.ctx = full;
  // a setting the chip didn't answer for keeps the old file's value
  prof_write(&out, OpenFile(full, &of, OF_EXIST) != HFILE_ERROR ? &old : 0,
             &rep);
  copy_others(&w, full);
  if (esfm_bank_touched && esfm_last_bank[0])
    out_put(&w, "ESFM", "Bank", esfm_last_bank);
  bad = w.err || ferror(w.f);
  if (fclose(w.f))
    bad = 1;
  if (bad || replace_file(full, tmp, bak)) {
    remove(tmp);
    _bprintf(report, size,
             "can't write %.120s (is the disk full?), the old one is kept",
             full);
    return 2;
  }
  // Windows may still have the old file in its profile cache
  WritePrivateProfileString(0, 0, 0, full);
  report_text(&rep, 0, report, size);
  return rep.failed ? 1 : 0;
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

// ess3d's tray icon shows the 3-D setting: after a write to register reg,
// or to any (-1), it reads the setting again
void tray_notify(int reg) {
  HWND tray = FindWindow(TRAY_CLASS, 0);
  int i;

  if (!tray)
    return;
  for (i = 0; reg >= 0 && i < F_COUNT; i++)
    if (ess_fields[i].reg == reg && !strncmp(ess_fields[i].key, "fx.3d.", 6))
      break;
  if (i < F_COUNT)
    PostMessage(tray, TRAY_CHANGED, 0, 0);
}

int profile_load_file(const char *path, char *report, unsigned size) {
  struct prof_io io;
  struct prof_report parsed, rep;
  char full[144];
  OFSTRUCT of;
  HCURSOR cursor;
  int err, attempt, esfm_failed, quit = 0;

  full_path(path, full, sizeof(full));
  if (OpenFile(full, &of, OF_EXIST) == HFILE_ERROR) {
    sprintf(report, "%.100s not found", full);
    return 2;
  }
  io.get = ini_get;
  io.put = ini_put;
  io.keys = ini_keys;
  io.ctx = full;
  // the file first, outside the hardware bracket
  prof_parse(&io, &parsed);
  // DSP registers are refused while Windows plays a sound (at startup, the
  // startup sound), so try again a little later
  for (attempt = 0;; attempt++) {
    rep = parsed;
    if ((err = winio_begin()) < 0) {
      rep.failed = 1;
      strncpy(rep.problem, esshw_strerror(err), sizeof(rep.problem) - 1);
    } else {
      prof_apply(&rep);
      winio_end();
    }
    if (!rep.failed || attempt == 4 || quit)
      break;
    // no other command from the window meanwhile
    if (g_main)
      EnableWindow(g_main, FALSE);
    cursor = SetCursor(LoadCursor(0, IDC_WAIT));
    quit = wait_ms(1000);
    SetCursor(cursor);
    if (g_main && IsWindow(g_main))
      EnableWindow(g_main, TRUE);
  }
  report_text(&rep, 1, report, size);
  tray_notify(-1);
  esfm_failed = quit ? 0 : load_esfm(full, report, size);
  if (!rep.applied && (rep.failed || rep.invalid || rep.unknown))
    return 2;
  return (rep.failed || rep.mismatched || rep.invalid || rep.unknown ||
          rep.refused || esfm_failed)
             ? 1
             : 0;
}

// --- register dump ----------------------------------------------------------

int dump_file(const char *path) {
  static char info[3072];
  static int raw[R_COUNT];
  char full[144];
  FILE *f;
  int i, j, v, err, bad;
  char text[48];

  full_path(path, full, sizeof(full));
  info_text(info, sizeof(info));
  // the registers into memory, then the file: no file work while the DSP
  // is held
  if ((err = winio_begin()) == 0) {
    // how to decode the Audio 1 rate: mixer 71h bit 5
    if ((v = ess_read(R_MX71)) >= 0)
      cat_a1_like_70 = (v >> 5) & 1;
    for (i = 0; i < R_COUNT; i++)
      raw[i] =
          ess_regs[i].flags & (RF_READ_SIDEFX | RF_WRITEONLY) ? 0 : ess_read(i);
    winio_end();
  }
  f = fopen(full, "w");
  if (!f)
    return 2;
  fprintf(f, "ES1869 Control %s register dump\n\n%s\n", ESSCTL_VERSION, info);
  if (err < 0)
    fprintf(f, "registers: %s\n", esshw_strerror(err));
  for (i = 0; err == 0 && i < R_COUNT; i++) {
    const struct ess_reg *r = &ess_regs[i];
    fprintf(f, "\n%-11s %02Xh  %s", ess_bank_names[r->bank], r->addr, r->name);
    if (r->flags & (RF_READ_SIDEFX | RF_WRITEONLY)) {
      fprintf(f, "  (not read)\n");
      continue;
    }
    if (raw[i] < 0) {
      fprintf(f, "  %s\n", ess_strerror(raw[i]));
      continue;
    }
    fprintf(f, "  = %02Xh\n", raw[i]);
    for (j = 0; j < F_COUNT; j++) {
      if (ess_fields[j].reg != i)
        continue;
      cat_format(&ess_fields[j], (u8)raw[i], text, sizeof(text));
      fprintf(f, "    %-34s %s\n", ess_fields[j].label, text);
    }
  }
  if (GetModuleHandle("ESFM")) {
    struct esfm_diag d;
    int rc = esfm_diag_read(&d, 1);
    esfm_diag_text(&d, rc, "\n", info, sizeof(info));
    fprintf(f, "\nESFM.DRV\n\n%s", info);
  }
  // a full disk shows here, not as a short file that looks written
  bad = ferror(f);
  if (fclose(f))
    bad = 1;
  return bad ? 2 : err < 0 ? 1 : 0;
}

// --- the music DAC ----------------------------------------------------------

// ESSWaveTableChip, which ES1869.DRV reads when Windows starts
int fmdac_set(int fm_only, char *report, unsigned size) {
  char text[200];
  int err, rc = 0;

  if (drvcfg_set(DRVCFG_WAVETABLE, fm_only ? 1 : 0)) {
    strcpy(text, "the ES1869's driver settings aren't in the registry");
    rc = 2;
  } else if (!fm_only) {
    strcpy(text, "ESS's driver gives the music DAC to I2S again from the "
                 "next Windows start");
  } else {
    // FM has the DAC now too, until a MIDI program closes before the restart
    err = winio_begin();
    if (err == 0) {
      err = ess_field_write(F_I2S_EN, 0, 0);
      winio_end();
    }
    if (err < 0) {
      sprintf(text,
              "FM keeps the music DAC from the next Windows start "
              "(clearing 7Fh bit 0 now failed: %s)",
              ess_strerror(err));
      rc = 1;
    } else {
      strcpy(text, "FM has the music DAC now, and ESS's driver keeps it "
                   "for FM from the next Windows start");
    }
  }
  strncpy(report, text, size - 1);
  report[size - 1] = 0;
  return rc;
}

// --- command line -----------------------------------------------------------

struct cmdline {
  struct winio_opts io;
  char load[128], save[128], dump[128];
  int ui, bad;
  int i2s; // /i2s=: 1 off (FM keeps the music DAC), 2 on
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
    else if (!strcmp(word + 1, "i2s") && arg && !stricmp(arg, "off"))
      c->i2s = 1;
    else if (!strcmp(word + 1, "i2s") && arg && !stricmp(arg, "on"))
      c->i2s = 2;
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
  if (c->i2s) {
    r = fmdac_set(c->i2s == 1, report, sizeof(report));
    sprintf(line, "/i2s=%s: %s", c->i2s == 1 ? "off" : "on", report);
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
    sprintf(line, "/dump %.120s: %s", c->dump,
            r == 2   ? "failed"
            : r == 1 ? "written, without the registers"
                     : "written");
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
               "       [/i2s=off|on] [/sim] [/base=220] [/cfg=800] [/novxd]",
               "ES1869 Control", MB_OK | MB_ICONINFORMATION);
    return 3;
  }

  err = winio_init(&c.io);
  if (c.load[0] || c.save[0] || c.dump[0] || c.i2s) {
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

  if (!register_classes()) {
    msg_no_memory("Not enough memory for essctl's window");
    return 2;
  }
  proc = (DLGPROC)MakeProcInstance((FARPROC)main_dlg_proc, inst);
  g_main = CreateDialog(inst, "ESSCTL", 0, proc);
  if (!g_main) {
    msg_no_memory("Not enough memory for essctl's window");
    FreeProcInstance((FARPROC)proc);
    return 2;
  }
  if (err == -ESSHW_ENODEV)
    set_status("No ES1869 answered at %03Xh: use /base=, or /sim to try "
               "essctl without the card",
               esshw.audio_base);
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
