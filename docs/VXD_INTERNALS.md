# ES1869.VXD internals

How the Windows 95 virtual device driver of the ES1869 works. The notes are
based on the disassembly in `src/vxd/` and apply to version 4.04 (file
version 4.04.00.1319, 36028 bytes). For the program interface see
[VXD_API.md](VXD_API.md); for rebuilding and changing the driver, the last
two sections.

## File

`ES1869.VXD` is a Linear Executable (LE) with page size 200h. Its seven objects are:

| # | Name | Size | Contents |
|---|---|---|---|
| 1 | LCOD | 1954h | locked code and data: DDB, device list, trap handlers' helpers, DSP I/O primitives, DirectSound HAL GUIDs |
| 2 | MCOD | 11Dh | locked code (VxD service wrappers) |
| 3 | RARE | 22Ch | `Sys_Dynamic_Device_Exit` |
| 4 | PNP | D6Ch | Plug and Play: device creation, configuration handlers |
| 5 | PCOD | 37F5h | pageable code: trapping, ownership, the V86/PM API, hardware volume, power management, no-DMA emulation |
| 6 | PDAT | 4F8h | pageable data: registry names, API tables, strings |
| 7 | ICOD | 8Ch | `Sys_Dynamic_Device_Init` (discarded after init) |

Other parts of the file:

- **Fixups.** All are internal: 334 of type 7 (32-bit offset) and 241 of type 8 (32-bit relative), counting a fixup that crosses a page boundary once.
- **DDB** at o1:0258: name `AUDDRV`, device ID 3B07h, DDK version 4.0. The V86 and PM API procedures are both o5:135C. There is no service table.
- **MZ stub.** It contains the string `Cert DX2`, the DirectX certification mark.
- **Version resource.** VS_VERSION_INFO follows the fixup tables.
- **Slack pages.** File pages 15–21 hold leftovers the linker copied in: the COFF symbol table of `nodma.obj`. It names the no-DMA emulation routines (`AUDDRV_ND_*`, `_AUDDRV_Get_pADI_From_XXX`, `abNDOneOpndBypass`, ...), which is where many names in `src/vxd/names.txt` come from.

## Start-up

- **Loading.** The driver is dynamically loaded (`Sys_Dynamic_Device_Init`, o7:0000) by the Plug and Play configuration manager. It registers as device driver and enumerator for the ES1869 devnode.
- **`PnP_New_DevNode`** (o4:0ACE). The configuration handler calls `ADI_Create` (o4:01FC) with the allocated resources:
  - Audio_Base, the FM port and its alias, and the MPU-401 port;
  - the audio and MPU-401 IRQs;
  - the two DMA channels;
  - the flags read from the registry (see [DRIVER_CONFIG.md](DRIVER_CONFIG.md)).
- **`ADI_Create`** allocates the device structure (ADI, E9h bytes), adds it to the device list, virtualizes the IRQs (VPICD) and DMA channels (VDMAD), and installs the I/O trap handlers.
- **Once the device is running**, the driver:
  - tells MMDEVLDR whether `midi\esfm.drv` and `midi\essmpu.drv` are present (`MMDEVLDR_SetDevicePresence`);
  - sets the `BLASTER` environment string for DOS programs (`MMDEVLDR_SetEnvironmentString`, format `A%03X I%d D%d`, IRQ 9 written as 2).
- **No-DMA emulation.** When the devnode is the no-DMA variant (flag 0020h), a child devnode `VIRTUAL\ESS1869-DMAEmulation` takes the DMA resources, and DMA is emulated with programmed I/O.

Control messages handled (`AUDDRV_Control`, o1:03E0):

| Message | Handler | Purpose |
|---|---|---|
| Sys_VM_Init, VM_Critical_Init | o5:1000 | per-VM set-up of trapping |
| VM_Not_Executeable | o5:0F84 | a dying VM gives up what it owned (its "previous owner" becomes FFFFFFFFh, so the next owner gets a reset) |
| Sys_Dynamic_Device_Init | o7:0000 | start |
| Sys_Dynamic_Device_Exit | o3:0000 | unload |
| PnP_New_DevNode | o4:0ACE | a new ES1869 (or the no-DMA child) |

## Ownership and trapping

There are three resources: the **DSP** (the audio part, Audio_Base+4h to +Fh
except +8h/+9h), **FM** and the **MPU-401**. Each has an owner VM and a
previous owner in the ADI.

- **Trap handlers.** `DSP_Port_Trap` (o5:0C7C), `FM_Port_Trap` (o5:0D28) and `MPU_Port_Trap` (o5:0CE4) handle a trapped port access:
  - the owner, or a VM that finds the resource unowned and so becomes its owner (`Acquire_Resources`, o5:1177), gets the real port through `Simulate_IO` or a direct `in`/`out`;
  - any other VM gets `Show_Contention_Message` (o5:0F0F), a SHELL message box shown once per VM unless "Disable Warning" is set, and reads return FFh.
- **Acquiring** stops trapping for the new owner.
  - When the DSP changes hands (the new owner is not the previous one), it resets the DSP (`DSP_Reset`, o5:195C: Audio_Base+6h = 1, three reads, 0) and prepares Audio 2 (mixer 71h |= 12h, and 74h, 76h, 78h and 7Ah cleared).
  - For a DOS VM it saves the mixer registers 7Ch, 1Ah, 32h, 36h, 38h, 3Eh, 3Ah, 3Ch, 64h, 60h and 62h (`Save_DOS_Mixer`).
  - For the system VM it disables DMA address translation of both channels.
- **Releasing** (`Release_Resources`, o5:1061) turns trapping back on.
  - It resets the DSP and sends DSP commands 10h and 80h (`DSP_Reset_On_Release`: direct output of silence).
  - For a DOS VM it restores the saved mixer registers (`Restore_DOS_Mixer`). Before that it sends DSP commands C6h and C3h, reads a byte and, if its bit 0 is clear, sends C2h 01h. These commands are not in the data sheet.
  - When nothing is owned any more and "Want Local Powerdown" is set, the digital section is powered down (o5:2122: Audio_Base+7h bits 3 and 2 set, wait for Audio_Base+6h bit 3, then bit 2 cleared).
- **Who acquires.** `ES1869.DRV` acquires the DSP when a wave device opens and releases it on close. On an idle Windows desktop the DSP is therefore unowned, and the first program in any VM that touches a port becomes its owner.

## Hardware volume

- **Buttons.** The volume buttons change the master volume in the chip (mixer 60h/62h).
- **Callback.** When a 16:16 callback is registered with function 0006:
  - the driver enables the hardware volume interrupt (mixer 64h bits 1:0 set, `HwVol_Int_Enable` o5:1F0F; the request is cleared through mixer 66h);
  - on each interrupt, `HwVol_Event` (o5:1F2A) reports the new volume to the callback through nested execution (`Call_Client_Callback`, o5:2180).
- **`ES1869.DRV`** registers this callback so that the Windows volume control follows the buttons.
- **Registry.** "HwVolume 2-Wire Mode" and "HwVolume Count By 3" set mixer 64h bits 3:2 and 5.

## No-DMA emulation

For machines whose ISA DMA cannot be used, the driver emulates the DMA
channels:
- `AUDDRV_ND_Emulate_DMA_In/Out` (o5:24D0, o5:2528) replace the port accesses of the DMA transfer;
- `ND_Setup_PIO_Buffer` (o1:0C3C, API 0007) sets up the buffer;
- interrupts are generated by `AUDDRV_ND_Generate_Interrupt` (o5:2A0C).

This mode is flag 0020h of the ADI; function 0004 then reports an emulated
count.

## Rebuilding the driver

`src/vxd/` holds the complete driver as NASM source:

| File | Contents |
|---|---|
| `es1869.asm` | top file, one include per LE object |
| `lcod.asm` ... `icod.asm` | one file per LE object |
| `vxd.inc`, `services.inc`, `ctlmsg.inc` | VxD service and control message macros and names |
| `adi.inc` | ADI field names |
| `essext.asm` | the essreg register API (group 4) |
| `layout.json`, `stub.bin`, `version.bin`, `gap.bin`, `slack.bin` | the parts of the file that are not code: object order and flags, header fields, the stub, the version resource, and the bytes between the tables |
| `names.txt` | the names and comments given to addresses; `tools/vxd2asm.py` generated the source from the original with them |

```
python3 tools/build_vxd.py                  # build/ES1869.VXD with the essreg API
python3 tools/build_vxd.py --stock --verify # must equal driver/ES1869.VXD byte for byte
```

**How the build works.** `build_vxd.py` assembles `es1869.asm` with
`nasm -f elf32` and links it with `tools/lelink.py`. The linker writes an LE
file the way Microsoft's linker did:
- fixup records in the same order, merged and split at page boundaries;
- data pages aligned;
- the last page padded.

`--stock` (`ESSREG_EXT=0`) must reproduce the original file exactly;
`tests/test_retools.py` checks this.

**Editing.** Every reference in the source is symbolic and becomes a fixup, so code
can grow in the middle of an object and the addresses follow. Two cautions:

- Some data is reached through a base label plus an offset (for example
  `[D2_0000+0Ch]`). Inserting bytes inside such a structure breaks those
  references. Append new data at the end of PDAT (or LCOD, if it must be
  locked).
- The padding between routines and about a dozen instructions are kept as
  `db` bytes, because NASM has no spelling that reproduces their original
  encoding. Two of them are short jumps with fixed distances (o4:0A27 and
  o4:0AC1 in `pnp.asm`): do not insert code between them and their targets.

The essreg extension changes the original in two places only, both the same
length:
- the dispatcher's group bound (`cmp ah,4` becomes `cmp ah,5`);
- the address of the group table (a copy with a fifth entry, at the end of PDAT).

Everything else is appended.

## Installing the extended driver

The extended `build/ES1869.VXD` behaves like the original for Windows, DOS
programs and the ESS drivers, and adds the register API. **Keep a copy of the
original.** Replacing the driver removes its DirectX certification mark
(`Cert DX2`); DirectX setup may then report the driver as uncertified.

1. Copy `C:\WINDOWS\SYSTEM\ES1869.VXD` to `ES1869.ORG` in the same directory.
2. Copy `build\ES1869.VXD` over `C:\WINDOWS\SYSTEM\ES1869.VXD`.
3. Restart Windows. essctl's *Device information* page now shows "Register API: version 1.00".

**If Windows does not start** or sound stops working: restart and press F8
at "Starting Windows 95", choose *Command prompt only*, and run:

```
copy C:\WINDOWS\SYSTEM\ES1869.ORG C:\WINDOWS\SYSTEM\ES1869.VXD
```
