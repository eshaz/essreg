/*
 * The checks of sb16test, see sb16t.h.
 *
 * Notes:
 *
 * A check runs from a DSP reset, so that nothing of the last one is left,
 * and ends with one. A transfer's buffer loops (the DMA auto-initializes)
 * and holds a whole number of the tone's cycles, so that it plays without
 * a click. The rates are measured from the block interrupts' times,
 * without the first, which comes early on the ES1869: its FIFO takes the
 * first bytes at once. The handler's path (sbt_irq and the hooks) may run
 * on another program's stack and in the middle of the checks' arithmetic,
 * so it takes no address of a local, has no varargs and does no floating
 * point.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sb16t.h"

#ifdef ESS_HOST
#define sbt_vfmt vsnprintf
#else
#define sbt_vfmt _vbprintf
#endif

#define PI 3.14159265358979

// the samples' format
#define F16 0x01 // 16-bit
#define FST 0x02 // stereo
#define FSG 0x04 // signed

// the 8237's modes, with the channel's bits 1:0 clear
#define DMA_PLAY 0x58 // single, auto-initialize, from memory
#define DMA_REC 0x54  // single, auto-initialize, into memory
#define DMA_E2 0x44   // single, into memory, once

#define TOL 0.03 // a rate's tolerance: the ES1869's divisors

enum { R_PASS, R_FAIL, R_KNOWN, R_INFO, R_SKIP };
static const char *const rname[] = {"PASS", "FAIL", "KNOWN", "INFO", "SKIP"};

static const struct sbt_hal *H;
static const struct sbt_card *C;
static unsigned O;
static u16 B;
static int stop_now;

static u16 counts[5];
static char failed[12][24];
static u16 nfailed;
static char text[220];

static u8 ver_hi, ver_lo; // E1h's answer
static u8 front;          // the ES1869 behind a front (C6h, then its register)
static u16 cfg;           // the ES1869's configuration port, 0 if unknown
static u8 audio2;         // 16-bit sound plays on Audio 2 (mixer 78h)
static u8 cur_v;          // the rate generator's value the last command set

// the interrupts, as the handler saw them
#define NLOG 24
static volatile u16 n8, n16, nnone, nlog;
static volatile u8 last82, ack_off;
static volatile u32 tlog[NLOG];
static volatile u8 klog[NLOG];
static void (*hook)(u8 kinds);
static u32 dsp_lim; // a DSP write's wait in ticks, for the handler's path

#define TAB 1024
static s16 sine_tab[TAB];

struct tone {
  u32 inc[2], ph[2];
  long amp;
};

// --- the report --------------------------------------------------------------

static void put(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  sbt_vfmt(text, sizeof(text), fmt, ap);
  va_end(ap);
  H->line(text);
}

static void say(const char *s) {
  if (H->say != NULL)
    H->say(s);
}

static const char *cur_section;

static void section(const char *name, const char *what) {
  if (cur_section != NULL && H->state != NULL)
    H->state(cur_section);
  cur_section = name;
  put("");
  put("[%s]", name);
  say(what);
}

// a check's line: its name, its result and the details
static void res(const char *id, int r, const char *fmt, ...) {
  va_list ap;
  int n;

  n = sprintf(text, "%-20s %-5s ", id, rname[r]);
  va_start(ap, fmt);
  sbt_vfmt(text + n, sizeof(text) - n, fmt, ap);
  va_end(ap);
  H->line(text);
  counts[r]++;
  if (r == R_FAIL && nfailed < 12) {
    strncpy(failed[nfailed], id, sizeof(failed[0]) - 1);
    nfailed++;
  }
}

// FAIL, or KNOWN on the ES1869, where the front can't do it
static int fail_or_known(int known) {
  return known && front ? R_KNOWN : R_FAIL;
}

// --- time --------------------------------------------------------------------

static u32 tk(u32 us) { return (u32)((double)us * (double)C->hz / 1e6 + 0.5); }

static u32 us_of(u32 t) { return (u32)((double)t * 1e6 / (double)C->hz + 0.5); }

static int escaped(void) {
  if (!stop_now && H->esc != NULL && H->esc())
    stop_now = 1;
  return stop_now;
}

// us pass; 0 if the person stopped
static int pass_time(u32 us) {
  u32 t0 = H->now(), n = tk(us);

  while (H->now() - t0 < n) {
    H->idle();
    if (escaped())
      return 0;
  }
  return 1;
}

// until *n reaches want, at most us; 1 if it did
static int wait_n(volatile u16 *n, u16 want, u32 us) {
  u32 t0 = H->now(), lim = tk(us);

  while (*n < want) {
    if (H->now() - t0 > lim || escaped())
      return 0;
    H->idle();
  }
  return 1;
}

// --- the card ----------------------------------------------------------------

static int dsp_out(u8 v) {
  u32 t0 = H->now();

  while (H->inb(B + 0x0C) & 0x80)
    if (H->now() - t0 > dsp_lim)
      return -1;
  H->outb(B + 0x0C, v);
  return 0;
}

// n command bytes; -1 if the DSP stays busy
static int cmd(int n, ...) {
  va_list ap;
  int i, r = 0;

  va_start(ap, n);
  for (i = 0; i < n; i++)
    if (r == 0 && dsp_out((u8)va_arg(ap, int)) < 0)
      r = -1;
  va_end(ap);
  return r;
}

// a byte from the DSP, -1 if none comes within us
static int dsp_in(u32 us) {
  u32 t0 = H->now(), lim = tk(us);

  while (!(H->inb(B + 0x0E) & 0x80)) {
    if (H->now() - t0 > lim)
      return -1;
    H->idle();
  }
  return H->inb(B + 0x0A);
}

// the reset handshake: 0 when AAh came, and how long it took in *us
static int dsp_reset(u32 *us) {
  u32 t0;
  int v;

  H->outb(B + 6, 1);
  pass_time(20);
  H->outb(B + 6, 0);
  t0 = H->now();
  v = dsp_in(100000L);
  if (us != NULL)
    *us = us_of(H->now() - t0);
  return v == 0xAA ? 0 : -1;
}

static u8 mix_get(u8 reg) {
  u8 v;

  H->lock();
  H->outb(B + 4, reg);
  v = H->inb(B + 5);
  H->unlock();
  return v;
}

static void mix_set(u8 reg, u8 v) {
  H->lock();
  H->outb(B + 4, reg);
  H->outb(B + 5, v);
  H->unlock();
}

static const u8 page_port[8] = {0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A};

// channel ch for bytes at addr, in Creative's order: mask, mode, flip-flop,
// address, page, count, unmask; a 16-bit channel's in words
static void dma_set(u8 ch, u32 addr, u16 bytes, u8 mode) {
  u16 mask, mp, ff, ap, cp, a, n;
  u8 page;

  if (ch >= 4) {
    mask = 0xD4, mp = 0xD6, ff = 0xD8;
    ap = (u16)(0xC0 + 4 * (ch - 4));
    cp = (u16)(ap + 2);
    a = (u16)(addr >> 1);
    n = (u16)(bytes / 2 - 1);
    page = (u8)((addr >> 16) & 0xFE);
  } else {
    mask = 0x0A, mp = 0x0B, ff = 0x0C;
    ap = (u16)(2 * ch);
    cp = (u16)(ap + 1);
    a = (u16)addr;
    n = (u16)(bytes - 1);
    page = (u8)(addr >> 16);
  }
  H->lock();
  H->outb(mask, (u8)(4 | (ch & 3)));
  H->outb(mp, (u8)(mode | (ch & 3)));
  H->outb(ff, 0);
  H->outb(ap, (u8)a);
  H->outb(ap, (u8)(a >> 8));
  H->outb(page_port[ch], page);
  H->outb(cp, (u8)n);
  H->outb(cp, (u8)(n >> 8));
  H->outb(mask, (u8)(ch & 3));
  H->unlock();
}

// the channel's count, as games read it: the bytes (or words) left, less one
static u16 dma_left(u8 ch) {
  u16 ff = ch >= 4 ? 0xD8 : 0x0C;
  u16 cp = ch >= 4 ? (u16)(0xC2 + 4 * (ch - 4)) : (u16)(2 * ch + 1);
  u16 v;

  H->lock();
  H->outb(ff, 0);
  v = H->inb(cp);
  v |= (u16)H->inb(cp) << 8;
  H->unlock();
  return v;
}

static void dma_mask(u8 ch) {
  H->outb(ch >= 4 ? 0xD4 : 0x0A, (u8)(4 | (ch & 3)));
}

static void counters(void) {
  H->lock();
  n8 = n16 = nnone = nlog = 0;
  H->unlock();
}

// a check ends: the DSP reset, the channels masked, nothing pending
static void quiet(void) {
  hook = NULL;
  dsp_reset(NULL);
  dma_mask(C->dma8);
  if (C->dma16)
    dma_mask(C->dma16);
  H->inb(B + 0x0E);
  H->inb(B + 0x0F);
  ack_off = 0;
  cur_v = 0x38;
  pass_time(2000);
  counters();
}

void sbt_irq(void) {
  u8 st;

  if (H == NULL)
    return;
  H->outb(B + 4, 0x82);
  st = H->inb(B + 5);
  last82 = st;
  if (nlog < NLOG) {
    tlog[nlog] = H->now();
    klog[nlog] = (u8)(st & 3);
  }
  nlog++;
  if (!ack_off) {
    if (st & 2)
      H->inb(B + 0x0F);
    if (st & 1)
      H->inb(B + 0x0E);
  }
  if (st & 2)
    n16++;
  if (st & 1)
    n8++;
  if (!(st & 3))
    nnone++;
  if (hook != NULL)
    hook((u8)(st & 3));
}

// --- what DSP 4.05 does ------------------------------------------------------

u8 sbt_tc_to_v(u8 tc) {
  u8 t = tc > 0xEB ? 0xEB : tc;
  u32 v;

  if (t == 0x68)
    return 0x25;
  if (t == 0x6C)
    return 0x26;
  if (t == 0xC8)
    return 0xC8; // the ROM table's error before 4.13: 35.6 kHz
  v = 248000000UL / ((u32)(256 - t) * 44100UL);
  return (u8)(v > 0xFF ? 0xFF : v);
}

u8 sbt_rate_to_v(u16 rate) {
  u16 hi = rate >> 8, lo = rate & 0xFF, s, x;

  if (hi >= 0xB1)
    return 0xFF;
  if (hi < 0x13)
    return 0x1C;
  // the firmware multiplies by 23 in two bytes and loses the carry
  s = (u16)(((23 * hi) & 0xFF) + ((23 * lo) >> 8));
  x = (u16)((((23 * hi) >> 8) << 8) | (s & 0xFF));
  return (u8)((x >> 4) + ((x >> 3) & 1));
}

double sbt_v_hz(u8 v) { return v * 44100.0 / 248.0; }

static void rate41(u16 hz) {
  cmd(3, 0x41, hz >> 8, hz & 0xFF);
  cur_v = sbt_rate_to_v(hz);
}

static void rate40(u8 tc) {
  cmd(2, 0x40, tc);
  cur_v = sbt_tc_to_v(tc);
}

// --- tones -------------------------------------------------------------------

static void tab_init(void) {
  int i;

  for (i = 0; i < TAB; i++)
    sine_tab[i] = (s16)floor(32767.0 * sin(2 * PI * i / TAB) + 0.5);
}

// a frequency near hz with a whole number of cycles in frames at rate
static double fit_hz(double hz, u16 frames, double rate) {
  double cycles = floor(hz * frames / rate + 0.5);

  if (cycles < 1)
    cycles = 1;
  return cycles * rate / frames;
}

// hz_l on the left (or mono), hz_r on the right, 0 for none, at amp of
// full scale (0-32767)
static void tone_set(struct tone *t, double hz_l, double hz_r, double rate,
                     long amp) {
  t->inc[0] = (u32)(hz_l / rate * 4294967296.0);
  t->inc[1] = (u32)(hz_r / rate * 4294967296.0);
  t->ph[0] = t->ph[1] = 0;
  t->amp = amp;
}

static s16 tone_next(struct tone *t, int c) {
  s16 v;

  if (t->inc[c] == 0)
    return 0;
  v = (s16)(((long)sine_tab[(u16)(t->ph[c] >> 22) & (TAB - 1)] * t->amp) >> 15);
  t->ph[c] = (t->ph[c] + t->inc[c]) & 0xFFFFFFFFUL;
  return v;
}

// frames of the tone at addr, in the format fmt
static void fill(u32 addr, u16 frames, u8 fmt, struct tone *t) {
  static u8 buf[256];
  u16 i = 0, f, w;
  int c, chans = (fmt & FST) ? 2 : 1;
  s16 s;

  for (f = 0; f < frames; f++) {
    for (c = 0; c < chans; c++) {
      s = tone_next(t, c);
      w = (u16)s;
      if (fmt & F16) {
        if (!(fmt & FSG))
          w ^= 0x8000;
        buf[i++] = (u8)w;
        buf[i++] = (u8)(w >> 8);
      } else {
        w >>= 8;
        if (!(fmt & FSG))
          w ^= 0x80;
        buf[i++] = (u8)w;
      }
    }
    if (i >= sizeof(buf) - 4) {
      H->poke(addr, buf, i);
      addr += i;
      i = 0;
    }
  }
  if (i)
    H->poke(addr, buf, i);
}

static void fill_byte(u32 addr, u16 n, u8 v) {
  static u8 buf[128];
  u16 k;

  memset(buf, v, sizeof(buf));
  while (n) {
    k = n > sizeof(buf) ? (u16)sizeof(buf) : n;
    H->poke(addr, buf, k);
    addr += k;
    n -= k;
  }
}

// --- the buffers and the durations -------------------------------------------

static u16 frame_bytes(u8 fmt) {
  return (u16)(((fmt & F16) ? 2 : 1) * ((fmt & FST) ? 2 : 1));
}

#define BUF8 (C->area)            // the 8-bit transfers', 16 KB
#define BUF16 (C->area + 0x4000U) // the 16-bit transfers', 16 KB

// frames of a measured block at rate: about 60 ms, or short, more than the
// ES1869's FIFO (256 bytes), which takes a block's first bytes at once
static u16 block_frames(double rate, u8 fmt) {
  double f = rate / 16;

  if (O & SBT_O_SHORT)
    f = 320 / frame_bytes(fmt);
  if (f < 32)
    f = 32;
  if (f > 2048)
    f = 2048;
  return (u16)((u16)f & ~1U);
}

static u16 blocks(void) { return (O & SBT_O_SHORT) ? 4 : 8; }

static u32 sound_us(void) { return (O & SBT_O_SHORT) ? 20000UL : 2000000UL; }

// --- a transfer --------------------------------------------------------------

struct xfer {
  u8 cmd;     // the DSP command
  u8 mode;    // Bxh and Cxh's mode byte: bit 4 signed, bit 5 stereo
  u8 fmt;     // the samples, F16, FST and FSG
  u8 ch;      // the DMA channel
  u16 frames; // a block
  u8 nb;      // the blocks in the buffer, which loops
  u8 rec;     // a recording
  u32 addr;   // the buffer
};

struct meas {
  u16 ints;   // the interrupts of the transfer's kind
  u16 other;  // of the other kind
  double hz;  // the rate their times give
  u8 moving;  // the DMA count moved
  u8 started; // the command went in
  u32 first;  // the first interrupt, us after the command
};

static u8 kind_of(const struct xfer *x) {
  return (u8)((x->cmd & 0xF0) == 0xB0 ? 2 : 1);
}

static int generic(u8 c) { return (c & 0xF0) == 0xB0 || (c & 0xF0) == 0xC0; }

// the buffer and the DMA, then the command; 0 if the DSP took it
static int x_start(const struct xfer *x) {
  u16 chans = (x->fmt & FST) ? 2 : 1;
  u16 len = (u16)(x->frames * chans - 1);
  u16 bytes = (u16)(x->frames * frame_bytes(x->fmt) * x->nb);
  u16 blen = (u16)(x->frames * frame_bytes(x->fmt) - 1);

  dma_set(x->ch, x->addr, bytes, x->rec ? DMA_REC : DMA_PLAY);
  if (generic(x->cmd))
    return cmd(4, x->cmd, x->mode, len & 0xFF, len >> 8);
  if (x->cmd == 0x14 || x->cmd == 0x24)
    return cmd(3, x->cmd, blen & 0xFF, blen >> 8);
  return cmd(1, x->cmd); // 1Ch, 2Ch, 90h, 91h, 98h, 99h: 48h's length
}

// the interrupts' rate of the transfer's kind, from the second on
static double log_rate(u8 kind, u16 frames) {
  u16 i, k = 0, n = nlog < NLOG ? nlog : NLOG;
  u32 first = 0, last = 0;

  for (i = 0; i < n; i++) {
    if (!(klog[i] & kind))
      continue;
    k++;
    if (k == 2)
      first = tlog[i];
    last = tlog[i];
  }
  if (k < 3 || last == first)
    return 0;
  return (double)(k - 2) * frames * (double)C->hz / (double)(last - first);
}

// the transfer runs until want interrupts of its kind came, or time out
static void x_run(const struct xfer *x, u16 want, struct meas *m) {
  u32 t0, lim, step, ts;
  u16 c0, c;
  double block_us = x->frames * 1e6 / sbt_v_hz(cur_v);
  volatile u16 *n;

  memset(m, 0, sizeof(*m));
  n = kind_of(x) == 2 ? &n16 : &n8;
  counters();
  m->started = x_start(x) == 0;
  t0 = H->now();
  lim = tk((u32)(block_us * (want + 2) * 2) + 200000UL);
  step = tk((u32)(block_us / 8) + 1);
  c0 = dma_left(x->ch);
  ts = t0;
  while (*n < want && H->now() - t0 < lim && !escaped()) {
    H->idle();
    if (H->now() - ts >= step) {
      ts = H->now();
      c = dma_left(x->ch);
      if (c != c0)
        m->moving = 1;
    }
  }
  m->ints = *n;
  m->other = kind_of(x) == 2 ? n8 : n16;
  m->hz = log_rate(kind_of(x), x->frames);
  if (nlog > 0)
    m->first = us_of(tlog[0] - t0);
}

static double err_of(double got, double want) {
  return want > 0 ? (got - want) / want : 0;
}

// a measured transfer's line: PASS when its interrupts came at the rate
// the Sound Blaster 16 plays
static int x_check(const char *id, const struct xfer *x, u16 want) {
  struct meas m;
  double sb = sbt_v_hz(cur_v), e;
  int ok;

  x_run(x, want, &m);
  e = err_of(m.hz, sb);
  ok = m.started && m.ints >= want && m.moving && (want < 3 || fabs(e) <= TOL);
  res(id, ok ? R_PASS : R_FAIL,
      "ints=%u/%u other=%u rate=%.0f sb16=%.0f err=%+.2f%% dma=%s "
      "first=%luus 82h=%02X",
      m.ints, want, m.other, m.hz, sb, e * 100, m.moving ? "moving" : "still",
      (unsigned long)m.first, last82);
  quiet();
  return ok;
}

// --- the ES1869 behind a front -----------------------------------------------

// the chip's own Extended mode: C6h and a register command switch a front
// to the chip until the next reset (C6h is also the 8-bit auto-initialize
// transfer, whose mode byte has only bits 4 and 5); -1 if no answer
static int ess_read(u8 reg) {
  if (cmd(3, 0xC6, 0xC0, reg) < 0)
    return -1;
  return dsp_in(20000);
}

static int ess_more(u8 reg) {
  if (cmd(2, 0xC0, reg) < 0)
    return -1;
  return dsp_in(20000);
}

static u8 cfg_get(u16 off) { return H->inb((u16)(cfg + off)); }

// a PnP register through the configuration port, the index and the
// logical device put back
static int pnp_get(u8 ldn, u8 reg) {
  u8 old_index, old_ldn, v;

  if (cfg == 0)
    return -1;
  H->lock();
  old_index = H->inb(cfg);
  H->outb(cfg, 0x07);
  old_ldn = H->inb((u16)(cfg + 1));
  if (ldn != 0xFF)
    H->outb((u16)(cfg + 1), ldn);
  H->outb(cfg, reg);
  v = H->inb((u16)(cfg + 1));
  H->outb(cfg, 0x07);
  H->outb((u16)(cfg + 1), old_ldn);
  H->outb(cfg, old_index);
  H->unlock();
  return v;
}

// the chip's registers as it has them, in its own Extended mode; a reset
// gives the front back
static void chip_dump(const char *when) {
  static const u8 pnp_card[] = {0x20, 0x21, 0x22, 0x23, 0x24,
                                0x25, 0x26, 0x27, 0x28};
  static const u8 pnp_ldn[] = {0x30, 0x60, 0x61, 0x62, 0x63, 0x70,
                               0x71, 0x72, 0x73, 0x74, 0x75};
  char *p;
  int i, v, n;
  u8 id[4];

  if (ess_read(0xA0) < 0) {
    res("chip.regs", R_FAIL, "%s: no answer in the chip's Extended mode", when);
    quiet();
    return;
  }
  p = text + sprintf(text, "%-20s INFO  %s A0-BF:", "chip.ctrl", when);
  for (i = 0xA0; i <= 0xBF; i++) {
    v = ess_more((u8)i);
    if (v < 0)
      p += sprintf(p, " --");
    else
      p += sprintf(p, " %02X", v);
  }
  H->line(text);
  counts[R_INFO]++;
  // the mixer: the chip's own in this mode; 40h's reads give its ID
  for (i = 0; i < 0x80; i += 0x20) {
    p = text + sprintf(text, "%-20s INFO  %s %02X-%02X:", "chip.mixer", when, i,
                       i + 0x1F);
    for (n = i; n < i + 0x20; n++)
      if (n == 0x40)
        p += sprintf(p, " id");
      else
        p += sprintf(p, " %02X", mix_get((u8)n));
    H->line(text);
  }
  H->lock();
  H->outb(B + 4, 0x40);
  for (i = 0; i < 4; i++)
    id[i] = H->inb(B + 5);
  H->unlock();
  if (id[0] == 0x18 && id[1] == 0x69)
    cfg = (u16)((id[2] << 8) | id[3]);
  res("chip.id", cfg ? R_INFO : R_FAIL,
      "%s 40h: %02X %02X %02X %02X config=%Xh", when, id[0], id[1], id[2],
      id[3], cfg);
  if (cfg) {
    p = text + sprintf(text, "%-20s INFO  %s ports:", "chip.config", when);
    for (i = 5; i < 8; i++)
      p += sprintf(p, " +%d=%02X", i, cfg_get((u16)i));
    p += sprintf(p, " card:");
    for (i = 0; i < (int)sizeof(pnp_card); i++)
      p += sprintf(p, " %02X=%02X", pnp_card[i], pnp_get(0xFF, pnp_card[i]));
    H->line(text);
    p = text + sprintf(text, "%-20s INFO  %s ldn1:", "chip.pnp", when);
    for (i = 0; i < (int)sizeof(pnp_ldn); i++)
      p += sprintf(p, " %02X=%02X", pnp_ldn[i], pnp_get(1, pnp_ldn[i]));
    H->line(text);
  }
  quiet();
}

// the chip's Audio 2 and mixer registers while a transfer of a front's
// plays (they pass through it), and the interrupt status
static void chip_now(const char *id) {
  static const u8 regs[] = {0x14, 0x1C, 0x70, 0x71, 0x72, 0x74,
                            0x76, 0x78, 0x7A, 0x7C, 0x7F};
  char *p;
  int i;

  if (!front)
    return;
  p = text + sprintf(text, "%-20s INFO ", id);
  for (i = 0; i < (int)sizeof(regs); i++)
    p += sprintf(p, " %02X=%02X", regs[i], mix_get(regs[i]));
  if (cfg)
    p += sprintf(p, " isr=%02X imr=%02X", cfg_get(6), cfg_get(7));
  H->line(text);
  counts[R_INFO]++;
}

// --- the checks: the DSP -----------------------------------------------------

static int read_n(u8 *out, int n) {
  int i, v;

  for (i = 0; i < n; i++) {
    v = dsp_in(20000);
    if (v < 0)
      return i;
    out[i] = (u8)v;
  }
  return n;
}

static int check_dsp(void) {
  u8 b[4];
  u32 us;
  int n, ok;

  section("dsp", "the DSP's commands");
  ok = dsp_reset(&us) == 0;
  res("dsp.reset", ok ? R_PASS : R_FAIL, "AAh after %luus", (unsigned long)us);
  if (!ok)
    return 0;
  cmd(1, 0xE1);
  n = read_n(b, 2);
  ver_hi = n == 2 ? b[0] : 0;
  ver_lo = n == 2 ? b[1] : 0;
  res("dsp.version", ver_hi == 4 ? R_PASS : R_FAIL,
      "E1h: %u.%02u (4.05 expected)", ver_hi, ver_lo);
  return ver_hi == 4;
}

static void check_dsp2(void) {
  static const char copyright[] =
      "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";
  u8 b[48], held;
  int n, ok;

  // the fronts answer 2xCh from their own state, which a reset clears
  H->outb(B + 6, 1);
  pass_time(20);
  held = H->inb(B + 0x0C);
  H->outb(B + 6, 0);
  dsp_in(100000L);
  res("dsp.reset_held", (held & 0x80) ? R_PASS : fail_or_known(1),
      "2xC=%02X while held in reset (busy expected)", held);
  cmd(1, 0xE3);
  n = read_n(b, 45);
  b[n < 45 ? n : 45] = 0;
  res("dsp.copyright",
      n == 45 && b[44] == 0 && !strcmp((char *)b, copyright) ? R_PASS : R_FAIL,
      "E3h: %d bytes \"%.44s\"", n, (char *)b);
  cmd(2, 0xE0, 0x5A);
  b[0] = (u8)dsp_in(20000);
  cmd(2, 0xE0, 0x0F);
  b[1] = (u8)dsp_in(20000);
  res("dsp.invert", b[0] == 0xA5 && b[1] == 0xF0 ? R_PASS : R_FAIL,
      "E0h 5Ah: %02X, 0Fh: %02X", b[0], b[1]);
  cmd(2, 0xE4, 0x96);
  cmd(1, 0xE8);
  b[0] = (u8)dsp_in(20000);
  cmd(2, 0xE4, 0x69);
  cmd(1, 0xE8);
  b[1] = (u8)dsp_in(20000);
  res("dsp.test_reg", b[0] == 0x96 && b[1] == 0x69 ? R_PASS : R_FAIL,
      "E4h/E8h: %02X %02X", b[0], b[1]);
  cmd(2, 0xD3, 0xD8);
  b[0] = (u8)dsp_in(20000);
  cmd(2, 0xD1, 0xD8);
  b[1] = (u8)dsp_in(20000);
  res("dsp.speaker", b[0] == 0x00 && b[1] == 0xFF ? R_PASS : R_FAIL,
      "D8h after D3h: %02X, after D1h: %02X", b[0], b[1]);
  cmd(1, 0xF8);
  b[0] = (u8)dsp_in(20000);
  res("dsp.f8", b[0] == 0 ? R_PASS : R_FAIL, "F8h: %02X", b[0]);
  cmd(1, 0xF4);
  n = read_n(b, 2);
  res("dsp.rom_id", n == 2 && b[0] == 0x7D && b[1] == 0x1B ? R_PASS : R_INFO,
      "F4h: %02X %02X (4.05: 7D 1B)", b[0], b[1]);
  // a command written while an answer waits is taken once it is read
  cmd(3, 0xE0, 0x11, 0xE1);
  n = read_n(b, 3);
  res("dsp.queued",
      n == 3 && b[0] == 0xEE && b[1] == 4 && b[2] == ver_lo ? R_PASS : R_FAIL,
      "E0h 11h then E1h: %d bytes %02X %02X %02X", n, b[0], b[1], b[2]);
  cmd(1, 0xE1);
  read_n(b, 2);
  cmd(1, 0xFD);
  b[0] = (u8)dsp_in(20000);
  res("dsp.fd", b[0] == 0xE1 ? R_PASS : R_FAIL, "FDh after E1h: %02X", b[0]);
  // the firmware's RAM: 48h's length, 40h's rate value, the version's bytes
  cmd(3, 0x48, 0x34, 0x12);
  cmd(2, 0x40, 0xD3);
  cmd(2, 0xF9, 0x0E);
  b[0] = (u8)dsp_in(20000);
  cmd(2, 0xF9, 0x0F);
  b[1] = (u8)dsp_in(20000);
  cmd(2, 0xF9, 0x37);
  b[2] = (u8)dsp_in(20000);
  cmd(2, 0xF9, 0x81);
  b[3] = (u8)dsp_in(20000);
  ok = b[0] == 0x34 && b[1] == 0x12 && b[2] == 0x7C && b[3] == 0x1D;
  res("dsp.ram", ok ? R_PASS : fail_or_known(1),
      "F9h 0Eh/0Fh: %02X %02X (34 12), 37h: %02X (7C), 81h: %02X (1D)", b[0],
      b[1], b[2], b[3]);
  quiet();
}

// E2h writes a byte through the 8-bit channel that depends on its operand
static void check_e2(void) {
  u8 got = 0x00;
  u8 want = (u8)(0xAA + (0xAA ^ 0x96)); // the first after a cold boot

  dsp_reset(NULL);
  fill_byte(BUF8, 4, 0x00);
  dma_set(C->dma8, BUF8, 1, DMA_E2);
  cmd(2, 0xE2, 0xAA);
  pass_time(10000);
  H->peek(BUF8, &got, 1);
  res("dsp.e2", got == want ? R_PASS : fail_or_known(1),
      "E2h AAh wrote %02X (%02X expected)", got, want);
  quiet();
}

// --- the checks: the mixer ---------------------------------------------------

static const u8 mix_defaults[0x18] = {
    0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1F, 0x15, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80};
static const u8 mix_masks[0x18] = {
    0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xC0,
    0x1F, 0x7F, 0x7F, 0xC0, 0xC0, 0xC0, 0xC0, 0x01, 0xF0, 0xF0, 0xF0, 0xF0};

static u8 saved[0x18]; // the view's 30h-47h when the program started
static u8 saved81;
static u8 keep[SBT_KEEP];
static u16 nkeep;
static u8 saved_1c;

static void keep_add(u8 reg, u8 v) {
  if (nkeep + 2 <= SBT_KEEP) {
    keep[nkeep++] = reg;
    keep[nkeep++] = v;
  }
}

// what the program changes, kept for a restart after a crash
static void mix_save(void) {
  int i;

  nkeep = 0;
  for (i = 0; i < 0x18; i++) {
    saved[i] = mix_get((u8)(0x30 + i));
    keep_add((u8)(0x30 + i), saved[i]);
  }
  saved81 = mix_get(0x81);
  if (front) {
    saved_1c = mix_get(0x1C);
    keep_add(0x1C, saved_1c);
  }
  if (H->keep != NULL)
    H->keep(keep, nkeep);
}

static void mix_restore(void) {
  int i;

  mix_set(0x81, saved81);
  for (i = 0; i < 0x18; i++)
    mix_set((u8)(0x30 + i), saved[i]);
  if (front)
    mix_set(0x1C, saved_1c);
  if (H->keep != NULL)
    H->keep(keep, 0);
}

static u8 irq_bit(u8 irq) {
  switch (irq) {
  case 2:
  case 9:
    return 1;
  case 5:
    return 2;
  case 7:
    return 4;
  case 10:
    return 8;
  }
  return 0;
}

static void check_mixer(void) {
  static char hex[0x18 * 2 + 1]; // res() formats into text
  u8 v[0x18], bad = 0, a, b2, c;
  int i;
  char *p;

  section("mixer", "the mixer");
  H->lock();
  H->outb(B + 4, 0x33);
  a = H->inb(B + 4);
  H->unlock();
  res("mix.index", a == 0x33 ? R_PASS : R_FAIL, "2x4 reads %02X (33)", a);
  mix_set(0x00, 0x00);
  for (i = 0; i < 0x18; i++) {
    v[i] = mix_get((u8)(0x30 + i));
    if (v[i] != mix_defaults[i])
      bad++;
  }
  p = hex;
  for (i = 0; i < 0x18; i++)
    p += sprintf(p, "%02X", v[i]);
  res("mix.reset", bad ? R_FAIL : R_PASS, "30h-47h after 00h: %s", hex);
  bad = 0;
  for (i = 0; i < 0x18; i++) {
    mix_set((u8)(0x30 + i), 0xFF);
    v[i] = mix_get((u8)(0x30 + i));
    if (v[i] != mix_masks[i])
      bad++;
    mix_set((u8)(0x30 + i), 0x00);
    if (mix_get((u8)(0x30 + i)) != 0)
      bad++;
  }
  p = hex;
  for (i = 0; i < 0x18; i++)
    p += sprintf(p, "%02X", v[i]);
  res("mix.bits", bad ? R_FAIL : R_PASS, "30h-47h written FFh: %s", hex);
  // the Sound Blaster Pro's registers are views of the Sound Blaster 16's
  mix_set(0x04, 0xA6);
  a = mix_get(0x04);
  b2 = mix_get(0x32);
  c = mix_get(0x33);
  res("mix.pro", a == 0xA6 && b2 == 0xA8 && c == 0x68 ? R_PASS : R_FAIL,
      "04h A6h: 04h=%02X 32h=%02X 33h=%02X (A6 A8 68)", a, b2, c);
  mix_set(0x22, 0x5C);
  a = mix_get(0x22);
  b2 = mix_get(0x30);
  c = mix_get(0x31);
  res("mix.pro_master", a == 0x5C && b2 == 0x58 && c == 0xC8 ? R_PASS : R_FAIL,
      "22h 5Ch: 22h=%02X 30h=%02X 31h=%02X (5C 58 C8)", a, b2, c);
  mix_set(0x0A, 0x05);
  a = mix_get(0x0A);
  b2 = mix_get(0x3A);
  res("mix.pro_mic", a == 0x05 && b2 == 0xB8 ? R_PASS : R_FAIL,
      "0Ah 05h: 0Ah=%02X 3Ah=%02X (05 B8)", a, b2);
  a = mix_get(0x0C);
  b2 = mix_get(0x0E);
  res("mix.absent", a == 0 && b2 == 0 ? R_PASS : R_FAIL,
      "0Ch=%02X 0Eh=%02X (the CT1745 has neither)", a, b2);
  a = mix_get(0x80);
  res("mix.80", a == (0xF0 | irq_bit(C->irq)) ? R_PASS : R_FAIL,
      "80h=%02X for IRQ %u", a, C->irq);
  a = mix_get(0x81);
  b2 = (u8)((1 << C->dma8) | (C->dma16 ? 1 << C->dma16 : 0));
  res("mix.81", a == b2 ? R_PASS : R_FAIL, "81h=%02X for DMA %u and %u (%02X)",
      a, C->dma8, C->dma16, b2);
  a = mix_get(0x82);
  res("mix.82", (a & 0x0F) == 0 ? R_PASS : R_FAIL,
      "82h=%02X with nothing pending", a);
  if (C->dma16) {
    mix_set(0x81, (u8)(1 << C->dma8));
    a = mix_get(0x81);
    mix_set(0x81, b2);
    c = mix_get(0x81);
    res("mix.81_write", a == (u8)(1 << C->dma8) && c == b2 ? R_PASS : R_FAIL,
        "81h without the 16-bit channel: %02X, with it again: %02X", a, c);
  }
  for (i = 0; i < 0x18; i++)
    mix_set((u8)(0x30 + i), saved[i]);
}

// --- the checks: interrupts --------------------------------------------------

static void check_irq(void) {
  u8 p1, p2, p3, p4;
  u32 t0;
  int got;

  section("irq", "the interrupts");
  counters();
  t0 = H->now();
  cmd(1, 0xF2);
  got = wait_n(&n8, 1, 50000L);
  res("irq.f2", got && last82 & 1 ? R_PASS : R_FAIL,
      "F2h: %u 8-bit interrupts, after %luus, 82h=%02X, on IRQ %u", n8,
      (unsigned long)(nlog ? us_of(tlog[0] - t0) : 0), last82, C->irq);
  counters();
  t0 = H->now();
  cmd(1, 0xF3);
  got = wait_n(&n16, 1, 50000L);
  res("irq.f3", got && last82 & 2 ? R_PASS : R_FAIL,
      "F3h: %u 16-bit interrupts, after %luus, 82h=%02X", n16,
      (unsigned long)(nlog ? us_of(tlog[0] - t0) : 0), last82);
  // with the IRQ masked: 82h holds each until its acknowledge
  H->irq_mask(1);
  cmd(1, 0xF2);
  pass_time(2000);
  p1 = mix_get(0x82);
  H->inb(B + 0x0E);
  p2 = mix_get(0x82);
  cmd(1, 0xF3);
  pass_time(2000);
  p3 = mix_get(0x82);
  H->inb(B + 0x0F);
  p4 = mix_get(0x82);
  H->irq_mask(0);
  pass_time(2000);
  res("irq.82",
      (p1 & 3) == 1 && (p2 & 3) == 0 && (p3 & 3) == 2 && (p4 & 3) == 0 ? R_PASS
                                                                       : R_FAIL,
      "82h after F2h %02X, 2xE read %02X, after F3h %02X, 2xF read %02X", p1,
      p2, p3, p4);
  counters();
}

// --- the checks: transfers ---------------------------------------------------

static void x_init(struct xfer *x, u8 c, u8 mode, u8 fmt, double rate) {
  memset(x, 0, sizeof(*x));
  x->cmd = c;
  x->mode = mode;
  x->fmt = fmt;
  x->ch = (fmt & F16) && C->dma16 ? C->dma16 : C->dma8;
  x->frames = block_frames(rate, fmt);
  x->nb = 2;
  x->addr = (fmt & F16) ? BUF16 : BUF8;
}

// the transfer's buffer: a quiet tone, so that the checks don't blare
static void x_tone(const struct xfer *x, double rate) {
  struct tone t;
  double hz = fit_hz(440, (u16)(x->frames * x->nb), rate);

  tone_set(&t, hz, (x->fmt & FST) ? hz * 2 : 0, rate, 2000);
  if (x->fmt & FST)
    t.inc[1] = (u32)(fit_hz(880, (u16)(x->frames * x->nb), rate) / rate *
                     4294967296.0);
  fill(x->addr, (u16)(x->frames * x->nb), x->fmt, &t);
}

static void play8(const char *id, u8 c, u8 mode, u8 fmt, u16 hz, u8 tc) {
  struct xfer x;
  double rate;

  dsp_reset(NULL);
  cur_v = 0x38;
  if (hz)
    rate41(hz);
  else
    rate40(tc);
  rate = sbt_v_hz(cur_v);
  x_init(&x, c, mode, fmt, rate);
  if (!generic(c)) {
    u16 blen = (u16)(x.frames * frame_bytes(fmt) - 1);
    cmd(3, 0x48, blen & 0xFF, blen >> 8);
  }
  x_tone(&x, rate);
  x_check(id, &x,
          (c == 0x14 || c == 0x91 || c == 0xC0 || c == 0xB0) ? 1 : blocks());
}

static void check_play(void) {
  section("play", "8-bit and 16-bit playback");
  play8("p8.14h", 0x14, 0, 0, 0, 0xD3);
  play8("p8.1ch", 0x1C, 0, 0, 0, 0xD3);
  play8("p8.90h", 0x90, 0, 0, 0, 0xE9);
  play8("p8.91h", 0x91, 0, 0, 0, 0xE9);
  play8("p8.c0h", 0xC0, 0x00, 0, 22050, 0);
  play8("p8.c6h_mono", 0xC6, 0x00, 0, 22050, 0);
  play8("p8.c6h_stereo", 0xC6, 0x20, FST, 22050, 0);
  play8("p8.c6h_signed", 0xC6, 0x10, FSG, 22050, 0);
  play8("p8.c6h_stsigned", 0xC6, 0x30, FST | FSG, 22050, 0);
  play8("p16.b0h", 0xB0, 0x10, F16 | FSG, 44100, 0);
  play8("p16.b6h_mono", 0xB6, 0x10, F16 | FSG, 44100, 0);
  play8("p16.b6h_stereo", 0xB6, 0x30, F16 | FST | FSG, 44100, 0);
  play8("p16.b6h_unsigned", 0xB6, 0x00, F16, 22050, 0);
  play8("p16.b6h_stunsign", 0xB6, 0x20, F16 | FST, 22050, 0);
}

// 16-bit sound through the 8-bit channel, as mixer 81h allows without a
// 16-bit channel
static void check_play_on8(void) {
  struct xfer x;
  u8 m81;

  if (!C->dma16)
    return;
  m81 = mix_get(0x81);
  mix_set(0x81, (u8)(1 << C->dma8));
  dsp_reset(NULL);
  rate41(22050);
  x_init(&x, 0xB6, 0x10, F16 | FSG, 22050);
  x.ch = C->dma8;
  x_tone(&x, 22050);
  x_check("p16.on_8bit_dma", &x, blocks());
  mix_set(0x81, m81);
}

// the chip's Audio 2 while a 16-bit transfer plays, and Audio 1 while an
// 8-bit one does
static void check_routing(void) {
  struct xfer x;
  struct meas m;
  u8 r78;

  if (!front)
    return;
  section("route", "which DAC of the ES1869 plays what");
  dsp_reset(NULL);
  rate41(22050);
  x_init(&x, 0xB6, 0x30, F16 | FST | FSG, 22050);
  x_tone(&x, 22050);
  x_run(&x, 2, &m);
  r78 = mix_get(0x78);
  audio2 = (r78 & 0x03) == 0x03;
  chip_now("route.16bit");
  res("route.audio2", R_INFO, "78h=%02X: 16-bit sound plays on %s", r78,
      audio2 ? "Audio 2" : "Audio 1");
  quiet();
  rate41(22050);
  x_init(&x, 0xC6, 0x00, 0, 22050);
  x_tone(&x, 22050);
  x_run(&x, 2, &m);
  chip_now("route.8bit");
  quiet();
}

static void check_rates(void) {
  static const u16 hz41[] = {5000,  8000,  11025, 16000, 22050,
                             23000, 32000, 44100, 48000};
  static const u8 tc40[] = {0x68, 0x6C, 0xA5, 0xC8, 0xD3, 0xE9};
  char id[24];
  u16 i, n;

  section("rate", "the rates");
  n = (O & SBT_O_QUICK) ? 3 : (u16)ESS_ARRAY_SIZE(hz41);
  for (i = 0; i < n; i++) {
    sprintf(id, "rate.41h_%u", hz41[i]);
    play8(id, 0xC6, 0x00, 0, hz41[i], 0);
  }
  n = (O & SBT_O_QUICK) ? 2 : (u16)ESS_ARRAY_SIZE(tc40);
  for (i = 0; i < n; i++) {
    sprintf(id, "rate.40h_%02X", tc40[i]);
    play8(id, 0xC6, 0x00, 0, 0, tc40[i]);
  }
  if (C->dma16) {
    play8("rate.16bit_22050", 0xB6, 0x10, F16 | FSG, 22050, 0);
    play8("rate.16bit_11025", 0xB6, 0x10, F16 | FSG, 11025, 0);
    if (!(O & SBT_O_QUICK))
      play8("rate.16bit_8000", 0xB6, 0x10, F16 | FSG, 8000, 0);
  }
  // a reset after the high-speed and MIDI UART commands keeps the rate (a
  // warm boot), another sets 9958 Hz (a cold one)
  dsp_reset(NULL);
  rate40(0xD3);
  cmd(1, 0x35);
  dsp_reset(NULL);
  cur_v = 0x7C;
  {
    struct xfer x;
    x_init(&x, 0xC6, 0x00, 0, sbt_v_hz(cur_v));
    x_tone(&x, sbt_v_hz(cur_v));
    x_check("rate.warm_boot", &x, blocks());
    cur_v = 0x38;
    x_init(&x, 0xC6, 0x00, 0, sbt_v_hz(cur_v));
    x_tone(&x, sbt_v_hz(cur_v));
    x_check("rate.cold_boot", &x, blocks());
  }
}

// --- the checks: pause, continue, the end of auto-initialize -----------------

static void ctl_start(struct xfer *x, int sixteen) {
  dsp_reset(NULL);
  rate41(22050);
  if (sixteen)
    x_init(x, 0xB6, 0x10, F16 | FSG, 22050);
  else
    x_init(x, 0xC6, 0x00, 0, 22050);
  x_tone(x, 22050);
  counters();
  x_start(x);
}

static void check_pause(int sixteen) {
  struct xfer x;
  volatile u16 *n = sixteen ? &n16 : &n8;
  double bus = block_frames(22050, 0) * 1e6 / sbt_v_hz(sbt_rate_to_v(22050));
  u16 before, during, c1, c2;
  int ok;

  ctl_start(&x, sixteen);
  wait_n(n, 2, (u32)(bus * 6) + 100000UL);
  cmd(1, sixteen ? 0xD5 : 0xD0);
  pass_time((u32)bus);
  before = *n;
  c1 = dma_left(x.ch);
  pass_time((u32)(bus * 3));
  during = *n;
  c2 = dma_left(x.ch);
  cmd(1, sixteen ? 0xD6 : 0xD4);
  ok = wait_n(n, (u16)(during + 2), (u32)(bus * 6) + 100000UL);
  res(sixteen ? "ctl.pause16" : "ctl.pause8",
      ok && during == before && c1 == c2 ? R_PASS : R_FAIL,
      "%s: %u interrupts in 3 blocks of pause, count %04X then %04X, "
      "%s after %s",
      sixteen ? "D5h" : "D0h", during - before, c1, c2,
      ok ? "on again" : "nothing", sixteen ? "D6h" : "D4h");
  quiet();
}

static void check_exit(int sixteen) {
  struct xfer x;
  volatile u16 *n = sixteen ? &n16 : &n8;
  double bus = block_frames(22050, 0) * 1e6 / sbt_v_hz(sbt_rate_to_v(22050));
  u16 at, after;

  ctl_start(&x, sixteen);
  wait_n(n, 2, (u32)(bus * 6) + 100000UL);
  cmd(1, sixteen ? 0xD9 : 0xDA);
  at = *n;
  pass_time((u32)(bus * 4));
  after = *n;
  res(sixteen ? "ctl.exit16" : "ctl.exit8", after == at + 1 ? R_PASS : R_FAIL,
      "%s: %u interrupts after it (1 expected)", sixteen ? "D9h" : "DAh",
      after - at);
  quiet();
}

// a single-cycle command while its kind auto-initializes: one last block
// of its length
static void check_queue(int sixteen) {
  struct xfer x;
  volatile u16 *n = sixteen ? &n16 : &n8;
  double bus = block_frames(22050, 0) * 1e6 / sbt_v_hz(sbt_rate_to_v(22050));
  u16 at, after, len = (u16)(block_frames(22050, 0) / 2 - 1);

  ctl_start(&x, sixteen);
  wait_n(n, 2, (u32)(bus * 6) + 100000UL);
  cmd(4, sixteen ? 0xB0 : 0xC0, sixteen ? 0x10 : 0x00, len & 0xFF, len >> 8);
  at = *n;
  pass_time((u32)(bus * 4));
  after = *n;
  res(sixteen ? "ctl.queue16" : "ctl.queue8", after == at + 2 ? R_PASS : R_FAIL,
      "%s while auto-initialized: %u interrupts after it (2 expected)",
      sixteen ? "B0h" : "C0h", after - at);
  quiet();
}

// the DMA count a game reads moves through the buffer
static void check_count(int sixteen) {
  struct xfer x;
  volatile u16 *n = sixteen ? &n16 : &n8;
  double bus = block_frames(22050, 0) * 1e6 / sbt_v_hz(sbt_rate_to_v(22050));
  u16 c[6], i, moves = 0;

  ctl_start(&x, sixteen);
  wait_n(n, 1, (u32)(bus * 4) + 100000UL);
  for (i = 0; i < 6; i++) {
    c[i] = dma_left(x.ch);
    if (i && c[i] != c[i - 1])
      moves++;
    pass_time((u32)(bus / 3));
  }
  res(sixteen ? "ctl.count16" : "ctl.count8", moves >= 4 ? R_PASS : R_FAIL,
      "channel %u: %04X %04X %04X %04X %04X %04X", x.ch, c[0], c[1], c[2], c[3],
      c[4], c[5]);
  quiet();
}

// the handler doesn't acknowledge: the next block waits for it
static void check_stall(void) {
  struct xfer x;
  double bus = block_frames(22050, 0) * 1e6 / sbt_v_hz(sbt_rate_to_v(22050));
  u16 c1, c2, k;

  ack_off = 1;
  ctl_start(&x, 0);
  wait_n(&n8, 1, (u32)(bus * 4) + 100000UL);
  pass_time((u32)(bus * 2));
  c1 = dma_left(x.ch);
  pass_time((u32)bus);
  c2 = dma_left(x.ch);
  k = n8;
  ack_off = 0;
  H->inb(B + 0x0E);
  wait_n(&n8, (u16)(k + 2), (u32)(bus * 6) + 100000UL);
  res("ctl.unacknowledged", c1 == c2 ? R_PASS : fail_or_known(1),
      "the count with the interrupt pending: %04X then %04X, %u more after "
      "2xE",
      c1, c2, n8 - k);
  quiet();
}

static void check_control(void) {
  section("control", "pause, continue, the end of a transfer");
  check_pause(0);
  check_exit(0);
  check_queue(0);
  check_count(0);
  if (C->dma16) {
    check_pause(1);
    check_exit(1);
    check_queue(1);
    check_count(1);
  }
  check_stall();
}

// --- the checks: two transfers at once ---------------------------------------

static void dual_start(struct xfer *a, struct xfer *b, int rec8) {
  dsp_reset(NULL);
  rate41(44100);
  x_init(a, rec8 ? 0xCE : 0xC6, 0x00, 0, 44100);
  a->rec = (u8)rec8;
  if (rec8)
    fill_byte(a->addr, (u16)(a->frames * a->nb), 0x5A);
  else
    x_tone(a, 44100);
  x_init(b, 0xB6, 0x30, F16 | FST | FSG, 44100);
  b->frames = a->frames; // the same blocks: one rate generator
  x_tone(b, 44100);
  counters();
  x_start(a);
  x_start(b);
}

static void check_dual(void) {
  struct xfer a, b;
  double bus = block_frames(44100, 0) * 1e6 / sbt_v_hz(sbt_rate_to_v(44100));
  u16 k8, k16, i, j, changed;
  u8 buf[64];
  int ok;

  if (!C->dma16)
    return;
  section("dual", "8-bit and 16-bit transfers at once");
  dual_start(&a, &b, 0);
  wait_n(&n16, blocks(), (u32)(bus * blocks() * 3) + 200000UL);
  pass_time((u32)bus);
  chip_now("dual.chip");
  k8 = n8;
  k16 = n16;
  res("dual.play", k8 >= blocks() && k16 >= blocks() ? R_PASS : R_FAIL,
      "C6h and B6h: %u 8-bit and %u 16-bit interrupts", k8, k16);
  // D9h ends the 16-bit transfer, the 8-bit one plays on
  cmd(1, 0xD9);
  pass_time((u32)(bus * 3));
  k8 = n8;
  k16 = n16;
  pass_time((u32)(bus * 3));
  ok = n16 == k16 && n8 >= k8 + 2;
  res("dual.end16", ok ? R_PASS : R_FAIL,
      "after D9h: %u more 16-bit and %u more 8-bit interrupts", n16 - k16,
      n8 - k8);
  // a reset ends both
  dsp_reset(NULL);
  k8 = n8;
  k16 = n16;
  pass_time((u32)(bus * 3));
  res("dual.reset", n8 == k8 && n16 == k16 ? R_PASS : R_FAIL,
      "after a reset: %u 8-bit and %u 16-bit interrupts", n8 - k8, n16 - k16);
  quiet();
  // full duplex: 16-bit playback while an 8-bit recording runs
  dual_start(&a, &b, 1);
  wait_n(&n8, blocks(), (u32)(bus * blocks() * 3) + 200000UL);
  wait_n(&n16, blocks(), (u32)(bus * 3) + 100000UL);
  k8 = n8;
  k16 = n16;
  changed = 0;
  for (i = 0; i < a.frames; i = (u16)(i + 64)) {
    H->peek(a.addr + i, buf, 64);
    for (j = 0; j < 64; j++)
      if (buf[j] != 0x5A)
        changed++;
  }
  res("dual.duplex",
      k8 >= blocks() && k16 >= blocks() && changed ? R_PASS : R_FAIL,
      "B6h and CEh: %u 16-bit and %u 8-bit interrupts, %u bytes recorded", k16,
      k8, changed);
  quiet();
}

// --- the checks: recording ---------------------------------------------------

struct stats {
  int lo, hi;
  double mean, rms;
  u16 changed, n;
};

// what a recording put into the buffer, filled with 5Ah before
static void rec_stats(u32 addr, u16 n, u8 fmt, struct stats *s) {
  static u8 buf[128];
  u16 i, k, j, step = (fmt & F16) ? 2 : 1;
  double sum = 0, sq = 0;
  int v;

  s->lo = 32767;
  s->hi = -32768;
  s->changed = 0;
  s->n = 0;
  for (i = 0; i < n; i = (u16)(i + k)) {
    k = (u16)(n - i) > (u16)sizeof(buf) ? (u16)sizeof(buf) : (u16)(n - i);
    H->peek(addr + i, buf, k);
    for (j = 0; j + step <= k; j = (u16)(j + step)) {
      if (buf[j] != 0x5A || (step == 2 && buf[j + 1] != 0x5A))
        s->changed++;
      if (step == 2)
        v = (s16)(buf[j] | (buf[j + 1] << 8));
      else
        v = (int)buf[j] - 128;
      if (v < s->lo)
        s->lo = v;
      if (v > s->hi)
        s->hi = v;
      sum += v;
      sq += (double)v * v;
      s->n++;
    }
  }
  s->mean = s->n ? sum / s->n : 0;
  s->rms = s->n ? sqrt(sq / s->n - s->mean * s->mean) : 0;
}

static void record(const char *id, u8 c, u8 mode, u8 fmt, u16 hz, u8 tc) {
  struct xfer x;
  struct meas m;
  struct stats s;
  double rate, e;
  u16 want;
  int ok;

  dsp_reset(NULL);
  cur_v = 0x38;
  if (hz)
    rate41(hz);
  else
    rate40(tc);
  rate = sbt_v_hz(cur_v);
  x_init(&x, c, mode, fmt, rate);
  x.rec = 1;
  fill_byte(x.addr, (u16)(x.frames * frame_bytes(fmt) * x.nb), 0x5A);
  if (!generic(c)) {
    u16 blen = (u16)(x.frames * frame_bytes(fmt) - 1);
    cmd(3, 0x48, blen & 0xFF, blen >> 8);
  }
  want = (c == 0x24 || c == 0xB8 || c == 0xC8) ? 1 : blocks();
  x_run(&x, want, &m);
  rec_stats(x.addr, (u16)(x.frames * frame_bytes(fmt)), fmt, &s);
  e = err_of(m.hz, rate);
  ok = m.started && m.ints >= want && m.moving && s.changed > 0 &&
       (want < 3 || fabs(e) <= TOL);
  res(id, ok ? R_PASS : R_FAIL,
      "ints=%u/%u rate=%.0f sb16=%.0f err=%+.2f%% dma=%s written=%u/%u "
      "min=%d max=%d mean=%.1f rms=%.1f",
      m.ints, want, m.hz, rate, e * 100, m.moving ? "moving" : "still",
      s.changed, s.n, s.lo, s.hi, s.mean, s.rms);
  quiet();
}

static void check_record(void) {
  section("record", "recording (whatever the input has)");
  record("rec.24h", 0x24, 0, 0, 0, 0xD3);
  record("rec.2ch", 0x2C, 0, 0, 0, 0xD3);
  record("rec.c8h", 0xC8, 0x00, 0, 22050, 0);
  record("rec.ceh_mono", 0xCE, 0x00, 0, 22050, 0);
  record("rec.ceh_stereo", 0xCE, 0x20, FST, 22050, 0);
  record("rec.beh_mono", 0xBE, 0x10, F16 | FSG, 22050, 0);
  record("rec.beh_stereo", 0xBE, 0x30, F16 | FST | FSG, 22050, 0);
}

// --- the checks: the ES1869 hears itself (record source 7) -------------------

// the power of the frequency f (cycles a sample) in n samples, every
// step-th from start, of 8-bit unsigned data at addr
static double goertzel8(u32 addr, u16 n, u16 step, u16 start, double f) {
  static u8 buf[128];
  double w = 2 * cos(2 * PI * f), s1 = 0, s2 = 0, s0;
  u16 i, k, j, idx = 0;

  for (i = 0; i < n * step; i = (u16)(i + k)) {
    k = (u16)(n * step - i);
    if (k > (u16)sizeof(buf))
      k = (u16)sizeof(buf);
    H->peek(addr + i, buf, k);
    for (j = 0; j < k; j++, idx++) {
      if (idx % step != start)
        continue;
      s0 = ((int)buf[j] - 128) + w * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
  }
  return s1 * s1 + s2 * s2 - w * s1 * s2;
}

// a level in dB of full scale, of a tone whose Goertzel power is p
static double db_of(double p, u16 n) {
  double amp = 2 * sqrt(p > 0 ? p : 1e-9) / n;

  return 20 * log10(amp / 128 + 1e-9);
}

// the strongest frequency near f, and its level in *db
static double peak_near(u32 addr, u16 n, u16 start, double f, double *db) {
  double best = 0, bf = f, p, x;
  int i;

  for (i = -20; i <= 20; i++) {
    x = f * (1 + i * 0.005);
    p = goertzel8(addr, n, 2, start, x);
    if (p > best) {
      best = p;
      bf = x;
    }
  }
  *db = db_of(best, n);
  return bf;
}

static void loop_side(const char *id, int right) {
  struct xfer a, b;
  struct tone t;
  double rate = sbt_v_hz(sbt_rate_to_v(22050)), f, dl, dr, fl, fr;
  u16 n;
  int ok;

  dsp_reset(NULL);
  rate41(22050);
  x_init(&b, 0xB6, 0x30, F16 | FST | FSG, rate);
  f = fit_hz(1000, (u16)(b.frames * b.nb), rate);
  tone_set(&t, right ? 0 : f, right ? f : 0, rate, 16000);
  fill(b.addr, (u16)(b.frames * b.nb), b.fmt, &t);
  x_init(&a, 0xCE, 0x20, FST, rate);
  a.rec = 1;
  a.frames = b.frames;
  fill_byte(a.addr, (u16)(a.frames * 2 * a.nb), 0x80);
  counters();
  x_start(&b);
  pass_time((u32)(b.frames * 1e6 / rate));
  x_start(&a);
  wait_n(&n8, 3, (u32)(a.frames * 6e6 / rate) + 200000UL);
  dma_mask(a.ch);
  n = a.frames;
  fl = peak_near(a.addr, n, 0, f / rate, &dl) * rate;
  fr = peak_near(a.addr, n, 1, f / rate, &dr) * rate;
  ok = right ? dr > -40 && dr > dl + 15 : dl > -40 && dl > dr + 15;
  res(id, ok ? R_PASS : R_FAIL,
      "%.0f Hz on the %s: left %.1f dB at %.0f Hz, right %.1f dB at %.0f Hz", f,
      right ? "right" : "left", dl, fl, dr, fr);
  quiet();
}

static void check_loop(void) {
  if (!front || !audio2)
    return;
  section("loop", "the ES1869 records what Audio 2 plays");
  mix_set(0x1C, (u8)((saved_1c & 0xF8) | 0x07));
  mix_set(0x32, 0xF8);
  mix_set(0x33, 0xF8);
  loop_side("loop.left", 0);
  loop_side("loop.right", 1);
  mix_set(0x1C, saved_1c);
  mix_set(0x32, saved[2]);
  mix_set(0x33, saved[3]);
}

// --- the checks: ADPCM, silence, the direct DAC, MIDI ------------------------

// a block of one of the older commands, and when its interrupt came: on
// the ES1869, the firmware's FIFO takes up to 64 bytes ahead (DS p.13),
// codes bytes' worth of samples
static void one_block(const char *id, u8 c, u16 len, double samples, u8 setlen,
                      u8 codes) {
  double rate, want_us, early;
  u32 t0, got_us = 0;
  int ok;

  dsp_reset(NULL);
  rate40(0xA5);
  rate = sbt_v_hz(cur_v);
  fill_byte(BUF8, (u16)(len + 2), 0x88);
  dma_set(C->dma8, BUF8, (u16)(len + 2), DMA_PLAY);
  counters();
  if (setlen)
    cmd(3, 0x48, len & 0xFF, len >> 8);
  t0 = H->now();
  if (setlen)
    cmd(1, c);
  else
    cmd(3, c, len & 0xFF, len >> 8);
  want_us = samples * 1e6 / rate;
  early = front ? 64.0 * codes * 1e6 / rate : 0;
  ok = wait_n(&n8, 1, (u32)(want_us * 3) + 100000UL);
  if (nlog)
    got_us = us_of(tlog[0] - t0);
  ok = ok && got_us <= want_us * 1.1 && got_us >= (want_us - early) * 0.9;
  res(id, ok ? R_PASS : R_FAIL,
      "%u 8-bit interrupts, the first after %luus (%.0fus expected, %.0fus "
      "less on the ES1869)",
      n8, (unsigned long)got_us, want_us, early);
  quiet();
}

static void check_misc(void) {
  u16 len = (O & SBT_O_SHORT) ? 200 : 400;
  u32 t0;
  u16 i;
  int v, ok;

  section("misc", "ADPCM, silence, the direct DAC and ADC, MIDI");
  // 4-bit ADPCM: the reference byte, then two codes a byte
  one_block("x.adpcm4_75h", 0x75, len, 1 + 2.0 * len, 0, 2);
  one_block("x.adpcm3_77h", 0x77, len, 1 + 3.0 * len, 0, 3);
  one_block("x.adpcm2_17h", 0x17, len, 1 + 4.0 * len, 0, 4);
  one_block("x.adpcm4_74h", 0x74, len, 2.0 * (len + 1), 0, 2);
  one_block("x.silence_80h", 0x80, len, len + 1.0, 0, 0);
  dsp_reset(NULL);
  t0 = H->now();
  ok = 1;
  for (i = 0; i < 64 && ok; i++)
    ok = cmd(2, 0x10, (i & 8) ? 0xA0 : 0x60) == 0;
  res("x.direct_dac", ok ? R_PASS : R_FAIL, "64 10h commands, %luus each",
      (unsigned long)(us_of(H->now() - t0) / 64));
  cmd(1, 0x20);
  v = dsp_in(20000);
  res("x.direct_adc", v >= 0 ? R_PASS : R_FAIL, "20h: %02X", v & 0xFF);
  ok = cmd(2, 0x38, 0x90) == 0 && cmd(2, 0x38, 0x3C) == 0 &&
       cmd(2, 0x38, 0x00) == 0;
  res("x.midi_38h", ok ? R_PASS : R_FAIL, "38h: three bytes out");
  quiet();
}

// --- the checks: FM and the MPU-401 ------------------------------------------

static void opl(u16 port, u8 reg, u8 v) {
  int i;

  H->outb(port, reg);
  for (i = 0; i < 6; i++)
    H->inb(port);
  H->outb((u16)(port + 1), v);
  for (i = 0; i < 35; i++)
    H->inb(port);
}

// AdLib's detection: the timers' status bits
static int opl_found(u16 port, u8 *s1, u8 *s2) {
  opl(port, 0x04, 0x60);
  opl(port, 0x04, 0x80);
  *s1 = H->inb(port);
  opl(port, 0x02, 0xFF);
  opl(port, 0x04, 0x21);
  pass_time(200);
  *s2 = H->inb(port);
  opl(port, 0x04, 0x60);
  opl(port, 0x04, 0x80);
  return (*s1 & 0xE0) == 0x00 && (*s2 & 0xE0) == 0xC0;
}

static u16 fm_port;

static void check_fm_mpu(void) {
  static const u16 ports[] = {0x388, 0, 8};
  u8 s1, s2;
  u16 i, port;
  u32 t0;
  int v;

  section("fm", "FM and the MPU-401");
  for (i = 0; i < 3; i++) {
    port = (u16)(i == 0 ? 0x388 : B + ports[i]);
    v = opl_found(port, &s1, &s2);
    if (v && !fm_port)
      fm_port = port;
    res(i == 0   ? "fm.388h"
        : i == 1 ? "fm.base"
                 : "fm.base8",
        v ? R_PASS : R_INFO, "%Xh: status %02X then %02X: %s", port, s1, s2,
        v ? ((s1 & 0x06) ? "OPL2" : "OPL3") : "none");
  }
  if (!C->mpu)
    return;
  H->outb((u16)(C->mpu + 1), 0xFF);
  t0 = H->now();
  v = -1;
  while (H->now() - t0 < tk(100000L)) {
    if (!(H->inb((u16)(C->mpu + 1)) & 0x80)) {
      v = H->inb(C->mpu);
      if (v == 0xFE)
        break;
    }
    H->idle();
  }
  res("mpu.reset", v == 0xFE ? R_PASS : R_INFO, "%Xh reset: %s", C->mpu,
      v == 0xFE ? "FEh" : "no answer");
  if (v == 0xFE) {
    H->outb((u16)(C->mpu + 1), 0x3F);
    t0 = H->now();
    v = -1;
    while (H->now() - t0 < tk(100000L)) {
      if (!(H->inb((u16)(C->mpu + 1)) & 0x80)) {
        v = H->inb(C->mpu);
        break;
      }
      H->idle();
    }
    H->outb((u16)(C->mpu + 1), 0xFF);
    res("mpu.uart", v == 0xFE ? R_PASS : R_FAIL, "3Fh: %02X", v & 0xFF);
  }
}

// --- the checks: time --------------------------------------------------------

static void port_time(const char *id, u16 port) {
  u32 t0;
  u16 i, n = 100;

  t0 = H->now();
  for (i = 0; i < n; i++)
    H->inb(port);
  res(id, R_INFO, "%Xh: %.1fus a read", port, us_of(H->now() - t0) / (double)n);
}

static u16 blk_left;
static u8 blk_cmd[4];

// the next single-cycle block from the handler, as games start them; no
// varargs here: the handler may run on another stack than the program's
static void next_block(u8 kinds) {
  if ((kinds & 1) && blk_left) {
    blk_left--;
    dsp_out(blk_cmd[0]);
    dsp_out(blk_cmd[1]);
    dsp_out(blk_cmd[2]);
    dsp_out(blk_cmd[3]);
  }
}

static void check_time(void) {
  struct xfer x;
  u32 t0, us;
  u16 i, nb = (O & SBT_O_SHORT) ? 4 : 16;
  double rate, ideal;

  section("time", "how long the card takes");
  port_time("time.2xc", (u16)(B + 0x0C));
  port_time("time.2xe", (u16)(B + 0x0E));
  port_time("time.mixer", (u16)(B + 0x05));
  port_time("time.2x8", (u16)(B + 0x08));
  dsp_reset(NULL);
  rate41(22050);
  rate = sbt_v_hz(cur_v);
  x_init(&x, 0xC0, 0x00, 0, rate);
  x_tone(&x, rate);
  dma_set(x.ch, x.addr, (u16)(x.frames * x.nb), DMA_PLAY);
  t0 = H->now();
  for (i = 0; i < 4; i++) {
    cmd(4, 0xC6, 0x00, (x.frames - 1) & 0xFF, (x.frames - 1) >> 8);
    cmd(1, 0xDA);
  }
  us = us_of(H->now() - t0) / 4;
  res("time.start", R_INFO, "C6h and its operands, with DAh: %luus",
      (unsigned long)us);
  quiet();
  // blocks one after the other, each started from the handler
  rate41(22050);
  dma_set(x.ch, x.addr, (u16)(x.frames * x.nb), DMA_PLAY);
  blk_cmd[0] = 0xC0;
  blk_cmd[1] = 0x00;
  blk_cmd[2] = (u8)((x.frames - 1) & 0xFF);
  blk_cmd[3] = (u8)((x.frames - 1) >> 8);
  blk_left = (u16)(nb - 1);
  counters();
  hook = next_block;
  t0 = H->now();
  cmd(4, 0xC0, 0x00, blk_cmd[2], blk_cmd[3]);
  wait_n(&n8, nb, (u32)(x.frames * 1e6 / rate * nb * 3) + 200000UL);
  us = us_of(H->now() - t0);
  hook = NULL;
  ideal = x.frames * 1e6 / rate * nb;
  res("time.blocks", n8 >= nb ? R_PASS : R_FAIL,
      "%u single-cycle blocks of %u: %luus, %.0fus a block more than the "
      "sound",
      n8, x.frames, (unsigned long)us, (us - ideal) / nb);
  quiet();
}

// --- the listening checks ----------------------------------------------------

static u32 sound_rate;

static void sound_on(double rate) {
  sound_rate = (u32)(rate + 0.5);
  if (H->sound != NULL)
    H->sound(sound_rate);
}

static void sound_off(void) {
  if (H->sound != NULL)
    H->sound(0);
}

// an even number of frames near most (or 256 short) that holds whole
// cycles of hz at rate, exactly when rate is a multiple of hz
static u16 whole_cycles(double hz, double rate, u16 most) {
  double len = rate / hz, cycles;
  u16 f;

  if (O & SBT_O_SHORT)
    most = 256;
  cycles = floor(most / len);
  if (cycles < 1)
    cycles = 1;
  f = (u16)floor(cycles * len + 0.5);
  return (u16)(f & ~1U);
}

// a sound of the format at rate for us: hz on the left (or mono), hz_r on
// the right, through the command
static void play_tone(u8 c, u8 mode, u8 fmt, u16 rate_hz, double hz,
                      double hz_r, u32 us) {
  struct xfer x;
  struct tone t;
  double rate;
  u16 frames;

  dsp_reset(NULL);
  rate41(rate_hz);
  rate = sbt_v_hz(cur_v);
  x_init(&x, c, mode, fmt, rate);
  frames = whole_cycles(hz, rate, (fmt & F16) && (fmt & FST) ? 2048 : 4096);
  x.frames = (u16)(frames / 2);
  tone_set(&t, fit_hz(hz, frames, rate), hz_r ? fit_hz(hz_r, frames, rate) : 0,
           rate, 12000);
  fill(x.addr, frames, fmt, &t);
  sound_on(rate);
  x_start(&x);
  pass_time(us);
  sound_off();
  quiet();
}

static void s_tone8(void) {
  play_tone(0xC6, 0x00, 0, 22050, 440, 0, sound_us());
}

static void s_left16(void) {
  play_tone(0xB6, 0x30, F16 | FST | FSG, 44100, 440, 0, sound_us());
}

static void s_right16(void) {
  struct xfer x;
  struct tone t;
  double rate;
  u16 frames = (O & SBT_O_SHORT) ? 256 : 2048;

  dsp_reset(NULL);
  rate41(44100);
  rate = sbt_v_hz(cur_v);
  x_init(&x, 0xB6, 0x30, F16 | FST | FSG, rate);
  x.frames = (u16)(frames / 2);
  tone_set(&t, 0, fit_hz(440, frames, rate), rate, 12000);
  fill(x.addr, frames, x.fmt, &t);
  sound_on(rate);
  x_start(&x);
  pass_time(sound_us());
  sound_off();
  quiet();
}

static void s_left8(void) {
  play_tone(0xC6, 0x20, FST, 22050, 440, 0, sound_us());
}

static void s_right8(void) {
  struct xfer x;
  struct tone t;
  double rate;
  u16 frames = (O & SBT_O_SHORT) ? 256 : 4096;

  dsp_reset(NULL);
  rate41(22050);
  rate = sbt_v_hz(cur_v);
  x_init(&x, 0xC6, 0x20, FST, rate);
  x.frames = (u16)(frames / 2);
  tone_set(&t, 0, fit_hz(440, frames, rate), rate, 12000);
  fill(x.addr, frames, x.fmt, &t);
  sound_on(rate);
  x_start(&x);
  pass_time(sound_us());
  sound_off();
  quiet();
}

// 441 Hz: a whole number of frames a cycle at the three rates
static void s_pitch(void) {
  play_tone(0xC6, 0x00, 0, 11025, 441, 0, sound_us() / 2);
  pass_time(sound_us() / 8);
  play_tone(0xC6, 0x00, 0, 22050, 441, 0, sound_us() / 2);
  pass_time(sound_us() / 8);
  play_tone(0xB6, 0x10, F16 | FSG, 44100, 441, 0, sound_us() / 2);
}

static void s_dual(void) {
  struct xfer a, b;
  struct tone t;
  double rate;
  u16 frames = (O & SBT_O_SHORT) ? 256 : 4096;

  dsp_reset(NULL);
  rate41(22050);
  rate = sbt_v_hz(cur_v);
  x_init(&a, 0xC6, 0x00, 0, rate);
  a.frames = (u16)(frames / 2);
  tone_set(&t, fit_hz(330, frames, rate), 0, rate, 12000);
  fill(a.addr, frames, a.fmt, &t);
  x_init(&b, 0xB6, 0x10, F16 | FSG, rate);
  b.frames = (u16)(frames / 2);
  tone_set(&t, fit_hz(880, frames, rate), 0, rate, 12000);
  fill(b.addr, frames, b.fmt, &t);
  sound_on(rate);
  x_start(&a);
  x_start(&b);
  pass_time(sound_us());
  sound_off();
  quiet();
}

static void s_pause(void) {
  struct xfer x;
  struct tone t;
  double rate;
  u16 frames = (O & SBT_O_SHORT) ? 256 : 4096;

  dsp_reset(NULL);
  rate41(22050);
  rate = sbt_v_hz(cur_v);
  x_init(&x, 0xB6, 0x10, F16 | FSG, rate);
  x.frames = (u16)(frames / 2);
  tone_set(&t, fit_hz(440, frames, rate), 0, rate, 12000);
  fill(x.addr, frames, x.fmt, &t);
  sound_on(rate);
  x_start(&x);
  pass_time(sound_us() / 3);
  cmd(1, 0xD5);
  pass_time(sound_us() / 3);
  cmd(1, 0xD6);
  pass_time(sound_us() / 3);
  sound_off();
  quiet();
}

static void s_volume(void) {
  static const u8 steps[] = {0xF8, 0xB0, 0x70, 0x30};
  struct xfer x;
  struct tone t;
  double rate;
  u16 frames = (O & SBT_O_SHORT) ? 256 : 4096, i;

  dsp_reset(NULL);
  rate41(22050);
  rate = sbt_v_hz(cur_v);
  x_init(&x, 0xC6, 0x00, 0, rate);
  x.frames = (u16)(frames / 2);
  tone_set(&t, fit_hz(440, frames, rate), 0, rate, 12000);
  fill(x.addr, frames, x.fmt, &t);
  sound_on(rate);
  x_start(&x);
  for (i = 0; i < 4; i++) {
    mix_set(0x30, steps[i]);
    mix_set(0x31, steps[i]);
    pass_time(sound_us() / 4);
  }
  sound_off();
  quiet();
  mix_set(0x30, saved[0]);
  mix_set(0x31, saved[1]);
}

static void s_balance(void) {
  struct xfer x;
  struct tone t;
  double rate;
  u16 frames = (O & SBT_O_SHORT) ? 256 : 4096;

  dsp_reset(NULL);
  rate41(22050);
  rate = sbt_v_hz(cur_v);
  x_init(&x, 0xB6, 0x10, F16 | FSG, rate);
  x.frames = (u16)(frames / 2);
  tone_set(&t, fit_hz(440, frames, rate), 0, rate, 12000);
  fill(x.addr, frames, x.fmt, &t);
  sound_on(rate);
  mix_set(0x32, 0xF8);
  mix_set(0x33, 0x00);
  x_start(&x);
  pass_time(sound_us() / 2);
  mix_set(0x32, 0x00);
  mix_set(0x33, 0xF8);
  pass_time(sound_us() / 2);
  sound_off();
  quiet();
  mix_set(0x32, saved[2]);
  mix_set(0x33, saved[3]);
}

// Creative's 4-bit ADPCM (the decoder's tables, as DOSBox-X has them)
static const s8 a4_scale[64] = {
    0, 1,  2,  3,  4,  5,  6,  7,  0,  -1,  -2,  -3,  -4,  -5,  -6,  -7,
    1, 3,  5,  7,  9,  11, 13, 15, -1, -3,  -5,  -7,  -9,  -11, -13, -15,
    2, 6,  10, 14, 18, 22, 26, 30, -2, -6,  -10, -14, -18, -22, -26, -30,
    4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28, -36, -44, -52, -60};
static const u8 a4_adjust[64] = {
    0,   0, 0, 0, 0, 16, 16, 16, 0,   0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16, 240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16, 240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 0,  0,  0,  240, 0, 0, 0, 0, 0,  0,  0};

// the code whose decoded sample comes nearest to want, and the decoder's
// state after it
static u8 a4_code(int want, int *ref, u8 *step) {
  int best = 0, bd = 1000, s, r, d, c;

  for (c = 0; c < 16; c++) {
    s = c + *step;
    if (s > 63)
      s = 63;
    r = *ref + a4_scale[s];
    if (r < 0)
      r = 0;
    if (r > 255)
      r = 255;
    d = r > want ? r - want : want - r;
    if (d < bd) {
      bd = d;
      best = c;
    }
  }
  s = best + *step;
  if (s > 63)
    s = 63;
  r = *ref + a4_scale[s];
  *ref = r < 0 ? 0 : r > 255 ? 255 : r;
  *step = (u8)(*step + a4_adjust[s]);
  return (u8)best;
}

static void s_adpcm(void) {
  static u8 buf[129];
  struct tone t;
  double rate;
  u16 bytes = (O & SBT_O_SHORT) ? 128 : 8192, i, k, n;
  int ref = 128;
  u8 step = 0, hi;
  u32 addr = BUF8;

  dsp_reset(NULL);
  rate40(0xD3);
  rate = sbt_v_hz(cur_v);
  tone_set(&t, fit_hz(440, (u16)(bytes * 2), rate), 0, rate, 12000);
  // the reference byte, then two codes a byte
  buf[0] = 128;
  H->poke(addr, buf, 1);
  addr++;
  for (i = 0; i < bytes; i = (u16)(i + n)) {
    n = bytes - i > 128 ? 128 : (u16)(bytes - i);
    for (k = 0; k < n; k++) {
      hi = a4_code(128 + (tone_next(&t, 0) >> 8), &ref, &step);
      buf[k] =
          (u8)((hi << 4) | a4_code(128 + (tone_next(&t, 0) >> 8), &ref, &step));
    }
    H->poke(addr, buf, n);
    addr += n;
  }
  dma_set(C->dma8, BUF8, (u16)(bytes + 1), DMA_PLAY);
  sound_on(rate);
  cmd(3, 0x75, bytes & 0xFF, bytes >> 8);
  pass_time((u32)((bytes * 2.0 + 1) * 1e6 / rate) + 20000UL);
  sound_off();
  quiet();
}

static void s_direct(void) {
  u32 t0 = H->now(), next = t0, per = tk(125), end = tk(sound_us());
  u16 i = 0;

  dsp_reset(NULL);
  cmd(1, 0xD1);
  sound_on(8000);
  while (H->now() - t0 < end && !escaped()) {
    if ((s32)(H->now() - next) >= 0) {
      cmd(2, 0x10, (u8)(128 + (sine_tab[(u16)((i * 56UL) & (TAB - 1))] >> 9)));
      i++;
      next += per;
    }
    H->idle();
  }
  sound_off();
  quiet();
}

static void s_speaker(void) {
  dsp_reset(NULL);
  cmd(1, 0xD3);
  play_tone(0xC6, 0x00, 0, 22050, 440, 0, sound_us());
}

static void s_fm(void) {
  u16 p = fm_port;

  sound_on(49716);    // the FM synthesizer's rate
  opl(p, 0x20, 0x01); // an organ: modulator and carrier
  opl(p, 0x23, 0x01);
  opl(p, 0x40, 0x10);
  opl(p, 0x43, 0x00);
  opl(p, 0x60, 0xF0);
  opl(p, 0x63, 0xF0);
  opl(p, 0x80, 0x77);
  opl(p, 0x83, 0x77);
  opl(p, 0xC0, 0x30 | 0x02);
  opl(p, 0xA0, 0x41); // 440 Hz: F-number 241h, block 4
  opl(p, 0xB0, 0x32);
  pass_time(sound_us());
  opl(p, 0xB0, 0x12);
  sound_off();
}

static void s_blocks(void) {
  struct xfer x;
  struct tone t;
  double rate;
  u16 frames = (O & SBT_O_SHORT) ? 256 : 4096;

  dsp_reset(NULL);
  rate40(0xD3);
  rate = sbt_v_hz(cur_v);
  x_init(&x, 0x14, 0, 0, rate);
  x.frames = (u16)(frames / 4);
  x.nb = 4;
  tone_set(&t, fit_hz(440, frames, rate), 0, rate, 12000);
  fill(x.addr, frames, 0, &t);
  dma_set(x.ch, x.addr, frames, DMA_PLAY);
  blk_cmd[0] = 0x14;
  blk_cmd[1] = (u8)((x.frames - 1) & 0xFF);
  blk_cmd[2] = (u8)((x.frames - 1) >> 8);
  blk_left = 0xFFFF;
  hook = NULL;
  sound_on(rate);
  counters();
  {
    u32 t0 = H->now();
    u16 seen = 0;
    cmd(3, 0x14, blk_cmd[1], blk_cmd[2]);
    while (H->now() - t0 < tk(sound_us()) && !escaped()) {
      if (n8 != seen) {
        seen = n8;
        cmd(3, 0x14, blk_cmd[1], blk_cmd[2]);
      }
      H->idle();
    }
  }
  sound_off();
  quiet();
}

static void s_record(void) {
  struct xfer x;
  double rate;
  u16 frames = (O & SBT_O_SHORT) ? 256 : 0x3F00;
  u8 m3d = mix_get(0x3D), m3e = mix_get(0x3E), m3a = mix_get(0x3A);

  dsp_reset(NULL);
  rate41(11025);
  rate = sbt_v_hz(cur_v);
  mix_set(0x3D, 0x01); // the mic into both sides
  mix_set(0x3E, 0x01);
  mix_set(0x3A, 0xF8);
  x_init(&x, 0xCE, 0x00, 0, rate);
  x.frames = (u16)(frames / 2);
  x.rec = 1;
  fill_byte(x.addr, frames, 0x80);
  say("speak into the microphone now");
  x_start(&x);
  pass_time((u32)(frames * 1e6 / rate));
  quiet();
  mix_set(0x3D, m3d);
  mix_set(0x3E, m3e);
  mix_set(0x3A, m3a);
  rate41(11025);
  x.cmd = 0xC6;
  x.rec = 0;
  sound_on(rate);
  x_start(&x);
  pass_time((u32)(frames * 1e6 / rate));
  sound_off();
  quiet();
}

struct listen {
  const char *id;
  void (*play)(void);
  const char *question;
  const char *keys; // the answers, then r (again) and s (skip)
  const char *want; // the answers that pass
  u8 needs;         // 1: the 16-bit channel, 2: FM, 4: a person to speak
};

static const char ANS_YN[] = "yn";
static const char ANS_SIDE[] = "lrbn";

static const struct listen listens[] = {
    {"l.tone8", s_tone8, "You should hear a steady tone. Did you?", "ynd", "y",
     0},
    {"l.left16", s_left16, "Where did the tone come from?", ANS_SIDE, "l", 1},
    {"l.right16", s_right16, "Where did the tone come from?", ANS_SIDE, "r", 1},
    {"l.left8", s_left8, "Where did the tone come from?", ANS_SIDE, "l", 0},
    {"l.right8", s_right8, "Where did the tone come from?", ANS_SIDE, "r", 0},
    {"l.pitch", s_pitch, "Three tones played. Did they have the same pitch?",
     ANS_YN, "y", 1},
    {"l.dual", s_dual, "What did you hear?", "blhn", "", 1},
    {"l.pause", s_pause, "Did the tone stop for a moment and then go on?",
     ANS_YN, "y", 1},
    {"l.volume", s_volume, "Did the tone get quieter in steps?", ANS_YN, "y",
     0},
    {"l.balance", s_balance, "Did the tone move from the left to the right?",
     ANS_YN, "y", 1},
    {"l.adpcm", s_adpcm, "Did you hear a tone? It may sound rough.", ANS_YN,
     "y", 0},
    {"l.direct", s_direct, "Did you hear a tone? It may buzz.", ANS_YN, "y", 0},
    {"l.speaker", s_speaker, "Did you hear a tone?", ANS_YN, "y", 0},
    {"l.blocks", s_blocks, "Did the tone play without clicks or gaps?", ANS_YN,
     "y", 0},
    {"l.fm", s_fm, "Did you hear an organ note?", ANS_YN, "y", 2},
    {"l.record", s_record,
     "Speak into the microphone while the first sound plays: did you hear "
     "your voice after it?",
     ANS_YN, "y", 4},
};

static const char *legend(const char *keys) {
  static char s[120];
  const char *k;

  s[0] = 0;
  for (k = keys; *k; k++) {
    switch (*k) {
    case 'y':
      strcat(s, " [y]es");
      break;
    case 'n':
      strcat(s, keys == ANS_SIDE ? " [n]owhere" : " [n]o");
      break;
    case 'd':
      strcat(s, " [d]istorted");
      break;
    case 'l':
      strcat(s, keys == ANS_SIDE ? " [l]eft" : " [l]ow only");
      break;
    case 'r':
      strcat(s, " [r]ight");
      break;
    case 'b':
      strcat(s, keys == ANS_SIDE ? " [b]oth" : " [b]oth, a low and a high one");
      break;
    case 'h':
      strcat(s, " [h]igh only");
      break;
    }
  }
  strcat(s, " a[g]ain [s]kip");
  return s;
}

static void check_listen(void) {
  static char keys[16], question[200];
  const struct listen *l;
  const char *want;
  u16 i;
  int a;

  section("listen", "the listening checks: turn the speakers up");
  for (i = 0; i < ESS_ARRAY_SIZE(listens) && !stop_now; i++) {
    l = &listens[i];
    if (((l->needs & 1) && !C->dma16) || ((l->needs & 2) && !fm_port)) {
      res(l->id, R_SKIP, "not on this card");
      continue;
    }
    want = l->want;
    if (l->play == s_dual)
      want = front && audio2 ? "b" : "h"; // a Sound Blaster 16 has one DAC
    strcpy(keys, l->keys);
    strcat(keys, "gs");
    sprintf(question, "%s%s", l->question, legend(l->keys));
    do {
      l->play();
      a = H->ask(l->id, question, keys);
    } while (a == 'g' && !stop_now);
    if (a == 's' || a == 0)
      res(l->id, R_SKIP, "skipped");
    else
      res(l->id, strchr(want, a) != NULL ? R_PASS : R_FAIL,
          "answer %c (%s expected): %s", a, want, l->question);
  }
}

// --- the run -----------------------------------------------------------------

// the ES1869 behind a front: the chip's own Extended mode answers C6h's
// register command, which a Sound Blaster 16 takes as a transfer's first
// operands and starts nothing
static void find_es1869(void) {
  int v;

  front = 0;
  v = ess_read(0xB1);
  dsp_reset(NULL);
  if (v >= 0)
    front = 1;
  res("card.es1869", R_INFO, "%s",
      front ? "the ES1869 is behind a front: its Extended mode answered"
            : "no ES1869 behind it: a Sound Blaster 16, or another card");
}

static void head(void) {
  const char *const *e;

  put("sb16test %s: a Sound Blaster 16 on real hardware", SBT_VERSION);
  put("Send this file back with a note of what you heard. Each line is a");
  put("check: PASS, FAIL, KNOWN (a difference the ES1869 has), INFO, SKIP.");
  section("env", "the machine");
  for (e = C->env; e != NULL && *e != NULL; e++)
    put("%s", *e);
  put("blaster A%X I%u D%u H%u P%X, clock %luHz, area %05lXh, options %04X",
      C->base, C->irq, C->dma8, C->dma16, C->mpu, (unsigned long)C->hz,
      (unsigned long)C->area, O);
}

static void summary(void) {
  u16 i;
  char *p;

  section("summary", "done");
  put("pass=%u fail=%u known=%u info=%u skip=%u%s", counts[R_PASS],
      counts[R_FAIL], counts[R_KNOWN], counts[R_INFO], counts[R_SKIP],
      stop_now ? " (stopped)" : "");
  if (nfailed) {
    p = text + sprintf(text, "failed:");
    for (i = 0; i < nfailed; i++)
      p += sprintf(p, " %s", failed[i]);
    H->line(text);
  }
}

int sbt_run(const struct sbt_hal *hal, const struct sbt_card *card,
            unsigned opts) {
  H = hal;
  C = card;
  O = opts;
  cur_section = NULL;
  B = card->base;
  dsp_lim = tk(20000);
  stop_now = 0;
  memset(counts, 0, sizeof(counts));
  nfailed = 0;
  front = audio2 = 0;
  cfg = 0;
  fm_port = 0;
  hook = NULL;
  ack_off = 0;
  cur_v = 0x38;
  tab_init();
  counters();
  head();
  if (!check_dsp()) {
    res("card", R_FAIL,
        "no Sound Blaster 16 answers at %Xh: is esssb16 "
        "loaded, or BLASTER wrong?",
        B);
    summary();
    H = NULL;
    return counts[R_FAIL];
  }
  find_es1869();
  check_dsp2();
  if (front) {
    section("card", "the ES1869 behind the front");
    chip_dump("before");
  }
  mix_save();
  if (!(O & SBT_O_LISTEN)) {
    check_e2();
    check_mixer();
    check_irq();
    check_play();
    check_play_on8();
    check_routing();
    check_rates();
    check_control();
    check_dual();
    check_record();
    check_loop();
    check_misc();
    check_fm_mpu();
    check_time();
  } else {
    check_routing();
    check_fm_mpu();
  }
  if (!(O & SBT_O_AUTO) && !stop_now)
    check_listen();
  if (front && !stop_now) {
    section("chip", "the ES1869 after the checks");
    chip_dump("after");
  }
  quiet();
  mix_restore();
  summary();
  H = NULL;
  return counts[R_FAIL];
}
