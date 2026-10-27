/*
 * essprobe's measurements of the ES1869 itself, without a front: Audio 1
 * and Audio 2 in each of their modes, both at once, the resets, the DMA
 * controllers behind the memory manager, what the engines do to the rest
 * of the machine, and a game's refills behind the DMA. Each case is a line
 * of a table, a spec of keys over its group's base case, so that a line
 * names one combination. The report gives each case's measurements and,
 * in a recorded run, when its DTMF marker and its sound played, which
 * tools/essprec.py measures in the recording (docs/ESSPROBE.md).
 *
 * Notes:
 *
 * The platform gives the ports, a clock, the DMA area, a window on the
 * real ports past the memory manager, and the person (struct esp_hal):
 * src/essprobe.c on the machine, and the host tests run the same cases on
 * the chip model (tests/test_essprobe.py). Keep the code C89, with 16-bit
 * ints. The handler's path (esp_irq) may run on another program's stack
 * and in the middle of the cases' arithmetic, so it takes no address of a
 * local, has no varargs and does no floating point.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef ESSP_H
#define ESSP_H

#include "esstypes.h"

#define ESP_VERSION "1.5"

// esp_run's options
#define ESP_O_RECORD 0x0001 // a recorded run: the level tone, then markers
#define ESP_O_SHORT 0x0002  // the short run: the cases marked for it
#define ESP_O_SILENT 0x0004 // the cases without a sound only
#define ESP_O_TINY 0x0008   // short buffers and sounds (the host tests)

// the groups, a bit each in esp_run's mask
#define ESP_G_ENV 0x0001    // the card, the machine, the memory manager
#define ESP_G_RST 0x0002    // what each reset clears
#define ESP_G_A1 0x0004     // Audio 1 alone
#define ESP_G_A2 0x0008     // Audio 2 alone
#define ESP_G_BOTH 0x0010   // both at once
#define ESP_G_H1 0x0020     // the DMA controllers behind the memory manager
#define ESP_G_H4 0x0040     // what the engines do to the machine
#define ESP_G_STREAM 0x0080 // refills behind the DMA, as games do
#define ESP_G_FIX 0x0100    // which start keeps a 16-bit stream in step
#define ESP_G_ALL 0x01FF

// esp_card.flags
#define ESP_F_EMM386 0x01 // a V86 memory manager traps the DMA controllers
#define ESP_F_HDPMI 0x02  // HDPMI32i is resident

// the DMA area: 64 KB in one 64 KB page (so in one 128 KB page too)
#define ESP_AREA 0x10000UL

// the restore list's entries (esp_hal.keep): kind, index, value(s)
#define ESP_K_MIXER 'M'  // a mixer register
#define ESP_K_PNP 'P'    // a register of the audio device (PnP LDN 1)
#define ESP_K_CONFIG 'C' // a port of the configuration device (offset)
#define ESP_K_DAC 'V'    // a VGA DAC entry: index, then red, green, blue
#define ESP_KEEP 1024    // the list's bytes at most

// a window entry: in (its value comes back) or out
#define ESP_IN 1
#define ESP_OUT 2

struct esp_io {
  u8 kind, value;
  u16 port;
};

struct esp_hal {
  u8 (*inb)(u16 port);
  void (*outb)(u16 port, u8 v);
  u32 (*now)(void);   // the clock, esp_card.hz ticks a second
  void (*idle)(void); // time passes (the host tests' chip runs)
  void (*poke)(u32 addr, const u8 *src, u16 n); // memory, by address
  void (*peek)(u32 addr, u8 *dst, u16 n);
  void (*lock)(void); // interrupts off around a few port accesses
  void (*unlock)(void);
  void (*hook)(u8 irq);         // the card's IRQ hooked, 0: unhooked
  void (*irq_mask)(int masked); // the card's IRQ at the PIC
  // the person's key, one of keys, after the question: the level tone's
  // Enter in a recorded run
  int (*ask)(const char *id, const char *question, const char *keys);
  void (*line)(const char *text); // a line of the report
  void (*say)(const char *text);  // progress on the screen
  int (*esc)(void);               // the person wants to stop
  // what to put back if the program stops: n bytes of the restore list,
  // n = 0 when everything is back
  void (*keep)(const u8 *list, u16 n);
  // the window: n entries on the real ports, past the memory manager;
  // 0 if there is none, NULL without a memory manager
  int (*phys)(struct esp_io *io, u16 n);
};

struct esp_card {
  u16 base;               // Audio_Base: BLASTER's A, else 220h
  u32 hz;                 // the clock's ticks a second
  u32 area;               // the DMA area's address, ESP_AREA bytes
  u8 flags;               // ESP_F_*
  const char *const *env; // lines about the machine, NULL at the end
};

// runs the cases of the groups (ESP_G_*), or only those whose ids start
// with one of only's comma-separated names; the number that failed
int esp_run(const struct esp_hal *hal, const struct esp_card *card,
            unsigned opts, unsigned groups, const char *only);

// the card's interrupt, from the platform's handler
void esp_irq(void);

// the cases' table, for the host tests: the number of cases, and each
// one's id, spec and what the recording should hold
u16 esp_cases(void);
const char *esp_case_id(u16 i);
const char *esp_case_spec(u16 i);
const char *esp_case_expect(u16 i);

// a spec's keys as the runner reads them: 0 if they parse, else the
// position of the first key it doesn't know
int esp_parse_check(const char *spec);

#endif /* ESSP_H */
