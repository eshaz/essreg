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
 * The clock is the PIT's channel 0 in mode 2, which counts down by one at
 * 1.193 MHz, with the BIOS's ticks above it; mode 3, the BIOS's, counts by
 * two and can't be read in between. The program's interrupt handler runs
 * on whatever stack it interrupts, so nothing on its path takes the
 * address of a local. What the checks change in the mixer goes to
 * SB16TEST.RST first, which the next start puts back if this run didn't
 * finish. /T, for the host tests, runs the checks with short blocks
 * and sounds.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <bios.h>
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
#include <sys/stat.h>

#include "sb16t.h"

#define RST_NAME "SB16TEST.RST"
#define RST_TMP "SB16TEST.$$$"
#define RST_MAGIC "SBTRST1"
#define PIT_HZ 1193182UL

static struct sbt_card card;
static struct sbt_hal hal;
static char blaster[80];
static char envtext[10][100];
static const char *envp[11];
static int nenv;

static int report_fd = -1;
static int report_bad;
static char report_name[80] = "SB16TEST.TXT";

static volatile int stop;
static int lock_depth;
static unsigned lock_flags;

static void(__interrupt __far *old_vect)(void);
static u8 vect, irq_on_pic2, old_mask, hooked;
static u8 pit_mode2;
static u32 last_now;

static u16 tsr_seg; // esssb16's PSP, 0 if not resident

static unsigned dma_seg; // the DMA area's DOS block

// --- the hardware layer ------------------------------------------------------

static u8 dos_inb(u16 port) { return (u8)inp(port); }

static void dos_outb(u16 port, u8 v) { outp(port, v); }

static unsigned save_flags(void);
static void put_flags(unsigned f);
// clang-format off
#pragma aux save_flags = "pushf" "pop ax" "cli" value[ax];
#pragma aux put_flags = "push ax" "popf" parm[ax];
// clang-format on

static void dos_lock(void) {
  unsigned f = save_flags();

  if (lock_depth++ == 0)
    lock_flags = f;
}

static void dos_unlock(void) {
  if (--lock_depth == 0)
    put_flags(lock_flags);
}

// the PIT's count and the BIOS's ticks, in 1.193 MHz ticks; a wrap whose
// tick the BIOS hasn't counted yet (its interrupt waits) is added. The
// interrupts stay off until last_now is written, which the handler's
// reading also writes.
static u32 dos_now(void) {
  u32 ticks, t;
  u16 c;
  unsigned f = save_flags();

  ticks = *(u32 __far *)MK_FP(0x40, 0x6C);
  outp(0x43, 0x00);
  c = (u16)inp(0x40);
  c |= (u16)inp(0x40) << 8;
  t = (ticks << 16) + (u16)(0 - c);
  if ((s32)(t - last_now) < 0 && last_now - t < 0x18000UL)
    t += 0x10000UL;
  last_now = t;
  put_flags(f);
  return t;
}

static void dos_idle(void) {}

static void __far *lin(u32 addr) {
  return MK_FP((unsigned)(addr >> 4), (unsigned)(addr & 0x0F));
}

static void dos_poke(u32 addr, const u8 *src, u16 n) {
  _fmemcpy(lin(addr), src, n);
}

static void dos_peek(u32 addr, u8 *dst, u16 n) { _fmemcpy(dst, lin(addr), n); }

static void dos_irq_mask(int masked) {
  u16 port = irq_on_pic2 ? 0xA1 : 0x21;
  u8 bit = (u8)(1 << (card.irq & 7));

  if (masked)
    outp(port, inp(port) | bit);
  else
    outp(port, inp(port) & ~bit);
}

static void __interrupt __far sb_isr(void) {
  sbt_irq();
  if (irq_on_pic2)
    outp(0xA0, 0x20);
  outp(0x20, 0x20);
}

static int key(void) { return _bios_keybrd(_KEYBRD_READ) & 0xFF; }

static int dos_esc(void) {
  if (_bios_keybrd(_KEYBRD_READY) && key() == 27)
    stop = 1;
  return stop;
}

static int dos_ask(const char *id, const char *question, const char *keys) {
  int k;

  printf("\n%s: %s\n> ", id, question);
  fflush(stdout);
  for (;;) {
    k = tolower(key());
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

static void dos_line(const char *text) {
  static char buf[260];
  int n;

  if (report_fd < 0 || report_bad)
    return;
  n = (int)strlen(text);
  if (n > 256)
    n = 256;
  memcpy(buf, text, n);
  buf[n++] = '\n';
  if (write(report_fd, buf, n) != n) {
    report_bad = 1;
    printf("can't write %.60s: is the disk full?\n", report_name);
  }
  if (text[0] == '[')
    _commit(report_fd);
}

static void dos_say(const char *text) { printf("%s...\n", text); }

// the restore file: the mixer's registers as they were, written whole as
// SB16TEST.$$$ and renamed over SB16TEST.RST; gone when they are back
static void dos_keep(const u8 *pairs, u16 n) {
  int fd;
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
  fd = open(RST_TMP, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
            S_IREAD | S_IWRITE);
  if (fd < 0)
    return;
  if (write(fd, head, 12) != 12 || write(fd, pairs, n) != (int)n) {
    close(fd);
    remove(RST_TMP);
    return;
  }
  _commit(fd);
  if (close(fd) != 0) {
    remove(RST_TMP);
    return;
  }
  remove(RST_NAME);
  rename(RST_TMP, RST_NAME);
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
    dos_lock();
    outp(base + 4, pairs[i]);
    outp(base + 5, pairs[i + 1]);
    dos_unlock();
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
    dos_line(buf);
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

static int device_there(const char *name) {
  int fd = open(name, O_RDONLY | O_BINARY);

  if (fd < 0)
    return 0;
  close(fd);
  return 1;
}

// esssb16's resident block: a program's own block with its signature at
// 103h, as its installed check finds it
static u16 find_tsr(void) {
  union REGS r;
  struct SREGS sr;
  u16 mcb, owner, size;
  const char __far *sig;

  segread(&sr);
  r.h.ah = 0x52;
  intdosx(&r, &r, &sr);
  mcb = *(u16 __far *)MK_FP(sr.es, r.x.bx - 2);
  for (;;) {
    char kind = *(char __far *)MK_FP(mcb, 0);
    if (kind != 'M' && kind != 'Z')
      break;
    owner = *(u16 __far *)MK_FP(mcb, 1);
    size = *(u16 __far *)MK_FP(mcb, 3);
    if (owner == mcb + 1 && size >= 0x20) {
      sig = (const char __far *)MK_FP(owner, 0x103);
      if (_fmemcmp(sig, "ESSSB16", 8) == 0 && owner != _psp)
        return owner;
    }
    if (kind == 'Z')
      break;
    mcb = (u16)(mcb + size + 1);
  }
  return 0;
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
  u16 ver, feat, sett;
  const char *mm = "none";

  _dos_getdate(&d);
  _dos_gettime(&t);
  env_add("program: sb16test %s, %04u-%02u-%02u %02u:%02u", SBT_VERSION, d.year,
          d.month, d.day, t.hour, t.minute);
  r.x.ax = 0x3306;
  intdos(&r, &r);
  env_add("dos: %u.%02u", r.h.bl, r.h.bh);
  r.x.ax = 0x1600;
  int86(0x2F, &r, &r);
  if (r.h.al != 0 && r.h.al != 0x80) {
    card.flags |= SBT_F_WINDOWS;
    env_add("windows: %u.%02u, a DOS box", r.h.al, r.h.ah);
    if (vxd_info(&ver, &feat, &sett))
      env_add("es1869.vxd: register API %X.%02X, features %04X, settings "
              "%04X",
              ver >> 8, ver & 0xFF, feat, sett);
    else
      env_add("es1869.vxd: no register API (ESS's driver, or none)");
  } else
    env_add("windows: not running");
  if (device_there("QEMM386$"))
    mm = "QEMM";
  else if (device_there("EMMXXXX0") || device_there("EMMQXXX0"))
    mm = "EMM386 or JEMM";
  r.x.ax = 0x1687;
  int86(0x2F, &r, &r);
  env_add("memory manager: %s; DPMI host: %s", mm,
          r.x.ax == 0 ? "resident" : "none");
  tsr_seg = find_tsr();
  if (tsr_seg) {
    card.flags |= SBT_F_TSR;
    env_add("esssb16: resident at %04Xh, %lu bytes", tsr_seg,
            *(u16 __far *)MK_FP(tsr_seg - 1, 3) * 16UL);
  } else
    env_add("esssb16: not resident");
  env_add("blaster: %.60s", blaster[0] ? blaster : "(not set)");
}

// BLASTER=A220 I5 D1 H5 P330 T6
static void parse_blaster(void) {
  const char *b = getenv("BLASTER"), *p;

  card.base = 0x220;
  card.irq = 5;
  card.dma8 = 1;
  card.dma16 = 5;
  card.mpu = 0x330;
  if (b == NULL)
    return;
  strncpy(blaster, b, sizeof(blaster) - 1);
  for (p = b; *p; p++) {
    switch (toupper(*p)) {
    case 'A':
      card.base = (u16)strtoul(p + 1, NULL, 16);
      break;
    case 'I':
      card.irq = (u8)strtoul(p + 1, NULL, 10);
      break;
    case 'D':
      card.dma8 = (u8)strtoul(p + 1, NULL, 10);
      break;
    case 'H':
      card.dma16 = (u8)strtoul(p + 1, NULL, 10);
      break;
    case 'P':
      card.mpu = (u16)strtoul(p + 1, NULL, 16);
      break;
    }
    while (p[1] && p[1] != ' ')
      p++;
  }
  if (card.irq == 2)
    card.irq = 9;
  if (card.dma16 == card.dma8 || card.dma16 < 4 || card.dma16 > 7)
    card.dma16 = 0;
}

// the DMA area: 32 KB in one 64 KB page, out of a 64 KB block
static int dma_area(void) {
  u32 a, next;

  if (_dos_allocmem(0x1000, &dma_seg) != 0)
    return 0;
  a = (u32)dma_seg << 4;
  next = (a + 0xFFFFUL) & ~0xFFFFUL;
  if ((a & 0xFFFFUL) + SBT_AREA <= 0x10000UL)
    card.area = a;
  else
    card.area = next;
  return 1;
}

static void hook_irq(void) {
  irq_on_pic2 = card.irq >= 8;
  vect = (u8)(irq_on_pic2 ? 0x70 + card.irq - 8 : 8 + card.irq);
  old_vect = _dos_getvect(vect);
  old_mask = (u8)inp(irq_on_pic2 ? 0xA1 : 0x21);
  _dos_setvect(vect, sb_isr);
  hooked = 1;
  dos_irq_mask(0);
}

// the PIT's channel 0 in mode 2, at the BIOS's 18.2 Hz
static void pit_init(void) {
  unsigned f = save_flags();

  outp(0x43, 0x34);
  outp(0x40, 0);
  outp(0x40, 0);
  put_flags(f);
  pit_mode2 = 1;
}

static void cleanup(void) {
  unsigned f;
  u16 port;

  if (hooked) {
    port = irq_on_pic2 ? 0xA1 : 0x21;
    outp(port, (inp(port) & ~(1 << (card.irq & 7))) |
                   (old_mask & (1 << (card.irq & 7))));
    _dos_setvect(vect, old_vect);
    hooked = 0;
  }
  if (pit_mode2) {
    f = save_flags();
    outp(0x43, 0x36);
    outp(0x40, 0);
    outp(0x40, 0);
    put_flags(f);
    pit_mode2 = 0;
  }
  outp(0x0A, 4 | (card.dma8 & 3));
  if (card.dma16)
    outp(0xD4, 4 | (card.dma16 & 3));
  if (report_fd >= 0) {
    _commit(report_fd);
    close(report_fd);
    report_fd = -1;
  }
  if (dma_seg) {
    _dos_freemem(dma_seg);
    dma_seg = 0;
  }
}

static void on_break(int sig) {
  stop = 1;
  signal(sig, on_break);
}

static void usage(void) {
  puts("usage: sb16test [/A] [/L] [/Q] [/O:file]\n"
       "  /A  the automatic checks only, no listening\n"
       "  /L  the listening checks only\n"
       "  /Q  quicker: fewer rates\n"
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
    case 'T': // short blocks and sounds, for the host tests' emulator
      opts |= SBT_O_SHORT;
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
  parse_blaster();
  restored = restore_old();
  if (!dma_area()) {
    puts("not enough memory for the DMA buffers");
    return 2;
  }
  report_fd = open(report_name, O_WRONLY | O_CREAT | O_TRUNC | O_TEXT,
                   S_IREAD | S_IWRITE);
  if (report_fd < 0) {
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
  card.hz = PIT_HZ;
  card.env = envp;
  hal.inb = dos_inb;
  hal.outb = dos_outb;
  hal.now = dos_now;
  hal.idle = dos_idle;
  hal.poke = dos_poke;
  hal.peek = dos_peek;
  hal.lock = dos_lock;
  hal.unlock = dos_unlock;
  hal.irq_mask = dos_irq_mask;
  hal.ask = dos_ask;
  hal.sound = dos_sound;
  hal.line = dos_line;
  hal.say = dos_say;
  hal.esc = dos_esc;
  hal.keep = dos_keep;
  hal.state = dos_state;
  puts("The checks play quiet tones; the listening checks at the end ask "
       "what you\nheard. Esc stops at any time.");
  pit_init();
  hook_irq();
  failed = sbt_run(&hal, &card, opts);
  cleanup();
  printf("\n%d checks failed. The report is %.60s: send it back.\n", failed,
         report_name);
  return failed ? 4 : 0;
}
