/*
 * esfmlive.h -- replace the FM patch bank of the running ESFM.DRV.
 *
 * ESFM.DRV copies its patch bank (resource type 256, ID 1234) into a
 * global memory block when it is enabled and plays every MIDI program from
 * that block.  essctl copies a bank file over the block (growing it when
 * needed), so the new sounds are used from the next note on, until the
 * driver is disabled or Windows restarts.  The original bank is restored
 * from the driver's own resource.  See docs/ESFM_BANK.md.
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#ifndef ESFMLIVE_H
#define ESFMLIVE_H

#include "esstypes.h"

struct esfm_status {
  int loaded;     /* ESFM.DRV is loaded */
  int known;      /* its code is the build essctl knows how to patch */
  u32 bank_size;  /* size of the bank block in memory */
  char path[144]; /* file name of the driver */
  char why[96];   /* why it cannot be patched */
};

void esfm_get_status(struct esfm_status *st);

/* 0 on success, else a message in msg */
int esfm_live_load(const char *path, char *msg, unsigned size);
int esfm_live_restore(char *msg, unsigned size);

/* the bank loaded last in this session ("" if none) */
extern char esfm_last_bank[144];

/* voices and state of the running driver, for the ESFM page and the
 * stress test (see docs/ESFM_MIDI.md) */
#define ESFM_VOICES 18

struct esfm_voice {
  u8 flags;   /* 1 keyed on, 2 keyed off, 4 note off held by the pedal,
                 8 second voice of a patch */
  u8 channel; /* 0-15 */
  u8 note;
  u8 chip;    /* key on as read back from the chip: 0, 1, or 0xFF unread */
  u32 age;    /* note ons since this voice was keyed on */
};

struct esfm_diag {
  int device;         /* the driver has an ES1869 device */
  int open;           /* a program has the MIDI device open */
  int suspended;      /* power suspend */
  u16 fm_port;
  u16 pedal;          /* bit n: sustain pedal down on channel n+1 */
  struct esfm_voice v[ESFM_VOICES];
  int chip_read;      /* v[].chip was read from the chip */
  int fixed;          /* the driver has the stuck-note fix (build/ESFM.DRV) */
  u32 queued;         /* fixed driver: messages that came while it was busy */
  u32 overflow;       /* ... refused because its queue was full */
  u16 maxdepth;
  u16 purged;
  char why[96];       /* why nothing could be read */
};

/* 0 when the state was read; read_chip also reads the key-on registers
 * (only while a program has the device open, so that Windows owns FM) */
int esfm_diag_read(struct esfm_diag *d, int read_chip);

/* the state as text, lines ended by nl; rc is what esfm_diag_read returned.
 * A voice keyed on in the chip that the driver thinks is free, or keyed on
 * with no note playing, is a stuck note. */
void esfm_diag_text(const struct esfm_diag *d, int rc, const char *nl,
                    char *buf, unsigned size);

#endif /* ESFMLIVE_H */
