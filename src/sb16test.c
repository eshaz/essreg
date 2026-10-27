/*
 * sb16test checks the Sound Blaster 16 a DOS game finds: esssb16.com's in
 * DOS, ES1869.VXD's in a Windows DOS box, or a real card. It runs the
 * checks of sb16t.c, plays the listening checks' sounds and asks what you
 * heard, and writes everything to SB16TEST.TXT, which you send back.
 *
 * Usage:
 *   `sb16test [/A] [/L] [/Q] [/O:file]`
 *
 * Notes:
 *
 * The clock, the interrupt handler, the report and the files are
 * dosplat.c's. What the checks change in the mixer goes to SB16TEST.RST
 * first, which the next start puts back if this run didn't finish. /T,
 * for the host tests, runs the checks with short blocks and sounds.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <conio.h>
#include <ctype.h>
#include <dos.h>
#include <fcntl.h>
#include <i86.h>
#include <io.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dosplat.h"
#include "sb16t.h"

#define RST_NAME "SB16TEST.RST"
#define RST_TMP "SB16TEST.$$$"
#define RST_MAGIC "SBTRST1"

static struct sbt_card card;
static struct sbt_hal hal;
static struct plat_blaster blaster;
static char envtext[10][100];
static const char *envp[11];
static int nenv;

static char report_name[80] = "SB16TEST.TXT";
static int h_over = -1; // /H: the 16-bit DMA in place of BLASTER's H

static volatile int stop;

static u16 tsr_seg; // esssb16's PSP, 0 if not resident

// --- the hardware layer ------------------------------------------------------

static u8 dos_inb(u16 port) { return (u8)inp(port); }

static void dos_outb(u16 port, u8 v) { outp(port, v); }

static void dos_idle(void) {}

static void __far *lin(u32 addr) {
  return MK_FP((unsigned)(addr >> 4), (unsigned)(addr & 0x0F));
}

static void dos_poke(u32 addr, const u8 *src, u16 n) {
  _fmemcpy(lin(addr), src, n);
}

static void dos_peek(u32 addr, u8 *dst, u16 n) { _fmemcpy(dst, lin(addr), n); }

static int dos_esc(void) {
  if (plat_key_ready() && plat_key() == 27)
    stop = 1;
  return stop;
}

static int dos_ask(const char *id, const char *question, const char *keys) {
  int k;

  printf("\n%s: %s\n> ", id, question);
  fflush(stdout);
  for (;;) {
    k = tolower(plat_key());
    if (k == 27) {
      stop = 1;
      k = 's';
    }
    if (k && strchr(keys, k) != NULL) {
      printf("%c\n", k);
      fflush(stdout);
      return k;
    }
  }
}

static void dos_sound(u32 rate) {
  if (rate) {
    printf("  (playing)\r");
    fflush(stdout);
  }
}

static void dos_say(const char *text) { printf("%s...\n", text); }

// the restore file: the mixer's registers as they were, written whole as
// SB16TEST.$$$ and renamed over SB16TEST.RST; gone when they are back
static void dos_keep(const u8 *pairs, u16 n) {
  u8 head[12];

  if (n == 0) {
    remove(RST_NAME);
    return;
  }
  memcpy(head, RST_MAGIC, 8);
  head[8] = (u8)card.base;
  head[9] = (u8)(card.base >> 8);
  head[10] = (u8)n;
  head[11] = (u8)(n >> 8);
  plat_save(RST_TMP, RST_NAME, head, 12, pairs, n);
}

// a run that didn't finish left the mixer changed: put it back
static int restore_old(void) {
  static u8 pairs[SBT_KEEP];
  u8 head[12];
  u16 base, n, i;
  int fd = open(RST_NAME, O_RDONLY | O_BINARY);

  if (fd < 0)
    return 0;
  if (read(fd, head, 12) != 12 || memcmp(head, RST_MAGIC, 8) != 0) {
    close(fd);
    return 0;
  }
  base = (u16)(head[8] | (head[9] << 8));
  n = (u16)(head[10] | (head[11] << 8));
  if (n > SBT_KEEP || read(fd, pairs, n) != (int)n) {
    close(fd);
    return 0;
  }
  close(fd);
  for (i = 0; i + 1 < n; i += 2) {
    plat_lock();
    outp(base + 4, pairs[i]);
    outp(base + 5, pairs[i + 1]);
    plat_unlock();
  }
  remove(RST_NAME);
  return 1;
}

// esssb16's variables, from its signature on, for the report
static void dos_state(const char *section) {
  static char buf[200];
  const u8 __far *p;
  char *q;
  int i, part;

  if (!tsr_seg)
    return;
  p = (const u8 __far *)MK_FP(tsr_seg, 0x103);
  for (part = 0; part < 2; part++) {
    q = buf + sprintf(buf, "esssb16 %.12s %d:", section, part);
    for (i = 0; i < 96; i++)
      q += sprintf(q, "%02X", p[part * 96 + i]);
    plat_line(buf);
  }
}

// --- the machine -------------------------------------------------------------

static void env_add(const char *fmt, ...) {
  va_list ap;

  if (nenv >= 10)
    return;
  va_start(ap, fmt);
  _vbprintf(envtext[nenv], sizeof(envtext[0]), fmt, ap);
  va_end(ap);
  envp[nenv] = envtext[nenv];
  nenv++;
  envp[nenv] = NULL;
}

// ES1869.VXD's register API in a DOS box: its version and settings. A
// call of the API is a far call to the entry that int 2Fh 1684h gives,
// with the function in DX; CF set means it failed.
// clang-format off
static int vxd_info(u16 *ver, u16 *feat, u16 *sett) {
  static u8 adi[256];
  union REGS r;
  struct SREGS sr;
  u16 eseg, eoff, aseg, aoff, a = 0, b = 0, c = 0, fl = 1, dlo, dhi;

  segread(&sr);
  r.x.ax = 0x1684;
  r.x.bx = 0x3B07;
  r.x.di = 0;
  sr.es = 0;
  int86x(0x2F, &r, &r, &sr);
  if (sr.es == 0 && r.x.di == 0)
    return 0;
  eseg = sr.es;
  eoff = r.x.di;
  aseg = FP_SEG((void __far *)adi);
  aoff = FP_OFF((void __far *)adi);
  // 0001: the first device's structure, as many bytes of it as the
  // buffer's first dword says, with its devnode at 55h
  adi[0] = 0xE9;
  adi[1] = adi[2] = adi[3] = 0;
  _asm {
    push es
    push bp
    mov ax, aseg
    mov es, ax
    mov bx, aoff
    xor ax, ax
    mov dx, 0x0001
    push eseg
    push eoff
    mov bp, sp
    call dword ptr [bp]
    pop bp
    pop bp
    pop bp
    pop es
    pushf
    pop fl
  }
  if (fl & 1)
    return 0;
  dlo = (u16)(adi[0x55] | (adi[0x56] << 8));
  dhi = (u16)(adi[0x57] | (adi[0x58] << 8));
  // 0400, with the devnode in ECX: the version, features and settings
  _asm {
    push bp
    mov ax, dhi
    shl eax, 16
    mov ax, dlo
    mov ecx, eax
    mov dx, 0x0400
    push eseg
    push eoff
    mov bp, sp
    call dword ptr [bp]
    pop bp
    pop bp
    pop bp
    pushf
    pop fl
    mov a, ax
    mov b, bx
    mov c, cx
  }
  if (fl & 1)
    return 0;
  *ver = a;
  *feat = b;
  *sett = c;
  return 1;
}
// clang-format on

static void machine(void) {
  union REGS r;
  struct dosdate_t d;
  struct dostime_t t;
  u16 ver, feat, sett, win;
  const char *mm = "none";

  _dos_getdate(&d);
  _dos_gettime(&t);
  env_add("program: sb16test %s, %04u-%02u-%02u %02u:%02u", SBT_VERSION, d.year,
          d.month, d.day, t.hour, t.minute);
  r.x.ax = 0x3306;
  intdos(&r, &r);
  env_add("dos: %u.%02u", r.h.bl, r.h.bh);
  win = plat_windows();
  if (win) {
    card.flags |= SBT_F_WINDOWS;
    env_add("windows: %u.%02u, a DOS box", win >> 8, win & 0xFF);
    if (vxd_info(&ver, &feat, &sett))
      env_add("es1869.vxd: register API %X.%02X, features %04X, settings "
              "%04X",
              ver >> 8, ver & 0xFF, feat, sett);
    else
      env_add("es1869.vxd: no register API (ESS's driver, or none)");
  } else
    env_add("windows: not running");
  if (plat_device("QEMM386$"))
    mm = "QEMM";
  else if (plat_device("EMMXXXX0") || plat_device("EMMQXXX0"))
    mm = "EMM386 or JEMM";
  r.x.ax = 0x1687;
  int86(0x2F, &r, &r);
  env_add("memory manager: %s; DPMI host: %s", mm,
          r.x.ax == 0 ? "resident" : "none");
  tsr_seg = plat_find_tsr();
  if (tsr_seg) {
    card.flags |= SBT_F_TSR;
    env_add("esssb16: resident at %04Xh, %lu bytes", tsr_seg,
            *(u16 __far *)MK_FP(tsr_seg - 1, 3) * 16UL);
  } else
    env_add("esssb16: not resident");
  env_add("blaster: %.60s", blaster.text[0] ? blaster.text : "(not set)");
  if (h_over >= 0)
    env_add("/H: the 16-bit DMA %d, in place of BLASTER's H", h_over);
}

static void cleanup(void) {
  plat_unhook_irq();
  plat_pit_done();
  outp(0x0A, 4 | (card.dma8 & 3));
  if (card.dma16)
    outp(0xD4, 4 | (card.dma16 & 3));
  plat_report_close();
  plat_area_free();
}

static void on_break(int sig) {
  stop = 1;
  signal(sig, on_break);
}

static void usage(void) {
  puts("usage: sb16test [/A] [/L] [/Q] [/R] [/H:n] [/O:file]\n"
       "  /A  the automatic checks only, no listening\n"
       "  /L  the listening checks only\n"
       "  /Q  quicker: fewer rates\n"
       "  /R  a recorded run: a tone to set the recorder's level, then each\n"
       "      sound after its DTMF marker, and no questions\n"
       "  /H  the 16-bit DMA channel, in place of BLASTER's H\n"
       "  /O  the report's file, else SB16TEST.TXT\n"
       "Set BLASTER first (A I D H P), as for a game. Esc stops.");
}

int main(int argc, char **argv) {
  unsigned opts = 0;
  int i, failed, restored;

  for (i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (a[0] != '/' && a[0] != '-') {
      usage();
      return 1;
    }
    switch (toupper(a[1])) {
    case 'A':
      opts |= SBT_O_AUTO;
      break;
    case 'L':
      opts |= SBT_O_LISTEN;
      break;
    case 'Q':
      opts |= SBT_O_QUICK;
      break;
    case 'R':
      opts |= SBT_O_RECORD;
      break;
    case 'T': // short blocks and sounds, for the host tests' emulator
      opts |= SBT_O_SHORT;
      break;
    case 'H':
      if (a[2] != ':' || a[3] < '0' || a[3] > '7' || a[4]) {
        usage();
        return 1;
      }
      h_over = a[3] - '0';
      break;
    case 'O':
      if (a[2] != ':' || !a[3]) {
        usage();
        return 1;
      }
      strncpy(report_name, a + 3, sizeof(report_name) - 1);
      break;
    default:
      usage();
      return 1;
    }
  }
  if ((opts & SBT_O_AUTO) && (opts & SBT_O_LISTEN)) {
    usage();
    return 1;
  }
  printf("sb16test %s: checks the Sound Blaster 16 that games find\n",
         SBT_VERSION);
  plat_blaster(&blaster);
  card.base = blaster.base;
  card.irq = blaster.irq;
  card.dma8 = blaster.dma8;
  card.dma16 = blaster.dma16;
  if (h_over >= 0) // as plat_blaster takes H: none for D's or an 8-bit one
    card.dma16 = (u8)(h_over == card.dma8 || h_over < 4 ? 0 : h_over);
  card.mpu = blaster.mpu;
  restored = restore_old();
  card.area = plat_area(SBT_AREA);
  if (!card.area) {
    puts("not enough memory for the DMA buffers");
    return 2;
  }
  if (!plat_report_open(report_name)) {
    printf("can't create %.60s\n", report_name);
    cleanup();
    return 3;
  }
  atexit(cleanup);
  signal(SIGINT, on_break);
  signal(SIGBREAK, on_break);
  machine();
  if (restored)
    env_add("restored the mixer from %s: the last run didn't finish", RST_NAME);
  card.hz = PLAT_PIT_HZ;
  card.env = envp;
  hal.inb = dos_inb;
  hal.outb = dos_outb;
  hal.now = plat_now;
  hal.idle = dos_idle;
  hal.poke = dos_poke;
  hal.peek = dos_peek;
  hal.lock = plat_lock;
  hal.unlock = plat_unlock;
  hal.irq_mask = plat_irq_mask;
  hal.ask = dos_ask;
  hal.sound = dos_sound;
  hal.line = plat_line;
  hal.say = dos_say;
  hal.esc = dos_esc;
  hal.keep = dos_keep;
  hal.state = dos_state;
  if (opts & SBT_O_RECORD)
    puts("A recorded run: a tone to set the recorder's level by, then the "
         "checks, and\neach sound after its DTMF marker. Esc stops at any "
         "time.");
  else
    puts("The checks play quiet tones; the listening checks at the end ask "
         "what you\nheard. Esc stops at any time.");
  plat_pit_init();
  plat_hook_irq(card.irq, sbt_irq);
  failed = sbt_run(&hal, &card, opts);
  cleanup();
  printf("\n%d checks failed. The report is %.60s: send it back.\n", failed,
         report_name);
  return failed ? 4 : 0;
}
