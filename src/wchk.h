/*
 * esswchk's measures and verdicts, apart from Windows, for esswchk.exe and
 * the host tests (tests/host/t_wchk.c), and what /sim makes up in place of
 * the card.
 *
 * Notes:
 *
 * Each verdict is one of WCHK_PASS to WCHK_SKIP with its line's text, at
 * most WCHK_TEXT bytes, which esswchk writes as sb16test writes its lines.
 * The measures are a rate fitted to the time by least squares, the levels
 * of a recording's two sides, the positions a program polls, the time the
 * last of a drain's buffers comes back, the torn readings of a DMA count
 * (tears.c), and sb16test's report from a DOS box.
 *
 * /sim stands for the fixed drivers and /sim=old for those before the
 * fixes, so that the Wine tests see every verdict: ESS's VxD reads the
 * count once, the old player gives the last buffer back once the DMA took
 * its end, a torn count moves its position, and ESFM.DRV without TrueSides
 * plays a channel panned left on the chip's right output.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef WCHK_H
#define WCHK_H

#include "esstypes.h"
#include "tears.h"

enum { WCHK_PASS, WCHK_FAIL, WCHK_KNOWN, WCHK_INFO, WCHK_SKIP };

extern const char *const wchk_rname[5];

#define WCHK_TEXT 200       // a verdict's text, with its 0
#define WCHK_FM_HZ 49819.5  // the music DAC's rate, as Linux's driver measured
#define WCHK_DRAIN_MS 500   // eight headers of 500 bytes at 8 kHz
#define WCHK_DRAIN_SLOW 600 // later than this is INFO

// a least-squares line through (t, m)
struct wchk_fit {
  double n, t, m, tt, tm;
};

void wchk_fit_add(struct wchk_fit *f, double t, double m);

// m a unit of t, 0 for fewer than three points
double wchk_fit_slope(const struct wchk_fit *f);

// the two sides of 16-bit stereo frames
struct wchk_sides {
  u32 frames;
  double s[2], ss[2];
};

void wchk_sides_add(struct wchk_sides *w, const s16 *pcm, u16 frames);

// a side's level in dBFS, its mean taken out, a full-scale sine at 0;
// -120 for silence
double wchk_side_db(const struct wchk_sides *w, int side);

// positions a program polls (GETPOS in bytes) and when, against the most
// a position may move: bytes_per_ms for each ms since the last one and 2
// more, and 1024 bytes
struct wchk_pos {
  u32 n, last, last_ms;
  u32 back, worst_back;           // steps back, the largest
  u32 over, worst_over, worst_ms; // steps over the most, the largest, its ms
  int have;
};

void wchk_pos_init(struct wchk_pos *p);
void wchk_pos_add(struct wchk_pos *p, u32 pos, u32 ms, double bytes_per_ms);

// the ring a channel's count runs down, from the largest count read: the
// next power of two, as the drivers' DMA buffers are
u32 wchk_ring(u16 most);

// a count's position in a ring of units, from 0
u16 wchk_count_pos(u16 count, u32 ring);

// sb16test's report, fed a line at a time: its summary and its ctl.poll16
struct wchk_sbt {
  int found; // the summary line came
  unsigned pass, fail, known, info, skip;
  char poll16[WCHK_TEXT];
};

void wchk_sbt_init(struct wchk_sbt *s);
void wchk_sbt_line(struct wchk_sbt *s, const char *line);

// the verdicts, each with its text into text
// vxd.count1 and vxd.count2: readings of a moving channel's count through
// VxD 0004; fixed when the VxD reads until two agree (1.26 or later, with
// DmaCountTwice on)
int wchk_count(const struct tears *t, int fixed, char *text);

// a1.position: GETPOS over ms
int wchk_position(const struct wchk_pos *p, u32 ms, char *text);

// a1.drain: the last of eight 500-byte headers at 8 kHz back last_ms after
// the first write, in each of n runs; the earliest counts
int wchk_drain(const u32 *last_ms, int n, char *text);

// fm.pan0 and fm.pan127: the sides' levels of a note panned to want, 'l'
// or 'r', with ESFM.DRV's TrueSides on or off
int wchk_fm_side(double left_db, double right_db, char want, int true_sides,
                 char *text);

// fm.rate: the music DAC's rate fitted over secs of recording
int wchk_fm_rate(double hz, double secs, char *text);

// sb.box: sb16test's report from the DOS box
int wchk_sbt_verdict(const struct wchk_sbt *s, char *text);

// /sim: old 0 for the fixed drivers, 1 for those before the fixes
// a channel's count at us, moving at rate units a second down a ring of
// ring units, as VxD 0004 gives reading i: once in 300 readings torn,
// 256 low, by ESS's VxD
u16 wchk_sim_count(u32 us, double rate, u16 ring, u32 i, int old);

// GETPOS at us, bytes_per_ms on, as poll i gives it: once in 50 polls a
// torn count's 256 bytes ahead, through the old player
u32 wchk_sim_pos(u32 us, double bytes_per_ms, u32 i, int old);

// a drain's last header back, ms after the first write, in run
u32 wchk_sim_drain(int run, int old);

// frames of the note in a recording from frame at: 440 Hz at the music
// DAC's rate, on the side a channel panned to side ('l' or 'r') plays on,
// the other side with old
void wchk_sim_fm(s16 *pcm, u16 frames, u32 at, char side, int old);

#endif /* WCHK_H */
