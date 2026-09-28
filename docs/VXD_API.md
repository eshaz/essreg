# ES1869.VXD programming interface

* `ES1869.VXD` is the Windows 95 virtual device driver of the ES1869: device `AUDDRV`, ID **3B07h**, version 4.04.
* Its only interface for programs is a V86/protected-mode API. The 16-bit ESS drivers use it: `ES1869.DRV` (wave, mixer), `ESFM.DRV` (FM MIDI) and `ESSMPU.DRV` (MPU-401).
* It has no `W32_DEVICEIOCONTROL` handler and no VxD service table.
* The rebuilt driver (`build/ES1869.VXD`) adds **group 4**, a register interface for programs. See [Group 4](#group-4-register-access-essreg).
  * The stock driver answers group 4 with the carry flag set. That's how a program tells the two apart.
* Everything here was read from the driver's code. Handler addresses are `object:offset` in the LE file (object 5 = PCOD). The source is in `src/vxd/pcod.asm` and `src/vxd/essext.asm`.

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

* **DX** selects the function. The dispatcher (o5:135C) checks the group and index against a table of `{count, function table}` pairs and sets CF for anything out of range.
* **ECX** is the ES1869's devnode, for the functions that act on a device. Function 0001 with AX = 0 returns the first device's structure, and its dword at offset 55h is the devnode.
* The other inputs are in AX, BX, and ES:BX or ES:DI.
* Results come back in the client registers. On failure most functions put an error code in AX, but some leave AX unchanged.
* The call works from V86 mode (DOS programs in a DOS box) and from 16-bit protected mode (Windows programs).
  * `src/win/vxdcall.asm` is a thunk that loads and returns the full 32-bit registers from 16-bit C.
  * `src/vxdapi.c` wraps the functions below.

## Functions

Safety classes:
* **info**: reads only, callable at any time.
* **ownership**: changes which VM owns part of the card, with side effects on the hardware.
* **internal**: for the ESS drivers only. A program must never call these. They register callbacks into the caller's code, or set up buffers the driver uses later at interrupt time.

| DX | Handler | Class | Function |
|---|---|---|---|
| 0000 | o5:14F2 | info | Version: AX = 0404h |
| 0001 | o5:15DA | info | Copy the device structure (ADI) to ES:BX |
| 0002 | o5:14FD | ownership | Acquire the DSP (BX = 1) or FM (BX = 2) |
| 0003 | o5:155D | ownership | Release the DSP (BX = 1) or FM (BX = 2) |
| 0004 | o5:148B | info | DMA transfer count |
| 0005 | o5:138C | info / ownership | Read (EAX ≠ 0) or set (EAX = 0) the GPO pins |
| 0006 | o5:13CD | internal | Set the hardware volume callback |
| 0007 | o5:142A | internal | Set up the no-DMA (PIO) emulation buffer |
| 0008 | o5:1459 | info | Configuration port |
| 0009 | o5:1404 | internal | Set a second callback |
| 000A | o5:1922 | info | A global flag byte |
| 000B | o5:1930 | internal | Set a third callback |
| 0100 | o5:14F2 | info | Version (same handler as 0000) |
| 0101 | o5:16D1 | info | FM information |
| 0102 | o5:1654 | ownership | Acquire FM |
| 0103 | o5:1691 | ownership | Release FM |
| 0200 | o5:1750 | internal | Register a notification client |
| 0201 | o5:17E2 | internal | Unregister a notification client |
| 0300 | o5:14F2 | info | Version (same handler as 0000) |
| 0301 | o5:1893 | info | MPU-401 information |
| 0302 | o5:1816 | ownership | Acquire the MPU-401 |
| 0303 | o5:1853 | ownership | Release the MPU-401 |
| 0400 | essext | info | essreg extension: version and features |
| 0401 | essext | info | Read a mixer register |
| 0402 | essext | register | Write a mixer register |
| 0403 | essext | info | Read a controller register |
| 0404 | essext | register | Write a controller register |
| 0405 | essext | info | Read an Audio_Base port |
| 0406 | essext | register | Write an Audio_Base port |
| 0407 | essext | info | Read a configuration port |
| 0408 | essext | register | Write a configuration port |
| 0409 | essext | info | Read a PnP register |
| 040A | essext | register | Write a PnP register |
| 040B | essext | info | Read mixer registers 00h-7Fh |
| 040C | essext | info | Owner and status information |

The ESS drivers use these groups:
* `ES1869.DRV`: group 0 (including 0006, 0007, 0009 and 000B) and group 2.
* `ESFM.DRV`: group 1.
* `ESSMPU.DRV`: group 3.

essctl:
* Only calls the *info* functions, and 0002 and 0003 only as described under [Ownership](#ownership-and-port-trapping).
* Never calls 0006, 0007, 0009, 000B, 0200 or 0201. A callback left registered by a program that has exited would be called into freed memory.

### 0000, 0100, 0300: version

Out: AX = 0404h.

### 0001: device structure

In:
* AX = 0 to get the first ES1869, or 1 to select by devnode (ECX).
* ES:BX = buffer. Its first dword is the number of bytes to copy, at most E9h.

Out: the buffer holds that many bytes of the driver's device structure (ADI), and AX = 1. On failure CF is set and AX = 0.

Known ADI fields:

| Offset | Size | Contents |
|---|---|---|
| 04h | word | flags, low word (0001h no "in use" warnings, 0020h no-DMA emulation, 2000h MPU-401 shares the audio IRQ; see [DRIVER_CONFIG.md](DRIVER_CONFIG.md)) |
| 06h | word | Audio_Base |
| 08h | word | FM port the driver uses: Audio_Base (220h), FFFFh if none |
| 0Ah | word | FM alias base (388h), FFFFh if none |
| 0Ch | word | MPU-401 port, FFFFh if none |
| 0Eh | byte | MPU-401 IRQ |
| 0Fh | byte | audio IRQ |
| 10h | byte | DMA channel of Audio 1 |
| 13h | word | flags, high word (0002h: power the digital section down when unused) |
| 17h | word | driver version (0404h) |
| 2Dh / 31h | dword | VPICD handles of the MPU-401 and audio IRQs |
| 35h / 39h | dword | VM owning the DSP / previous owner |
| 3Dh / 41h | dword | VM owning FM / previous owner |
| 45h / 49h | dword | VM owning the MPU-401 / previous owner |
| 4Dh / 7Ah | dword | VDMAD handles of the two DMA channels |
| 55h | dword | devnode |
| 71h | dword | 16:16 hardware volume callback (0006) |
| 79h | byte | DMA channel of Audio 2 |
| E1h / E5h | dword | callbacks of 0009 and 000B |

### 0002 / 0003: acquire / release

In: AX = Audio_Base (BX = 1, DSP) or the FM port of 0101 (BX = 2, FM), which is Audio_Base too.

Out: AX = 0. On failure CF is set and:

| AX | Meaning |
|---|---|
| 1 | no such device |
| 2 | another VM owns it (0002), or the caller does not (0003) |

Acquire:
* Makes the caller's VM the owner and stops trapping the ports for that VM.
* If the previous owner was a different VM, it first resets the DSP, sets mixer 71h bits 4 and 1, and clears mixer 74h, 76h, 78h and 7Ah.
* For a DOS VM it saves the mixer registers 7Ch, 1Ah, 32h, 36h, 38h, 3Eh, 3Ah, 3Ch, 64h, 60h and 62h.

Release:
* Always resets the DSP and sends DSP commands 10h and 80h.
* For a DOS VM it restores the saved mixer registers.
* If "Want Local Powerdown" is set and nothing is owned any more, it powers the digital section down.

`ES1869.DRV` acquires the DSP when a wave device is opened and releases it when the device is closed.

### 0004: DMA count

In: BX = 0 (Audio 1) or 1 (Audio 2), ECX = devnode.

Out: AX = the physical count from VDMAD, or an emulated count in no-DMA mode. FFFFh with CF on failure.

### 0005: GPO pins

In: ECX = devnode.
* EAX ≠ 0: read. EBX = Audio_Base+7h bits 1:0 (GPO1, GPO0).
* EAX = 0: write. Audio_Base+7h bits 1:0 = BL bits 1:0.

### 0008: configuration port

In: ECX = devnode.

Out: DX = the ES1869's configuration port (Config_Base). The driver finds it with the mixer 40h identification sequence.

### 000A: global flag

Out: EAX = a byte the driver keeps at o1:0328. What it means isn't known.

### 0101: FM information

In: AX = 1, ECX = devnode, ES:BX = buffer whose first dword is its size (at most 1Ch).

Out:
* word at +4 = 1
* word at +6 = FM port: ADI 08h, Audio_Base, not the 388h alias
* dword at +0Ch = devnode

### 0102 / 0103: acquire / release FM

In: AX = the FM port of 0101 (Audio_Base). With 388h they fail with AX = 1.

Out: AX = 0. On failure CF is set and AX = 1 (no device), 2 (in use) or 3 (not the owner).

### 0301: MPU-401 information

In: AX = 1, ECX = devnode, ES:BX = buffer whose first dword is its size (at most 24h).

Out:
* word at +4: bit 0 set if the MPU-401 shares the audio IRQ
* word at +6 = MPU-401 port
* byte at +8 = its IRQ

## Ownership and port trapping

The driver traps these ports in every VM:
* the audio ports Audio_Base+4h to +Fh, except +8h and +9h, which belong to FM
* the FM ports
* the MPU-401 ports

Trapping stops only for the VM that owns the device.

* **A port access while the device is unowned** makes the accessing VM its owner.
  * This goes through `Acquire_Resources`, with all the side effects above.
  * It applies to Windows itself (the system VM) as much as to a DOS box.
* **A port access while another VM owns the device:**
  * shows "Unable to play sound...", "Unable to play MIDI..." or "Unable to access MIDI port..." (unless "Disable Warning" is set)
  * returns FFh for reads and ignores writes

The rebuilt driver answers FM ports differently: a VM that can't have FM gets a virtual FM chip, and Windows gets FM from a port access only until a DOS program wants it. See [VXD_INTERNALS.md](VXD_INTERNALS.md#dos-boxes).

**What this means for a register tool.** With the stock driver, essctl brackets each batch of port accesses:
1. It reads the owner from the ADI (0001).
2. It acquires the DSP (0002 BX=1). If a DOS box owns it, essctl stops here and reports "in use" without touching a port.
3. After the batch, it releases the DSP (0003), but only if step 1 found it free.

* This leaves ownership exactly as it was. The release costs one DSP reset, the same as closing a wave device.
* essctl doesn't refresh on a timer with the stock driver, because every refresh would be one of these brackets.
* The group 4 functions do their I/O at ring 0, where there's no trapping. They never acquire anything, so they have none of these side effects.

## Group 4: register access (essreg)

Only in the rebuilt driver (`src/vxd/essext.asm`, built with `ESSREG_EXT=1`).

Common rules:
* ECX = devnode of the ES1869 (ADI offset 55h). An unknown devnode fails with AX = 1.
* CF clear on success. CF set and AX = error code on failure:

  | Code | Meaning |
  |---|---|
  | 1 | NODEV: ECX is not an ES1869 devnode |
  | 2 | INUSE: another VM owns what the access would disturb (see below) |
  | 3 | PARAM: register or offset out of range, a port that's never written, bad buffer |
  | 4 | BUSY: the DSP write buffer stays busy, or ESS powered the chip down |
  | 5 | TIMEOUT: the DSP returned no data |
  | 6 | NOCFG: configuration port not found |

* Every index/data port pair runs with interrupts disabled.
* The mixer index (Audio_Base+4h) is restored afterwards, and so are the PnP index and logical device number. So an access never disturbs ES1869.DRV, a DOS program in another VM, or the driver's own interrupt code.

| DX | In | Out |
|---|---|---|
| 0400 | - | AX = 0110h (version 1.10), BX = feature bits (01FFh), DX = number of functions (13) |
| 0401 | BL = mixer register | AL = value |
| 0402 | BL = mixer register, BH = value | - |
| 0403 | BL = controller register A0h-BFh | AL = value |
| 0404 | BL = controller register A0h-BFh, BH = value | - |
| 0405 | BL = offset 0-Fh | AL = Audio_Base+offset |
| 0406 | BL = offset 0-Fh, BH = value | - |
| 0407 | BL = offset 0-7 | AL = Config_Base+offset |
| 0408 | BL = offset 0-7, except 2-4, BH = value | - |
| 0409 | BL = logical device (FFh = card level), BH = PnP register | AL = value |
| 040A | BL = logical device (FFh = card level), BH = PnP register, AL = value | - |
| 040B | ES:DI = 128-byte buffer | mixer registers 00h-7Fh (40h reads as 0: its reads advance the identification sequence) |
| 040C | - | AL = DSP owner, AH = FM owner, BL = MPU-401 owner (0 none, 1 the caller's VM, 2 another VM); BH = Audio_Base+Ch; DX = ADI flags |

**Controller registers (0403, 0404)** go through the DSP command channel:
* The DSP must be idle first. That wait, up to 2000h polls, runs with interrupts on; the rest with interrupts off.
* A byte nobody read (a late answer) is dropped first.
* C6h (extended mode), then C0h and the register to read, or the register and the value to write. While no VM owns the DSP, C7h follows, so the next owner finds extended mode off as after a reset.
* Read data is polled on Audio_Base+Ch bit 6. Reading Audio_Base+Eh would clear the audio interrupt.
* They fail with INUSE while another VM owns the DSP, and with BUSY or TIMEOUT if the DSP doesn't take the command's first byte within 200h polls, the rest within 2000h, or doesn't answer. While ESS has the chip powered down (ADI flag 0080h) they fail with BUSY at once.

**What else is refused:**
* While another VM owns the DSP: 0405 of Audio_Base+Ah, +Eh and +Fh (its read data, interrupt and FIFO), and 0406 of +6h, +Ch and +Fh (its reset, commands and FIFO).
* While another VM owns FM: 0406 of the FM ports (+0h-3h, +8h, +9h). Their reads stay allowed.
* 0408 of Config_Base+2h-4h, the EEPROM data, command (erase all, write all) and address ports.

**Other checks:**
* The configuration port is looked up on every call, as ESS's 0008 finds it, and used only if it's in 100h-FF8h, a multiple of 8, and its logical device 1 has Audio_Base as I/O base.
* 040B's buffer must be the caller's writable memory: in V86 mode DI up to FF80h; in protected mode a present, writable, expand-up segment whose limit covers the 128 bytes at the offset Map_Flat used.
* A 0402 by Windows while a DOS box has the DSP also changes the value Windows gets back afterwards.

Feature bits of 0400 (BX):

| Bit | Function |
|---|---|
| 0 | mixer |
| 1 | controller |
| 2 | ports |
| 3 | configuration ports |
| 4 | PnP |
| 5 | mixer block |
| 6 | owner information |
| 7 | DOS FM: the virtual FM chip and the rest of [DOS boxes](VXD_INTERNALS.md#dos-boxes) |
| 8 | DOS mixer: Windows' mixer saved and put back around a DOS program |

### Detecting the extension

```c
// after getting the entry point and the devnode (function 0001)
r.edx = 0x0400; r.ecx = devnode;
if (!vxd_raw_call(entry, &r))      // carry clear
    version = (u16)r.eax;          // 0100h or later: extension present
// the stock driver: carry set, AX unchanged
```
