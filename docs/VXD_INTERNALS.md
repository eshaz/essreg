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

## DirectSound

The driver is a DirectSound driver as well (DirectX's `dsdriver.h`). When
the device is created, `L5_2D00` (o5:2D00) loads `dsound.vxd`, builds an
`IDsDriver` object with the vtable at o6:0488 and registers it with
`DSOUND_RegisterDeviceDriver`. Its `GetDriverDesc` (o5:2AA0) names the
driver, the device's devnode and the VxD ID 3B07h, with the flags
DSDDESC_DOMMSYSTEMOPEN and DSDDESC_DONTNEEDPRIMARYLOCK. The first makes
DirectSound open the wave device through MMSYSTEM first, so that ES1869.DRV
opens it and owns it as for any program. `GetCaps` (o5:2B50) offers one
primary buffer, mono or stereo, 8 or 16 bits, and no hardware mixing, so
DirectSound mixes every secondary buffer in software into the primary
buffer. `Open` (o5:2B20) takes the DSP through `L5_335E` and
`Acquire_Resources`, and `Close` gives it back.

`CreateSoundBuffer` (o5:2BA0) makes the primary buffer: an `IDsDriverBuffer`
with the vtable at o6:04C8, over the second of the buffers that `ADI_Create`
allocated for Audio 2's DMA channel (ADI 86h, its linear address, 82h its
physical address and 8Ah its size, 32 KB). DirectSound writes into this
buffer. `SetFormat` (o5:2F10) and `SetFrequency` (o5:2F90) program the rate
through `L5_34F6`, and `GetPosition` (o5:2FF0) reads the DMA count
(`VDMAD_Get_Phys_Count`) and reports it as both cursors. `Lock` and `Unlock`
return E_NOTIMPL, which the DONTNEEDPRIMARYLOCK flag allows.

`Play` (o5:30D0) depends on the channel. On an 8-bit DMA channel (0 to 3)
the DMA plays DirectSound's buffer itself. On a 16-bit channel (5) the chip
takes only the low byte of each word, so the driver plays a third buffer of
twice the size (ADI D3h, its physical address at CFh), with each byte of
DirectSound's buffer in the low byte of a word (`L5_3309`, the same copy
that ES1869.DRV makes at 1:1B82). ESS's `Play` copies the first 80 ms, then
sets a global time-out that runs every 20 ms (`L5_3040`), which copies 80 ms
again from wherever the DMA is. The DMA therefore plays what the time-out
copied last, and a time-out that is late by more than DirectSound's own lead
plays what the buffer held a lap ago.

With `DirectSoundLock=1`, the extension changes four entries of the two
vtables. `ESSREG_DS_GetDriverDesc` runs ESS's `GetDriverDesc` and clears
DSDDESC_DONTNEEDPRIMARYLOCK, so that DirectSound brackets each write to the
primary buffer with `Lock` and `Unlock`. `ESSREG_DS_Lock` returns the part
of the buffer that DirectSound asks for, in two pieces where it wraps.
`ESSREG_DS_Unlock` copies the pieces that DirectSound wrote into the 16-bit
channel's buffer, at twice their offset, with `L5_3309`, once and at once;
on an 8-bit channel it has nothing to do. `ESSREG_DS_Play` copies the whole
buffer first, so that the DMA finds what DirectSound wrote before it
started, sets no time-out, and continues in ESS's `Play` at o5:3178, which
programs the DMA controller and starts Audio 2. `Stop`, `GetPosition` and
the rest stay ESS's. With `DirectSoundLock=0`, the default since version
1.16, the four entries run ESS's code: a card played 8-bit with the lock on,
which the emulator can't show, so the lock waits for a check on the card.

The driver answers `QueryInterface` for `IDsDriverNotify`,
`IDsDriverPropertySet` and `IDsCaptureDriver` with E_NOINTERFACE, so
DirectSound keeps its own notifications and property sets, and
DirectSoundCapture records through the wave input device. Hardware
notifications and property sets concern hardware secondary buffers, which
the ES1869 doesn't have, so DirectSound does as well without them. A capture
driver would need its own DMA path on Audio 1 inside the VxD, and nothing
here tests one, so it isn't attempted.

`tests/test_vxdext.py` (`DirectSoundTest`) runs both vtables in the
emulator: the description's flags, `Lock` in two pieces, `Unlock` on both
kinds of channel, `Play` with and without the time-out, and the stock driver
against the extended one with `DirectSoundLock=0`.

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
* A DOS program written for the Sound Blaster 16 finds a Sound Blaster Pro,
  DSP version 3.01, and plays 8-bit sound at best, because the chip plays
  16-bit sound only through its own Extended mode commands.

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

### A recording takes the DSP

A DOS program takes the DSP with its first access to a Sound Blaster port,
which is usually its sound card detection, and its VM keeps the DSP until
the program ends. ES1869.DRV's wave input needs the DSP too, because it
records through Audio 1. So with ESS's driver, a DOS game that plays only FM
music keeps every recording out, including the recordings of its own music.

`RecordTakesDSP` lets a recording of the FM take the DSP from the game.
ES1869.DRV's FM recording device and esfmrec call function 040D right before
they open the wave input. When a DOS VM owns the DSP, `ESSREG_VSB_Start`
reads what the VM's Audio 1 channel does (`VDMAD_Get_Virt_State`) and
releases the DSP from the VM through ESS's `Release_Resources`, with the
reset, the trapping and the mixer registers that a release brings. If the VM
owns FM, it then gets the music DAC back, because ESS's release gives it to
I2S. The VM gets a virtual Sound Blaster, and the recording opens as usual.
A call from a DOS VM, and every call while `RecordTakesDSP=0`, fails with
INUSE, and ES1869.DRV's open then refuses as before.

The virtual Sound Blaster replaces ESS's `DSP_Port_Trap` for that VM
(`ESSREG_DSP_Trap`), and every other VM still gets ESS's handler. It answers
the way the chip does:
* Writing 1 and then 0 to the reset port puts AAh in the read data.
* It takes the DSP commands of the Sound Blaster and the Sound Blaster Pro:
  the time constant (40h), the rate (41h, 42h), the block size (48h), the
  single-cycle, auto-initialize and ADPCM transfers, pause, continue and
  exit (D0h, D4h, DAh, D9h), the speaker (D1h, D3h, D8h), E0h, E4h, E8h and
  F2h, which interrupts.
* E1h and E7h return the chip's own version and identification, which the
  VxD asks the chip for before the VM's first access, unless ESS has the
  chip powered down just then: an ES1869 with version 3.01 stands in.
* In ESS's Extended mode (C6h, C7h), the controller registers A0h-BFh keep
  what the program wrote and C0h reads them back. B8h bit 0 starts or stops
  a transfer of the count in A4h/A5h, at the rate of A1h, for the channels
  of A8h and the sample size of B7h.
* The mixer registers 00h-3Fh start as the chip had them for the program,
  keep what the program writes, and a write to 00h clears them. The chip
  keeps Windows' mixer.
* Audio_Base+Ch always takes a byte, and reading Audio_Base+Eh acknowledges
  the interrupt.

The transfers are silent but keep their timing. A block lasts as long as the
chip would take for its bytes at the programmed rate, and a VMM global
time-out (`Set_Global_Time_Out`) ends it, in units of 1/1024 ms so that the
blocks don't drift. VTD gives the VxD 1 ms timer interrupts while the
virtual Sound Blaster exists (`VTD_Begin_Min_Int_Period`), so a block ends
when it is due and not at the timer's next 55 ms tick. At the end of a
block, the VM gets the audio interrupt through VPICD
(`VPICD_Set_Int_Request`), unless an Extended mode transfer runs without B1h
bit 6, and an auto-initialize transfer starts its next block. ESS's
end-of-interrupt handler (o1:0734) would end a physical interrupt that the
chip never raised, so `ESSREG_EOI` only clears the request for the virtual
Sound Blaster's VM, except when ESS's handler delivered a physical MPU-401
interrupt to that VM on the shared IRQ (ADI flag 1000h), which ESS's handler
ends. A transfer that runs when the recording starts goes on as blocks of
half its buffer at 21.7 kHz, because the reset clears the rate it set, until
the program starts its next transfer.

The program gets the real DSP back when it resets the DSP while nobody has
it, or when Windows lets go of the DSP (0003, `ESSREG_VSB_Back`) while its
virtual Sound Blaster is idle in Sound Blaster mode. The VxD then acquires
the DSP for the VM, writes the program's stereo bit (mixer 0Eh) to the chip,
and sends the speaker state, the time constant and the block size that the
program set. The levels stay Windows', as after any DOS program.
`ESSREG_DMA1` and `ESSREG_DMA2` kept the program's DMA programming off the
chip meanwhile, so an event in the program's VM (`Schedule_VM_Event`), which
runs before the program does, calls ESS's DMA handler for Audio 1's channel,
as VDMAD does when a VM programs a channel. A program whose transfer still
runs keeps the virtual Sound Blaster until it stops the transfer, resets the
DSP or ends.

A device has one virtual Sound Blaster at a time, so the next recording that
takes the DSP from another program ends the first program's. The virtual
Sound Blaster ends with its program, with its VM, with the device or with
the VxD, and no time-out or event outlives it. The program's FM is not
affected by any of this: it goes on to the chip, and so into the recording.

### A DOS program's FM on its own clock

A DOS program plays its FM music from its timer interrupt: it programs the
timer to a rate such as 70, 140 or 700 Hz and writes the FM registers of
each step of the music at a tick. Windows gives a DOS box its ticks only
while the box runs. When Windows itself is busy, for example with a
recording or a disk, the box can wait 20 ms or more, and then it gets the
ticks it missed one right after the other. Its FM writes come in the same
bursts, so the music stops and then hurries, and its tempo wavers.

`DosFMDelay` plays the FM writes of a real-mode program on a clock that
follows the program's ticks, so they reach the chip as evenly as the program
meant them, in their order, a fixed delay later. The default delay is 30 ms,
which covers the usual waits, and `DosFMDelay` takes it in milliseconds, up
to 250.

The ticks come from interrupt 8, the timer's, which VMM reflects into the
VM. A hook on it (`Hook_V86_Int_Chain`, `ESSREG_FMQ_Int8`) sees each tick
that the program gets, also those of a burst. VPICD watches the hooks that
VxDs set, and it delivers a hooked interrupt through VMM's `Simulate_Int`,
which calls them, instead of its own faster path.

When a real-mode program takes the FM chip, `ESSREG_FMQ_Start` traps its FM
ports again. From then on, its virtual chip answers its reads, the status
port with its timers included, and keeps its registers, while each port
write also goes into a queue (`ESSREG_FMQ_Put`). The write gets the time of
the program's last tick on the clock, plus the delay, and a time-out plays
it to the chip when it's due (`ESSREG_FMQ_Drain`), with ESS's 6 status reads
after each port write. VTD gives the VxD 1 ms timer interrupts meanwhile
(`VTD_Begin_Min_Int_Period`), so the time-outs come within a millisecond.

The clock (`ESSREG_FMQ_Tick`) starts from the first 16 intervals between
ticks. It takes the 12th shortest as a tick, which is the usual interval
between ticks that came on time, whatever bursts came among them. From then
on, each tick moves the clock on by a tick, but never past the real time and
never further behind it than the delay. After each window of 32 ticks, the
smallest lag of the window, which a tick that came on time has, pulls the
clock to the ticks over the next window. The average since the clock locked
corrects the tick. When most ticks of a window run into one of the limits,
the program changed its rate, and the clock starts again from 16 intervals.
In the emulator, the writes of a burst after 20 ms without ticks reach the
chip within a millisecond of their places.

Some programs can't play from the queue, and play directly, as with ESS's
driver:
* A program in protected mode (`VMSTAT_PM_APP`), such as a DOS/4GW game,
  gets its ticks in protected mode, where the hook doesn't see them.
* A program whose music doesn't follow interrupt 8, such as one that uses
  the RTC or polls a timer, writes long after its last tick. When a quarter
  of 64 data writes come at least 4 ms and half a tick after the last tick,
  the queue goes to the chip at once and the program plays directly until it
  ends (`ESSREG_FMQ_Direct`). Until the first tick, the start counts as a
  tick of the timer's default 55 ms, so a program that sets up every
  register as soon as it finds the chip stays on the queue.

When the program lets go of the chip, by ending or with its VM, the queue
goes to the chip at once (`ESSREG_FMQ_End`). When the device or the VxD goes
away, the queue is dropped, and the VxD unhooks interrupt 8. Each FM access
of a program that plays from the queue goes through the VxD, which costs
some processor time, as it does for a program with a virtual chip.

### A Sound Blaster 16 for DOS programs

The chip answers DSP commands as a Sound Blaster Pro, version 3.01, and
plays 16-bit sound only through its own Extended mode. A DOS program written
for the Sound Blaster 16 asks for version 4.xx, starts its transfers with
Bxh and Cxh, programs a 16-bit DMA channel for them, sets its volumes in
mixer registers 30h-47h and acknowledges a 16-bit transfer's interrupt at
Audio_Base+Fh. On the chip it plays 8-bit sound at best, or nothing.

With `SoundBlaster16=1`, a DOS VM that takes the DSP gets a Sound Blaster 16
in front of the chip (`ESSREG_S16_Start`, from ESS's `Acquire_Resources`).
ESS's code untraps the audio ports for the owner, and the front traps seven
of them again for its VM: the mixer index and data, the reset, the read
data, the command, the status and the 16-bit acknowledge ports. Everything
else, the FM ports included, stays as ESS has it.

The front answers as DSP 4.05 and the CT1745 mixer do, as the model of
[SB16.md](SB16.md) has them. Every command takes the operands the DSP's
firmware takes, and an answer goes out a byte at a time through the read
latch, so E1h gives version 4.05, E3h the copyright, E0h the inverted byte
and E4h/E8h the test register. The rate generator of 40h, 41h and 42h runs
at v * 44100 / 248 Hz, with the firmware's table for the time constant, and
a reset gives the cold boot's rate, block length and speaker, or the warm
boot's after the high-speed and MIDI UART commands. The 8-bit and 16-bit
transfers have their own interrupts in mixer 82h and their own acknowledges
(+Eh and +Fh).

The 8-bit transfers, Cxh and the older commands, and every recording play or
record on Audio 1 in the chip's Extended mode (`ESSREG_S16_Xfer`), and a
16-bit playback plays on Audio 2, so that both play at once, as the Sound
Blaster 16's two engines do. On Audio 1 the front follows the sequence of
the data sheet (p.48) that ES1869.DRV's recording uses: C6h and D1h once,
B8h for the direction and auto-initialize, B9h for demand transfers of 2
bytes, or single ones with ESS's "Single Mode DMA" setting, A8h for mono or
stereo, A1h and A2h for the rate as ES1869.DRV computes them from the rate
generator's value, A4h and A5h for the length in bytes, B6h and B7h for the
format, B1h and B2h for the interrupt and the DMA request, mixer 71h bit 5
off, so that A1h means what the front computes, then B8h bit 0. A DSP reset
would take the chip out of Extended mode, so the front emulates the
program's reset: the transfer stops, the chip's interrupt is acknowledged,
AAh comes back, and the chip stays in Extended mode. The firmware's commands
that Extended mode doesn't have, the direct DAC and ADC, ADPCM, silence and
MIDI, go to the chip in its Compatibility mode, after a reset of its own,
with the rate as a time constant.

Pause and continue (D0h and D4h, D5h and D6h) turn Audio 1's DMA requests
off and on (B2h bit 6), and D9h and DAh end the auto-initialize with the
block that plays (B8h bit 2). A single-cycle transfer of the kind that runs
auto-initialized becomes its last block, as the firmware queues it.

On Audio 2 (`ESSREG_S16_A2_Xfer`) the front starts the 16-bit playback as
ES1869.DRV starts its wave output (6:2C1A): 78h at 0, so that the transfer
starts anew, 70h and 72h for the rate and the filter clock as ES1869.DRV
computes them (6:2436), 74h and 76h for minus the length in bytes, 7Ch for
the volume of the program's voice level, 7Ah for the format and the
interrupt, 71h bit 1 for Audio 2's own clock, and then 78h for the DMA and
the FIFO to the DAC, auto-initialized as the command says, with demand
transfers of 4 bytes, or single ones with "Single Mode DMA". D5h and D6h
turn 78h bit 0 off and on, D9h ends the auto-initialize (78h bit 4), and a
single-cycle 16-bit command while Audio 2 auto-initializes becomes its last
block. The program's reset stops Audio 2 and clears only the interrupt bits
of 7Ah (7 and 6), so that what its FIFO still holds plays out in the
stream's format, in whole frames. A reset of the chip clears 7Ah and 7Ch (DS
p.51), so the front's own resets, for the commands of Compatibility mode,
put them back while Audio 2 plays. Audio 2 needs an 8-bit channel of its
own, 0, 1 or 3 and not Audio 1's, because the chip takes one byte a DMA
cycle on any channel, and `SoundBlaster16Audio2=1`, the default. Otherwise
the 16-bit playback plays on Audio 1, as a 16-bit recording always does, and
a transfer of one kind ends the other kind's.

The whole sequence is about 30 bytes, and four of its registers are read
first for the bits the front keeps, each read waiting for the chip's answer
with interrupts off. A game that plays block by block sends a transfer
command for every block, so the front keeps the chip's registers A0h-BFh
that it wrote or read since the chip's last reset (`EX_S16_Ctrl`,
`EX_S16_Known`): a transfer writes only the registers that change and reads
each of the others once, and a transfer like the last costs B8h twice. The
work stays in the port trap, where the program waits for it: the chip takes
each byte within microseconds, and a time-out would start the transfer
later.

The DMA runs on the DACs' 8-bit channels, where a 16-bit sample is two
bytes. A program that uses the 16-bit channel (`SoundBlaster16DMA`, 5 by
default, the `H5` of `BLASTER`) programs it as ever. When that channel is
Audio 2's, ESS's handler for it gets the programming (`ESSREG_DMA2`); else
the VxD takes the channel from VDMAD once (`VDMAD_Virtualize_Channel`), and
its callback gets it. Either way the programming, region, length and mode,
goes to the channel of the DAC that plays the transfer, Audio 2's or Audio
1's (`VDMAD_Set_Virt_State`, with the length in bytes and the mode changed
to demand transfers), and ESS's handler for that channel programs the chip's
channel from it, as if the program had (`ESSREG_S16_DMA_State`). A transfer
command takes the channel's state from VDMAD as well, so a program that
programmed the channel before the front had it plays too, and while the
channel is masked, the DAC's waits masked. Masking the 16-bit channel masks
the DAC's. VDMAD tells the front when the program reads the channel's count
(`VDMAD_Set_Channel_Callbacks`), and the front answers with the DAC
channel's count in words (`VDMAD_Get_Phys_Count` and `VDMAD_Set_Virt_Count`,
`ESSREG_S16_Position`), also at the end of each block, so a program that
polls the position sees it move. Mixer 81h picks the 16-bit channel: the one
`SoundBlaster16DMA` names, or none, when the program plays 16-bit sound
through the 8-bit channel, which it programs itself. Another channel only
reads back, because the VxD has just the one from VDMAD.

ESS's hardware interrupt procedure requests the owner VM's interrupt without
acknowledging the chip. The program's handler reads 82h, which the front
answers from the chip's Audio_Base+Ch bits: Audio 1's counter interrupt is
its transfer's kind, and the processor's (F2h, the firmware's transfers,
MIDI input) is the 8-bit kind unless F3h asked for the 16-bit one. Audio 2's
latch (7Ah bit 7) is the 16-bit kind, read from 7Ah because ESS's procedure
masks Audio 2's interrupt (7Ah bit 6) where the MPU-401 shares the IRQ. The
acknowledge of the right kind reads the chip's +Eh, or clears Audio 2's
latch (`ESSREG_S16_Ack`). ESS's code puts Audio 2's interrupt on the card's
IRQ (PnP 72h, o5:1BF2), so both reach the program.

The Sound Blaster 16's mixer registers 30h-47h are the program's view
(`EX_S16_Mix`), in the bits the CT1745 keeps, and the chip gets the volumes
they stand for, each register its own (`ESSREG_S16_Mix_Apply`): master in
60h/62h, voice in 14h, and in Audio 2's 7Ch while the front uses Audio 2,
MIDI in 36h, CD in 38h, line in 3Eh, the mic in 1Ah and the PC speaker in
3Ch, with CD, line and the mic through the output switches of 3Ch, and the
record source of 1Ch from the input switches of 3Dh/3Eh. A program that
starts sees the levels the chip has, which are Windows'
(`ESSREG_S16_Mix_Chip`), so a program that reads its volumes and writes them
back changes nothing but the bits the CT1745 doesn't have: the lowest of the
master's 6 and of the PC speaker's 3. A mixer reset (00h) is the Sound
Blaster 16's: the view gets the Sound Blaster 16's reset values and the chip
their levels, and the chip's other registers stay as Windows and the VxD set
them: the 3-D effect, the DACs' modes, the music DAC's source and Audio 2,
which the chip's own reset would put back to their power-on values.

The Sound Blaster Pro's registers 04h, 0Ah, 22h, 26h, 28h and 2Eh are views
of the Sound Blaster 16's, as on the CT1745, and 0Ch and 0Eh aren't there,
so a Sound Blaster Pro program's stereo bit has no effect, as on a Sound
Blaster 16. 80h-82h answer with the card's IRQ, its channels and the
interrupts that are pending, and writes to them don't reach the chip. Every
other mixer register reaches the chip as it comes. The chip's own volume
registers change a level of the view: the front marks that level stale and
reads it back from the chip the next time the program uses the Sound Blaster
16's registers (`ESSREG_S16_Mix_Stale`, `ESSREG_S16_Mix_Fresh`). When a
recording takes the DSP, the view keeps the program's levels, and they go
back to the chip when the program gets the DSP again.

A program that uses Extended mode itself gets the chip throughout: its
commands, Bxh and Cxh included, its registers and its mixer go to the chip
as they come, until its next reset, whose AAh the front takes from the chip
and answers itself. C6h is both the Sound Blaster 16's 8-bit
auto-initializing transfer and the chip's Extended mode, and the byte after
it tells them apart: a transfer's mode byte has only bits 4 and 5, and a
command has more. E7h, the chip's identification, isn't the Sound Blaster
16's, so a program that asks for it gets no answer, as from a Sound Blaster
16, and goes on as a Sound Blaster 16 program. The front's settings stay
with the VM while a recording has the DSP, and go when the program or the VM
ends.

Some limits remain. A buffer that crosses a 64 KB physical page is copied
through VDMAD's buffer, as every DOS program's is on Audio 1's channel, and
an auto-initializing transfer in such a buffer plays stale data. The whole
16-bit sound goes through one 8-bit channel, two bytes a sample, with demand
transfers of 4 bytes on Audio 2 and 2 on Audio 1, so the bus is arbitrated
no more often than for a Sound Blaster 16's words. Each DAC has its own
rate, where the Sound Blaster 16's engines share one, so a program that
plays both at different rates hears each at its own. And E2h, the DSP's DMA
identification, writes no byte through the DMA, because the front has no way
to.

In DOS itself, `esssb16.com` (`src/esssb16.asm`) is the same front as a
resident program. There no driver sees a port access, so it traps the Sound
Blaster ports through the memory manager: EMM386's port trap interface,
QEMM's QPI, which JEMM gets from QPIEMU, and for protected-mode games
HDPMI32i, with which it registers as a 32-bit client. Without VDMAD, it
traps the ports of the game's 16-bit channel itself where the memory manager
lets it (QPI and HDPMI32i), follows its programming and answers its count
from the DAC's channel, as VDMAD's callbacks let the VxD do. It plays 16-bit
sound on Audio 2 when the card gives Audio 2 an 8-bit channel of its own
(PnP 75h, which `/2` sets), and puts Audio 2's interrupt on Audio 1's IRQ
(PnP 72h), as ESS's VxD does. EMM386 has no traps for the DMA controllers,
so there the front reads the programming back from the controller at each
16-bit command, and the count the game reads stands still. Its view of the
mixer starts from the chip's levels when it loads and then stays the game's:
the chip's own registers that a program writes don't change it. Nothing
tells it when a game starts, so a DSP reset makes the front a Sound Blaster
16 again for the next game, even after a program that used the chip's
Extended mode.

`tests/test_sb16conf.py` plays the games' scenes of `tests/sbscenes.py` on
the model and on both fronts, in front of a model of the chip, and compares
what the game saw; `tests/test_sb16front.py` checks what both fronts send
the chip ([TESTING.md](TESTING.md)).

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
* the ten FM trap handlers in PDAT, which are now `ESSREG_FM_Trap`, and the
  26 DSP trap handlers, which are now `ESSREG_DSP_Trap`
* the end-of-interrupt handler of the audio IRQ in PDAT (0368), which is now
  `ESSREG_EOI`
* the calls in `Acquire_Resources` and `Release_Resources` of the FM reset
  (o5:1229), `FM_Enable_Local_Trapping` (o5:10B1), `Save_DOS_Mixer`
  (o5:1279) and `Restore_DOS_Mixer` (o5:110B)
* the size of the ADI (E9h becomes 1ACh, o4:0235) and of the per-VM node
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
* four entries of the DirectSound vtables in PDAT: GetDriverDesc (0494),
  Lock (04D4), Unlock (04D8) and Play (04F0), which are now `ESSREG_DS_*`
  ([DirectSound](#directsound))

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
   API: version 1.19".

If sound stops working, run `essinst /restore`. If Windows doesn't start,
restart, press F8 at "Starting Windows 95", choose *Command prompt only*,
and put ESS's driver back:

```
copy C:\WINDOWS\SYSTEM\ES1869VX.ORG C:\WINDOWS\SYSTEM\ES1869.VXD
```
