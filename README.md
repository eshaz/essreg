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
ES1869 Register Utility
(c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>

|Option------|Description----------------------------------------------
| c          | (Calibrate Op Amp)
| r=[path]   | (Dump ES1869 Registers)
|            |   * `path` to save the registers; defaults to essreg.txt
| g          | (Get all values)
| 3=[value]  | (Get / Set 3D Amount)
|            |   * 0         3D disable
|            |   * 1  - 63   3D amount (raw value)
|            |   * 1% - 100% 3D amount (percent)
| ml=[value] | (Get / Set Mono-In Level)
|            |   * 0  - 15   Mono-In Level (raw value)
|            |   * 0% - 100% Mono-In Level (percent)
| m=[1,0]    | (Enable / Disable Mono-In)
| p=[1,0]    | (Enable / Disable Mic Preamp)
| d=[1,0]    | (Enable / Disable FM,IIS,ES689 digital record)
| t=[1,0]    | (Enable / Disable Telegaming Mode)

`3`, `ml`, `m`, `p`, `d`, `t`
  * Omitting the parameter will show the current value.

Example: `essreg r=before.txt 3=0 m=1 p=1 r=0 t=0 r=after.txt`
```

## [`1869opl3.com`](build)
* Utility to fix ES1869 OPL3 playback for some games in the Windows DOS box.
* This program reads the FM volume mixer register on the sound card and then runs the application.
* Sound card with ES1869 chip is assumed to be on I/O port 220h.

```
Example:

c:\>1869opl3.com "c:\path\to\game.exe"
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