/*
 * The settings of ESS's Windows 95 drivers in the registry: the values
 * under HKLM\System\CurrentControlSet\Services\Class\Media\<nnnn>\Config,
 * the software key of the ES1869 (the one whose Driver is es1869.vxd).
 *
 * Notes:
 *
 * ES1869.DRV reads its values when Windows starts, so a change takes
 * effect at the next start (docs/DRIVER_CONFIG.md).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef DRVCFG_H
#define DRVCFG_H

#include <windows.h>

// nonzero: ES1869.DRV never gives the music DAC to the I2S input
#define DRVCFG_WAVETABLE "ESSWaveTableChip"

// 1 and the value, 0 if the value isn't there (the driver's default
// applies), -1 if there's no ES1869 key or no registry
int drvcfg_get(const char *name, DWORD *value);

// writes a 4-byte binary value, as ESS's OEMSETUP.INF does: 0 or -1
int drvcfg_set(const char *name, DWORD value);

#endif /* DRVCFG_H */
