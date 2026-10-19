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
 *   /split=N   go on in a new file every N seconds
 *   /raw       raw PCM, no WAV header
 *   /min       start minimized
 *   /q         no message boxes, problems only go to the log
 *   /log=file  the log, ESFMREC.LOG if left out
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
 * Audio 1 is also the channel Sound Blaster digital sound plays through,
 * and a DOS program takes it with its first Sound Blaster access, until it
 * ends. So before each open, esfmrec asks the extended ES1869.VXD for it
 * (function 040D, RecordTakesDSP): the program goes on with a virtual
 * Sound Blaster, timed but silent, and its FM music records along. ESS's
 * VxD keeps the channel with the program.
 *
 * Windows 98 can hang, crash or lose power in the middle of a recording:
 * - every 5 s the WAV header gets the length so far and the file is
 *   committed to the disk, so a crash loses the last seconds, not the file
 * - no file passes 2 h 45 min (1,968,753,600 bytes, under FAT16's 2 GB):
 *   the recording goes on without a gap in NAME_2.WAV, or the next
 *   FMRECnnn.WAV
 * - a full disk stops the recording, and what was written stays playable
 * - nothing shows a message box while blocks come in: a box would run
 *   the next blocks inside it
 * - before it changes the chip or the driver, esfmrec writes the old
 *   settings to ESFMREC.RST. If it's ended by force, its next start puts
 *   them back and repairs the file's header
 * - 32 blocks of 82 ms (2.6 s) ride out a busy Windows, and the timer
 *   picks up a block whose message got lost
 * - every 2 s it checks 7Fh, and sets bits 4 and 0 again when ESS's driver
 *   changed them (a mixer reset, or a MIDI program closing)
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

#include <fcntl.h>
#include <io.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
#define IDC_CLOSE 106

#define WIDM_DCDRIFT 0x4488 // ES1869.DRV: DC drift removal, get or set
#define REQ_RATE 48000      // what the driver is asked for, see the notes
#define BUFS 32             // 2.6 s
#define BUF_FRAMES 4096     // 16 KB, 82 ms
#define SHOW_MS 250
#define SAVE_MS 5000  // the header and a commit
#define CHECK_MS 2000 // 7Fh
#define QUIET_MS 5000 // no block for this long goes to the log

static const char usage[] =
    "esfmrec [options] [file]\n\n"
    "Records the FM synthesizer digitally to a WAV file at 49,716 Hz.\n\n"
    "Options: /t=N (seconds), /split=N (a new file every N seconds), /raw, "
    "/min, /q, /log=file, /sim, /base=220, /cfg=800, /novxd";

static struct {
  char file[128], log[128];
  u32 secs, split; // /t= and /split=, in seconds
  int raw, min, quiet;
  struct winio_opts io;
} opt;

static HINSTANCE inst;
static HWND dlg;
static int other;         // another esfmrec runs
static char failure[200]; // why the recording has to stop (see fail)

static struct {
  int on, failed;
  int fd;          // the file, -1 if none
  char path[144];  // the file being written
  char first[144]; // the file named on the command line, its parts follow
  unsigned part;   // 1 for the first file
  u32 part_frames; // frames in this file
  u32 secs, frac;  // recorded: whole seconds and the frames after them
  u32 t_start;     // timeGetTime at the start
  u32 t_now;       // and at the last block
  u32 t_first;     // and at the end of the first block
  u32 t_saved, t_checked;
  double first_frames; // frames at the end of the first block
  int got_first, quiet, refused;
  int queued;         // blocks the driver holds
  unsigned dry;       // times it held none, so samples were lost
  unsigned dry_new;   // of those, not in the log yet
  u16 peak_l, peak_r; // since the display last showed them
  int prev_drec;      // 7Fh bit 4 before, -1 if esfmrec didn't set it
  int prev_i2s;       // 7Fh bit 0 before, -1 if esfmrec didn't clear it
  DWORD dcdrift;      // the driver's DCdrift before
  long dc_restore;    // DCdrift to put back, -1 if it stays
  int dc_changed;
  int marked; // the marker file holds the settings to put back
  u32 seq;    // blocks given to the driver, which fills them in this order
  int dev_open;
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

// a line with the time, committed to the disk so a crash can't lose it
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
  fflush(f);
  _commit(fileno(f));
  fclose(f);
}

// a problem goes to the log, and to a message box unless /q
// never while blocks come in (see fail): the box would take them
static void problem(const char *text) {
  log_line(text);
  if (!opt.quiet)
    MessageBox(dlg, text, TITLE, MB_OK | MB_ICONEXCLAMATION);
}

// the box that shows even when memory is low
static void no_memory(const char *text) {
  log_line(text);
  if (!opt.quiet)
    MessageBox(dlg, text, TITLE, MB_OK | MB_ICONHAND | MB_SYSTEMMODAL);
}

// the recording has to stop: the next tick stops it and says why, away
// from the blocks' path, where a box is safe
static void fail(const char *text) {
  if (rec.failed)
    return;
  rec.failed = 1;
  strncpy(failure, text, sizeof(failure) - 1);
}

static void set_text(int id, const char *text) {
  if (dlg)
    SetDlgItemText(dlg, id, text);
}

// h:mm:ss of the time recorded
static void clock_text(char *out) {
  sprintf(out, "%lu:%02lu:%02lu", rec.secs / 3600, rec.secs / 60 % 60,
          rec.secs % 60);
}

// --- the chip ---------------------------------------------------------------

// a field's value, or -1
static int chip_get(int field) {
  u8 v;
  int err = winio_begin();

  if (err == 0) {
    err = ess_field_read(field, &v, 0);
    winio_end();
  }
  return err < 0 ? -1 : v;
}

static int chip_put(int field, u8 value) {
  int err = winio_begin();

  if (err == 0) {
    err = ess_field_write(field, value, 0);
    winio_end();
  }
  return err;
}

// 1 if a DOS box has the DSP, and so Audio 1
static int dsp_in_dos(void) {
  struct vxd_owners o;

  if (winio_path == WIO_VXDEXT)
    return vxd_ext_owners(&o) == 0 && o.dsp == VXD_OWNER_OTHER;
  if (winio_path == WIO_DIRECT_VXD) {
    // the stock driver: an acquire tells a DOS box from a Windows program
    if (winio_begin() == -ESSHW_EINUSE)
      return 1;
    winio_end();
  }
  return 0;
}

// 1 if a Windows program has the FM open: ESS's driver gives FM the
// music DAC then, and I2S when it closes
static int fm_in_windows(void) {
  struct vxd_owners o;

  if (winio_path == WIO_VXDEXT)
    return vxd_ext_owners(&o) == 0 && o.fm == VXD_OWNER_SELF;
  if (winio_path == WIO_DIRECT_VXD && vxd_read_adi() == 0)
    return vxd_owner_class(vxd_adi_dword(ADI_FM_OWNER)) == VXD_OWNER_SELF;
  return 0;
}

// 7Fh bit 0 back, unless a MIDI program opened meanwhile and so has the
// music DAC for FM
static int restore_i2s(int old) {
  if (old < 0 || (old == 1 && fm_in_windows()))
    return 0;
  return chip_put(F_I2S_EN, (u8)old);
}

// a driver may change 7Fh while esfmrec records: a mixer reset clears
// bit 4, a MIDI program's close sets bit 0. Set them again
static void check_chip(void) {
  char text[120], at[16];
  u8 drec = 1, i2s = 0;
  int fixed_drec = 0, fixed_i2s = 0;

  if (winio_begin())
    return;
  if (!ess_field_read(F_MUSIC_DREC, &drec, 0) && !drec)
    fixed_drec = !ess_field_write(F_MUSIC_DREC, 1, 0);
  if (!ess_field_read(F_I2S_EN, &i2s, 0) && i2s)
    fixed_i2s = !ess_field_write(F_I2S_EN, 0, 0);
  winio_end();
  // the log after the bracket: no file work while the DSP is held
  clock_text(at);
  if (fixed_drec) {
    sprintf(text,
            "Music DAC digital record went off (a mixer reset), on again "
            "at %s",
            at);
    log_line(text);
  }
  if (fixed_i2s) {
    sprintf(text,
            "I2S took the music DAC (a MIDI program closed), FM has it "
            "again at %s",
            at);
    log_line(text);
  }
}

// --- the marker: settings to put back after a crash --------------------------

static const char *marker_name(void) {
  // /sim's own, so a test never puts back a real recording's settings
  return opt.io.sim ? "ESFMSIM.RST" : "ESFMREC.RST";
}

// Windows' start in time() seconds, to tell this session's marker from
// one left before a restart (which resets the chip and the driver)
static u32 boot_time(void) { return (u32)time(0) - GetTickCount() / 1000; }

// the old settings, committed before esfmrec changes anything
static void marker_write(void) {
  char path[144], text[300];
  int fd, n = -1, len;

  if (other && opt.io.sim)
    return; // two /sim recordings could share it
  full_path(marker_name(), path, sizeof(path));
  sprintf(text, "file=%s\r\nboot=%lu\r\ndrec=%d\r\ni2s=%d\r\ndcdrift=%ld\r\n",
          rec.path, boot_time(), rec.prev_drec, rec.prev_i2s, rec.dc_restore);
  len = (int)strlen(text);
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
  if (fd >= 0) {
    n = write(fd, text, len);
    _commit(fd); // at best: not every file system has it
    if (close(fd))
      n = -1;
  }
  if (n == len) {
    rec.marked = 1;
  } else if (!rec.marked) {
    sprintf(text,
            "Can't write %.100s: if esfmrec is ended by force, the chip goes "
            "on recording FM until Windows restarts",
            path);
    log_line(text);
  }
}

static void marker_delete(void) {
  char path[144];

  if (!rec.marked)
    return;
  full_path(marker_name(), path, sizeof(path));
  remove(path);
  rec.marked = 0;
}

// --- the device -------------------------------------------------------------

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

// Audio 1 from a DOS program that has it, for the open after this; the
// open finds out whether the VxD gave it
static void take_dsp(void) {
  if (winio_path == WIO_VXDEXT)
    vxd_ext_take_dsp();
}

// notify: blocks come back as MM_WIM_DATA to the window
static UINT open_input(int dev, int notify) {
  PCMWAVEFORMAT fmt;

  // stereo as 7Fh bit 4 wants it; the rate is the music DAC's anyway
  fmt.wf.wFormatTag = WAVE_FORMAT_PCM;
  fmt.wf.nChannels = 2;
  fmt.wf.nSamplesPerSec = REQ_RATE;
  fmt.wf.nAvgBytesPerSec = REQ_RATE * 4L;
  fmt.wf.nBlockAlign = 4;
  fmt.wBitsPerSample = 16;
  return waveInOpen(&rec.hwi, dev, (LPWAVEFORMAT)&fmt, notify ? (DWORD)dlg : 0,
                    0, notify ? CALLBACK_WINDOW : CALLBACK_NULL);
}

// the driver's DCdrift to v, through an open of its own; 0 or -1
static int dc_put(DWORD v) {
  int dev = find_input(), err;

  if (dev < 0)
    return 0; // no ES1869.DRV, no setting
  if (open_input(dev, 0))
    return -1;
  err = dcdrift(0, &v);
  waveInClose(rec.hwi);
  return err ? -1 : 0;
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
static UINT queue_block(WAVEHDR FAR *h) {
  UINT err;

  h->dwUser = ++rec.seq;
  h->dwFlags &= ~WHDR_DONE;
  err = waveInAddBuffer(rec.hwi, h, sizeof(WAVEHDR));
  if (!err)
    rec.queued++;
  if (err && rec.on && !rec.refused) {
    rec.refused = 1;
    log_line("The driver refused a recording block, there are fewer left");
  }
  return err;
}

// --- the file ---------------------------------------------------------------

// FMREC001.WAV and up, the first that doesn't exist; 0, or -1 if all do
static int next_name(char *path, unsigned size) {
  char name[16];
  OFSTRUCT of;
  int i;

  for (i = 1; i < 1000; i++) {
    sprintf(name, "FMREC%03d.%s", i, opt.raw ? "PCM" : "WAV");
    full_path(name, path, size);
    if (OpenFile(path, &of, OF_EXIST) == HFILE_ERROR)
      return 0;
  }
  return -1;
}

// rec.path, with a header saying it's empty so far; 0 or -1
static int file_open(void) {
  u8 hdr[FMREC_HEADER];

  rec.part_frames = 0;
  rec.fd = open(rec.path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
                S_IREAD | S_IWRITE);
  if (rec.fd < 0)
    return -1;
  if (!opt.raw) {
    fmrec_wav_header(hdr, FMREC_RATE, 0);
    if (write(rec.fd, hdr, FMREC_HEADER) != FMREC_HEADER) {
      close(rec.fd);
      rec.fd = -1;
      return -1;
    }
  }
  return 0;
}

// the header with the length so far, and everything committed to the
// disk, directory entry too; 0 or -1
static int file_save(void) {
  u8 hdr[FMREC_HEADER];
  int err = 0;

  if (rec.fd < 0)
    return 0;
  if (!opt.raw) {
    fmrec_wav_header(hdr, FMREC_RATE, rec.part_frames * 4);
    if (lseek(rec.fd, 0L, SEEK_SET) != 0 ||
        write(rec.fd, hdr, FMREC_HEADER) != FMREC_HEADER ||
        lseek(rec.fd, 0L, SEEK_END) < 0)
      err = -1;
  }
  // INT 21h 68h: at best, a network drive may not have it
  _commit(rec.fd);
  return err;
}

// the file done: its header, and its line in the log; 0 or -1
static int file_close(const char *more) {
  char line[320];
  u32 f = rec.part_frames;
  int err;

  if (rec.fd < 0)
    return 0;
  err = file_save();
  if (close(rec.fd))
    err = -1;
  rec.fd = -1;
  sprintf(line, "%.140s: %lu.%lu s, %lu bytes at %lu Hz%.120s", rec.path,
          f / FMREC_RATE, f % FMREC_RATE * 10 / FMREC_RATE, f * 4, FMREC_RATE,
          more);
  log_line(line);
  return err;
}

// frames in each file: /split= or 2 h 45 min
static u32 part_limit(void) {
  u32 s =
      opt.split && opt.split < FMREC_PART_SECS ? opt.split : FMREC_PART_SECS;

  return s * FMREC_RATE;
}

// the next file of a long recording; 0 or -1
static int next_part(void) {
  char text[200];
  int err;

  if (file_close("")) {
    sprintf(text, "Writing %.140s failed: is the disk full?", rec.path);
    fail(text);
    return -1;
  }
  rec.part++;
  err = rec.first[0]
            ? fmrec_part_name(rec.first, rec.part, rec.path, sizeof(rec.path))
            : next_name(rec.path, sizeof(rec.path));
  if (err || file_open()) {
    sprintf(text, "Can't create the recording's next file, %.120s", rec.path);
    fail(text);
    return -1;
  }
  marker_write(); // the file to repair after a crash
  set_text(IDC_FILE, rec.path);
  sprintf(text, "Recording goes on in %.140s", rec.path);
  log_line(text);
  return 0;
}

// --- recording --------------------------------------------------------------

static double frames_done(void) {
  return (double)rec.secs * FMREC_RATE + rec.frac;
}

// frames until /t= is reached, at most a minute's
static u32 frames_left(void) {
  u32 s;

  if (!opt.secs)
    return 60 * FMREC_RATE;
  if (rec.secs >= opt.secs)
    return 0;
  s = opt.secs - rec.secs;
  return s > 60 ? 60 * FMREC_RATE : s * FMREC_RATE - rec.frac;
}

// frames to the file; 0, or -1 after asking to stop (a full disk)
static int write_frames(const s16 FAR *pcm, u16 frames) {
  char text[200];
  int want = (int)(frames * 4), n = write(rec.fd, pcm, (unsigned)want);
  u32 got = n > 0 ? (u32)n / 4 : 0;

  rec.part_frames += got;
  rec.frac += got;
  while (rec.frac >= FMREC_RATE) {
    rec.frac -= FMREC_RATE;
    rec.secs++;
  }
  if (n != want) {
    sprintf(text, "Writing %.140s failed: is the disk full?", rec.path);
    fail(text);
    return -1;
  }
  return 0;
}

// a block of samples: into the file as they are, and into the meter
static void take(const s16 FAR *pcm, u16 frames) {
  char text[80];
  u16 l, r;
  u32 n;

  if (rec.fd < 0 || rec.failed || !frames)
    return;
  // /t= ends the recording at its frame
  if (frames > frames_left())
    frames = (u16)frames_left();
  if (!frames)
    return;
  rec.t_now = timeGetTime();
  if (rec.quiet) {
    rec.quiet = 0;
    clock_text(text);
    strcat(text, ": samples come in again");
    log_line(text);
  }
  fmrec_peaks(pcm, frames, &l, &r);
  if (l > rec.peak_l)
    rec.peak_l = l;
  if (r > rec.peak_r)
    rec.peak_r = r;
  // a file that's full goes on in the next one, without a gap
  while (frames) {
    if (rec.part_frames >= part_limit() && next_part())
      return;
    n = part_limit() - rec.part_frames;
    if (n > frames)
      n = frames;
    if (write_frames(pcm, (u16)n))
      return;
    pcm += 2 * (u16)n;
    frames -= (u16)n;
  }
  if (!rec.got_first) {
    rec.got_first = 1;
    rec.t_first = rec.t_now;
    rec.first_frames = frames_done();
  }
}

// the blocks the driver filled, in the order it filled them: not only
// the one a message came for, since a message may be lost. Each goes
// back to the driver, unless the recording is closing
static void drain(int requeue) {
  WAVEHDR FAR *next;
  int i, done = 0;

  // every block the driver had is back: it has none for the samples that
  // come in until one goes back, and they're lost
  for (i = 0; requeue && i < BUFS; i++)
    if (rec.hdr[i] && (rec.hdr[i]->dwFlags & WHDR_DONE) && rec.hdr[i]->dwUser)
      done++;
  if (done && done == rec.queued) {
    rec.dry++;
    rec.dry_new++;
  }
  for (;;) {
    next = 0;
    for (i = 0; i < BUFS; i++)
      if (rec.hdr[i] && (rec.hdr[i]->dwFlags & WHDR_DONE) &&
          rec.hdr[i]->dwUser && (!next || rec.hdr[i]->dwUser < next->dwUser))
        next = rec.hdr[i];
    if (!next)
      return;
    next->dwUser = 0;
    rec.queued--;
    take((s16 FAR *)next->lpData, (u16)(next->dwBytesRecorded / 4));
    if (requeue)
      queue_block(next);
  }
}

// the device and the buffers, back to the driver; the blocks it holds
// go to the file first
static void close_device(void) {
  if (opt.io.sim)
    KillTimer(dlg, TIMER_SIM);
  if (!rec.dev_open)
    return;
  waveInReset(rec.hwi);
  drain(0);
}

// the chip and the driver as they were, then the marker goes
static void put_back(void) {
  char text[160];
  int ok = 1, i;

  if (rec.prev_drec >= 0 && chip_put(F_MUSIC_DREC, (u8)rec.prev_drec))
    ok = 0;
  if (restore_i2s(rec.prev_i2s))
    ok = 0;
  rec.prev_drec = rec.prev_i2s = -1;
  // the reset left the device open, and the driver takes 4488h then
  if (rec.dc_changed && dcdrift(0, &rec.dcdrift))
    ok = 0;
  rec.dc_changed = 0;
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
  if (rec.dev_open)
    waveInClose(rec.hwi);
  rec.dev_open = 0;
  if (ok) {
    marker_delete();
  } else if (rec.marked) {
    sprintf(text, "The chip's settings couldn't all be put back: esfmrec "
                  "tries again when it starts");
    log_line(text);
  }
}

// the device or the simulation, the old settings to the marker, then the
// chip set to record the FM; 0, or -1 after saying why
static int rec_open(void) {
  char text[200];
  DWORD off = 0;
  UINT err;
  int dev = -1, i;

  if (!opt.io.sim) {
    dev = find_input();
    if (dev < 0) {
      problem("ESS's wave input (ES1869.DRV) isn't installed");
      return -1;
    }
    take_dsp();
    err = open_input(dev, 1);
    if (err) {
      if (err == MMSYSERR_ALLOCATED && other)
        strcpy(text, "esfmrec is already recording in another window");
      else if (err == MMSYSERR_ALLOCATED && dsp_in_dos() &&
               vxd.ext_version >= 0x0113)
        strcpy(text, "A DOS program has the Sound Blaster until it ends, "
                     "and the FM is recorded through the same channel. "
                     "RecordTakesDSP=1 in [ES1869.VXD] lets esfmrec take "
                     "it");
      else if (err == MMSYSERR_ALLOCATED && dsp_in_dos())
        strcpy(text, "A DOS program has the Sound Blaster until it ends, "
                     "and the FM is recorded through the same channel. "
                     "The rebuilt ES1869.VXD lets esfmrec take it");
      else if (err == MMSYSERR_ALLOCATED)
        strcpy(text, "Another program is recording");
      else
        waveInGetErrorText(err, text, sizeof(text));
      problem(text);
      return -1;
    }
    rec.dev_open = 1;
    if (dcdrift(1, &rec.dcdrift) == 0 && rec.dcdrift)
      rec.dc_restore = (long)rec.dcdrift;
  }
  // the old settings first, where a crash can't lose them
  rec.prev_drec = chip_get(F_MUSIC_DREC);
  rec.prev_i2s = chip_get(F_I2S_EN);
  if (rec.prev_drec < 0 || rec.prev_i2s < 0) {
    rec.prev_drec = rec.prev_i2s = -1;
    put_back();
    problem("The ES1869 didn't answer, so the FM can't be recorded");
    return -1;
  }
  if (rec.prev_drec == 1)
    log_line("Music DAC digital record was on before esfmrec started, and "
             "stays on after it");
  marker_write();
  if (rec.dc_restore >= 0) {
    // off for the next open, which is where the driver looks at it
    rec.dc_changed = dcdrift(0, &off) == 0;
    waveInClose(rec.hwi);
    rec.dev_open = 0;
    // the close may have given a DOS program its Sound Blaster back
    take_dsp();
    err = open_input(dev, 1);
    if (err) {
      if (rec.dc_changed && dc_put(rec.dcdrift) == 0)
        rec.dc_changed = 0;
      put_back();
      waveInGetErrorText(err, text, sizeof(text));
      problem(text);
      return -1;
    }
    rec.dev_open = 1;
  }
  for (i = 0; !opt.io.sim && i < BUFS; i++) {
    rec.hdr[i] = new_buffer(i);
    if (!rec.hdr[i] ||
        waveInPrepareHeader(rec.hwi, rec.hdr[i], sizeof(WAVEHDR)) ||
        queue_block(rec.hdr[i])) {
      if (rec.hdr[i] && !(rec.hdr[i]->dwFlags & WHDR_PREPARED))
        rec.hdr[i] = 0; // nothing to unprepare, just free
      close_device();
      put_back();
      no_memory("Out of memory for the recording buffers");
      return -1;
    }
  }
  // the music DAC's samples in place of the ADC's, from FM, while the
  // device is Windows'
  if (chip_put(F_I2S_EN, 0) || chip_put(F_MUSIC_DREC, 1)) {
    close_device();
    put_back();
    problem("The ES1869 didn't answer, so the FM can't be recorded");
    return -1;
  }
  return 0;
}

// frames per second, from the blocks after the first; 0 before 2 s
static u32 measured_rate(void) {
  if (!rec.got_first || rec.t_now - rec.t_first < 2000)
    return 0;
  return (u32)((frames_done() - rec.first_frames) * 1000.0 /
               (rec.t_now - rec.t_first));
}

static void show(void) {
  char text[112], l[16], r[16], at[16];
  u32 rate = measured_rate();
  u32 mb10 = (u32)(frames_done() * 4 * 10 / 1048576.0);
  int db;

  clock_text(at);
  sprintf(text, "%s %s, %lu.%lu MB", rec.on ? "Recording" : "Saved", at,
          mb10 / 10, mb10 % 10);
  set_text(IDC_TIME, text);
  if (rate)
    sprintf(text, "%lu Hz, measured %lu Hz", FMREC_RATE, rate);
  else
    sprintf(text, "%lu Hz, 16-bit stereo, as the FM makes it", FMREC_RATE);
  set_text(IDC_RATE, text);
  if (rec.on) {
    db = fmrec_db10(rec.peak_l);
    if (db < -900)
      strcpy(l, "silent");
    else
      sprintf(l, "%s%d.%d dB", db < 0 ? "-" : "", -db / 10, -db % 10);
    db = fmrec_db10(rec.peak_r);
    if (db < -900)
      strcpy(r, "silent");
    else
      sprintf(r, "%s%d.%d dB", db < 0 ? "-" : "", -db / 10, -db % 10);
    sprintf(text, "Peak: left %s, right %s", l, r);
    set_text(IDC_LEVEL, text);
    rec.peak_l = rec.peak_r = 0;
  }
}

static void rec_stop(void) {
  char more[120], text[200];
  u32 rate;

  if (!rec.on)
    return;
  rec.on = 0;
  close_device();
  // over 30 s the timer is good to 0.05%: a rate 0.1% below the music
  // DAC's means the driver lost blocks
  more[0] = 0;
  rate = measured_rate();
  if (rate && rec.t_now - rec.t_first >= 30000 && rate + 50 < FMREC_RATE)
    sprintf(more, ", but only %lu Hz came in: samples were lost", rate);
  if (rec.dry)
    sprintf(more + strlen(more), ", the driver ran out of blocks %u times",
            rec.dry);
  // only the log: a box isn't safe while the window closes
  if (file_close(more) && !rec.failed) {
    rec.failed = 1;
    sprintf(text, "Writing the end of %.140s failed: is the disk full?",
            rec.path);
    log_line(text);
  }
  put_back();
  set_text(IDC_REC, "&Record");
  show();
}

static void rec_start(void) {
  char text[200];

  if (rec.on)
    return;
  memset(&rec, 0, sizeof(rec));
  rec.fd = -1;
  rec.prev_drec = rec.prev_i2s = -1;
  rec.dc_restore = -1;
  rec.part = 1;
  if (opt.file[0]) {
    full_path(opt.file, rec.first, sizeof(rec.first));
    strcpy(rec.path, rec.first);
    opt.file[0] = 0; // the next recording gets a new name
  } else if (next_name(rec.path, sizeof(rec.path))) {
    rec.failed = 1;
    problem("FMREC001 to FMREC999 are all taken: move them, or name the file");
    return;
  }
  if (rec_open()) {
    rec.failed = 1;
    return;
  }
  // a named file is only replaced once the device is esfmrec's
  if (file_open()) {
    close_device();
    put_back();
    rec.failed = 1;
    sprintf(text, "Can't create %.120s", rec.path);
    problem(text);
    return;
  }
  rec.t_start = rec.t_now = rec.t_saved = rec.t_checked = timeGetTime();
  if (opt.io.sim) {
    rec.sim_t0 = rec.t_start;
    if (!SetTimer(dlg, TIMER_SIM, 50, 0)) {
      file_close("");
      remove(rec.path);
      put_back();
      rec.failed = 1;
      no_memory("No timer left for esfmrec: close a program and try again");
      return;
    }
  } else if (waveInStart(rec.hwi)) {
    close_device();
    file_close("");
    remove(rec.path);
    put_back();
    rec.failed = 1;
    problem("ESS's driver didn't start recording");
    return;
  }
  rec.on = 1;
  sprintf(text, "Recording to %.140s", rec.path);
  log_line(text);
  set_text(IDC_FILE, rec.path);
  set_text(IDC_REC, "&Stop");
  show();
}

// /sim: the test tone, as much as the time since the start calls for
static void sim_tick(void) {
  u32 due = (u32)((timeGetTime() - rec.sim_t0) * (FMREC_RATE / 1000.0));
  u16 n;

  while (rec.on && !rec.failed && rec.sim_frames < due) {
    n = (u16)(due - rec.sim_frames > BUF_FRAMES ? BUF_FRAMES
                                                : due - rec.sim_frames);
    // /t= gets exactly its frames
    if (n > frames_left())
      n = (u16)frames_left();
    if (!n)
      break;
    fmrec_tone(sim_buf, n, rec.tone);
    take(sim_buf, n);
    rec.sim_frames += n;
  }
}

// --- window -----------------------------------------------------------------

static void timed_stop(void) {
  if (opt.secs && rec.on && rec.secs >= opt.secs) {
    rec_stop();
    DestroyWindow(dlg);
  }
}

// no block for QUIET_MS goes to the log once; a /t= run that gets no
// samples ends 30 s after its time
static void watchdog(u32 now) {
  char text[100];

  if (!rec.quiet && now - rec.t_now >= QUIET_MS) {
    rec.quiet = 1;
    clock_text(text);
    strcat(text, ": no samples came in for 5 s");
    log_line(text);
  }
  if (opt.secs && (now - rec.t_start) / 1000 > opt.secs + 30)
    fail("ESS's driver stopped giving esfmrec samples");
}

static void tick(void) {
  char text[200];
  u32 now = timeGetTime();

  if (!rec.on)
    return;
  if (!opt.io.sim) {
    drain(1);
    watchdog(now);
  }
  if (rec.dry_new && rec.dry <= 10) {
    clock_text(text);
    strcat(text, ": the driver had no free block, samples were lost (Windows "
                 "was busy)");
    log_line(text);
  }
  rec.dry_new = 0;
  if (!rec.failed && now - rec.t_saved >= SAVE_MS) {
    rec.t_saved = now;
    if (file_save()) {
      sprintf(text, "Writing %.140s failed: is the disk full?", rec.path);
      fail(text);
    }
  }
  if (!rec.failed && now - rec.t_checked >= CHECK_MS) {
    rec.t_checked = now;
    check_chip();
  }
  if (rec.failed) {
    rec_stop();
    problem(failure);
    // a batch job (/t=) ends with it
    if (opt.secs && IsWindow(dlg))
      DestroyWindow(dlg);
    return;
  }
  show();
  timed_stop();
}

BOOL CALLBACK __export rec_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  (void)lp;
  switch (msg) {
  case WM_INITDIALOG:
    dlg = w;
    return TRUE;

  case MM_WIM_DATA:
    if (rec.on && (HWAVEIN)wp == rec.hwi) {
      drain(1);
      timed_stop();
    }
    return TRUE;

  case WM_TIMER:
    if (wp == TIMER_SIM) {
      sim_tick();
      timed_stop();
    } else {
      tick();
    }
    return TRUE;

  case WM_COMMAND:
    // Esc and Enter don't end a recording by accident
    if (wp == IDC_REC) {
      if (rec.on)
        rec_stop();
      else
        rec_start();
    } else if (wp == IDC_CLOSE || (wp == IDCANCEL && !rec.on)) {
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

// --- a recording that didn't finish -----------------------------------------

// the header of the file a recording that was ended by force wrote: set
// to the length the file has; 0, or -1 if it isn't esfmrec's WAV
static int repair(const char *path, u32 *secs) {
  u8 hdr[FMREC_HEADER];
  long size;
  u32 data;
  int fd = open(path, O_RDWR | O_BINARY), err = -1;

  if (fd < 0)
    return -1;
  size = lseek(fd, 0L, SEEK_END);
  if (size >= FMREC_HEADER && lseek(fd, 0L, SEEK_SET) == 0 &&
      read(fd, hdr, FMREC_HEADER) == FMREC_HEADER &&
      fmrec_wav_fix(hdr, (u32)size, &data) == 0 &&
      lseek(fd, 0L, SEEK_SET) == 0 &&
      write(fd, hdr, FMREC_HEADER) == FMREC_HEADER) {
    _commit(fd);
    *secs = data / 4 / FMREC_RATE;
    err = 0;
  }
  close(fd);
  return err;
}

// the marker of an esfmrec that was ended by force: its settings back and
// its file's header repaired; -1 if the settings can't be put back yet,
// since a new recording would save the wrong ones as the old ones
static int recover(void) {
  char path[144], line[160], file[144], text[400];
  long drec = -1, i2s = -1, dc = -1;
  u32 boot = 0, secs;
  int ok = 1, same;
  FILE *f;

  full_path(marker_name(), path, sizeof(path));
  f = fopen(path, "r");
  if (!f)
    return 0;
  file[0] = 0;
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strncmp(line, "file=", 5)) {
      strncpy(file, line + 5, sizeof(file) - 1);
      file[sizeof(file) - 1] = 0;
    } else if (!strncmp(line, "boot=", 5)) {
      boot = strtoul(line + 5, 0, 10);
    } else if (!strncmp(line, "drec=", 5)) {
      drec = strtol(line + 5, 0, 10);
    } else if (!strncmp(line, "i2s=", 4)) {
      i2s = strtol(line + 4, 0, 10);
    } else if (!strncmp(line, "dcdrift=", 8)) {
      dc = strtol(line + 8, 0, 10);
    }
  }
  fclose(f);
  strcpy(text, "The last recording stopped without esfmrec, which was ended "
               "by force.");
  // a restart since then has reset the chip and the driver already
  same = labs((long)(boot_time() - boot)) < 120;
  if (same) {
    if (drec >= 0 && drec <= 1 && chip_put(F_MUSIC_DREC, (u8)drec))
      ok = 0;
    if (i2s >= 0 && i2s <= 1 && restore_i2s((int)i2s))
      ok = 0;
    if (dc >= 0 && dc_put((DWORD)dc))
      ok = 0;
  }
  if (!ok) {
    strcat(text, " Its settings can't be put back yet: close the program "
                 "that has the sound device, then start esfmrec again.");
    problem(text);
    return -1;
  }
  if (same)
    strcat(text, " esfmrec put the chip's settings back.");
  if (file[0] && repair(file, &secs) == 0)
    sprintf(text + strlen(text), " %.140s is repaired, %lu:%02lu:%02lu long.",
            file, secs / 3600, secs / 60 % 60, secs % 60);
  remove(path);
  problem(text);
  return 0;
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
    if (*p == '"') {
      // a file name in quotes
      for (p++; *p && *p != '"'; p++)
        if (n + 1 < sizeof(word))
          word[n++] = *p;
        else
          return -1;
      if (*p)
        p++;
    } else {
      for (; *p && *p != ' ' && *p != '\t'; p++)
        if (n + 1 < sizeof(word))
          word[n++] = *p;
        else
          return -1;
    }
    word[n] = 0;
    if (word[0] != '/' && word[0] != '-') {
      if (opt.file[0] || !word[0])
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
    else if (!strcmp(word + 1, "split") && arg && atol(arg) > 0)
      opt.split = atol(arg);
    else if (!strcmp(word + 1, "raw"))
      opt.raw = 1;
    else if (!strcmp(word + 1, "min"))
      opt.min = 1;
    else if (!strcmp(word + 1, "q"))
      opt.quiet = 1;
    else if (!strcmp(word + 1, "log") && arg && *arg)
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
  int err, q;

  inst = hinst;
  other = prev != 0;
  // room for the driver's messages while Windows is busy, before any
  // window; a failed size leaves no queue, so try smaller ones
  for (q = 64; !SetMessageQueue(q) && q > 8; q /= 2)
    ;
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
  // settings a recording ended by force left behind, unless it's the
  // other esfmrec's own
  if (!other && recover() < 0)
    return 2;
  proc = (DLGPROC)MakeProcInstance((FARPROC)rec_proc, inst);
  dlg = CreateDialog(inst, "ESFMREC", 0, proc);
  if (!dlg || !SetTimer(dlg, TIMER_SHOW, SHOW_MS, 0)) {
    if (dlg)
      DestroyWindow(dlg);
    dlg = 0;
    no_memory("Not enough memory for esfmrec's window and timer");
    FreeProcInstance((FARPROC)proc);
    return 2;
  }
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
