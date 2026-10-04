# essreg
ES1869 Register Utility

This repository contains utilities and drivers designed for the ESS
AudioDrive ES1869 sound card chipset.

## [`essctl.exe`](build)

essctl is a control panel for the ES1869 under Windows 95 and 98, written as
a 16-bit Windows program. It shows every register of the chip, 147 registers
with 299 settings, on these pages: *Output mixer*, *Master volume*,
*Record*, *3-D, mic, MONO, I2S*, *Serial / telegaming*, *DACs and ADC*,
*Power & GPO*, *Status & interrupts*, *Plug and Play*, *SB compatible
mixer*, *Raw registers* and *ESFM patch bank*.

The *DACs and ADC* page controls both audio channels in one window, in three
sections: Audio 1, the first DAC, which is the ADC when it records, then the
ADC's record level and offset, then Audio 2, the second DAC. The registers
of Audio 1 and the ADC are controller registers, which go through the DSP,
and essctl reads them like the others: when a page opens, when you press F5,
and every second while View > *Auto refresh* is on.

Each setting shows its value decoded, with sample rates in hertz, ADC
offsets in samples and choices by name, and the help line gives the
setting's register, bits and data sheet page. essctl writes every change and
reads it back, so a page always shows what the chip returned. Levels, signed
values and raw values have a text field with a slider to its right. The
slider moves in steps of one across the setting's range, and the text field
accepts a decimal number, or hex for raw values, and writes it when you
press Enter or leave the field.

The pages also include the registers that the data sheet leaves out but the
drivers use, marked *(undocumented)*: the 3-D limit and mono bits (mixer 50h
bits 0 and 1), the 3-D registers 54h, 56h, 58h and 5Ah, and bits 7:5 and 2
of controller register A8h. [docs/REGISTERS.md](docs/REGISTERS.md) and
[docs/SPATIALIZER.md](docs/SPATIALIZER.md) describe them. The *Plug and
Play* page shows the optional logical devices (MPU-401, CD-ROM, modem and
general-purpose) under the numbers that the card's PnP header gives them,
and marks the ones it leaves out as "not present".

Settings that can stop playback, hang the DSP or move the card to other
resources stay read-only until you turn on Options > *Expert mode*, which
lasts until essctl closes.

essctl reaches the card in one of three ways:
* Through the register interface of the rebuilt `ES1869.VXD` (see below).
  Nothing that Windows or a DOS game is doing is disturbed.
* Directly, with ESS's driver. essctl borrows the sound device for each
  access and gives it back at once, so a DOS game finds the device in use
  only while essctl is reading.
* From a simulated ES1869, with `/sim`, so that you can try essctl without
  the card.

In a text field, Esc discards what you typed. Anywhere else, it closes
essctl.

```
essctl [/load file] [/save file] [/dump file] [/ui] [/q]
       [/i2s=off|on] [/sim] [/base=220] [/cfg=800] [/novxd]

/load, /save  apply or save a profile without opening the window
/dump         write every readable register to a text file
/i2s=off      FM keeps the music DAC (/i2s=on: ESS's default)
/ui           open the window after /load, /save or /dump
/q            no message boxes (problems go to ESSCTL.LOG)
/sim          simulated ES1869
/base, /cfg   Audio_Base and Config_Base (hex) if detection fails
/novxd        direct I/O even with the register interface
```

### Profiles

File > *Save profile* stores the 56 ordinary settings in an INI file, and
File > *Load profile* restores them. ESS's driver resets the mixer whenever
Windows starts and then puts back only its own settings
([docs/DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md)). To get yours back at every
start of Windows, put `essctl /load C:\ESS\MY.INI` in the StartUp group.

A save reads the chip first, then writes `MY.$$$` and renames it over
`MY.INI`, so a crash, a full disk or a DOS game that has the sound device
leaves the old profile as it was. A setting that the chip didn't answer for
keeps its old value, and any sections of your own in the file stay.

### The music DAC

ESS's driver gives the music DAC to the I2S input (mixer 7Fh bit 0) whenever
no program has the MIDI synthesizer open. FM from a DOS box or another
program then doesn't play at all on cards whose MODE pin enables I2S, and on
other cards it plays at the IIS line's volume. The rebuilt `ES1869.VXD`
gives a DOS box's FM the music DAC by itself.

Options > *FM keeps the music DAC*, or `essctl /i2s=off`, stops this, so
that FM has the DAC at all times, and `/i2s=on` goes back to ESS's default.
The option sets `ESSWaveTableChip` in the driver's registry settings, which
ESS's driver reads when Windows starts
([docs/DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md)), and essctl also clears 7Fh
bit 0 at once. The Windows mixer then leaves out the IIS line. Until a
program first opens the MIDI synthesizer, the FM volume (36h) stays at the
chip's default, so let a profile loaded at startup set it. Only a card with
a device on its I2S input, such as Zoom Video or MPEG audio, needs ESS's
default.

### ESFM

ESFM > *Load patch bank* replaces the FM synthesizer's sounds while Windows
runs. The *ESFM patch bank* page shows the synthesizer's 18 voices live and
marks hanging notes, and its *Stress test* plays dense music to check for
them ([docs/ESFM_MIDI.md](docs/ESFM_MIDI.md)).

A bank larger than the one in memory waits until no program has the MIDI
device open, because ESS's driver keeps its bank locked in memory for use at
interrupt time while the device is open. The fixed `ESFM.DRV` takes the new
bank at the next open.

## [`ess3d.exe`](build)

ess3d switches the ES1869's 3-D effect, the Spatializer, from the command
line. It shows the new setting for a moment and exits, which makes it
suitable for keys: a shortcut's *Shortcut key*, or keyboard software that
runs a command. It reaches the card in the same ways as essctl, through the
register interface of the rebuilt `ES1869.VXD`, directly with ESS's driver,
or from a simulated chip with `/sim`.

The new setting appears in a small box at the bottom of the screen for 1.5
seconds, on top of everything else, and the box never takes the focus from
the game or program in front. If you press the key again while the box is
up, the same box shows the newer setting instead of a second box opening. A
click closes the box.

Commands run in order, so `ess3d on level 40` switches 3-D on and then sets
the level. `ess3d tray` puts an icon in the taskbar's tray with a panel of
every 3-D setting ([below](#the-tray-icon)).

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
limit on, off, toggle   the 3-D limit (mixer 50h bit 0, undocumented)
mono on, off, toggle    the 3-D mono bit (mixer 50h bit 1, undocumented)
reg XX YY         Spatializer register XX (54, 56, 58 or 5A) to YY, in hex
defaults          what ESS's driver sets when Windows starts: 3-D on,
                  level 63, limit off, 54h-5Ah 8Fh, 95h, 94h and 80h
show              change nothing, show the setting
tray              the tray icon, or the panel if it's already there
exit              close the tray icon

/q                no box and no message boxes (problems go to ESS3D.LOG,
                  or to the /log= file)
/t=1500           how long the box stays, in ms
/log=file         append each result to a file, problems too
/sim, /base=220, /cfg=800, /novxd   as for essctl

Examples:
ess3d toggle
ess3d up 8
ess3d on level 50%
ess3d limit toggle
ess3d reg 54 A0
```

A bad command shows a message box with the usage, unless you give `/q`.
Other problems, such as a missing ES1869, show in the box at the bottom of
the screen for 3 s and go to the log. No message box waits behind a
full-screen game, and pressing the key again doesn't pile them up. With
ESS's driver, ess3d can't reach the card while a DOS program has the sound
device, so it says so and changes nothing. The rebuilt `ES1869.VXD` has no
such limit.

The exit code is 0 when the command worked, 1 when the chip returned other
values than were written, 2 when it failed, and 3 for a bad command line.

The data sheet doesn't describe registers 54h-5Ah or the limit and mono
bits. ESS's drivers set them when Windows starts, but what they do isn't
known yet. [docs/SPATIALIZER.md](docs/SPATIALIZER.md) has what is known, and
how to find out more.

### Putting ess3d on a key

In Windows 98, a desktop shortcut can run ess3d from a key:
1. Copy `build\ess3d.exe` to `C:\ESSREG`.
2. Right-click the desktop and choose *New* > *Shortcut*. Enter the command
   line `C:\ESSREG\ESS3D.EXE toggle`, and name the shortcut `3-D toggle`.
3. Right-click the shortcut and choose *Properties*, then click in *Shortcut
   key* and press a key. Windows makes it Ctrl+Alt plus that key.
4. Make one shortcut for each command, for example `ess3d up` and
   `ess3d down` on two more keys.

Shortcut keys work only for shortcuts on the desktop or in the Start menu.
Keyboard software with keys that run a program takes the same command lines.

The 3-D controls of the Windows mixer write the same registers as ess3d, and
the last write wins. ESS's driver calls them *Spatializer Enable* and *3D
Effect* ([docs/DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md#23-mixer-state)). The
mixer doesn't see ess3d's changes, so its controls keep their own values,
and ESS's driver writes its own 3-D setting back each time it is enabled,
when Windows starts and when it resumes from standby. Full-screen DirectX
games draw over the box, so it may not show there, but the setting still
changes.

### The tray icon

`ess3d tray` puts an icon in the taskbar's tray, next to the clock. The icon
shows a green *3D* while the effect is heard and turns gray while the effect
is off or held in reset, and its tooltip shows the setting. A click on the
icon, with either button, opens a small panel with every 3-D setting:
* Check boxes switch 3-D on, release it from reset, and set the mono and
  limit bits. *Reset* resets the effect.
* The level and each Spatializer register have a text field with a slider to
  its right, which moves in steps of one. The text field takes the level in
  decimal and the registers in hex, and writes the value when you press
  Enter or leave the field.
* *Driver defaults* sets what ESS's driver sets when Windows starts.
* *Close tray icon* removes the icon, as `ess3d exit` does.

The panel closes when you click somewhere else, click the icon again or
press Esc. Every change is written and read back, so the panel shows what
the chip returned. The icon follows ess3d's commands and essctl's 3-D
settings, and with the rebuilt `ES1869.VXD` it also reads the setting every
3 seconds, so it follows the Windows mixer too. The panel has every
Spatializer register in essctl's list, so a register found later appears in
the panel, in `ess3d reg` and in essctl.

To have the icon at every start of Windows, put a shortcut with the command
line `C:\ESSREG\ESS3D.EXE tray` in the Start menu's *Programs* > *StartUp*
folder. A second `ess3d tray` opens the panel of the icon that is already
there, so the same command also works on a key. The tray icon needs Windows
95 or later, because ess3d is a 16-bit program and reaches the tray through
Windows 95's 32-bit shell.

## [`esfmrec.exe`](build)

esfmrec records the FM synthesizer digitally to a WAV file. It records the
samples that the chip makes, before any analog stage, at the music DAC's own
rate of 49,716 Hz in 16-bit stereo. Nothing is resampled, converted or
corrected, and the volume settings don't change the samples. It records the
FM of Windows programs, and of DOS games in a DOS box, while they go on
playing.

`esfmrec` starts recording to `FMREC001.WAV` next to esfmrec.exe, and to
`FMREC002.WAV` and so on after that. *Stop* saves the file, and *Record*
starts the next one. The window shows the time, the size, the rate at which
the samples come in and the peak levels. Enter and Esc don't stop a
recording, so click *Stop* or *Close*.

The chip does the recording itself. Mixer 7Fh bit 4 sends the music DAC's
samples to Audio 1's DMA in place of the ADC's (DS p.65), so esfmrec opens
ESS's wave input like any recording program and sets the bit while it
records. During the recording, the music DAC belongs to FM (7Fh bit 0 off),
and both bits go back to what they were at the end. esfmrec also turns off
ESS's DC offset correction (DCdrift) for the recording, and back on
afterwards ([docs/DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md)).

esfmrec is made for long sessions on Windows 98:
* Every 5 s, it writes the length so far into the WAV header and writes the
  file through to the disk. If Windows hangs or the power goes, only the
  last seconds are lost.
* A file never grows past 2 h 45 min, a little under the 2 GB that FAT16
  allows. The recording goes on in `NAME_2.WAV`, or in the next
  `FMRECnnn.WAV`, without a gap, and `/split=` makes the files shorter.
* A full disk stops the recording, and the file stays playable.
* Before it changes anything, esfmrec saves the old settings in
  `ESFMREC.RST`. If it is ended by force, with Ctrl+Alt+Del or by a crash,
  its next start puts the settings back and repairs the file's header. Until
  then, other programs would record the FM instead of the microphone.
* Every 2 s, it checks mixer 7Fh and sets it again if ESS's driver has
  changed it, for example when a MIDI program closes or the mixer is reset.
  The log says when this happens.

Audio 1 is also the channel that Sound Blaster digital sound plays through,
so a DOS game's Sound Blaster sound and a recording can't run at the same
time, although the game's FM music records. A DOS game takes the Sound
Blaster with its first access, usually when it looks for the card, and keeps
it until it ends. With the extended
[`ES1869.VXD`](#es1869vxd-with-a-register-interface-and-better-dos-boxes),
esfmrec takes it from the game, so a recording can start at any time. The
game runs on with a silent Sound Blaster, and gets the real one back after
the recording. With ESS's VxD, esfmrec can't start until the game ends.
While esfmrec records, a game that starts gets no digital sound, and Windows
may say that the device is in use. Only one program can record at a time.

```
esfmrec [options] [file]

file       the WAV file, FMREC001.WAV and up if left out
/t=N       stop after N seconds, then exit
/split=N   go on in a new file every N seconds
/raw       the samples alone, without a WAV header
/min       start minimized
/q         no message boxes, problems only go to the log
/log=file  the log, ESFMREC.LOG next to esfmrec.exe if left out
/sim       simulated ES1869, with a test tone for the FM
/base, /cfg, /novxd  as for essctl
```

The log has a line for each file that gives its length and, if the samples
came in slower than 49,716 Hz, says that ESS's driver lost some. Problems go
to the log too.

esfmrec is a 16-bit program and takes 8.3 names, so for a folder with a long
name, use its short name, such as `C:\MYDOCU~1\FM.WAV`. With essctl's
Options > *FM keeps the music DAC*, ESS's driver never gives the music DAC
to I2S, not even between recordings.

## [`ES1869.VXD`](build) with a register interface and better DOS boxes

This is the Windows 95 driver of the ES1869, rebuilt from the source in
[`src/vxd`](src/vxd), with a register interface for programs added
([docs/VXD_API.md](docs/VXD_API.md)). It gives DOS boxes the sound card as a
DOS game expects it
([docs/VXD_INTERNALS.md](docs/VXD_INTERNALS.md#dos-boxes)):
* FM detection always succeeds, for AdLib, OPL3 and ESFM, whoever has the FM
  synthesizer. A DOS box that can't have it, because a Windows MIDI program
  or another DOS box does, gets a virtual one, so the game runs and plays
  silently. When the synthesizer is free, the game's next FM access takes
  it, with everything the game has written so far.
* A Windows program that only touched an FM port no longer keeps FM from DOS
  boxes. A Windows MIDI program still does, until it closes.
* FM from a DOS box is heard, because the game gets the music DAC, and an FM
  volume if the IIS volume was 0. `1869opl3` isn't needed.
* A game whose child program ends keeps its FM instruments, where ESS's
  driver resets the synthesizer under it.
* After a DOS game, all of Windows' mixer settings come back: the 3-D
  effect, the record source and levels, the wave volume and the rest. ESS's
  driver puts back only 11 levels.
* FM that a DOS game left behind is reset the next time Windows plays a
  sound or a level changes, so no note keeps sounding. After a DOS game,
  moving a slider in the tray's volume control resets the card.
* A DOS game's FM music keeps its tempo. Windows hands a DOS box its timer
  ticks in bursts when Windows itself is busy, and the music came in the
  same bursts. The driver now plays a real-mode game's FM on a clock that
  follows its ticks, 30 ms late but evenly, in the game's order. A protected
  mode game, such as one with DOS/4GW, plays as before.
* A recording of the FM, by esfmrec or by the *ESS AudioDrive FM Digital*
  device, can start while a DOS game has the Sound Blaster. The game goes on
  with a virtual Sound Blaster, which answers as the chip does and keeps the
  timing of the game's sound and its interrupts, but plays nothing. The
  game's FM music goes on to the chip, and so into the recording.

The Sound Blaster part and the MPU-401 still belong to one program at a
time, as with ESS's driver. A DOS game that starts while Windows plays a
sound finds no Sound Blaster, and it finds one once the sound has ended.

DirectSound and the rest of the driver's own Audio 2 playback play without
4x oversampling and with the filter bypassed, as
[`ES1869.DRV`](#es1869drv-with-four-wave-devices-and-unfiltered-playback)
below does for Windows sounds. The Audio 1 CODEC's filter stays bypassed as
well, for DOS programs that play or record through it. In every other way
the driver works like ESS's, and `python3 tools/build_vxd.py --stock
--verify` rebuilds the original byte for byte. Each change can be turned off
with its own key under `[ES1869.VXD]` in `SYSTEM.INI`, which the driver
reads when Windows starts
([docs/DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md#6-the-rebuilt-drivers-systemini-settings)).

[`essinst.exe`](#essinstexe) installs it and restarts Windows.
[docs/VXD_INTERNALS.md](docs/VXD_INTERNALS.md#installing-the-extended-driver)
gives the steps by hand, and how to go back if something goes wrong. The
rebuilt driver no longer carries ESS's DirectX certification mark.

## [`ESFM.DRV`](build) without hanging notes

This is the Windows 95 MIDI driver of the ES1869's FM synthesizer, rebuilt
from the source in [`src/esfm`](src/esfm). ESS's driver drops MIDI messages
that arrive while it is busy, so notes hang when the music gets busy. This
one queues the messages instead, and it fixes more:
* It silences every voice when a program closes the device with the sustain
  pedal down.
* A sustain pedal that a song leaves down no longer holds notes forever,
  because a program change, or a GM, GS or XG reset, lets it up.
* It supports General MIDI: modulation and channel pressure (vibrato), fine
  and coarse tuning, the bend range in cents, master volume, GM, GS and XG
  resets, and controller 121 as GM defines it
  ([docs/ESFM_GM.md](docs/ESFM_GM.md)).
* Running status in `midiOutLongMsg` buffers no longer mixes up notes. It
  carries on across buffers, and a clock or active sensing byte no longer
  breaks it.
* A program always gets its `midiOutLongMsg` buffers back, including when
  the queue is full, when the program closes the device while a buffer
  waits, and when the device is suspended.
* Its built-in patch bank is
  [`bnk_com_better_square_wave.bin`](esfm_patch_banks), and it carries ESS's
  bank too.
* It can play a patch bank straight from a file named in `SYSTEM.INI`. It
  reads the file when a program opens the MIDI device, if the file's date or
  time has changed
  ([docs/ESFM_BANK.md](docs/ESFM_BANK.md#bank-file-buildesfmdrv)).

[docs/ESFM_MIDI.md](docs/ESFM_MIDI.md) has the details. Each change can be
turned off with its own key under `[ESFM.DRV]` in `SYSTEM.INI`, which the
driver reads when Windows starts. With all of them at 0, it plays as ESS's
driver does, and `BetterSquareWave=0` plays ESS's bank
([docs/DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md#63-esfmdrv)). Everything else
is ESS's code: `python3 tools/build_esfm.py --stock --verify` rebuilds the
original byte for byte, and `esfmpat` and essctl's banks work as before.

[`essinst.exe`](#essinstexe) installs it and restarts Windows. By hand,
install it from DOS, because Windows has the driver open: keep a copy of
`C:\WINDOWS\SYSTEM\ESFM.DRV` as `ESFM.ORG`, copy `build\ESFM.DRV` over it
and restart Windows. [Installing and testing on Windows
98](#installing-and-testing-on-windows-98) goes through it step by step.

## [`ES1869.DRV`](build) with four wave devices and unfiltered playback

This is ESS's wave, mixer and aux driver 4.04.00.1319, with three changes.

The first change is a second wave device, "ESS AudioDrive Audio 1", which
plays through the chip's other DAC, the one that ESS's driver only records
with. Two programs can then play at once, each on its own DAC, and ESS's
device is named after its DAC too: "ESS AudioDrive Audio 2".
* A program that opens the first device while another program plays there
  gets the second DAC, instead of "the device is in use".
* Audio 1 either records or plays, so while something plays on it, Sound
  Recorder can't record, and the other way round.
* The second device also plays 4-channel files, with channels 1-2 on one DAC
  and 3-4 on the other, both from one clock. This is dual playback, and
  [`tools/dualwav.py`](tools/dualwav.py) makes such files to hear the two
  DACs together: the same signal on both, one against the other (a null
  test), and more ([docs/AUDIO1.md](docs/AUDIO1.md#dual-playback)).
* `Audio1Device=0` and `SharedWaveOut=0` under `[ES1869.DRV]` in
  `SYSTEM.INI` give back ESS's single device.

[docs/AUDIO1.md](docs/AUDIO1.md) explains how the second device works, and
what ESS's driver and the data sheet say about Audio 1.

The second change is a second recording device, "ESS AudioDrive FM Digital",
which records the FM synthesizer's own samples at 49,716 Hz, 16-bit stereo,
as esfmrec does. A program that lets you type in the sample rate can record
from it. The chip has a single ADC, on Audio 1, so the two recording devices
take turns, and neither can record while the Audio 1 device plays.
`FMRecordDevice=0` under `[ES1869.DRV]` removes it
([docs/AUDIO1.md](docs/AUDIO1.md#the-fm-recording-device)).

The third change concerns the Audio 2 DAC, which plays Windows' wave output.
It now plays the samples as they are, without 4x oversampling and with the
filter bypassed, at every sample rate. ESS's driver turns the 4x
oversampling on at every playback, and its interpolation most likely dulls
the treble.
[docs/AUDIO_PIPELINE.md](docs/AUDIO_PIPELINE.md#the-audio-2-dac-oversampling-and-the-filter)
explains why, and what each setting does. This is how a non-oversampling
(NOS) DAC plays, so at low sample rates, such as the 11 and 22 kHz of many
Windows sounds, its images are audible as treble above the sound's own. The
Audio 1 CODEC's filter, which smooths the Audio 1 DAC and filters what the
ADC records, stays bypassed too, at all times
([docs/AUDIO1.md](docs/AUDIO1.md#the-filter-of-the-audio-1-codec)).

`SYSTEM.INI` chooses the mode, and the driver reads it when Windows starts.
`Audio2Oversampling=1`, `Audio2Filter=1` and `Audio1Filter=1` under
`[ES1869.DRV]` give ESS's mode back, for DirectSound too
([docs/DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md#62-es1869drv)). DirectSound
plays through `ES1869.VXD`. The [extended
one](#es1869vxd-with-a-register-interface-and-better-dos-boxes) plays it in
the same mode, and ESS's turns the 4x oversampling on. To try each setting
live, use *Audio 2 4x oversampling* and *Audio 2 filter bypass* on essctl's
*DACs and ADC* page. The driver puts its own setting back the next time a
program opens the wave device and plays, and when Windows starts or resumes.

The driver is rebuilt from the source in [`src/es1869`](src/es1869), and
`python3 tools/build_es1869drv.py --stock --verify` rebuilds the original
byte for byte. The changes are fourteen instructions and table entries of
ESS's code, each kept at the same length, and code added after ESS's, which
stays at its addresses. The driver replaces ESS's 4.04.00.1319 only, so
check the version as for [`ESFM.DRV`](#before-you-start).

[`essinst.exe`](#essinstexe) installs it and restarts Windows. By hand,
install it from MS-DOS mode, because Windows has the driver open (Start >
*Shut Down* > *Restart in MS-DOS mode*), with the file copied to
`C:\ESSREG`:

```
copy C:\WINDOWS\SYSTEM\ES1869.DRV C:\WINDOWS\SYSTEM\ES1869.ORG
copy C:\ESSREG\ES1869.DRV C:\WINDOWS\SYSTEM\ES1869.DRV
```

Type `exit` to go back to Windows. To go back to ESS's driver, copy
`ES1869.ORG` over `ES1869.DRV` in the same way.

## [`essreg.exe`](build)
* Utility to control otherwise unsupported registers for the ES1869 audio
  chip.
* This program can be executed in native DOS or Windows DOS box environment.
  * In a DOS box, the chip answers only while no Windows program has the
    sound device. essreg checks the chip's ID first and says so, instead of
    showing FFh as settings.
* Sound card with ES1869 chip is assumed to be on I/O port 220h.
* A value out of range is refused, and essreg shows the range. The exit code
  is 1 when something didn't work, such as a bad value or a dump that
  couldn't be written, for use in batch files.
* Check out `fmd=1`, which makes the chip record the FM digitally at its own
  rate, 49,716 Hz! In Windows, `esfmrec` (above) records it to a file.
  * [Examples!](digital_recording)

```
ES1869 Register Utility (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>

|Option--------------|Description----------------------------------------
| a                  | Get all values
| r=[path]           | Dump ES1869 Registers        default "essreg.txt"
| c                  | Calibrate Op Amp
| 3=[0,63; 0%,100%]  | 3D Amount                    Get / Set
| 3l=[1,0]           | 3D Limit (undocumented)      Enable / Disable
| ol=[-1024,960]     | ADC Offset Samples Left      Get / Set
| or=[-1024,960]     | ADC Offset Samples Right     Get / Set
| a1s                | Audio 1 Sample Rate          Get
| a1f                | Audio 1 Filter Clock         Get
| a2s                | Audio 2 Sample Rate          Get
| a2f                | Audio 2 Filter Clock         Get
| pa=[1,0]           | Analog Stays On              Enable / Disable
| pd                 | Digital Power Down           Get
| m=[1,0]            | Mono-In direct to output     Enable / Disable
| ml=[0,15; 0%,100%] | Mono-In Mixer Volume         Get / Set 
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
* This program reads the FM volume mixer register on the sound card and then
  runs the application.
* The sound card's I/O port comes from the `BLASTER` variable (`A220`), or
  is 220h without it.
* The IIS mixer control must not be muted in the Windows volume mixer.
  However, the volume can be set to 0.
* The exit code is the command's, or 1 if the command couldn't run.
* With the rebuilt `ES1869.VXD`, it isn't needed, because the driver gives a
  DOS box's FM the music DAC and a volume itself.

```
Example:

c:\>1869opl3.com "c:\path\to\game.exe"
```

## [`esfmpat.exe`](build)
* Utility to write custom patch banks to the Windows ESFM VxD driver.
* Patch banks larger than the existing bank now work, up to 32752 bytes.
  They are appended to the driver file, and the driver is told the new size.
* The original driver is kept as `ESFM.BAK`.
* Nothing is changed in place. The backup and the patched driver are first
  written to `ESFM.$$$`, checked and then renamed, so a disk error, Ctrl+C
  or a power cut leaves the old files as they were, and a backup that isn't
  a whole driver is made again.
* Run it from MS-DOS mode (Start > *Shut Down* > *Restart in MS-DOS mode*).
  In a Windows DOS box, it won't patch the `ESFM.DRV` that Windows has
  loaded, because Windows reads parts of the driver from the file again
  later and would mix old and new.
* It accepts raw banks and RIFF "Ptch" bank files, and it checks the bank
  and the driver before it changes anything.
  [docs/ESFM_BANK.md](docs/ESFM_BANK.md) describes the bank format.
* See the [ESFM patch banks](esfm_patch_banks) for the Windows 98, Windows
  NT4, and other custom patches.
* See the [recordings](digital_recording) for ESFM patch comparisons.
* To try a bank without restarting Windows, load it with essctl (ESFM >
  *Load patch bank*).

```
ESFM Patch Utility (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>

This utility writes your custom patch set to an existing ESFM.DRV file.
Compatible with the VxD Windows driver only.  The original driver is kept
as ESFM.BAK next to it.

Usage: esfmpat "c:\path\to\esfm.drv" "c:\path\to\patch.bin"
       esfmpat "c:\path\to\esfm.drv"   (show its patch bank)
```

## [`essinst.exe`](build)

essinst installs the rebuilt drivers and restarts Windows. Copy it to the
Windows 95 or 98 machine together with `build\ES1869.VXD`,
`build\ES1869.DRV` and `build\ESFM.DRV`, with the four files in one folder,
and run it. It lists what it will do and asks once. When you press OK, it
restarts Windows, and the rebuilt drivers are in place when Windows is back.

Windows has the drivers open while it runs, so essinst doesn't replace them
itself. It copies each new driver next to the old one in
`C:\WINDOWS\SYSTEM`, as `ES1869VX.NEW`, `ES1869.NEW` and `ESFM.NEW`, and
lists them in the `[rename]` section of `C:\WINDOWS\WININIT.INI`. Windows
moves them into place while it restarts, before it loads any driver, which
is how Windows' own setup replaces files that are in use. Nothing is listed
until every file is written and checked, so an error changes nothing.

essinst first checks the installed drivers, and it changes nothing when one
of them is neither ESS's driver 4.04.00.1319, which the rebuilt drivers are
made from, nor an earlier rebuilt driver. It keeps ESS's drivers as
`ES1869VX.ORG`, `ES1869.ORG` and `ESFM.ORG` in the same folder. A driver
that esfmpat gave another bank counts as ESS's, and it is kept with its
bank. essinst installs only the drivers that are in its own folder, so to
install one of them alone, put essinst and that driver in a folder of their
own.

The rebuilt `ES1869.DRV` names the wave devices after their DACs, and
Windows keeps its preferred playback and recording devices by name. essinst
gives them the new names in the registry, for you and for new users, so that
Windows still finds them, and `/restore` gives them ESS's names back. If an
earlier essinst installed the drivers, run the new one once: it puts the new
`ES1869.DRV` in place and renames the preferred devices. When only the names
are left to do, it does that without a restart.

These switches change what it does:
* `/restore` puts ESS's drivers back from the `.ORG` copies in the same way.
* `/norestart` leaves the restart to you. The drivers go in place when
  Windows next restarts.
* `/y` asks nothing and shows no message, for a batch file.

Each run writes what essinst found and did to `C:\WINDOWS\ESSINST.LOG`. If
Windows doesn't start properly with a rebuilt driver, start it in Safe Mode,
which loads no sound driver, and run `essinst /restore`. To get there, hold
Ctrl while the computer starts (on Windows 95, press F8 at "Starting Windows
95") and choose *Safe mode*. [Going back](#going-back) also gives the
commands for the command prompt.

## Installing and testing on Windows 98

The rebuilt drivers are made from ESS's ES1869 AudioDrive driver
4.04.00.1319 in [`driver`](driver), which ESS made for Windows 95 and 98.
These steps install them and essctl on a Windows 98 machine with essinst,
and test the hanging-note fix of `ESFM.DRV`, its bank file and General MIDI.

### Before you start

Check the version of ESS's driver: in Explorer, right-click
`C:\WINDOWS\SYSTEM\ESFM.DRV` and choose *Properties* > *Version*. It should
say 4.04.00.1319. With another version, first install ESS's driver from the
[`driver`](driver) folder, through *Device Manager* > the ES1869 >
*Properties* > *Driver* > *Update Driver*. essinst checks the version too,
and changes nothing if it is another one.

Copy these files to `C:\ESSREG` on the Windows 98 machine, from a floppy, a
CD or a network share:
* `build\essinst.exe`, `build\ES1869.VXD`, `build\ES1869.DRV` and
  `build\ESFM.DRV`
* `build\essctl.exe`
* `build\GMCHECK.MID`
* `esfm_patch_banks\bnk_com.bin` and `esfm_patch_banks\bnk_NT4.bin`

Keep folder names to 8 characters, because MS-DOS mode only sees short
names.

### Installing

1. Run `C:\ESSREG\essinst.exe`. It lists the three drivers as ESS's, each
   kept as an `.ORG` file. Press OK, and Windows restarts.
2. Open Control Panel > *Multimedia* > *MIDI*. *Single instrument* should be
   *ESFM Synthesis*, followed by the FM port.
3. Start `C:\ESSREG\essctl.exe` and open the *ESFM patch bank* page. It
   should say `Fixed driver`.

### Using a bank file

1. Make a folder `C:\BANKS`, and copy `C:\ESSREG\bnk_NT4.bin` into it as
   `TEST.BIN`.
2. In essctl, choose ESFM > *Load patch bank* and pick `C:\BANKS\TEST.BIN`.
   essctl loads the bank at once, and names the file in `SYSTEM.INI` for the
   driver:
   ```
   [ESFM.DRV]
   Bank=C:\BANKS\TEST.BIN
   ```
   Start > *Run* > `sysedit` shows these lines, and you can also add them
   yourself.

The driver reads the file when a program opens the MIDI device, if the
file's date or time has changed since it last read it
([docs/ESFM_BANK.md](docs/ESFM_BANK.md#bank-file-buildesfmdrv)).

### Testing

Keep essctl open on the *ESFM patch bank* page.

1. Double-click a MIDI file to play it in Media Player, for example
   `C:\WINDOWS\MEDIA\CANYON.MID`. The page says `Bank file:
   C:\BANKS\TEST.BIN, 8288 bytes`, and the instruments sound like the NT4
   bank.
2. Close Media Player and play the file again. The page counts one more
   check and no new load.
3. Close Media Player. Copy `C:\ESSREG\bnk_com.bin` over
   `C:\BANKS\TEST.BIN`, then give the file the current date in an MS-DOS
   prompt (Start > *Programs* > *MS-DOS Prompt*):
   ```
   cd C:\BANKS
   copy /b TEST.BIN +,,
   ```
   Play again. ESS's original sounds play, and the page counts one more
   load. A copied file keeps its date and time, and the driver reads only a
   file whose date or time has changed, which is why the second command is
   needed.
4. Change the file in the same way while music plays. Nothing changes until
   Media Player is closed and plays again.
5. Restart Windows and play. The driver reads the bank file at the first
   play.
6. Delete `C:\BANKS\TEST.BIN` and play. The page says "cannot read", and the
   last bank keeps playing.
7. Click *Stress test* on the page. No voices are left sounding.
8. Play the MIDI files and games that used to hang notes. If a note hangs,
   the page shows its voice, channel and state (*held by pedal*, for
   example), and `essctl /dump` writes it to a file.
9. Choose ESFM > *Restore original bank*. The `Bank=` line is gone from
   `SYSTEM.INI`, and the driver's own bank plays.
10. Play `C:\ESSREG\GMCHECK.MID`, which tries the General MIDI features one
    at a time: vibrato, pan, tuning, a wide bend and master volume.
    [docs/TESTING.md](docs/TESTING.md#g3-general-midi) says what to hear,
    and when.

[docs/TESTING.md](docs/TESTING.md) has more checks, in sections G, G2 and
G3.

### Going back

Run `C:\ESSREG\essinst.exe /restore` and press OK. Windows restarts with
ESS's drivers. ESS's drivers don't read the `[ES1869.VXD]`, `[ES1869.DRV]`
and `[ESFM.DRV]` sections, so the sections can stay.

If Windows doesn't start, hold Ctrl while the computer starts and choose
*Command prompt only* in the Startup Menu. Put ESS's drivers back, remove
the three sections with `edit C:\WINDOWS\SYSTEM.INI`, and restart:

```
copy C:\WINDOWS\SYSTEM\ES1869VX.ORG C:\WINDOWS\SYSTEM\ES1869.VXD
copy C:\WINDOWS\SYSTEM\ES1869.ORG C:\WINDOWS\SYSTEM\ES1869.DRV
copy C:\WINDOWS\SYSTEM\ESFM.ORG C:\WINDOWS\SYSTEM\ESFM.DRV
```

## Documentation
* [docs/REGISTERS.md](docs/REGISTERS.md): every register and setting of the
  ES1869, generated from the catalog [`src/esscat.tbl`](src/esscat.tbl).
* [docs/VXD_API.md](docs/VXD_API.md): the programming interface of
  `ES1869.VXD`, including the added register interface.
* [docs/VXD_INTERNALS.md](docs/VXD_INTERNALS.md): how the driver works, and
  how to rebuild and install it.
* [docs/DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md): the registry settings of
  ESS's drivers, the registers the Windows driver sets, and the `SYSTEM.INI`
  settings of the rebuilt drivers.
* [docs/SPATIALIZER.md](docs/SPATIALIZER.md): the 3-D effect, where it comes
  from (ESS's ES938), and its undocumented registers.
* [docs/ESFM_BANK.md](docs/ESFM_BANK.md): FM patch banks and `ESFM.DRV`.
* [docs/ESFM_MIDI.md](docs/ESFM_MIDI.md): why ESFM notes hang, the MIDI
  driver stack, and the fixed `ESFM.DRV`.
* [docs/ESFM_GM.md](docs/ESFM_GM.md): what General MIDI asks for, and what
  the fixed `ESFM.DRV` adds.
* [docs/AUDIO_PIPELINE.md](docs/AUDIO_PIPELINE.md): where skipped samples
  come from, the Audio 2 DAC's oversampling and filter, and what to try on
  the card.
* [docs/AUDIO1.md](docs/AUDIO1.md): the chip's Audio 1 channel, telegaming,
  and the second wave device of `ES1869.DRV`.
* [docs/RE_NOTES.md](docs/RE_NOTES.md): how the drivers were
  reverse-engineered.
* [docs/TESTING.md](docs/TESTING.md): the automated tests, and a checklist
  for real hardware.
* [docs/STYLE.md](docs/STYLE.md): how the code comments and the
  documentation are written.
* [docs/datasheet](docs/datasheet): the ES1869, ES938 and ES1868 data
  sheets.

## Building

### Prerequisites
* MSDOS like build environment (MS-DOS, Windows 9x, DOS-BOX, etc.)
* [Watcom C 11.0](https://winworldpc.com/product/watcom-c-c/110b)
  * Install the DOS and 16-bit Windows targets.
* Python 3 and NASM for the drivers, on any operating system

### Building
* Clone this repo and copy it your build environment.
* Run [`build.bat`](build.bat) to build the executables: `essreg.exe`,
  `1869opl3.com`, `esfmpat.exe`, `essctl.exe`, `ess3d.exe`, `esfmrec.exe`
  and `essinst.exe`.
* Run `python3 tools/build_vxd.py` to build `build/ES1869.VXD`,
  `python3 tools/build_esfm.py` to build `build/ESFM.DRV`, and
  `python3 tools/build_es1869drv.py` to build `build/ES1869.DRV`.
* On Linux, `tools/ow2build.sh <open-watcom-v2 directory>` builds the
  programs with Open Watcom v2 into `out/ow2/`. The programs in
  [`build`](build), other than `1869opl3.com`, were built this way, and
  `build.bat` builds the same programs with Watcom C 11.0.

### Testing
* This code has been tested using a real ES1869 soundcard on I/O port 0x220.
  It probably won't work with any other ESS sound chips.
* For essctl, ess3d, esfmrec, essinst and the rebuilt drivers,
  [docs/TESTING.md](docs/TESTING.md) has a checklist for real hardware.
* `python3 tests/run_tests.py` runs the automated tests against a simulated
  ES1869.

## License
* GPL 3.0

## Thanks to
* The community over at
  [Vogons](https://www.vogons.org/viewtopic.php?f=62&p=1258077) who helped
  out!
* [Phil's Computer
  Lab](https://www.philscomputerlab.com/ess-audiodrive-es1868.html) for
  uploading the ES1869 datasheet!
