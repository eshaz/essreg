# ES1869.VXD internals

How the Windows 95 virtual device driver of the ES1869 works.

* Based on the disassembly in `src/vxd/`, for version 4.04 (file version 4.04.00.1319, 36028 bytes).
* For the program interface, see [VXD_API.md](VXD_API.md).
* For rebuilding and changing the driver, see the last two sections.

## File

`ES1869.VXD` is a Linear Executable (LE) with page size 200h. It has seven objects:

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
* **Fixups.** All internal: 334 of type 7 (32-bit offset) and 241 of type 8 (32-bit relative). A fixup that crosses a page boundary is counted once.
* **DDB** at o1:0258: name `AUDDRV`, device ID 3B07h, DDK version 4.0. The V86 and PM API procedures are both o5:135C. There's no service table.
* **MZ stub.** Contains the string `Cert DX2`, the DirectX certification mark.
* **Version resource.** VS_VERSION_INFO follows the fixup tables.
* **Slack pages.** File pages 15-21 hold leftovers the linker copied in: the COFF symbol table of `nodma.obj`. It names the no-DMA emulation routines (`AUDDRV_ND_*`, `_AUDDRV_Get_pADI_From_XXX`, `abNDOneOpndBypass`, ...), and many names in `src/vxd/names.txt` come from there.

## Start-up

* **Loading.** The Plug and Play configuration manager loads the driver dynamically (`Sys_Dynamic_Device_Init`, o7:0000). It registers as device driver and enumerator for the ES1869 devnode.
* **`PnP_New_DevNode`** (o4:0ACE). The configuration handler calls `ADI_Create` (o4:01FC) with the allocated resources:
  * Audio_Base, the FM port and its alias, and the MPU-401 port
  * the audio and MPU-401 IRQs
  * the two DMA channels
  * the flags read from the registry (see [DRIVER_CONFIG.md](DRIVER_CONFIG.md))
* **`ADI_Create`** allocates the device structure (ADI, E9h bytes), adds it to the device list, virtualizes the IRQs (VPICD) and DMA channels (VDMAD), and installs the I/O trap handlers.
* **Once the device is running**, the driver:
  * tells MMDEVLDR whether `midi\esfm.drv` and `midi\essmpu.drv` are present (`MMDEVLDR_SetDevicePresence`)
  * sets the `BLASTER` environment string for DOS programs (`MMDEVLDR_SetEnvironmentString`, format `A%03X I%d D%d`, IRQ 9 written as 2)
* **No-DMA emulation.** When the devnode is the no-DMA variant (flag 0020h), a child devnode `VIRTUAL\ESS1869-DMAEmulation` takes the DMA resources, and DMA is emulated with programmed I/O.

Control messages handled by `AUDDRV_Control` (o1:03E0):

| Message | Handler | Purpose |
|---|---|---|
| Sys_VM_Init, VM_Critical_Init | o5:1000 | per-VM set-up of trapping |
| VM_Not_Executeable | o5:0F84 | a dying VM gives up what it owned (its "previous owner" becomes FFFFFFFFh, so the next owner gets a reset) |
| Sys_Dynamic_Device_Init | o7:0000 | start |
| Sys_Dynamic_Device_Exit | o3:0000 | unload |
| PnP_New_DevNode | o4:0ACE | a new ES1869 (or the no-DMA child) |

## Ownership and trapping

There are three resources: the **DSP** (the audio part, Audio_Base+4h to +Fh except +8h/+9h), **FM** and the **MPU-401**. Each one has an owner VM and a previous owner in the ADI.

* **FM ports.** Audio_Base+0h to +3h (ADI 08h holds Audio_Base: it's the FM port 0101 reports and 0102/0103 expect), +8h/+9h, and the alias 388h to 38Bh (ADI 0Ah).
* **Trap handlers.** `DSP_Port_Trap` (o5:0C7C), `FM_Port_Trap` (o5:0D28) and `MPU_Port_Trap` (o5:0CE4) handle a trapped port access:
  * The owner gets the real port through `Simulate_IO` or a direct `in`/`out`. So does a VM that finds the resource unowned, which makes it the owner (`Acquire_Resources`, o5:1177).
  * Any other VM gets `Show_Contention_Message` (o5:0F0F), and reads return FFh.
  * The message is a SHELL message box, shown once per VM, unless "Disable Warning" is set; ESS's INF sets it. It says "Unable to play sound - the ESS AudioDrive is in use by another application." for the DSP, "Unable to play MIDI - the ESS FM Synthesizer is in use by another application." for FM and "Unable to access MIDI port - the ESS MPU-401 is in use by another application." for the MPU-401.
* **Acquiring** stops trapping for the new owner.
  * Every FM acquisition resets the FM synthesizer (o5:19C4: Audio_Base+7h bit 5 high for 25 reads, 36h muted meanwhile). One that comes from a port access then writes 65 registers (o5:0BD0): OPL2 mode, all operators silent, the timers masked.
  * When the DSP changes hands (the new owner isn't the previous one), it resets the DSP (`DSP_Reset`, o5:195C: Audio_Base+6h = 1, three reads, 0) and prepares Audio 2 (mixer 71h |= 12h, and 74h, 76h, 78h and 7Ah cleared).
  * For a DOS VM it saves the mixer registers 7Ch, 1Ah, 32h, 36h, 38h, 3Eh, 3Ah, 3Ch, 64h, 60h and 62h (`Save_DOS_Mixer`), and gives FM the music DAC (7Fh bit 0 cleared, `L1_039C`).
  * For the system VM it disables DMA address translation of both channels.
* **Releasing** (`Release_Resources`, o5:1061) turns trapping back on.
  * It resets the DSP and sends DSP commands 10h and 80h (`DSP_Reset_On_Release`: direct output of silence).
  * For a DOS VM it restores the saved mixer registers (`Restore_DOS_Mixer`) and 7Fh bit 0. Before that it sends DSP commands C6h and C3h, reads a byte and, if its bit 0 is clear, sends C2h 01h. These commands aren't in the data sheet.
  * Releasing FM doesn't silence the synthesizer: notes left on sound until the next FM acquisition resets it.
  * When nothing is owned any more and "Want Local Powerdown" is set, it powers the digital section down (o5:2122: Audio_Base+7h bits 3 and 2 set, wait for Audio_Base+6h bit 3, then bit 2 cleared).
* **A program's end.** The driver hooks DOSMGR_End_V86_App (o7:0076, handler o1:04B8). When any program in a DOS VM ends, the VM gives back everything it owns, even if the program was a child and its parent goes on.
* **Who acquires.** `ES1869.DRV` acquires the DSP when a wave device opens and releases it on close, and around every mixer change. `ESFM.DRV` acquires FM from MODM_OPEN to MODM_CLOSE. So on an idle Windows desktop nothing is owned, and the first program in any VM that touches a port becomes its owner.

## DOS boxes

With ESS's driver:
* A DOS program's FM detection works only while FM is free or already its VM's. While a Windows MIDI program or another DOS box has FM, every read returns FFh, and the AdLib, OPL3 and ESFM detections all fail.
* A Windows program that touches an FM port while FM is free makes Windows the owner until a MIDI program opens and closes. DOS boxes then have no FM.
* The FM owner doesn't get the music DAC (7Fh bit 0) nor an FM volume (36h). While no Windows MIDI program is open, ES1869.DRV gives them to I2S and IIS, so a DOS program that only uses FM can be silent. `1869opl3` works around this.
* When a child program ends, its parent's next FM access resets the synthesizer under it.
* When a DOS program gives the DSP back, 11 mixer registers go back to Windows' values. The others stay as it left them: the 3-D effect, the record source and levels, the wave volume, MONO_IN and MONO_OUT.

The extended driver (`src/vxd/essext.asm`) changes this:
* **FM detection always succeeds.** A VM that can't have the FM chip gets a virtual one, allocated on its first access (1.3 KB).
  * It answers like the ES1869: the OPL3 registers of both banks, the timers (80 and 320 us per count) with the status port's IRQ, FT1 and FT2 flags, bits 4:0 reading 0 as on an OPL3, and ESFM native mode with its readback.
  * The timers run on the processor's time stamp counter, calibrated against the system time, or on the system time.
  * Writes go to it and nothing reaches the chip: the program runs, silently.
* **Hand-over.** When the chip is free, the VM's next FM access takes it. Its virtual registers go to the chip first, key-on last, then its address latch, then the access.
* **Windows gives way.** Windows gets FM from a port access only until a DOS program wants it. `ESFM.DRV`'s 0102 still keeps it until MODM_CLOSE.
* **The same VM keeps its chip.** A DOS VM taking back the chip it had last isn't reset, since nobody used the chip in between.
* **DOS FM is heard.** A DOS FM owner gets the music DAC and, if 36h was 00h, FM volume FFh. When it lets go, both go back, unless something changed them meanwhile.
  * `ESFM.DRV` releases FM before it tells ES1869.DRV that MIDI closed. When a DOS box takes FM in that moment, the first DSP release by Windows afterwards (0003) gives the DOS box the music DAC and the volume again.
* **Windows' mixer comes back.** When a DOS VM takes the DSP, 30 mixer registers are saved (`ESSREG_Snap_Regs`); when it lets go, all of them go back after ESS's 11. A change made meanwhile through the register interface (essctl, ess3d) counts as Windows'.
* **Windows' next use resets FM.** When Windows acquires through the API, to play a sound, change a level (0002), open MIDI (0102) or the MPU-401 (0302), FM a DOS program left is reset: notes still sounding stop, and the next DOS program starts from a clean chip.
* A program's end drops the notes and timers of its VM's virtual chip, and closing the VM frees it.

## Hardware volume

* **Buttons.** The volume buttons change the master volume in the chip (mixer 60h/62h).
* **Callback.** When a 16:16 callback is registered with function 0006:
  * the driver enables the hardware volume interrupt (mixer 64h bits 1:0 set, `HwVol_Int_Enable` o5:1F0F). The request is cleared through mixer 66h.
  * on each interrupt, `HwVol_Event` (o5:1F2A) reports the new volume to the callback through nested execution (`Call_Client_Callback`, o5:2180)
* **`ES1869.DRV`** registers this callback so the Windows volume control follows the buttons.
* **Registry.** "HwVolume 2-Wire Mode" and "HwVolume Count By 3" set mixer 64h bits 3:2 and 5.

## No-DMA emulation

For machines whose ISA DMA can't be used, the driver emulates the DMA channels:
* `AUDDRV_ND_Emulate_DMA_In/Out` (o5:24D0, o5:2528) replace the port accesses of the DMA transfer.
* `ND_Setup_PIO_Buffer` (o1:0C3C, API 0007) sets up the buffer.
* `AUDDRV_ND_Generate_Interrupt` (o5:2A0C) generates the interrupts.

This mode is flag 0020h of the ADI. Function 0004 then reports an emulated count.

## Rebuilding the driver

`src/vxd/` holds the complete driver as NASM source:

| File | Contents |
|---|---|
| `es1869.asm` | top file, one include per LE object |
| `lcod.asm` ... `icod.asm` | one file per LE object |
| `vxd.inc`, `services.inc`, `ctlmsg.inc` | VxD service and control message macros and names |
| `adi.inc` | ADI field names |
| `essext.asm`, `essext.inc` | the essreg register API (group 4) and the DOS box improvements |
| `layout.json`, `stub.bin`, `version.bin`, `gap.bin`, `slack.bin` | the parts of the file that are not code: object order and flags, header fields, the stub, the version resource, and the bytes between the tables |
| `names.txt` | the names and comments given to addresses; `tools/vxd2asm.py` generated the source from the original with them |

```
python3 tools/build_vxd.py                  # build/ES1869.VXD with the essreg API
python3 tools/build_vxd.py --stock --verify # must equal driver/ES1869.VXD byte for byte
```

**How the build works.**
* `build_vxd.py` assembles `es1869.asm` with `nasm -f elf32` and links it with `tools/lelink.py`.
* The linker writes an LE file the way Microsoft's linker did:
  * fixup records in the same order, merged and split at page boundaries
  * data pages aligned
  * the last page padded
* `--stock` (`ESSREG_EXT=0`) must reproduce the original file exactly. `tests/test_retools.py` checks this.

**Editing.** Every reference in the source is symbolic and becomes a fixup, so code can grow in the middle of an object and the addresses follow. Two things to watch out for:
* Some data is reached through a base label plus an offset (for example `[D2_0000+0Ch]`). Inserting bytes inside such a structure breaks those references. Append new data at the end of PDAT (or LCOD, if it must be locked).
* The padding between routines and about a dozen instructions are kept as `db` bytes, because NASM has no spelling that reproduces their original encoding. Two of them are short jumps with fixed distances (o4:0A27 and o4:0AC1 in `pnp.asm`): don't insert code between them and their targets.

The essreg extension changes the original only in these places, each the same length, under `%if ESSREG_EXT`:
* the dispatcher's group bound (`cmp ah,4` becomes `cmp ah,5`), and the address of its group table: a copy with a fifth entry, whose groups 0, 1 and 3 wrap 0002, 0003, 0102, 0103 and 0302
* the ten FM trap handlers (PDAT), now `ESSREG_FM_Trap`
* in `Acquire_Resources` and `Release_Resources`: the FM reset (o5:1229), `FM_Enable_Local_Trapping` (o5:10B1), `Save_DOS_Mixer` (o5:1279) and `Restore_DOS_Mixer` (o5:110B)
* the size of the ADI (E9h becomes 110h, o4:0235) and of the per-VM node (2Eh becomes 34h: o4:00AB, o5:100D, o7:0042)
* in the control dispatcher, VM_Not_Executeable and Sys_Dynamic_Device_Exit; the node removal of a device that goes (o4:09C1)
* the DOSMGR hook's jump to the next hook (o1:0511), which now goes through `ESSREG_App_End`

Everything else is appended, so ESS's code stays at its addresses: Windows' sound, DirectSound and the interrupt handlers run the same bytes as in ESS's driver. `EssCodeTest` in `tests/test_vxdext.py` compares the two builds after their fixups, and a new hook goes in its list. `PcmPathTest` runs ES1869.DRV's calls around a wave device and DirectSound's acquire and release in both, and compares the port accesses.

## Installing the extended driver

* The extended `build/ES1869.VXD` behaves like the original for Windows and the ESS drivers, adds the register API, and does better for DOS programs ([DOS boxes](#dos-boxes)).
* **Keep a copy of the original.**
* Replacing the driver removes its DirectX certification mark (`Cert DX2`). DirectX setup may then report the driver as uncertified.

1. Copy `C:\WINDOWS\SYSTEM\ES1869.VXD` to `ES1869.ORG` in the same directory.
2. Copy `build\ES1869.VXD` over `C:\WINDOWS\SYSTEM\ES1869.VXD`.
3. Restart Windows. essctl's *Device information* page now shows "Register API: version 1.10".

**If Windows doesn't start** or sound stops working: restart, press F8 at "Starting Windows 95", choose *Command prompt only*, and run:

```
copy C:\WINDOWS\SYSTEM\ES1869.ORG C:\WINDOWS\SYSTEM\ES1869.VXD
```
