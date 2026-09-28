/*
 * ESFM stress test (see esfmtest.h and docs/ESFM_MIDI.md).
 *
 * Notes:
 *
 * MMSYSTEM's stream player sends the events of a dense MIDI
 * stream to the ESFM device from a timer at interrupt time,
 * the way the MCI sequencer and games play music. While it
 * plays, essctl sends controller changes to the same device
 * from the program.
 *
 * Every note on in the stream has its note off and every
 * sustain pedal is released, so no voice should be sounding
 * when the stream is done. ESS's ESFM.DRV drops a message that
 * arrives while it is still busy with another one, and a
 * dropped note off leaves a voice on.
 *
 * Every 50 ms essctl lets the other programs run. It stops the
 * test when essctl is closed, and Windows closes the stream
 * before the buffer goes.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <windows.h>

#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#include "esfmlive.h"
#include "esfmtest.h"
#include "essctl.h"

// MIDI streams came with Windows 95, not in the Windows 3.1 headers
typedef struct {
  LPSTR lpData;
  DWORD dwBufferLength;
  DWORD dwBytesRecorded;
  DWORD dwUser;
  DWORD dwFlags;
  void FAR *lpNext;
  DWORD reserved;
  DWORD dwOffset;
  DWORD dwReserved[4];
} MIDIHDR95;

typedef struct {
  DWORD cbStruct;
  DWORD dwTimeDiv;
} MIDIPROPTIMEDIV;

#define MEVT_LONGMSG 0x80000000L // MEVT_F_LONG | (MEVT_LONGMSG << 24)
#define MIDIPROP_SET 0x80000000L
#define MIDIPROP_TIMEDIV 0x00000001L

typedef UINT(FAR PASCAL *STREAMOPEN)(UINT FAR *, UINT FAR *, DWORD, DWORD,
                                     DWORD, DWORD);
typedef UINT(FAR PASCAL *STREAMOUT)(UINT, MIDIHDR95 FAR *, UINT);
typedef UINT(FAR PASCAL *STREAMPROP)(UINT, BYTE FAR *, DWORD);
typedef UINT(FAR PASCAL *STREAMCTL)(UINT);

#define TICKS 700 // 48 per quarter note at 120 bpm, 7.3 s
#define CHANNELS 12
#define MAX_OFFS 96
#define BUF_BYTES 60000U

static DWORD __far *ev;
static unsigned evn, events, ev_max;
static DWORD last_tick;

static int put(DWORD tick, DWORD event) {
  if (evn + 3 > ev_max)
    return -1;
  ev[evn++] = tick - last_tick;
  ev[evn++] = 0;
  ev[evn++] = event;
  last_tick = tick;
  events++;
  return 0;
}

static int put_short(DWORD tick, BYTE status, BYTE d1, BYTE d2) {
  return put(tick, status | ((DWORD)d1 << 8) | ((DWORD)d2 << 16));
}

static int put_sysex(DWORD tick, const BYTE *b, unsigned n) {
  unsigned words = (n + 3) / 4;

  if (evn + 3 + words > ev_max)
    return -1;
  put(tick, MEVT_LONGMSG | n);
  _fmemset(ev + evn, 0, words * 4);
  _fmemcpy(ev + evn, b, n);
  evn += words;
  return 0;
}

// build a dense arrangement: overlapping notes on 12 channels, pitch bend,
// volume, the sustain pedal and a SysEx now and then
static int make_stream(void) {
  static const BYTE gm_on[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
  struct {
    DWORD tick;
    BYTE ch, note;
  } off[MAX_OFFS];
  unsigned noffs = 0, i;
  DWORD t;
  int c;

  evn = events = 0;
  last_tick = 0;
  for (c = 0; c < CHANNELS; c++)
    if (put_short(0, (BYTE)(0xC0 | c), (BYTE)(c * 7), 0))
      return -1;
  for (t = 0; t < TICKS; t++) {
    // note offs that are due
    for (i = 0; i < noffs;) {
      if (off[i].tick == t) {
        if (put_short(t, (BYTE)(0x80 | off[i].ch), off[i].note, 0))
          return -1;
        off[i] = off[--noffs];
      } else {
        i++;
      }
    }
    if (t + 8 >= TICKS)
      continue; // only note offs at the end
    for (c = 0; c < CHANNELS; c++) {
      BYTE note;
      if ((t + c) % 3 || noffs >= MAX_OFFS)
        continue;
      note = (BYTE)(36 + (t * 7 + c * 5) % 48);
      if (put_short(t, (BYTE)(0x90 | c), note, 100))
        return -1;
      off[noffs].tick = t + 1 + c % 4 + (t % 5 == 0);
      off[noffs].ch = (BYTE)c;
      off[noffs].note = note;
      noffs++;
    }
    c = (int)(t % CHANNELS);
    if (t % 2 == 0 &&
        put_short(t, (BYTE)(0xE0 | c), 0, (BYTE)(0x30 + (t * 3) % 0x20)))
      return -1;
    if (t % 4 == 1 && put_short(t, (BYTE)(0xB0 | c), 7, (BYTE)(90 + t % 30)))
      return -1;
    if (t % 16 == 2 && put_short(t, (BYTE)(0xB0 | c), 64, 127))
      return -1;
    if (t % 16 == 10 &&
        put_short(t, (BYTE)(0xB0 | ((t - 8) % CHANNELS)), 64, 0))
      return -1;
    if (t % 48 == 24 && put_sysex(t, gm_on, sizeof(gm_on)))
      return -1;
  }
  // pedals up, bends centered
  for (c = 0; c < CHANNELS; c++)
    if (put_short(TICKS, (BYTE)(0xB0 | c), 64, 0) ||
        put_short(TICKS, (BYTE)(0xE0 | c), 0, 0x40))
      return -1;
  return 0;
}

static int find_esfm(UINT *id) {
  MIDIOUTCAPS caps;
  UINT i, n = midiOutGetNumDevs();

  for (i = 0; i < n; i++)
    if (midiOutGetDevCaps(i, &caps, sizeof(caps)) == 0 &&
        strstr(caps.szPname, "ESFM")) {
      *id = i;
      return 0;
    }
  return -1;
}

// the other programs run; 1 when essctl is closing (WM_QUIT, put back
// for the main loop)
static int pump(void) {
  MSG msg;

  while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      PostQuitMessage(msg.wParam);
      return 1;
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}

static void append(char *out, unsigned size, const char *text) {
  unsigned n = strlen(out);

  if (n + 1 < size) {
    strncpy(out + n, text, size - n - 1);
    out[size - 1] = 0;
  }
}

static const char *note_name(BYTE note, char *buf) {
  static const char names[] = "C C#D D#E F F#G G#A A#B ";
  buf[0] = names[(note % 12) * 2];
  buf[1] = names[(note % 12) * 2 + 1];
  sprintf(buf + (buf[1] == ' ' ? 1 : 2), "%d", note / 12 - 1);
  return buf;
}

int esfm_stress_test(char *report, unsigned size) {
  HINSTANCE mm = GetModuleHandle("MMSYSTEM");
  STREAMOPEN s_open;
  STREAMOUT s_out;
  STREAMPROP s_prop;
  STREAMCTL s_restart, s_stop, s_close;
  MIDIPROPTIMEDIV div;
  static MIDIHDR95 hdr;
  struct esfm_diag before, after;
  HGLOBAL mem;
  UINT id, hms = 0, r;
  DWORD start, sent = 0, refused = 0, took, pumped;
  char text[200], nb[8];
  int i, stuck = 0, rc = -1, quit = 0;
  HCURSOR old;

  report[0] = 0;
  if (find_esfm(&id)) {
    append(report, size, "No ESFM MIDI device: is ESFM.DRV installed?");
    return -1;
  }
  s_open = (STREAMOPEN)GetProcAddress(mm, "MIDISTREAMOPEN");
  s_out = (STREAMOUT)GetProcAddress(mm, "MIDISTREAMOUT");
  s_prop = (STREAMPROP)GetProcAddress(mm, "MIDISTREAMPROPERTY");
  s_restart = (STREAMCTL)GetProcAddress(mm, "MIDISTREAMRESTART");
  s_stop = (STREAMCTL)GetProcAddress(mm, "MIDISTREAMSTOP");
  s_close = (STREAMCTL)GetProcAddress(mm, "MIDISTREAMCLOSE");
  if (!s_open || !s_out || !s_prop || !s_restart || !s_stop || !s_close) {
    append(report, size, "The stress test needs Windows 95 (MIDI streams).");
    return -1;
  }
  mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, BUF_BYTES);
  ev = mem ? (DWORD __far *)GlobalLock(mem) : 0;
  if (!ev) {
    append(report, size, "Not enough memory for the test.");
    if (mem)
      GlobalFree(mem);
    return -1;
  }
  ev_max = BUF_BYTES / 4;
  if (make_stream()) {
    append(report, size, "The test stream does not fit its buffer.");
    goto out_mem;
  }
  esfm_diag_read(&before, 0);

  r = s_open(&hms, &id, 1, 0, 0, 0); // CALLBACK_NULL
  if (r) {
    sprintf(text, "Cannot open the ESFM device (error %u)%s.", r,
            r == MMSYSERR_ALLOCATED ? ": it is in use, stop other MIDI "
                                      "playback first"
                                    : "");
    append(report, size, text);
    goto out_mem;
  }
  div.cbStruct = sizeof(div);
  div.dwTimeDiv = 48;
  s_prop(hms, (BYTE FAR *)&div, MIDIPROP_SET | MIDIPROP_TIMEDIV);
  memset(&hdr, 0, sizeof(hdr));
  hdr.lpData = (LPSTR)ev;
  hdr.dwBufferLength = hdr.dwBytesRecorded = (DWORD)evn * 4;
  r = midiOutPrepareHeader((HMIDIOUT)hms, (LPMIDIHDR)&hdr, sizeof(hdr));
  if (!r)
    r = s_out(hms, &hdr, sizeof(hdr));
  if (!r)
    r = s_restart(hms);
  if (r) {
    sprintf(text, "Cannot play the test stream (error %u).", r);
    append(report, size, text);
    goto out_close;
  }

  old = SetCursor(LoadCursor(0, IDC_WAIT));
  start = pumped = GetTickCount();
  // send controller changes from the program while the stream plays,
  // volume and expression rewrite the levels of every sounding voice
  while (!(hdr.dwFlags & MHDR_DONE) && GetTickCount() - start < 20000) {
    BYTE c = (BYTE)(sent % CHANNELS);
    BYTE cc = (BYTE)(sent & 16 ? 11 : 7);
    r = midiOutShortMsg((HMIDIOUT)hms, 0xB0 | c | ((DWORD)cc << 8) |
                                           ((DWORD)(100 + sent % 27) << 16));
    sent++;
    if (r)
      refused++;
    if (GetTickCount() - pumped >= 50) {
      pumped = GetTickCount();
      if ((quit = pump()) != 0)
        break;
    }
  }
  took = GetTickCount() - start;
  // wait for the releases
  if (!quit)
    quit = wait_ms(400);
  SetCursor(old);
  if (quit) {
    append(report, size, "Stopped: essctl is closing.");
    goto out_stop;
  }
  esfm_diag_read(&after, 1);

  sprintf(text,
          "Played %u MIDI events in %lu.%lu s from MMSYSTEM's stream "
          "player (interrupt time), while essctl sent %lu controller "
          "messages (%lu refused).\r\n\r\n",
          events, took / 1000, took % 1000 / 100, sent, refused);
  append(report, size, text);
  if (!(hdr.dwFlags & MHDR_DONE))
    append(report, size, "The stream did not finish in 20 s.\r\n\r\n");
  for (i = 0; i < ESFM_VOICES; i++) {
    const struct esfm_voice *v = &after.v[i];
    if (!(v->flags & 1) && !(after.chip_read && v->chip == 1))
      continue;
    if (!stuck)
      append(report, size, "Voices left sounding:");
    stuck++;
    sprintf(text, "%s voice %d: channel %d, %s%s", stuck > 1 ? "," : "", i + 1,
            v->channel + 1, note_name(v->note, nb),
            v->flags & 1 ? "" : " (the driver thinks it is off)");
    append(report, size, text);
  }
  if (stuck) {
    append(report, size, ".\r\nThese are stuck notes.\r\n\r\n");
  } else if (after.device) {
    append(report, size, "No voice was left sounding.\r\n\r\n");
  }
  if (after.device && after.fixed) {
    sprintf(text,
            "Fixed ESFM.DRV: %lu messages arrived while it was busy "
            "and were queued (the ESS driver drops these), %lu "
            "refused with a full queue.",
            after.queued - before.queued, after.overflow - before.overflow);
    append(report, size, text);
  } else if (after.device) {
    append(report, size,
           "This is ESS's ESFM.DRV: messages that arrive while "
           "it is busy are dropped.  Install build\\ESFM.DRV "
           "and run the test again.");
  }
  rc = stuck ? 1 : 0;

out_stop:
  s_stop(hms);
  midiOutReset((HMIDIOUT)hms);
out_close:
  // a buffer the stream still holds can't be unprepared or freed
  if (hdr.dwFlags & MHDR_INQUEUE) {
    s_stop(hms);
    midiOutReset((HMIDIOUT)hms);
  }
  if (hdr.dwFlags & MHDR_PREPARED)
    midiOutUnprepareHeader((HMIDIOUT)hms, (LPMIDIHDR)&hdr, sizeof(hdr));
  s_close(hms);
out_mem:
  GlobalUnlock(mem);
  GlobalFree(mem);
  return rc;
}
