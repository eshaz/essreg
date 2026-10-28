/*
 * The DOS side of sb16test and essprobe: the clock, the card's interrupt,
 * the keyboard, the report, the files that have to survive a crash, the
 * BLASTER variable, the DMA area and what runs on the machine.
 *
 * Notes:
 *
 * The clock is the PIT's channel 0 in mode 2, which counts down by one at
 * 1.193 MHz, with the BIOS's ticks above it; mode 3, the BIOS's, counts by
 * two and can't be read in between. The interrupt handler runs on
 * whatever stack it interrupts, so nothing on its path takes the address
 * of a local. DOS only, small model.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef DOSPLAT_H
#define DOSPLAT_H

#include "esstypes.h"

#define PLAT_PIT_HZ 1193182UL

struct plat_blaster {
  u16 base;            // A, else 220h
  u8 irq, dma8, dma16; // I, D and H (0: none), else 5, 1 and 5
  u16 mpu;             // P, else 330h
  char text[80];       // the variable as it was, empty if not set
};

// interrupts off around a few port accesses, nested
void plat_lock(void);
void plat_unlock(void);

// the clock: the PIT's channel 0 in mode 2, then back in the BIOS's mode 3
void plat_pit_init(void);
void plat_pit_done(void);
u32 plat_now(void);

// the card's IRQ: fn runs at each interrupt, then the EOI; masked or not
// at the PIC; the vector and the mask bit put back
void plat_hook_irq(u8 irq, void (*fn)(void));
void plat_irq_mask(int masked);
void plat_unhook_irq(void);

// the keyboard: a key, waiting for it; whether one waits
int plat_key(void);
int plat_key_ready(void);

// the report: lines with a newline each, committed after a section's
// heading, and a warning once if the disk is full
int plat_report_open(const char *name);
void plat_line(const char *text);
void plat_report_close(void);

// a file written whole as tmp, committed, closed and renamed over name:
// head, then data; 0 if anything failed, and then tmp is gone
int plat_save(const char *tmp, const char *name, const u8 *head, u16 nhead,
              const u8 *data, u16 n);

// BLASTER=A220 I5 D1 H5 P330 T6
void plat_blaster(struct plat_blaster *b);

// a DMA area of bytes (64 KB at most) in one 64 KB page, out of a DOS
// block twice as long; its address, 0 without the memory
u32 plat_area(u32 bytes);
void plat_area_free(void);

// whether a device driver of that name answers
int plat_device(const char *name);

// esssb16's resident block, 0 if it isn't there
u16 plat_find_tsr(void);

// the Windows version a DOS box runs in (int 2Fh 1600h), 0 in real DOS
u16 plat_windows(void);

#endif /* DOSPLAT_H */
