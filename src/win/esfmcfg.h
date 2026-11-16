/*
 * ESFMCFG.DLL, the box of the patch banks of ESFM.DRV's two MIDI devices,
 * which the Multimedia control panel's Settings button opens (see
 * esfmcfg.c).
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef ESFMCFG_H
#define ESFMCFG_H

#define IDD_BANKS 100
#define IDC_ESFM_LABEL 101
#define IDC_ESFM 102
#define IDC_ESFM_BROWSE 103
#define IDC_OPL3_LABEL 104
#define IDC_OPL3 105
#define IDC_OPL3_BROWSE 106
#define IDC_NOTE 107

// the devices: ESFM.DRV's device 0, and the OPL3 device, its device 1
// (ESFMOPL3.DRV's)
#define CFG_ESFM 0
#define CFG_OPL3 1

#ifndef RC_INVOKED
// the box, over parent, with device's list first; DRVCNF_OK when it ended
// with OK, else DRVCNF_CANCEL (0)
UINT FAR PASCAL BankDialog(HWND parent, UINT device);
#endif

#endif /* ESFMCFG_H */
