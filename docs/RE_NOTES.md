# Reverse-engineering notes

How the ES1869 driver set was taken apart, and where the results are. The
tools are in `tools/` and need only Python 3 and NASM (`ndisasm` is part
of NASM).

## Files studied

| File | Format | What it is | Results |
|---|---|---|---|
| `driver/ES1869.VXD` | LE | virtual device driver (device AUDDRV, 3B07h) | full source in `src/vxd/`, [VXD_API.md](VXD_API.md), [VXD_INTERNALS.md](VXD_INTERNALS.md) |
| `driver/ESFM.DRV` | NE | FM MIDI driver | full source in `src/esfm/`; bank loader and patch format: [ESFM_BANK.md](ESFM_BANK.md); hanging notes: [ESFM_MIDI.md](ESFM_MIDI.md) |
| `driver/ES1869.DRV` | NE | wave, mixer and aux driver | settings and registers it uses: [DRIVER_CONFIG.md](DRIVER_CONFIG.md) |
| `driver/ESSDC.EXE` | MZ | ESS DOS configuration program | its DSP protocol for controller registers (C6h, then poll Audio_Base+Ch) |
| ES1869 data sheet | PDF | `docs/datasheet/` | the register catalog `src/esscat.tbl` and [REGISTERS.md](REGISTERS.md) |

The ES1868 driver sets for DOS, Windows 3.1, 95, 98, NT and OS/2 (from
philscomputerlab.com) were used only for comparison and are not in the
repository.

## Tools

| Tool | Purpose |
|---|---|
| `tools/retools/le.py` | LE reader and writer. It writes a file the way Microsoft's linker did (fixup record order and merging, records split at page boundaries, data page alignment, last page padding), so parsing and writing `ES1869.VXD` gives back the same bytes. |
| `tools/retools/ne.py` | NE reader: segments with relocations, resources, entry table, imported modules. |
| `tools/retools/vxdsvc.py` | Names of VMM, VPICD, VDMAD, SHELL, VXDLDR, CONFIGMG, MMDEVLDR and DSOUND services, and of the control messages. |
| `tools/retools/elf32.py` | Reader for NASM's ELF output (sections, symbols, relocations). |
| `tools/vxd2asm.py` | Turns `ES1869.VXD` into NASM source (below). |
| `tools/lelink.py` | Links NASM's ELF output back into an LE file (layout from `src/vxd/layout.json`). |
| `tools/build_vxd.py` | Builds `build/ES1869.VXD`; `--stock --verify` checks the byte-identical rebuild. |
| `tools/regdoc.py` | Generates [REGISTERS.md](REGISTERS.md) from the catalog. |
| `tools/ne2asm.py` | Turns a 16-bit NE module (`ESFM.DRV`) into NASM source: one section per segment, followed by its relocation table. |
| `tools/nelink.py` | Links that source back into an NE file (layout from `src/esfm/layout.json`). |
| `tools/build_esfm.py` | Builds `build/ESFM.DRV`; `--stock --verify` checks the byte-identical rebuild. |
| `tests/esfmemu.py` | Runs `ESFM.DRV` in a 16-bit CPU emulator (Unicorn) with an FM chip model and simulated interrupts ([ESFM_MIDI.md](ESFM_MIDI.md)). |

## From VxD to source

`vxd2asm.py` disassembles by recursive descent. It starts from:
- the DDB procedures and the API and control dispatch tables;
- every fixup target inside a code object;
- the names in `src/vxd/names.txt`.

A region is decoded only if it decodes cleanly and is reached. Anything else
stays data (`db`); this keeps tables and the DirectSound GUIDs in LCOD from
being read as code.

**Operands.** Every fixup becomes a symbolic operand:
- a label;
- `VxDCall`/`VxDJmp` with the service name, for `INT 20h` plus a dword;
- `Client_*` offsets inside the API handlers;
- control message names in `AUDDRV_Control`.

A fixup whose target is not a label start is written as `label+offset wrt ..sym`, so NASM emits a relocation against the symbol with the offset as addend.

**Encodings.** Reproducing the original encodings was the hard part.
- The original assembler (MASM) chose different encodings than NASM for
  register-to-register moves, for immediates that fit in a byte, and for
  displacements. The generator writes macros (`mov_ eax,ebx` for the "load"
  direction) and size keywords (`strict dword`, `[byte ...]`, `[dword ...]`).
- `vxd2asm.py` checks the result line by line against a NASM listing, and
  tries alternative spellings until each instruction assembles to the
  original bytes.
- The few that NASM cannot express (for example `cmp di,0FFFFh` with a word
  immediate) are kept as `db` with their fixups.

**Names.** Many routine names come from the COFF symbol table of `nodma.obj`,
which Microsoft's linker left in unused pages of the file. The symbols were
matched to the code by the relative offsets of three of them (`_PTEXT`,
`PNP` and `abNDOneOpndBypass`). The rest are descriptive names given during
analysis (`names.txt`).

## Checks that keep the results honest

- `tests/test_retools.py` pins the facts:
  - the LE round trip;
  - the object table;
  - fixup counts (334 and 241);
  - the DDB and the control messages;
  - the API table shape (12/4/2/4 functions);
  - the byte-identical rebuild.
- `tests/test_vxdext.py` runs the added register API (group 4) of the
  rebuilt driver in a CPU emulator (Unicorn) against a simulated ES1869,
  including the ownership refusal and the index restoring.
- `tests/test_docs.py` keeps [VXD_API.md](VXD_API.md)'s function table equal
  to the dispatch tables and [REGISTERS.md](REGISTERS.md) equal to the
  catalog.
- `tests/test_esfmpat.py` and `tests/host/t_esfm.c` patch copies of
  `ESFM.DRV` and check them with the NE reader.
- `tests/test_wine.py` (opt-in) loads the real `ESFM.DRV` under Wine and
  replaces its bank in memory with essctl.

## Findings worth knowing

- **Ownership.** Every port access from Windows or a DOS box goes through
  the driver's trap handlers. An unowned device is taken by whichever VM
  touches it first, and the DSP is reset whenever ownership changes hands.
  A register tool that simply writes ports from Windows therefore steals
  the card. See [VXD_API.md](VXD_API.md#ownership-and-port-trapping) for
  how essctl avoids that.
- **Controller registers (A0h-BFh)** are read through the DSP (C6h, C0h,
  register). The data-ready flag must be polled at Audio_Base+Ch bit 6.
  Reading Audio_Base+Eh, as the original essreg did, also clears the audio
  interrupt.
- **Undocumented DSP commands.** The driver sends C3h (read a status byte)
  and C2h 01h before restoring a DOS box's mixer settings.
- **Bypass key.** The driver writes a 32-byte key sequence (PDAT 0249h) to
  port 279h, followed by an address. This is the ES1869's "bypass key"
  (DS p.28), which places the configuration device without the ISA PnP
  isolation protocol.
- **ESFM.DRV bank size.** It is hard-coded four times, and there is a
  complete RIFF bank-file loader that the driver never uses. See
  [ESFM_BANK.md](ESFM_BANK.md).
- **Data sheet contradictions.** Where the data sheet contradicts itself
  (telegaming bit, 1Ch record sources, B9h transfer types, ...), the
  catalog follows the register description and leaves a note (see the
  notes in [REGISTERS.md](REGISTERS.md)).

## From NE driver to source

`ne2asm.py` does for `ESFM.DRV` what `vxd2asm.py` does for the VxD:
- **Where code is found.** It disassembles by recursive descent from the entry table, the start address, far calls between segments, jump tables and the names in `src/esfm/names.txt`.
- **Relocations.** An NE relocation is a chain: each site holds the offset of the next site with the same target. Every site becomes a label, and the chain links are written as labels too, so code can move and the chains stay right.
- **Far calls** into another segment name their target.
- **Assembling.** `nasm -f bin` assembles every segment into its own section (`vstart=0`, so labels are segment offsets), followed by the segment's relocation table exactly as it sits in the file. A last section tells `nelink.py` the lengths and the exported offsets.
- **Encodings.** Instructions that NASM would encode differently are written with the "load" macros (`mov_ bx,ax`) or `strict word`; only 2 needed raw bytes.
- **The layout.** `nelink.py` writes the header, the segment, resource, name and entry tables and the gang-load area. It places the segments and resources at their original offsets, moving the ones after a segment that grew.
