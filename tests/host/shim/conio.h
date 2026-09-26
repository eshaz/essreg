/* host shim: port I/O goes to the simulated ES1869 */
#ifndef SHIM_CONIO_H
#define SHIM_CONIO_H
#include "simhw.h"
#define inp(p) simhw_in((u16)(p))
#define outp(p, v) simhw_out((u16)(p), (u8)(v))
#endif
