/*
 * esfmrec records the ES1869's FM synthesizer digitally, straight from
 * the chip: the samples of its music DAC, before any analog stage. It
 * saves them unchanged in a WAV file at the music DAC's own rate, 49,716
 * Hz, 16-bit stereo.
 *
 * Usage:
 *   `esfmrec [options] [file]`
 *
 *   file       the WAV file, FMREC001.WAV and up if left out
 *   /t=N       stop after N seconds, then exit
 *   /raw       raw PCM, no WAV header
 *   /min       start minimized
 *   /q         no message boxes, problems go to ESFMREC.LOG
 *   /log=file  append each result to a file, problems too
 *   /sim       a simulated ES1869, with a test tone for the FM
 *   /base=220, /cfg=800, /novxd  as for essctl
 *
 * Notes:
 *
 * Mixer 7Fh bit 4 sends the music DAC's samples, as the FM synthesizer
 * makes them, to Audio 1's DMA in place of the ADC's, at the music DAC's
 * rate whatever rate is programmed (DS p.65). esfmrec opens ESS's wave
 * input like any recording program and sets the bit while it records,
 * with 7Fh bit 0 clear so the music DAC is FM's. Nothing analog is
 * involved: the samples don't pass the DAC, the mixer or the ADC, and
 * the volume settings don't change them.
 *
 * ESS's driver takes the average of a recording's first block off every
 * sample (its DCdrift setting). Its private message 4488h turns that off
 * for the next open, so esfmrec opens the device twice, and puts the
 * setting back at the end (docs/DRIVER_CONFIG.md).
 *
 * Audio 1 is also the channel Sound Blaster digital sound plays through.
 * A DOS program's FM music records along, but its Sound Blaster sound
 * and a recording can't run at the same time: whichever starts first
 * has the channel.
 *
 * Relative file names are taken from esfmrec's own directory.
 *
 * Exit codes: 0 done, 2 failed, 3 bad command line.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esshw.h"
#include "essio.h"
#include "fmrec.h"
#include "vxdapi.h"
#include "winio.h"

#define TITLE "ES1869 FM Recorder"
#define TIMER_SHOW 1
#define TIMER_SIM 2

#define IDC_FILE 101
#define IDC_TIME 102
#define IDC_RATE 103
#define IDC_LEVEL 104
#define IDC_REC 105

#define WIDM_DCDRIFT 0x4488 // ES1869.DRV: DC drift removal, get or set
#define REQ_RATE 48000      // what the driver is asked for, see the notes
#define BUFS 12
#define BUF_FRAMES 4096 // 16 KB, 82 ms

static const char usage[] =
    "esfmrec [options] [file]\n\n"
    "Records the FM synthesizer digitally to a WAV file at 49,716 Hz.\n\n"
    "Options: /t=N (seconds), /raw, /min, /q, /log=file, /sim, "
    "/base=220, /cfg=800, /novxd";

static struct {
  char file[128], log[128];
  u32 secs;
  int raw, min, quiet;
  struct winio_opts io;
} opt;

static HINSTANCE inst;
static HWND dlg;

static struct {
  int on, failed;
  FILE *f;
  char path[144];
  u32 bytes;          // PCM bytes written
  u32 t_first, t_now; // timeGetTime at the end of the first and last block
  u32 bytes_first;    // bytes of the first block
  u16 peak_l, peak_r; // since the display last showed them
  int prev_drec;      // 7Fh bit 4 before, -1 if esfmrec didn't set it
  int prev_i2s;       // 7Fh bit 0 before, -1 if esfmrec didn't clear it
  DWORD dcdrift;      // the driver's DCdrift before
  int dc_changed;
  u32 seq; // blocks given to the driver, which fills them in this order
  HWAVEIN hwi;
  WAVEHDR FAR *hdr[BUFS];
  HGLOBAL mem[BUFS], hmem[BUFS];
  u32 sim_t0, sim_frames, tone[2];
} rec;

static s16 sim_buf[2 * BUF_FRAMES];

// --- log and messages -------------------------------------------------------

// relative names are in esfmrec.exe's directory
static void full_path(const char *name, char *path, unsigned size) {
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

static void log_line(const char *text) {
  char path[144], stamp[32];
  time_t now = time(0);
  FILE *f;

  full_path(opt.log[0] ? opt.log : "ESFMREC.LOG", path, sizeof(path));
  f = fopen(path, "a");
  if (!f)
    return;
  strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
  fprintf(f, "%s %s\n", stamp, text);
  fclose(f);
}

// a problem goes to the log with /q or /log=, and to a message box
// unless /q
static void problem(const char *text) {
  if (opt.quiet || opt.log[0])
    log_line(text);
  if (!opt.quiet)
    MessageBox(dlg, text, TITLE, MB_OK | MB_ICONEXCLAMATION);
}

static void set_text(int id, const char *text) {
  if (dlg)
    SetDlgItemText(dlg, id, text);
}

// --- the chip ---------------------------------------------------------------

// field to value, its old value in *old; 0 or an error
static int chip_set(int field, u8 value, int *old) {
  u8 v;
  int err = winio_begin();

  if (err == 0) {
    err = ess_field_read(field, &v, 0);
    if (err == 0) {
      *old = v;
      if (v != value)
        err = ess_field_write(field, value, 0);
    }
    winio_end();
  }
  return err;
}

static void chip_restore(int field, int *old) {
  if (*old >= 0 && winio_begin() == 0) {
    ess_field_write(field, (u8)*old, 0);
    winio_end();
  }
  *old = -1;
}

// 1 if a DOS box has the DSP, and so Audio 1
static int dsp_in_dos(void) {
  struct vxd_owners o;

  if (winio_path == WIO_VXDEXT)
    return vxd_ext_owners(&o) == 0 && o.dsp == VXD_OWNER_OTHER;
  if (winio_path == WIO_DIRECT_VXD && vxd_read_adi() == 0)
    return vxd_owner_class(vxd_adi_dword(ADI_DSP_OWNER)) == VXD_OWNER_OTHER;
  return 0;
}

// --- display ----------------------------------------------------------------

static void db_text(char *out, u16 peak) {
  int db = fmrec_db10(peak);

  if (db < -900)
    strcpy(out, "silent");
  else
    sprintf(out, "%s%d.%d dB", db < 0 ? "-" : "", -db / 10, -db % 10);
}

// frames per second, from the blocks after the first; 0 before 2 s
static u32 measured_rate(void) {
  if (rec.t_now - rec.t_first < 2000)
    return 0;
  return (u32)((rec.bytes - rec.bytes_first) / 4 * 1000.0 /
               (rec.t_now - rec.t_first));
}

static void show(void) {
  char text[112], l[16], r[16];
  u32 secs = rec.bytes / 4 / FMREC_RATE, rate = measured_rate();

  sprintf(text, "%s %lu:%02lu:%02lu, %lu.%lu MB",
          rec.on ? "Recording" : "Saved", secs / 3600, secs / 60 % 60,
          secs % 60, rec.bytes / 1048576L,
          rec.bytes % 1048576L * 10 / 1048576L);
  set_text(IDC_TIME, text);
  if (rate)
    sprintf(text, "%lu Hz, measured %lu Hz", FMREC_RATE, rate);
  else
    sprintf(text, "%lu Hz, 16-bit stereo, as the FM makes it", FMREC_RATE);
  set_text(IDC_RATE, text);
  if (rec.on) {
    db_text(l, rec.peak_l);
    db_text(r, rec.peak_r);
    sprintf(text, "Peak: left %s, right %s", l, r);
    set_text(IDC_LEVEL, text);
    rec.peak_l = rec.peak_r = 0;
  }
}

// --- recording --------------------------------------------------------------

// FMREC001.WAV and up, the first that doesn't exist
static void next_name(char *path, unsigned size) {
  char name[16];
  OFSTRUCT of;
  int i;

  for (i = 1; i < 1000; i++) {
    sprintf(name, "FMREC%03d.%s", i, opt.raw ? "PCM" : "WAV");
    full_path(name, path, size);
    if (OpenFile(path, &of, OF_EXIST) == HFILE_ERROR)
      return;
  }
}

// a block of samples: into the file as they are, and into the meter
static void take(const s16 FAR *pcm, u16 frames) {
  u16 l, r;

  if (!rec.f || !frames)
    return;
  if (fwrite(pcm, 4, frames, rec.f) != frames && !rec.failed) {
    rec.failed = 1;
    problem("Writing the file failed: is the disk full?");
  }
  rec.t_now = timeGetTime();
  if (!rec.bytes) {
    rec.t_first = rec.t_now;
    rec.bytes_first = (u32)frames * 4;
  }
  rec.bytes += (u32)frames * 4;
  fmrec_peaks(pcm, frames, &l, &r);
  if (l > rec.peak_l)
    rec.peak_l = l;
  if (r > rec.peak_r)
    rec.peak_r = r;
}

// ESS's wave input, or -1
static int find_input(void) {
  WAVEINCAPS caps;
  UINT i, n = waveInGetNumDevs();

  for (i = 0; i < n; i++)
    if (!waveInGetDevCaps(i, &caps, sizeof(caps)) &&
        !strncmp(caps.szPname, "ESS AudioDrive Record", 21))
      return i;
  return -1;
}

// DC drift removal of ES1869.DRV: get (*v = 1 if on) or set; 0 or an error
static UINT dcdrift(int get, DWORD *v) {
  DWORD msg[2];
  UINT err;

  msg[0] = get;
  msg[1] = *v;
  err = (UINT)waveInMessage(rec.hwi, WIDM_DCDRIFT, (DWORD)(void FAR *)msg, 0);
  if (!err)
    *v = msg[1];
  return err;
}

static UINT open_input(int dev) {
  PCMWAVEFORMAT fmt;

  // stereo as 7Fh bit 4 wants it; the rate is the music DAC's anyway
  fmt.wf.wFormatTag = WAVE_FORMAT_PCM;
  fmt.wf.nChannels = 2;
  fmt.wf.nSamplesPerSec = REQ_RATE;
  fmt.wf.nAvgBytesPerSec = REQ_RATE * 4L;
  fmt.wf.nBlockAlign = 4;
  fmt.wBitsPerSample = 16;
  return waveInOpen(&rec.hwi, dev, (LPWAVEFORMAT)&fmt, (DWORD)dlg, 0,
                    CALLBACK_WINDOW);
}

// a WAVEHDR and its data in shared global memory, as MMSYSTEM wants
static WAVEHDR FAR *new_buffer(int i) {
  WAVEHDR FAR *h;

  rec.hmem[i] =
      GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT, sizeof(WAVEHDR));
  rec.mem[i] = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, BUF_FRAMES * 4L);
  if (!rec.hmem[i] || !rec.mem[i])
    return 0;
  h = (WAVEHDR FAR *)GlobalLock(rec.hmem[i]);
  h->lpData = (LPSTR)GlobalLock(rec.mem[i]);
  h->dwBufferLength = BUF_FRAMES * 4L;
  return h;
}

// a block back to the driver; dwUser is its place in the driver's queue
static void queue_block(WAVEHDR FAR *h) {
  h->dwUser = ++rec.seq;
  h->dwFlags &= ~WHDR_DONE;
  waveInAddBuffer(rec.hwi, h, sizeof(WAVEHDR));
}

// the device back to the driver, as it was
static void close_input(void) {
  WAVEHDR FAR *next;
  int i;

  waveInReset(rec.hwi);
  // the blocks the driver gave back that no message was handled for yet,
  // in the order it filled them
  for (;;) {
    next = 0;
    for (i = 0; i < BUFS; i++)
      if (rec.hdr[i] && (rec.hdr[i]->dwFlags & WHDR_DONE) &&
          rec.hdr[i]->dwUser && (!next || rec.hdr[i]->dwUser < next->dwUser))
        next = rec.hdr[i];
    if (!next)
      break;
    next->dwUser = 0;
    take((s16 FAR *)next->lpData, (u16)(next->dwBytesRecorded / 4));
  }
  chip_restore(F_MUSIC_DREC, &rec.prev_drec);
  chip_restore(F_I2S_EN, &rec.prev_i2s);
  if (rec.dc_changed)
    dcdrift(0, &rec.dcdrift);
  for (i = 0; i < BUFS; i++) {
    if (rec.hdr[i])
      waveInUnprepareHeader(rec.hwi, rec.hdr[i], sizeof(WAVEHDR));
    if (rec.mem[i]) {
      GlobalUnlock(rec.mem[i]);
      GlobalFree(rec.mem[i]);
    }
    if (rec.hmem[i]) {
      GlobalUnlock(rec.hmem[i]);
      GlobalFree(rec.hmem[i]);
    }
    rec.hdr[i] = 0;
    rec.mem[i] = rec.hmem[i] = 0;
  }
  waveInClose(rec.hwi);
}

static int open_recording(void) {
  char text[200];
  DWORD off = 0;
  UINT err;
  int dev, i;

  dev = find_input();
  if (dev < 0) {
    problem("ESS's wave input (ES1869.DRV) isn't installed");
    return -1;
  }
  err = open_input(dev);
  if (!err && dcdrift(1, &rec.dcdrift) == 0 && rec.dcdrift) {
    // off for the next open, which is where the driver looks at it
    rec.dc_changed = dcdrift(0, &off) == 0;
    waveInClose(rec.hwi);
    err = open_input(dev);
  }
  if (err) {
    if (rec.dc_changed && open_input(dev) == 0) {
      dcdrift(0, &rec.dcdrift);
      waveInClose(rec.hwi);
    }
    if (err == MMSYSERR_ALLOCATED && dsp_in_dos())
      strcpy(text, "A DOS program is playing Sound Blaster sound. The "
                   "FM is recorded through the same channel, so esfmrec "
                   "can start once it stops");
    else if (err == MMSYSERR_ALLOCATED)
      strcpy(text, "Another program is recording");
    else
      waveInGetErrorText(err, text, sizeof(text));
    problem(text);
    return -1;
  }
  for (i = 0; i < BUFS; i++) {
    rec.hdr[i] = new_buffer(i);
    if (!rec.hdr[i] ||
        waveInPrepareHeader(rec.hwi, rec.hdr[i], sizeof(WAVEHDR))) {
      if (rec.hdr[i])
        rec.hdr[i]->dwFlags = 0;
      problem("Out of memory for the recording buffers");
      close_input();
      return -1;
    }
    queue_block(rec.hdr[i]);
  }
  // the music DAC's samples in place of the ADC's, from FM, while the
  // device is Windows'
  if (chip_set(F_I2S_EN, 0, &rec.prev_i2s) ||
      chip_set(F_MUSIC_DREC, 1, &rec.prev_drec)) {
    problem("The ES1869 didn't answer, so the FM can't be recorded");
    close_input();
    return -1;
  }
  waveInStart(rec.hwi);
  return 0;
}

static void write_header(void) {
  u8 hdr[FMREC_HEADER];

  if (opt.raw || !rec.f)
    return;
  fmrec_wav_header(hdr, FMREC_RATE, rec.bytes);
  fseek(rec.f, 0, SEEK_SET);
  fwrite(hdr, 1, sizeof(hdr), rec.f);
}

static void rec_stop(void) {
  char line[256];
  u32 rate;

  if (!rec.on)
    return;
  rec.on = 0;
  if (opt.io.sim)
    KillTimer(dlg, TIMER_SIM);
  else
    close_input();
  write_header();
  fclose(rec.f);
  rec.f = 0;
  sprintf(line, "%s: %lu.%lu s, %lu bytes at %lu Hz", rec.path,
          rec.bytes / 4 / FMREC_RATE,
          rec.bytes / 4 % FMREC_RATE * 10 / FMREC_RATE, rec.bytes, FMREC_RATE);
  // over 30 s the timer is good to 0.05%: a rate 0.1% below the music
  // DAC's means the driver lost blocks
  rate = measured_rate();
  if (rate && rec.t_now - rec.t_first >= 30000 && rate + 50 < FMREC_RATE)
    sprintf(line + strlen(line), ", but only %lu Hz came in: samples were lost",
            rate);
  if (opt.log[0] || opt.quiet)
    log_line(line);
  set_text(IDC_REC, "&Record");
  show();
}

static void rec_start(void) {
  char text[200];
  u8 hdr[FMREC_HEADER];

  if (rec.on)
    return;
  memset(&rec, 0, sizeof(rec));
  rec.prev_drec = rec.prev_i2s = -1;
  if (opt.file[0]) {
    full_path(opt.file, rec.path, sizeof(rec.path));
    opt.file[0] = 0; // the next recording gets a new name
  } else {
    next_name(rec.path, sizeof(rec.path));
  }
  rec.f = fopen(rec.path, "wb");
  if (!rec.f) {
    sprintf(text, "Can't create %.120s", rec.path);
    problem(text);
    rec.failed = 1;
    return;
  }
  if (!opt.raw) {
    fmrec_wav_header(hdr, FMREC_RATE, 0);
    fwrite(hdr, 1, sizeof(hdr), rec.f);
  }
  if (opt.io.sim) {
    rec.sim_t0 = timeGetTime();
    SetTimer(dlg, TIMER_SIM, 50, 0);
  } else if (open_recording()) {
    fclose(rec.f);
    rec.f = 0;
    remove(rec.path);
    rec.failed = 1;
    return;
  }
  rec.on = 1;
  set_text(IDC_FILE, rec.path);
  set_text(IDC_REC, "&Stop");
  show();
}

// /sim: the test tone, as much as the time since the start calls for
static void sim_tick(void) {
  u32 due = (u32)((timeGetTime() - rec.sim_t0) * (FMREC_RATE / 1000.0));
  u16 n;

  while (rec.on && rec.sim_frames < due) {
    n = (u16)(due - rec.sim_frames > BUF_FRAMES ? BUF_FRAMES
                                                : due - rec.sim_frames);
    // /t= gets exactly its frames
    if (opt.secs && rec.sim_frames + n > opt.secs * FMREC_RATE)
      n = (u16)(opt.secs * FMREC_RATE - rec.sim_frames);
    fmrec_tone(sim_buf, n, rec.tone);
    take(sim_buf, n);
    rec.sim_frames += n;
    if (!n)
      break;
  }
}

// --- window -----------------------------------------------------------------

static void timed_stop(void) {
  if (opt.secs && rec.on && rec.bytes / 4 >= opt.secs * FMREC_RATE) {
    rec_stop();
    DestroyWindow(dlg);
  }
}

BOOL CALLBACK __export rec_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  WAVEHDR FAR *h;

  switch (msg) {
  case WM_INITDIALOG:
    dlg = w;
    SetTimer(w, TIMER_SHOW, 250, 0);
    return TRUE;

  case MM_WIM_DATA:
    h = (WAVEHDR FAR *)lp;
    // blocks after a stop were taken in close_input
    if (rec.on && (HWAVEIN)wp == rec.hwi && h->dwUser) {
      take((s16 FAR *)h->lpData, (u16)(h->dwBytesRecorded / 4));
      queue_block(h);
      timed_stop();
    }
    return TRUE;

  case WM_TIMER:
    if (wp == TIMER_SIM) {
      sim_tick();
      timed_stop();
    } else if (rec.on) {
      show();
    }
    return TRUE;

  case WM_COMMAND:
    if (wp == IDC_REC) {
      if (rec.on)
        rec_stop();
      else
        rec_start();
    } else if (wp == IDCANCEL) {
      DestroyWindow(w);
    }
    return TRUE;

  case WM_ENDSESSION:
    if (wp)
      rec_stop();
    return TRUE;

  case WM_CLOSE:
    DestroyWindow(w);
    return TRUE;

  case WM_DESTROY:
    rec_stop();
    KillTimer(w, TIMER_SHOW);
    PostQuitMessage(0);
    return TRUE;
  }
  return FALSE;
}

// --- main -------------------------------------------------------------------

static int parse(LPSTR p) {
  char word[128], *arg;
  unsigned n;

  memset(&opt, 0, sizeof(opt));
  for (;;) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (!*p)
      return 0;
    n = 0;
    while (*p && *p != ' ' && *p != '\t' && n + 1 < sizeof(word))
      word[n++] = *p++;
    word[n] = 0;
    if (word[0] != '/' && word[0] != '-') {
      if (opt.file[0])
        return -1;
      strcpy(opt.file, word);
      continue;
    }
    arg = strchr(word, '=');
    if (arg)
      *arg++ = 0;
    strlwr(word);
    if (!strcmp(word + 1, "t") && arg && atol(arg) > 0)
      opt.secs = atol(arg);
    else if (!strcmp(word + 1, "raw"))
      opt.raw = 1;
    else if (!strcmp(word + 1, "min"))
      opt.min = 1;
    else if (!strcmp(word + 1, "q"))
      opt.quiet = 1;
    else if (!strcmp(word + 1, "log") && arg)
      strncpy(opt.log, arg, sizeof(opt.log) - 1);
    else if (!strcmp(word + 1, "sim"))
      opt.io.sim = 1;
    else if (!strcmp(word + 1, "novxd"))
      opt.io.novxd = 1;
    else if (!strcmp(word + 1, "base") && arg)
      opt.io.audio_base = (u16)strtoul(arg, 0, 16);
    else if (!strcmp(word + 1, "cfg") && arg)
      opt.io.config_base = (u16)strtoul(arg, 0, 16);
    else
      return -1;
  }
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE prev, LPSTR cmdline, int show) {
  char text[160];
  DLGPROC proc;
  MSG msg;
  int err;

  (void)prev;
  inst = hinst;
  if (parse(cmdline) < 0) {
    if (!opt.quiet)
      MessageBox(0, usage, TITLE, MB_OK | MB_ICONINFORMATION);
    return 3;
  }
  err = winio_init(&opt.io);
  if (err == -ESSHW_ENODEV) {
    sprintf(text,
            "No ES1869 answered at %03Xh: use /base=, or /sim to try "
            "esfmrec without the card",
            esshw.audio_base);
    problem(text);
    return 2;
  }
  if (err < 0) {
    sprintf(text, "esfmrec could not reach the ES1869: %s",
            esshw_strerror(err));
    problem(text);
    return 2;
  }
  proc = (DLGPROC)MakeProcInstance((FARPROC)rec_proc, inst);
  dlg = CreateDialog(inst, "ESFMREC", 0, proc);
  if (!dlg)
    return 2;
  ShowWindow(dlg, opt.min ? SW_SHOWMINNOACTIVE : show);
  rec_start();
  // with /t= esfmrec is a batch job: nothing to do if it couldn't start
  if (rec.failed && opt.secs)
    DestroyWindow(dlg);
  while (GetMessage(&msg, 0, 0, 0))
    if (!IsDialogMessage(dlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  FreeProcInstance((FARPROC)proc);
  return rec.failed ? 2 : 0;
}
