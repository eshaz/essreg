/*
 * The MIDI output devices of Windows' Multimedia control panel, for
 * essinst and esswchk: the entries of its MIDI list, and the entry of the
 * rebuilt ESFM.DRV's OPL3 device.
 *
 * Notes:
 *
 * Windows 95 and 98 list the MIDI output devices in the MIDI tab of the
 * Multimedia control panel from the registry, each by a key under
 * HKLM\System\CurrentControlSet\Control\MediaResources\midi named after
 * its driver and a number, such as esfm.drv<0000>. Driver names the
 * driver, Port the device among the driver's devices, and FriendlyName and
 * Description the name that the list shows; DevNode and SoftwareKey tie
 * the entry to the card. Windows makes the entry of a driver's first
 * device when it installs the driver, from the INF's Drivers\midi lines.
 * A driver with a second device has a second entry with Port 1, as the INF
 * of Creative's AudioPCI writes for the two devices of es1371.dll.
 *
 * ESS's INF knows one device of ESFM.DRV, so essinst copies its entry to
 * the next free number with Port 1 and the OPL3 device's name, and removes
 * that entry again when the OPL3 device goes. The name is ESS's with ESFM
 * made OPL3 FM, so Internal ESFM Synthesis becomes Internal OPL3 FM
 * Synthesis. A removal also gives the MIDI mapper's CurrentInstrument back
 * to ESS's device where it names the OPL3 device's entry.
 *
 * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */

#ifndef MIDIREG_H
#define MIDIREG_H

#include <windows.h>

#define MR_KEY 32  // an entry's key, with its 0
#define MR_NAME 48 // a device's name, with its 0

struct mr_entry {
  char key[MR_KEY];   // esfm.drv<0000>
  char driver[16];    // Driver
  char name[MR_NAME]; // FriendlyName, or Description without one
  long port;          // Port, or -1 without one
  long active;        // Active, or -1 without one
};

// the entries of the MIDI list, up to max into list: how many, or -1
// without the registry or the list
int mr_list(struct mr_entry *list, int max);

// the MIDI mapper's CurrentInstrument for the current user, "" without
// one, into out (MR_NAME bytes)
void mr_mapper(char *out);

// what mr_opl3 does
#define MR_KEEP 0    // the list is as it should be
#define MR_ADD 1     // the OPL3 device's entry is added
#define MR_REMOVE 2  // the OPL3 device's entries are removed
#define MR_NO_ESFM 3 // the list has no entry of ESS's device to copy
#define MR_FAIL 4    // the registry refused the change

// the OPL3 device's entry in the MIDI list: with on, an entry of ESFM.DRV
// with Port 1, made from ESS's device's entry if there is none; without
// on, every entry of ESFM.DRV with a Port above 0 removed. With set 0 it
// only finds out. MR_*, with the entry that is or would be added, or the
// first one removed, in e
int mr_opl3(int on, int set, struct mr_entry *e);

#endif /* MIDIREG_H */
