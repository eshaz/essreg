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

#endif /* ESFMLIVE_H */
