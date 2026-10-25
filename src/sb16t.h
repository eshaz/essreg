/*
 * The checks of sb16test: what DOS games do with a Sound Blaster 16, run
 * on the card the program finds, against what a Sound Blaster 16 does
 * (DSP 4.05 and the CT1745 mixer, docs/SB16.md). Each check writes a line
 * of the report: its name, PASS, FAIL, KNOWN (a difference the ES1869's
 * fronts are known to have), INFO or SKIP, and what it measured. The
 * listening checks play a sound and ask the person what they heard.
 *
 * Notes:
 *
 * The platform gives the ports, a clock, the DMA area and the person
 * (struct sbt_hal): src/sb16test.c on the machine, and the host tests run
 * the same checks on the model of the card and on both fronts
 * (tests/test_sb16test.py). Keep the code C89, with 16-bit ints.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef SB16T_H
#define SB16T_H

#include "esstypes.h"

#define SBT_VERSION "1.0"

// sbt_run's options
#define SBT_O_AUTO 0x0001   // no listening checks
#define SBT_O_LISTEN 0x0002 // the listening checks only
#define SBT_O_SHORT 0x0004  // short blocks and sounds (the host tests)
#define SBT_O_QUICK 0x0008  // fewer rates
#define SBT_O_RECORD 0x0010 // a recorded run: no questions, DTMF markers

// sbt_card.flags: what the platform found
#define SBT_F_WINDOWS 0x01 // a Windows DOS box
#define SBT_F_TSR 0x02     // esssb16.com is resident

// the size of the DMA area the platform gives, in one 64 KB page
#define SBT_AREA 0x8000U

// the most register pairs the restore file holds
#define SBT_KEEP 64

struct sbt_hal {
  u8 (*inb)(u16 port);
  void (*outb)(u16 port, u8 v);
  u32 (*now)(void);   // the clock, sbt_card.hz ticks a second
  void (*idle)(void); // time passes (the host tests' card runs)
  void (*poke)(u32 addr, const u8 *src, u16 n); // the DMA area
  void (*peek)(u32 addr, u8 *dst, u16 n);
  void (*lock)(void); // interrupts off around a few port accesses
  void (*unlock)(void);
  void (*irq_mask)(int masked); // the card's IRQ at the PIC
  // the person's answer, one of keys, to a listening check's question
  int (*ask)(const char *id, const char *question, const char *keys);
  void (*sound)(u32 rate);        // a listening sound plays at rate, or 0: ends
  void (*line)(const char *text); // a line of the report
  void (*say)(const char *text);  // progress on the screen
  int (*esc)(void);               // the person wants to stop
  // the mixer registers to put back if the program stops: n pairs of
  // register and value, n = 0 when they are back
  void (*keep)(const u8 *pairs, u16 n);
  // a section ended: the platform may add lines about the fronts' state
  void (*state)(const char *section);
};

struct sbt_card {
  u16 base;               // BLASTER's A
  u8 irq, dma8, dma16;    // I, D and H (0: none)
  u16 mpu;                // P (0: none)
  u32 hz;                 // the clock's ticks a second
  u32 area;               // the DMA area's address, SBT_AREA bytes
  u8 flags;               // SBT_F_*
  const char *const *env; // lines about the machine, NULL at the end
};

// runs the checks; the number that failed
int sbt_run(const struct sbt_hal *hal, const struct sbt_card *card,
            unsigned opts);

// the card's interrupt, from the platform's handler
void sbt_irq(void);

// what DSP 4.05 does, for the host tests too: the rate generator's value
// for 40h's time constant and for 41h's rate, and its frequency in Hz
u8 sbt_tc_to_v(u8 tc);
u8 sbt_rate_to_v(u16 rate);
double sbt_v_hz(u8 v);

#endif /* SB16T_H */
