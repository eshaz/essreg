# ESFM.DRV's OPL3 device

`build/ESFM.DRV` has a second MIDI output device, *OPL3 FM Synthesis*, which
gives a program the ES1869's OPL3 compatibility mode. ESS's own device,
*ESFM Synthesis*, plays on the chip's ESFM native mode, with ESS's patch
bank. The OPL3 device does two things:
* It plays General MIDI on the chip's OPL3 mode, as a driver writes an OPL3,
  with the instruments of the Freedoom project, so a MIDI song sounds as it
  does on an OPL3 card, such as a Sound Blaster 16 with Doom's music driver.
* The program that opens it has the chip as a real OPL3: its own reads and
  writes of the FM ports reach the chip, so it can program its instruments,
  the rhythm and the timers itself
  ([below](#a-program-that-writes-the-chip-itself)).

The code is [`src/esfm/esfmopl.asm`](../src/esfm/esfmopl.asm), assembled
into the fixed driver only. `Opl3Device=0` under `[ESFM.DRV]` in
`SYSTEM.INI` turns the device off, and the driver then has ESS's one device
([DRIVER_CONFIG.md](DRIVER_CONFIG.md#63-esfmdrv)). essinst adds the device
to the MIDI list of the Multimedia control panel
([below](#the-multimedia-control-panel)).

## The two devices

Programs see two MIDI output devices for each ES1869, named after FM_Base:
* Device 0, *ESFM Synthesis (388)*, is ESS's synthesizer.
* Device 1, *OPL3 FM Synthesis (388)*, is the OPL3 device. Its capabilities
  are device 0's: an FM synthesizer with 18 voices on 16 channels, with a
  volume of its own.

The two devices share the chip, so one of them is open at a time. A program
that opens one while the other is open gets `MMSYSERR_ALLOCATED`, as a
second program that opens device 0 does with ESS's driver. A MIDI player
that offers a list of devices shows both, and Windows' MIDI mapper plays on
the device that the Multimedia control panel names.

ESS's code opens and closes the OPL3 device as it does device 0, so
ES1869.VXD gives the FM to Windows the same way, a DOS box takes it back the
same way, and the mixer's MIDI volume (`MODM_SETVOLUME`) works for both.

## How the device takes the chip

When the OPL3 device opens, the chip leaves native mode, which a write to
FM_Base+0 does, and it is as an OPL3 after a reset, the way ES1869.VXD
leaves it for a program that takes FM with a port access:
* register 105h to 0: OPL2 mode, as an OPL3 starts;
* 104h to 0: two operators on each of the 18 channels;
* 01h and 08h to 0;
* BDh to 0: no rhythm mode;
* 04h to 60h: both timers masked;
* every key off, and every operator at its lowest level.

The device writes nothing more until a MIDI message comes. The first one
sets the OPL3 up for General MIDI:
* register 105h to 1: OPL3 mode, for the sides of each channel;
* 01h to 20h: the waveforms on;
* 08h to 40h: note select;
* BDh to 0: no rhythm mode, and the shallow vibrato;
* 04h to 60h: both timers masked;
* every key off, and every operator at its lowest level.

When the device closes, every key is off, the rhythm's too, and every
operator silent, also those that its program played itself. Then ESS's chip
reset brings native mode back for device 0.

While the OPL3 device is open, ESS's code writes nothing to the chip:
`fm_write` drops ESS's register writes for that ES1869, so that a native
write can't land in the OPL3's registers. A power suspend keys off every
voice, the program's notes and the rhythm too, and lets the pedals up. After
the resume, ESS's chip reset leaves the chip in native mode, and the
device's next message sets the OPL3 up again. A program that writes the chip
itself sets its instruments again then, as after any loss of power.

essctl's *ESFM* page reads the driver's voices from the chip's native
registers. While the OPL3 device is open, it says so instead and reads
nothing from the chip, since the chip's native voices are idle then and a
read in native mode would change what the device's program wrote.

## A program that writes the chip itself

A program that has the OPL3 device open has the chip as a real OPL3, as
software written for an AdLib, a Sound Blaster or an OPL3 card expects it:
* Windows owns FM while the device is open, because ESS's open asks
  ES1869.VXD for it. The VxD traps none of the FM ports for the owner, so a
  Windows program's `in` and `out` instructions reach the chip directly:
  Audio_Base+0h to +3h (both banks), +8h and +9h (the first bank), and the
  AdLib ports 388h to 38Bh.
* The program finds the chip as an OPL3 after a reset, in OPL2 mode, and
  sets it up as it likes: the OPL3 mode, four-operator voices, rhythm, the
  waveforms, its own instruments.
* Its reads of the status port return the chip's status, so an AdLib or OPL3
  detection with the timers works.
* The device writes nothing of its own until a MIDI message comes, ESS's
  code nothing at all, and essctl reads nothing from the chip. Device 0
  stays closed to other programs, and a DOS box gets the VxD's virtual FM
  chip meanwhile.

The program can send MIDI to the device too, but the first message sets the
chip up for General MIDI, which keys off what the program played and takes
the voices for its notes.

A program can also write the FM ports without opening a device. ES1869.VXD
then gives Windows FM at its first access, in the same state. But a DOS
program that wants FM takes it away again (`DosTakesFM`), and so does a
program that opens ESS's device 0, whose open resets the chip to native
mode. With the OPL3 device open, neither can.

## What it plays

The instruments are the `GENMIDI` lump of Freedoom 0.13.0, in the format of
the DMX sound library that Doom plays its music with
([third_party/genmidi](../third_party/genmidi/README.md)):
* the 128 General MIDI programs;
* the drums of notes 35 to 81 on channel 10, each with its own fixed note.

The device plays them as DMX does in Chocolate Doom, for which they were
made:
* An instrument of two voices takes two of the chip's 18 voices, the second
  with the instrument's fine tuning.
* A note gets a voice that is off, first one that holds the same instrument,
  then the one off the longest. With every voice on, the second voice of an
  instrument goes first, then the note on the longest.
* The carrier's level follows General MIDI's curve, 40 log10 of the velocity
  and of controllers 7 and 11, in the chip's steps of 0.75 dB. The modulator
  keeps the instrument's level, and in additive mode, where it sounds as
  well, it is never louder than the carrier.
* The pitch is tuned to the chip's clock of 49,819.5 Hz, with the
  instrument's base note offset, and notes beyond the chip's range fold in
  by octaves.

The device handles:
* note on and off, programs and pitch bend;
* controller 1 (the chip's vibrato), 6 and 38 with RPN 0 (the bend range in
  semitones and cents), 7, 10, 11, 64, 120, 121 and 123 to 127;
* running status, in short messages and across long messages, and around
  real-time bytes;
* long messages: a GM, GM2, GS or XG reset sets every channel back.

Pan has three positions, as on an OPL3: left from 0 to 47, both from 48 to
80, and right from 81 to 127. The chip plays an OPL3's left (bit 4 of
register C0h) on its right output, so with `TrueSides`, the default, the
device swaps the two bits, as device 0 does, and a channel panned left
sounds on the left. With `TrueSides=0` it sounds on the right.

A message that comes while the driver is busy, such as one sent at interrupt
time, waits in the driver's queue as device 0's do (`QueueWhileBusy`).

## The Multimedia control panel

Windows 95 and 98 list the MIDI output devices in the MIDI tab of the
Multimedia control panel, where the *Single instrument* setting chooses the
device that the MIDI mapper plays on. The list comes from the registry, with
one key for each MIDI driver of a card under:

```
HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\MediaResources\midi
```

A key is named after its driver and the number of the card's install in the
MEDIA class, such as `esfm.drv<0018>` for the card in
`HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\MEDIA\0018`. Its
values name the driver (`Driver`), the names that the list shows
(`FriendlyName`, such as *MIDI for Internal ESFM Synthesis*, and
`Description`), whether the card is there (`Active`), and the card's device
node and software key (`DevNode` and `SoftwareKey`). An earlier install of
the card leaves its key behind with `Active` 0. The MIDI mapper keeps the
device it plays on by the device's own name, such as *ESFM Synthesis (220)*
(`CurrentInstrument` under
`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Multimedia\MIDIMap`).

The entries come from the card's MIDI drivers, the keys under the software
key's `Drivers\midi`, which ESS's INF writes for `esfm.drv` and
`essmpu.drv`, each with its `Driver` and `Description`. Windows loads those
drivers for the card at every start, with the card's devnode in their
messages, and keeps their entries active with the devnode of that start;
ESS's VxD tells Windows' multimedia loader (MMDEVLDR) whether each of them
is there by those names, `midi\esfm.drv` and `midi\essmpu.drv`. An entry
that no driver of a card's key has goes inactive: on the laptop, a copy of
ESS's entry with `Port` 1, which an earlier essinst made, had `Active` 0 and
the devnode of the start before after every restart.

So the OPL3 device has a MIDI driver of its own, `ESFMOPL3.DRV`
([src/esfm/esfmopl3.asm](../src/esfm/esfmopl3.asm)), which essinst adds to
the card's drivers as ESS's INF adds `essmpu.drv`:
* `ESFMOPL3.DRV` goes in the `SYSTEM` folder.
* The card's software key gets `Drivers\midi\esfmopl3.drv`, with ESS's
  values for `esfm.drv`, `Driver` `esfmopl3.drv`, and `Description`
  *Internal OPL3 FM Synthesis*: ESS's with ESFM made OPL3 FM.
* The MIDI list gets `esfmopl3.drv<0018>`, a copy of ESS's entry with that
  driver and those names (*MIDI for Internal OPL3 FM Synthesis*), its empty
  `Instruments` key included. Windows takes it over at the next start.
* `[ESFM.DRV]` of `SYSTEM.INI` gets `Opl3Device=2`.

`ESFMOPL3.DRV` has one device, whose every message it passes on to
ESFM.DRV's `modMessage` as device 1's, with the card's devnode as Windows
gave it, and with `Opl3Device=2` ESFM.DRV counts only its own device, so
that each program sees the OPL3 device once, as before, under the same name.
The driver finds ESFM.DRV by name at the count, the caps and the open, and
the messages that may come at interrupt time only follow that pointer, in
fixed memory. The messages of the devnode's coming and going stay with it,
since ESFM.DRV gets them itself. Without `Opl3Device=2`, it has no device.
ESFM.DRV decides at its first count of the session: if `ESFMOPL3.DRV` isn't
loaded then, ESFM.DRV keeps device 1 for the session and `ESFMOPL3.DRV`,
which asks ESFM.DRV's count, has none. So whatever order Windows loads the
two drivers in, the device is neither missing nor there twice, and esswchk's
`opl.list` says when ESFM.DRV kept it.

essinst adds all of that when the ESFM.DRV in place after its run is a
rebuilt driver with the OPL3 device, `Opl3Device` isn't 0, the MIDI list has
ESS's entry on the card that is there (`Active` 1), and the card's software
key has ESS's `esfm.drv`; otherwise the device stays ESFM.DRV's device 1,
without an entry. Entries and keys of `ESFMOPL3.DRV` for another install of
the card go, as do the copies of ESS's entry with a `Port` above 0 from an
earlier essinst. essinst takes everything out again, with `Opl3Device=1`,
when ESS's driver comes back with `/restore`, and when `Opl3Device=0` turns
the device off. Where the MIDI mapper's `CurrentInstrument`, for the current
user or for new ones, names the OPL3 device by its own name, *OPL3 FM
Synthesis (220)*, or a removed entry by its key or its name, essinst gives
it ESS's device back in the same form. ESSINST.LOG has each step. A run with
the drivers already in place only changes the list, which takes effect at
the next start of Windows.

esswchk's report lists the MIDI list, an entry a line with its `DevNode`
(`env.midi`), the mapper's choice (`env.mapper`) and the card's MIDI drivers
(`env.card`), and `opl.list` checks after a start that Windows kept the
entry: active on the card's devnode, `ESFMOPL3.DRV` loaded, and the OPL3
device once among the MIDI devices.

## Checks

* `Opl3Test` in [tests/test_esfmdrv.py](../tests/test_esfmdrv.py) runs the
  driver in the emulator, whose chip has the OPL3 mode
  ([tests/esfmemu.py](../tests/esfmemu.py)): the two devices and their
  names, one open at a time, the chip as an OPL3 after a reset at the open,
  the notes' registers against the bank, levels, pan with and without
  `TrueSides`, the pedal, bend and its range, drums, 18 voices, running
  status, long messages, resets, messages from an interrupt, a suspend and
  resume, and ESS's writes dropped while the device is open. A program that
  writes the chip itself keeps every register as it wrote it, and the close
  and a suspend key off its notes and its rhythm.
* `Opl3DriverTest` in [tests/test_esfmdrv.py](../tests/test_esfmdrv.py) runs
  `ESFMOPL3.DRV` next to ESFM.DRV in the emulator: one device with
  `Opl3Device=2` and none without, its caps and notes through ESFM.DRV's
  device 1, the devnode's messages kept, and no import at interrupt time.
* `test_essinst_midi_list` in [tests/test_wine.py](../tests/test_wine.py)
  runs essinst under Wine against a MIDI list and the card's drivers as
  ESS's INF leaves them, and `test_esswchk_opl3_list` esswchk's `opl.list`.
* esswchk's `opl.pan0` and `opl.pan127` play a note on the OPL3 device
  panned left, then right, and record it through the FM recording device
  ([ESSWCHK.md](ESSWCHK.md)).

## Limits

* The device uses the OPL3's two-operator voices only, as DMX does: no
  four-operator voices and no rhythm mode.
* A DOS program that writes the FM ports itself doesn't use the device. In a
  DOS box, the VxD's virtual FM chip answers it
  ([DRIVER_CONFIG.md](DRIVER_CONFIG.md#61-es1869vxd)).
* essinst keeps the mapper's single instrument right, but not a custom
  configuration (a MIDI scheme) that names the OPL3 device's entry.

## Sources

* ESS's INF for Windows 95
  ([driver/es1869f/WIN95DRV/OEMSETUP.INF](../driver/es1869f/WIN95DRV/OEMSETUP.INF)),
  whose `Drivers\midi` lines name the card's MIDI drivers, and ESS's VxD,
  which calls `MMDEVLDR_SetDevicePresence` with `midi\esfm.drv` and
  `midi\essmpu.drv` (`src/vxd/pcod.asm`); the service's arguments are in
  `MMDEVLDR.H` of the Windows 95 DDK.
* A post to wine-devel on the registry of Windows' multimedia drivers, with
  the `Drivers` keys of the MEDIA class and their `MediaResources` entries,
  whose name may differ from the driver's file:
  https://www.mail-archive.com/wine-devel@winehq.com/msg04730.html
* The INF of Creative's AudioPCI for Windows NT 4.0, whose
  `MediaResources\MIDI\es1371.dll<0000>` and `<0001>` keys carry `Port` 0
  and 1, as NT 4.0 lists a driver's two devices; Windows 98 didn't keep such
  a second entry on the laptop:
  https://contents.driverguide.com/content.php?id=771613&path=DRIVERS%2FAUDIO%2FSBPCI64D%2FAUDIOPCI.INF
* MIDI Yoke, a Windows 95 and 98 driver with up to 16 MIDI ports, each in
  the Multimedia control panel: http://www.midiox.com/myoke.htm
* Using several MIDI output devices with Windows 95 and 98:
  https://www.pgmusic.com/tutorial_multiple_midi_devices.htm
* Microsoft's article Q138314, General MIDI troubleshooting for Windows 95
  and 98: https://jeffpar.github.io/kbarchive/kb/138/Q138314/
* Wine's MIDI mapper, which reads `CurrentInstrument` and the schemes:
  https://doxygen.reactos.org/da/d55/midimap_8c_source.html
