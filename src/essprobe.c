/*
 * essprobe measures the ES1869 itself, without a front, in real DOS: its
 * two engines in each of their modes, both at once, its resets, the DMA
 * controllers behind the memory manager, what the engines do to the rest
 * of the machine, and a game's refills behind the DMA. It writes
 * everything to ESSPROBE.TXT, which you send back with the recording of a
 * recorded run (docs/ESSPROBE.md).
 *
 * Usage:
 *   `essprobe [/R] [/S] [/A] [/C:names] [/N] [/O:file]`
 *
 * Notes:
 *
 * It refuses to run in a Windows DOS box, where the VxD has the chip, and
 * with esssb16 resident, whose front would answer instead of the chip. The
 * cases are essp.c's; the clock, the interrupt and the files are
 * dosplat.c's. What the cases change outside the chip goes to ESSPROBE.RST
 * first, which the next start puts back if this run didn't finish. Under
 * EMM386 a window on the real ports (src/essp0.asm) lets the cases see
 * the 8237 past EMM386's DMA translation: it is a port trap of
 * Audio_Base+Dh, which the chip doesn't decode, through int 2Fh 4A15h,
 * removed at the end; a run that didn't end leaves it to the next, which
 * removes it first. /T, for the host tests, runs the cases with short
 * buffers and sounds.
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
#include "essp.h"
#include "essp0.h"

#define RST_NAME "ESSPROBE.RST"
#define RST_TMP "ESSPROBE.$$$"
#define RST_MAGIC "ESPRST1"

// the exit codes
#define X_USAGE 1
#define X_NOCARD 2
#define X_WINDOWS 3
#define X_TSR 4
#define X_MEMORY 5
#define X_FAILED 6

static struct esp_card card;
static struct esp_hal hal;
static struct plat_blaster blaster;
static char envtext[14][100];
static const char *envp[15];
static int nenv;

static char report_name[80] = "ESSPROBE.TXT";

static volatile int stop;
static int v86; // the processor runs DOS in V86 mode: a memory manager

static unsigned win_seg; // the window's block, 0 without one
static u16 win_port;
static u16 win_handle;
static int win_kept; // EMM386 kept the window's trap at the end

// --- the hardware layer
// --------------------------------------------------------

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

static void dos_hook(u8 irq) {
  if (irq)
    plat_hook_irq(irq, esp_irq);
  else
    plat_unhook_irq();
}

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
      return 0;
    }
    if (k && strchr(keys, k) != NULL) {
      printf("\n");
      fflush(stdout);
      return k;
    }
  }
}

static void dos_say(const char *text) { printf("%s...\n", text); }

// the restore list, written whole as ESSPROBE.$$$ and renamed over
// ESSPROBE.RST; gone when everything is back
static void dos_keep(const u8 *list, u16 n) {
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
  plat_save(RST_TMP, RST_NAME, head, 12, list, n);
}

// the ports themselves, where nothing translates them (real mode)
static int direct_phys(struct esp_io *io, u16 n) {
  u16 i;

  plat_lock();
  for (i = 0; i < n; i++)
    if (io[i].kind == ESP_IN)
      io[i].value = (u8)inp(io[i].port);
    else
      outp(io[i].port, io[i].value);
  plat_unlock();
  return 1;
}

// --- the window under EMM386
// -----------------------------------------------------

static u8 __far *win(u16 off) { return (u8 __far *)MK_FP(win_seg, off); }

// int 2Fh 4A15h: fn 0 installs a trap of port with the table at blk's
// ESSP0_O_TABLE, and gives its handle; fn 1 removes the trap of handle
// (SI). CF set: refused
// clang-format off
static int emm_trap(u16 fn, u16 blk, u16 port, u16 *handle) {
  u16 h = *handle, fl = 1;

  _asm {
    push ds
    push si
    push di
    mov dx, port
    mov ax, dx
    shl edx, 16
    mov dx, ax
    mov cx, 1
    mov bx, fn
    mov si, h
    cmp bx, 0
    jne go
    mov si, ESSP0_O_TABLE
  go:
    mov di, ESSP0_SIZE
    mov ax, blk
    mov ds, ax
    mov ax, 0x4A15
    stc
    int 0x2F
    pop di
    pop si
    pop ds
    pushf
    pop fl
    mov h, ax
  }
  if (fl & 1)
    return 0;
  if (fn == 0)
    *handle = h;
  return 1;
}
// clang-format on

// a block of DOS's with the window's signature: a run that didn't end
// left it, its trap still in; the trap removed and the block freed
static void win_old(void) {
  union REGS r;
  struct SREGS sr;
  u16 mcb, owner, size, seg, h;

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
    seg = (u16)(mcb + 1);
    if (owner == 8 && size >= (ESSP0_SIZE + 15) >> 4 &&
        _fmemcmp(MK_FP(seg, ESSP0_O_SIGNATURE), "ESSP0", 6) == 0) {
      h = *(u16 __far *)MK_FP(seg, ESSP0_O_HANDLE);
      *(u8 __far *)MK_FP(seg, ESSP0_O_ARMED) = 0;
      if (emm_trap(1, seg, 0, &h))
        _dos_freemem(seg);
      break;
    }
    if (kind == 'Z')
      break;
    mcb = (u16)(mcb + size + 1);
  }
}

// the window: the block copied, owned by DOS, its trap of port in, armed;
// 1 if a read of the port then runs its list
static int win_install(u16 port) {
  unsigned seg;

  win_old();
  if (_dos_allocmem((ESSP0_SIZE + 15) >> 4, &seg) != 0)
    return 0;
  win_seg = seg;
  _fmemcpy(win(0), essp0_code, ESSP0_SIZE);
  *(u16 __far *)win(ESSP0_O_TABLE) = port;
  *(u16 __far *)MK_FP(seg - 1, 1) = 8; // DOS's: kept if the program dies
  if (!emm_trap(0, (u16)seg, port, &win_handle)) {
    *(u16 __far *)MK_FP(seg - 1, 1) = _psp;
    _dos_freemem(seg);
    win_seg = 0;
    return 0;
  }
  *(u16 __far *)win(ESSP0_O_HANDLE) = win_handle;
  win_port = port;
  *win(ESSP0_O_REQ) = 0;
  *win(ESSP0_O_ARMED) = ESSP0_ARMED;
  if ((u8)inp(port) != ESSP0_DONE) {
    *win(ESSP0_O_ARMED) = 0;
    return 0;
  }
  return 1;
}

// the window's trap removed and its block freed, which the report's last
// line says; an EMM386 that keeps the trap keeps the block, DOS's, until a
// restart, and esssb16 can't trap that port meanwhile
static void win_remove(void) {
  static char line[80];

  if (!win_seg)
    return;
  *win(ESSP0_O_ARMED) = 0;
  if (emm_trap(1, (u16)win_seg, win_port, &win_handle)) {
    *(u16 __far *)MK_FP(win_seg - 1, 1) = _psp;
    _dos_freemem(win_seg);
    plat_line("window: removed");
  } else {
    win_kept = 1;
    _bprintf(line, sizeof(line),
             "window: EMM386 kept its trap of %Xh until a restart", win_port);
    plat_line(line);
  }
  win_seg = 0;
}

// n entries of the list on the real ports, through the window's port
static int win_phys(struct esp_io *io, u16 n) {
  u8 __far *q;
  u16 i;

  if (!win_seg || n > ESSP0_NREQ)
    return 0;
  q = win(ESSP0_O_REQ);
  for (i = 0; i < n; i++, q += 4) {
    q[0] = io[i].kind;
    q[1] = (u8)io[i].port;
    q[2] = (u8)(io[i].port >> 8);
    q[3] = io[i].value;
  }
  if (n < ESSP0_NREQ)
    q[0] = 0;
  if ((u8)inp(win_port) != ESSP0_DONE || *win(ESSP0_O_COUNT) != n) {
    *win(ESSP0_O_REQ) = 0;
    return 0;
  }
  q = win(ESSP0_O_REQ);
  for (i = 0; i < n; i++, q += 4)
    if (io[i].kind == ESP_IN)
      io[i].value = q[3];
  // an empty list again, so that a window a run left armed does nothing
  *win(ESSP0_O_REQ) = 0;
  return 1;
}

// --- the machine
// -----------------------------------------------------------------

static void env_add(const char *fmt, ...) {
  va_list ap;

  if (nenv >= 14)
    return;
  va_start(ap, fmt);
  _vbprintf(envtext[nenv], sizeof(envtext[0]), fmt, ap);
  va_end(ap);
  envp[nenv] = envtext[nenv];
  nenv++;
  envp[nenv] = NULL;
}

// clang-format off
static int in_v86(void) {
  u16 msw = 0;

  _asm {
    smsw ax
    mov msw, ax
  }
  return msw & 1;
}
// clang-format on

// EMM386's version through its device's IOCTL, as esssb16 reads it: the
// buffer's first byte asks for subfunction 2, and comes back as the major
// version, the second as the minor; 0 without the device
static u16 emm_version(const char *name) {
  union REGS r;
  u8 buf[2];
  int fd = open(name, O_RDONLY | O_BINARY);

  if (fd < 0)
    return 0;
  buf[0] = 2;
  buf[1] = 0;
  r.x.ax = 0x4402;
  r.x.bx = (unsigned)fd;
  r.x.cx = 2;
  r.x.dx = (unsigned)buf;
  intdos(&r, &r);
  close(fd);
  if (r.x.cflag)
    return 0xFFFF;
  return (u16)((buf[0] << 8) | buf[1]);
}

// the boot drive's CONFIG.SYS lines that load a memory manager
static void config_lines(void) {
  static char buf[2048], path[16], line[100];
  union REGS r;
  int fd, n, i, j, found = 0;

  r.x.ax = 0x3305;
  intdos(&r, &r);
  sprintf(path, "%c:\\CONFIG.SYS", 'A' + r.h.dl - 1);
  fd = open(path, O_RDONLY | O_BINARY);
  if (fd < 0) {
    env_add("config.sys: %s isn't there", path);
    return;
  }
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n < 0)
    n = 0;
  buf[n] = 0;
  for (i = 0; i < n && found < 3;) {
    for (j = 0;
         i < n && buf[i] != '\r' && buf[i] != '\n' && j < (int)sizeof(line) - 1;
         i++)
      line[j++] = buf[i];
    line[j] = 0;
    while (i < n && buf[i] != '\n')
      i++;
    i++;
    strupr(line);
    if (strstr(line, "EMM386") || strstr(line, "JEMM") ||
        strstr(line, "QEMM") || strstr(line, "HIMEM")) {
      env_add("config.sys: %.70s", line);
      found++;
    }
  }
  if (!found)
    env_add("config.sys: no memory manager's line");
}

// clang-format off
static u32 pci_get(u32 at) {
  u16 alo = (u16)at, ahi = (u16)(at >> 16), lo = 0, hi = 0;

  _asm {
    mov ax, ahi
    shl eax, 16
    mov ax, alo
    mov dx, 0x0CF8
    out dx, eax
    mov dx, 0x0CFC
    in eax, dx
    mov lo, ax
    shr eax, 16
    mov hi, ax
  }
  return ((u32)hi << 16) | lo;
}
// clang-format on

// clang-format off
static void pci_put(u32 at) {
  u16 alo = (u16)at, ahi = (u16)(at >> 16);

  _asm {
    mov ax, ahi
    shl eax, 16
    mov ax, alo
    mov dx, 0x0CF8
    out dx, eax
  }
}
// clang-format on

// clang-format off
static u32 pci_cf8(void) {
  u16 lo = 0, hi = 0;

  _asm {
    mov dx, 0x0CF8
    in eax, dx
    mov lo, ax
    shr eax, 16
    mov hi, ax
  }
  return ((u32)hi << 16) | lo;
}
// clang-format on

// the bridges and the sound devices on PCI bus 0, through configuration
// mechanism 1, its address put back
static void pci_lines(void) {
  static char line[100];
  u32 old, id, cls;
  u16 dev;
  char *q = line;

  plat_lock();
  old = pci_cf8();
  pci_put(0x80000000UL);
  if (pci_cf8() != 0x80000000UL) {
    pci_put(old);
    plat_unlock();
    env_add("pci: none (configuration mechanism 1 doesn't answer)");
    return;
  }
  q += sprintf(q, "pci bus 0:");
  for (dev = 0; dev < 32 && q < line + sizeof(line) - 24; dev++) {
    id = pci_get(0x80000000UL | ((u32)dev << 11));
    if ((u16)id == 0xFFFF)
      continue;
    cls = pci_get(0x80000008UL | ((u32)dev << 11)) >> 16;
    if ((cls >> 8) == 0x06 || (cls >> 8) == 0x04)
      q += sprintf(q, " %u:%04X:%04X/%04X", dev, (u16)id, (u16)(id >> 16),
                   (u16)cls);
  }
  pci_put(old);
  plat_unlock();
  env_add("%s", line);
}

static void machine(void) {
  union REGS r;
  struct SREGS sr;
  struct dosdate_t d;
  struct dostime_t t;
  u16 ver;
  const char __far *logo;

  _dos_getdate(&d);
  _dos_gettime(&t);
  env_add("program: essprobe %s, %04u-%02u-%02u %02u:%02u", ESP_VERSION, d.year,
          d.month, d.day, t.hour, t.minute);
  r.x.ax = 0x3306;
  intdos(&r, &r);
  env_add("dos: %u.%02u, %s", r.h.bl, r.h.bh,
          v86 ? "V86 mode (a memory manager)" : "real mode");
  if (plat_device("QEMM386$"))
    env_add("memory manager: QEMM");
  ver = emm_version("EMMXXXX0");
  if (!ver)
    ver = emm_version("EMMQXXX0");
  if (ver == 0xFFFF)
    env_add("memory manager: EMMXXXX0, no version");
  else if (ver)
    env_add("memory manager: EMMXXXX0 %u.%02u (EMM386, or JEMM)", ver >> 8,
            ver & 0xFF);
  else if (!plat_device("QEMM386$"))
    env_add("memory manager: none");
  config_lines();
  r.x.ax = 0x1687;
  segread(&sr);
  int86x(0x2F, &r, &r, &sr);
  if (r.x.ax == 0) {
    logo = (const char __far *)MK_FP(sr.es, 0);
    env_add("dpmi host: %s, %s clients",
            _fmemcmp(logo, "HDPMI", 5) == 0 ? "HDPMI" : "resident, not HDPMI",
            (r.x.bx & 1) ? "32-bit" : "16-bit");
    if (_fmemcmp(logo, "HDPMI", 5) == 0)
      card.flags |= ESP_F_HDPMI;
  } else
    env_add("dpmi host: none");
  pci_lines();
  env_add("blaster: %.60s", blaster.text[0] ? blaster.text : "(not set)");
}

// a run that didn't finish left the list: everything on it put back
static int restore_old(void) {
  static u8 list[ESP_KEEP];
  u8 head[12], id[4];
  u16 base, cfg, n, i;
  int fd = open(RST_NAME, O_RDONLY | O_BINARY);

  if (fd < 0)
    return 0;
  if (read(fd, head, 12) != 12 || memcmp(head, RST_MAGIC, 8) != 0) {
    close(fd);
    return 0;
  }
  base = (u16)(head[8] | (head[9] << 8));
  n = (u16)(head[10] | (head[11] << 8));
  if (n > ESP_KEEP || read(fd, list, n) != (int)n) {
    close(fd);
    return 0;
  }
  close(fd);
  plat_lock();
  outp(base + 4, 0x40);
  for (i = 0; i < 4; i++)
    id[i] = (u8)inp(base + 5);
  plat_unlock();
  cfg = (u16)(id[0] == 0x18 && id[1] == 0x69 ? (id[2] << 8) | id[3] : 0);
  for (i = 0; i + 3 <= n;) {
    u8 kind = list[i];
    plat_lock();
    if (kind == ESP_K_MIXER) {
      outp(base + 4, list[i + 1]);
      outp(base + 5, list[i + 2]);
    } else if (kind == ESP_K_PNP && cfg) {
      outp(cfg, 0x07);
      outp(cfg + 1, 1);
      outp(cfg, list[i + 1]);
      outp(cfg + 1, list[i + 2]);
    } else if (kind == ESP_K_CONFIG && cfg)
      outp(cfg + list[i + 1], list[i + 2]);
    else if (kind == ESP_K_DAC && i + 5 <= n) {
      outp(0x3C8, list[i + 1]);
      outp(0x3C9, list[i + 2]);
      outp(0x3C9, list[i + 3]);
      outp(0x3C9, list[i + 4]);
    }
    plat_unlock();
    i = (u16)(i + (kind == ESP_K_DAC ? 5 : 3));
  }
  remove(RST_NAME);
  return 1;
}

static int chip_there(u16 base) {
  u8 id[4];
  int i;

  plat_lock();
  outp(base + 4, 0x40);
  for (i = 0; i < 4; i++)
    id[i] = (u8)inp(base + 5);
  plat_unlock();
  return id[0] == 0x18 && id[1] == 0x69;
}

static void cleanup(void) {
  win_remove();
  plat_unhook_irq();
  plat_pit_done();
  plat_report_close();
  plat_area_free();
}

static void on_break(int sig) {
  stop = 1;
  signal(sig, on_break);
}

static void usage(void) {
  puts("usage: essprobe [/R] [/S] [/A] [/C:names] [/N] [/O:file]\n"
       "  /R  a recorded run: a tone to set the recorder's level, then each\n"
       "      sound after its DTMF marker\n"
       "  /S  the short run: a case of each kind\n"
       "  /A  the cases without a sound only\n"
       "  /C  only the cases whose names start so: groups (env rst a1 a2 both\n"
       "      h1 h4 stream) or cases, comma-separated\n"
       "  /N  no window on the DMA controllers under EMM386\n"
       "  /O  the report's file, else ESSPROBE.TXT\n"
       "Run it in real DOS without esssb16, BLASTER set (A). Esc stops.");
}

int main(int argc, char **argv) {
  static char only[80];
  unsigned opts = 0;
  int i, failed, restored, nowin = 0;
  u16 win98;

  for (i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (a[0] != '/' && a[0] != '-') {
      usage();
      return X_USAGE;
    }
    switch (toupper(a[1])) {
    case 'R':
      opts |= ESP_O_RECORD;
      break;
    case 'S':
      opts |= ESP_O_SHORT;
      break;
    case 'A':
      opts |= ESP_O_SILENT;
      break;
    case 'T': // short buffers and sounds, for the host tests' emulator
      opts |= ESP_O_TINY;
      break;
    case 'N':
      nowin = 1;
      break;
    case 'C':
      if (a[2] != ':' || !a[3]) {
        usage();
        return X_USAGE;
      }
      strncpy(only, a + 3, sizeof(only) - 1);
      strlwr(only);
      break;
    case 'O':
      if (a[2] != ':' || !a[3]) {
        usage();
        return X_USAGE;
      }
      strncpy(report_name, a + 3, sizeof(report_name) - 1);
      break;
    default:
      usage();
      return X_USAGE;
    }
  }
  if ((opts & ESP_O_RECORD) && (opts & ESP_O_SILENT)) {
    usage();
    return X_USAGE;
  }
  printf("essprobe %s: measures the ES1869 itself\n", ESP_VERSION);
  win98 = plat_windows();
  if (win98) {
    printf("Windows %u.%02u runs: essprobe needs real DOS (restart in MS-DOS "
           "mode)\n",
           win98 >> 8, win98 & 0xFF);
    return X_WINDOWS;
  }
  if (plat_find_tsr()) {
    puts("esssb16 is resident: restart without it, then run essprobe");
    return X_TSR;
  }
  plat_blaster(&blaster);
  card.base = blaster.base;
  restored = restore_old();
  if (!chip_there(card.base)) {
    printf("no ES1869 answers at %Xh: set BLASTER's A to the card's base\n",
           card.base);
    return X_NOCARD;
  }
  card.area = plat_area(ESP_AREA);
  if (!card.area) {
    puts("not enough memory for the DMA buffers");
    return X_MEMORY;
  }
  if (!plat_report_open(report_name)) {
    printf("can't create %.60s\n", report_name);
    cleanup();
    return X_MEMORY;
  }
  atexit(cleanup);
  signal(SIGINT, on_break);
  signal(SIGBREAK, on_break);
  v86 = in_v86();
  machine();
  if (restored)
    env_add("restored the chip from %s: the last run didn't finish", RST_NAME);
  hal.phys = NULL;
  if (!v86)
    hal.phys = direct_phys;
  else if (!nowin) {
    if (win_install((u16)(card.base + 0x0D))) {
      hal.phys = win_phys;
      env_add("window: Audio_Base+Dh trapped through int 2Fh 4A15h");
    } else
      env_add("window: refused, so the 8237 is seen as a program sees it");
  } else
    env_add("window: none (/N)");
  if (v86)
    card.flags |= ESP_F_EMM386;
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
  hal.hook = dos_hook;
  hal.irq_mask = plat_irq_mask;
  hal.ask = dos_ask;
  hal.line = plat_line;
  hal.say = dos_say;
  hal.esc = dos_esc;
  hal.keep = dos_keep;
  if (opts & ESP_O_RECORD)
    puts("A recorded run: a tone to set the recorder's level by, then each "
         "case's\nsound after its DTMF marker. Esc stops at any time.");
  else
    puts("The cases play quiet tones. Esc stops at any time.");
  plat_pit_init();
  failed = esp_run(&hal, &card, opts, ESP_G_ALL, only);
  cleanup();
  printf("\n%d cases failed. The report is %.60s: send it back.\n", failed,
         report_name);
  if (win_kept)
    puts("EMM386 kept the window's port trap: restart before you load "
         "esssb16.");
  return failed ? X_FAILED : 0;
}
