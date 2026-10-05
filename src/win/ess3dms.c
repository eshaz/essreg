/*
 * ess3d measure: the 3-D effect measured through its own output, with a
 * report of what each setting does. s3dmeas.c makes the tones and reads
 * the recording; this is the program around it.
 *
 * Usage:
 *   `ess3d measure [quick | 54 | 56 | 58 | 5A] [/out=file] [/sim]`
 *
 *   quick      each register's ends only, about 2 minutes
 *   54 ... 5A  one register from 00h to FFh in steps of 10h
 *   /out=file  the report, ESS3D.TXT in ess3d's directory if left out
 *   /q         no window: it closes when it's done (exit code 0 done, 1
 *              cancelled, 2 failed)
 *   /sim       the made-up effect of s3dsim.c, without the card
 *
 * Notes:
 *
 * It opens ESS's Audio 2 wave output and its wave input at 48 kHz, 16-bit
 * stereo. For each run it writes the run's registers (50h, 52h-5Ah),
 * starts the recording, plays the run's tones and hands what it records
 * to the engine. ESS's driver writes 1Ch, B4h, 71h and 7Ch when a device
 * starts, so after each start ess3d sets record source 7 (the 3-D output
 * before the master volume), the record level to 0 dB, the filters of
 * both DACs on, so that no image of a tone folds back onto it in the
 * recording, and the Audio 2 volume to -4.5 dB.
 *
 * The mixer's other inputs are muted while it runs, and the master volume
 * is at its lowest step, so the speakers stay quiet. That step puts a
 * fixed -5.25 dB before the effect (DS p.59), the same for every run. The
 * driver takes 60h and 62h as its master volume at each playback start,
 * so the end plays one silent block after the mixer is back.
 *
 * Before it changes the mixer it writes the old values to ESS3D.RST, and
 * it puts them back and deletes the file at the end. If it's ended by
 * force, the next ess3d puts them back. The report goes to NAME.$$$ as
 * the runs finish, committed after each, and gets its name at the end, so
 * a crash leaves what was measured.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "ess3d.h"
#include "ess3dms.h"
#include "ess3dres.h"
#include "ess3dtr.h"
#include "esscat.h"
#include "esshw.h"
#include "s3dmeas.h"
#include "s3dsim.h"
#include "vxdapi.h"
#include "winio.h"

#define TITLE "ES1869 3-D measurement"
#define BUFS 16
#define BUF_FRAMES 4096 // 85 ms
#define TIMER_ID 1
#define TIMER_MS 100
#define STALL_MS 4000             // no block back for so long: stopped
#define WM_M_START (WM_USER + 30) // open everything, in the message loop
#define WM_M_NEXT (WM_USER + 31)  // start the next run
#define WM_M_SIM (WM_USER + 32)   // /sim: the next blocks of a run
#define A2VOL 0xCC                // Audio 2 volume, -4.5 dB (DS p.55)
#define RECLEV 0x44               // B4h: 0 dB for a line source (DS p.69)

// running states
#define S_INIT 0
#define S_RUN 1
#define S_STOP 2 // between runs
#define S_DONE 3

// the mixer registers ess3d changes, and puts back; B4h is the driver's,
// written at the start of each recording
static const u8 saved_regs[] = {0x50, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x1C,
                                0x14, 0x1A, 0x36, 0x38, 0x3A, 0x3C, 0x3E,
                                0x6D, 0x60, 0x62, 0x71, 0x7C};
#define NSAVED (sizeof(saved_regs) / sizeof(saved_regs[0]))

// the playback mixer's inputs other than Audio 2
static const u8 muted_regs[] = {0x14, 0x1A, 0x36, 0x38, 0x3A, 0x3C, 0x3E, 0x6D};

struct buf {
  HGLOBAL hh, hd;
  WAVEHDR FAR *h;
};

static const struct ess3d_cmd *cmd;
static HINSTANCE inst;
static HWND dlg;
static HGLOBAL meas_mem;
static struct s3d_meas *meas;
static struct s3dsim sim;
static HWAVEIN hwi;
static HWAVEOUT hwo;
static struct buf inb[BUFS], outb[BUFS];
static int in_queued, out_queued, next_in, next_out;
static u32 play_left, rec_left;
static int state, cur, tries, full, first = 1, cancel, nfailed;
static int saved[NSAVED];
static int marked;  // ESS3D.RST written
static int changed; // the mixer may differ from saved[]
static FILE *rep;   // the report
static int rep_bad; // writing it failed
static char rep_tmp[144], rep_path[144];
static char dev_out[40], dev_in[40];
static DWORD t_start, t_run, t_last;
static int code; // the exit code

// --- small things -----------------------------------------------------------

static void set_text(int id, const char *text) {
  SetDlgItemText(dlg, id, text);
}

static void log_line(const char *text) {
  ess3d_log(cmd->log[0] ? cmd->log : "ESS3D.LOG", text);
}

// Windows' start in time() seconds, to tell a restore file of this
// session from one left before a restart, which resets the mixer
static u32 boot_time(void) { return (u32)time(0) - GetTickCount() / 1000; }

static const char *marker_name(void) {
  // /sim's own, so a test never puts back a real measurement's mixer
  return cmd->sim ? "ESS3DSIM.RST" : "ESS3D.RST";
}

static void out_line(void *ctx, const char *line) {
  (void)ctx;
  if (!rep || rep_bad)
    return;
  if (fputs(line, rep) < 0 || fputs("\n", rep) < 0)
    rep_bad = 1;
}

// the report on the disk after each run, for a crash; 0 or -1 when the
// disk is full
static int rep_commit(void) {
  if (!rep)
    return 0;
  if (fflush(rep) || ferror(rep))
    rep_bad = 1;
  else
    _commit(fileno(rep));
  return rep_bad ? -1 : 0;
}

// --- the mixer --------------------------------------------------------------

// the old values, committed to ESS3D.RST before anything changes
static int save_mixer(void) {
  char path[144], text[300];
  unsigned i;
  int fd, n = -1, len, err;

  err = winio_begin();
  if (err < 0)
    return err;
  for (i = 0; i < NSAVED; i++) {
    saved[i] = esshw_mixer_read(saved_regs[i]);
    if (saved[i] < 0) {
      err = saved[i];
      break;
    }
  }
  winio_end();
  if (err < 0)
    return err;
  sprintf(text, "boot=%lu\r\n", (unsigned long)boot_time());
  for (i = 0; i < NSAVED; i++)
    sprintf(text + strlen(text), "%02X=%02X\r\n", saved_regs[i], saved[i]);
  len = (int)strlen(text);
  ess3d_path(marker_name(), path, sizeof(path));
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
  if (fd >= 0) {
    n = write(fd, text, len);
    _commit(fd);
    if (close(fd))
      n = -1;
  }
  if (n != len) {
    remove(path);
    return -1;
  }
  marked = 1;
  return 0;
}

static void restore_mixer(void) {
  char path[144];
  unsigned i;
  int err = 0;

  if (changed) {
    err = winio_begin();
    if (err == 0) {
      for (i = 0; i < NSAVED && err == 0; i++)
        err = esshw_mixer_write(saved_regs[i], (u8)saved[i]);
      winio_end();
    }
  }
  if (err < 0) {
    log_line("ess3d measure could not put the mixer back, ESS3D.RST keeps "
             "it for the next ess3d");
    return;
  }
  changed = 0;
  if (marked) {
    ess3d_path(marker_name(), path, sizeof(path));
    remove(path);
    marked = 0;
  }
}

// the registers of a restore file that a measurement ended by force left
static void put_back(int sim_mode) {
  char path[144], line[64];
  unsigned long boot = 0, now = boot_time();
  unsigned reg, v;
  u8 regs[NSAVED + 4], vals[NSAVED + 4];
  FILE *f;
  int i, n = 0, err;

  ess3d_path(sim_mode ? "ESS3DSIM.RST" : "ESS3D.RST", path, sizeof(path));
  f = fopen(path, "r");
  if (!f)
    return;
  // the file first, then the chip: no file I/O inside a winio bracket
  while (fgets(line, sizeof(line), f) && n < NSAVED + 4)
    if (sscanf(line, "boot=%lu", &boot) != 1 &&
        sscanf(line, "%2x=%2x", &reg, &v) == 2 && reg >= 0x14 && reg <= 0x7F) {
      regs[n] = (u8)reg;
      vals[n++] = (u8)v;
    }
  fclose(f);
  // a file from before a restart is stale: Windows set the mixer again
  if (boot + 5 < now || boot > now + 5)
    n = 0;
  if (n) {
    err = winio_begin();
    if (err < 0)
      return; // try again the next time
    for (i = 0; i < n && err == 0; i++)
      err = esshw_mixer_write(regs[i], vals[i]);
    winio_end();
    if (err < 0)
      return;
    ess3d_log("ESS3D.LOG", "ess3d put the mixer back as an interrupted 3-D "
                           "measurement had found it");
  }
  remove(path);
}

void measure_put_back(int sim_mode) {
  // while a measurement runs, the file is its own
  if (!FindWindow(MEASURE_CLASS, 0))
    put_back(sim_mode);
}

// the run's registers 50h, 52h-5Ah
static int write_run(const u8 *reg) {
  static const u8 addr[S3D_REGS] = {0x50, 0x52, 0x54, 0x56, 0x58, 0x5A};
  int i, err = winio_begin();

  if (err < 0)
    return err;
  for (i = 0; err == 0 && i < S3D_REGS; i++)
    err = esshw_mixer_write(addr[i], reg[i]);
  winio_end();
  return err;
}

// after the recording starts: record source 7, the record level, and the
// filters on and the 4x oversampling off (71h bits 4:2 clear)
static int record_path(void) {
  int err = winio_begin(), v;

  if (err < 0)
    return err;
  v = esshw_mixer_read(0x1C);
  if (v >= 0)
    v = esshw_mixer_write(0x1C, (u8)((v & ~0x17) | 0x07));
  if (v >= 0)
    v = esshw_ctrl_write(0xB4, RECLEV);
  if (v >= 0)
    v = esshw_mixer_read(0x71);
  if (v >= 0)
    v = esshw_mixer_write(0x71, (u8)(v & ~0x1C));
  // read back: a run with another level would measure another path
  if (v >= 0)
    v = (esshw_mixer_read(0x1C) & 0x17) == 0x07 &&
                esshw_ctrl_read(0xB4) == RECLEV
            ? 0
            : -1;
  winio_end();
  return v < 0 ? -1 : 0;
}

// after the playback starts: the Audio 2 volume, and 71h again
static int play_path(void) {
  int err = winio_begin(), v;

  if (err < 0)
    return err;
  v = esshw_mixer_write(0x7C, A2VOL);
  if (v >= 0)
    v = esshw_mixer_read(0x71);
  if (v >= 0)
    v = esshw_mixer_write(0x71, (u8)(v & ~0x1C));
  if (v >= 0)
    v = esshw_mixer_read(0x7C) == A2VOL ? 0 : -1;
  winio_end();
  return v < 0 ? -1 : 0;
}

// the master volume at its lowest step and the other inputs muted
static int quiet_mixer(void) {
  unsigned i;
  int err = winio_begin();

  if (err < 0)
    return err;
  for (i = 0; err == 0 && i < sizeof(muted_regs); i++)
    err = esshw_mixer_write(muted_regs[i], 0);
  if (err == 0)
    err = esshw_mixer_write(0x60, 0);
  if (err == 0)
    err = esshw_mixer_write(0x62, 0);
  winio_end();
  return err;
}

// the rates the devices run at, from 70h, and A1h with 71h bit 5; and the
// filter clocks for the report
static void read_rates(u32 *play, u32 *rec, char *info) {
  int r70, r71, ra1, r72, ra2;

  *play = *rec = S3D_RATE;
  if (cmd->sim || winio_begin() < 0)
    return;
  r70 = esshw_mixer_read(0x70);
  r71 = esshw_mixer_read(0x71);
  r72 = esshw_mixer_read(0x72);
  ra1 = esshw_ctrl_read(0xA1);
  ra2 = esshw_ctrl_read(0xA2);
  winio_end();
  if (r70 >= 0)
    *play = cat_rate_70((u8)r70);
  if (ra1 >= 0 && r71 >= 0) {
    cat_a1_like_70 = (r71 >> 5) & 1;
    *rec = cat_rate_a1((u8)ra1);
  }
  if (*play < 40000 || *play > 56000 || *rec < 40000 || *rec > 56000) {
    strcat(info, "\nthe rates read back as unlikely values, 48000 Hz "
                 "assumed");
    *play = *rec = S3D_RATE;
  }
  if (r71 >= 0 && r72 >= 0 && ra2 >= 0)
    sprintf(info + strlen(info),
            "\nwhile measuring: 71h %02Xh (filters on), 72h %02Xh, A2h "
            "%02Xh, record level B4h %02Xh, Audio 2 volume 7Ch %02Xh, master "
            "00h",
            r71 & 0xE3, r72, ra2, RECLEV, A2VOL);
}

// --- the devices ------------------------------------------------------------

static int find_out(void) {
  WAVEOUTCAPS caps;
  UINT i, n = waveOutGetNumDevs();

  for (i = 0; i < n; i++)
    if (!waveOutGetDevCaps(i, &caps, sizeof(caps)) &&
        (!strncmp(caps.szPname, "ESS AudioDrive Audio 2", 22) ||
         !strncmp(caps.szPname, "ESS AudioDrive Playback", 23))) {
      strncpy(dev_out, caps.szPname, sizeof(dev_out) - 1);
      return i;
    }
  return -1;
}

static int find_in(void) {
  WAVEINCAPS caps;
  UINT i, n = waveInGetNumDevs();

  for (i = 0; i < n; i++)
    if (!waveInGetDevCaps(i, &caps, sizeof(caps)) &&
        !strncmp(caps.szPname, "ESS AudioDrive Record", 21)) {
      strncpy(dev_in, caps.szPname, sizeof(dev_in) - 1);
      return i;
    }
  return -1;
}

// a WAVEHDR and its data in shared global memory, as MMSYSTEM wants
static int new_buffer(struct buf *b) {
  b->hh =
      GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT, sizeof(WAVEHDR));
  b->hd =
      GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT, BUF_FRAMES * 4L);
  if (!b->hh || !b->hd)
    return -1;
  b->h = (WAVEHDR FAR *)GlobalLock(b->hh);
  b->h->lpData = (LPSTR)GlobalLock(b->hd);
  b->h->dwBufferLength = BUF_FRAMES * 4L;
  return 0;
}

static void free_buffer(struct buf *b) {
  if (b->hd) {
    GlobalUnlock(b->hd);
    GlobalFree(b->hd);
  }
  if (b->hh) {
    GlobalUnlock(b->hh);
    GlobalFree(b->hh);
  }
  b->hh = b->hd = 0;
  b->h = 0;
}

// ESS's driver reads 60h and 62h back at each playback start and takes
// them as its master volume (5:3BAC), so once the mixer is back, a silent
// block gives it the old volume again, before Windows can save the low one
static void master_back(void) {
  DWORD t;

  if (!hwo || !outb[0].h)
    return;
  waveOutReset(hwo);
  memset(outb[0].h->lpData, 0, BUF_FRAMES * 4);
  outb[0].h->dwFlags &= ~WHDR_DONE;
  if (waveOutWrite(hwo, outb[0].h, sizeof(WAVEHDR)))
    return;
  // 85 ms of playing, while Windows goes on
  t = GetTickCount();
  while (!(outb[0].h->dwFlags & WHDR_DONE) && GetTickCount() - t < 1000)
    Yield();
}

static void close_devices(void) {
  int i;

  if (hwo) {
    waveOutReset(hwo);
    for (i = 0; i < BUFS; i++)
      if (outb[i].h && (outb[i].h->dwFlags & WHDR_PREPARED))
        waveOutUnprepareHeader(hwo, outb[i].h, sizeof(WAVEHDR));
    waveOutClose(hwo);
    hwo = 0;
  }
  if (hwi) {
    waveInReset(hwi);
    for (i = 0; i < BUFS; i++)
      if (inb[i].h && (inb[i].h->dwFlags & WHDR_PREPARED))
        waveInUnprepareHeader(hwi, inb[i].h, sizeof(WAVEHDR));
    waveInClose(hwi);
    hwi = 0;
  }
}

// the devices and their buffers; 0, or -1 with why in text
static int open_devices(char *text) {
  PCMWAVEFORMAT fmt;
  char err_text[80];
  int dout, din, i;
  UINT err;

  for (i = 0; i < BUFS; i++)
    if (new_buffer(&inb[i]) || new_buffer(&outb[i])) {
      strcpy(text, "Not enough memory for the measurement's buffers.");
      return -1;
    }
  if (cmd->sim)
    return 0;
  dout = find_out();
  din = find_in();
  if (dout < 0 || din < 0) {
    strcpy(text, "ESS's wave devices aren't installed: the measurement "
                 "plays on ESS AudioDrive Audio 2 and records on ESS "
                 "AudioDrive Record.");
    return -1;
  }
  fmt.wf.wFormatTag = WAVE_FORMAT_PCM;
  fmt.wf.nChannels = 2;
  fmt.wf.nSamplesPerSec = S3D_RATE;
  fmt.wf.nAvgBytesPerSec = S3D_RATE * 4;
  fmt.wf.nBlockAlign = 4;
  fmt.wBitsPerSample = 16;
  // the DSP from a DOS program that has it, as esfmrec does
  if (winio_path == WIO_VXDEXT)
    vxd_ext_take_dsp();
  err = waveInOpen(&hwi, din, (LPWAVEFORMAT)&fmt, (DWORD)(UINT)dlg, 0,
                   CALLBACK_WINDOW);
  if (!err)
    err = waveOutOpen(&hwo, dout, (LPWAVEFORMAT)&fmt, (DWORD)(UINT)dlg, 0,
                      CALLBACK_WINDOW);
  if (err) {
    if (hwi)
      waveInClose(hwi);
    hwi = 0;
    hwo = 0;
    waveInGetErrorText(err, err_text, sizeof(err_text));
    sprintf(text,
            "The wave devices can't open at 48 kHz: %.70s Close programs "
            "that play or record, and try again.",
            err_text);
    return -1;
  }
  for (i = 0; i < BUFS; i++)
    if (waveInPrepareHeader(hwi, inb[i].h, sizeof(WAVEHDR)) ||
        waveOutPrepareHeader(hwo, outb[i].h, sizeof(WAVEHDR))) {
      strcpy(text, "Not enough memory for the measurement's buffers.");
      close_devices();
      return -1;
    }
  return 0;
}

// --- the runs ---------------------------------------------------------------

// the bar, done of n runs
static void show_bar(int done, int n) {
  RECT rc;
  long w;

  GetWindowRect(GetDlgItem(dlg, IDC_M_FRAME), &rc);
  ScreenToClient(dlg, (POINT FAR *)&rc.left);
  ScreenToClient(dlg, (POINT FAR *)&rc.right);
  w = n ? (long)(rc.right - rc.left - 4) * done / n : 0;
  MoveWindow(GetDlgItem(dlg, IDC_M_FILL), rc.left + 2, rc.top + 2, (int)w,
             rc.bottom - rc.top - 4, TRUE);
}

static void show_progress(void) {
  char text[120];
  u8 reg[S3D_REGS];
  int n = s3d_count(meas), left;

  if (cur >= n)
    return;
  s3d_regs(meas, cur, reg);
  sprintf(text, "Run %d of %d: %s", cur + 1, n, s3d_name(meas, cur));
  set_text(IDC_M_RUN, text);
  sprintf(text,
          "50h %02Xh, 52h %02Xh, 54h %02Xh, 56h %02Xh, 58h %02Xh, "
          "5Ah %02Xh",
          reg[0], reg[1], reg[2], reg[3], reg[4], reg[5]);
  set_text(IDC_M_INFO, text);
  // the time left from the runs so far, or the plan's estimate
  if (cur)
    left = (int)((GetTickCount() - t_start) / 1000 * (n - cur) / cur);
  else
    left = s3d_seconds(meas);
  sprintf(text,
          "About %d min %02d s left. The speakers stay quiet: ess3d records "
          "the tones inside the chip.",
          left / 60, left % 60);
  set_text(IDC_M_LEFT, text);
  show_bar(cur, n);
  // paint now: under /sim the next blocks keep the queue full, and
  // WM_PAINT only comes when it's empty
  UpdateWindow(dlg);
}

// the end: the summary, the mixer back, the report under its name
static void finish(const char *why) {
  char text[300];
  int n = meas ? s3d_count(meas) : 0;

  if (state == S_DONE)
    return;
  state = S_DONE;
  KillTimer(dlg, TIMER_ID);
  if (hwi)
    waveInReset(hwi);
  restore_mixer();
  master_back();
  close_devices();
  if (rep) {
    if (cur > 0 && meas)
      s3d_summary(meas);
    if (why) {
      out_line(0, "");
      out_line(0, why);
    }
    rep_commit();
    fclose(rep);
    rep = 0;
    remove(rep_path);
    if (rename(rep_tmp, rep_path))
      strcpy(rep_path, rep_tmp);
  }
  if (why) {
    sprintf(text, "%.200s", why);
    log_line(text);
    code = cancel ? 1 : 2;
  } else {
    sprintf(text,
            "Done: %d runs in %lu min %02lu s%s. The report is in %.100s.", n,
            (GetTickCount() - t_start) / 60000,
            (GetTickCount() - t_start) / 1000 % 60,
            nfailed ? ", some failed" : "", rep_path);
    code = 0;
  }
  if (rep_bad) {
    strcat(text, " Writing the report failed: is the disk full?");
    code = 2;
  }
  set_text(IDC_M_LEFT, text);
  if (!why)
    show_bar(n, n);
  set_text(IDC_M_RUN, why ? "The measurement stopped." : "Finished.");
  set_text(IDC_M_INFO, "");
  set_text(IDCANCEL, "Close");
  if (rep_path[0] && !why)
    EnableWindow(GetDlgItem(dlg, IDC_M_OPEN), TRUE);
  // /q: nobody waits at the window
  if (cmd->quiet)
    PostMessage(dlg, WM_CLOSE, 0, 0);
  else
    MessageBeep(MB_OK);
}

static void queue_in(int i) {
  inb[i].h->dwFlags &= ~WHDR_DONE;
  inb[i].h->dwBytesRecorded = 0;
  if (!waveInAddBuffer(hwi, inb[i].h, sizeof(WAVEHDR))) {
    in_queued++;
    rec_left = rec_left > BUF_FRAMES ? rec_left - BUF_FRAMES : 0;
  }
}

static void queue_out(int i) {
  s3d_fill(meas, (s16 *)outb[i].h->lpData, BUF_FRAMES);
  outb[i].h->dwFlags &= ~WHDR_DONE;
  if (!waveOutWrite(hwo, outb[i].h, sizeof(WAVEHDR))) {
    out_queued++;
    play_left = play_left > BUF_FRAMES ? play_left - BUF_FRAMES : 0;
  }
}

static void end_run(int stalled) {
  char text[100];
  int res;

  state = S_STOP;
  if (!cmd->sim) {
    waveOutReset(hwo);
    waveInReset(hwi);
  }
  in_queued = out_queued = 0;
  res = stalled ? S3D_SHORT : s3d_end(meas);
  if (res & (S3D_NOSIGNAL | S3D_SHORT)) {
    sprintf(text, "ess3d measure: run %d (%s) %s", cur, s3d_name(meas, cur),
            res & S3D_NOSIGNAL ? "had no signal" : "stopped early");
    log_line(text);
    if (++tries < 2) {
      PostMessage(dlg, WM_M_NEXT, 0, 0); // once more
      return;
    }
    if (cur == 0) {
      finish("Run 0, with the effect off, had no signal in the recording. "
             "Check that ESS's driver records and that the wave volume "
             "isn't muted.");
      return;
    }
    s3d_failed(meas, res & S3D_NOSIGNAL ? "no signal in the recording"
                                        : "the recording stopped early");
    nfailed++;
  }
  tries = 0;
  cur++;
  if (rep_commit() < 0) {
    finish("Writing the report failed: is the disk full?");
    return;
  }
  PostMessage(dlg, WM_M_NEXT, 0, 0);
}

static void start_run(void) {
  char info[400], text[160];
  u8 reg[S3D_REGS];
  u32 rate_play, rate_rec;
  time_t now;
  int i;

  if (state == S_DONE)
    return;
  if (cancel) {
    finish("Cancelled.");
    return;
  }
  if (cur >= s3d_count(meas)) {
    finish(0);
    return;
  }
  show_progress();
  s3d_regs(meas, cur, reg);
  changed = 1;
  if (write_run(reg) < 0) {
    finish("Writing the 3-D registers failed.");
    return;
  }
  s3d_begin(meas, cur, &rec_left);
  play_left = meas->play_len;
  full = 0;
  t_run = t_last = GetTickCount();
  in_queued = out_queued = 0;
  next_in = next_out = 0;
  if (cmd->sim) {
    s3dsim_regs(&sim, reg);
  } else {
    // the recording first, so it starts in the silence before the tones
    for (i = 0; i < BUFS && rec_left; i++)
      queue_in(i);
    if (waveInStart(hwi) || record_path() < 0) {
      log_line("ess3d measure: the recording didn't start, or ESS's driver "
               "kept another record source or level");
      end_run(1);
      return;
    }
    for (i = 0; i < BUFS && play_left; i++)
      queue_out(i);
    if (play_path() < 0) {
      log_line("ess3d measure: the Audio 2 volume didn't stay");
      end_run(1);
      return;
    }
  }
  if (first) {
    first = 0;
    now = time(0);
    strftime(info, 40, "%Y-%m-%d %H:%M:%S, ", localtime(&now));
    winio_path_text(text, sizeof(text));
    strcat(info, text);
    if (!cmd->sim)
      sprintf(info + strlen(info), "\ndevices: %s, %s", dev_out, dev_in);
    read_rates(&rate_play, &rate_rec, info);
    s3d_header(meas, rate_play, rate_rec, info);
  }
  state = S_RUN;
  if (cmd->sim)
    PostMessage(dlg, WM_M_SIM, 0, 0);
}

// the blocks the driver gave back, in the order they were queued
static void service(void) {
  WAVEHDR FAR *h;

  if (state != S_RUN || cmd->sim)
    return;
  while (in_queued && (inb[next_in].h->dwFlags & WHDR_DONE)) {
    h = inb[next_in].h;
    in_queued--;
    t_last = GetTickCount();
    if (!full && h->dwBytesRecorded >= 4)
      full =
          s3d_take(meas, (const s16 *)h->lpData, (u16)(h->dwBytesRecorded / 4));
    if (!full && rec_left)
      queue_in(next_in);
    next_in = (next_in + 1) % BUFS;
  }
  while (out_queued && (outb[next_out].h->dwFlags & WHDR_DONE)) {
    out_queued--;
    t_last = GetTickCount();
    if (play_left)
      queue_out(next_out);
    next_out = (next_out + 1) % BUFS;
  }
  if (full || (!in_queued && !rec_left))
    end_run(0);
  else if (GetTickCount() - t_last > STALL_MS)
    end_run(1);
}

// /sim: the next blocks through the made-up effect, then the message loop
static void sim_step(void) {
  s16 *play = (s16 *)outb[0].h->lpData, *rec = (s16 *)inb[0].h->lpData;
  int k;

  if (state != S_RUN)
    return;
  for (k = 0; k < 8 && !full && rec_left; k++) {
    s3d_fill(meas, play, BUF_FRAMES);
    s3dsim_run(&sim, play, rec, BUF_FRAMES);
    full = s3d_take(meas, rec, BUF_FRAMES);
    rec_left = rec_left > BUF_FRAMES ? rec_left - BUF_FRAMES : 0;
  }
  if (full || !rec_left)
    end_run(0);
  else
    PostMessage(dlg, WM_M_SIM, 0, 0);
}

// "C:\ESS\ESS3D.TXT" gives "C:\ESS\ESS3D.$$$"
static void tmp_name(const char *path, char *out, unsigned size) {
  char *dot, *slash;

  strncpy(out, path, size - 5);
  out[size - 5] = 0;
  dot = strrchr(out, '.');
  slash = strrchr(out, '\\');
  if (dot && (!slash || dot > slash))
    *dot = 0;
  strcat(out, ".$$$");
}

static void start(void) {
  struct winio_opts io;
  char text[200];
  int err;

  memset(&io, 0, sizeof(io));
  io.audio_base = cmd->audio_base;
  io.config_base = cmd->config_base;
  io.sim = cmd->sim;
  io.novxd = cmd->novxd;
  err = winio_init(&io);
  if (err == -ESSHW_ENODEV) {
    sprintf(text,
            "No ES1869 answered at %03Xh: use /base=, or /sim to try the "
            "measurement without the card.",
            esshw.audio_base);
    finish(text);
    return;
  }
  if (err < 0) {
    sprintf(text, "ess3d could not reach the ES1869: %s.", esshw_strerror(err));
    finish(text);
    return;
  }
  put_back(cmd->sim);
  meas_mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(*meas));
  meas = meas_mem ? (struct s3d_meas *)GlobalLock(meas_mem) : 0;
  if (!meas) {
    finish("Not enough memory for the measurement.");
    return;
  }
  s3d_init(meas, cmd->plan, cmd->plan_reg, out_line, 0);
  s3dsim_reset(&sim, S3D_RATE);
  ess3d_path(cmd->out[0] ? cmd->out : "ESS3D.TXT", rep_path, sizeof(rep_path));
  tmp_name(rep_path, rep_tmp, sizeof(rep_tmp));
  rep = fopen(rep_tmp, "w");
  if (!rep) {
    sprintf(text, "Can't write %.100s.", rep_tmp);
    finish(text);
    return;
  }
  if (save_mixer() < 0) {
    finish("Can't save the mixer to ESS3D.RST, so the measurement doesn't "
           "change it.");
    return;
  }
  if (open_devices(text) < 0) {
    finish(text);
    return;
  }
  changed = 1;
  if (quiet_mixer() < 0) {
    finish("Writing the mixer failed.");
    return;
  }
  if (!SetTimer(dlg, TIMER_ID, TIMER_MS, 0)) {
    finish("No timer left in Windows.");
    return;
  }
  t_start = GetTickCount();
  log_line("ess3d measure started");
  PostMessage(dlg, WM_M_NEXT, 0, 0);
}

static void open_report(void) {
  char line[160];

  sprintf(line, "notepad.exe %.140s", rep_path);
  WinExec(line, SW_SHOWNORMAL);
}

BOOL CALLBACK __export measure_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  (void)lp;
  switch (msg) {
  case WM_INITDIALOG:
    dlg = w;
    return TRUE;
  case WM_M_START:
    start();
    return TRUE;
  case WM_M_NEXT:
    start_run();
    return TRUE;
  case WM_M_SIM:
    sim_step();
    return TRUE;
  case MM_WIM_DATA:
  case MM_WOM_DONE:
  case WM_TIMER:
    // every block that's back, whichever message comes
    service();
    return TRUE;
  case WM_COMMAND:
    if (wp == IDC_M_OPEN) {
      open_report();
    } else if (wp == IDCANCEL) {
      if (state == S_DONE) {
        DestroyWindow(w);
      } else {
        cancel = 1;
        set_text(IDC_M_LEFT, "Stopping after this run...");
        if (state == S_INIT)
          finish("Cancelled.");
      }
    }
    return TRUE;
  case WM_CLOSE:
    if (state == S_DONE)
      DestroyWindow(w);
    else
      cancel = 1;
    return TRUE;
  case WM_ENDSESSION:
    // Windows ends: the mixer back and the report saved first
    if (wp && state != S_DONE)
      finish("Windows ended.");
    return TRUE;
  case WM_DESTROY:
    // whatever happened, the mixer goes back
    if (state != S_DONE)
      finish("Ended.");
    PostQuitMessage(code);
    return TRUE;
  }
  return FALSE;
}

static BOOL register_class(void) {
  WNDCLASS wc;

  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = DefDlgProc;
  wc.cbWndExtra = DLGWINDOWEXTRA;
  wc.hInstance = inst;
  wc.hIcon = LoadIcon(inst, "ESS3D");
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = MEASURE_CLASS;
  return RegisterClass(&wc);
}

int measure_main(const struct ess3d_cmd *c, HINSTANCE hinst) {
  DLGPROC proc;
  HWND other;
  MSG msg;
  int i, q;

  cmd = c;
  inst = hinst;
  other = FindWindow(MEASURE_CLASS, 0);
  if (other) {
    BringWindowToTop(other);
    if (IsIconic(other))
      ShowWindow(other, SW_RESTORE);
    return 0;
  }
  // room for the driver's messages while Windows is busy, before any
  // window; a failed size leaves no queue, so try smaller ones
  for (q = 64; !SetMessageQueue(q) && q > 8; q /= 2)
    ;
  register_class();
  proc = (DLGPROC)MakeProcInstance((FARPROC)measure_proc, hinst);
  dlg = CreateDialog(hinst, "MEASURE", 0, proc);
  if (!dlg) {
    ess3d_log("ESS3D.LOG", "Not enough memory for the measurement's window");
    FreeProcInstance((FARPROC)proc);
    return 2;
  }
  if (c->sim)
    SetWindowText(dlg, TITLE " (simulated)");
  set_text(IDC_M_RUN, "Starting...");
  if (!c->quiet) {
    ShowWindow(dlg, SW_SHOWNORMAL);
    UpdateWindow(dlg);
  }
  PostMessage(dlg, WM_M_START, 0, 0);
  while (GetMessage(&msg, 0, 0, 0))
    if (!IsDialogMessage(dlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  FreeProcInstance((FARPROC)proc);
  for (i = 0; i < BUFS; i++) {
    free_buffer(&inb[i]);
    free_buffer(&outb[i]);
  }
  if (meas_mem) {
    GlobalUnlock(meas_mem);
    GlobalFree(meas_mem);
  }
  return code;
}
