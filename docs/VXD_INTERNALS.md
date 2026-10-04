# ES1869.VXD internals

This page describes how `ES1869.VXD`, the Windows 95 virtual device driver
of the ES1869, works. It is based on the disassembly in `src/vxd/` of
version 4.04 (file version 4.04.00.1319, 36028 bytes).
[VXD_API.md](VXD_API.md) describes the program interface, and the last two
sections explain how to rebuild and change the driver and how to install the
extended one.

## File

`ES1869.VXD` is a Linear Executable (LE) with page size 200h. It has seven
objects, listed here by name with each object's number and size in
parentheses:

* **LCOD** (object 1, size 1954h) holds locked code and data: the DDB, the
  device list, the trap handlers' helpers, the DSP I/O primitives and the
  DirectSound HAL GUIDs.
* **MCOD** (object 2, size 11Dh) holds locked code, the VxD service
  wrappers.
* **RARE** (object 3, size 22Ch) holds `Sys_Dynamic_Device_Exit`.
* **PNP** (object 4, size D6Ch) holds the Plug and Play code: device
  creation and the configuration handlers.
* **PCOD** (object 5, size 37F5h) holds pageable code: trapping, ownership,
  the V86/PM API, hardware volume, power management and the no-DMA
  emulation.
* **PDAT** (object 6, size 4F8h) holds pageable data: registry names, API
  tables and strings.
* **ICOD** (object 7, size 8Ch) holds `Sys_Dynamic_Device_Init` and is
  discarded after init.

All fixups are internal: 334 of type 7 (32-bit offset) and 241 of type 8
(32-bit relative), where a fixup that crosses a page boundary is counted
once. The DDB, at o1:0258, holds the name `AUDDRV`, device ID 3B07h and DDK
version 4.0. Its V86 and PM API procedures are both o5:135C, and there's no
service table.

The MZ stub contains the string `Cert DX2`, the DirectX certification mark,
and the version resource (VS_VERSION_INFO) follows the fixup tables. File
pages 15-21 are slack pages, which hold leftovers that the linker copied in:
the COFF symbol table of `nodma.obj`. That table names the no-DMA emulation
routines, such as `AUDDRV_ND_*`, `_AUDDRV_Get_pADI_From_XXX` and
`abNDOneOpndBypass`, and many of the names in `src/vxd/names.txt` come from
there.

## Start-up

The Plug and Play configuration manager loads the driver dynamically
(`Sys_Dynamic_Device_Init`, o7:0000). `PnP_New_DevNode` (o4:0ACE) registers
the driver as device driver and enumerator for the ES1869 devnode, and the
driver's configuration handler then calls `ADI_Create` (o4:01FC) with the
allocated resources:
* Audio_Base, the FM port and its alias, and the MPU-401 port
* the audio and MPU-401 IRQs
* the two DMA channels
* the flags read from the registry (see
  [DRIVER_CONFIG.md](DRIVER_CONFIG.md))

`ADI_Create` allocates the device structure (ADI, E9h bytes), adds it to the
device list, virtualizes the IRQs through VPICD and the DMA channels through
VDMAD, and installs the I/O trap handlers. Once the device is running, the
driver tells MMDEVLDR whether `midi\esfm.drv` and `midi\essmpu.drv` are
present (`MMDEVLDR_SetDevicePresence`). It also sets the `BLASTER`
environment string for DOS programs (`MMDEVLDR_SetEnvironmentString`) in the
format `A%03X I%d D%d`, with IRQ 9 written as 2.

When the devnode is the no-DMA variant (flag 0020h), a child devnode,
`VIRTUAL\ESS1869-DMAEmulation`, takes the DMA resources, and the driver
emulates DMA with programmed I/O.

`AUDDRV_Control` (o1:03E0) handles these control messages, with the address
of each message's handler in parentheses:

* **Sys_VM_Init** and **VM_Critical_Init** (o5:1000) set up the trapping for
  each VM.
* **VM_Not_Executeable** (o5:0F84) makes a dying VM give up what it owned,
  and the "previous owner" of each of those resources becomes FFFFFFFFh, so
  the next owner gets a reset.
* **Sys_Dynamic_Device_Init** (o7:0000) starts the driver, and the extended
  driver reads SYSTEM.INI first.
* **Sys_Dynamic_Device_Exit** (o3:0000) unloads the driver.
* **PnP_New_DevNode** (o4:0ACE) announces a new ES1869, or its no-DMA child.

## Ownership and trapping

The driver manages three resources: the **DSP**, which is the audio part
(Audio_Base+4h to +Fh, except +8h and +9h), **FM** and the **MPU-401**. For
each of them, the ADI records an owner VM and a previous owner.

The FM ports are Audio_Base+0h to +3h, +8h and +9h, and the alias 388h to
38Bh (ADI 0Ah). ADI 08h holds Audio_Base, which is the FM port that 0101
reports and that 0102 and 0103 expect.

`DSP_Port_Trap` (o5:0C7C), `FM_Port_Trap` (o5:0D28) and `MPU_Port_Trap`
(o5:0CE4) handle a trapped port access. The owner gets the real port,
through `Simulate_IO` or a direct `in` or `out`. So does a VM that finds the
resource unowned, which makes it the owner (`Acquire_Resources`, o5:1177).
Any other VM gets `Show_Contention_Message` (o5:0F0F), and its reads return
FFh.

The message is a SHELL message box, shown once per VM, and "Disable
Warning", which ESS's INF sets, turns it off. The box says:
* "Unable to play sound - the ESS AudioDrive is in use by another
  application." for the DSP
* "Unable to play MIDI - the ESS FM Synthesizer is in use by another
  application." for FM
* "Unable to access MIDI port - the ESS MPU-401 is in use by another
  application." for the MPU-401

Acquiring a resource stops trapping for the new owner. Every FM acquisition
resets the FM synthesizer (o5:19C4: Audio_Base+7h bit 5 high for 25 reads,
with 36h muted meanwhile), and one that comes from a port access then writes
65 registers (o5:0BD0) that select OPL2 mode, silence all operators and mask
the timers.

When the DSP changes hands, that is, when the new owner isn't the previous
one, the driver resets the DSP (`DSP_Reset`, o5:195C: Audio_Base+6h = 1,
three reads, 0) and prepares Audio 2 (mixer 71h |= 12h, and 74h, 76h, 78h
and 7Ah cleared). When a DOS VM acquires the DSP, the driver saves the mixer
registers 7Ch, 1Ah, 32h, 36h, 38h, 3Eh, 3Ah, 3Ch, 64h, 60h and 62h
(`Save_DOS_Mixer`) and gives FM the music DAC (7Fh bit 0 cleared,
`L1_039C`). When the system VM acquires it, the driver disables DMA address
translation for both channels.

Releasing a resource (`Release_Resources`, o5:1061) turns trapping back on.
For the DSP, the driver resets it and sends DSP commands 10h and 80h, a
direct output of silence (`DSP_Reset_On_Release`). For a DOS VM, the driver
also restores the saved mixer registers (`Restore_DOS_Mixer`) and 7Fh bit 0.
Before it writes the mixer registers back, it sends DSP commands C6h and
C3h, reads a byte, and sends C2h 01h if the byte's bit 0 is clear. These
commands aren't in the data sheet.

Releasing FM doesn't silence the synthesizer, so notes left on keep sounding
until the next FM acquisition resets it. When nothing is owned any more and
"Want Local Powerdown" is set, the driver powers the digital section down
(o5:2122: Audio_Base+7h bits 3 and 2 set, a wait until Audio_Base+6h bit 3
clears, then bit 2 cleared).

The driver hooks DOSMGR_End_V86_App (o7:0076, handler o1:04B8). When any
program in a DOS VM ends, the VM gives back everything it owns, even if the
program was a child and its parent goes on.

`ES1869.DRV` acquires the DSP when a wave device opens and releases it on
close, and it also acquires and releases the DSP around every mixer change.
`ESFM.DRV` acquires FM at MODM_OPEN and releases it at MODM_CLOSE. So on an
idle Windows desktop nothing is owned, and the first program in any VM that
touches a port becomes its owner.

## DOS boxes

With ESS's driver, DOS boxes have these limits:
* A DOS program's FM detection works only while FM is free or already
  belongs to its VM. While a Windows MIDI program or another DOS box has FM,
  every read returns FFh, and the AdLib, OPL3 and ESFM detections all fail.
* A Windows program that touches an FM port while FM is free makes Windows
  the owner until a MIDI program opens and closes, and until then DOS boxes
  have no FM.
* A DOS FM owner gets neither the music DAC (7Fh bit 0) nor an FM volume
  (36h). While no Windows MIDI program is open, ES1869.DRV gives them to I2S
  and IIS, so a DOS program that uses only FM can be silent. `1869opl3`
  works around this.
* When a child program ends, its parent's next FM access resets the
  synthesizer under it.
* When a DOS program gives the DSP back, 11 mixer registers go back to
  Windows' values. The others stay as the program left them: the 3-D effect,
  the record source and levels, the wave volume, MONO_IN and MONO_OUT.

The extended driver (`src/vxd/essext.asm`) changes this. Each change has a
key in the `[ES1869.VXD]` section of SYSTEM.INI. The keys are on by default,
and `0` gives back ESS's behavior
([DRIVER_CONFIG.md](DRIVER_CONFIG.md#6-the-rebuilt-drivers-systemini-settings)).

`VirtualFM` makes FM detection always succeed. A VM that can't have the FM
chip gets a virtual one, a block of 1.3 KB that the driver allocates on the
VM's first access. The virtual chip answers like the ES1869. It has the OPL3
registers of both banks, ESFM native mode with its readback, and the timers
(80 and 320 us per count). Its status port shows the IRQ, FT1 and FT2 flags,
and bits 4:0 read 0 as on an OPL3. The timers run on the processor's time
stamp counter, calibrated against the system time, or else on the system
time. Writes go to the virtual chip and nothing reaches the real one, so the
program runs, silently.

When the real chip is free, the VM's next FM access takes it. The driver
first writes the VM's virtual registers to the chip, key-on last, then its
address latch, and only then carries out the access. When a program ends,
the driver drops the notes and timers of its VM's virtual chip, and it frees
the virtual chip when the VM closes.

`DosTakesFM` makes Windows give way. Windows gets FM from a port access only
until a DOS program wants it, but `ESFM.DRV`'s 0102 still keeps FM until
MODM_CLOSE.

`DosKeepsFM` lets the same VM keep its chip. A DOS VM that takes back the
chip it had last isn't reset, since nobody used the chip in between.

`DosFMAudible` makes FM from DOS programs audible. A DOS FM owner gets the
music DAC and, if 36h was 00h, FM volume FFh. When it lets go, both go back,
unless something changed them meanwhile. `ESFM.DRV` releases FM before it
tells ES1869.DRV that MIDI closed, and if a DOS box takes FM in that moment,
the first DSP release by Windows afterwards (0003) gives the DOS box the
music DAC and the volume again.

`DosMixerRestore` brings Windows' mixer back. When a DOS VM takes the DSP,
the driver saves 30 mixer registers (`ESSREG_Snap_Regs`), and when the VM
lets go, it writes all of them back after ESS's 11. A change made meanwhile
through the register interface, by essctl or ess3d, counts as Windows'.
Mixer register 71h comes back with the driver's mode of the DACs, by default
no 4x oversampling and both filters bypassed, because ESS's code sets that
mode before the save (o5:198C).

`ResetDosFM` resets FM when Windows next uses the card. When Windows
acquires through the API, to play a sound or change a level (0002), open
MIDI (0102) or open the MPU-401 (0302), FM that a DOS program left is reset,
so notes still sounding stop and the next DOS program starts from a clean
chip.

A DOS program also gets the DACs in the mode that Windows' sound has. The
VxD reads `Audio2Oversampling`, `Audio2Filter` and `Audio1Filter` from the
`[ES1869.DRV]` section, as ES1869.DRV does. When a VM takes the DSP, 71h
gets the mode of both DACs, and with `Audio1Filter=0` the Audio 1 DAC's
filter is bypassed as well, for Windows and for DOS programs. The DMA
handlers then follow a DOS program's transfers, which VDMAD reports to them
without any extra port trapping (`VDMAD_Get_Virt_State`), and write the mode
again in case the program reset the mixer: on Audio 1's channel the Audio 1
filter's bypass, in either direction, and on Audio 2's channel the Audio 2
mode ([AUDIO1.md](AUDIO1.md#the-filter-of-the-audio-1-codec)).

## Hardware volume

The volume buttons change the master volume in the chip (mixer 60h/62h).
When a 16:16 callback is registered with function 0006, the driver enables
the hardware volume interrupt (mixer 64h bits 1:0 set, `HwVol_Int_Enable`
o5:1F0F), whose request is cleared through mixer 66h. On each interrupt,
`HwVol_Event` (o5:1F2A) reports the new volume to the callback through
nested execution (`Call_Client_Callback`, o5:2180). `ES1869.DRV` registers
this callback so that the Windows volume control follows the buttons.

The registry values "HwVolume 2-Wire Mode" and "HwVolume Count By 3" set
mixer 64h bits 3:2 and bit 5 respectively.

## No-DMA emulation

For machines whose ISA DMA can't be used, the driver emulates the DMA
channels. `AUDDRV_ND_Emulate_DMA_In` and `AUDDRV_ND_Emulate_DMA_Out`
(o5:24D0, o5:2528) replace the port accesses of the DMA transfer,
`ND_Setup_PIO_Buffer` (o1:0C3C, API function 0007) sets up the buffer, and
`AUDDRV_ND_Generate_Interrupt` (o5:2A0C) generates the interrupts. The mode
is flag 0020h of the ADI, and in this mode function 0004 reports an emulated
count.

## Rebuilding the driver

`src/vxd/` holds the complete driver as NASM source:

* `es1869.asm` is the top file, with one include per LE object.
* `lcod.asm` ... `icod.asm` hold one LE object each.
* `vxd.inc`, `services.inc` and `ctlmsg.inc` have the macros and names of
  the VxD services and control messages.
* `adi.inc` has the ADI field names.
* `essext.asm` and `essext.inc` hold the essreg register API (group 4) and
  the DOS box improvements.
* `layout.json`, `stub.bin`, `version.bin`, `gap.bin` and `slack.bin` hold
  the parts of the file that aren't code: the object order and flags, the
  header fields, the stub, the version resource, the bytes between the
  tables, and the slack pages.
* `names.txt` has the names and comments given to addresses, with which
  `tools/vxd2asm.py` generated the source from the original file.

The first of these commands builds `build/ES1869.VXD` with the essreg API,
and what the second builds must equal `driver/ES1869.VXD` byte for byte:

```
python3 tools/build_vxd.py
python3 tools/build_vxd.py --stock --verify
```

`build_vxd.py` assembles `es1869.asm` with `nasm -f elf32` and links it with
`tools/lelink.py`. The linker writes an LE file the way Microsoft's linker
did, with the fixup records in the same order, merged and split at page
boundaries, the data pages aligned and the last page padded. `--stock`
(`ESSREG_EXT=0`) must reproduce the original file exactly, and
`tests/test_retools.py` checks that it does.

### Changing the driver

Every reference in the source is symbolic and becomes a fixup, so code can
grow in the middle of an object and the addresses follow. One thing needs
care: some data is reached through a base label plus an offset, for example
`[D2_0000+0Ch]`, and inserting bytes inside such a structure breaks those
references. Append new data at the end of PDAT, or of LCOD if it must be
locked.

The padding between routines and about a dozen instructions are kept as `db`
bytes, because NASM has no spelling that reproduces their original encoding,
such as `8B C0` for `mov eax,eax`. Two thunks that the driver uses only by
their address (o4:0A17 and o4:0AB1 in `pnp.asm`) are kept as bytes as well.

The essreg extension changes ESS's code only in the following places, each
under `%if ESSREG_EXT` and each keeping its length:
* the dispatcher's group bound (`cmp ah,4` becomes `cmp ah,5`), and the
  address of its group table, which now points to a copy with a fifth entry
  whose groups 0, 1 and 3 wrap 0002, 0003, 0102, 0103 and 0302
* the ten FM trap handlers in PDAT, which are now `ESSREG_FM_Trap`
* the calls in `Acquire_Resources` and `Release_Resources` of the FM reset
  (o5:1229), `FM_Enable_Local_Trapping` (o5:10B1), `Save_DOS_Mixer`
  (o5:1279) and `Restore_DOS_Mixer` (o5:110B)
* the size of the ADI (E9h becomes 110h, o4:0235) and of the per-VM node
  (2Eh becomes 34h: o4:00AB, o5:100D, o7:0042)
* the control dispatcher's jumps for VM_Not_Executeable,
  Sys_Dynamic_Device_Exit and Sys_Dynamic_Device_Init, which now reads the
  settings first
* the node removal for a device that goes away (o4:09C1)
* the DOSMGR hook's jump to the next hook (o1:0511), which now goes through
  `ESSREG_App_End`
* the two writes of mixer 71h, the Audio 2 mode, when a VM takes the DSP
  (o5:198C) and at the VxD's own Audio 2 start (o5:3603), which now go
  through `ESSREG_A2_Mode` and by default turn 4x oversampling off and
  bypass both DACs' filters
  ([AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#the-audio-2-dac-oversampling-and-the-filter)).
* the last jump of the two DMA handlers, for Audio 1's channel (o1:0546) and
  Audio 2's (o1:0870), which now goes to `VDMAD_Default_Handler` through
  `ESSREG_DMA1` and `ESSREG_DMA2`

Everything else is appended, so ESS's code stays at its addresses, and
Windows' sound, DirectSound and the interrupt handlers run the same bytes as
in ESS's driver. `EssCodeTest` in `tests/test_vxdext.py` compares the two
builds after their fixups, and a new hook goes in its list. `PcmPathTest`
runs ES1869.DRV's calls around a wave device, and DirectSound's acquire and
release, in both builds and compares the port accesses.

### Settings

`ESSREG_Read_Settings` reads the keys once, at `Sys_Dynamic_Device_Init`,
into `ESSREG_Opts`, and each changed routine checks its bit. VMM's profile
services (`Get_Profile_String`) exist only while Windows starts, so the VxD
reads SYSTEM.INI only if `VMM_GetSystemInitState` is still below 40000000h,
that is, before Init_Complete ends. A VxD loaded later, for a card found
while Windows runs, keeps the defaults.

A value is read the way `GetPrivateProfileInt` reads it in the 16-bit
drivers, as its leading digits or 0 if there are none, so `VirtualFM=0` and
`VirtualFM=no` both turn the change off. `RegisterAPI=0` sets group 4's
function count to 0, so 04xx fails with CF set and AX unchanged, as in ESS's
driver.

Function 0400 returns the settings in CX ([VXD_API.md](VXD_API.md)), and
essctl's *Device information* page lists the changes that are off and
whether SYSTEM.INI was read. `tests/test_vxdini.py` reads each key and
checks what each one turns off. It also runs DOS programs, Windows' sound
and MIDI with every key at 0 against ESS's driver, and compares the port
accesses, owners, messages and trapping.

## Installing the extended driver

The extended `build/ES1869.VXD` behaves like ESS's driver for Windows and
the ESS drivers, adds the register API, and does better for DOS programs
([DOS boxes](#dos-boxes)). Replacing the driver removes its DirectX
certification mark (`Cert DX2`), so DirectX setup may then report the driver
as uncertified.

[`essinst.exe`](../README.md#essinstexe) installs it, together with the
other rebuilt drivers that are next to it, and restarts Windows. To install
it by hand:

1. Keep a copy of ESS's driver: copy `C:\WINDOWS\SYSTEM\ES1869.VXD` to
   `ES1869VX.ORG` in the same directory, the name essinst uses too.
2. Copy `build\ES1869.VXD` over `C:\WINDOWS\SYSTEM\ES1869.VXD`.
3. Restart Windows. essctl's *Device information* page now shows "Register
   API: version 1.12".

If sound stops working, run `essinst /restore`. If Windows doesn't start,
restart, press F8 at "Starting Windows 95", choose *Command prompt only*,
and put ESS's driver back:

```
copy C:\WINDOWS\SYSTEM\ES1869VX.ORG C:\WINDOWS\SYSTEM\ES1869.VXD
```
