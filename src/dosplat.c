/*
 * The DOS side of sb16test and essprobe, see dosplat.h.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "dosplat.h"

static int lock_depth;
static unsigned lock_flags;

static void(__interrupt __far *old_vect)(void);
static void (*irq_fn)(void);
static u8 irq_num, vect, irq_on_pic2, old_mask, hooked;
static u8 pit_mode2;
static u32 last_now;

static int report_fd = -1;
static int report_bad;
static char report_name[80];

static unsigned area_seg; // the DMA area's DOS block

static unsigned save_flags(void);
static void put_flags(unsigned f);
// clang-format off
#pragma aux save_flags = "pushf" "pop ax" "cli" value[ax];
#pragma aux put_flags = "push ax" "popf" parm[ax];
// clang-format on

void plat_lock(void) {
  unsigned f = save_flags();

  if (lock_depth++ == 0)
    lock_flags = f;
}

void plat_unlock(void) {
  if (--lock_depth == 0)
    put_flags(lock_flags);
}

// --- the clock ---------------------------------------------------------------

void plat_pit_init(void) {
  unsigned f = save_flags();

  outp(0x43, 0x34);
  outp(0x40, 0);
  outp(0x40, 0);
  put_flags(f);
  pit_mode2 = 1;
}

void plat_pit_done(void) {
  unsigned f;

  if (!pit_mode2)
    return;
  f = save_flags();
  outp(0x43, 0x36);
  outp(0x40, 0);
  outp(0x40, 0);
  put_flags(f);
  pit_mode2 = 0;
}

// the PIT's count and the BIOS's ticks, in 1.193 MHz ticks; a wrap whose
// tick the BIOS hasn't counted yet (its interrupt waits) is added. The
// interrupts stay off until last_now is written, which the handler's
// reading also writes.
u32 plat_now(void) {
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

// --- the card's interrupt ----------------------------------------------------

static void __interrupt __far plat_isr(void) {
  irq_fn();
  if (irq_on_pic2)
    outp(0xA0, 0x20);
  outp(0x20, 0x20);
}

void plat_irq_mask(int masked) {
  u16 port = irq_on_pic2 ? 0xA1 : 0x21;
  u8 bit = (u8)(1 << (irq_num & 7));

  if (masked)
    outp(port, inp(port) | bit);
  else
    outp(port, inp(port) & ~bit);
}

void plat_hook_irq(u8 irq, void (*fn)(void)) {
  irq_num = irq;
  irq_fn = fn;
  irq_on_pic2 = irq >= 8;
  vect = (u8)(irq_on_pic2 ? 0x70 + irq - 8 : 8 + irq);
  old_vect = _dos_getvect(vect);
  old_mask = (u8)inp(irq_on_pic2 ? 0xA1 : 0x21);
  _dos_setvect(vect, plat_isr);
  hooked = 1;
  plat_irq_mask(0);
}

void plat_unhook_irq(void) {
  u16 port;
  u8 bit;

  if (!hooked)
    return;
  port = irq_on_pic2 ? 0xA1 : 0x21;
  bit = (u8)(1 << (irq_num & 7));
  outp(port, (inp(port) & ~bit) | (old_mask & bit));
  _dos_setvect(vect, old_vect);
  hooked = 0;
}

// --- the keyboard ------------------------------------------------------------

int plat_key(void) { return _bios_keybrd(_KEYBRD_READ) & 0xFF; }

int plat_key_ready(void) { return _bios_keybrd(_KEYBRD_READY) != 0; }

// --- the files ---------------------------------------------------------------

int plat_report_open(const char *name) {
  strncpy(report_name, name, sizeof(report_name) - 1);
  report_bad = 0;
  report_fd = open(report_name, O_WRONLY | O_CREAT | O_TRUNC | O_TEXT,
                   S_IREAD | S_IWRITE);
  return report_fd >= 0;
}

void plat_line(const char *text) {
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
  _commit(report_fd); // every line, for a machine that stops in a case
}

void plat_report_close(void) {
  if (report_fd < 0)
    return;
  _commit(report_fd);
  close(report_fd);
  report_fd = -1;
}

int plat_save(const char *tmp, const char *name, const u8 *head, u16 nhead,
              const u8 *data, u16 n) {
  int fd =
      open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);

  if (fd < 0)
    return 0;
  if (write(fd, head, nhead) != (int)nhead || write(fd, data, n) != (int)n) {
    close(fd);
    remove(tmp);
    return 0;
  }
  _commit(fd);
  if (close(fd) != 0) {
    remove(tmp);
    return 0;
  }
  remove(name);
  return rename(tmp, name) == 0;
}

// --- the machine -------------------------------------------------------------

void plat_blaster(struct plat_blaster *b) {
  const char *v = getenv("BLASTER"), *p;

  b->base = 0x220;
  b->irq = 5;
  b->dma8 = 1;
  b->dma16 = 5;
  b->mpu = 0x330;
  b->text[0] = 0;
  if (v == NULL)
    return;
  strncpy(b->text, v, sizeof(b->text) - 1);
  b->text[sizeof(b->text) - 1] = 0;
  for (p = v; *p; p++) {
    switch (toupper(*p)) {
    case 'A':
      b->base = (u16)strtoul(p + 1, NULL, 16);
      break;
    case 'I':
      b->irq = (u8)strtoul(p + 1, NULL, 10);
      break;
    case 'D':
      b->dma8 = (u8)strtoul(p + 1, NULL, 10);
      break;
    case 'H':
      b->dma16 = (u8)strtoul(p + 1, NULL, 10);
      break;
    case 'P':
      b->mpu = (u16)strtoul(p + 1, NULL, 16);
      break;
    }
    while (p[1] && p[1] != ' ')
      p++;
  }
  if (b->irq == 2)
    b->irq = 9;
  if (b->dma16 == b->dma8 || b->dma16 < 4 || b->dma16 > 7)
    b->dma16 = 0;
}

u32 plat_area(u32 bytes) {
  u32 a, next;

  if (_dos_allocmem((unsigned)((bytes * 2 + 15) >> 4), &area_seg) != 0) {
    area_seg = 0;
    return 0;
  }
  a = (u32)area_seg << 4;
  next = (a + 0xFFFFUL) & ~0xFFFFUL;
  return (a & 0xFFFFUL) + bytes <= 0x10000UL ? a : next;
}

void plat_area_free(void) {
  if (area_seg) {
    _dos_freemem(area_seg);
    area_seg = 0;
  }
}

int plat_device(const char *name) {
  int fd = open(name, O_RDONLY | O_BINARY);

  if (fd < 0)
    return 0;
  close(fd);
  return 1;
}

// esssb16's resident block: a program's own block with its signature at
// 103h, as its installed check finds it, also in upper memory (LH): the
// UMBs are linked into the chain while it looks, and the link put back
u16 plat_find_tsr(void) {
  union REGS r;
  struct SREGS sr;
  u16 mcb, owner, size, found = 0, link;
  const char __far *sig;

  r.x.ax = 0x5802;
  intdos(&r, &r);
  link = r.x.cflag ? 0xFFFF : r.h.al;
  if (link != 0xFFFF) {
    r.x.ax = 0x5803;
    r.x.bx = 1;
    intdos(&r, &r);
  }
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
      if (_fmemcmp(sig, "ESSSB16", 8) == 0 && owner != _psp) {
        found = owner;
        break;
      }
    }
    if (kind == 'Z')
      break;
    mcb = (u16)(mcb + size + 1);
  }
  if (link != 0xFFFF) {
    r.x.ax = 0x5803;
    r.x.bx = link;
    intdos(&r, &r);
  }
  return found;
}

u16 plat_windows(void) {
  union REGS r;

  r.x.ax = 0x1600;
  int86(0x2F, &r, &r);
  if (r.h.al == 0 || r.h.al == 0x80)
    return 0;
  return (u16)((r.h.al << 8) | r.h.ah);
}
