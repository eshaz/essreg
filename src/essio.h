/*
 * essio.h -- read and write catalog registers and fields through esshw.
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 * Licensed under GPL Version 3.0
 */

#ifndef ESSIO_H
#define ESSIO_H

#include "esscat.h"

#define ESSIO_ETIER 20 /* the field may not be written at this tier */

/* register value (0-255) or negative ESSHW_E* / -ESSIO_ETIER */
int ess_read(int reg);
int ess_write(int reg, u8 value);

/* field value via *value (and the whole register via *raw if not NULL) */
int ess_field_read(int field, u8 *value, u8 *raw);

/* read-modify-write one field; K_PULSE writes 1 then 0, K_ACTION writes
 * the field's maximum.  Registers that are write-only or change when read
 * are written from 0 instead of their current value.  Refused unless
 * cat_writable(field, expert). */
int ess_field_write(int field, u8 value, int expert);

#endif /* ESSIO_H */
