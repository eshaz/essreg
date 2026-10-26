/*
 * essprobe's cases, see essp.h.
 *
 * Notes:
 *
 * A case starts from the chip's own reset (Audio_Base+6 = 3, which clears
 * the FIFO too) and Extended mode, programs what its spec says, plays for
 * its time while the DMA counts are sampled, and ends with a reset, so
 * that nothing of it is left for the next. What the cases change outside
 * the chip's volatile registers goes to the restore list first (the
 * mixer, the audio device's PnP registers, the interrupt mask, the VGA
 * DAC), and comes back at the end. The DMA counts are read past the
 * memory manager where the platform has a window, else as a game reads
 * them. The rates the cases ask for are computed with the clocks the
 * first report measured, 793.8 and 396.9 kHz for A1h, 793.8 and 768 kHz
 * for 70h.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "essp.h"
#include "sbtsound.h"

#ifdef ESS_HOST
#define esp_vfmt vsnprintf
#else
#define esp_vfmt _vbprintf
#endif

#define F16 SND_F16
#define FST SND_FST
#define FSG SND_FSG

enum { R_PASS, R_FAIL, R_KNOWN, R_INFO, R_SKIP };
static const char *const rname[] = {"PASS", "FAIL", "KNOWN", "INFO", "SKIP"};

static const struct esp_hal *H;
static const struct esp_card *C;
static unsigned O;
static u16 B;      // Audio_Base
static u16 cfg;    // Config_Base, 0 if unknown
static u8 irq;     // the audio interrupt (PnP 70h)
static u8 dma1;    // Audio 1's channel (PnP 74h)
static u8 dma2;    // Audio 2's (PnP 75h), 4 for none
static u8 pins[4]; // the channels the card's DMA pins are wired to (23h, 24h)
static int window; // the platform has a window on the real ports
static int stop_now;

static u16 counts[5];
static char failed[12][24];
static u16 nfailed;
static char text[260];
static u16 sound_no; // the recorded sounds so far

static u32 rec_t0; // the clock as the start marker began

// the DMA area's parts, from C->area
#define G_A 0x0000UL  // guard, 4 KB
#define BUF1 0x1000UL // Audio 1's buffer, 16 KB
#define G_B 0x5000UL  // guard
#define BUF2 0x6000UL // Audio 2's, 32 KB (a 16-bit channel's words)
#define G_C 0xE000UL  // guard
#define SCR 0xF000UL  // what a transfer the wrong way round writes
#define GUARD 0x1000U
#define BUF1_MAX 0x4000U
#define BUF2_MAX 0x8000U

// the 8237's modes, the channel's bits 1:0 clear
#define DMA_SINGLE 0x40
#define DMA_DEMAND 0x00
#define DMA_AUTO 0x10
#define DMA_READ 0x08  // from memory: playback
#define DMA_WRITE 0x04 // into memory

static const u8 page_port[8] = {0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A};

// --- the report --------------------------------------------------------------

static void put(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  esp_vfmt(text, sizeof(text), fmt, ap);
  va_end(ap);
  H->line(text);
}

static void say(const char *s) {
  if (H->say != NULL)
    H->say(s);
}

static void section(const char *name, const char *what) {
  put("");
  put("[%s] %s", name, what);
  say(what);
}

static void res(const char *id, int r, const char *fmt, ...) {
  va_list ap;
  int n;

  n = sprintf(text, "%-20s %-5s ", id, rname[r]);
  va_start(ap, fmt);
  esp_vfmt(text + n, sizeof(text) - n, fmt, ap);
  va_end(ap);
  H->line(text);
  counts[r]++;
  if (r == R_FAIL && nfailed < ESS_ARRAY_SIZE(failed)) {
    strncpy(failed[nfailed], id, sizeof(failed[0]) - 1);
    failed[nfailed][sizeof(failed[0]) - 1] = 0;
    nfailed++;
  }
}

// --- time --------------------------------------------------------------------

static u32 tk(u32 us) { return (u32)((double)us * (double)C->hz / 1e6 + 0.5); }

static u32 us_of(u32 t) { return (u32)((double)t * 1e6 / (double)C->hz + 0.5); }

static u32 since(u32 t0) { return (H->now() - t0) & 0xFFFFFFFFUL; }

static int escaped(void) {
  if (!stop_now && H->esc != NULL && H->esc())
    stop_now = 1;
  return stop_now;
}

// time passes; 0 if the person stopped
static int pass_time(u32 us) {
  u32 t0 = H->now(), n = tk(us);

  while (since(t0) < n) {
    H->idle();
    if (escaped())
      return 0;
  }
  return 1;
}

// --- the chip ----------------------------------------------------------------

// a byte to the command port, once it takes one: 0, or -1 after 20 ms
static int dsp_out(u8 v) {
  u32 t0 = H->now(), lim = tk(20000);

  while (H->inb((u16)(B + 0x0C)) & 0x80) {
    if (since(t0) > lim)
      return -1;
    H->idle();
  }
  H->outb((u16)(B + 0x0C), v);
  return 0;
}

// a byte from the read port, once there is one, or -1
static int dsp_in(u32 us) {
  u32 t0 = H->now(), lim = tk(us);

  while (!(H->inb((u16)(B + 0x0E)) & 0x80)) {
    if (since(t0) > lim)
      return -1;
    H->idle();
  }
  return H->inb((u16)(B + 0x0A));
}

// the chip's reset: +6 = kind (3 Extended, the FIFO too; 1 Compatibility),
// a few microseconds, 0, then its AAh; 1 if it answered
static int chip_reset(u8 kind) {
  u32 t0;
  int i, v;

  H->outb((u16)(B + 6), kind);
  t0 = H->now();
  while (since(t0) < tk(10))
    H->idle();
  H->outb((u16)(B + 6), 0);
  for (i = 0; i < 4; i++) {
    v = dsp_in(20000);
    if (v == 0xAA)
      return 1;
    if (v < 0)
      break;
  }
  return 0;
}

// Audio 1's FIFO emptied (+6 bit 1, DS p.48)
static void fifo_reset(void) {
  H->outb((u16)(B + 6), 2);
  H->outb((u16)(B + 6), 0);
}

static int ext_on(void) { return dsp_out(0xC6); }

// a controller register (A0h-BFh) in Extended mode
static void reg_w(u8 reg, u8 v) {
  dsp_out(reg);
  dsp_out(v);
}

static int reg_r(u8 reg) {
  if (dsp_out(0xC0) < 0 || dsp_out(reg) < 0)
    return -1;
  return dsp_in(20000);
}

static u8 mix_r(u8 reg) {
  u8 v;

  H->lock();
  H->outb((u16)(B + 4), reg);
  v = H->inb((u16)(B + 5));
  H->unlock();
  return v;
}

static u8 cfg_r(u8 off) { return cfg ? H->inb((u16)(cfg + off)) : 0xFF; }

// a PnP register through the configuration port (LDN 0xFF: card level),
// the index and the logical device put back; -1 without the port
static int pnp_r(u8 ldn, u8 reg) {
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

// --- the restore list --------------------------------------------------------

static u8 keep[ESP_KEEP];
static u16 nkeep;

static int kept(u8 kind, u8 index) {
  u16 i = 0;

  while (i < nkeep) {
    if (keep[i] == kind && keep[i + 1] == index)
      return 1;
    i = (u16)(i + (keep[i] == ESP_K_DAC ? 5 : 3));
  }
  return 0;
}

static void keep_add(u8 kind, u8 index, const u8 *v, u16 n) {
  u16 i;

  if (kept(kind, index) || nkeep + 2 + n > ESP_KEEP)
    return;
  keep[nkeep++] = kind;
  keep[nkeep++] = index;
  for (i = 0; i < n; i++)
    keep[nkeep++] = v[i];
  if (H->keep != NULL)
    H->keep(keep, nkeep);
}

static void mix_w(u8 reg, u8 v) {
  u8 old;

  if (!kept(ESP_K_MIXER, reg) && reg != 0x00) {
    old = mix_r(reg);
    keep_add(ESP_K_MIXER, reg, &old, 1);
  }
  H->lock();
  H->outb((u16)(B + 4), reg);
  H->outb((u16)(B + 5), v);
  H->unlock();
}

static void pnp_w(u8 reg, u8 v) {
  int old = pnp_r(1, reg);
  u8 o;

  if (old < 0)
    return;
  o = (u8)old;
  keep_add(ESP_K_PNP, reg, &o, 1);
  H->lock();
  H->outb(cfg, 0x07);
  H->outb((u16)(cfg + 1), 1);
  H->outb(cfg, reg);
  H->outb((u16)(cfg + 1), v);
  H->unlock();
}

static void cfg_w(u8 off, u8 v) {
  u8 old;

  if (!cfg)
    return;
  old = cfg_r(off);
  keep_add(ESP_K_CONFIG, off, &old, 1);
  H->outb((u16)(cfg + off), v);
}

static void dac_get(u8 i, u8 *rgb) {
  H->lock();
  H->outb(0x3C7, i);
  rgb[0] = H->inb(0x3C9);
  rgb[1] = H->inb(0x3C9);
  rgb[2] = H->inb(0x3C9);
  H->unlock();
}

static void dac_put(u8 i, const u8 *rgb) {
  H->lock();
  H->outb(0x3C8, i);
  H->outb(0x3C9, rgb[0]);
  H->outb(0x3C9, rgb[1]);
  H->outb(0x3C9, rgb[2]);
  H->unlock();
}

// everything back as it was, the last change first
static void restore_all(void) {
  static u16 at[ESP_KEEP / 3];
  u16 i = 0, n = 0;
  u8 *e;

  while (i < nkeep && n < ESS_ARRAY_SIZE(at)) {
    at[n++] = i;
    i = (u16)(i + (keep[i] == ESP_K_DAC ? 5 : 3));
  }
  while (n--) {
    e = keep + at[n];
    switch (e[0]) {
    case ESP_K_MIXER:
      H->lock();
      H->outb((u16)(B + 4), e[1]);
      H->outb((u16)(B + 5), e[2]);
      H->unlock();
      break;
    case ESP_K_PNP:
      H->lock();
      H->outb(cfg, 0x07);
      H->outb((u16)(cfg + 1), 1);
      H->outb(cfg, e[1]);
      H->outb((u16)(cfg + 1), e[2]);
      H->unlock();
      break;
    case ESP_K_CONFIG:
      H->outb((u16)(cfg + e[1]), e[2]);
      break;
    case ESP_K_DAC:
      dac_put(e[1], e + 2);
      break;
    }
  }
  nkeep = 0;
  if (H->keep != NULL)
    H->keep(keep, 0);
}

// --- the DMA controllers -----------------------------------------------------

static u16 ff_port(u8 ch) { return ch >= 4 ? 0xD8 : 0x0C; }
static u16 mask_port(u8 ch) { return ch >= 4 ? 0xD4 : 0x0A; }
static u16 mode_port(u8 ch) { return ch >= 4 ? 0xD6 : 0x0B; }
static u16 addr_port(u8 ch) {
  return (u16)(ch >= 4 ? 0xC0 + 4 * (ch - 4) : 2 * ch);
}
static u16 count_port(u8 ch) {
  return (u16)(ch >= 4 ? 0xC2 + 4 * (ch - 4) : 2 * ch + 1);
}

static void dma_mask(u8 ch, int masked) {
  H->outb(mask_port(ch), (u8)((masked ? 4 : 0) | (ch & 3)));
}

// a channel for bytes at addr in mode (DMA_*), in Creative's order, still
// masked unless unmask; a 16-bit channel's address and count in words
static void dma_set(u8 ch, u32 addr, u32 bytes, u8 mode, int unmask) {
  u16 a, n;
  u8 page;

  if (ch >= 4) {
    a = (u16)((addr >> 1) & 0xFFFF);
    n = (u16)((bytes >> 1) - 1);
    page = (u8)((addr >> 16) & 0xFE);
  } else {
    a = (u16)(addr & 0xFFFF);
    n = (u16)(bytes - 1);
    page = (u8)(addr >> 16);
  }
  H->lock();
  dma_mask(ch, 1);
  H->outb(mode_port(ch), (u8)(mode | (ch & 3)));
  H->outb(ff_port(ch), 0);
  H->outb(addr_port(ch), (u8)a);
  H->outb(addr_port(ch), (u8)(a >> 8));
  H->outb(page_port[ch], page);
  H->outb(count_port(ch), (u8)n);
  H->outb(count_port(ch), (u8)(n >> 8));
  if (unmask)
    dma_mask(ch, 0);
  H->unlock();
}

// the count and address as a program reads them, twice until they agree
static void dma_read(u8 ch, u16 *count, u16 *addr) {
  u16 c1, c2 = 0, a1 = 0;
  int i;

  for (i = 0; i < 4; i++) {
    H->lock();
    H->outb(ff_port(ch), 0);
    a1 = H->inb(addr_port(ch));
    a1 |= (u16)(H->inb(addr_port(ch)) << 8);
    c1 = H->inb(count_port(ch));
    c1 |= (u16)(H->inb(count_port(ch)) << 8);
    H->outb(ff_port(ch), 0);
    c2 = H->inb(count_port(ch));
    c2 |= (u16)(H->inb(count_port(ch)) << 8);
    H->unlock();
    if ((u16)(c1 - c2) <= 0x40)
      break;
  }
  *count = c2;
  if (addr != NULL)
    *addr = a1;
}

// an entry of a window's list
static void io_add(struct esp_io *io, u16 *n, u8 kind, u16 port, u8 v) {
  io[*n].kind = kind;
  io[*n].port = port;
  io[*n].value = v;
  (*n)++;
}

// the same past the memory manager, the count read twice until the two
// agree, as its bytes can change between the reads: 0 without a window
static int dma_phys(u8 ch, u16 *count, u16 *addr, u8 *page) {
  static struct esp_io io[10];
  u16 n, c1, c2 = 0;
  int i;

  if (!window)
    return 0;
  for (i = 0; i < 4; i++) {
    n = 0;
    io_add(io, &n, ESP_OUT, ff_port(ch), 0);
    io_add(io, &n, ESP_IN, addr_port(ch), 0);
    io_add(io, &n, ESP_IN, addr_port(ch), 0);
    io_add(io, &n, ESP_IN, count_port(ch), 0);
    io_add(io, &n, ESP_IN, count_port(ch), 0);
    io_add(io, &n, ESP_IN, page_port[ch], 0);
    io_add(io, &n, ESP_OUT, ff_port(ch), 0);
    io_add(io, &n, ESP_IN, count_port(ch), 0);
    io_add(io, &n, ESP_IN, count_port(ch), 0);
    if (!H->phys(io, n))
      return 0;
    c1 = (u16)(io[3].value | (io[4].value << 8));
    c2 = (u16)(io[7].value | (io[8].value << 8));
    if ((u16)(c1 - c2) <= 0x40)
      break;
  }
  if (addr != NULL)
    *addr = (u16)(io[1].value | (io[2].value << 8));
  if (count != NULL)
    *count = c2;
  if (page != NULL)
    *page = io[5].value;
  return 1;
}

// the count a case measures: past the memory manager where it can
static u16 dma_left(u8 ch) {
  u16 c;

  if (dma_phys(ch, &c, NULL, NULL))
    return c;
  dma_read(ch, &c, NULL);
  return c;
}

// --- the samples -------------------------------------------------------------

static void mem_poke(u32 addr, const u8 *src, u16 n) { H->poke(addr, src, n); }

static u16 frame_bytes(u8 fmt) {
  return (u16)(((fmt & F16) ? 2 : 1) * ((fmt & FST) ? 2 : 1));
}

// a tone's frames at addr; spread: each byte into a word's low byte, as
// Audio 2 takes them from a 16-bit channel
static void put_tone(u32 addr, u16 frames, u8 fmt, struct snd_tone *t,
                     int spread) {
  static u8 buf[256];
  u16 i = 0, f, w;
  int c, chans = (fmt & FST) ? 2 : 1, j, k;
  u8 b[2];
  s16 s;

  for (f = 0; f < frames; f++) {
    for (c = 0; c < chans; c++) {
      s = snd_next(t, c);
      w = (u16)s;
      if (fmt & F16) {
        if (!(fmt & FSG))
          w ^= 0x8000;
        b[0] = (u8)w;
        b[1] = (u8)(w >> 8);
        k = 2;
      } else {
        w >>= 8;
        if (!(fmt & FSG))
          w ^= 0x80;
        b[0] = (u8)w;
        k = 1;
      }
      for (j = 0; j < k; j++) {
        buf[i++] = b[j];
        if (spread)
          buf[i++] = 0;
      }
    }
    if (i >= sizeof(buf) - 8) {
      H->poke(addr, buf, i);
      addr += i;
      i = 0;
    }
  }
  if (i)
    H->poke(addr, buf, i);
}

// guards: a pattern in the area's gaps, and how many bytes of it changed
static u8 guard_byte(u16 i, u8 which) { return (u8)(0x5A ^ i ^ (which << 6)); }

static void guards_set(void) {
  static u8 buf[256];
  static const u32 at[3] = {G_A, G_B, G_C};
  u16 g, i, j;

  for (g = 0; g < 3; g++)
    for (i = 0; i < GUARD; i = (u16)(i + sizeof(buf))) {
      for (j = 0; j < sizeof(buf); j++)
        buf[j] = guard_byte((u16)(i + j), (u8)g);
      H->poke(C->area + at[g] + i, buf, sizeof(buf));
    }
}

// the bytes that changed, and the first's offset in the area
static u16 guards_check(u32 *first) {
  static u8 buf[256];
  static const u32 at[3] = {G_A, G_B, G_C};
  u16 g, i, j, n = 0;

  *first = 0;
  for (g = 0; g < 3; g++)
    for (i = 0; i < GUARD; i = (u16)(i + sizeof(buf))) {
      H->peek(C->area + at[g] + i, buf, sizeof(buf));
      for (j = 0; j < sizeof(buf); j++)
        if (buf[j] != guard_byte((u16)(i + j), (u8)g)) {
          if (!n)
            *first = at[g] + i + j;
          n++;
        }
    }
  return n;
}

// --- the rates ---------------------------------------------------------------

#define A1_HI 793800.0 // A1h's clocks, as the first report measured them
#define A1_LO 396900.0
#define A2_HI 793800.0 // 70h's: bit 7 clear
#define A2_768 768000.0

static double rnd(double x) { return floor(x + 0.5); }

// ES1869.DRV's filter dividers for playback (DGROUP 4Eh, which 6:2308
// takes unless its flag says recording) for the 793.8 kHz clock's
// divisors 18-35
static const u8 filter_play[18] = {3, 5, 5, 5, 6, 6, 6, 7, 7,
                                   7, 7, 8, 8, 8, 8, 9, 9, 9};

// A1h and A2h for rate: clk 1 the low clock, 2 the high one, 0 as the
// rate needs (the high one above 22050 Hz); the rate they give
static double a1_regs(double rate, u8 clk, u8 *a1, u8 *a2) {
  double d, got, f;

  if (clk == 1 || (clk == 0 && rate <= 22050)) {
    d = rnd(A1_LO / rate);
    if (d < 1)
      d = 1;
    if (d > 128)
      d = 128;
    *a1 = (u8)(128 - d);
    got = A1_LO / d;
  } else {
    d = rnd(A1_HI / rate);
    if (d < 1)
      d = 1;
    if (d > 128)
      d = 128;
    *a1 = (u8)(256 - d);
    got = A1_HI / d;
  }
  if (*a1 & 0x80 && d >= 18 && d <= 35)
    f = filter_play[(int)d - 18];
  else
    f = rnd(199606.6 / got);
  *a2 = (u8)(256 - f);
  return got;
}

// 70h and 72h: the clock that comes nearer, the divisor 128 at most
static double a2_regs(double rate, u8 *r70, u8 *r72) {
  double d1 = rnd(A2_HI / rate), d2 = rnd(A2_768 / rate), got;

  if (d1 > 128)
    d1 = 128;
  if (d2 > 128)
    d2 = 128;
  if (fabs(A2_768 / d2 - rate) < fabs(A2_HI / d1 - rate)) {
    *r70 = (u8)(0x80 | ((128 - (int)d2) & 0x7F));
    got = A2_768 / d2;
  } else {
    *r70 = (u8)((128 - (int)d1) & 0x7F);
    got = A2_HI / d1;
  }
  *r72 = rate >= 44100 ? 0xFD : (u8)(256 - rnd(199606.6 / got));
  return got;
}

// --- the interrupts ----------------------------------------------------------

static volatile u16 n1, n2;    // Audio 1's and Audio 2's interrupts
static volatile u32 t1a, t1b;  // the first and the last of Audio 1's
static volatile u32 t2a, t2b;  // and of Audio 2's
static volatile u8 ack1, ack2; // the handler acknowledges them
static volatile u16 nother;    // the card's line, from neither
static volatile u8 irq_ready;  // the handler may touch the chip

void esp_irq(void) {
  u8 st, idx, v;
  u32 t;

  if (!irq_ready)
    return;
  st = cfg ? H->inb((u16)(cfg + 6)) : 0x03;
  t = H->now();
  if (st & 0x01) {
    if (!n1)
      t1a = t;
    t1b = t;
    n1++;
    if (ack1)
      H->inb((u16)(B + 0x0E));
  }
  if (st & 0x02) {
    idx = H->inb((u16)(B + 4));
    H->outb((u16)(B + 4), 0x7A);
    v = H->inb((u16)(B + 5));
    if (v & 0x80) {
      if (!n2)
        t2a = t;
      t2b = t;
      n2++;
      if (ack2)
        H->outb((u16)(B + 5), (u8)(v & 0x7F));
    }
    H->outb((u16)(B + 4), idx);
  }
  if (!(st & 0x03))
    nother++;
}

static void irq_clear(void) {
  H->lock();
  n1 = n2 = nother = 0;
  t1a = t1b = t2a = t2b = 0;
  H->unlock();
}

// a block's period in ms from an engine's interrupts, the first left out,
// which the FIFO brings early; 0 for fewer than three
static double irq_ms(u16 n, u32 ta, u32 tb) {
  if (n < 3)
    return 0;
  return (double)us_of((tb - ta) & 0xFFFFFFFFUL) / 1000.0 / (n - 1);
}

// --- the cases ---------------------------------------------------------------

// what runs a case
enum {
  RUN_PLAY,    // the engines play a tone (a1, a2, both)
  RUN_MACHINE, // the platform's lines about the machine
  RUN_CARD,    // the card's identity and its PnP registers
  RUN_MIXER,   // the registers as the program found them
  RUN_DMA,     // the DMA controllers as found
  RUN_RST,     // what a reset clears
  RUN_REACH,   // when a program's DMA programming reaches the 8237
  RUN_READ,    // what a program reads of a channel while it moves
  RUN_FF,      // whether the memory manager's flip-flop is its own
  RUN_CROSS,   // a buffer across a page: the memory manager's own buffer
  RUN_STALE,   // a channel carried over before or after its unmask
  RUN_LOAD,    // what the engines do to the CPU, the VGA, memory
  RUN_STREAM,  // a game's refills behind the count
  RUN_MIRROR   // the same through a channel carried over, as esssb16 does
};

#define E1 0x01 // Audio 1
#define E2 0x02 // Audio 2

// the pauses
enum { P_NONE, P_B2, P_B8, P_B7, P_78F, P_78D, P_78FD, P_D0 };

// a case's spec, parsed
struct pc {
  u8 run, eng, ch, kind;
  u8 fmt1, fmt2;    // F16 | FST | FSG
  u16 rate1, rate2; // Hz
  u8 clk;           // A1h's clock: 0 as the rate needs, 1 low, 2 high
  u8 newa1;         // 71h bit 5: A1h as 70h
  u8 once;          // normal mode: one block
  u8 b9;            // B9h
  u8 dmd;           // the channels in demand mode: 1 Audio 1's, 2 Audio 2's
  u8 t2;            // 78h bits 7:6
  u8 wide;          // 78h bit 5
  u8 packed;        // a 16-bit channel's buffer as a game's words
  u8 slaved;        // 71h bit 1 clear: Audio 2 on Audio 1's clock
  u8 x4;            // 71h bit 4
  u8 fb;            // 71h bits 3:2, 0xFF as they are
  u8 vol1, vol2;    // 14h and 7Ch
  u8 fifo;          // 0 a reset first, 1 kept, 2 filled before the DAC
  u8 pause;         // P_*
  u8 irq1, irq2;    // the engines' interrupts on
  u8 noack;         // never acknowledged
  u8 after;         // the DMA unmasked after the start
  u16 late;         // and this many us after it
  u8 hold;          // the pause's end with the DMA masked, unmasked after
  u16 b2late;       // Audio 1's DMA requests on this many us after B8h
  u8 order;         // Audio 1's registers in the Windows player's order
  u8 b7;            // B7h's first write: 0 as Table 16, 1 none, 2 bit 0 clear
  u8 start2;        // Audio 2 started in two steps: 78h bit 1, then bit 0
  u16 ms;           // how long it plays
  u8 side;          // 0, 'l' or 'r'
  u16 hz1, hz2;     // the tones, 0 for none
  u8 wrong;         // the channel programmed the wrong way round
  u8 sh;            // in the short run
};

struct ecase {
  const char *id, *spec, *expect;
};

// Each line: the id (its group, a dot, its name), the spec over the
// group's base (spec_base) and what the recording should hold: tone, left,
// right, dual (294 Hz and 882 Hz), low (294 Hz alone), none, pause, glide,
// loop, any (what the chip does there is to be found), or - (no sound).
// S marks the short run's cases.
static const struct ecase cases[] = {
    // the machine and the card
    {"env.machine", "run=machine S", "-"},
    {"env.card", "run=card S", "-"},
    {"env.regs", "run=mixer S", "-"},
    {"env.dma", "run=dma S", "-"},
    // what each reset clears: +6 = 3, 1, 2, and mixer 00h
    {"rst.ext", "run=rst k=3 S", "-"},
    {"rst.compat", "run=rst k=1", "-"},
    {"rst.fifo", "run=rst k=2", "-"},
    {"rst.mixer", "run=rst k=0", "-"},
    // Audio 1 alone: 16-bit stereo at 44100 Hz, auto-initialized, B9h 2
    {"a1.base", "S", "tone"},
    {"a1.f8m", "f=8m", "tone"},
    {"a1.f8s", "f=8s S", "tone"},
    {"a1.f16m", "f=16m", "tone"},
    {"a1.s8", "f=8m sg=1", "tone"},
    {"a1.u16", "sg=0", "tone"},
    {"a1.r8000", "r=8000", "tone"},
    {"a1.r11025", "r=11025", "tone"},
    {"a1.r22050", "r=22050 S", "tone"},
    {"a1.r48000", "r=48000", "tone"},
    {"a1.lo44100", "clk=lo", "tone"},
    {"a1.hi22050", "r=22050 clk=hi", "tone"},
    {"a1.new48000", "r=48000 new", "tone"},
    {"a1.once", "once r=11025", "tone"},
    {"a1.b9_0", "b9=0 S", "tone"},
    {"a1.b9_1", "b9=1", "tone"},
    {"a1.b9_3", "b9=3", "tone"},
    {"a1.dmd", "dmd", "tone"},
    {"a1.keep", "fifo=keep", "tone"},
    {"a1.prime", "fifo=prime", "tone"},
    {"a1.pause_b2", "pause=b2 S", "pause"},
    {"a1.pause_b8", "pause=b8", "pause"},
    {"a1.pause_d0", "pause=d0", "any"},
    {"a1.after", "after S", "tone"},
    {"a1.left", "side=l", "left"},
    {"a1.right", "side=r S", "right"},
    {"a1.irq_off", "irq=0", "tone"},
    {"a1.noack", "noack", "tone"},
    {"a1.muted", "v1=0", "none"},
    // Audio 2 alone: the same, on the card's channel, demand transfers of 4
    // bytes, each byte in a word's low byte on a 16-bit channel
    {"a2.base", "S", "tone"},
    {"a2.f8m", "f=8m", "tone"},
    {"a2.f8s", "f=8s", "tone"},
    {"a2.f16m", "f=16m S", "tone"},
    {"a2.s8", "f=8m sg=1", "tone"},
    {"a2.u16", "sg=0", "tone"},
    {"a2.r8000", "r=8000", "tone"},
    {"a2.r22050", "r=22050 S", "tone"},
    {"a2.r48000", "r=48000", "tone"},
    {"a2.t2_0", "t2=0 S", "tone"},
    {"a2.t2_1", "t2=1", "tone"},
    {"a2.t2_3", "t2=3", "tone"},
    {"a2.dmd", "dmd=2 S", "tone"},
    {"a2.slaved", "slaved r1=22050 S", "tone"},
    {"a2.x4", "x4", "tone"},
    {"a2.fb", "fb=2", "tone"},
    {"a2.once", "once r=11025", "tone"},
    {"a2.pause_f", "pause=f S", "pause"},
    {"a2.pause_d", "pause=d", "pause"},
    {"a2.after", "after", "tone"},
    {"a2.left", "side=l", "left"},
    {"a2.right", "side=r", "right"},
    {"a2.start2", "start2", "tone"},
    {"a2.left2", "side=l start2", "left"},
    {"a2.irq_off", "irq=0", "tone"},
    {"a2.noack", "noack", "tone"},
    {"a2.muted", "v2=0", "none"},
    {"a2.packed", "packed", "any"},
    {"a2.wide", "wide", "any"},
    {"a2.wide_packed", "wide packed S", "any"},
    // both: Audio 1 at 294 Hz and Audio 2 at 882 Hz
    {"both.base", "S", "dual"},
    {"both.slaved", "slaved S", "dual"},
    {"both.f8_16", "f1=8m", "dual"},
    {"both.f16_8", "f2=8m", "dual"},
    {"both.r22_44", "r1=22050", "dual"},
    {"both.t2_0", "t2=0 S", "dual"},
    {"both.single", "b9=0 t2=0", "dual"},
    {"both.dmd", "dmd=3", "dual"},
    {"both.muted2", "v2=0 t2=0", "low"},
    {"both.pace", "v2=0 t2=0 slaved f2=8s irq2=0 S", "low"},
    // the DMA controllers behind the memory manager
    {"h1.reach1", "run=reach ch=1 S", "-"},
    {"h1.reach5", "run=reach ch=5 S", "-"},
    {"h1.read1", "run=read ch=1", "-"},
    {"h1.read5", "run=read ch=5", "-"},
    {"h1.ff1", "run=ff ch=1", "-"},
    {"h1.ff5", "run=ff ch=5", "-"},
    {"h1.cross1", "run=cross ch=1", "-"},
    {"h1.cross5", "run=cross ch=5", "-"},
    {"h1.stale", "run=stale S", "any"},
    {"h1.fresh", "run=stale after", "tone"},
    // what the engines do to the machine
    {"h4.idle", "run=load e=0 S", "none"},
    {"h4.a1", "run=load e=1 dmd", "tone"},
    {"h4.a1s", "run=load e=1 b9=0", "tone"},
    {"h4.a2", "run=load e=2", "tone"},
    {"h4.a2s", "run=load e=2 t2=0 S", "tone"},
    {"h4.pace", "run=load e=3 v2=0 t2=0 slaved f2=8s irq2=0 S", "tone"},
    {"h4.both", "run=load e=3", "any"},
    {"h4.wrong1", "run=load e=1 wrong", "any"},
    {"h4.wrong2", "run=load e=2 wrong", "any"},
    // a game's refills behind the count, a glide that rises
    {"stream.a1", "run=stream e=1 S", "glide"},
    {"stream.a1_8", "run=stream e=1 f=8s", "glide"},
    {"stream.mirror", "run=mirror S", "loop"},
    {"stream.a2", "run=stream e=2 S", "glide"},
    {"stream.a2w", "run=stream e=2 wide packed", "any"},
    // which start keeps a 16-bit stream in step, and which pause keeps it
    // after the continue: the tone on the left alone, so that a stream a
    // byte off is noise and two bytes off is on the right; Audio 1 by
    // default, and the cases repeated, since a start is a race
    {"fix.base1", "S", "left"},
    {"fix.base2", "", "left"},
    {"fix.late0", "late=0", "left"},
    {"fix.late20", "late=20", "left"},
    {"fix.late50", "late=50", "left"},
    {"fix.late100a", "late=100 S", "left"},
    {"fix.late100b", "late=100", "left"},
    {"fix.late100c", "late=100", "left"},
    {"fix.late300", "late=300", "left"},
    {"fix.late2000", "late=2000", "left"},
    {"fix.win1", "order=win S", "left"},
    {"fix.win2", "order=win", "left"},
    {"fix.win3", "order=win", "left"},
    {"fix.once7a", "b7=once S", "left"},
    {"fix.once7b", "b7=once", "left"},
    {"fix.noload1", "b7=nl", "left"},
    {"fix.noload2", "b7=nl", "left"},
    {"fix.winlate1", "order=win late=100 S", "left"},
    {"fix.winlate2", "order=win late=100", "left"},
    {"fix.r22late", "r=22050 late=200", "left"},
    {"fix.r11late", "r=11025 late=300", "left"},
    {"fix.r8late", "r=8000 late=400", "left"},
    {"fix.r22win", "r=22050 order=win", "left"},
    {"fix.r11win", "r=11025 order=win", "left"},
    {"fix.s8base", "f=8s", "left"},
    {"fix.s8late", "f=8s late=100", "left"},
    {"fix.pb2late", "late=100 pause=b2 ms=1500", "lpause"},
    {"fix.pb2late2", "late=100 pause=b2 ms=1500", "lpause"},
    {"fix.pb2late3", "late=100 pause=b2 ms=1500", "lpause"},
    {"fix.pb8late", "late=100 pause=b8 ms=1500", "lpause"},
    {"fix.pb2hold1", "late=100 pause=b2 hold S ms=1500", "lpause"},
    {"fix.pb2hold2", "late=100 pause=b2 hold ms=1500", "lpause"},
    {"fix.pb2hold3", "late=100 pause=b2 hold ms=1500", "lpause"},
    {"fix.pb8hold1", "late=100 pause=b8 hold ms=1500", "lpause"},
    {"fix.pb8hold2", "late=100 pause=b8 hold ms=1500", "lpause"},
    {"fix.b2late1", "b2late=100 S", "left"},
    {"fix.b2late2", "b2late=100", "left"},
    {"fix.b2late3", "b2late=100", "left"},
    {"fix.r11b2late", "r=11025 b2late=300", "left"},
    {"fix.pb7a", "late=100 pause=b7 S ms=1500", "lpause"},
    {"fix.pb7b", "late=100 pause=b7 ms=1500", "lpause"},
    {"fix.pb7c", "late=100 pause=b7 ms=1500", "lpause"},
    {"fix.pb7m", "f=16m late=100 pause=b7 ms=1500", "pause"},
    {"fix.pb7s8", "f=8s late=100 pause=b7 ms=1500", "lpause"},
    {"fix.pb7r22", "r=22050 late=200 pause=b7 ms=1500", "lpause"},
    {"fix.a2base1", "e=2 S", "left"},
    {"fix.a2base2", "e=2", "left"},
    {"fix.a2pf1", "e=2 pause=f ms=1500", "lpause"},
    {"fix.a2pf2", "e=2 pause=f ms=1500", "lpause"},
    {"fix.a2pd1", "e=2 pause=d S ms=1500", "lpause"},
    {"fix.a2pd2", "e=2 pause=d ms=1500", "lpause"},
    {"fix.a2pd3", "e=2 pause=d ms=1500", "lpause"},
    {"fix.a2pfd1", "e=2 pause=fd S ms=1500", "lpause"},
    {"fix.a2pfd2", "e=2 pause=fd ms=1500", "lpause"},
    {"fix.a2pfd3", "e=2 pause=fd ms=1500", "lpause"},
    {"fix.a2pdhold1", "e=2 pause=d hold S ms=1500", "lpause"},
    {"fix.a2pdhold2", "e=2 pause=d hold ms=1500", "lpause"},
    {"fix.a2pfdhold", "e=2 pause=fd hold ms=1500", "lpause"},
};

u16 esp_cases(void) { return (u16)ESS_ARRAY_SIZE(cases); }
const char *esp_case_id(u16 i) { return cases[i].id; }
const char *esp_case_spec(u16 i) { return cases[i].spec; }
const char *esp_case_expect(u16 i) { return cases[i].expect; }

// a format's name: 8-bit unsigned or 16-bit signed, mono or stereo; FFh
// if it isn't one
static u8 fmt_of(const char *v) {
  if (!strcmp(v, "8m"))
    return 0;
  if (!strcmp(v, "8s"))
    return FST;
  if (!strcmp(v, "16m"))
    return F16 | FSG;
  if (!strcmp(v, "16s"))
    return F16 | FST | FSG;
  return 0xFF;
}

// the base case of a group
static void spec_base(struct pc *p, const char *id) {
  memset(p, 0, sizeof(*p));
  p->fmt1 = p->fmt2 = F16 | FST | FSG;
  p->rate1 = p->rate2 = 44100;
  p->b9 = 2;
  p->t2 = 2;
  p->fb = 0xFF;
  p->vol1 = p->vol2 = 0xCC;
  p->irq1 = p->irq2 = 1;
  p->ms = 1500;
  p->hz1 = p->hz2 = (u16)SND_HZ;
  if (!strncmp(id, "a1.", 3))
    p->eng = E1;
  else if (!strncmp(id, "a2.", 3))
    p->eng = E2;
  else if (!strncmp(id, "both.", 5)) {
    p->eng = E1 | E2;
    p->ms = 2000;
    p->hz1 = 294;
    p->hz2 = 882;
  } else if (!strncmp(id, "fix.", 4)) {
    p->eng = E1;
    p->ms = 800;
    p->side = 'l';
  }
}

// one key of a spec, k with its value v (NULL for a flag); 0 if unknown
static int spec_key(struct pc *p, const char *k, const char *v) {
  unsigned n = v != NULL ? (unsigned)strtoul(v, NULL, 10) : 1;

#define IS(name) (!strcmp(k, name))
  if (IS("S"))
    p->sh = 1;
  else if (IS("run") && v != NULL) {
    static const char *const runs[] = {
        "play", "machine", "card",  "mixer", "dma",  "rst",    "reach",
        "read", "ff",      "cross", "stale", "load", "stream", "mirror"};
    unsigned i;
    for (i = 0; i < ESS_ARRAY_SIZE(runs); i++)
      if (!strcmp(v, runs[i]))
        break;
    if (i == ESS_ARRAY_SIZE(runs))
      return 0;
    p->run = (u8)i;
  } else if (IS("e"))
    p->eng = (u8)n;
  else if (IS("ch"))
    p->ch = (u8)n;
  else if (IS("k"))
    p->kind = (u8)n;
  else if ((IS("f") || IS("f1") || IS("f2")) && v != NULL) {
    u8 f = fmt_of(v);
    if (f == 0xFF)
      return 0;
    if (k[1] != '2')
      p->fmt1 = f;
    if (k[1] != '1')
      p->fmt2 = f;
  } else if (IS("sg")) {
    p->fmt1 = (u8)(n ? p->fmt1 | FSG : p->fmt1 & ~FSG);
    p->fmt2 = (u8)(n ? p->fmt2 | FSG : p->fmt2 & ~FSG);
  } else if (IS("r"))
    p->rate1 = p->rate2 = (u16)n;
  else if (IS("r1"))
    p->rate1 = (u16)n;
  else if (IS("r2"))
    p->rate2 = (u16)n;
  else if (IS("clk") && v != NULL && (!strcmp(v, "lo") || !strcmp(v, "hi")))
    p->clk = (u8)(!strcmp(v, "lo") ? 1 : 2);
  else if (IS("new"))
    p->newa1 = 1;
  else if (IS("once"))
    p->once = 1;
  else if (IS("b9"))
    p->b9 = (u8)(n & 3);
  else if (IS("dmd"))
    p->dmd = (u8)(n & 3);
  else if (IS("t2"))
    p->t2 = (u8)(n & 3);
  else if (IS("wide"))
    p->wide = 1;
  else if (IS("packed"))
    p->packed = 1;
  else if (IS("slaved"))
    p->slaved = 1;
  else if (IS("x4"))
    p->x4 = 1;
  else if (IS("fb"))
    p->fb = (u8)(n & 3);
  else if (IS("v1"))
    p->vol1 = (u8)n;
  else if (IS("v2"))
    p->vol2 = (u8)n;
  else if (IS("fifo") && v != NULL &&
           (!strcmp(v, "keep") || !strcmp(v, "prime")))
    p->fifo = (u8)(!strcmp(v, "keep") ? 1 : 2);
  else if (IS("pause") && v != NULL) {
    if (!strcmp(v, "b2"))
      p->pause = P_B2;
    else if (!strcmp(v, "b8"))
      p->pause = P_B8;
    else if (!strcmp(v, "b7"))
      p->pause = P_B7;
    else if (!strcmp(v, "f"))
      p->pause = P_78F;
    else if (!strcmp(v, "d"))
      p->pause = P_78D;
    else if (!strcmp(v, "fd"))
      p->pause = P_78FD;
    else if (!strcmp(v, "d0"))
      p->pause = P_D0;
    else
      return 0;
  } else if (IS("irq"))
    p->irq1 = p->irq2 = (u8)n;
  else if (IS("irq1"))
    p->irq1 = (u8)n;
  else if (IS("irq2"))
    p->irq2 = (u8)n;
  else if (IS("noack"))
    p->noack = 1;
  else if (IS("after"))
    p->after = 1;
  else if (IS("late")) {
    p->after = 1;
    p->late = (u16)n;
  } else if (IS("hold"))
    p->hold = 1;
  else if (IS("b2late"))
    p->b2late = (u16)n;
  else if (IS("order") && v != NULL && !strcmp(v, "win"))
    p->order = 1;
  else if (IS("b7") && v != NULL && (!strcmp(v, "once") || !strcmp(v, "nl")))
    p->b7 = (u8)(!strcmp(v, "once") ? 1 : 2);
  else if (IS("start2"))
    p->start2 = 1;
  else if (IS("ms"))
    p->ms = (u16)n;
  else if (IS("side") && v != NULL && (!strcmp(v, "l") || !strcmp(v, "r")))
    p->side = (u8)v[0];
  else if (IS("hz1"))
    p->hz1 = (u16)n;
  else if (IS("hz2"))
    p->hz2 = (u16)n;
  else if (IS("wrong"))
    p->wrong = 1;
  else
    return 0;
#undef IS
  return 1;
}

// a spec into p over the base: 0, or the position of the first key that
// doesn't parse, from 1
static int spec_parse(struct pc *p, const char *spec) {
  static char key[16], val[16];
  const char *s = spec, *e;
  unsigned n;

  while (*s) {
    while (*s == ' ')
      s++;
    if (!*s)
      break;
    e = s;
    while (*e && *e != ' ' && *e != '=')
      e++;
    n = (unsigned)(e - s);
    if (n >= sizeof(key))
      return (int)(s - spec) + 1;
    memcpy(key, s, n);
    key[n] = 0;
    if (*e == '=') {
      s = ++e;
      while (*e && *e != ' ')
        e++;
      n = (unsigned)(e - s);
      if (n == 0 || n >= sizeof(val))
        return (int)(s - spec) + 1;
      memcpy(val, s, n);
      val[n] = 0;
      if (!spec_key(p, key, val))
        return (int)(s - spec) + 1;
    } else if (!spec_key(p, key, NULL))
      return (int)(s - spec) + 1;
    s = e;
  }
  return 0;
}

int esp_parse_check(const char *spec) {
  struct pc p;

  spec_base(&p, "a1.");
  return spec_parse(&p, spec);
}

// --- the engines -------------------------------------------------------------

// an engine's transfer in a case
struct eng {
  u8 on, ch, fmt;
  u8 spread;    // each byte in a word's low byte (Audio 2, a 16-bit channel)
  u8 per_unit;  // the stream's bytes a DMA unit carries: 2 for words whole
  u8 r1, r2;    // A1h and A2h, or 70h and 72h
  u32 addr;     // the buffer
  u16 frames;   // its frames, whole cycles of the tone
  u16 sbytes;   // its stream bytes
  u16 units;    // the channel's units (bytes, or words) it takes
  double rate;  // the rate the registers give
  u16 last;     // the last count read
  u32 moved;    // units moved since the start
  u32 ta, ma;   // the clock and the units at the first sample counted
  u32 tb, mb;   // at the last that moved
  u32 stall;    // the time it stood for 20 ms or more in between
  u32 pause_at; // the units when the pause began, and during it
  u32 in_pause;
};

static struct eng en[2];
static u8 b8_run, r78_run; // B8h and 78h while the engines run

// the frames a buffer of bytes holds; 2048 bytes of samples at most in
// the short sounds
static u16 most_frames(u8 fmt, int spread, u16 bytes) {
  u16 n = (u16)(bytes / (frame_bytes(fmt) * (spread ? 2 : 1)));

  if (O & ESP_O_TINY && n > 2048 / frame_bytes(fmt))
    n = (u16)(2048 / frame_bytes(fmt));
  return n;
}

// the rates and buffers of the case's engines
static void plan(const struct pc *p) {
  struct eng *a = &en[0], *b = &en[1];
  u8 x70, x72;

  memset(en, 0, sizeof(en));
  a->on = (u8)((p->eng & E1) != 0);
  b->on = (u8)((p->eng & E2) != 0);
  a->ch = dma1;
  a->fmt = p->fmt1;
  a->per_unit = 1;
  a->addr = C->area + BUF1;
  if (p->newa1) {
    a->rate = a2_regs(p->rate1, &a->r1, &x72);
    a->r2 = x72;
  } else
    a->rate = a1_regs(p->rate1, p->clk, &a->r1, &a->r2);
  b->ch = dma2;
  b->fmt = p->fmt2;
  b->spread = (u8)(b->ch >= 4 && !p->packed);
  b->per_unit = (u8)(b->ch >= 4 && p->wide ? 2 : 1);
  b->addr = C->area + BUF2;
  b->rate = a2_regs(p->rate2, &x70, &x72);
  b->r1 = x70;
  b->r2 = x72;
  if (p->slaved)
    b->rate = a->rate;
}

// the engine's buffer: whole cycles of hz at its rate, on one side or both
static void fill_tone(struct eng *e, u16 hz, u8 side, long amp) {
  struct snd_tone t;
  u16 most = most_frames(e->fmt, e->spread, e->spread ? BUF2_MAX : BUF1_MAX);
  double f;

  e->frames = snd_whole_cycles(hz, e->rate, most, 2);
  f = snd_fit_hz(hz, e->frames, e->rate);
  if (e->fmt & FST)
    snd_tone(&t, side == 'r' ? 0 : f, side == 'l' ? 0 : f, e->rate, amp);
  else
    snd_tone(&t, f, 0, e->rate, amp);
  put_tone(e->addr, e->frames, e->fmt, &t, e->spread);
  e->sbytes = (u16)(e->frames * frame_bytes(e->fmt));
  e->units =
      e->ch >= 4 ? (u16)(e->spread ? e->sbytes : e->sbytes / 2) : e->sbytes;
}

// the channel of an engine: auto-initialized, from memory, or into the
// scratch area the wrong way round
static void eng_dma(const struct pc *p, struct eng *e, int unmask) {
  u8 mode = (u8)((p->dmd & (e == &en[0] ? 1 : 2) ? DMA_DEMAND : DMA_SINGLE) |
                 DMA_AUTO);
  u32 bytes = e->ch >= 4 ? (u32)e->units * 2 : e->units;

  if (p->wrong) {
    if (bytes > GUARD)
      bytes = GUARD;
    dma_set(e->ch, C->area + SCR, bytes, (u8)(mode | DMA_WRITE), unmask);
  } else
    dma_set(e->ch, e->addr, bytes, (u8)(mode | DMA_READ), unmask);
}

// mixer 71h for the case: Audio 2's own clock unless slaved, 4x, A1h's
// mode, the filters
static void set_71(const struct pc *p) {
  u8 r = mix_r(0x71);

  r = (u8)(r & ~0x33);
  if (!p->slaved)
    r |= 0x02;
  if (p->x4)
    r |= 0x10;
  if (p->newa1)
    r |= 0x20;
  if (p->fb != 0xFF)
    r = (u8)((r & ~0x0C) | (p->fb << 2));
  mix_w(0x71, r);
}

// Audio 1's count, A4h and A5h: minus a block's bytes
static void a1_count(const struct pc *p, const struct eng *e) {
  u16 block = (u16)(p->once ? e->sbytes : e->sbytes / 2), cnt;

  cnt = (u16)(0 - block);
  reg_w(0xA4, (u8)cnt);
  reg_w(0xA5, (u8)(cnt >> 8));
}

// Audio 1's format: B6h, B7h's first write (51h or 71h, with its bit 0,
// as Table 16 has it, or without, or none), then B7h's format
static void a1_format(const struct pc *p, const struct eng *e) {
  u8 b7;

  reg_w(0xB6, (u8)((e->fmt & FSG) ? 0x00 : 0x80));
  if (p->b7 != 1)
    reg_w(0xB7, (u8)(((e->fmt & FSG) ? 0x71 : 0x51) & (p->b7 ? 0xFE : 0xFF)));
  b7 = 0xD0;
  if (e->fmt & FSG)
    b7 ^= 0x20;
  if (e->fmt & F16)
    b7 ^= 0x04;
  if (e->fmt & FST)
    b7 ^= 0x48;
  reg_w(0xB7, (u8)(p->fifo == 2 ? b7 & 0x7F : b7));
}

// B1h and B2h with their bits 6 and 4, the volume and the DAC's way in
static void a1_on(const struct pc *p) {
  u8 v;
  int r;

  r = reg_r(0xB1);
  v = (u8)((r < 0 ? 0 : r) | 0x10);
  reg_w(0xB1, (u8)(p->irq1 ? v | 0x40 : v & ~0x40));
  r = reg_r(0xB2);
  reg_w(0xB2, (u8)((r < 0 ? 0 : r) | 0x50));
  mix_w(0x14, p->vol1);
  dsp_out(0xD1);
}

// Audio 1 as the native drivers start it, stopped: B8h, B9h, A8h, A1h,
// A2h, A4h and A5h, the FIFO emptied, B6h, B7h twice, B1h, B2h, D1h; or in
// the order of build/ES1869.DRV's player, which writes the format when a
// device opens, and B8h, the FIFO and the count at each start: A1h, A2h,
// A8h, B9h, the format, B1h, B2h, D1h, then B8h, the FIFO, A4h and A5h
static void a1_program(const struct pc *p, struct eng *e) {
  int r;

  b8_run = (u8)(p->once ? 0x00 : 0x04);
  if (p->order) {
    reg_w(0xA1, e->r1);
    reg_w(0xA2, e->r2);
    r = reg_r(0xA8);
    reg_w(0xA8, (u8)(((r < 0 ? 0 : r) & 0xFC) | ((e->fmt & FST) ? 1 : 2)));
    reg_w(0xB9, p->b9);
    a1_format(p, e);
    a1_on(p);
    reg_w(0xB8, b8_run);
    if (p->fifo == 0)
      fifo_reset();
    a1_count(p, e);
    return;
  }
  reg_w(0xB8, b8_run);
  reg_w(0xB9, p->b9);
  r = reg_r(0xA8);
  reg_w(0xA8, (u8)(((r < 0 ? 0 : r) & 0xFC) | ((e->fmt & FST) ? 1 : 2)));
  reg_w(0xA1, e->r1);
  reg_w(0xA2, e->r2);
  a1_count(p, e);
  if (p->fifo == 0)
    fifo_reset();
  a1_format(p, e);
  a1_on(p);
}

// Audio 2 as ES1869.DRV starts it, stopped: 78h, 70h and 72h, 74h and
// 76h, 7Ah, 7Ch
static void a2_program(const struct pc *p, struct eng *e) {
  u16 block = (u16)(p->once ? e->sbytes : e->sbytes / 2), cnt;

  mix_w(0x78, 0x00);
  mix_w(0x70, e->r1);
  mix_w(0x72, e->r2);
  cnt = (u16)(0 - block);
  mix_w(0x74, (u8)cnt);
  mix_w(0x76, (u8)(cnt >> 8));
  mix_w(0x7A, (u8)((p->irq2 ? 0x40 : 0) | ((e->fmt & FSG) ? 4 : 0) |
                   ((e->fmt & FST) ? 2 : 0) | ((e->fmt & F16) ? 1 : 0)));
  mix_w(0x7C, p->vol2);
  r78_run =
      (u8)((p->t2 << 6) | (p->wide ? 0x20 : 0) | (p->once ? 0 : 0x10) | 0x03);
}

// Audio 1's DMA requests on or off (B2h bit 6); 0 if B2h doesn't read
static int b2_dma(int on) {
  int r = reg_r(0xB2);

  if (r < 0)
    return 0;
  reg_w(0xB2, (u8)(on ? r | 0x40 : r & ~0x40));
  return 1;
}

// the engines' channels masked or unmasked
static void eng_masks(int masked) {
  int i;

  for (i = 0; i < 2; i++)
    if (en[i].on)
      dma_mask(en[i].ch, masked);
}

// the engines start: Audio 2's DMA first, its FIFO off the DAC, then
// Audio 1 and the FIFO onto the DAC with interrupts off, as ES1869.DRV's
// dual playback starts them. With b2late, Audio 1 starts without its DMA
// requests, which come that much later
static void eng_start(const struct pc *p) {
  if (en[1].on && en[0].on) {
    mix_w(0x78, (u8)(r78_run & ~0x01));
    pass_time(2000);
    H->lock();
    reg_w(0xB8, (u8)(b8_run | 1));
    mix_w(0x78, r78_run);
    H->unlock();
  } else if (en[1].on) {
    if (p->start2) { // the DMA first, the FIFO onto the DAC 10 us later
      mix_w(0x78, (u8)(r78_run & ~0x01));
      pass_time(10);
    }
    mix_w(0x78, r78_run);
  } else if (en[0].on) {
    if (p->b2late)
      b2_dma(0);
    reg_w(0xB8, (u8)(b8_run | 1));
    if (p->b2late) {
      pass_time(p->b2late);
      b2_dma(1);
    }
  }
  if (p->after) {
    if (p->late)
      pass_time(p->late);
    eng_masks(0);
  }
  if (p->fifo == 2 && en[0].on) {
    pass_time(20000);
    reg_w(0xB7, (u8)(reg_r(0xB7) | 0x80));
  }
}

// the engines stop, their channels masked, the chip reset
static void eng_stop(void) {
  if (en[0].on)
    reg_w(0xB8, 0x00);
  if (en[1].on)
    mix_w(0x78, 0x00);
  if (en[0].on)
    dma_mask(en[0].ch, 1);
  if (en[1].on)
    dma_mask(en[1].ch, 1);
  chip_reset(3);
}

// the counts sampled: each engine's units moved, counted from the first
// sample after settle, once the FIFOs had their fill
static u32 settle;

static void eng_sample(u32 t0) {
  struct eng *e;
  u16 c, pos, lastpos;
  u32 t = H->now();
  int i;

  for (i = 0; i < 2; i++) {
    e = &en[i];
    if (!e->on || e->units == 0)
      continue;
    c = dma_left(e->ch);
    if (c >= e->units)
      c = (u16)(e->units - 1); // a count beyond the buffer: as at its end
    pos = (u16)(e->units - 1 - c);
    lastpos = (u16)(e->units - 1 - e->last);
    e->moved += (u16)((pos + e->units - lastpos) % e->units);
    e->last = c;
    if (!e->ta && since(t0) >= settle) {
      e->ta = e->tb = t;
      e->ma = e->mb = e->moved;
    } else if (e->ta && e->moved != e->mb) {
      // a stop of 20 ms or more (a pause, a block's end) isn't the rate's
      if (((t - e->tb) & 0xFFFFFFFFUL) >= tk(20000))
        e->stall += (t - e->tb) & 0xFFFFFFFFUL;
      e->tb = t;
      e->mb = e->moved;
    }
  }
}

static void eng_first_counts(void) {
  int i;

  for (i = 0; i < 2; i++)
    if (en[i].on)
      en[i].last = (u16)(en[i].units - 1);
}

// an engine's rate as the DMA measured it, in frames a second; 0 if it
// didn't move
static double eng_rate(const struct eng *e) {
  u32 us = us_of((e->tb - e->ta - e->stall) & 0xFFFFFFFFUL);

  if (!e->ta || us < 20000 || e->mb == e->ma)
    return 0;
  return (double)(e->mb - e->ma) * e->per_unit / frame_bytes(e->fmt) * 1e6 / us;
}

// --- the recording -----------------------------------------------------------

// Audio 1 plays n bytes at BUF1 once, 8-bit mono at 22050 Hz: a marker,
// after 200 ms of quiet (50 in the short sounds); the clock as it began
static u32 marker(const char *syms) {
  u8 a1, a2;
  u16 n, cnt;
  u32 t0;
  double rate;

  pass_time((O & ESP_O_TINY) ? 50000UL : 200000UL);
  chip_reset(3);
  ext_on();
  rate = a1_regs(22050, 1, &a1, &a2);
  n = snd_dtmf(mem_poke, C->area + BUF1, syms, rate,
               (O & ESP_O_TINY) ? 662 : 1323);
  dma_set(dma1, C->area + BUF1, n, DMA_SINGLE | DMA_READ, 1);
  reg_w(0xB8, 0x00);
  reg_w(0xB9, 0x02);
  reg_w(0xA8, (u8)((reg_r(0xA8) & 0xFC) | 2));
  reg_w(0xA1, a1);
  reg_w(0xA2, a2);
  cnt = (u16)(0 - n);
  reg_w(0xA4, (u8)cnt);
  reg_w(0xA5, (u8)(cnt >> 8));
  reg_w(0xB6, 0x80);
  reg_w(0xB7, 0x51);
  reg_w(0xB7, 0xD0);
  reg_w(0xB1, (u8)((reg_r(0xB1) | 0x10) & ~0x40));
  reg_w(0xB2, (u8)(reg_r(0xB2) | 0x50));
  mix_w(0x14, 0xCC);
  dsp_out(0xD1);
  t0 = H->now();
  reg_w(0xB8, 0x01);
  pass_time((u32)(n * 1e6 / rate) + 30000UL);
  reg_w(0xB8, 0x00);
  dma_mask(dma1, 1);
  chip_reset(3);
  return t0;
}

static u32 rec_us(u32 t) { return us_of((t - rec_t0) & 0xFFFFFFFFUL); }

// the recorded run's start: a full-scale tone until the person, who sets
// the recorder's level by it, presses Enter; then the start marker
static void record_start(void) {
  struct pc p;
  int a;

  section("record", "a recorded run: set the recorder's level");
  spec_base(&p, "a1.");
  p.fmt1 = 0;
  p.rate1 = 22050;
  p.b9 = 2;
  p.irq1 = 0;
  chip_reset(3);
  ext_on();
  plan(&p);
  fill_tone(&en[0], (u16)SND_HZ, 0, 32767);
  set_71(&p);
  a1_program(&p, &en[0]);
  eng_dma(&p, &en[0], 1);
  eng_start(&p);
  a = H->ask("r.level",
             "A full-scale tone plays: set the recorder's level\n just under "
             "clipping, start recording, and press Enter",
             "\r");
  eng_stop();
  if (a != '\r') {
    stop_now = 1;
    res("rec.level", R_SKIP, "stopped");
    return;
  }
  rec_t0 = marker("ABCD");
  res("rec.start", R_INFO,
      "marker ABCD at 0 us; the level tone was %u Hz, 8-bit mono at "
      "22050 Hz, full scale",
      (unsigned)SND_HZ);
}

static void record_end(void) {
  u32 t = marker("DCBA");

  res("rec.end", R_INFO, "marker DCBA at %lu us", (unsigned long)rec_us(t));
}

// a sound's marker, * its number in three digits #, in a recorded run;
// the clock as it began, or 0
static u32 sound_marker(char *syms) {
  sound_no++;
  sprintf(syms, "*%03u#", sound_no);
  if (!(O & ESP_O_RECORD))
    return 0;
  return marker(syms);
}

// a sound's recorded line: its marker, its parts, what it should hold
static void sound_line(const char *id, const char *syms, u32 tm,
                       const u32 *parts, u16 n, const char *expect) {
  static char p[64];
  char *q = p;
  u16 i;

  if (!(O & ESP_O_RECORD))
    return;
  p[0] = 0;
  for (i = 0; i + 1 < n && q < p + sizeof(p) - 24; i += 2)
    q += sprintf(q, " %lu-%lu", (unsigned long)rec_us(parts[i]),
                 (unsigned long)rec_us(parts[i + 1]));
  res(id, R_INFO, SND_RECORDED, syms, (unsigned long)rec_us(tm), p, expect);
}

// --- playing -----------------------------------------------------------------

// the pause (on) or its end; with hold, the end comes with the engines'
// channels masked, unmasked late us after it (100 without late); 0 if the
// pause can't be made. B7h bit 7 takes the FIFO off the DAC, so that it
// fills and the DMA stops when it is full
static int pause_on(const struct pc *p, int on) {
  int ok = 1, r;

  if (!on && p->hold)
    eng_masks(1);
  switch (p->pause) {
  case P_B2:
    ok = b2_dma(!on);
    break;
  case P_B8:
    reg_w(0xB8, (u8)(on ? b8_run : b8_run | 1));
    break;
  case P_B7:
    r = reg_r(0xB7);
    if (r < 0)
      ok = 0;
    else
      reg_w(0xB7, (u8)(on ? r & ~0x80 : r | 0x80));
    break;
  case P_78F:
    mix_w(0x78, (u8)(on ? r78_run & ~0x01 : r78_run));
    break;
  case P_78D:
    mix_w(0x78, (u8)(on ? r78_run & ~0x02 : r78_run));
    break;
  case P_78FD:
    mix_w(0x78, (u8)(on ? r78_run & ~0x03 : r78_run));
    break;
  case P_D0:
    ok = dsp_out((u8)(on ? 0xD0 : 0xD4)) == 0;
    break;
  default:
    ok = 0;
  }
  if (!on && p->hold) {
    pass_time(p->late ? p->late : 100);
    eng_masks(0);
  }
  return ok;
}

// the engines run for the case's time, sampled; with a pause in the
// middle; parts gets when the sound played (on, off, ...); the parts
static u16 eng_run(const struct pc *p, u32 *parts) {
  u32 t0, ms = p->ms, half, tp = 0;
  u16 n = 0;
  int i, paused = 0, done_pause = 0;
  u32 pause_us = (O & ESP_O_TINY) ? 60000UL : 300000UL;

  if ((O & ESP_O_TINY) && !p->once)
    ms = ms / 8 < 80 ? 80 : ms / 8;
  settle = tk(p->once ? 10000UL : 60000UL);
  half = tk(ms * 500UL);
  eng_first_counts();
  irq_clear();
  t0 = H->now();
  parts[n++] = t0;
  eng_start(p);
  while (since(t0) < tk(ms * 1000UL) + (done_pause ? tk(pause_us) : 0)) {
    eng_sample(t0);
    if (p->pause && !done_pause && !paused && since(t0) >= half) {
      if (pause_on(p, 1)) {
        paused = 1;
        tp = H->now();
        parts[n++] = tp;
        for (i = 0; i < 2; i++)
          en[i].pause_at = en[i].moved;
      } else
        done_pause = 1;
    }
    if (paused && since(tp) >= tk(pause_us)) {
      // what moved after the FIFOs had their fill, the last 3/4 of it
      for (i = 0; i < 2; i++)
        en[i].in_pause = en[i].moved - en[i].pause_at;
      pause_on(p, 0);
      paused = 0;
      done_pause = 1;
      parts[n++] = H->now();
    }
    if (!pass_time(1000))
      break;
  }
  parts[n++] = H->now();
  return n;
}

// an engine's results into the line
static int eng_text(char *q, const char *name, const struct eng *e, u16 nirq,
                    u32 ta, u32 tb, const struct pc *p) {
  double got = eng_rate(e), ms = irq_ms(nirq, ta, tb);
  int ok = got > 0 && fabs(got - e->rate) <= e->rate * 0.03;

  sprintf(q, "; %s ch %u %.0f Hz set, %.0f measured, %u irq%s", name, e->ch,
          e->rate, got, nirq, nirq == 1 ? "" : "s");
  if (ms > 0)
    sprintf(q + strlen(q), " every %.1f ms", ms);
  if (p->pause) {
    sprintf(q + strlen(q), ", %lu moved in the pause",
            (unsigned long)e->in_pause);
    if (e->in_pause > 512)
      ok = 0;
  }
  return ok;
}

static void run_play(const struct ecase *c, const struct pc *spec) {
  static char syms[8], line[200];
  struct pc q = *spec, *p = &q;
  u32 parts[6], tm;
  u16 np;
  int ok = 1, any = !strcmp(c->expect, "any");
  double t;

  if (p->eng & E2) {
    if (dma2 >= 8 || dma2 == 4 || dma2 == 2) {
      res(c->id, R_SKIP, "Audio 2 has no channel (PnP 75h %u)", dma2);
      return;
    }
    if ((p->packed || p->wide) && dma2 < 4) {
      res(c->id, R_SKIP,
          "Audio 2 is on the 8-bit channel %u, as the card is configured",
          dma2);
      return;
    }
  }
  tm = sound_marker(syms);
  chip_reset(3);
  ext_on();
  plan(p);
  if (en[0].on)
    fill_tone(&en[0], p->hz1, p->side, 12000);
  if (en[1].on)
    fill_tone(&en[1], p->hz2, p->side, 12000);
  if (p->once) { // one block: as long as the longer of the buffers
    t = 0;
    if (en[0].on)
      t = en[0].frames / en[0].rate;
    if (en[1].on && en[1].frames / en[1].rate > t)
      t = en[1].frames / en[1].rate;
    p->ms = (u16)(t * 1000 + 10);
  }
  if (p->slaved && !en[0].on) {
    reg_w(0xA1, en[0].r1);
    reg_w(0xA2, en[0].r2);
  }
  set_71(p);
  ack1 = (u8)!p->noack;
  ack2 = (u8)!p->noack;
  if (en[0].on) {
    a1_program(p, &en[0]);
    eng_dma(p, &en[0], !p->after);
  }
  if (en[1].on) {
    a2_program(p, &en[1]);
    eng_dma(p, &en[1], !p->after);
  }
  np = eng_run(p, parts);
  eng_stop();
  line[0] = 0;
  if (en[0].on)
    ok &= eng_text(line, "a1", &en[0], n1, t1a, t1b, p);
  if (en[1].on)
    ok &= eng_text(line + strlen(line), "a2", &en[1], n2, t2a, t2b, p);
  res(c->id,
      any  ? R_INFO
      : ok ? R_PASS
           : R_FAIL,
      "%s", line[0] ? line + 2 : "nothing ran");
  put("%-20s spec  %s", c->id, c->spec);
  sound_line(c->id, syms, tm, parts, np, c->expect);
}

// --- the card ----------------------------------------------------------------

// the chip at B: its reset's AAh, the mixer's 40h sequence (18h 69h and
// Config_Base, DS p.28), and from its PnP registers the interrupt, both
// channels and the DMA pins
static int find_card(void) {
  u8 id[4];
  int i, v;
  u16 base;

  if (!chip_reset(1))
    return 0;
  H->lock();
  H->outb((u16)(B + 4), 0x40);
  for (i = 0; i < 4; i++)
    id[i] = H->inb((u16)(B + 5));
  H->unlock();
  if (id[0] != 0x18 || id[1] != 0x69)
    return 0;
  base = (u16)((id[2] << 8) | id[3]);
  cfg = (base >= 0x100 && base <= 0xFF8 && !(base & 7)) ? base : 0;
  irq = 5;
  dma1 = 1;
  dma2 = 4;
  memset(pins, 0xFF, sizeof(pins));
  if (cfg) {
    v = pnp_r(1, 0x70);
    irq = (u8)(v & 0x0F);
    dma1 = (u8)(pnp_r(1, 0x74) & 0x07);
    dma2 = (u8)(pnp_r(1, 0x75) & 0x07);
    for (i = 0; i < 2; i++) {
      v = pnp_r(0xFF, (u8)(0x23 + i));
      pins[2 * i] = (u8)(v & 0x0F);
      pins[2 * i + 1] = (u8)((v >> 4) & 0x0F);
    }
  }
  return 1;
}

static void run_machine(const struct ecase *c) {
  const char *const *e;
  u16 n = 0;

  for (e = C->env; e != NULL && *e != NULL; e++, n++)
    res(c->id, R_INFO, "%s", *e);
  if (!n)
    res(c->id, R_INFO, "no lines about the machine");
}

static void run_card(const struct ecase *c) {
  static const u8 card_regs[] = {0x20, 0x21, 0x22, 0x23, 0x24,
                                 0x25, 0x26, 0x27, 0x28};
  static const u8 ldn_regs[] = {0x30, 0x60, 0x61, 0x62, 0x63, 0x70,
                                0x71, 0x72, 0x73, 0x74, 0x75};
  int e7a, e7b, va, vb;
  char *q;
  u16 i;

  chip_reset(1);
  dsp_out(0xE7);
  e7a = dsp_in(20000);
  e7b = dsp_in(20000);
  dsp_out(0xE1);
  va = dsp_in(20000);
  vb = dsp_in(20000);
  res(c->id, R_INFO, "E7h %02X %02X, E1h %02X %02X, Config_Base %Xh", e7a, e7b,
      va, vb, cfg);
  res(c->id, R_INFO,
      "audio IRQ %u, Audio 1 DMA %u, Audio 2 DMA %u, DMA pins wired to %u %u "
      "%u %u (15: none)",
      irq, dma1, dma2, pins[0], pins[1], pins[2], pins[3]);
  if (!cfg)
    return;
  q = text + sprintf(text, "%-20s %-5s card:", c->id, "INFO");
  for (i = 0; i < sizeof(card_regs); i++)
    q += sprintf(q, " %02X=%02X", card_regs[i], pnp_r(0xFF, card_regs[i]));
  H->line(text);
  counts[R_INFO]++;
  q = text + sprintf(text, "%-20s %-5s ldn1:", c->id, "INFO");
  for (i = 0; i < sizeof(ldn_regs); i++)
    q += sprintf(q, " %02X=%02X", ldn_regs[i], pnp_r(1, ldn_regs[i]));
  q += sprintf(q, " config +5=%02X +6=%02X +7=%02X", cfg_r(5), cfg_r(6),
               cfg_r(7));
  H->line(text);
  counts[R_INFO]++;
}

// the mixer's registers and the controller's as the program found them
static void run_mixer(const struct ecase *c) {
  char *q;
  int i, n, v;

  for (i = 0; i < 0x80; i += 0x20) {
    q = text + sprintf(text, "%-20s %-5s mixer %02X:", c->id, "INFO", i);
    for (n = i; n < i + 0x20; n++)
      q += n == 0x40 ? sprintf(q, " id") : sprintf(q, " %02X", mix_r((u8)n));
    H->line(text);
    counts[R_INFO]++;
  }
  chip_reset(3);
  ext_on();
  q = text + sprintf(text, "%-20s %-5s after a reset, A0-BF:", c->id, "INFO");
  for (i = 0xA0; i <= 0xBF; i++) {
    v = reg_r((u8)i);
    q += v < 0 ? sprintf(q, " --") : sprintf(q, " %02X", v);
  }
  H->line(text);
  counts[R_INFO]++;
  chip_reset(3);
}

// the DMA controllers as found: the status registers, what the mask
// registers read, the channels' registers as a program reads them and,
// through the window, as they are
static void run_dma(const struct ecase *c) {
  static const u8 chans[] = {0, 1, 3, 5, 6, 7};
  u16 cnt, addr, pc, pa, i;
  u8 page;
  char *q;

  res(c->id, R_INFO,
      "status 08h=%02X D0h=%02X, 0Fh reads %02X, DEh %02X (the mask where "
      "a chipset gives it), window: %s",
      H->inb(0x08), H->inb(0xD0), H->inb(0x0F), H->inb(0xDE),
      window ? "yes" : "none");
  q = text + sprintf(text, "%-20s %-5s", c->id, "INFO");
  for (i = 0; i < sizeof(chans); i++) {
    dma_read(chans[i], &cnt, &addr);
    q += sprintf(q, " ch%u %04X/%04X p%02X", chans[i], addr, cnt,
                 H->inb(page_port[chans[i]]));
    if (dma_phys(chans[i], &pc, &pa, &page) && (pc != cnt || pa != addr))
      q += sprintf(q, " (8237 %04X/%04X p%02X)", pa, pc, page);
  }
  H->line(text);
  counts[R_INFO]++;
}

// --- the resets --------------------------------------------------------------

// the registers a reset may clear, with the values set before it
static const u8 rst_mix[] = {0x70, 0x71, 0x72, 0x74, 0x76, 0x78, 0x7A, 0x7C,
                             0x14, 0x36, 0x38, 0x3A, 0x3C, 0x3E, 0x50, 0x52};
static const u8 rst_mix_v[] = {0x6E, 0x22, 0xF4, 0x34, 0xF8, 0x00, 0x47, 0xA5,
                               0x99, 0x77, 0x66, 0x55, 0x44, 0x33, 0x00, 0x00};
static const u8 rst_ctl[] = {0xA1, 0xA2, 0xA4, 0xA5, 0xA8,
                             0xB1, 0xB2, 0xB6, 0xB7, 0xB9};
static const u8 rst_ctl_v[] = {0x6E, 0xF6, 0x12, 0xF3, 0x01,
                               0x50, 0x50, 0x80, 0x51, 0x01};

static void run_rst(const struct ecase *c, const struct pc *p) {
  static u8 before[sizeof(rst_mix)], after[sizeof(rst_mix)];
  static int cb[sizeof(rst_ctl)], ca[sizeof(rst_ctl)];
  char *q, *m;
  u16 i;
  int ext;

  chip_reset(3);
  ext_on();
  for (i = 0; i < sizeof(rst_mix); i++)
    mix_w(rst_mix[i], rst_mix_v[i]);
  for (i = 0; i < sizeof(rst_ctl); i++)
    reg_w(rst_ctl[i], rst_ctl_v[i]);
  for (i = 0; i < sizeof(rst_mix); i++)
    before[i] = mix_r(rst_mix[i]);
  for (i = 0; i < sizeof(rst_ctl); i++)
    cb[i] = reg_r(rst_ctl[i]);
  if (p->kind == 0) {
    // a mixer reset sets every level: all of them go to the restore list
    for (i = 1; i < 0x80; i++)
      if (i != 0x40 && !kept(ESP_K_MIXER, (u8)i)) {
        u8 old = mix_r((u8)i);
        keep_add(ESP_K_MIXER, (u8)i, &old, 1);
      }
    mix_w(0x00, 0x00);
  } else if (p->kind == 2)
    fifo_reset();
  else
    chip_reset(p->kind);
  for (i = 0; i < sizeof(rst_mix); i++)
    after[i] = mix_r(rst_mix[i]);
  // the controller's registers: C6h again, since a register command in
  // Compatibility mode is undefined; the reset's AAh says whether it was
  // one (+6 bit 0)
  ext = p->kind == 0 || p->kind == 2;
  ext_on();
  for (i = 0; i < sizeof(rst_ctl); i++)
    ca[i] = reg_r(rst_ctl[i]);
  q = text + sprintf(text, "%-20s %-5s %s; mixer", c->id, "INFO",
                     ext ? "no reset of the DSP" : "Compatibility mode");
  m = q;
  for (i = 0; i < sizeof(rst_mix); i++)
    if (after[i] != before[i])
      q += sprintf(q, " %02X:%02X>%02X", rst_mix[i], before[i], after[i]);
  if (q == m)
    q += sprintf(q, " kept");
  q += sprintf(q, "; regs");
  m = q;
  for (i = 0; i < sizeof(rst_ctl); i++)
    if (ca[i] != cb[i])
      q +=
          sprintf(q, " %02X:%02X>%02X", rst_ctl[i], cb[i] & 0xFF, ca[i] & 0xFF);
  if (q == m)
    sprintf(q, " kept");
  H->line(text);
  counts[R_INFO]++;
  chip_reset(3);
  for (i = 0; i < sizeof(rst_mix); i++)
    if (rst_mix[i] >= 0x70 && rst_mix[i] <= 0x7C)
      mix_w(rst_mix[i], 0x00);
}

// --- the DMA controllers behind the memory manager ---------------------------

// the card's channel for a case's: Audio 1's for an 8-bit one, Audio 2's
// for a 16-bit one where it is one; 0xFF without. The cases use the
// channels the card is configured for (PnP 74h and 75h), never another
// device's
static u8 card_ch(u8 ch) {
  if (ch < 4)
    return dma1;
  return (u8)(dma2 >= 5 && dma2 <= 7 ? dma2 : 0xFF);
}

static int no_ch(const struct ecase *c, u8 ch) {
  if (ch != 0xFF)
    return 0;
  res(c->id, R_SKIP, "the card has no 16-bit channel (PnP 75h %u)", dma2);
  return 1;
}

static const char *reach_word(int at_write, int at_unmask) {
  return at_write ? "at once" : at_unmask ? "at the unmask" : "not at all";
}

// a program programs the channel while it is masked, then unmasks it:
// when the 8237 gets it, and what the program reads back meanwhile
static void run_reach(const struct ecase *c, const struct pc *p) {
  u8 ch = card_ch(p->ch), page, page2;
  u32 addr = C->area + BUF2;
  u16 want_a, want_c, a0, c0, a1, c1, a2, c2, va, vc;
  int w, u;

  if (no_ch(c, ch))
    return;
  if (!window) {
    dma_mask(ch, 1);
    dma_set(ch, addr, 0x200, DMA_SINGLE | DMA_AUTO | DMA_READ, 0);
    dma_read(ch, &vc, &va);
    res(c->id, R_SKIP, "no window on the 8237; reads back %04X/%04X", va, vc);
    return;
  }
  want_a = (u16)(ch >= 4 ? (addr >> 1) & 0xFFFF : addr & 0xFFFF);
  want_c = (u16)(ch >= 4 ? 0xFF : 0x1FF);
  dma_mask(ch, 1);
  dma_phys(ch, &c0, &a0, &page);
  dma_set(ch, addr, 0x200, DMA_SINGLE | DMA_AUTO | DMA_READ, 0);
  dma_phys(ch, &c1, &a1, &page2);
  dma_read(ch, &vc, &va);
  dma_mask(ch, 0);
  dma_phys(ch, &c2, &a2, &page2);
  dma_mask(ch, 1);
  w = a1 == want_a && c1 == want_c;
  u = a2 == want_a && c2 == want_c;
  res(c->id, R_INFO,
      "programmed %04X/%04X masked: the 8237 had %04X/%04X, then %04X/%04X, "
      "%04X/%04X after the unmask (page %02X): it gets it %s; the program "
      "reads %04X/%04X",
      want_a, want_c, a0, c0, a1, c1, a2, c2, page2, reach_word(w, u), va, vc);
}

// a transfer moves the channel: what a program reads, against the 8237
static void run_read(const struct ecase *c, const struct pc *p) {
  struct pc q = *p;
  u16 vc = 0, va = 0, pc, pa;
  int i, same = 0, n = 0;

  q.ch = card_ch(p->ch);
  if (no_ch(c, q.ch))
    return;
  q.eng = (u8)(q.ch == dma1 ? E1 : E2);
  q.ms = 300;
  chip_reset(3);
  ext_on();
  plan(&q);
  set_71(&q);
  if (en[0].on) {
    fill_tone(&en[0], 441, 0, 0);
    a1_program(&q, &en[0]);
    eng_dma(&q, &en[0], 1);
  }
  if (en[1].on) {
    fill_tone(&en[1], 441, 0, 0);
    a2_program(&q, &en[1]);
    eng_dma(&q, &en[1], 1);
  }
  eng_start(&q);
  pass_time(30000);
  for (i = 0; i < 16; i++) {
    dma_read(q.ch, &vc, &va);
    if (!dma_phys(q.ch, &pc, &pa, NULL))
      break;
    n++;
    if ((u16)(pc - vc) <= 0x20 || (u16)(vc - pc) <= 0x20)
      same++;
    pass_time(3000);
  }
  eng_stop();
  if (!n)
    res(c->id, R_SKIP, "no window on the 8237; the program read %04X/%04X", va,
        vc);
  else
    res(c->id, R_INFO,
        "while it moves, %d of %d reads agree with the 8237 (the last %04X "
        "against %04X): the program reads %s",
        same, n, vc, pc, same == n ? "the 8237" : "something else");
}

// the flip-flop: the window sets the count to 1234h and clears it; a
// program then reads one byte of the count, or writes one of the address;
// the window reads one byte without clearing it again: the high byte if
// the program's access toggled the 8237's flip-flop
static u8 ff_after(u8 ch, int write) {
  static struct esp_io io[8];
  u16 n = 0;

  io_add(io, &n, ESP_OUT, ff_port(ch), 0);
  io_add(io, &n, ESP_OUT, count_port(ch), 0x34);
  io_add(io, &n, ESP_OUT, count_port(ch), 0x12);
  io_add(io, &n, ESP_OUT, ff_port(ch), 0);
  H->phys(io, n);
  H->outb(ff_port(ch), 0);
  if (write)
    H->outb(addr_port(ch), 0x00);
  else
    H->inb(count_port(ch));
  n = 0;
  io_add(io, &n, ESP_IN, count_port(ch), 0);
  H->phys(io, n);
  return io[0].value;
}

static const char *ff_word(u8 v) {
  return v == 0x12   ? "the 8237's"
         : v == 0x34 ? "the memory manager's own"
                     : "neither";
}

static void run_ff(const struct ecase *c, const struct pc *p) {
  u8 ch = card_ch(p->ch), r, w;

  if (no_ch(c, ch))
    return;
  if (!window) {
    res(c->id, R_SKIP, "no window on the 8237");
    return;
  }
  dma_mask(ch, 1);
  r = ff_after(ch, 0);
  w = ff_after(ch, 1);
  res(c->id, R_INFO,
      "count 1234h, the 8237 then gave %02X after a read, %02X after a "
      "write: a read toggles %s flip-flop, a write %s",
      r, w, r == 0x12 ? "the 8237's" : ff_word(r), ff_word(w));
}

// a buffer across a page: the memory manager puts its own buffer there;
// under one, the channel isn't auto-initialized, because EMM386 stops the
// machine at an auto-initialized transfer that it would have to buffer
// ("DMA mode not supported")
static void run_cross(const struct ecase *c, const struct pc *p) {
  u8 ch = card_ch(p->ch), page;
  u8 mode = (u8)(C->flags & ESP_F_EMM386 ? DMA_SINGLE | DMA_READ
                                         : DMA_SINGLE | DMA_AUTO | DMA_READ);
  u32 addr = C->area + 0xFF80UL;
  u16 pc, pa;

  if (no_ch(c, ch))
    return;
  // the 16-bit channel's pages are 128 KB: the area's end, or its start
  if (ch >= 4)
    addr = (C->area + 0x10000UL) & 0x1FFFFUL ? C->area - 0x100UL
                                             : C->area + 0xFF00UL;

  dma_set(ch, addr, 0x200, mode, 0);
  dma_mask(ch, 0);
  if (!dma_phys(ch, &pc, &pa, &page)) {
    dma_mask(ch, 1);
    res(c->id, R_SKIP, "no window on the 8237");
    return;
  }
  dma_mask(ch, 1);
  res(c->id, R_INFO,
      "512 bytes at %05lXh, across %s, %s: the 8237 got %04X page %02X, "
      "count %04X (%s)",
      (unsigned long)addr, ch >= 4 ? "a 128 KB page" : "a 64 KB page",
      C->flags & ESP_F_EMM386 ? "single-cycle" : "auto-initialized", pa, page,
      pc,
      (ch >= 4 ? (u32)page << 16 | (u32)pa << 1 : (u32)page << 16 | pa) == addr
          ? "the program's buffer"
          : "a buffer of the memory manager's");
}

// esssb16 under EMM386: a game programs its 16-bit channel while it is
// masked and sends the command; the front carries the channel over to
// Audio 1's from the 8237 as it has it then (or after the unmask), Audio
// 1 plays; the game unmasks after
static void run_stale(const struct ecase *c, const struct pc *p) {
  static char syms[8];
  struct pc q = *p;
  u32 parts[6], tm;
  u16 cnt, addr, np;
  u8 page, gch = card_ch(5);
  u32 from;

  if (no_ch(c, gch))
    return;
  spec_base(&q, "a1.");
  q.after = 0;
  tm = sound_marker(syms);
  chip_reset(3);
  ext_on();
  plan(&q);
  en[0].addr = C->area + BUF2;
  fill_tone(&en[0], (u16)SND_HZ, 0, 12000);
  // the game's channel 5, masked
  dma_set(gch, en[0].addr, en[0].sbytes, DMA_SINGLE | DMA_AUTO | DMA_READ, 0);
  if (p->after)
    dma_mask(gch, 0);
  if (!dma_phys(gch, &cnt, &addr, &page)) {
    dma_read(gch, &cnt, &addr);
    page = H->inb(page_port[gch]);
  }
  from = ((u32)page << 16) | ((u32)addr << 1);
  set_71(&q);
  a1_program(&q, &en[0]);
  dma_set(dma1, from, (u32)(cnt + 1) * 2, DMA_SINGLE | DMA_AUTO | DMA_READ, 1);
  np = eng_run(&q, parts);
  eng_stop();
  dma_mask(gch, 1);
  res(c->id, R_INFO,
      "the game's buffer at %05lXh; carried over %s the unmask: %05lXh, "
      "%lu bytes (%s)",
      (unsigned long)en[0].addr, p->after ? "after" : "before",
      (unsigned long)from, (unsigned long)(cnt + 1) * 2,
      from == en[0].addr && (u32)(cnt + 1) * 2 == en[0].sbytes ? "the game's"
                                                               : "stale");
  put("%-20s spec  %s", c->id, c->spec);
  sound_line(c->id, syms, tm, parts, np, c->expect);
}

// --- what the engines do to the machine --------------------------------------

// a busy loop's rounds in 20 ms
static u32 cpu_rounds(void) {
  static volatile u16 x;
  u32 t0 = H->now(), n = 0, lim = tk(20000);
  u16 k;

  while (since(t0) < lim) {
    for (k = 0; k < 32; k++)
      x++;
    n++;
    H->idle();
  }
  return n;
}

// the VGA's vertical retrace: its period and the most a period was off,
// in us, over up to 10 periods; 0 without one
static u32 retrace(u32 *worst) {
  u32 t0 = H->now(), lim = tk(200000), last = 0, sum = 0, d, mean;
  static u32 per[10];
  u16 n = 0, i;
  u8 was = 1, now;

  *worst = 0;
  while (since(t0) < lim && n < 10) {
    now = (u8)(H->inb(0x3DA) & 0x08);
    if (now && !was) {
      if (last)
        per[n++] = us_of((H->now() - last) & 0xFFFFFFFFUL);
      last = H->now();
    }
    was = now;
    H->idle();
  }
  if (!n)
    return 0;
  for (i = 0; i < n; i++)
    sum += per[i];
  mean = sum / n;
  for (i = 0; i < n; i++) {
    d = per[i] > mean ? per[i] - mean : mean - per[i];
    if (d > *worst)
      *worst = d;
  }
  return mean;
}

// the pattern in the VGA DAC's entries 192-255, which text modes don't use
static void dac_want(u16 i, u8 *rgb) {
  u16 c;

  for (c = 0; c < 3; c++)
    rgb[c] = (u8)((i * 7 + c * 13) & 0x3F);
}

static void run_load(const struct ecase *c, const struct pc *p) {
  static char syms[8];
  static u8 dac0[256][3];
  struct pc q = *p;
  u32 parts[6], tm, idle, busy, per, worst, first;
  u16 np = 0, i, bad_dac = 0, bad;
  u8 rgb[3], want[3];
  static u32 idle_rounds;

  if (p->eng == 0) {
    idle_rounds = cpu_rounds();
    per = retrace(&worst);
    res(c->id, R_INFO,
        "%lu rounds in 20 ms; the retrace every %lu us, off by %lu at most",
        (unsigned long)idle_rounds, (unsigned long)per, (unsigned long)worst);
    return;
  }
  if (q.eng & E1)
    q.hz1 = (u16)SND_HZ;
  if (q.eng & E2)
    q.hz2 = q.eng & E1 ? 882 : (u16)SND_HZ;
  for (i = 0; i < 256; i++)
    dac_get((u8)i, dac0[i]);
  for (i = 192; i < 256; i++) {
    keep_add(ESP_K_DAC, (u8)i, dac0[i], 3);
    dac_want(i, want);
    dac_put((u8)i, want);
  }
  guards_set();
  if (q.wrong)
    snd_fill_byte(mem_poke, C->area + SCR, GUARD, 0x5A);
  tm = sound_marker(syms);
  chip_reset(3);
  ext_on();
  plan(&q);
  if (en[0].on)
    fill_tone(&en[0], q.hz1, 0, 12000);
  if (en[1].on)
    fill_tone(&en[1], q.hz2, 0, 12000);
  set_71(&q);
  if (en[0].on) {
    a1_program(&q, &en[0]);
    eng_dma(&q, &en[0], 1);
  }
  if (en[1].on) {
    a2_program(&q, &en[1]);
    eng_dma(&q, &en[1], 1);
  }
  irq_clear();
  eng_first_counts();
  parts[np++] = H->now();
  eng_start(&q);
  pass_time(60000);
  busy = cpu_rounds();
  per = retrace(&worst);
  pass_time(300000);
  parts[np++] = H->now();
  eng_stop();
  for (i = 0; i < 256; i++) {
    dac_get((u8)i, rgb);
    if (i >= 192)
      dac_want(i, want);
    else
      memcpy(want, dac0[i], 3);
    if (memcmp(rgb, want, 3))
      bad_dac++;
  }
  for (i = 192; i < 256; i++)
    dac_put((u8)i, dac0[i]);
  bad = guards_check(&first);
  idle = idle_rounds ? idle_rounds : busy;
  res(c->id, R_INFO,
      "%lu rounds in 20 ms (%lu%% of idle); the retrace every %lu us, off by "
      "%lu at most; %u DAC entries changed; %u guard bytes changed",
      (unsigned long)busy, (unsigned long)(idle ? busy * 100 / idle : 0),
      (unsigned long)per, (unsigned long)worst, bad_dac, bad);
  if (bad)
    res(c->id, R_INFO, "the first changed guard byte at area+%05lXh",
        (unsigned long)first);
  if (q.wrong) {
    static u8 buf[16];
    H->peek(C->area + SCR, buf, sizeof(buf));
    res(c->id, R_INFO,
        "the channel the wrong way round wrote: %02X %02X %02X %02X %02X "
        "%02X %02X %02X",
        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
  }
  put("%-20s spec  %s", c->id, c->spec);
  sound_line(c->id, syms, tm, parts, np, c->expect);
}

// --- a game's refills behind the count ---------------------------------------

// the glide: a sine that rises from 300 Hz by 400 Hz a second
struct glide {
  u32 ph, inc, dinc;
};

static void glide_init(struct glide *g, double rate) {
  g->ph = 0;
  g->inc = (u32)(300.0 / rate * 4294967296.0);
  g->dinc = (u32)(400.0 / rate / rate * 4294967296.0);
}

static s16 glide_next(struct glide *g) {
  s16 v = (s16)(((long)snd_sine((u16)(g->ph >> 22)) * 12000L) >> 15);

  g->ph = (g->ph + g->inc) & 0xFFFFFFFFUL;
  g->inc = (g->inc + g->dinc) & 0xFFFFFFFFUL;
  return v;
}

// n frames of the glide into the buffer from frame at (mod its frames)
static void glide_put(struct eng *e, struct glide *g, u16 at, u16 n) {
  static u8 buf[64];
  u16 fb = frame_bytes(e->fmt), i, k, c, chans = (e->fmt & FST) ? 2 : 1;
  u16 step = (u16)(e->spread ? 2 : 1), w;
  s16 s;

  while (n--) {
    s = glide_next(g);
    k = 0;
    for (c = 0; c < chans; c++) {
      w = (u16)s;
      if (e->fmt & F16) {
        if (!(e->fmt & FSG))
          w ^= 0x8000;
        buf[k] = (u8)w;
        buf[k + step] = (u8)(w >> 8);
        k = (u16)(k + 2 * step);
      } else {
        buf[k] = (u8)((w >> 8) ^ ((e->fmt & FSG) ? 0 : 0x80));
        k = (u16)(k + step);
      }
    }
    if (e->spread)
      for (i = 1; i < k; i += 2)
        buf[i] = 0;
    H->poke(e->addr + (u32)at * fb * step, buf, k);
    at = (u16)((at + 1) % e->frames);
  }
}

static u16 frames_at(const struct eng *e, u16 count, u8 ch) {
  u16 units = ch >= 4 && !e->spread ? (u16)(e->sbytes / 2) : e->units;
  u16 pos;

  if (count >= units)
    count = (u16)(units - 1);
  pos = (u16)(units - 1 - count);
  return (u16)((u32)pos * e->frames / units);
}

// the game: a buffer of two ticks of 20 ms, and at each tick the frames
// behind the position its channel's count gives filled for their next time
// round. stream.mirror: the game's channel is 5, which Audio 1 plays from
// its own, carried over at the command
static void run_stream(const struct ecase *c, const struct pc *p) {
  static char syms[8];
  struct pc q = *p;
  struct glide g;
  struct eng *e;
  u32 parts[4], tm, t0, ticks = 0, n_ticks, done, pos, last = 0, laps = 0;
  u16 np = 0, cnt, addr, count;
  u8 gch, page;
  int mirror = p->run == RUN_MIRROR;

  if (mirror)
    q.eng = E1;
  e = &en[(q.eng & E2) ? 1 : 0];
  gch = mirror ? card_ch(5) : 0;
  if (mirror && no_ch(c, gch))
    return;
  if ((q.wide || q.packed) && dma2 < 4) {
    res(c->id, R_SKIP,
        "Audio 2 is on the 8-bit channel %u, as the card is configured", dma2);
    return;
  }
  tm = sound_marker(syms);
  chip_reset(3);
  ext_on();
  plan(&q);
  e->frames = (u16)(e->rate * 0.040 + 0.5) & 0xFFFE;
  if (e->frames < 64)
    e->frames = 64;
  e->sbytes = (u16)(e->frames * frame_bytes(e->fmt));
  e->units =
      e->ch >= 4 ? (u16)(e->spread ? e->sbytes : e->sbytes / 2) : e->sbytes;
  if (!mirror)
    gch = e->ch;
  if (mirror)
    e->addr = C->area + BUF2;
  glide_init(&g, e->rate);
  glide_put(e, &g, 0, e->frames);
  set_71(&q);
  if (mirror) {
    // the game's channel 5, masked, as a game programs it; Audio 1's own
    // carried over from it as the 8237 has it, then the game's unmask
    dma_set(gch, e->addr, e->sbytes, DMA_SINGLE | DMA_AUTO | DMA_READ, 0);
    if (!dma_phys(gch, &cnt, &addr, &page)) {
      dma_read(gch, &cnt, &addr);
      page = H->inb(page_port[gch]);
    }
    a1_program(&q, e);
    dma_set(dma1, ((u32)page << 16) | ((u32)addr << 1), (u32)(cnt + 1) * 2,
            DMA_SINGLE | DMA_AUTO | DMA_READ, 1);
  } else if (e == &en[0]) {
    a1_program(&q, e);
    eng_dma(&q, e, 1);
  } else {
    a2_program(&q, e);
    eng_dma(&q, e, 1);
  }
  irq_clear();
  t0 = H->now();
  parts[np++] = t0;
  eng_start(&q);
  if (mirror)
    dma_mask(gch, 0);
  done = e->frames;
  n_ticks = (O & ESP_O_TINY) ? 8 : 75;
  while (ticks < n_ticks && !escaped()) {
    pass_time(20000);
    ticks++;
    dma_read(gch, &count, NULL);
    pos = frames_at(e, count, gch);
    if (pos < last)
      laps++;
    last = pos;
    pos += laps * e->frames;
    if (pos + e->frames > done) {
      glide_put(e, &g, (u16)(done % e->frames), (u16)(pos + e->frames - done));
      done = pos + e->frames;
    }
  }
  parts[np++] = H->now();
  eng_stop();
  if (mirror)
    dma_mask(gch, 1);
  res(c->id, R_INFO,
      "%u frames a lap at %.0f Hz on channel %u; the game mixed %lu frames "
      "in %lu ticks, %lu laps",
      e->frames, e->rate, gch, (unsigned long)(done - e->frames),
      (unsigned long)ticks, (unsigned long)laps);
  put("%-20s spec  %s", c->id, c->spec);
  sound_line(c->id, syms, tm, parts, np, c->expect);
}

// --- the run -----------------------------------------------------------------

static unsigned group_of(const char *id) {
  static const char *const names[] = {"env", "rst", "a1",     "a2", "both",
                                      "h1",  "h4",  "stream", "fix"};
  unsigned i, n;

  for (i = 0; i < ESS_ARRAY_SIZE(names); i++) {
    n = (unsigned)strlen(names[i]);
    if (!strncmp(id, names[i], n) && id[n] == '.')
      return 1U << i;
  }
  return 0;
}

static int wanted(const char *id, const char *only) {
  const char *s = only, *e;
  unsigned n;

  if (only == NULL || !*only)
    return 1;
  while (*s) {
    e = strchr(s, ',');
    n = e ? (unsigned)(e - s) : (unsigned)strlen(s);
    if (n && !strncmp(id, s, n))
      return 1;
    if (!e)
      break;
    s = e + 1;
  }
  return 0;
}

static int has_sound(const struct ecase *c) { return strcmp(c->expect, "-"); }

static void run_case(const struct ecase *c) {
  struct pc p;
  int bad;

  spec_base(&p, c->id);
  bad = spec_parse(&p, c->spec);
  if (bad) {
    res(c->id, R_FAIL, "the spec doesn't parse at %d: %s", bad, c->spec);
    return;
  }
  say(c->id);
  switch (p.run) {
  case RUN_PLAY:
    run_play(c, &p);
    break;
  case RUN_MACHINE:
    run_machine(c);
    break;
  case RUN_CARD:
    run_card(c);
    break;
  case RUN_MIXER:
    run_mixer(c);
    break;
  case RUN_DMA:
    run_dma(c);
    break;
  case RUN_RST:
    run_rst(c, &p);
    break;
  case RUN_REACH:
    run_reach(c, &p);
    break;
  case RUN_READ:
    run_read(c, &p);
    break;
  case RUN_FF:
    run_ff(c, &p);
    break;
  case RUN_CROSS:
    run_cross(c, &p);
    break;
  case RUN_STALE:
    run_stale(c, &p);
    break;
  case RUN_LOAD:
    run_load(c, &p);
    break;
  case RUN_STREAM:
  case RUN_MIRROR:
    run_stream(c, &p);
    break;
  }
}

static void head(unsigned groups, const char *only) {
  put("essprobe %s: the ES1869 itself, without a front", ESP_VERSION);
  put("Send this file back, and the recording of a recorded run. Each line");
  put("is a case: PASS, FAIL, INFO (what was measured), SKIP; spec lines");
  put("give each case's settings.");
  section("probe", "the run");
  put("base %Xh, clock %luHz, area %05lXh, options %04X, groups %04X%s%s",
      C->base, (unsigned long)C->hz, (unsigned long)C->area, O, groups,
      only != NULL && *only ? ", only " : "", only != NULL ? only : "");
}

static void summary(void) {
  u16 i;
  char *q;

  section("summary", "done");
  put("pass=%u fail=%u info=%u skip=%u%s", counts[R_PASS], counts[R_FAIL],
      counts[R_INFO], counts[R_SKIP], stop_now ? " (stopped)" : "");
  if (nfailed) {
    q = text + sprintf(text, "failed:");
    for (i = 0; i < nfailed; i++)
      q += sprintf(q, " %s", failed[i]);
    H->line(text);
  }
}

int esp_run(const struct esp_hal *hal, const struct esp_card *card,
            unsigned opts, unsigned groups, const char *only) {
  static const char *const gname[] = {"env", "rst", "a1",     "a2", "both",
                                      "h1",  "h4",  "stream", "fix"};
  static const char *const gwhat[] = {
      "the machine and the card",
      "what each reset clears",
      "Audio 1 alone",
      "Audio 2 alone",
      "both engines at once",
      "the DMA controllers behind the memory manager",
      "what the engines do to the machine",
      "a game's refills behind the count",
      "which start keeps a 16-bit stream in step"};
  u16 i;
  unsigned g, last_g = 0;
  int v;

  H = hal;
  C = card;
  O = opts;
  B = card->base;
  stop_now = 0;
  memset(counts, 0, sizeof(counts));
  nfailed = 0;
  nkeep = 0;
  sound_no = 0;
  irq_ready = 0;
  window = 0;
  snd_init();
  head(groups, only);
  if (!find_card()) {
    res("card", R_FAIL, "no ES1869 answers at %Xh: is BLASTER's A right?", B);
    summary();
    H = NULL;
    return counts[R_FAIL];
  }
  if (H->phys != NULL) {
    static struct esp_io io[1];
    io[0].kind = ESP_IN, io[0].port = 0x08;
    window = H->phys(io, 1);
  }
  // both engines' interrupts on Audio 1's, unmasked at Config_Base+7
  if (cfg) {
    v = pnp_r(1, 0x72);
    if (v >= 0 && (v & 0x0F) != irq)
      pnp_w(0x72, irq);
    v = cfg_r(7);
    if ((v & 0x03) != 0x03)
      cfg_w(7, (u8)(v | 0x03));
  }
  if (H->hook != NULL)
    H->hook(irq);
  irq_ready = 1;
  if (O & ESP_O_RECORD)
    record_start();
  for (i = 0; i < ESS_ARRAY_SIZE(cases) && !stop_now; i++) {
    const struct ecase *c = &cases[i];
    struct pc p;
    g = group_of(c->id);
    if (!(g & groups) || !wanted(c->id, only))
      continue;
    if ((O & ESP_O_SILENT) && has_sound(c))
      continue;
    spec_base(&p, c->id);
    if (!spec_parse(&p, c->spec) && (O & ESP_O_SHORT) && !p.sh)
      continue;
    if (g != last_g) {
      unsigned k = 0;
      while ((1U << k) != g)
        k++;
      section(gname[k], gwhat[k]);
      last_g = g;
    }
    run_case(c);
  }
  if ((O & ESP_O_RECORD) && !stop_now)
    record_end();
  chip_reset(3);
  irq_ready = 0;
  if (H->hook != NULL)
    H->hook(0);
  restore_all();
  summary();
  H = NULL;
  return counts[R_FAIL];
}
