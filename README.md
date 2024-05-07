# essreg
ES1869 Register Utility

This repository contains two utilities designed for the ESS AudioDrive ES1869sound card chipset.

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
| or=[-1024,960]     | ADC Offset Wamples Right     Get / Set
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

Example: `essreg r=before.txt 3=0 m=1 p=1 r=0 t=0 r=after.txt`
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
* Known Issues: Patch banks that are larger than the existing bank will cause page faults. The custom patch bank must be the same size in bytes of the original.
* See the [ESFM patch banks](esfm_patch_banks) for the Windows 98, Windows NT4, and other custom patches.
* See the [recordings](digital_recording) for ESFM patch comparisons.

```
ESFM Patch Utility (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>

This utility writes your custom patch set to an existing ESFM.DRV file.
Compatible with the VxD Windows driver only.

** WARNING: BACKUP YOUR ORIGINAL ESFM.DRV BEFORE RUNNING THIS UTILITY! **

Usage: esfmpat "c:\path\to\esfm.drv" "c:\path\to\patch.bin"
```

## Building

### Prerequisites
* MSDOS like build environment (MS-DOS, Windows 9x, DOS-BOX, etc.)
* [Watcom C 11.0](https://winworldpc.com/product/watcom-c-c/110b)

### Building
* Clone this repo and copy it your build environment.
* Run [`build.bat`](build.bat) to build the executables

### Testing
* This code has been tested using a real ES1869 soundcard on I/O port 0x220. It probably won't work with any other ESS sound chips.

## License
* GPL 3.0

## Thanks to
* The community over at [Vogons](https://www.vogons.org/viewtopic.php?f=62&p=1258077) who helped out!
* [Phil's Computer Lab](https://www.philscomputerlab.com/ess-audiodrive-es1868.html) for uploading the ES1869 datasheet!
