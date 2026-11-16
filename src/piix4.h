/*
 * The Intel PIIX4's Type F DMA timing, which its ISA bridge keeps for the
 * motherboard's DMA channels in two registers (MBDMA, 76h and 77h of
 * function 0's configuration space): bit 7 turns the timing on, and bits
 * 2:0 name the channel (4 for none). The Compaq Armada 1500c's BIOS turns
 * it on for both of the ES1869's channels (76h = 8Dh for DMA 5, 77h = 89h
 * for DMA 1), and under Linux a recording hung the machine with it on.
 *
 * Notes:
 *
 * The rule lives here only: act on an Intel PIIX4 (8086:7110) at function
 * 0 of a bus-0 device, and only on the register that names the channel
 * asked for, never one that names another device's channel. A write is one
 * byte at CFCh + (the register & 3), with CF8h put back after each access
 * and interrupts off around it. The accesses go through struct piix4_io,
 * which the DOS programs fill with configuration mechanism 1 (piix4_dos)
 * and the host tests with a model (tests/test_piix4.py).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef PIIX4_H
#define PIIX4_H

#include "esstypes.h"

#define PIIX4_NONE 0xFF  // no PIIX4 on bus 0
#define PIIX4_TYPEF 0x80 // MBDMA bit 7: Type F timing
#define PIIX4_CH 0x07    // MBDMA bits 2:0: the channel, 4 for none
#define PIIX4_REG 0x76   // the first MBDMA register; 77h is the second

struct piix4_io {
  u32 (*addr_get)(void);           // CF8h as it is
  void (*addr_put)(u32 addr);      // CF8h
  u32 (*data_get)(void);           // the dword at CFCh
  void (*data_put8)(u8 off, u8 v); // the byte at CFCh + off
  void (*lock)(void);              // interrupts off around an access
  void (*unlock)(void);
};

// 1 when configuration mechanism 1 answers (CF8h keeps an address)
int piix4_pci(const struct piix4_io *io);

// the dword at a configuration address, CF8h put back
u32 piix4_read(const struct piix4_io *io, u32 addr);

// the device number of a PIIX4's function 0 on bus 0, or PIIX4_NONE
u8 piix4_find(const struct piix4_io *io);

// 76h and 77h of device dev into r[0] and r[1]
void piix4_mbdma(const struct piix4_io *io, u8 dev, u8 r[2]);

// 0 for 76h or 1 for 77h, the register that names channel ch, or -1
int piix4_reg_of(const u8 r[2], u8 ch);

// Type F on or off for channel ch: 1 when it wrote the register naming ch,
// 0 when that register was already so, -1 when none names ch; r[] read
// back afterwards
int piix4_typef(const struct piix4_io *io, u8 dev, u8 ch, int on, u8 r[2]);

// the registers as a line: "PIIX4 0:07.0: 76h=8Dh (Type F, DMA 5),
// 77h=89h (Type F, DMA 1)"; buf takes 80 characters
void piix4_text(char *buf, u8 dev, const u8 r[2]);

#ifndef ESS_HOST
extern const struct piix4_io piix4_dos; // mechanism 1 by 32-bit port I/O
#endif

#endif /* PIIX4_H */
