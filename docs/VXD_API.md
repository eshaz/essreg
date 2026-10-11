# ES1869.VXD programming interface

`ES1869.VXD` is the Windows 95 virtual device driver of the ES1869: device
`AUDDRV`, ID **3B07h**, version 4.04. Its only interface for programs is a
V86/protected-mode API, which the 16-bit ESS drivers use: `ES1869.DRV` for
wave and mixer, `ESFM.DRV` for FM MIDI and `ESSMPU.DRV` for the MPU-401. The
driver has no `W32_DEVICEIOCONTROL` handler and no VxD service table.

The rebuilt driver (`build/ES1869.VXD`) adds **group 4**, a register
interface for programs (see [Group 4](#group-4-register-access-essreg)).

Everything on this page was read from the driver's code. Handler addresses
are given as `object:offset` in the LE file, where object 5 is PCOD, and the
handlers' source is in `src/vxd/pcod.asm` and `src/vxd/essext.asm`.

## Calling the API

```
mov  ax,1684h          ; get device entry point
mov  bx,3B07h          ; AUDDRV
xor  di,di
mov  es,di
int  2Fh               ; ES:DI = entry point, 0:0 if not loaded
...
mov  dx,function       ; DH = group, DL = index
mov  ecx,devnode       ; for most functions: the ES1869 devnode
call far [entry]       ; CF clear = success, CF set = failure
```

DX selects the function. The dispatcher (o5:135C) checks the group and the
index against a table of `{count, function table}` pairs, and it sets CF for
anything out of range.

For the functions that act on a device, ECX holds the ES1869's devnode.
Function 0001 with AX = 0 returns the first device's structure, and the
dword at offset 55h of that structure is the devnode. The other inputs are
in AX, BX, and ES:BX or ES:DI. Results come back in the client registers. On
failure, most functions put an error code in AX, but some leave AX
unchanged.

The call works from V86 mode and from 16-bit protected mode, so DOS programs
in a DOS box and Windows programs can both use it. `src/win/vxdcall.asm` is
a thunk through which 16-bit C loads the full 32-bit registers, makes the
call and gets the registers back, and `src/vxdapi.c` wraps the functions
below.

## Functions

The Class column tells how safe a function is to call:
* **info** functions only read, and a program can call them at any time.
* **ownership** functions change which VM owns part of the card, with side
  effects on the hardware.
* **internal** functions are for the ESS drivers only, and a program must
  never call them. They register callbacks into the caller's code, or set up
  buffers that the driver uses later at interrupt time.
* **register** functions exist only in the rebuilt driver's group 4. They
  write a register or a port, and they refuse an access that would disturb a
  VM that owns the hardware ([Group 4](#group-4-register-access-essreg)).

| DX   | Handler | Class          | Function                               |
|------|---------|----------------|----------------------------------------|
| 0000 | o5:14F2 | info           | Version: AX = 0404h                    |
| 0001 | o5:15DA | info           | Copy the device structure (ADI)        |
| 0002 | o5:14FD | ownership      | Acquire the DSP or FM (BX = 1 or 2)    |
| 0003 | o5:155D | ownership      | Release the DSP or FM (BX = 1 or 2)    |
| 0004 | o5:148B | info           | DMA transfer count                     |
| 0005 | o5:138C | info/ownership | Read or set the GPO pins               |
| 0006 | o5:13CD | internal       | Set the hardware volume callback       |
| 0007 | o5:142A | internal       | Set up the PIO buffer for no-DMA mode  |
| 0008 | o5:1459 | info           | Configuration port                     |
| 0009 | o5:1404 | internal       | Set a second callback                  |
| 000A | o5:1922 | info           | Whether DirectSound has the DSP        |
| 000B | o5:1930 | internal       | Set a third callback                   |
| 0100 | o5:14F2 | info           | Version, with the same handler as 0000 |
| 0101 | o5:16D1 | info           | FM information                         |
| 0102 | o5:1654 | ownership      | Acquire FM                             |
| 0103 | o5:1691 | ownership      | Release FM                             |
| 0200 | o5:1750 | internal       | Register a notification client         |
| 0201 | o5:17E2 | internal       | Unregister a notification client       |
| 0300 | o5:14F2 | info           | Version, with the same handler as 0000 |
| 0301 | o5:1893 | info           | MPU-401 information                    |
| 0302 | o5:1816 | ownership      | Acquire the MPU-401                    |
| 0303 | o5:1853 | ownership      | Release the MPU-401                    |
| 0400 | essext  | info           | essreg extension: version and features |
| 0401 | essext  | info           | Read a mixer register                  |
| 0402 | essext  | register       | Write a mixer register                 |
| 0403 | essext  | info           | Read a controller register             |
| 0404 | essext  | register       | Write a controller register            |
| 0405 | essext  | info           | Read an Audio_Base port                |
| 0406 | essext  | register       | Write an Audio_Base port               |
| 0407 | essext  | info           | Read a configuration port              |
| 0408 | essext  | register       | Write a configuration port             |
| 0409 | essext  | info           | Read a PnP register                    |
| 040A | essext  | register       | Write a PnP register                   |
| 040B | essext  | info           | Read mixer registers 00h-7Fh           |
| 040C | essext  | info           | Owner and status information           |
| 040D | essext  | ownership      | Take the DSP from a DOS box to record  |

`ES1869.DRV` uses group 0, including 0006, 0007, 0009 and 000B, and group 2.
`ESFM.DRV` uses group 1, and `ESSMPU.DRV` uses group 3.

Of ESS's functions, essctl calls only the *info* functions, and 0002 and
0003 only as [Ownership and port trapping](#ownership-and-port-trapping)
describes. With the rebuilt driver, it reads and writes the chip through
group 4. It never calls 0006, 0007, 0009, 000B, 0200 or 0201, because a
callback left registered by a program that has exited would be called into
freed memory.

### 0000, 0100, 0300: version

Out: AX = 0404h.

### 0001: device structure

In: AX = 0 for the first ES1869, or AX = 1 to select one by its devnode in
ECX. ES:BX points to a buffer whose first dword is the number of bytes to
copy, at most E9h.

Out: the buffer holds that many bytes of the driver's device structure
(ADI), and AX = 1. On failure, CF is set and AX = 0.

These fields of the ADI are known, and the size of each field is in
parentheses:

* **04h** (word) holds the flags' low word, where 0001h means no "in use"
  warnings, 0020h means no-DMA emulation, and 2000h means that the MPU-401
  shares the audio IRQ ([DRIVER_CONFIG.md](DRIVER_CONFIG.md)).
* **06h** (word) holds Audio_Base.
* **08h** (word) holds the FM port that the driver uses, which is Audio_Base
  (220h), or FFFFh if there is none.
* **0Ah** (word) holds the FM alias base (388h), or FFFFh if there is none.
* **0Ch** (word) holds the MPU-401 port, or FFFFh if there is none.
* **0Eh** (byte) holds the MPU-401 IRQ.
* **0Fh** (byte) holds the audio IRQ.
* **10h** (byte) holds the DMA channel of Audio 1.
* **13h** (word) holds the flags' high word, where 0002h powers the digital
  section down when unused.
* **17h** (word) holds the driver version (0404h).
* **2Dh / 31h** (dwords) hold the VPICD handles of the MPU-401 and audio
  IRQs.
* **35h / 39h** (dwords) hold the VM that owns the DSP, and the previous
  owner.
* **3Dh / 41h** (dwords) hold the VM that owns FM, and the previous owner.
* **45h / 49h** (dwords) hold the VM that owns the MPU-401, and the previous
  owner.
* **4Dh / 7Ah** (dwords) hold the VDMAD handles of the two DMA channels.
* **55h** (dword) holds the devnode.
* **71h** (dword) holds the 16:16 hardware volume callback (0006).
* **79h** (byte) holds the DMA channel of Audio 2.
* **E1h / E5h** (dwords) hold the callbacks of 0009 and 000B.

### 0002 / 0003: acquire / release

In: BX = 1 for the DSP, with AX = Audio_Base, or BX = 2 for FM, with AX =
the FM port of 0101, which is Audio_Base too.

Out: AX = 0. On failure, CF is set and AX holds one of these codes:

| AX  | Meaning                                                        |
|-----|----------------------------------------------------------------|
| 1   | no such device                                                 |
| 2   | another VM owns it (0002), or the caller doesn't own it (0003) |

Acquiring makes the caller's VM the owner and stops trapping the ports for
that VM. Every acquisition of FM resets the FM synthesizer
([VXD_INTERNALS.md](VXD_INTERNALS.md)). When a VM acquires the DSP and the
previous owner was a different VM, the driver first resets the DSP, sets
mixer 71h bits 4 and 1, and clears mixer 74h, 76h, 78h and 7Ah. When a DOS
VM acquires the DSP, the driver also saves the mixer registers 7Ch, 1Ah,
32h, 36h, 38h, 3Eh, 3Ah, 3Ch, 64h, 60h and 62h.

Releasing the DSP always resets it and sends DSP commands 10h and 80h, and
for a DOS VM it restores the saved mixer registers. When "Want Local
Powerdown" is set and nothing is owned any more, a release also powers the
digital section down.

`ES1869.DRV` acquires the DSP when a wave device is opened and releases it
when the device is closed.

### 0004: DMA count

In: BX = 0 for Audio 1 or 1 for Audio 2, and ECX = devnode.

Out: AX = the physical count from VDMAD, or an emulated count in no-DMA
mode. On failure, CF is set and AX = FFFFh.

### 0005: GPO pins

In: ECX = devnode, and EAX selects the direction.
* With a nonzero EAX, the function reads the pins: EBX = Audio_Base+7h bits
  1:0 (GPO1, GPO0).
* With EAX = 0, it writes them: Audio_Base+7h bits 1:0 = BL bits 1:0.

### 0008: configuration port

In: ECX = devnode.

Out: DX = the ES1869's configuration port (Config_Base), which the driver
finds with the mixer 40h identification sequence.

### 000A: global flag

Out: EAX = the byte at o1:0328, which is 1 while DirectSound has the DSP.
DirectSound's acquire (o1:125C) sets it.

ES1869.DRV calls 000A at 4:01A7 with EAX = a 16:16 pointer to
`dsp_busy_callback` (3:4C3C), as if it registered a callback. This version
of the VxD ignores the pointer, so the callback never runs.

### 0101: FM information

In: AX = 1, ECX = devnode, and ES:BX = a buffer whose first dword is its
size, at most 1Ch.

Out:
* the word at +4 = 1
* the word at +6 = the FM port, ADI 08h, which is Audio_Base and not the
  388h alias
* the dword at +0Ch = the devnode

### 0102 / 0103: acquire / release FM

In: AX = the FM port of 0101 (Audio_Base). With 388h, both functions fail
with AX = 1.

Out: AX = 0. On failure, CF is set and AX = 1 (no device), 2 (in use) or 3
(not the owner).

### 0301: MPU-401 information

In: AX = 1, ECX = devnode, and ES:BX = a buffer whose first dword is its
size, at most 24h.

Out:
* the word at +4 has bit 0 set if the MPU-401 shares the audio IRQ
* the word at +6 = the MPU-401 port
* the byte at +8 = its IRQ

## Ownership and port trapping

The driver traps these ports in every VM:
* the audio ports, Audio_Base+4h to +Fh, except +8h and +9h, which belong to
  FM
* the FM ports
* the MPU-401 ports

Trapping stops only for the VM that owns the device. When a VM accesses a
port while the device is unowned, that VM becomes the owner. This goes
through `Acquire_Resources`, with the same side effects as 0002, and it
applies to Windows itself (the system VM) as much as to a DOS box. When a VM
accesses a port while another VM owns the device, its reads return FFh and
its writes are ignored. Unless "Disable Warning" is set, the driver also
shows "Unable to play sound...", "Unable to play MIDI..." or "Unable to
access MIDI port...".

The rebuilt driver answers the FM ports differently. A VM that can't have FM
gets a virtual FM chip, and Windows gets FM from a port access only until a
DOS program wants it ([VXD_INTERNALS.md](VXD_INTERNALS.md#dos-boxes)). A DOS
program that gave up the DSP for a recording (040D) gets a virtual Sound
Blaster on the DSP's ports in the same way.

With ESS's driver, a register tool has to work within these rules, so essctl
brackets each batch of port accesses:
1. It reads the owner from the ADI (0001).
2. It acquires the DSP (0002 with BX = 1). If a DOS box owns it, essctl
   stops here and reports "in use" without touching a port.
3. After the batch, it releases the DSP (0003), but only if step 1 found it
   free.

This leaves ownership exactly as it was, and the release costs one DSP
reset, the same as closing a wave device. Because every refresh would be one
of these brackets, essctl doesn't refresh on a timer with ESS's driver. The
group 4 functions do their I/O at ring 0, where nothing is trapped, and they
never acquire anything, so they have none of these side effects.

## Group 4: register access (essreg)

Group 4 exists only in the rebuilt driver (`src/vxd/essext.asm`, built with
`ESSREG_EXT=1`). Its functions take the ES1869's devnode in ECX (ADI offset
55h). A function clears CF on success, and on failure it sets CF and returns
an error code in AX:

| Code | Meaning                                                          |
|------|------------------------------------------------------------------|
| 1    | NODEV: ECX is not an ES1869 devnode                              |
| 2    | INUSE: another VM owns what the access would disturb (see below) |
| 3    | PARAM: a bad register, offset or buffer, or a port never written |
| 4    | BUSY: the DSP stays busy, or ESS's code powered the chip down    |
| 5    | TIMEOUT: the DSP returned no data                                |
| 6    | NOCFG: the configuration port wasn't found                       |

Every index/data port pair runs with interrupts disabled. Afterwards the
function restores the mixer index (Audio_Base+4h), the PnP index and the
logical device number, so an access never disturbs ES1869.DRV, a DOS program
in another VM, or the driver's own interrupt code.

The functions have these inputs and outputs:

* **0400** takes nothing and returns AX = 0113h (version 1.13), BX = the
  feature bits (0FFFh), CX = the SYSTEM.INI settings and DX = the number of
  functions (14).
* **0401** takes BL = a mixer register and returns AL = its value.
* **0402** takes BL = a mixer register and BH = the value.
* **0403** takes BL = a controller register (A0h-BFh) and returns AL = its
  value.
* **0404** takes BL = a controller register (A0h-BFh) and BH = the value.
* **0405** takes BL = an offset (0-Fh) and returns AL = Audio_Base+offset.
* **0406** takes BL = an offset (0-Fh) and BH = the value.
* **0407** takes BL = an offset (0-7) and returns AL = Config_Base+offset.
* **0408** takes BL = an offset (0-7, except 2-4) and BH = the value.
* **0409** takes BL = a logical device (FFh = card level) and BH = a PnP
  register, and returns AL = its value.
* **040A** takes BL = a logical device (FFh = card level), BH = a PnP
  register and AL = the value.
* **040B** takes ES:DI = a 128-byte buffer and fills it with mixer registers
  00h-7Fh, with 40h read as 0 because its reads advance the identification
  sequence.
* **040C** takes nothing and returns AL = the DSP owner, AH = the FM owner,
  BL = the MPU-401 owner (0 none, 1 the caller's VM, 2 another VM), BH =
  Audio_Base+Ch and DX = the ADI flags.
* **040D** takes nothing, and comes from Windows right before it opens
  ES1869.DRV's wave input to record the FM. A DOS program takes the DSP with
  its first Sound Blaster access and keeps it until it ends, which would
  keep the recording out. So when a DOS box owns the DSP, the VxD releases
  it from that box, and the program goes on with a virtual Sound Blaster.
  The function also succeeds when nobody or Windows owns the DSP, and then
  changes nothing. It fails with INUSE while `RecordTakesDSP=0`, and when a
  DOS box calls it. ES1869.DRV's FM recording device and esfmrec call it,
  and essctl never does.

The controller registers (0403, 0404) are reached through the DSP's command
channel. Both functions first wait until the DSP is idle, for up to 2000h
polls with interrupts on, and do the rest with interrupts off. They drop a
byte that nobody read, such as a late answer, and then send C6h (extended
mode), followed by C0h and the register to read, or by the register and the
value to write. While no VM owns the DSP, C7h follows, so the next owner
finds extended mode off, as after a reset. The functions poll Audio_Base+Ch
bit 6 for read data, because reading Audio_Base+Eh would clear the audio
interrupt.

The two functions fail with INUSE while another VM owns the DSP. They fail
with BUSY if the DSP doesn't take the command's first byte within 200h polls
or the rest within 2000h, and with TIMEOUT if it doesn't answer. While ESS's
code has the chip powered down (ADI flag 0080h), they fail with BUSY at
once.

These accesses are refused as well:
* While another VM owns the DSP, 0405 and 0406 fail with INUSE for the DSP's
  own ports: 0405 for Audio_Base+Ah, +Eh and +Fh (its read data, interrupt
  and FIFO), and 0406 for +6h, +Ch and +Fh (its reset, commands and FIFO).
* While another VM owns FM, 0406 fails with INUSE for the FM ports (+0h-3h,
  +8h, +9h), but reads of these ports stay allowed.
* 0408 always fails with PARAM for Config_Base+2h-4h, the EEPROM data,
  command (erase all, write all) and address ports.

A few more checks apply:
* Every call that uses the configuration port looks it up again, in the same
  way as ESS's 0008 finds it, and uses it only if it lies in 100h-FF8h, is a
  multiple of 8, and reports Audio_Base as the I/O base of logical device 1.
* 040B's buffer must be memory that the caller can write. In V86 mode, DI
  can be at most FF80h. In protected mode, ES must be a present, writable,
  expand-up segment whose limit covers the 128 bytes at the offset that
  Map_Flat used.

When Windows writes a mixer register with 0402 while a DOS box has the DSP,
the write also changes the value that Windows gets back afterwards.

0400 returns these feature bits in BX:

| Bit | Function                                                   |
|-----|------------------------------------------------------------|
| 0   | mixer                                                      |
| 1   | controller                                                 |
| 2   | ports                                                      |
| 3   | configuration ports                                        |
| 4   | PnP                                                        |
| 5   | mixer block                                                |
| 6   | owner information                                          |
| 7   | DOS FM, clear with `VirtualFM=0`                           |
| 8   | DOS mixer, clear with `DosMixerRestore=0`                  |
| 9   | CX holds the settings below (version 1.11)                 |
| 10  | Audio 1 filter, clear with `Audio1Filter=1` (version 1.12) |
| 11  | the DSP for a recording, clear with `RecordTakesDSP=0`     |
| 12  | DOS FM on its own clock, clear with `DosFMDelay=0`         |

DOS FM stands for the virtual FM chip and the rest of [DOS
boxes](VXD_INTERNALS.md#dos-boxes). DOS mixer means that Windows' mixer is
saved and put back around a DOS program. Audio 1 filter means that the VxD
keeps the Audio 1 CODEC's filter bypassed, for DOS programs and for
DirectSound. The DSP for a recording means that 040D takes the DSP from a
DOS program
([VXD_INTERNALS.md](VXD_INTERNALS.md#a-recording-takes-the-dsp)), and DOS FM
on its own clock means that a DOS program's FM plays evenly on the clock of
its timer ticks
([VXD_INTERNALS.md](VXD_INTERNALS.md#a-dos-programs-fm-on-its-own-clock)),
both since version 1.13.

CX holds the settings that the VxD read from SYSTEM.INI when it started
([DRIVER_CONFIG.md](DRIVER_CONFIG.md#6-the-rebuilt-drivers-systemini-settings)).
A set bit means that the change is on:

| Bit | Key                               | Default |
|-----|-----------------------------------|---------|
| 0   | `[ES1869.VXD] RegisterAPI`        | 1       |
| 1   | `VirtualFM`                       | 1       |
| 2   | `DosTakesFM`                      | 1       |
| 3   | `DosKeepsFM`                      | 1       |
| 4   | `DosFMAudible`                    | 1       |
| 5   | `DosMixerRestore`                 | 1       |
| 6   | `ResetDosFM`                      | 1       |
| 7   | `RecordTakesDSP` (version 1.13)   | 1       |
| 8   | `[ES1869.DRV] Audio2Oversampling` | 0       |
| 9   | `[ES1869.DRV] Audio2Filter`       | 0       |
| 10  | `[ES1869.DRV] Audio1Filter`       | 0       |
| 11  | `DosFMDelay` isn't 0 (1.13)       | 30      |
| 15  | SYSTEM.INI was read               |         |

Bit 0 is always set here, because with `RegisterAPI=0`, 0400 fails. When bit
15 is clear, the VxD was loaded after Windows started and runs with the
defaults.

### Detecting the extension

ESS's driver answers group 4 with the carry flag set, and that is how a
program tells the two drivers apart:

```c
// after getting the entry point and the devnode (function 0001)
r.edx = 0x0400; r.ecx = devnode;
if (!vxd_raw_call(entry, &r))      // carry clear
    version = (u16)r.eax;          // 0100h or later: extension present
// the stock driver: carry set, AX unchanged
```
