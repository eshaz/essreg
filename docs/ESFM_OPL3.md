# ESFM.DRV's OPL3 device

`build/ESFM.DRV` has a second MIDI output device, *OPL3 FM Synthesis*, which
plays General MIDI on the ES1869's OPL3 compatibility mode. ESS's own
device, *ESFM Synthesis*, plays on the chip's ESFM native mode, with ESS's
patch bank. The OPL3 device writes the chip's FM ports as a driver writes an
OPL3, with the instruments of the Freedoom project, so a MIDI song sounds as
it does on an OPL3 card, such as a Sound Blaster 16 with Doom's music
driver.

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
FM_Base+0 does, and the driver sets the OPL3 up:
* register 105h to 1: OPL3 mode;
* 104h to 0: two operators on each of the 18 channels;
* 01h to 20h: the waveforms on;
* 08h to 40h: note select;
* BDh to 0: no rhythm mode, and the shallow vibrato;
* 04h to 60h: both timers masked.

Then each voice is keyed off at its lowest level. When the device closes,
its voices are muted, and ESS's chip reset brings native mode back for
device 0.

While the OPL3 device has the chip, ESS's code writes nothing to it:
`fm_write` drops ESS's register writes for that ES1869, so that a native
write can't land in the OPL3's registers. A power suspend keys off every
voice and lets the pedals up. After the resume, ESS's chip reset leaves the
chip in native mode, and the device's next message sets the OPL3 up again.

essctl's *ESFM* page reads the driver's voices from the chip's native
registers. While the OPL3 device is open, it says so instead, since the
chip's native voices are idle then.

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
one key for each device under:

```
HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\MediaResources\midi
```

A key is named after its driver and a number, such as `esfm.drv<0000>`. Its
values name the driver (`Driver`), the device among the driver's devices
(`Port`), the name that the list shows (`FriendlyName` and `Description`),
whether the entry is in use (`Active`), and the card's device node and
software key (`DevNode` and `SoftwareKey`). Windows makes the key of a
driver's first device when it installs the driver from the INF's
`Drivers\midi` lines, and ESS's INF knows one device of ESFM.DRV. A driver
with a second device has a second key with `Port` 1, as the INF of
Creative's AudioPCI writes for the two MIDI devices of `es1371.dll`.

So essinst ([src/win/midireg.c](../src/win/midireg.c)) copies the key of
ESS's device, the entry of `esfm.drv` with `Port` 0 or none, to the next
free number, normally `esfm.drv<0001>`:
* every value of ESS's entry, and its empty `Instruments` key;
* `Port` 1, a DWORD, or of the type that ESS's entry keeps it in;
* `FriendlyName` and `Description`: ESS's name with ESFM made OPL3 FM, so
  *Internal ESFM Synthesis* becomes *Internal OPL3 FM Synthesis*.

essinst adds the entry when the ESFM.DRV in place after its run is a rebuilt
driver with the OPL3 device and `Opl3Device` isn't 0, and only when no entry
of ESFM.DRV with a `Port` above 0 exists. It removes every such entry when
ESS's driver comes back with `/restore`, or when `Opl3Device=0` turns the
device off. Where the MIDI mapper's `CurrentInstrument`, for the current
user or for new ones, names a removed entry by its key or by its name,
essinst gives it ESS's device back in the same form. ESSINST.LOG has each
step. A run with the drivers already in place only changes the list, and
Windows doesn't restart.

esswchk's report lists the MIDI list, an entry a line (`env.midi`), and the
mapper's choice (`env.mapper`), so a run on the machine shows what Windows
made of the entries. If the MIDI tab shows no *Internal OPL3 FM Synthesis*,
those lines tell why.

## Checks

* `Opl3Test` in [tests/test_esfmdrv.py](../tests/test_esfmdrv.py) runs the
  driver in the emulator, whose chip has the OPL3 mode
  ([tests/esfmemu.py](../tests/esfmemu.py)): the two devices and their
  names, one open at a time, the notes' registers against the bank, levels,
  pan with and without `TrueSides`, the pedal, bend and its range, drums, 18
  voices, running status, long messages, resets, messages from an interrupt,
  a suspend and resume, and ESS's writes dropped while the device has the
  chip.
* `test_essinst_midi_list` in [tests/test_wine.py](../tests/test_wine.py)
  runs essinst under Wine against a MIDI list as ESS's INF leaves it.
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

* The INF of Creative's AudioPCI for Windows NT 4.0, whose
  `MediaResources\MIDI\es1371.dll<0000>` and `<0001>` keys carry `Port` 0
  and 1:
  https://contents.driverguide.com/content.php?id=771613&path=DRIVERS%2FAUDIO%2FSBPCI64D%2FAUDIOPCI.INF
* MIDI Yoke, a Windows 95 and 98 driver with up to 16 MIDI ports, each in
  the Multimedia control panel: http://www.midiox.com/myoke.htm
* Using several MIDI output devices with Windows 95 and 98:
  https://www.pgmusic.com/tutorial_multiple_midi_devices.htm
* Microsoft's article Q138314, General MIDI troubleshooting for Windows 95
  and 98: https://jeffpar.github.io/kbarchive/kb/138/Q138314/
* Wine's MIDI mapper, which reads `CurrentInstrument` and the schemes:
  https://doxygen.reactos.org/da/d55/midimap_8c_source.html
