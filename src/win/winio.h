/*
 * How essctl reaches the ES1869:
 *
 *   VxD API     the essreg functions of the extended ES1869.VXD
 *               (group 4), ring-0 access that never changes device
 *               ownership
 *   direct      port I/O from Windows, when ES1869.VXD is loaded
 *               each batch of accesses is bracketed by
 *               vxd_dsp_begin/end
 *   simulated   the simhw model, for trying essctl without the card
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef WINIO_H
#define WINIO_H

#include "esstypes.h"

struct winio_opts {
  u16 audio_base;  // /base=, 0 = from the driver or 220h
  u16 config_base; // /cfg=, 0 = from the driver or the chip
  u8 sim;          // /sim
  u8 novxd;        // /novxd, direct I/O even with the extended VxD
};

enum winio_path { WIO_VXDEXT, WIO_DIRECT_VXD, WIO_DIRECT, WIO_SIM };

extern int winio_path;
extern int winio_present; // an ES1869 answered (or the driver has one)

// choose the access path, returns 0 or a negative ESSHW_E* code when no
// ES1869 answers (essctl still runs, the values show errors)
int winio_init(const struct winio_opts *opts);

// bracket a batch of register accesses (no yielding in between)
// returns 0, -ESSHW_EINUSE when a DOS box owns the DSP, or -ESSHW_ENODEV
// when no ES1869 was found
int winio_begin(void);
void winio_end(void);

// 1 when reading registers has no side effects on the driver, so the
// display can refresh on a timer
int winio_can_poll(void);

// one-line descriptions for the status bar
void winio_path_text(char *buf, unsigned size);
void winio_owner_text(char *buf, unsigned size);

#endif /* WINIO_H */
