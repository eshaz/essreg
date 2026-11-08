/*
 * ess3d measure: the 3-D effect measured through its own output, with a
 * report of what each setting does (see ess3dms.c).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef ESS3DMS_H
#define ESS3DMS_H

#include <windows.h>

#include "ess3d.h"

// the measurement's window, for FindWindow
#define MEASURE_CLASS "Ess3dMeasure"

// run the measurement in its window until it's closed; returns ess3d's
// exit code
int measure_main(const struct ess3d_cmd *c, HINSTANCE inst);

// put back the mixer registers that a measurement ended by force left
// changed, unless one is running; the chip has to be reachable (winio)
void measure_put_back(int sim);

#endif /* ESS3DMS_H */
