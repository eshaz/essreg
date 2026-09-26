/*
 * esfmbank.c -- ESFM patch banks and ESFM.DRV (see esfmbank.h).
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#include <stdio.h>
#include <string.h>

#include "esfmbank.h"

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }

static u32 rd32(const u8 *p) {
  return rd16(p) | ((u32)rd16(p + 2) << 16);
}

static void wr16(u8 *p, u16 v) {
  p[0] = (u8)v;
  p[1] = (u8)(v >> 8);
}

static void wr32(u8 *p, u32 v) {
  wr16(p, (u16)v);
  wr16(p + 2, (u16)(v >> 16));
}

static void set_why(char *why, const char *text, unsigned n) {
  char tmp[80];
  sprintf(tmp, text, n);
  strcpy(why, tmp);
}

/* --- banks --------------------------------------------------------------- */

int bank_check(const u8 *bank, u32 size, struct bank_info *info) {
  unsigned i, voices;
  u16 off;

  memset(info, 0, sizeof(*info));
  if (size < BANK_TABLE) {
    set_why(info->why, "too short for the 256-entry table (%u bytes)",
            (unsigned)size);
    return -1;
  }
  if (size > BANK_MAX) {
    set_why(info->why, "larger than the driver can load (%u bytes)",
            (unsigned)size);
    return -1;
  }
  info->size = (u16)size;
  for (i = 0; i < BANK_ENTRIES; i++) {
    off = rd16(bank + 2 * i);
    if (!off)
      continue;
    if (off < BANK_TABLE || off >= size) {
      set_why(info->why, "entry %u points outside the patches", i);
      return -1;
    }
    switch ((bank[off] >> 1) & 3) {
    case 0:
      voices = 1;
      break;
    case 3:
      voices = 0; /* the driver ignores it */
      break;
    default:
      voices = 2;
      info->two++;
    }
    if ((u32)off + voices * BANK_VOICE > size) {
      set_why(info->why, "the patch of entry %u runs past the end", i);
      return -1;
    }
    info->patches++;
  }
  if (!info->patches) {
    strcpy(info->why, "no patches");
    return -1;
  }
  return 0;
}

int bank_unwrap(const u8 *file, u32 size, u32 *offset, u32 *length) {
  u32 pos, len;

  if (size < 12 || memcmp(file, "RIFF", 4) || memcmp(file + 8, "Ptch", 4)) {
    *offset = 0;
    *length = size;
    return 0;
  }
  for (pos = 12; pos + 8 <= size; pos += 8 + len + (len & 1)) {
    len = rd32(file + pos + 4);
    if (len > size - pos - 8)
      return -1;
    if (!memcmp(file + pos, "fm4 ", 4)) {
      *offset = pos + 8;
      *length = len;
      return 0;
    }
  }
  return -1;
}

void bank_riff_header(u8 hdr[20], u16 size) {
  memcpy(hdr, "RIFF", 4);
  wr32(hdr + 4, 4 + 8 + (u32)size + (size & 1));
  memcpy(hdr + 8, "Ptchfm4 ", 8);
  wr32(hdr + 16, size);
}

/* --- ESFM.DRV ------------------------------------------------------------ */

/* code of the bank loader (segment 3, DRV_ENABLE) around the constants */
static const struct {
  u16 off;
  u8 len;
  u8 bytes[14];
} loader_sig[] = {
    /* push bp; mov bp,sp; sub sp,134h; push di; push si;
     * mov ax,2042h; push ax; mov ax, */
    {0x0662, 14, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x34, 0x01, 0x57, 0x56, 0xB8,
                  0x42, 0x20, 0x50, 0xB8}},
    /* <size>; cwd; push dx; push ax; call far GlobalAlloc */
    {0x0672, 4, {0x99, 0x52, 0x50, 0x9A}},
    /* mov word [12h],0; mov [14h],ax; or ax,[12h] */
    {0x067A, 13, {0xC7, 0x06, 0x12, 0x00, 0x00, 0x00, 0xA3, 0x14, 0x00, 0x0B,
                  0x06, 0x12, 0x00}},
    /* mov ax,1234; cwd; push dx; push ax; mov ax,256 (FindResource) */
    {0x06B4, 9, {0xB8, 0xD2, 0x04, 0x99, 0x52, 0x50, 0xB8, 0x00, 0x01}},
    /* mov cx,<size/2>; rep movsw */
    {0x06FB, 1, {0xB9}},
    {0x06FE, 2, {0xF3, 0xA5}},
    /* cmp word [bp-2Ch],<size>; jna; mov word [bp-2Ch],<size> */
    {0x0779, 3, {0x81, 0x7E, 0xD4}},
    {0x077E, 5, {0x76, 0x08, 0xC7, 0x46, 0xD4}},
};

static int read_at(FILE *f, u32 pos, u8 *buf, unsigned n) {
  return fseek(f, (long)pos, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
}

static int write_at(FILE *f, u32 pos, const u8 *buf, unsigned n) {
  return fseek(f, (long)pos, SEEK_SET) == 0 && fwrite(buf, 1, n, f) == n;
}

int esfm_drv_inspect(FILE *f, struct esfm_drv *d) {
  u8 buf[64];
  u32 res, pos;
  u16 k[4], nseg, align;
  unsigned i;

  memset(d, 0, sizeof(*d));
  if (fseek(f, 0, SEEK_END) != 0)
    goto io;
  d->file_size = (u32)ftell(f);
  if (!read_at(f, 0, buf, 64) || buf[0] != 'M' || buf[1] != 'Z') {
    strcpy(d->why, "not an executable");
    return -1;
  }
  d->ne = rd32(buf + 0x3C);
  if (!read_at(f, d->ne, buf, 64) || buf[0] != 'N' || buf[1] != 'E' ||
      buf[0x36] != 2) {
    strcpy(d->why, "not a 16-bit Windows driver");
    return -1;
  }
  d->autodata = (u8)rd16(buf + 0x0E);
  nseg = rd16(buf + 0x1C);
  align = rd16(buf + 0x32);
  res = d->ne + rd16(buf + 0x24);
  if (nseg < 4 || align > 12)
    goto unknown;
  if (!read_at(f, d->ne + rd16(buf + 0x22) + 2 * 8, buf, 8))
    goto io;
  d->seg3 = (u32)rd16(buf) << align;

  /* resource table: shift, then per type {type, count, 4 reserved} and
   * count entries {offset, length, flags, id, 4 reserved} */
  if (!read_at(f, res, buf, 2))
    goto io;
  d->res_shift = rd16(buf);
  if (d->res_shift > 12)
    goto unknown;
  for (pos = res + 2;; pos += 8 + 12 * (u32)rd16(buf + 2)) {
    if (!read_at(f, pos, buf, 8))
      goto io;
    if (rd16(buf) == 0)
      break;
    if (rd16(buf) == (0x8000 | BANK_RES_TYPE)) {
      u16 count = rd16(buf + 2);
      for (i = 0; i < count; i++) {
        u8 e[12];
        if (!read_at(f, pos + 8 + 12 * i, e, 12))
          goto io;
        if (rd16(e + 6) == (0x8000 | BANK_RES_ID)) {
          d->res_entry = pos + 8 + 12 * i;
          d->bank_off = (u32)rd16(e) << d->res_shift;
          d->bank_len = (u32)rd16(e + 2) << d->res_shift;
        }
      }
      break;
    }
  }
  if (!d->res_entry) {
    strcpy(d->why, "no patch bank resource (type 256, ID 1234)");
    return -1;
  }

  for (i = 0; i < sizeof(loader_sig) / sizeof(loader_sig[0]); i++)
    if (!read_at(f, d->seg3 + loader_sig[i].off, buf, loader_sig[i].len) ||
        memcmp(buf, loader_sig[i].bytes, loader_sig[i].len))
      goto unknown;
  if (!read_at(f, d->seg3 + ESFM_K_ALLOC, buf, 2))
    goto io;
  k[0] = rd16(buf);
  if (!read_at(f, d->seg3 + ESFM_K_WORDS, buf, 2))
    goto io;
  k[1] = rd16(buf);
  if (!read_at(f, d->seg3 + ESFM_K_RIFF1, buf, 2))
    goto io;
  k[2] = rd16(buf);
  if (!read_at(f, d->seg3 + ESFM_K_RIFF2, buf, 2))
    goto io;
  k[3] = rd16(buf);
  if (k[1] * 2u != k[0] || k[2] != k[0] || k[3] != k[0] ||
      k[0] > BANK_MAX || k[0] > d->bank_len) {
    strcpy(d->why, "the bank size constants of the loader disagree");
    return -1;
  }
  d->bank_size = k[0];
  return 0;

unknown:
  strcpy(d->why, "not the ESFM.DRV build essreg knows how to patch");
  return -1;
io:
  strcpy(d->why, "read error");
  return -1;
}

int esfm_drv_patch(FILE *f, struct esfm_drv *d, const u8 *bank, u16 size) {
  static const u8 zeros[16];
  u16 even = (u16)((size + 1) & ~1u);
  u32 unit = 1UL << d->res_shift;
  u32 at, len;
  u8 buf[4];

  if (!size || even > BANK_MAX) {
    strcpy(d->why, "bank too large");
    return -1;
  }
  if (even <= d->bank_len) {
    /* fits in the resource: overwrite it, clear what is left */
    at = d->bank_off;
    len = d->bank_len;
  } else {
    /* append at the end, aligned, and move the resource entry there */
    at = (d->file_size + unit - 1) & ~(unit - 1);
    len = (even + unit - 1) & ~(unit - 1);
    if ((at >> d->res_shift) > 0xFFFF || (len >> d->res_shift) > 0xFFFF) {
      strcpy(d->why, "file too large for the resource table");
      return -1;
    }
    if (at > d->file_size &&
        !write_at(f, d->file_size, zeros, (unsigned)(at - d->file_size)))
      goto io;
  }
  if (!write_at(f, at, bank, size))
    goto io;
  if (fseek(f, (long)(at + size), SEEK_SET) != 0)
    goto io;
  {
    u32 n = len - size;
    while (n) {
      unsigned chunk = n > sizeof(zeros) ? sizeof(zeros) : (unsigned)n;
      if (fwrite(zeros, 1, chunk, f) != chunk)
        goto io;
      n -= chunk;
    }
  }
  if (at != d->bank_off || len != d->bank_len) {
    wr16(buf, (u16)(at >> d->res_shift));
    wr16(buf + 2, (u16)(len >> d->res_shift));
    if (!write_at(f, d->res_entry, buf, 4))
      goto io;
  }
  wr16(buf, even);
  if (!write_at(f, d->seg3 + ESFM_K_ALLOC, buf, 2) ||
      !write_at(f, d->seg3 + ESFM_K_RIFF1, buf, 2) ||
      !write_at(f, d->seg3 + ESFM_K_RIFF2, buf, 2))
    goto io;
  wr16(buf, (u16)(even / 2));
  if (!write_at(f, d->seg3 + ESFM_K_WORDS, buf, 2))
    goto io;
  if (fflush(f) != 0)
    goto io;
  d->bank_off = at;
  d->bank_len = len;
  d->bank_size = even;
  if (at + len > d->file_size)
    d->file_size = at + len;
  return 0;
io:
  strcpy(d->why, "write error");
  return -1;
}
