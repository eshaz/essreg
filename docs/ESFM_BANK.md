# ESFM patch banks

* `ESFM.DRV` is the Windows 95 MIDI driver for the ES1869's FM synthesizer (ESFM native mode, 18 four-operator voices).
* Every sound it plays comes from one *patch bank*. The bank is stored in the driver file and copied into memory when the driver is enabled.
* This page covers the build shipped in `driver/ESFM.DRV` (20976 bytes, module `ESFM`, expected Windows version 4.0). Everything here was read from its code. Addresses are `segment:offset` within that file.
* The fixed `build/ESFM.DRV` ([ESFM_MIDI.md](ESFM_MIDI.md)) has the same bank loader, so all of this applies to it too.

## Bank format

| Offset | Size | Contents |
|---|---|---|
| 0 | 256 x 2 | little-endian offsets of the patches, relative to the start of the bank |
| 200h | ... | the patches |

* **Table entries.**
  * Entries 0-127 are the General MIDI programs.
  * Entries 128-255 are the percussion notes 0-127 of MIDI channel 10: note-on for note *n* uses entry 128 + *n* (seg1:0C50).
  * An offset of 0 means the program or drum is silent.
* **Patch structure.** A patch is one or two 36-byte *voices*. Each voice is a 4-byte header followed by four 8-byte operator records.
* **Header byte 0, bits 2:1** say how the patch is played (seg1:0C98):

  | Value | Meaning |
  |---|---|
  | 0 | one voice |
  | 1 | two voices; the second (at +24h) sounds only if a second hardware voice is free |
  | 2 | two voices, both always allocated |
  | 3 | the entry is ignored |

* **Header byte 0, bit 0** of each voice is passed to the voice allocator. What it does exactly isn't known.
* **Operator records** are the eight ESFM operator registers. The driver writes them with these changes (seg1:0808):
  * **Byte 1** (KSL and attenuation): the attenuation in bits 5:0 is raised by an amount that depends on velocity and volume.
  * **Bytes 4 and 5** hold a pitch offset, not a frequency:
    * `((byte5 << 2) | (byte4 & 3)) & 7Fh` is a signed 7-bit number of semitones added to the note
    * byte 4 bits 7:2 scale a fine detune
    * byte 5 bits 7:5 (envelope delay) are kept, and the rest of both bytes is replaced by the computed block and F-number
  * **Bytes 0, 2, 3, 6 and 7** are copied as they are.

The shipped bank:
* `esfm_patch_banks/bnk_com.bin` is 8288 bytes (2060h), with 175 patches, 41 of them two-voice.
* `bank_check` in `src/esfmbank.c` checks a bank with these rules: every nonzero offset must point past the table, and the whole patch must fit in the bank.

### RIFF bank files

A bank can also be stored as a RIFF file with form type `Ptch` and the bank in an `fm4 ` chunk:

```
"RIFF" <size> "Ptch" "fm4 " <bank size> <bank> [pad byte]
```

* This is the format of the driver's own, unused file loader (below).
* `esfmpat` and essctl accept raw banks and RIFF files.

## How ESFM.DRV loads the bank

* The bank is **resource type 256, ID 1234**. In the shipped file it sits at offset 2400h, 2060h bytes long, inside the fast-load area.
* **`DriverProc` with `DRV_ENABLE`** calls seg3:0662. That routine:
  1. `GlobalAlloc(GMEM_SHARE | GMEM_ZEROINIT | GMEM_MOVEABLE, 2060h)`. The size is loaded with `mov ax,2060h; cwd`, so a size of 8000h or more would sign-extend into a huge request.
  2. Stores the handle as a far pointer at `DGROUP:0012` (offset, always 0) and `DGROUP:0014` (the handle, used directly as a selector).
  3. Compares two strings in its data segment, both `"Undefined"` (DGROUP:0047 and DGROUP:0051). They're equal, so it takes the resource: `FindResource`, `LoadResource`, `LockResource`, then `mov cx,1030h; rep movsw`.
  4. Otherwise (never, in this build) it opens the file named at DGROUP:0047 with `mmioOpen`, checks for `RIFF`/`Ptch`, finds the `fm4 ` chunk and `mmioRead`s at most 2060h bytes of it.
* **`DRV_DISABLE`** frees the block and clears the pointer.
* **During playback** every note-on reads the table and patches through the pointer at DGROUP:0012.

The bank size shows up in four places in segment 3:

| seg3 offset | Instruction |
|---|---|
| 0670h | `mov ax,size` for `GlobalAlloc` |
| 06FCh | `mov cx,size/2` for `rep movsw` |
| 077Ch | `cmp word [bp-2Ch],size` (RIFF loader cap) |
| 0783h | `mov word [bp-2Ch],size` |

## Loading a bank into the running driver (essctl)

ESFM > *Load patch bank* (or the *ESFM patch bank* page) replaces the bank of the running driver, so the next notes use the new patches.

1. **Find and check the driver.** Find the module with `GetModuleHandle("ESFM")`. Check its file (`GetModuleFileName`) with `esfm_drv_inspect`: the loader code around the four size constants must match this build, and the constants must agree with each other.
2. **Check the data segment.** Get it (NE `autodata` segment) with ToolHelp's `GlobalEntryModule`. It must hold the two `"Undefined"` strings, and a bank pointer with offset 0 and a nonzero handle. Otherwise the driver is loaded but not enabled.
3. **Grow the block if needed.** If the new bank is larger, the driver's own block is grown with `GlobalReAlloc`. It stays owned by `ESFM.DRV`, which frees it as usual. If the handle changes, DGROUP:0014 is updated with interrupts disabled.
4. **Copy the bank.** The copy runs with interrupts disabled, so a MIDI timer callback never sees half a patch.

* ESFM > *Restore original bank* copies resource 1234 of the driver back.
* The loaded bank lasts until the driver is disabled or Windows restarts.
* File > *Save profile* records it as `[ESFM] Bank=path`, and `essctl /load profile.ini` in the StartUp group loads it again at every start.

## Changing the bank in the file (esfmpat)

`esfmpat esfm.drv bank.bin` changes the file itself, for the next start of Windows:

1. Checks the bank and the driver (as above). It refuses an unknown build without changing anything.
2. Keeps the original driver as `ESFM.BAK`. A later run never overwrites an existing backup.
3. A bank that fits in the resource is written in place, and the rest of the resource is cleared.
4. A larger bank is appended at the end of the file, 16-byte aligned. The resource table entry is moved to it, and the old copy in the fast-load area stays unused.
5. The four size constants are set to the bank size, rounded up to even. The limit is 7FF0h bytes (32752), because of the `cwd`.

* `esfmpat esfm.drv` alone prints where the driver's bank is and how large it is.
* *Note: before this version, `esfmpat` always wrote at offset 2400h. A bank larger than 2060h bytes overwrote the start of segment 3 (at 44A0h) and broke the driver.*
