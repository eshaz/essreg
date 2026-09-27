/*
 * Replaces the FM patch bank of the running ESFM.DRV.
 *
 * Notes:
 *
 * ESFM.DRV copies its patch bank (resource type 256, ID 1234)
 * into a global memory block when it is enabled, and plays
 * every MIDI program from that block. essctl copies a bank
 * file over the block, growing it when needed, so the new
 * sounds play from the next note on until the driver is
 * disabled or Windows restarts.
 *
 * The original bank is restored from the driver's own
 * resource. See docs/ESFM_BANK.md.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef ESFMLIVE_H
#define ESFMLIVE_H

#include "esstypes.h"

struct esfm_status {
  int loaded;     // ESFM.DRV is loaded
  int known;      // the driver is the build essctl knows how to patch
  u32 bank_size;  // size of the bank block in memory
  char path[144]; // file name of the driver
  char why[96];   // why it cannot be patched
};

void esfm_get_status(struct esfm_status *st);

// 0 on success, the result message goes to msg
// with the fixed ESFM.DRV (build/ESFM.DRV) a load also names the file in
// SYSTEM.INI [ESFM.DRV] Bank=, so the driver keeps playing it, and a
// restore removes the setting
int esfm_live_load(const char *path, char *msg, unsigned size);
int esfm_live_restore(char *msg, unsigned size);

// where the fixed ESFM.DRV finds its bank file
#define ESFM_INI_FILE "SYSTEM.INI"
#define ESFM_INI_SECTION "ESFM.DRV"
#define ESFM_INI_KEY "Bank"

// last bank loaded in this session ("" if none)
extern char esfm_last_bank[144];

// voices and state of the running driver, for the ESFM page and the
// stress test (see docs/ESFM_MIDI.md)
#define ESFM_VOICES 18

struct esfm_voice {
  u8 flags;   // 1 keyed on, 2 keyed off, 4 note off held by the pedal,
              // 8 second voice of a patch
  u8 channel; // 0-15
  u8 note;
  u8 chip;    // key on read back from the chip (0 or 1), 0xFF if not read
  u32 age;    // note ons since this voice was keyed on
};

struct esfm_diag {
  int device;         // the driver has an ES1869 device
  int open;           // a program has the MIDI device open
  int suspended;      // power suspend
  u16 fm_port;
  struct esfm_voice v[ESFM_VOICES];
  int chip_read;      // v[].chip was read from the chip
  int fixed;          // the driver has the stuck note fix (build/ESFM.DRV)
  u32 queued;         // fixed driver, messages that came while it was busy
  u32 overflow;       // fixed driver, messages refused with a full queue
  u16 maxdepth;
  u16 purged;
  u16 version;        // fixed driver: 2 has the bank file
  u16 file_state;     // ESFM_FILE_* below
  u16 file_used;      // the bank that plays came from the file
  u16 file_size;      // bytes of that bank
  u16 file_loads;     // times the driver loaded the file
  u16 file_checks;    // times it checked the file (at MODM_OPEN)
  u16 file_date;      // DOS date and time of the file it read last
  u16 file_time;
  char file[128];     // Bank= from SYSTEM.INI
  char why[96];       // why nothing could be read
};

// file_state: what the last MODM_OPEN found
#define ESFM_FILE_NONE 0    // no bank file plays
#define ESFM_FILE_LOADED 1  // the file's bank plays
#define ESFM_FILE_MISSING 2 // the file can't be opened or read
#define ESFM_FILE_BAD 3     // the file isn't a patch bank
#define ESFM_FILE_NOMEM 4   // not enough memory for it

// read the driver's state, 0 on success
// read_chip also reads the key-on registers, only while a program has the
// device open so that Windows owns FM
int esfm_diag_read(struct esfm_diag *d, int read_chip);

// the state as text with lines ended by nl, rc is what esfm_diag_read
// returned
// a voice keyed on in the chip that the driver thinks is free, or keyed on
// with no note playing, is a stuck note
void esfm_diag_text(const struct esfm_diag *d, int rc, const char *nl,
                    char *buf, unsigned size);

#endif /* ESFMLIVE_H */
