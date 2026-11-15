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
 * its driver and the number of the card's install, such as esfm.drv<0018>
 * for the card in the MEDIA class's 0018. Driver names the driver, Port the
 * device among the driver's devices, FriendlyName and Description the name
 * that the list shows, and Active whether the card is there; DevNode and
 * SoftwareKey tie the entry to the card. An earlier install of the card
 * leaves its entry with Active 0. Windows makes the entry of a driver's
 * first device when it installs the driver, from the INF's Drivers\midi
 * lines. A driver with a second device has a second entry with Port 1, as
 * the INF of Creative's AudioPCI writes for the two devices of es1371.dll.
 * The MIDI mapper keeps the device it plays on by the device's own name
 * (CurrentInstrument, "ESFM Synthesis (220)").
 *
 * ESS's INF knows one device of ESFM.DRV, so essinst copies the entry of
 * ESS's device on the card that is there (Active 1) with Port 1 and the
 * OPL3 device's name, to a number from 9001 up, which no install of a card
 * takes. A copy that no longer matches, after an install of the card or
 * from an earlier essinst, is replaced, and the copy goes again when the
 * OPL3 device goes. The name is ESS's with ESFM made OPL3 FM, so Internal
 * ESFM Synthesis becomes Internal OPL3 FM Synthesis. A removal also gives
 * the MIDI mapper's CurrentInstrument back to ESS's device where it names
 * the OPL3 device, by its key, its name or the device's own name.
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
  char key[MR_KEY];      // esfm.drv<0018>
  char driver[16];       // Driver
  char name[MR_NAME];    // FriendlyName, or Description without one
  long port;             // Port, or -1 without one
  long active;           // Active, or -1 without one
  unsigned long devnode; // DevNode, where has_devnode
  int has_devnode;
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
#define MR_NO_ESFM 3 // the list has no entry of ESS's device on the card
#define MR_FAIL 4    // the registry refused the change
#define MR_TIDY 5    // the entry stays, and copies that don't match go

// the OPL3 device's entry in the MIDI list: with on, an entry of ESFM.DRV
// with Port 1 that matches ESS's device's entry on the card, made from it
// if there is none, and every other entry of ESFM.DRV with a Port above 0
// removed; without on, all of those removed. With set 0 it only finds out.
// MR_*, with the entry that stays or is added, or the first one removed,
// in e, and how many others go in stale
int mr_opl3(int on, int set, struct mr_entry *e, int *stale);

#endif /* MIDIREG_H */
