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

## [`ess3d.exe`](build)
* Switches the ES1869's 3-D effect (Spatializer) from the command line, shows the new setting for a moment and exits. Made for keys: a shortcut's *Shortcut key*, or keyboard software that runs a command.
* Reaches the card like essctl: through the register interface of the rebuilt `ES1869.VXD`, directly with the stock driver, or `/sim`.
* The new setting shows in a small box at the bottom of the screen for 1.5 seconds, on top of everything. It never takes the focus from the game or program in front.
  * Press the key again while the box is up and the box shows the new setting, no second box.
  * A click closes it.
* Commands run in order, so `ess3d on level 40` switches 3-D on, then sets the level.

```
ess3d [options] command [command...]

on                3-D on (also releases it from reset)
off               3-D off (bypassed)
toggle            off if it's on, on if it's off or held in reset
level N           level 0 to 63, or N% of 63
level +N, -N      up or down N steps, stopping at 0 and 63
up [N], down [N]  up or down N steps, 4 if N is left out
reset             reset the effect, keeping on/off and the level
hold              hold the effect in reset (on or reset releases it)
show              change nothing, show the setting

/q                no box and no message boxes (problems go to ESS3D.LOG,
                  or to the /log= file)
/t=1500           how long the box stays, in ms
/log=file         append each result to a file, problems too
/sim, /base=220, /cfg=800, /novxd   as for essctl

Examples:
ess3d toggle
ess3d up 8
ess3d on level 50%
```

* A bad command, or no ES1869, shows a message box (unless `/q`).
* With the stock driver, ess3d can't reach the card while a DOS program has the sound device. It says so, and nothing changes. The rebuilt `ES1869.VXD` has no such limit.
* Exit codes: 0 done, 1 the chip returned other values than were written, 2 failed, 3 bad command line.

### Putting ess3d on a key
* **A shortcut's Shortcut key**, in Windows 98:
  1. Copy `build\ess3d.exe` to `C:\ESSREG`.
  2. Right-click the desktop > *New* > *Shortcut*. Command line: `C:\ESSREG\ESS3D.EXE toggle`. Name it `3-D toggle`.
  3. Right-click the shortcut > *Properties*, click in *Shortcut key* and press a key. Windows makes it Ctrl+Alt+ that key.
  4. One shortcut per command, for example `ess3d up` and `ess3d down` on two more keys.
  * *Note: shortcut keys only work for shortcuts on the desktop or in the Start menu.*
* **Keyboard software** with keys that run a program: give it the same command line.
* The 3-D controls of the Windows mixer (ESS's driver has *Spatializer Enable* and *3D Effect*, see [docs/DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md#23-mixer-state)) write the same registers. The last write wins.
  * The mixer doesn't see ess3d's changes: its controls keep their own values.
  * ESS's driver writes its own 3-D setting back each time it's enabled: when Windows starts and when it resumes from standby.
* *Note: full-screen DirectX games draw over the box, so it may not show there. The setting still changes.*

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
* A sustain pedal that a song leaves down doesn't hold notes forever: a program change, or a GM, GS or XG reset, lets it up.
* General MIDI: modulation and channel pressure (vibrato), fine and coarse tuning, the bend range in cents, master volume, GM, GS and XG resets, and controller 121 as GM wants it. See [docs/ESFM_GM.md](docs/ESFM_GM.md).
* Running status in `midiOutLongMsg` buffers no longer mixes up notes.
* Its built-in patch bank is [`bnk_com_better_square_wave.bin`](esfm_patch_banks).
* It can play a patch bank straight from a file named in `SYSTEM.INI`. The file is read when a program opens the MIDI device, if its date or time changed. See [docs/ESFM_BANK.md](docs/ESFM_BANK.md#bank-file-buildesfmdrv).
* See [docs/ESFM_MIDI.md](docs/ESFM_MIDI.md) for the details.
* Everything else is ESS's code. `python3 tools/build_esfm.py --stock --verify` rebuilds the original byte for byte, and `esfmpat` and essctl's banks work the same.
* Install it from DOS, since Windows has the driver open: keep a copy of `C:\WINDOWS\SYSTEM\ESFM.DRV`, copy `build\ESFM.DRV` over it and restart Windows.
  * Step by step: [Installing and testing on Windows 98](#installing-and-testing-on-windows-98).

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

## Installing and testing on Windows 98
* `build\ESFM.DRV` is made from ESS's ES1869 AudioDrive driver 4.04.00.1319 in [`driver`](driver), which ESS made for Windows 95 and 98.
* These steps install it and essctl on a Windows 98 machine, and test the hanging-note fix, the bank file and General MIDI.
* *Note: the rebuilt `ES1869.VXD` isn't needed for this. See [docs/VXD_INTERNALS.md](docs/VXD_INTERNALS.md#installing-the-extended-driver) to install it too.*

### Before you start
* Check the ESS driver: in Explorer, right-click `C:\WINDOWS\SYSTEM\ESFM.DRV` > *Properties* > *Version*. It should say 4.04.00.1319.
  * With another version, install ESS's driver from the [`driver`](driver) folder first: *Device Manager* > the ES1869 > *Properties* > *Driver* > *Update Driver*.
* Copy these files to `C:\ESSREG` on the Windows 98 machine, from a floppy, CD or network share:
  * `build\ESFM.DRV`
  * `build\essctl.exe`
  * `build\GMCHECK.MID`
  * `esfm_patch_banks\bnk_com.bin` and `esfm_patch_banks\bnk_NT4.bin`
* *Note: keep folder names to 8 characters, since MS-DOS mode only sees short names.*

### Installing
`ESFM.DRV` is in use while Windows runs, so it's replaced from MS-DOS mode.

1. Start > *Shut Down* > *Restart in MS-DOS mode*.
2. Keep ESS's driver as `ESFM.ORG` and copy the fixed one over it:
   ```
   copy C:\WINDOWS\SYSTEM\ESFM.DRV C:\WINDOWS\SYSTEM\ESFM.ORG
   copy C:\ESSREG\ESFM.DRV C:\WINDOWS\SYSTEM\ESFM.DRV
   ```
3. Type `exit` to go back to Windows.
4. Control Panel > *Multimedia* > *MIDI*: *Single instrument* should be *ESFM Synthesis*, followed by the FM port.
5. Start `C:\ESSREG\essctl.exe` and open the *ESFM patch bank* page. It should say `Fixed driver`.

### Using a bank file
1. Make a folder `C:\BANKS` and copy `C:\ESSREG\bnk_NT4.bin` into it as `TEST.BIN`.
2. In essctl, ESFM > *Load patch bank* > `C:\BANKS\TEST.BIN`. It loads the bank now, and names the file in `SYSTEM.INI` for the driver:
   ```
   [ESFM.DRV]
   Bank=C:\BANKS\TEST.BIN
   ```
   * Start > *Run* > `sysedit` shows it. You can also add these two lines yourself.
* The driver reads the file when a program opens the MIDI device, if the file's date or time changed since it last read it. See [docs/ESFM_BANK.md](docs/ESFM_BANK.md#bank-file-buildesfmdrv).

### Testing
Keep essctl open on the *ESFM patch bank* page.

1. Double-click a MIDI file to play it in Media Player, for example `C:\WINDOWS\MEDIA\CANYON.MID`. The page says `Bank file: C:\BANKS\TEST.BIN, 8288 bytes`, and the instruments sound like the NT4 bank.
2. Close Media Player and play the file again. The page counts one more check and no new load.
3. Close Media Player. Copy `C:\ESSREG\bnk_com.bin` over `C:\BANKS\TEST.BIN`, then give it the current date in an MS-DOS prompt (Start > *Programs* > *MS-DOS Prompt*):
   ```
   cd C:\BANKS
   copy /b TEST.BIN +,,
   ```
   Play again: ESS's original sounds, and one more load.
   * *Note: a copied file keeps its date and time, and the driver only reads a file whose date or time changed.*
4. Change the file this way while music plays. Nothing changes until Media Player is closed and plays again.
5. Restart Windows and play: the bank file is read at the first play.
6. Delete `C:\BANKS\TEST.BIN` and play: the page says "cannot read", and the last bank keeps playing.
7. *Stress test* (button on the page): no voices left sounding.
8. Play the MIDI files and games that used to hang notes. If a note hangs, the page shows its voice, channel and state (*held by pedal*, for example). `essctl /dump` writes it to a file.
9. ESFM > *Restore original bank*: the `Bank=` line is gone from `SYSTEM.INI`, and the driver's own bank plays.
10. Play `C:\ESSREG\GMCHECK.MID`. It tries the General MIDI features one at a time: vibrato, pan, tuning, a wide bend and master volume. What to hear, and when, is in [docs/TESTING.md](docs/TESTING.md#g3-general-midi).

More checks are in [docs/TESTING.md](docs/TESTING.md) (G, G2 and G3).

### Going back
1. Start > *Shut Down* > *Restart in MS-DOS mode*.
2. `copy C:\WINDOWS\SYSTEM\ESFM.ORG C:\WINDOWS\SYSTEM\ESFM.DRV`
3. Type `exit`.

* ESS's driver doesn't read the `[ESFM.DRV]` section, so it can stay.
* If Windows doesn't start: hold Ctrl while the computer starts and pick *Command prompt only* in the Startup Menu. Do step 2, remove the `[ESFM.DRV]` section with `edit C:\WINDOWS\SYSTEM.INI`, and restart.

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
* Run [`build.bat`](build.bat) to build the executables: `essreg.exe`, `1869opl3.com`, `esfmpat.exe`, `essctl.exe` and `ess3d.exe`.
* Run `python3 tools/build_vxd.py` to build `build/ES1869.VXD`, and `python3 tools/build_esfm.py` to build `build/ESFM.DRV`.
* On Linux, `tools/ow2build.sh <open-watcom-v2 directory>` builds the programs with Open Watcom v2 into `out/ow2/`.
  * The programs in [`build`](build), other than `1869opl3.com`, were built this way. `build.bat` builds the same programs with Watcom C 11.0.

### Testing
* This code has been tested using a real ES1869 soundcard on I/O port 0x220. It probably won't work with any other ESS sound chips.
* For essctl, ess3d and the rebuilt drivers, [docs/TESTING.md](docs/TESTING.md) has a checklist for real hardware.
* `python3 tests/run_tests.py` runs the automated tests against a simulated ES1869.

## License
* GPL 3.0

## Thanks to
* The community over at [Vogons](https://www.vogons.org/viewtopic.php?f=62&p=1258077) who helped out!
* [Phil's Computer Lab](https://www.philscomputerlab.com/ess-audiodrive-es1868.html) for uploading the ES1869 datasheet!
