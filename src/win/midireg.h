/*
 * The MIDI output devices of Windows' Multimedia control panel, for
 * essinst and esswchk: the entries of its MIDI list, and ESFMOPL3.DRV, the
 * driver of the rebuilt ESFM.DRV's OPL3 device, among the card's drivers.
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
 * leaves its entry with Active 0. The entries come from the MIDI drivers
 * of the card's software key, Class\MEDIA\nnnn\Drivers\midi\<driver>,
 * which ESS's INF writes for ESFM.DRV and ESSMPU.DRV, and Windows loads
 * those drivers and makes their entries active, with the devnode of the
 * start, at every start. An entry that no driver of a card's key has goes
 * inactive at the next start: a copy of ESS's entry with Port 1 did, on
 * the laptop. The MIDI mapper keeps the device it plays on by the device's
 * own name (CurrentInstrument, "ESFM Synthesis (220)").
 *
 * So the OPL3 device has a driver of its own, ESFMOPL3.DRV, which essinst
 * adds to the drivers of the card that is there (ESS's entry with Active
 * 1): Drivers\midi\esfmopl3.drv with ESS's values for ESFM.DRV, its
 * Driver, and ESS's Description with ESFM made OPL3 FM, so Internal ESFM
 * Synthesis becomes Internal OPL3 FM Synthesis. Its entry in the list,
 * esfmopl3.drv<nnnn>, is a copy of ESS's entry with that driver and those
 * names, there before the next start, when Windows takes it over. Entries
 * and keys of ESFMOPL3.DRV for another install go, as do the copies of
 * ESS's entry with a Port above 0 that an earlier essinst made, and all of
 * them when the OPL3 device goes. A removal also gives the MIDI mapper's
 * CurrentInstrument back to ESS's device where it names the OPL3 device,
 * by its key, its name or the device's own name.
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
#define MR_SOFT 64 // a card's software key, with its 0
#define MR_OPL3_DRIVER "esfmopl3.drv"

struct mr_entry {
  char key[MR_KEY];      // esfm.drv<0018>
  char driver[16];       // Driver
  char name[MR_NAME];    // FriendlyName, or Description without one
  long port;             // Port, or -1 without one
  long active;           // Active, or -1 without one
  unsigned long devnode; // DevNode, where has_devnode
  int has_devnode;
  char soft[MR_SOFT]; // the card's software key, under HKLM, or ""
};

// the entries of the MIDI list, up to max into list: how many, or -1
// without the registry or the list
int mr_list(struct mr_entry *list, int max);

// the MIDI mapper's CurrentInstrument for the current user, "" without
// one, into out (MR_NAME bytes)
void mr_mapper(char *out);

// ESS's device's entry on the card that is there into e, with the card's
// software key in e->soft: 0, or -1 without one
int mr_ess(struct mr_entry *e);

// the MIDI drivers of the card's software key soft (Drivers\midi), their
// names after one another into out (size bytes): how many, or -1
int mr_drivers(const char *soft, char *out, unsigned size);

// what mr_opl3 does
#define MR_KEEP 0    // the list is as it should be
#define MR_ADD 1     // ESFMOPL3.DRV and its entry are added
#define MR_REMOVE 2  // the OPL3 device's drivers and entries are removed
#define MR_NO_ESFM 3 // the list has no entry of ESS's device on the card
#define MR_FAIL 4    // the registry refused the change
#define MR_TIDY 5    // the driver and entry stay, and stale ones go

// ESFMOPL3.DRV among the drivers of the card that is there, and its entry
// in the MIDI list: with on, the driver's key and entry for the card, made
// from ESS's if they aren't there, and every other entry or key of the
// OPL3 device removed (ESFMOPL3.DRV's for another install, and the copies
// of ESS's entry with a Port above 0); without on, all of them removed.
// With set 0 it only finds out. MR_*, with the entry that stays or is
// added, or the first one removed, in e, and how many others go in stale
int mr_opl3(int on, int set, struct mr_entry *e, int *stale);

#endif /* MIDIREG_H */
