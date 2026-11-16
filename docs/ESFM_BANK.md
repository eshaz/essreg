# ESFM patch banks

`ESFM.DRV` is the Windows 95 MIDI driver for the ES1869's FM synthesizer,
which it runs in ESFM native mode with 18 four-operator voices. Every sound
it plays comes from one *patch bank*, which is stored in the driver file and
copied into memory when the driver is enabled.

This page covers the build shipped in `driver/ESFM.DRV` (20976 bytes, module
`ESFM`, expected Windows version 4.0). Everything on it was read from that
build's code. Addresses are `segment:offset` within that file. The fixed
`build/ESFM.DRV` ([ESFM_MIDI.md](ESFM_MIDI.md)) has the same bank loader, so
all of this applies to it too, although its built-in bank is not ESS's
`bnk_com.bin` but `esfm_patch_banks/bnk_com_better_square_wave.bin`. The
fixed driver can also play a bank file straight from disk and reads the file
again when its date changes, as [Bank file](#bank-file-buildesfmdrv)
describes, and its OPL3 device plays a GENMIDI bank file in the same way.
The Settings button of the Multimedia control panel chooses both files
([below](#choosing-a-bank-in-the-multimedia-control-panel)).

## Bank format

| Offset | Size    | Contents                                          |
|--------|---------|---------------------------------------------------|
| 0      | 256 x 2 | little-endian patch offsets from the bank's start |
| 200h   | ...     | the patches                                       |

Entries 0-127 of the offset table are the General MIDI programs. Entries
128-255 are the percussion notes 0-127 of MIDI channel 10, so a note-on for
note *n* on that channel uses entry 128 + *n* (seg1:0C50). An offset of 0
means that the program or drum is silent.

A patch is one or two 36-byte *voices*, and each voice is a 4-byte header
followed by four 8-byte operator records. Bits 2:1 of header byte 0 say how
the patch is played (seg1:0C98):

| Value | Meaning                                                          |
|-------|------------------------------------------------------------------|
| 0     | one voice                                                        |
| 1     | two voices, the second (+24h) only if another chip voice is free |
| 2     | two voices, both always allocated                                |
| 3     | the entry is ignored                                             |

Bit 0 of header byte 0 is passed to the voice allocator for each voice, but
what exactly it does there isn't known.

The operator records hold the eight ESFM operator registers, which the
driver writes with these changes (seg1:0808):
* Byte 1 holds KSL and the attenuation. The driver raises the attenuation in
  bits 5:0 by an amount that depends on velocity and volume.
* Bytes 4 and 5 hold a pitch offset rather than a frequency.
  `((byte5 << 2) | (byte4 & 3)) & 7Fh` is a signed 7-bit number of semitones
  added to the note, and byte 4 bits 7:2 scale a fine detune. The driver
  keeps byte 5 bits 7:5, the envelope delay, and replaces the rest of both
  bytes with the block and F-number it computes.
* Bytes 0, 2, 3, 6 and 7 are copied as they are.

The bank shipped in the driver, `esfm_patch_banks/bnk_com.bin`, is 8288
bytes (2060h) long and holds 175 patches, 41 of them with two voices.
`bank_check` in `src/esfmbank.c` checks a bank with these rules: every
nonzero offset must point past the table, and the whole patch must fit in
the bank.

### RIFF bank files

A bank can also be stored as a RIFF file with the form type `Ptch` and the
bank in an `fm4 ` chunk:

```
"RIFF" <size> "Ptch" "fm4 " <bank size> <bank> [pad byte]
```

This is the format of the driver's own file loader, which the driver never
uses (see below). `esfmpat` and essctl accept both raw banks and RIFF files.

## How ESFM.DRV loads the bank

The bank is resource type 256, ID 1234. In the shipped file it sits at
offset 2400h, 2060h bytes long, inside the fast-load area.

When `DriverProc` gets `DRV_ENABLE`, it calls seg3:0662, which does the
following:
1. It calls `GlobalAlloc(GMEM_SHARE | GMEM_ZEROINIT | GMEM_MOVEABLE,
   2060h)`. The size is loaded with `mov ax,2060h; cwd`, so a size of 8000h
   or more would sign-extend into a huge request.
2. It stores the handle as a far pointer, with the offset at `DGROUP:0012`,
   where it is always 0, and the handle at `DGROUP:0014`, where it is used
   directly as a selector.
3. It compares two strings in its data segment, both `"Undefined"`
   (DGROUP:0047 and DGROUP:0051). Because they are equal, it takes the bank
   from the resource with `FindResource`, `LoadResource` and `LockResource`,
   then copies it with `mov cx,1030h; rep movsw`.
4. If the strings differed, which never happens in this build, it would open
   the file named at DGROUP:0047 with `mmioOpen`, check for `RIFF` and
   `Ptch`, find the `fm4 ` chunk and read at most 2060h bytes of it with
   `mmioRead`.

`DRV_DISABLE` frees the block and clears the pointer. During playback, every
note-on reads the table and the patches through the pointer at DGROUP:0012.

The bank size appears in four places in segment 3:

| seg3 offset | Instruction                                     |
|-------------|-------------------------------------------------|
| 0670h       | `mov ax,size` for `GlobalAlloc`                 |
| 06FCh       | `mov cx,size/2` for `rep movsw`                 |
| 077Ch       | `cmp word [bp-2Ch],size`, the RIFF loader's cap |
| 0783h       | `mov word [bp-2Ch],size`                        |

## Loading a bank into the running driver (essctl)

essctl's ESFM > *Load patch bank*, or the *ESFM patch bank* page, replaces
the bank of the running driver, so the next notes use the new patches.
essctl does this in four steps:

1. It finds the module with `GetModuleHandle("ESFM")` and gets its file with
   `GetModuleFileName`, then checks the file with `esfm_drv_inspect`. The
   loader code around the four size constants must match this build, and the
   constants must agree with each other.
2. It gets the driver's data segment, the NE `autodata` segment, with
   ToolHelp's `GlobalEntryModule`. The segment must hold the two "Undefined"
   strings and a bank pointer with offset 0 and a nonzero handle. Without
   that pointer, the driver is loaded but not enabled.
3. If the new bank is larger, it grows the driver's own block with
   `GlobalReAlloc`. The block stays owned by `ESFM.DRV`, which frees it as
   usual. If the handle changes, essctl updates DGROUP:0014 with interrupts
   disabled.
4. It copies the bank with interrupts disabled, so a MIDI timer callback
   never sees half a patch.

ESFM > *Restore original bank* copies the driver's resource 1234 back. A
loaded bank lasts until the driver is disabled or Windows restarts, but
File > *Save profile* records it as `[ESFM] Bank=path`, and
`essctl /load profile.ini` in the StartUp group then loads it again at every
start. With the fixed driver, *Load patch bank* and *Restore original bank*
also change the driver's bank file, as [Bank file](#bank-file-buildesfmdrv)
describes.

## Changing the bank in the file (esfmpat)

`esfmpat esfm.drv bank.bin` changes the driver file itself, so the new bank
plays from the next start of Windows. It goes through these steps:

1. It checks the bank with `bank_check` and the driver with
   `esfm_drv_inspect`, as described above, and it refuses an unknown build
   without changing anything.
2. It keeps the original driver as `ESFM.BAK`. A later run never overwrites
   an existing backup.
3. It writes a bank that fits in the resource in place and clears the rest
   of the resource.
4. It appends a larger bank at the end of the file, aligned to 16 bytes, and
   moves the resource table entry to it. The old copy in the fast-load area
   stays unused.
5. It sets the four size constants to the bank size, rounded up to an even
   number. Because of the `cwd`, the limit is 7FF0h bytes (32752).

Given only the driver, `esfmpat esfm.drv` prints where the driver's bank is
and how large it is.

Earlier versions of `esfmpat` always wrote the bank at offset 2400h, so a
bank larger than 2060h bytes overwrote the start of segment 3 (at 44A0h) and
broke the driver.

## Bank file (`build/ESFM.DRV`)

The fixed driver can play a bank file straight from disk, instead of the
bank built into `ESFM.DRV`. Name the file in `SYSTEM.INI`, with a full path
that includes the drive:

```
[ESFM.DRV]
Bank=C:\BANKS\MYBANK.BIN
```

ESS's `ESFM.DRV` doesn't read this setting. The file can be a raw bank or a
RIFF `Ptch` file, the same files that `esfmpat` and essctl take, and the
driver checks it with the rules of `bank_check`, up to 7FF0h bytes (32752).

The driver reads the file when a program opens the MIDI device
(`MODM_OPEN`), and only if the file's date or time has changed since the
driver last read it, or if `Bank=` names another file. Nothing is read while
a program has the device open, so a change plays from the next time a
program opens it. At `DRV_ENABLE`, when Windows starts, the driver loads its
own bank as before, and it reads the file at the first open after that.

Copying a file keeps its date and time, so if you copy a bank over the file
and both have the same date and time, the driver doesn't see the change. To
give the file the current date, run `cd C:\BANKS` and then
`copy /b MYBANK.BIN +,,` in an MS-DOS prompt.

If the file is missing or broken, the driver keeps the bank that is playing,
which is either the file's last good version or the driver's own bank. It
doesn't read a broken file again until the file's date or time changes.
Without a `Bank=` line, the driver plays its own bank, so removing the line
puts the driver's own bank back the next time a program opens the device.

The driver's own bank is the square-wave bank built in as resource 1234.
With `BetterSquareWave=0` in the same section, it is ESS's `bnk_com.bin`
instead, which the fixed driver carries as resource 1235
([DRIVER_CONFIG.md](DRIVER_CONFIG.md#63-esfmdrv)). The driver reads that key
once, at `DRV_ENABLE`, like its other switches, but it reads `Bank=` at
every open.

With this driver, essctl's ESFM > *Load patch bank* also writes `Bank=` to
`SYSTEM.INI`, so the driver keeps playing the file after a restart. ESFM >
*Restore original bank* removes the `Bank=` line and puts the driver's own
bank back, which is ESS's bank when `BetterSquareWave=0` is set. The *ESFM
patch bank* page and `essctl /dump` show the file and what the last open
found:

```
Bank file: C:\BANKS\MYBANK.BIN, 8288 bytes, dated 2026-09-27 14:03
Read when a program opens the device, if its date or time changed:
5 checks, 2 loads
```

In the file that `essctl /dump` writes, the last two lines of this example
are one line.

### How it works

The code is in [`src/esfm/esfmfile.asm`](../src/esfm/esfmfile.asm). For
`MODM_OPEN`, `modMessage` checks the file before ESS's code runs, unless a
program already has the device open, in which case the new open is refused.
The path comes from `GetPrivateProfileString`, and the file is opened with
`DOS3Call`, through INT 21h 716Ch for long file names or 6C00h where that
call doesn't exist. Both calls ask DOS to return errors (BX bit 13), so a
path on a drive without a disk only fails the open, and no "drive not ready"
box stops the program that opened the MIDI device.

INT 21h 5700h gives the file's date and time. The driver keeps them together
with the name of the file it last read, and it reads the file only when they
differ. The bank goes into a new `GMEM_SHARE` block, like the one that ESS's
loader, `bank_load`, allocates. The new block replaces the old one at
DGROUP:0014 while the driver is held
([ESFM_MIDI.md](ESFM_MIDI.md#the-fix-buildesfmdrv)), and the old block is
freed. If the bank is page-locked, the new block is locked in the same way
(`GlobalWire`, `GlobalPageLock`).

For `DRV_ENABLE`, `DriverProc` calls the bank file code (the far call at
seg3:0057), which calls ESS's loader at seg3:0662 and forgets the file it
read, so the next open reads the file again. The rest of segment 3 is
unchanged, so `esfmpat` works with this driver too. The bank file's state is
in the `ESFMFIX` block, which has held it since version 2, and essctl reads
it there.

## Choosing a bank in the Multimedia control panel

With the rebuilt drivers, the Multimedia control panel chooses the bank of
each of the two MIDI devices, the ESFM device and the OPL3 device
([ESFM_OPL3.md](ESFM_OPL3.md)):

1. Open Control Panel > *Multimedia* > *Devices*, and open *MIDI Devices and
   Instruments*.
2. Select the ESFM device's entry, *MIDI for Internal ESFM Synthesis*, or
   the OPL3 device's, *MIDI for Internal OPL3 FM Synthesis*, and click
   *Properties*.
3. Click *Settings*. The box *MIDI patch banks* opens, with a list for each
   device and the focus on the list of the device whose Settings opened it.
4. Choose a bank in each list and click OK.

OK writes the choices that changed to `[ESFM.DRV]` in `SYSTEM.INI`: `Bank=`
for the ESFM device and `Opl3Bank=` for the OPL3 device, each with the
file's full path. The driver's own bank takes the key away. Cancel changes
nothing. A device plays its new bank from the next time a program opens it,
as `Bank=` always did, so Windows needn't restart, but a program that has
the device open keeps the bank it has until it closes the device.

Each list holds:
* the driver's own bank first: the square-wave bank or ESS's bank
  (`BetterSquareWave=0`) for the ESFM device, and Freedoom's instruments for
  the OPL3 device;
* the files of the `ESFMBANK` folder in Windows' `SYSTEM` folder
  (`C:\WINDOWS\SYSTEM\ESFMBANK`) that are banks of the device's kind;
* the file that the device's key names, by its path, if it is somewhere
  else;
* the files that *Browse* picks.

An ESFM bank is a raw bank or a RIFF `Ptch` file, which the box checks with
the driver's rules (`bank_check`), and an OPL3 bank is a GENMIDI lump, the
format of Doom's `GENMIDI` lump and of `.OP2` files: `#OPL_II#` and 175
instruments of 36 bytes, the 128 General MIDI programs and the drums of
notes 35 to 81 ([ESFM_OPL3.md](ESFM_OPL3.md#what-it-plays)). *Browse* takes
only a file of the list's kind. With `Opl3Device=0` the OPL3 device isn't
there, and its list is gray.

essinst puts the box, `ESFMCFG.DLL`, in the `SYSTEM` folder with the
drivers, and the banks of its `BANKS` folder in `SYSTEM\ESFMBANK`, where
more banks can go:
* `ESS.BIN`, ESS's bank (`esfm_patch_banks/bnk_com.bin`);
* `ESSNT4.BIN`, the bank of ESS's Windows NT 4.0 driver (`bnk_NT4.bin`);
* `SQUARE.BIN`, the square-wave bank (`bnk_com_better_square_wave.bin`).

`essinst /restore` takes the box out with the rebuilt drivers and leaves the
banks. esswchk's report gives the two keys (`env.banks`), and essctl's *ESFM
patch bank* page and `essctl /dump` show what each device's last open found,
the OPL3 device's on an `OPL3 bank file:` line.

### How the Settings button works

The Multimedia control panel asks a MIDI driver's `DriverProc` whether it
has settings with `DRV_QUERYCONFIGURE`, and gives the *Settings* button
`DRV_CONFIGURE` with the panel's window. ESS's driver answers both with 0,
so the button is gray. The fixed `ESFM.DRV` answers the query with 1 while
`ESFMCFG.DLL` can be found, through `OpenFile`, and at `DRV_CONFIGURE` it
loads the DLL, calls its `BankDialog` with the panel's window and the
device, and frees the DLL again
([`src/esfm/esfmcfg.asm`](../src/esfm/esfmcfg.asm)). Both messages go
through ESS's code for `DRV_ENABLE` in segment 3, whose far call reaches the
fixed code, so that segment keeps its length. `ESFMOPL3.DRV`, the OPL3
device's driver, does the same for its device. `BankSettings=0` turns the
button off in both ([DRIVER_CONFIG.md](DRIVER_CONFIG.md#63-esfmdrv)).

The box is a 16-bit Windows DLL
([`src/win/esfmcfg.c`](../src/win/esfmcfg.c)). It checks the files with
`bank_kind` ([`src/esfmbank.c`](../src/esfmbank.c)), the checks of both
drivers, and it reads and writes the keys with Windows' profile functions,
which the drivers read them with at the next open.

The OPL3 device reads `Opl3Bank=` at its `MODM_OPEN` as the ESFM device
reads `Bank=`, with the same rules for the file's date and for a missing or
broken file ([`src/esfm/esfmfile.asm`](../src/esfm/esfmfile.asm),
`fix_opl3_check`). It keeps the instruments in a block of their own, wired
and page-locked, because it reads them at interrupt time, and it frees the
block it played before only while no program has a device open. Without the
key, the device plays the instruments built into the driver. The state of
the file is in the `ESFMFIX` block from its version 6, after `fix_opts`.
