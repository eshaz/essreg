# Reverse-engineering notes

This page describes how the ES1869 driver set was taken apart and where the results are. The tools are in `tools/` and need only Python 3 and NASM, which includes `ndisasm`.

## Files studied

| File | Format | What it is | Results |
|---|---|---|---|
| `driver/ES1869.VXD` | LE | the virtual device driver (device AUDDRV, 3B07h) | the full source in `src/vxd/`, [VXD_API.md](VXD_API.md) and [VXD_INTERNALS.md](VXD_INTERNALS.md) |
| `driver/ESFM.DRV` | NE | the FM MIDI driver | the full source in `src/esfm/`, the bank loader and patch format in [ESFM_BANK.md](ESFM_BANK.md), and the hanging notes in [ESFM_MIDI.md](ESFM_MIDI.md) |
| `driver/ES1869.DRV` | NE | the wave, mixer and aux driver | the full source in `src/es1869/`, and the settings and registers it uses in [DRIVER_CONFIG.md](DRIVER_CONFIG.md) |
| `driver/ESSDC.EXE` | MZ | ESS's DOS configuration program | its DSP protocol for the controller registers (C6h, then polling Audio_Base+Ch) |
| ES1869 data sheet | PDF | the chip's data sheet, in `docs/datasheet/` | the register catalog `src/esscat.tbl` and [REGISTERS.md](REGISTERS.md) |

The ES1868 driver sets for DOS, Windows 3.1, 95, 98, NT and OS/2, from philscomputerlab.com, served only for comparison and aren't in the repository. Neither are Microsoft's Windows 95 and 98 DDKs, which supplied the 16-bit multimedia headers and the MSSNDSYS sample described below.

## Tools

| Tool | Purpose |
|---|---|
| `tools/retools/le.py` | Reads and writes LE files. It writes a file the way Microsoft's linker did (fixup record order and merging, records split at page boundaries, data page alignment, last page padding), so parsing and writing `ES1869.VXD` gives back the same bytes. |
| `tools/retools/ne.py` | Reads NE files: the segments with their relocations, the resources, the entry table and the imported modules. |
| `tools/retools/vxdsvc.py` | Names the VMM, VPICD, VDMAD, SHELL, VXDLDR, CONFIGMG, MMDEVLDR and DSOUND services, and the control messages. |
| `tools/retools/elf32.py` | Reads NASM's ELF output: sections, symbols and relocations. |
| `tools/vxd2asm.py` | Turns `ES1869.VXD` into NASM source, as described below. |
| `tools/lelink.py` | Links NASM's ELF output back into an LE file, with the layout from `src/vxd/layout.json`. |
| `tools/build_vxd.py` | Builds `build/ES1869.VXD`. With `--stock --verify`, it checks that ESS's driver rebuilds byte for byte. |
| `tools/regdoc.py` | Generates [REGISTERS.md](REGISTERS.md) from the catalog. |
| `tools/ne2asm.py` | Turns a 16-bit NE module (`ESFM.DRV`, `ES1869.DRV`) into NASM source, with one section for each segment, followed by the segment's relocation table. |
| `tools/nelink.py` | Links that source back into an NE file, with the layout from the `layout.json` next to the source. |
| `tools/build_esfm.py`, `tools/build_es1869drv.py` | Build `build/ESFM.DRV` and `build/ES1869.DRV`. With `--stock --verify`, they check that ESS's driver rebuilds byte for byte. |
| `tests/esfmemu.py` | Runs `ESFM.DRV` in a 16-bit CPU emulator (Unicorn) with a model of the FM chip and simulated interrupts ([ESFM_MIDI.md](ESFM_MIDI.md)). |
| `tests/vxdemu.py` | Runs `ES1869.VXD` code in a 32-bit CPU emulator against a model of the ES1869's ports, with the VMM, VPICD, VDMAD and SHELL services it calls. |
| `tests/drvemu.py` | Runs `ES1869.DRV` in a 16-bit CPU emulator against a model of the DSP, the mixer and the DMA controllers ([AUDIO1.md](AUDIO1.md)). |

## From VxD to source

`vxd2asm.py` disassembles by recursive descent. It starts from the DDB procedures and the API and control dispatch tables, from every fixup target inside a code object, and from the names in `src/vxd/names.txt`. A region becomes code only if the descent reaches it and it decodes cleanly, and anything else stays data (`db`). This keeps tables and the DirectSound GUIDs in LCOD from being read as code.

`vxd2asm.py` writes the operands symbolically. Every fixup becomes a reference to a label, an `INT 20h` followed by a dword becomes `VxDCall` or `VxDJmp` with the service name, offsets inside the API handlers become `Client_*` names, and the control messages in `AUDDRV_Control` get their names too. A fixup whose target isn't the start of a label is written as `label+offset wrt ..sym`, so that NASM emits a relocation against the symbol with the offset as its addend.

Getting the original encodings back was the hard part. The original assembler, MASM, chose different encodings than NASM for register-to-register moves, for immediates that fit in a byte and for displacements. `vxd2asm.py` therefore writes macros (`mov_ eax,ebx` for the "load" direction) and size keywords (`strict dword`, `[byte ...]`, `[dword ...]`). It checks the result line by line against a NASM listing and tries other spellings until each instruction assembles to the original bytes. The few instructions that NASM can't express, such as `cmp di,0FFFFh` with a word immediate, are kept as `db` with their fixups.

Many routine names come from the COFF symbol table of `nodma.obj`, which Microsoft's linker left in unused pages of the file. The symbols were matched to the code by the relative offsets of three of them, `_PTEXT`, `PNP` and `abNDOneOpndBypass`. The other names are descriptive names given during the analysis, and they are in `names.txt`.

## From NE driver to source

`ne2asm.py` does for `ESFM.DRV` what `vxd2asm.py` does for the VxD. It disassembles by recursive descent from the entry table, the start address, the far calls between segments, jump tables and the names in `src/esfm/names.txt`. Far calls into another segment name their target.

An NE relocation is a chain, in which each site holds the offset of the next site with the same target. Every site becomes a label, and the links of the chain are written as labels too, so that code can move and the chains stay correct.

`nasm -f bin` assembles every segment into its own section, with `vstart=0` so that labels are segment offsets, and each section is followed by the segment's relocation table exactly as it sits in the file. A last section tells `nelink.py` the lengths and the exported offsets. Instructions that NASM would encode differently are written with the "load" macros (`mov_ bx,ax`) or with `strict word`, and only 2 of them needed raw bytes.

`nelink.py` writes the header, the segment, resource, name and entry tables, and the gang-load area. It places the segments and resources at their original offsets and moves the ones that come after a segment that grew.

### ES1869.DRV

`ES1869.DRV` goes through the same tool, with `src/es1869/names.txt`.

`ne2asm.py` decodes 512 bytes at a time. An instruction cut off by the end of a chunk used to come out as `db` bytes followed by bogus instructions, and it ended the decoding there, so the last 15 bytes of a chunk now go to the next one.

Recursive descent missed about 12 KB of code that is reached only through pointers: the callbacks handed to the VxD, the pipe callbacks and the power routines. Every undecoded gap that starts with a function prologue (`55 8B EC`, with `45` or `8C D8 90` before it) became an entry in the names file, and a few more were added by hand. What's left undecoded is strings, tables and padding.

A jump table in the mixer (5:15C6) is indexed by `(type & F000h) - 1000h >> 11`, which is a byte offset, so its 6 entries are given in the names file.

The interrupt handler (3:01B9) is copied to a fixed block when it is installed and patched there by offset, and three things in it depend on their place: the data selector and the device pointer (`mov ax,0FFFFh`, `mov si,1234h`) and the return offset (`push 5Bh`). ESS's code therefore has to keep its addresses, as in the VxD, so a change is either an instruction of the same length or new code after ESS's. `tests/test_es1869drv.py` checks this.

The imports are named from the IMPDEF records of the DDK's `LIBW.LIB` and `MMSYSTEM.LIB`.

### Compared with Microsoft's sample

The Windows 95 DDK's MSSNDSYS sample (`MMEDIA\SAMPLES\MSSNDSYS`), a Windows Sound System driver written in C with its VxD, has the same design as ESS's pair of drivers. The VxD owns the hardware. It hands the DSP to a VM or to the 16-bit driver (acquire and release), allocates the DMA buffers and virtualizes the Sound Blaster for DOS boxes. The 16-bit driver runs the wave, mixer and aux devices, with a structure for each device, linked in a list.

The interrupt handler is the sample's `ISR_Stub`. `Create_ISR` copies it to a fixed block and patches the device pointer into it (`mov si,1234h`), as ESS's 3:0261 does. The interrupt user (the sample's `hwi_bIntUsed`, ESS's +5Dh) selects the service routine from `isr_Srv_Table`. ESS added the MPU-401 and a second stub for Audio 2, which has its own user at +100h. The handler ends with the device's EOI word, which holds the slave's EOI in the low byte and the master's in the high byte (ESS's +5Bh, the sample's `wEOICommands`). ESS's device structure is laid out differently, though, with Audio_Base in its first word and the MPU-401 port next.

## Checks that keep the results honest

* `tests/test_retools.py` pins the facts: the LE round trip, the object table, the fixup counts (334 and 241), the DDB and the control messages, the shape of the API tables (12/4/2/4 functions) and the byte-identical rebuild.
* `tests/test_vxdext.py` runs the rebuilt driver's added register API (group 4) in a CPU emulator (Unicorn) against a simulated ES1869. It also checks that the API refuses the controller registers while another VM owns the DSP, and that it restores the index registers.
* `tests/test_docs.py` keeps the function table in [VXD_API.md](VXD_API.md) equal to the dispatch tables, and [REGISTERS.md](REGISTERS.md) equal to the catalog.
* `tests/test_esfmpat.py` and `tests/host/t_esfm.c` patch copies of `ESFM.DRV` and check them with the NE reader.
* `tests/test_wine.py`, an opt-in test, loads the real `ESFM.DRV` under Wine and has essctl replace its bank in memory.

## Findings worth knowing

Every port access from Windows or a DOS box goes through ES1869.VXD's trap handlers. An unowned device goes to whichever VM touches it first, and the DSP is reset whenever ownership changes hands. A register tool that writes to the ports directly from Windows therefore takes the card over. [VXD_API.md](VXD_API.md#ownership-and-port-trapping) describes how essctl avoids that.

The controller registers (A0h-BFh) are read through the DSP, which gets the bytes C6h, C0h and the register number. The data-ready flag has to be polled at Audio_Base+Ch bit 6, because reading Audio_Base+Eh, as the original essreg did, also clears the audio interrupt.

ES1869.VXD sends two undocumented DSP commands before it restores a DOS box's mixer settings: C3h, which reads a status byte, and C2h 01h.

It also writes a 32-byte key sequence (PDAT 0249h) to port 279h, followed by an address. This is the ES1869's "bypass key" (DS p.28), which places the configuration device without the ISA PnP isolation protocol.

ESFM.DRV hard-codes the size of its bank four times, and it has a complete loader for RIFF bank files that it never uses ([ESFM_BANK.md](ESFM_BANK.md)).

Where the data sheet contradicts itself, as it does for the telegaming bit, the record sources of 1Ch and the transfer types of B9h, the catalog follows the register description and leaves a note. [REGISTERS.md](REGISTERS.md) shows the notes.
