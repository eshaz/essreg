/*
 * The Intel PIIX4's Type F DMA timing (piix4.h): the bridge found on bus 0,
 * its MBDMA registers read and explained, and the one register that names
 * a channel changed.
 *
 * Notes:
 *
 * A configuration access puts CF8h back as it found it, with interrupts
 * off from the address to the restore, so that no other code's access comes
 * between. The DOS access (piix4_dos) is 32-bit port I/O in 16-bit code,
 * which needs a 386.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>

#include "piix4.h"

#define PCI_ON 0x80000000UL
#define PIIX4_ID 0x71108086UL

int piix4_pci(const struct piix4_io *io) {
  u32 old, got;

  io->lock();
  old = io->addr_get();
  io->addr_put(PCI_ON);
  got = io->addr_get();
  io->addr_put(old);
  io->unlock();
  return (got & 0xFFFFFFFFUL) == PCI_ON;
}

u32 piix4_read(const struct piix4_io *io, u32 addr) {
  u32 old, v;

  io->lock();
  old = io->addr_get();
  io->addr_put(addr);
  v = io->data_get();
  io->addr_put(old);
  io->unlock();
  return v & 0xFFFFFFFFUL;
}

static u32 at(u8 dev, u8 reg) {
  return PCI_ON | ((u32)dev << 11) | (reg & 0xFC);
}

u8 piix4_find(const struct piix4_io *io) {
  u8 dev;

  if (!piix4_pci(io))
    return PIIX4_NONE;
  for (dev = 0; dev < 32; dev++)
    if (piix4_read(io, at(dev, 0)) == PIIX4_ID)
      return dev;
  return PIIX4_NONE;
}

void piix4_mbdma(const struct piix4_io *io, u8 dev, u8 r[2]) {
  u32 v = piix4_read(io, at(dev, PIIX4_REG));

  r[0] = (u8)(v >> 16);
  r[1] = (u8)(v >> 24);
}

int piix4_reg_of(const u8 r[2], u8 ch) {
  int i;

  for (i = 0; i < 2; i++)
    if ((r[i] & PIIX4_CH) == ch)
      return i;
  return -1;
}

int piix4_typef(const struct piix4_io *io, u8 dev, u8 ch, int on, u8 r[2]) {
  int i;
  u8 v;
  u32 old;

  piix4_mbdma(io, dev, r);
  i = piix4_reg_of(r, ch);
  if (i < 0)
    return -1;
  v = on ? (u8)(r[i] | PIIX4_TYPEF) : (u8)(r[i] & ~PIIX4_TYPEF);
  if (v == r[i])
    return 0;
  io->lock();
  old = io->addr_get();
  io->addr_put(at(dev, PIIX4_REG));
  io->data_put8((u8)((PIIX4_REG + i) & 3), v);
  io->addr_put(old);
  io->unlock();
  piix4_mbdma(io, dev, r);
  return 1;
}

static void one(char *buf, u8 reg, u8 v) {
  if ((v & PIIX4_CH) == 4)
    sprintf(buf, "%02Xh=%02Xh (%sno channel)", reg, v,
            (v & PIIX4_TYPEF) ? "Type F, " : "");
  else
    sprintf(buf, "%02Xh=%02Xh (%sDMA %u)", reg, v,
            (v & PIIX4_TYPEF) ? "Type F, " : "", (unsigned)(v & PIIX4_CH));
}

void piix4_text(char *buf, u8 dev, const u8 r[2]) {
  char a[32], b[32];

  one(a, PIIX4_REG, r[0]);
  one(b, PIIX4_REG + 1, r[1]);
  sprintf(buf, "PIIX4 0:%02X.0: %s, %s", dev, a, b);
}

#ifndef ESS_HOST
#include <conio.h>
#include <i86.h>

// clang-format off
static u32 dos_addr_get(void) {
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

static void dos_addr_put(u32 a) {
  u16 lo = (u16)a, hi = (u16)(a >> 16);

  _asm {
    mov ax, hi
    shl eax, 16
    mov ax, lo
    mov dx, 0x0CF8
    out dx, eax
  }
}

static u32 dos_data_get(void) {
  u16 lo = 0, hi = 0;

  _asm {
    mov dx, 0x0CFC
    in eax, dx
    mov lo, ax
    shr eax, 16
    mov hi, ax
  }
  return ((u32)hi << 16) | lo;
}
// clang-format on

static void dos_data_put8(u8 off, u8 v) { outp(0x0CFC + off, v); }

static void dos_lock(void) { _disable(); }

static void dos_unlock(void) { _enable(); }

const struct piix4_io piix4_dos = {dos_addr_get,  dos_addr_put, dos_data_get,
                                   dos_data_put8, dos_lock,     dos_unlock};
#endif
