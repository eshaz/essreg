# essreg
ES1869 Register Utility

This repository contains utilities and drivers designed for the ESS AudioDrive ES1869 sound card chipset.

## [`essctl.exe`](build)
* Windows 95 control panel for the ES1869 (a 16-bit Windows program).
* Shows every register of the chip, 133 registers and 272 settings, on these pages: *Output mixer*, *Master volume*, *Record*, *3-D, mic, MONO, I2S*, *Serial / telegaming*, *Audio 2 channel*, *Audio 1 controller*, *ADC offset & power*, *Status & interrupts*, *Plug and Play*, *SB compatible mixer*, *Raw registers* and *ESFM patch bank*.
  * Each setting shows the decoded value (sample rates in Hz, ADC offsets in samples, named choices). The help line shows the register, bits and data sheet page.
  * Every change is written and read back, so the page shows what the chip returned.
* Settings that can stop playback, hang the DSP or move the card to other resources are read-only until Options > *Expert mode* is turned on for the session.
* Talks to the card in one of three ways:
  * Through the register interface of the rebuilt `ES1869.VXD` (below). Nothing Windows or a DOS game is doing is disturbed.
  * Directly, with the stock driver. essctl borrows the sound device around each access and gives it back, so a DOS game only gets "in use" while essctl is reading.
  * `/sim`: a simulated ES1869, to try essctl without the card.
* **Profiles**: File > *Save profile* stores the 50 ordinary settings in an INI file, and File > *Load profile* restores them.
  * The ESS driver resets the mixer whenever Windows starts and only puts back its own settings ([docs/DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md)). Put `essctl /load C:\ESS\MY.INI` in the StartUp group to restore yours at every start of Windows.
* **ESFM**: ESFM > *Load patch bank* replaces the FM synthesizer's sounds while Windows runs.
  * Check out the *ESFM patch bank* page! It shows the synthesizer's 18 voices live and marks hanging notes. *Stress test* plays dense music to check for them ([docs/ESFM_MIDI.md](docs/ESFM_MIDI.md)).

```
essctl [/load file] [/save file] [/dump file] [/ui] [/q]
       [/sim] [/base=220] [/cfg=800] [/novxd]

/load, /save  apply or save a profile without opening the window
/dump         write every readable register to a text file
/ui           open the window after /load, /save or /dump
/q            no message boxes (problems go to ESSCTL.LOG)
/sim          simulated ES1869
/base, /cfg   Audio_Base and Config_Base (hex) if detection fails
/novxd        direct I/O even with the register interface
```

## [`ES1869.VXD`](build) with a register interface
* The Windows 95 driver of the ES1869, rebuilt from source in [`src/vxd`](src/vxd), with a register interface for programs added ([docs/VXD_API.md](docs/VXD_API.md)).
* Otherwise it works exactly like the ESS driver. `python3 tools/build_vxd.py --stock --verify` rebuilds the original byte for byte.
* Install: keep a copy of `C:\WINDOWS\SYSTEM\ES1869.VXD`, copy `build\ES1869.VXD` over it and restart Windows.
  * See [docs/VXD_INTERNALS.md](docs/VXD_INTERNALS.md#installing-the-extended-driver) for the steps, and how to go back if something goes wrong.
* *Note: the rebuilt driver no longer carries ESS's DirectX certification mark.*

## [`ESFM.DRV`](build) without hanging notes
* The Windows 95 MIDI driver of the ES1869's FM synthesizer, rebuilt from source in [`src/esfm`](src/esfm).
* ESS's driver drops MIDI messages that come in while it's busy, so notes hang when the music gets busy. This one queues them instead.
* It also silences every voice when a program closes the device with the sustain pedal down.
* It can play a patch bank straight from a file named in `SYSTEM.INI`, and loads it again whenever the file changes. See [docs/ESFM_BANK.md](docs/ESFM_BANK.md#bank-file-buildesfmdrv).
* See [docs/ESFM_MIDI.md](docs/ESFM_MIDI.md) for the details.
* Everything else is ESS's code. `python3 tools/build_esfm.py --stock --verify` rebuilds the original byte for byte, and `esfmpat` and essctl's banks work the same.
* Install it from DOS, since Windows has the driver open: keep a copy of `C:\WINDOWS\SYSTEM\ESFM.DRV`, copy `build\ESFM.DRV` over it and restart Windows.

## [`essreg.exe`](build)
* Utility to control otherwise unsupported registers for the ES1869 audio chip.
* This program can be executed in native DOS or Windows DOS box environment.
* Sound card with ES1869 chip is assumed to be on I/O port 220h.
* Check out the `d` option that enables digital recording of the FM chip at 48kHz!
  * [Examples!](digital_recording)

```
ES1869 Register Utility (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>

|Option--------------|Description----------------------------------------
| a                  | Get all values
| r=[path]           | Dump ES1869 Registers        default "essreg.txt"
| c                  | Calibrate Op Amp
| 3=[0,63; 0%,100%]  | 3D Amount                    Get / Set
| ol=[-1024,960]     | ADC Offset Samples Left      Get / Set
| or=[-1024,960]     | ADC Offset Samples Right     Get / Set
| a1s                | Audio 1 Sample Rate          Get
| a1f                | Audio 1 Filter Clock         Get
| a2s                | Audio 2 Sample Rate          Get
| a2f                | Audio 2 Filter Clock         Get
| pa=[1,0]           | Analog Stays On              Enable / Disable
| pd                 | Digital Power Down           Get
| m=[1,0]            | Mono-In                      Enable / Disable
| ml=[0,25; 0%,100%] | Mono-In Level                Get / Set 
| micp=[1,0]         | Mic Preamp                   Enable / Disable
| fmd=[1,0]          | FM,IIS,ES689 digital record  Enable / Disable
| fms=[1,0]          | FM,IIS,ES689 digital sync    Enable / Disable
| fmr=[1,0]          | FM Reset                     Execute
| t=[1,0]            | Telegaming Mode              Enable / Disable
| x=[1,0]            | Safe DSP protocol (C6h)      Enable / Disable

Example: `essreg r=before.txt 3=0 m=1 pa=1 t=0 r=after.txt`
```

## [`1869opl3.com`](build)
* Utility to fix ES1869 OPL3 playback for some games in the Windows DOS box.
* This program reads the FM volume mixer register on the sound card and then runs the application.
* Sound card with ES1869 chip is assumed to be on I/O port 220h.
* The IIS mixer control must not be muted in the Windows volume mixer. However, the volume can be set to 0.

```
Example:

c:\>1869opl3.com "c:\path\to\game.exe"
```

## [`esfmpat.exe`](build)
* Utility to write custom patch banks to the Windows ESFM VxD driver.
* Patch banks that are larger than the existing bank work now, up to 32752 bytes. They're appended to the driver file and the driver is told the new size.
* The original driver is kept as `ESFM.BAK`.
* Accepts raw banks and RIFF "Ptch" bank files, and checks the bank and the driver before changing anything. See [docs/ESFM_BANK.md](docs/ESFM_BANK.md) for the bank format.
* See the [ESFM patch banks](esfm_patch_banks) for the Windows 98, Windows NT4, and other custom patches.
* See the [recordings](digital_recording) for ESFM patch comparisons.
* To try a bank without restarting Windows, load it with essctl (ESFM > *Load patch bank*).

```
ESFM Patch Utility (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>

This utility writes your custom patch set to an existing ESFM.DRV file.
Compatible with the VxD Windows driver only.  The original driver is kept
as ESFM.BAK next to it.

Usage: esfmpat "c:\path\to\esfm.drv" "c:\path\to\patch.bin"
       esfmpat "c:\path\to\esfm.drv"   (show its patch bank)
```

## Documentation
* [docs/REGISTERS.md](docs/REGISTERS.md): every register and setting of the ES1869, generated from the catalog [`src/esscat.tbl`](src/esscat.tbl).
* [docs/VXD_API.md](docs/VXD_API.md): the programming interface of `ES1869.VXD`, including the added register interface.
* [docs/VXD_INTERNALS.md](docs/VXD_INTERNALS.md): how the driver works, and how to rebuild and install it.
* [docs/DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md): the registry settings of the ESS drivers, and the registers the Windows driver sets.
* [docs/ESFM_BANK.md](docs/ESFM_BANK.md): FM patch banks and `ESFM.DRV`.
* [docs/ESFM_MIDI.md](docs/ESFM_MIDI.md): why ESFM notes hang, the MIDI driver stack, and the fixed `ESFM.DRV`.
* [docs/RE_NOTES.md](docs/RE_NOTES.md): how the drivers were reverse-engineered.
* [docs/TESTING.md](docs/TESTING.md): automated tests and a checklist for real hardware.
* [docs/datasheet](docs/datasheet): the ES1869 data sheet.

## Building

### Prerequisites
* MSDOS like build environment (MS-DOS, Windows 9x, DOS-BOX, etc.)
* [Watcom C 11.0](https://winworldpc.com/product/watcom-c-c/110b)
  * Install the DOS and 16-bit Windows targets.
* Python 3 and NASM for the drivers (any operating system)

### Building
* Clone this repo and copy it your build environment.
* Run [`build.bat`](build.bat) to build the executables
* Run `python3 tools/build_vxd.py` to build `build/ES1869.VXD`, and `python3 tools/build_esfm.py` to build `build/ESFM.DRV`.
* On Linux, `tools/ow2build.sh <open-watcom-v2 directory>` builds the programs with Open Watcom v2 into `out/ow2/`.
  * The programs in [`build`](build), other than `1869opl3.com`, were built this way. `build.bat` builds the same programs with Watcom C 11.0.

### Testing
* This code has been tested using a real ES1869 soundcard on I/O port 0x220. It probably won't work with any other ESS sound chips.
* For essctl and the rebuilt drivers, [docs/TESTING.md](docs/TESTING.md) has a checklist for real hardware.
* `python3 tests/run_tests.py` runs the automated tests against a simulated ES1869.

## License
* GPL 3.0

## Thanks to
* The community over at [Vogons](https://www.vogons.org/viewtopic.php?f=62&p=1258077) who helped out!
* [Phil's Computer Lab](https://www.philscomputerlab.com/ess-audiodrive-es1868.html) for uploading the ES1869 datasheet!
