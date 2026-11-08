/*
 * esswchk checks in Windows 98 that the drivers' fixes work on the card:
 * the VxD's reads of a moving DMA count, the Audio 1 player's positions and
 * its drain, the FM's sides and rate through the FM recording device, and
 * sb16test in a DOS box on the VxD's Sound Blaster 16. A recorded run adds
 * sounds after DTMF markers, which tools/wchkrec.py measures in the
 * recording of the line out.
 *
 * Usage:
 *   `esswchk [options]`
 *
 *   /auto      every check at once, with no question; the summary stays
 *              in the window
 *   /R         a recorded run: a level tone, a start marker, the sounds
 *              after their markers, an end marker
 *   /once=FILE delete FILE when the run ends, as ESSCHECK's StartUp batch
 *              wants, also when the run fails
 *   /out=FILE  the report, WCHECK.TXT in esswchk's directory if left out
 *   /log=FILE  the log, ESSWCHK.LOG
 *   /nobox     no DOS box: the report gives sb16test's command line
 *   /q         the window closes when the run ends
 *   /sim       made-up measures of the fixed drivers, without the card;
 *              /sim=old those of the drivers before the fixes
 *   /base=220, /cfg=800, /novxd  as for essctl
 *
 * Notes:
 *
 * The checks run one after the other, each straight through, with
 * Windows' messages pumped between its steps, so that the other programs
 * and the driver's messages run; a WM_QUIT there stops the run and is
 * posted again for the main loop. The measures and verdicts are wchk.c's.
 * Each check's lines go to the report as they come, committed to the disk,
 * as NAME.$$$, which gets its name once the run ends: a run ended by force
 * leaves what it measured.
 *
 * Nothing shows a message box, since nobody may be watching: problems go to
 * the log and to the window. esswchk changes no setting: it opens the
 * devices as any program does, sends the ESFM device program 0 and its
 * controllers' reset (CC121) before it closes it, and leaves the mixer to
 * the drivers. Before the DOS box it closes every device it opened.
 *
 * The recorded run's markers are BADC at the start, CDAB at the end and #,
 * the sound's number in three digits, * before each sound, so that one
 * recording can hold essprobe's run too, whose markers differ. A sound
 * expects its tone on a side (left), the same with a pause (lpause), both
 * DACs in step (step), or the FM's piano, not a clean tone, on a side by
 * its level (fml, fmr).
 *
 * Exit codes: 0 done, 1 stopped, 2 could not run, 3 bad command line, 4
 * some check failed.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <math.h>
#include <mmsystem.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "esfmlive.h"
#include "esshw.h"
#include "sbtsound.h"
#include "tears.h"
#include "vxdapi.h"
#include "wavestat.h"
#include "wchk.h"
#include "winio.h"

#define VERSION "1.0"
#define TITLE "ES1869 Windows checks"
#define IDC_CHECK 101
#define IDC_STEP 102
#define IDC_COUNTS 103
#define IDC_LAST 104
#define IDC_STOP 105
#define IDC_CLOSE 106

#define READINGS 100000UL // VxD 0004 calls a count check takes
#define CHUNK 250         // of them in a row, then the messages
#define POS_SECS 60       // a1.position's stream
#define DRAIN_RUNS 3
#define FM_NOTE_MS 600
#define FM_RATE_SECS 30
#define FM_BUFS 12
#define FM_FRAMES 4096 // 82 ms a block
#define BOX_SECS 300   // sb16test in the DOS box at most
#define SND_MS 1000    // each recorded sound
#define MAX_HDRS 32

static const char usage[] =
    "esswchk [options]\n\n"
    "Checks the drivers' fixes on the ES1869 in Windows and writes a "
    "report.\n\n"
    "Options: /auto, /R, /once=file, /out=file, /log=file, /nobox, /q, "
    "/sim, /sim=old, /base=220, /cfg=800, /novxd";

static struct {
  char out[144], log[144], once[144];
  int autorun, record, nobox, quiet, sim, old;
  struct winio_opts io;
} opt;

static HINSTANCE inst;
static HWND dlg;
static int stop_now, running;
static char line[300];

// --- the window, the log
// ------------------------------------------------------

// relative names are in esswchk.exe's directory
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

// the directory of a path, with its backslash
static void dir_of(const char *path, char *dir, unsigned size) {
  char *slash;

  strncpy(dir, path, size - 1);
  dir[size - 1] = 0;
  slash = strrchr(dir, '\\');
  if (slash)
    slash[1] = 0;
  else
    dir[0] = 0;
}

// a line with the time, committed to the disk so a crash can't lose it
static void log_line(const char *text) {
  char path[144], stamp[32];
  time_t now = time(0);
  FILE *f;

  full_path(opt.log[0] ? opt.log : "ESSWCHK.LOG", path, sizeof(path));
  f = fopen(path, "a");
  if (!f)
    return;
  strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
  fprintf(f, "%s %s\n", stamp, text);
  fflush(f);
  _commit(fileno(f));
  fclose(f);
}

static void show(int id, const char *text) {
  if (dlg)
    SetDlgItemText(dlg, id, text);
}

// the other programs run; 1 once the run has to stop (the Stop button, or
// WM_QUIT, posted again for the main loop)
static int pump(void) {
  MSG msg;

  while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      PostQuitMessage(msg.wParam);
      stop_now = 1;
      break;
    }
    if (!dlg || !IsDialogMessage(dlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  return stop_now;
}

// ms pass with the messages pumped; 0 if the run has to stop
static int wait_ms(DWORD ms) {
  DWORD t0 = timeGetTime();

  while (timeGetTime() - t0 < ms)
    if (pump())
      return 0;
  return 1;
}

// --- the report
// ---------------------------------------------------------------

static int rep_fd = -1;
static int rep_bad; // a write failed: the disk is full
static char rep_path[144], rep_tmp[144];
static unsigned counts[5];
static char failed[16][24];
static unsigned nfailed;

static void put(const char *fmt, ...) {
  va_list ap;
  unsigned n;

  va_start(ap, fmt);
  _vbprintf(line, sizeof(line) - 2, fmt, ap);
  va_end(ap);
  n = strlen(line);
  line[n] = '\r';
  line[n + 1] = '\n';
  if (rep_fd >= 0 && write(rep_fd, line, n + 2) != (int)(n + 2))
    rep_bad = 1;
  line[n] = 0;
}

static void section(const char *name, const char *what) {
  put("");
  put("[%s] %s", name, what);
  show(IDC_CHECK, what);
}

// a check's line, on the disk at once
static void res(const char *id, int r, const char *fmt, ...) {
  char text[WCHK_TEXT + 40];
  va_list ap;

  va_start(ap, fmt);
  _vbprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  put("%-20s %-5s %s", id, wchk_rname[r], text);
  if (rep_fd >= 0)
    _commit(rep_fd);
  counts[r]++;
  if (r == WCHK_FAIL && nfailed < sizeof(failed) / sizeof(failed[0])) {
    strncpy(failed[nfailed], id, sizeof(failed[0]) - 1);
    nfailed++;
  }
  _bprintf(text, sizeof(text), "pass %u, fail %u, known %u, info %u, skip %u",
           counts[0], counts[1], counts[2], counts[3], counts[4]);
  show(IDC_COUNTS, text);
  _bprintf(text, sizeof(text), "%.20s: %s", id, wchk_rname[r]);
  show(IDC_LAST, text);
}

static int report_open(void) {
  char *dot;

  full_path(opt.out[0] ? opt.out : "WCHECK.TXT", rep_path, sizeof(rep_path));
  strcpy(rep_tmp, rep_path);
  dot = strrchr(rep_tmp, '.');
  if (dot && !strchr(dot, '\\'))
    *dot = 0;
  if (strlen(rep_tmp) + 5 > sizeof(rep_tmp))
    return -1;
  strcat(rep_tmp, ".$$$");
  rep_fd = open(rep_tmp, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
                S_IREAD | S_IWRITE);
  return rep_fd < 0 ? -1 : 0;
}

// the summary, then NAME.$$$ checked and renamed over NAME; 0 or -1
static int report_close(void) {
  char text[16 * 25 + 8];
  unsigned i;
  char *p;

  section("summary", "done");
  put("pass=%u fail=%u known=%u info=%u skip=%u%s", counts[0], counts[1],
      counts[2], counts[3], counts[4], stop_now ? " (stopped)" : "");
  if (nfailed) {
    p = text + sprintf(text, "failed:");
    for (i = 0; i < nfailed; i++)
      p += sprintf(p, " %.23s", failed[i]);
    put("%s", text);
  }
  if (rep_fd < 0)
    return -1;
  _commit(rep_fd);
  if (close(rep_fd) || rep_bad) {
    rep_fd = -1;
    log_line("The report could not be written whole: is the disk full?");
    return -1;
  }
  rep_fd = -1;
  remove(rep_path);
  if (rename(rep_tmp, rep_path)) {
    log_line("The report kept its name NAME.$$$: the rename failed");
    return -1;
  }
  return 0;
}

// --- the devices
// --------------------------------------------------------------

// a WAVEHDR and its data in shared global memory, as MMSYSTEM wants them
struct wbuf {
  HGLOBAL hh, hd;
  WAVEHDR FAR *h;
};

static int wbuf_new(struct wbuf *b, DWORD bytes) {
  memset(b, 0, sizeof(*b));
  b->hh =
      GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT, sizeof(WAVEHDR));
  b->hd = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT, bytes);
  if (!b->hh || !b->hd) {
    if (b->hh)
      GlobalFree(b->hh);
    if (b->hd)
      GlobalFree(b->hd);
    memset(b, 0, sizeof(*b));
    log_line("Not enough memory for a wave buffer");
    return -1;
  }
  b->h = (WAVEHDR FAR *)GlobalLock(b->hh);
  b->h->lpData = (LPSTR)GlobalLock(b->hd);
  b->h->dwBufferLength = bytes;
  return 0;
}

static void wbuf_free(struct wbuf *b) {
  if (b->hd) {
    GlobalUnlock(b->hd);
    GlobalFree(b->hd);
  }
  if (b->hh) {
    GlobalUnlock(b->hh);
    GlobalFree(b->hh);
  }
  memset(b, 0, sizeof(*b));
}

// the wave output whose name starts so, or -1
static int find_out(const char *name) {
  WAVEOUTCAPS caps;
  UINT i, n = waveOutGetNumDevs();

  for (i = 0; i < n; i++)
    if (!waveOutGetDevCaps(i, &caps, sizeof(caps)) &&
        !strncmp(caps.szPname, name, strlen(name)))
      return (int)i;
  return -1;
}

static int find_in(const char *name) {
  WAVEINCAPS caps;
  UINT i, n = waveInGetNumDevs();

  for (i = 0; i < n; i++)
    if (!waveInGetDevCaps(i, &caps, sizeof(caps)) &&
        !strncmp(caps.szPname, name, strlen(name)))
      return (int)i;
  return -1;
}

static int find_esfm(void) {
  MIDIOUTCAPS caps;
  UINT i, n = midiOutGetNumDevs();

  for (i = 0; i < n; i++)
    if (!midiOutGetDevCaps(i, &caps, sizeof(caps)) &&
        strstr(caps.szPname, "ESFM"))
      return (int)i;
  return -1;
}

#define A1_DEV "ESS AudioDrive Audio 1"
#define A2_DEV "ESS AudioDrive Audio 2"
#define A2_DEV_ESS "ESS AudioDrive" // ESS's own driver names it so
#define FM_DEV "ESS AudioDrive FM Digital"

// Audio 2's device: the rebuilt driver's name, or ESS's first
static int find_a2(void) {
  int d = find_out(A2_DEV);

  return d >= 0 ? d : find_out(A2_DEV_ESS);
}

static void pcm_format(PCMWAVEFORMAT *f, DWORD rate, int chans, int bits) {
  memset(f, 0, sizeof(*f));
  f->wf.wFormatTag = WAVE_FORMAT_PCM;
  f->wf.nChannels = (WORD)chans;
  f->wf.nSamplesPerSec = rate;
  f->wf.nBlockAlign = (WORD)(chans * bits / 8);
  f->wf.nAvgBytesPerSec = rate * f->wf.nBlockAlign;
  f->wBitsPerSample = (WORD)bits;
}

// --- what the drivers are ----------------------------------------------------

static struct {
  int vxd_ext;     // the extended VxD's version, 0x0126 for 1.26
  int count_fixed; // its 0004 reads until two agree
  int sb16;        // its Sound Blaster 16 for DOS boxes is on
  int true_sides;  // ESFM.DRV plays a left channel on the left
  int dct, ts;     // DmaCountTwice and TrueSides in SYSTEM.INI
} drv;

static void check_env(void) {
  struct wavestat ws;
  struct esfm_diag d;
  WAVEOUTCAPS oc;
  WAVEINCAPS ic;
  MIDIOUTCAPS mc;
  char text[WCHK_TEXT];
  UINT i, n;
  int r, k;

  section("env", "the drivers and their settings");
  drv.dct =
      GetPrivateProfileInt("ES1869.VXD", "DmaCountTwice", 1, "SYSTEM.INI");
  drv.ts = GetPrivateProfileInt("ESFM.DRV", "TrueSides", 1, "SYSTEM.INI");
  if (opt.sim) {
    drv.vxd_ext = opt.old ? 0x0125 : 0x0126;
    drv.sb16 = 1;
    drv.true_sides = !opt.old && drv.ts;
    res("env.vxd", WCHK_INFO, "/sim%s: ES1869.VXD with the extension %X.%02X",
        opt.old ? "=old" : "", drv.vxd_ext >> 8, drv.vxd_ext & 0xFF);
    res("env.esfm", WCHK_INFO, "/sim%s: ESFM.DRV with TrueSides %s",
        opt.old ? "=old" : "", drv.true_sides ? "on" : "off");
  } else {
    drv.vxd_ext = vxd.ext_version;
    drv.sb16 = vxd.ext_version && (vxd.ext_features & VXD_F_SB16);
    if (vxd.entry)
      res("env.vxd", WCHK_INFO,
          "ES1869.VXD %X.%02X, the extension %X.%02X, features %04Xh, "
          "settings %04Xh",
          vxd.version >> 8, vxd.version & 0xFF, vxd.ext_version >> 8,
          vxd.ext_version & 0xFF, vxd.ext_features, vxd.ext_settings);
    else
      res("env.vxd", WCHK_INFO, "no ES1869.VXD answers");
    r = wave_live(&ws);
    if (r == 0 && ws.rebuilt)
      res("env.wave", WCHK_INFO,
          "ES1869.DRV rebuilt, version %u, settings %04Xh", ws.version,
          ws.opts);
    else
      res("env.wave", WCHK_INFO, "ES1869.DRV %s",
          r == 1  ? "isn't loaded"
          : r < 0 ? "isn't ESS's"
                  : "is ESS's");
    r = esfm_diag_read(&d, 0);
    drv.true_sides =
        r == 0 && d.fixed && d.version >= 4 && (d.opts & ESFM_OPT_SIDES) != 0;
    if (r == 0 && d.fixed)
      res("env.esfm", WCHK_INFO,
          "ESFM.DRV fixed, version %u, settings %04Xh: TrueSides %s", d.version,
          d.opts, drv.true_sides ? "on" : "off");
    else
      res("env.esfm", WCHK_INFO, "ESFM.DRV %s",
          r == 0 ? "is ESS's: no TrueSides" : "can't be read");
  }
  drv.count_fixed = drv.vxd_ext >= 0x0126 && drv.dct;
  res("env.ini", WCHK_INFO,
      "SYSTEM.INI: [ES1869.VXD] DmaCountTwice=%d, SoundBlaster16DMA=%d; "
      "[ESFM.DRV] TrueSides=%d",
      drv.dct,
      GetPrivateProfileInt("ES1869.VXD", "SoundBlaster16DMA", 5, "SYSTEM.INI"),
      drv.ts);
  if (opt.sim)
    return;
  k = sprintf(text, "out:");
  for (i = 0, n = waveOutGetNumDevs(); i < n; i++)
    if (!waveOutGetDevCaps(i, &oc, sizeof(oc)) && k < 150)
      k += sprintf(text + k, " %u %.31s;", i, oc.szPname);
  res("env.out", WCHK_INFO, "%s", text);
  k = sprintf(text, "in:");
  for (i = 0, n = waveInGetNumDevs(); i < n; i++)
    if (!waveInGetDevCaps(i, &ic, sizeof(ic)) && k < 150)
      k += sprintf(text + k, " %u %.31s;", i, ic.szPname);
  k += sprintf(text + k, " midi:");
  for (i = 0, n = midiOutGetNumDevs(); i < n; i++)
    if (!midiOutGetDevCaps(i, &mc, sizeof(mc)) && k < 160)
      k += sprintf(text + k, " %u %.31s;", i, mc.szPname);
  res("env.in", WCHK_INFO, "%s", text);
}

// --- a device that plays silence while a check runs
// ---------------------------

static struct {
  HWAVEOUT h;
  struct wbuf b;
  int open;
} hum;

// dev plays 44.1 kHz 16-bit stereo silence in a loop; 0, or an MMSYSERR
static UINT hum_start(int dev) {
  PCMWAVEFORMAT f;
  UINT err;

  pcm_format(&f, 44100, 2, 16);
  err = waveOutOpen(&hum.h, dev, (LPWAVEFORMAT)&f, 0, 0, CALLBACK_NULL);
  if (err)
    return err;
  hum.open = 1;
  if (wbuf_new(&hum.b, 44100L * 4 / 2))
    return MMSYSERR_NOMEM;
  hum.b.h->dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
  hum.b.h->dwLoops = 0x7FFFFFFFL;
  err = waveOutPrepareHeader(hum.h, hum.b.h, sizeof(WAVEHDR));
  if (!err)
    err = waveOutWrite(hum.h, hum.b.h, sizeof(WAVEHDR));
  return err;
}

static void hum_stop(void) {
  if (hum.open) {
    waveOutReset(hum.h);
    if (hum.b.h && (hum.b.h->dwFlags & WHDR_PREPARED))
      waveOutUnprepareHeader(hum.h, hum.b.h, sizeof(WAVEHDR));
    waveOutClose(hum.h);
  }
  wbuf_free(&hum.b);
  hum.open = 0;
}

// --- vxd.count2 and vxd.count1
// ------------------------------------------------

// the count of Audio 1's channel (a1) or Audio 2's, as VxD 0004 reads it,
// for reading i; 0, or -1
static int count_read(int a1, u32 i, u16 *count) {
  if (opt.sim) {
    *count = wchk_sim_count(i * 15, 176400.0, 0x2000, i, opt.old || !drv.dct);
    return 0;
  }
  return vxd_dma_count(a1 ? 0 : 1, count) ? -1 : 0;
}

static void check_count(int a1) {
  static u16 pos[CHUNK];
  static struct tears t;
  const char *id = a1 ? "vxd.count1" : "vxd.count2";
  char text[WCHK_TEXT];
  u16 c, most = 0;
  u32 i, ring;
  int j, dev, r, bad = 0;
  UINT err;

  show(IDC_STEP, a1 ? "Audio 1's count through VxD 0004"
                    : "Audio 2's count through VxD 0004");
  if (!opt.sim) {
    if (!vxd.entry) {
      res(id, WCHK_SKIP, "no ES1869.VXD answers");
      return;
    }
    dev = a1 ? find_out(A1_DEV) : find_a2();
    if (dev < 0) {
      res(id, WCHK_SKIP, "no %s device", a1 ? A1_DEV : "Audio 2");
      return;
    }
    err = hum_start(dev);
    if (err) {
      hum_stop();
      res(id, WCHK_FAIL, "the device didn't play silence (MMSYSERR %u)", err);
      return;
    }
    if (!wait_ms(200)) {
      hum_stop();
      return;
    }
  }
  // the ring from the largest count, then the readings
  for (i = 0; i < 4000; i++) {
    if (count_read(a1, i, &c)) {
      bad = 1;
      break;
    }
    if (c > most)
      most = c;
    if (i % CHUNK == CHUNK - 1 && pump())
      break;
  }
  ring = wchk_ring(most);
  tears_init(&t, ring, 2, 4096);
  for (i = 0; i < READINGS && !bad && !stop_now; i += CHUNK) {
    for (j = 0; j < CHUNK; j++) {
      if (count_read(a1, 4000 + i + j, &c)) {
        bad = 1;
        break;
      }
      pos[j] = wchk_count_pos(c, ring);
    }
    tears_add(&t, pos, (unsigned)j);
    pump();
  }
  if (!opt.sim)
    hum_stop();
  if (bad) {
    res(id, WCHK_FAIL, "VxD 0004 failed");
    return;
  }
  r = wchk_count(&t, drv.count_fixed, text);
  res(id, r, "%s; a ring of %lu", text, (unsigned long)ring);
}

// --- a1.position
// ---------------------------------------------------------------

#define POS_BUFS 8
#define POS_BYTES 8820 // 50 ms

static void check_position(void) {
  static struct wchk_pos p;
  struct wbuf b[POS_BUFS];
  PCMWAVEFORMAT f;
  HWAVEOUT h = 0;
  MMTIME mt;
  char text[WCHK_TEXT];
  u32 t0, now, us, i = 0, secs = opt.sim ? 4 : POS_SECS;
  int k, j, opened = 0, r;
  UINT err = 0;

  show(IDC_STEP, "the Audio 1 device's positions while it streams");
  memset(b, 0, sizeof(b));
  wchk_pos_init(&p);
  if (!opt.sim) {
    k = find_out(A1_DEV);
    if (k < 0) {
      res("a1.position", WCHK_SKIP, "no %s device: ESS's ES1869.DRV", A1_DEV);
      return;
    }
    pcm_format(&f, 44100, 2, 16);
    err = waveOutOpen(&h, k, (LPWAVEFORMAT)&f, 0, 0, CALLBACK_NULL);
    opened = !err;
    for (j = 0; !err && j < POS_BUFS; j++) {
      if (wbuf_new(&b[j], POS_BYTES))
        err = MMSYSERR_NOMEM;
      else if ((err = waveOutPrepareHeader(h, b[j].h, sizeof(WAVEHDR))) == 0)
        err = waveOutWrite(h, b[j].h, sizeof(WAVEHDR));
    }
  }
  t0 = timeGetTime();
  while (!err && !stop_now && timeGetTime() - t0 < secs * 1000UL &&
         (!opt.sim || i < READINGS)) {
    // a burst of readings, then the headers that came back queued again
    for (j = 0; j < CHUNK && !err; j++, i++) {
      if (opt.sim) {
        us = i / CHUNK * 55000UL + i % CHUNK * 20;
        wchk_pos_add(&p, wchk_sim_pos(us, 176.4, i, opt.old), us / 1000, 176.4);
        continue;
      }
      mt.wType = TIME_BYTES;
      err = waveOutGetPosition(h, &mt, sizeof(mt));
      now = timeGetTime();
      if (!err)
        wchk_pos_add(&p, mt.u.cb, now - t0, 176.4);
    }
    for (j = 0; !opt.sim && !err && j < POS_BUFS; j++)
      if (b[j].h->dwFlags & WHDR_DONE) {
        b[j].h->dwFlags &= ~WHDR_DONE;
        err = waveOutWrite(h, b[j].h, sizeof(WAVEHDR));
      }
    pump();
  }
  if (opened) {
    waveOutReset(h);
    for (j = 0; j < POS_BUFS; j++)
      if (b[j].h && (b[j].h->dwFlags & WHDR_PREPARED))
        waveOutUnprepareHeader(h, b[j].h, sizeof(WAVEHDR));
    waveOutClose(h);
  }
  for (j = 0; j < POS_BUFS; j++)
    wbuf_free(&b[j]);
  if (err) {
    res("a1.position", WCHK_FAIL, "the device failed (MMSYSERR %u)", err);
    return;
  }
  r = wchk_position(&p, opt.sim ? p.last_ms : timeGetTime() - t0, text);
  res("a1.position", r, "%s", text);
}

// --- a1.drain
// ------------------------------------------------------------------

// eight headers of 500 bytes at 8 kHz 8-bit mono written at once: when
// the last came back, ms after the first write, 0 if it never did
static u32 drain_run(int dev, UINT *err) {
  struct wbuf b[8];
  PCMWAVEFORMAT f;
  HWAVEOUT h;
  u32 t0, last = 0;
  int j, opened;

  memset(b, 0, sizeof(b));
  pcm_format(&f, 8000, 1, 8);
  *err = waveOutOpen(&h, dev, (LPWAVEFORMAT)&f, 0, 0, CALLBACK_NULL);
  opened = !*err;
  for (j = 0; !*err && j < 8; j++) {
    if (wbuf_new(&b[j], 500))
      *err = MMSYSERR_NOMEM;
    else {
      _fmemset(b[j].h->lpData, 0x80, 500);
      *err = waveOutPrepareHeader(h, b[j].h, sizeof(WAVEHDR));
    }
  }
  t0 = timeGetTime();
  for (j = 0; !*err && j < 8; j++)
    *err = waveOutWrite(h, b[j].h, sizeof(WAVEHDR));
  while (!*err && !stop_now && timeGetTime() - t0 < 2000) {
    if (b[7].h->dwFlags & WHDR_DONE) {
      last = timeGetTime() - t0;
      break;
    }
    pump();
  }
  if (opened) {
    waveOutReset(h);
    for (j = 0; j < 8; j++)
      if (b[j].h && (b[j].h->dwFlags & WHDR_PREPARED))
        waveOutUnprepareHeader(h, b[j].h, sizeof(WAVEHDR));
    waveOutClose(h);
  }
  for (j = 0; j < 8; j++)
    wbuf_free(&b[j]);
  return last;
}

static void check_drain(void) {
  u32 last[DRAIN_RUNS];
  char text[WCHK_TEXT];
  int dev = 0, i, r;
  UINT err = 0;

  show(IDC_STEP, "the Audio 1 device's last buffer back once it played");
  if (!opt.sim && (dev = find_out(A1_DEV)) < 0) {
    res("a1.drain", WCHK_SKIP, "no %s device: ESS's ES1869.DRV", A1_DEV);
    return;
  }
  for (i = 0; i < DRAIN_RUNS && !err && !stop_now; i++) {
    last[i] = opt.sim ? wchk_sim_drain(i, opt.old) : drain_run(dev, &err);
    if (!wait_ms(300))
      break;
  }
  if (err) {
    res("a1.drain", WCHK_FAIL, "the device failed (MMSYSERR %u)", err);
    return;
  }
  if (i < DRAIN_RUNS)
    return;
  r = wchk_drain(last, DRAIN_RUNS, text);
  res("a1.drain", r, "%s", text);
}

// --- the FM through the FM recording device
// ------------------------------------

static struct {
  HWAVEIN h;
  struct wbuf b[FM_BUFS];
  int open;
  u32 frames, at;
} fm;

// the FM recording device opened at the music DAC's rate and recording;
// 0, an MMSYSERR, or -1 without the device
static int fm_start(void) {
  PCMWAVEFORMAT f;
  int dev = find_in(FM_DEV), j;
  UINT err;

  memset(&fm, 0, sizeof(fm));
  if (opt.sim)
    return 0;
  if (dev < 0)
    return -1;
  pcm_format(&f, 49820L, 2, 16);
  err = waveInOpen(&fm.h, dev, (LPWAVEFORMAT)&f, 0, 0, CALLBACK_NULL);
  if (err) {
    pcm_format(&f, 49716L, 2, 16);
    err = waveInOpen(&fm.h, dev, (LPWAVEFORMAT)&f, 0, 0, CALLBACK_NULL);
  }
  if (err)
    return (int)err;
  fm.open = 1;
  for (j = 0; !err && j < FM_BUFS; j++) {
    if (wbuf_new(&fm.b[j], FM_FRAMES * 4L))
      err = MMSYSERR_NOMEM;
    else if ((err = waveInPrepareHeader(fm.h, fm.b[j].h, sizeof(WAVEHDR))) == 0)
      err = waveInAddBuffer(fm.h, fm.b[j].h, sizeof(WAVEHDR));
  }
  if (!err)
    err = waveInStart(fm.h);
  return (int)err;
}

static void fm_stop(void) {
  int j;

  if (fm.open) {
    waveInReset(fm.h);
    for (j = 0; j < FM_BUFS; j++)
      if (fm.b[j].h && (fm.b[j].h->dwFlags & WHDR_PREPARED))
        waveInUnprepareHeader(fm.h, fm.b[j].h, sizeof(WAVEHDR));
    waveInClose(fm.h);
  }
  for (j = 0; j < FM_BUFS; j++)
    wbuf_free(&fm.b[j]);
  fm.open = 0;
}

// the blocks that came in, into sides (or NULL) and fit (or NULL), each
// queued again; /sim makes them up, the note on side, a block each 82 ms
static void fm_take(struct wchk_sides *sides, struct wchk_fit *fit, char side,
                    u32 t0) {
  static s16 pcm[2 * 512];
  u32 now = timeGetTime() - t0;
  u16 n, k;
  int j;

  if (opt.sim) {
    while ((fm.at + FM_FRAMES) * 1000.0 / WCHK_FM_HZ <= now) {
      for (k = 0; k < FM_FRAMES; k += 512) {
        wchk_sim_fm(pcm, 512, fm.at + k, side, opt.old);
        if (sides && side)
          wchk_sides_add(sides, pcm, 512);
      }
      fm.at += FM_FRAMES;
      if (fit)
        wchk_fit_add(fit, (fm.at * 1000.0 / WCHK_FM_HZ + fm.at % 7) / 1000,
                     fm.at);
    }
    return;
  }
  for (j = 0; j < FM_BUFS; j++)
    if (fm.b[j].h->dwFlags & WHDR_DONE) {
      n = (u16)(fm.b[j].h->dwBytesRecorded / 4);
      fm.frames += n;
      if (sides)
        wchk_sides_add(sides, (const s16 *)fm.b[j].h->lpData, n);
      if (fit)
        wchk_fit_add(fit, now / 1000.0, fm.frames);
      fm.b[j].h->dwFlags &= ~WHDR_DONE;
      waveInAddBuffer(fm.h, fm.b[j].h, sizeof(WAVEHDR));
    }
}

static HMIDIOUT midi;

static int midi_open(void) {
  int dev;

  if (opt.sim)
    return 0;
  dev = find_esfm();
  if (dev < 0)
    return -1;
  return midiOutOpen(&midi, dev, 0, 0, CALLBACK_NULL) ? -1 : 0;
}

static void midi_msg(BYTE status, BYTE d1, BYTE d2) {
  if (!opt.sim)
    midiOutShortMsg(midi, status | ((DWORD)d1 << 8) | ((DWORD)d2 << 16));
}

// program 0, the controllers reset, the note off; then closed
static void midi_close(void) {
  if (opt.sim)
    return;
  midi_msg(0x80, 69, 0);
  midi_msg(0xB0, 121, 0);
  midi_msg(0xC0, 0, 0);
  midiOutClose(midi);
}

// note 69 on channel 1 panned to pan for ms, with program 0; while the
// FM recording runs, its blocks into sides
static int fm_note(BYTE pan, u32 ms, struct wchk_sides *sides, u32 t0) {
  u32 t1;

  midi_msg(0xC0, 0, 0);
  midi_msg(0xB0, 10, pan);
  midi_msg(0x90, 69, 100);
  t1 = timeGetTime();
  while (timeGetTime() - t1 < ms) {
    if (sides)
      fm_take(sides, NULL, pan < 64 ? 'l' : 'r', t0);
    if (pump())
      return 0;
  }
  midi_msg(0x80, 69, 0);
  return 1;
}

static void check_fm_pan(BYTE pan) {
  struct wchk_sides sides;
  char text[WCHK_TEXT];
  const char *id = pan ? "fm.pan127" : "fm.pan0";
  u32 t0;
  int err, r;

  show(IDC_STEP, "the FM's sides, recorded digitally");
  memset(&sides, 0, sizeof(sides));
  err = fm_start();
  if (err) {
    fm_stop();
    if (err < 0)
      res(id, WCHK_SKIP, "no %s device", FM_DEV);
    else
      res(id, WCHK_FAIL, "the FM recording failed (MMSYSERR %d)", err);
    return;
  }
  if (midi_open()) {
    fm_stop();
    res(id, WCHK_SKIP, "no ESFM MIDI device free");
    return;
  }
  t0 = timeGetTime();
  r = wait_ms(100) && fm_note(pan, FM_NOTE_MS, &sides, t0);
  midi_close();
  fm_stop();
  if (!r)
    return;
  r = wchk_fm_side(wchk_side_db(&sides, 0), wchk_side_db(&sides, 1),
                   pan < 64 ? 'l' : 'r', drv.true_sides, text);
  res(id, r, "%s; %lu frames", text, (unsigned long)sides.frames);
}

static void check_fm_rate(void) {
  struct wchk_fit fit;
  char text[WCHK_TEXT];
  u32 t0, secs = opt.sim ? 3 : FM_RATE_SECS;
  int err, r;

  show(IDC_STEP, "the music DAC's rate, by the clock");
  memset(&fit, 0, sizeof(fit));
  err = fm_start();
  if (err) {
    fm_stop();
    if (err < 0)
      res("fm.rate", WCHK_SKIP, "no %s device", FM_DEV);
    else
      res("fm.rate", WCHK_FAIL, "the FM recording failed (MMSYSERR %d)", err);
    return;
  }
  t0 = timeGetTime();
  while (timeGetTime() - t0 < secs * 1000UL) {
    fm_take(NULL, &fit, 0, t0);
    if (pump())
      break;
  }
  fm_stop();
  if (stop_now)
    return;
  r = wchk_fm_rate(wchk_fit_slope(&fit), secs, text);
  res("fm.rate", r, "%s", text);
}

// --- a recorded run
// --------------------------------------------------------------

static u8 __huge *poke_at;
static u32 rec_t0; // the start marker's time, ms
static unsigned sound_no;

static void poke(u32 addr, const u8 *src, u16 n) {
  u8 __huge *d = poke_at + addr;

  while (n--)
    *d++ = *src++;
}

// frames of the tone at p, 16-bit: stereo, or 4 channels for dual
// playback, Audio 1's left and Audio 2's right, the other two silent
static void fill(s16 __huge *p, u32 frames, int chans, struct snd_tone *t) {
  s16 l, r;

  while (frames--) {
    l = snd_next(t, 0);
    r = snd_next(t, 1);
    *p++ = l;
    if (chans == 4) {
      *p++ = 0;
      *p++ = 0;
    }
    *p++ = r;
  }
}

// 16-bit frames of the tone into headers of an eighth of a second, on
// dev, played to the end, with a pause of 400 ms pause_at ms in where it
// isn't 0; parts gets when they began, paused, went on and ended; 0, or an
// MMSYSERR
static UINT play(int dev, DWORD rate, int chans, struct snd_tone *t, u32 frames,
                 u32 pause_at, u32 *parts) {
  static struct wbuf b[MAX_HDRS];
  PCMWAVEFORMAT f;
  HWAVEOUT h;
  u32 per = rate / 8, left = frames, n;
  int j, k = 0, opened;
  UINT err;

  pcm_format(&f, rate, chans, 16);
  err = waveOutOpen(&h, dev, (LPWAVEFORMAT)&f, 0, 0, CALLBACK_NULL);
  opened = !err;
  for (k = 0; !err && left && k < MAX_HDRS; k++, left -= n) {
    n = left < per ? left : per;
    if (wbuf_new(&b[k], n * chans * 2)) {
      err = MMSYSERR_NOMEM;
      break;
    }
    fill((s16 __huge *)b[k].h->lpData, n, chans, t);
    err = waveOutPrepareHeader(h, b[k].h, sizeof(WAVEHDR));
  }
  parts[0] = timeGetTime() - rec_t0;
  for (j = 0; !err && j < k; j++)
    err = waveOutWrite(h, b[j].h, sizeof(WAVEHDR));
  if (!err && pause_at) {
    wait_ms(pause_at);
    waveOutPause(h);
    parts[1] = timeGetTime() - rec_t0;
    wait_ms(400);
    parts[2] = timeGetTime() - rec_t0;
    waveOutRestart(h);
  }
  while (!err && k && !(b[k - 1].h->dwFlags & WHDR_DONE) && !stop_now)
    pump();
  parts[pause_at ? 3 : 1] = timeGetTime() - rec_t0;
  if (opened) {
    waveOutReset(h);
    for (j = 0; j < k; j++)
      if (b[j].h->dwFlags & WHDR_PREPARED)
        waveOutUnprepareHeader(h, b[j].h, sizeof(WAVEHDR));
    waveOutClose(h);
  }
  for (j = 0; j < k; j++)
    wbuf_free(&b[j]);
  return err;
}

// a marker's DTMF on Audio 2's device, 22050 Hz 8-bit mono, 100 ms a
// symbol after 200 ms of quiet; the time it began, ms. /sim takes as long
static u32 marker(const char *syms) {
  struct wbuf b;
  PCMWAVEFORMAT f;
  HWAVEOUT h;
  u32 t;
  int dev = find_a2();

  wait_ms(200);
  t = timeGetTime();
  if (opt.sim || dev < 0 || wbuf_new(&b, 22050L)) {
    wait_ms(100UL * strlen(syms));
    return t;
  }
  pcm_format(&f, 22050, 1, 8);
  if (!waveOutOpen(&h, dev, (LPWAVEFORMAT)&f, 0, 0, CALLBACK_NULL)) {
    poke_at = (u8 __huge *)b.h->lpData;
    b.h->dwBufferLength = snd_dtmf(poke, 0, syms, 22050.0, 1323);
    if (!waveOutPrepareHeader(h, b.h, sizeof(WAVEHDR))) {
      t = timeGetTime();
      if (!waveOutWrite(h, b.h, sizeof(WAVEHDR)))
        while (!(b.h->dwFlags & WHDR_DONE) && !stop_now)
          pump();
      waveOutUnprepareHeader(h, b.h, sizeof(WAVEHDR));
    }
    waveOutClose(h);
  }
  wbuf_free(&b);
  return t;
}

// a sound's line: its marker, when its parts played, what it should hold,
// then its spec
static void sound_line(const char *id, const char *syms, u32 tm,
                       const u32 *parts, int n, const char *expect,
                       const char *spec) {
  char p[64];
  int i, k = 0;

  p[0] = 0;
  for (i = 0; i + 1 < n; i += 2)
    k += sprintf(p + k, " %lu-%lu", parts[i] * 1000UL, parts[i + 1] * 1000UL);
  res(id, WCHK_INFO, SND_RECORDED, syms, (tm - rec_t0) * 1000UL, p, expect);
  put("%-20s spec  %s", id, spec);
}

// a sound's marker, # its number in three digits *; when it began, ms
static u32 sound_marker(char *syms) {
  sprintf(syms, "#%03u*", ++sound_no);
  return marker(syms);
}

static void rec_tone(const char *id, int dev, DWORD rate, int chans,
                     double hz_l, double hz_r, u32 pause_ms, const char *expect,
                     const char *spec) {
  struct snd_tone t;
  char syms[8];
  u32 parts[4], tm;
  UINT err = 0;

  tm = sound_marker(syms);
  memset(parts, 0, sizeof(parts));
  snd_tone(&t, hz_l, hz_r, (double)rate, 12000);
  if (opt.sim) {
    parts[0] = timeGetTime() - rec_t0;
    if (pause_ms) {
      wait_ms(pause_ms);
      parts[1] = timeGetTime() - rec_t0;
      wait_ms(400);
      parts[2] = timeGetTime() - rec_t0;
    }
    wait_ms(SND_MS);
    parts[pause_ms ? 3 : 1] = timeGetTime() - rec_t0;
  } else if (dev < 0) {
    res(id, WCHK_SKIP, "no device for it");
    return;
  } else
    err = play(dev, rate, chans, &t, rate * (SND_MS + pause_ms) / 1000,
               pause_ms, parts);
  if (err) {
    res(id, WCHK_FAIL, "the device failed (MMSYSERR %u)", err);
    return;
  }
  sound_line(id, syms, tm, parts, pause_ms ? 4 : 2, expect, spec);
}

static void rec_fm(BYTE pan) {
  const char *id = pan ? "p.fm127" : "p.fm0";
  char syms[8];
  u32 parts[2], tm;

  tm = sound_marker(syms);
  if (midi_open()) {
    res(id, WCHK_SKIP, "no ESFM MIDI device free");
    return;
  }
  parts[0] = timeGetTime() - rec_t0;
  fm_note(pan, SND_MS, NULL, 0);
  parts[1] = timeGetTime() - rec_t0;
  midi_close();
  sound_line(id, syms, tm, parts, 2, pan ? "fmr" : "fml",
             pan ? "dev=esfm cc10=127 note=69" : "dev=esfm cc10=0 note=69");
}

static void check_sounds(void) {
  struct snd_tone t;
  u32 parts[2];
  int a1 = opt.sim ? 0 : find_out(A1_DEV), a2 = opt.sim ? 0 : find_a2();
  char id[16];
  int i;

  section("rec", "a recorded run's sounds, for tools/wchkrec.py");
  show(IDC_STEP, "a level tone, then each sound after its marker");
  snd_init();
  // the level tone: 441 Hz full scale on both sides of Audio 2's device
  rec_t0 = timeGetTime();
  snd_tone(&t, SND_HZ, SND_HZ, 44100.0, 32767);
  if (!opt.sim && a2 >= 0)
    play(a2, 44100, 2, &t, 44100UL * 3, 0, parts);
  rec_t0 = marker("BADC");
  res("rec.start", WCHK_INFO,
      "marker BADC at 0 us; the level tone was %u Hz, 16-bit stereo at "
      "44100 Hz, full scale, on Audio 2's device",
      (unsigned)SND_HZ);
  for (i = 0; i < 3 && !stop_now; i++) {
    sprintf(id, "p.start44%c", 'a' + i);
    rec_tone(id, a1, 44100, 2, SND_HZ, 0, 0, "left",
             "dev=a1 r=44100 f=16s side=l");
  }
  if (!stop_now)
    rec_tone("p.start22", a1, 22050, 2, SND_HZ, 0, 0, "left",
             "dev=a1 r=22050 f=16s side=l");
  if (!stop_now)
    rec_tone("p.restart", a1, 44100, 2, SND_HZ, 0, 600, "lpause",
             "dev=a1 r=44100 f=16s side=l pause=600");
  for (i = 0; i < 3 && !stop_now; i++) {
    sprintf(id, "p.dual44%c", 'a' + i);
    rec_tone(id, a1, 44100, 4, SND_HZ, SND_HZ, 0, "step",
             "dev=a1 r=44100 f=16s ch=4 left=a1 right=a2");
  }
  if (!stop_now)
    rec_fm(0);
  if (!stop_now)
    rec_fm(127);
  if (!stop_now)
    res("rec.end", WCHK_INFO, "marker CDAB at %lu us",
        (marker("CDAB") - rec_t0) * 1000UL);
}

// --- sb16test in a DOS box
// ---------------------------------------------------------

static void check_box(void) {
  char dir[144], rdir[144], bat[160], txt[160], cmd[200], text[WCHK_TEXT];
  struct wchk_sbt s;
  u16 base = 0x220, mpu = 0x330;
  u8 irq = 5, dma = 1;
  int h, r;
  u32 t0;
  FILE *f;

  section("box", "sb16test in a DOS box, on the VxD's Sound Blaster 16");
  show(IDC_STEP, "sb16test /A in a DOS box");
  if (!drv.sb16) {
    res("sb.box", WCHK_SKIP, "the VxD's Sound Blaster 16 is off");
    return;
  }
  full_path("SB16TEST.EXE", cmd, sizeof(cmd));
  dir_of(cmd, dir, sizeof(dir));
  dir_of(rep_path, rdir, sizeof(rdir));
  _bprintf(bat, sizeof(bat), "%.140sSB16BOX.BAT", dir);
  _bprintf(txt, sizeof(txt), "%.140sSB16BOX.TXT", rdir);
  if (!opt.sim && vxd.adi_valid) {
    base = vxd_adi_word(ADI_AUDIO_BASE);
    irq = vxd.adi[ADI_IRQ];
    dma = vxd.adi[ADI_DMA1];
    mpu = vxd_adi_word(ADI_MPU_BASE);
  }
  h = GetPrivateProfileInt("ES1869.VXD", "SoundBlaster16DMA", 5, "SYSTEM.INI");
  f = fopen(bat, "w");
  if (!f) {
    res("sb.box", WCHK_FAIL, "%.140s can't be written", bat);
    return;
  }
  fprintf(f, "@ECHO OFF\n");
  fprintf(f, "REM esswchk: sb16test on the VxD's Sound Blaster 16\n");
  if (h >= 5 && h <= 7)
    fprintf(f, "SET BLASTER=A%X I%u D%u H%d P%X T6\n", base, irq, dma, h, mpu);
  else
    fprintf(f, "SET BLASTER=A%X I%u D%u P%X T6\n", base, irq, dma, mpu);
  fprintf(f, "%sSB16TEST.EXE /A /O:%s\n", dir, txt);
  fprintf(f, "EXIT\n");
  r = fclose(f);
  if (r) {
    res("sb.box", WCHK_FAIL, "%.140s can't be written", bat);
    return;
  }
  if (opt.nobox) {
    res("sb.box", WCHK_SKIP, "/nobox: run %.140s in a DOS box", bat);
    return;
  }
  if (!opt.sim) {
    remove(txt);
    if (WinExec(bat, SW_SHOWNORMAL) < 32) {
      res("sb.box", WCHK_FAIL, "Windows didn't start %.140s", bat);
      return;
    }
  }
  // the report's summary, once sb16test wrote it
  t0 = timeGetTime();
  for (;;) {
    wchk_sbt_init(&s);
    f = fopen(txt, "r");
    if (f) {
      while (fgets(line, sizeof(line), f))
        wchk_sbt_line(&s, line);
      fclose(f);
    }
    if (s.found || !wait_ms(1000) ||
        timeGetTime() - t0 > (opt.sim ? 2000UL : BOX_SECS * 1000UL))
      break;
  }
  if (stop_now)
    return;
  if (!s.found && opt.sim) {
    res("sb.box", WCHK_SKIP, "/sim: no DOS box, and no %.140s", txt);
    return;
  }
  r = wchk_sbt_verdict(&s, text);
  res("sb.box", r, "%s", s.found ? text : "no summary after 5 minutes");
}

// --- the run
// ----------------------------------------------------------------------

static void run_all(void) {
  running = 1;
  timeBeginPeriod(1);
  put("esswchk %s: the drivers' fixes on the ES1869, in Windows", VERSION);
  put("Send this file back, with the recording of a recorded run. Each line");
  put("is a check: PASS, FAIL, KNOWN (a known difference), INFO, SKIP.");
  put("options:%s%s%s%s", opt.autorun ? " /auto" : "", opt.record ? " /R" : "",
      opt.sim ? (opt.old ? " /sim=old" : " /sim") : "",
      opt.nobox ? " /nobox" : "");
  check_env();
  if (!stop_now) {
    section("vxd", "VxD 0004's reads of a moving count");
    check_count(0);
  }
  if (!stop_now)
    check_count(1);
  if (!stop_now) {
    section("a1", "the Audio 1 device's positions and drain");
    check_position();
  }
  if (!stop_now)
    check_drain();
  if (!stop_now) {
    section("fm", "the FM, recorded digitally");
    check_fm_pan(0);
  }
  if (!stop_now)
    check_fm_pan(127);
  if (!stop_now)
    check_fm_rate();
  if (!stop_now && opt.record)
    check_sounds();
  if (!stop_now)
    check_box();
  timeEndPeriod(1);
  running = 0;
}

BOOL CALLBACK __export wchk_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  (void)lp;
  switch (msg) {
  case WM_INITDIALOG:
    dlg = w;
    return TRUE;

  case WM_COMMAND:
    if (wp == IDC_STOP && running) {
      stop_now = 1;
      show(IDC_STEP, "stopping");
    } else if (wp == IDC_CLOSE || wp == IDCANCEL) {
      if (running)
        stop_now = 1;
      else
        DestroyWindow(w);
    }
    return TRUE;

  case WM_CLOSE:
    if (running)
      stop_now = 1;
    else
      DestroyWindow(w);
    return TRUE;

  case WM_DESTROY:
    dlg = 0;
    PostQuitMessage(0);
    return TRUE;
  }
  return FALSE;
}

static int parse(LPSTR p) {
  char word[160], *arg;
  unsigned n;

  memset(&opt, 0, sizeof(opt));
  for (;;) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (!*p)
      return 0;
    for (n = 0; *p && *p != ' ' && *p != '\t'; p++)
      if (n + 1 < sizeof(word))
        word[n++] = *p;
      else
        return -1;
    word[n] = 0;
    if (word[0] != '/' && word[0] != '-')
      return -1;
    arg = strchr(word, '=');
    if (arg)
      *arg++ = 0;
    strlwr(word);
    if (!strcmp(word + 1, "auto"))
      opt.autorun = 1;
    else if (!strcmp(word + 1, "r"))
      opt.record = 1;
    else if (!strcmp(word + 1, "nobox"))
      opt.nobox = 1;
    else if (!strcmp(word + 1, "q"))
      opt.quiet = 1;
    else if (!strcmp(word + 1, "once") && arg && *arg)
      strncpy(opt.once, arg, sizeof(opt.once) - 1);
    else if (!strcmp(word + 1, "out") && arg && *arg)
      strncpy(opt.out, arg, sizeof(opt.out) - 1);
    else if (!strcmp(word + 1, "log") && arg && *arg)
      strncpy(opt.log, arg, sizeof(opt.log) - 1);
    else if (!strcmp(word + 1, "sim") && (!arg || !stricmp(arg, "old"))) {
      opt.sim = opt.io.sim = 1;
      opt.old = arg != NULL;
    } else if (!strcmp(word + 1, "novxd"))
      opt.io.novxd = 1;
    else if (!strcmp(word + 1, "base") && arg)
      opt.io.audio_base = (u16)strtoul(arg, 0, 16);
    else if (!strcmp(word + 1, "cfg") && arg)
      opt.io.config_base = (u16)strtoul(arg, 0, 16);
    else
      return -1;
  }
}

// the StartUp batch gone, whatever the run did
static void once_done(void) {
  char path[144];

  if (!opt.once[0])
    return;
  full_path(opt.once, path, sizeof(path));
  if (remove(path)) {
    _bprintf(line, sizeof(line), "%.140s couldn't be deleted", path);
    log_line(line);
  }
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE prev, LPSTR cmdline, int show_) {
  char text[200];
  DLGPROC proc;
  MSG msg;
  int q, code, err;

  inst = hinst;
  (void)prev;
  for (q = 64; !SetMessageQueue(q) && q > 8; q /= 2)
    ;
  if (parse(cmdline) < 0) {
    log_line("A bad command line; the options are /auto, /R, /once=, "
             "/out=, /log=, /nobox, /q, /sim, /sim=old, /base=, /cfg=, "
             "/novxd");
    if (!opt.quiet && !opt.autorun)
      MessageBox(0, usage, TITLE, MB_OK | MB_ICONINFORMATION);
    once_done();
    return 3;
  }
  err = winio_init(&opt.io);
  proc = (DLGPROC)MakeProcInstance((FARPROC)wchk_proc, inst);
  dlg = CreateDialog(inst, "ESSWCHK", 0, proc);
  if (dlg)
    ShowWindow(dlg, show_);
  else
    log_line("No window: CreateDialog failed; the checks go on");
  if (report_open()) {
    log_line("The report's file can't be made");
    if (dlg)
      DestroyWindow(dlg);
    FreeProcInstance((FARPROC)proc);
    once_done();
    return 2;
  }
  if (err < 0 && !opt.sim)
    log_line("No ES1869 answers: the checks of the devices go on");
  _bprintf(text, sizeof(text), "esswchk %s started:%s%s%s", VERSION,
           opt.autorun ? " /auto" : "", opt.record ? " /R" : "",
           opt.sim ? " /sim" : "");
  log_line(text);
  run_all();
  code = report_close() ? 2 : stop_now ? 1 : counts[WCHK_FAIL] ? 4 : 0;
  _bprintf(text, sizeof(text),
           "esswchk %s: pass=%u fail=%u known=%u info=%u skip=%u, exit %d",
           stop_now ? "stopped" : "done", counts[0], counts[1], counts[2],
           counts[3], counts[4], code);
  log_line(text);
  once_done();
  if (dlg) {
    show(IDC_CHECK, stop_now ? "Stopped." : "Done.");
    show(IDC_STEP, rep_path);
    if (opt.quiet)
      DestroyWindow(dlg);
    else
      EnableWindow(GetDlgItem(dlg, IDC_STOP), FALSE);
  }
  if (!opt.quiet)
    while (dlg && GetMessage(&msg, 0, 0, 0))
      if (!IsDialogMessage(dlg, &msg)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
  FreeProcInstance((FARPROC)proc);
  return code;
}
