/*
 * esfmbank.h -- ESFM patch banks and the ESFM.DRV that plays them.
 *
 * A bank starts with 256 little-endian offsets: entries 0-127 are the
 * General MIDI programs, 128-255 the percussion notes 0-127 of channel 10;
 * 0 means silent.  Each patch is one or two 36-byte voices (a 4-byte
 * header and four 8-byte operator records); bits 2:1 of the first header
 * byte say how many (0: one, 1 and 2: two, 3: none).  ESFM.DRV keeps the
 * bank as resource type 256, ID 1234 and copies it into global memory when
 * it is enabled.  A bank file is either the raw bank or a RIFF "Ptch"
 * file with the bank in an "fm4 " chunk (the format of the driver's
 * dormant file loader).  See docs/ESFM_BANK.md.
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#ifndef ESFMBANK_H
#define ESFMBANK_H

#include <stdio.h>

#include "esstypes.h"

#define BANK_ENTRIES 256
#define BANK_TABLE (2 * BANK_ENTRIES)
#define BANK_VOICE 36
#define BANK_ORIG_SIZE 0x2060 /* the bank of the shipped ESFM.DRV */
#define BANK_MAX 0x7FF0       /* the driver sign-extends the size (cwd) */
#define BANK_RES_TYPE 256
#define BANK_RES_ID 1234

struct bank_info {
  u16 size;    /* bank bytes */
  u16 patches; /* nonzero entries */
  u16 two;     /* two-voice patches */
  char why[80]; /* what is wrong, if bank_check failed */
};

/* 0 if `bank` (size bytes) is a usable bank, else -1 and info->why */
int bank_check(const u8 *bank, u32 size, struct bank_info *info);

/* where the bank is inside a bank file: the whole file, or the "fm4 "
 * chunk of a RIFF "Ptch" file; -1 if a RIFF file has no bank */
int bank_unwrap(const u8 *file, u32 size, u32 *offset, u32 *length);

/* RIFF "Ptch" wrapping for a bank of `size` bytes: 20-byte header, then
 * the bank, then a pad byte if size is odd */
void bank_riff_header(u8 hdr[20], u16 size);

/* --- ESFM.DRV ------------------------------------------------------------ */

struct esfm_drv {
  u32 file_size;
  u32 ne;         /* NE header */
  u32 seg3;       /* segment 3: bank loader and DriverProc */
  u32 res_entry;  /* resource table entry of the bank */
  u16 res_shift;  /* resource alignment */
  u32 bank_off;   /* resource data */
  u32 bank_len;   /* resource length (aligned) */
  u16 bank_size;  /* bytes the loader copies */
  u8 autodata;    /* segment number of the data segment */
  char why[80];   /* why the file is not the known build */
};

/* inspect a driver file; 0 if it is the ESFM.DRV build whose loader
 * essreg knows (any bank size), else -1 and d->why */
int esfm_drv_inspect(FILE *f, struct esfm_drv *d);

/* put `bank` into the driver (in place when it fits, else appended at the
 * end with the resource entry moved) and set the loader's size constants;
 * f must be opened "r+b" and inspected.  0 on success, -1 and d->why. */
int esfm_drv_patch(FILE *f, struct esfm_drv *d, const u8 *bank, u16 size);

/* offsets of the loader's size constants in segment 3 */
#define ESFM_K_ALLOC 0x0670 /* mov ax,size; cwd; push dx; push ax: GlobalAlloc */
#define ESFM_K_WORDS 0x06FC /* mov cx,size/2; rep movsw */
#define ESFM_K_RIFF1 0x077C /* cmp [bp-2Ch],size (RIFF loader cap) */
#define ESFM_K_RIFF2 0x0783 /* mov [bp-2Ch],size */

#endif /* ESFMBANK_H */
