# The 3-D effect (Spatializer)

This page describes what the ES1869's 3-D effect is, where its registers come from, and what is still unknown about the undocumented ones.

## In short

The ES1869 has the processor of ESS's ES938 chip built in as its 3-D effect, which is Spatializer 3-D stereo from Desper Products. The data sheet documents three controls. Mixer register 50h bit 3 turns the effect on, 50h bit 2 releases it from reset, and 52h sets its level.

Six more are undocumented: 50h bits 1 and 0, and the registers 54h, 56h, 58h and 5Ah. ESS's Windows 95 and NT drivers and Linux write 8Fh, 95h, 94h and 80h to 54h-5Ah. ESS's Windows 95 and NT drivers also set 50h bit 0 from a setting called *3D Limit*, and NetBSD calls 50h bit 1 *MONO*. No document found says what any of them does, but the ES938's data sheet and Desper's patent point to where they may come from: an effect *limit* and *mode*, a noise gate, and an automatic level control with attack and release times. essctl, `ess3d` and the tray panel can set every one of them, so you can try them on the card.

## Where it comes from

The ES1869 data sheet describes the effect as follows ([DS p.25](datasheet)): "The ES1869 incorporates an embedded Spatializer® VBX™ stereo audio processor provided by Desper Products, Inc., a subsidiary of Spatializer Audio Laboratories, Inc. It is positioned between the output of the playback mixer and the master volume controls and it produces a wider perceived stereo effect." The same page goes on: "The 3-D effect is enabled by register 50h bit 3. The amount of effect is controlled by directly programming 3-D Level register 52h." In register 50h, bit 3 enables the effect, bit 2 releases it from reset, and bits 7:4, 1 and 0 are "Reserved. Always write 0." (DS p.62). Pin 42, CAP3D, is a "Bypass capacitor to analog ground for 3-D effects" (DS p.6), and record source 7 of mixer register 1Ch records the output of the 3-D effect, before the master volume (DS p.59).

The ES1868 has no 3-D effect, and its data sheet (72 pages) has no mixer registers between 4Eh and 64h.

The ES938 is ESS's separate "3-D Audio Effects Processor" for cards of the ES1868 era, and it also has bass and treble controls (ES938 data sheet SAM0054-090497, 1997). The host programs it with MIDI System Exclusive messages sent out of the MPU-401 port, in the form `F0 00 00 7B 7F <register> <data> [<register> <data> ...] F7`. 00 00 7Bh is ESS's MIDI ID, and the command 7Fh writes registers while 7Eh reads them.

ESS's drivers show that the ES1869 and ES1879 have the ES938's 3-D processor built in. The Windows 95 setting that turns on the ES1869's own 3-D effect is called *Enable ES938* ([DRIVER_CONFIG.md](DRIVER_CONFIG.md)), and ESS's NT 4.0 driver marks `3D-Limit` and `SpatializerEnable` as "1869 1879" in its CONFIG.INI. Linux, too, gives the Spatializer to the ES1869 and ES1879 only, and its Solo-1 driver (ES1938, ES1946, ES1969) writes the same four registers.

## The ES938's registers

| Register | Name in the data sheet | Bits | Reset |
|---|---|---|---|
| 0 | Miscellaneous Control | bit 6 is 1; bits 5:4 set the tone input level to 0, -3, -6 or -9 dB; bits 3:2 are the noise gate, which "must be NG0=0, NG1=1"; bits 1:0 "must be" 01b | 49h |
| 1 | Tone Control | bits 6:4 treble and 2:0 bass, -9 to +12 dB in 3 dB steps | 33h |
| 2, 3, 4 | Reserved (Write 0) | | |
| 5 | Spatializer Effect Limit/Mode | bits 6:1 are SPC5-SPC0, the space control, and bit 0 is R; "00h: no effect, 47h: minimum effect level, 5Bh: medium effect level, 67h: maximum effect level" | |
| 6 | Spatializer Processor Enable | bits 1:0, 02h for disabled and 03h for enabled | 00h |
| 7 | Power Control | bit 2 enables pull-ups when powered down; bit 1 is the output enable, with 0 for bypass; bit 0 powers down | 0Eh |

The ES938's SPEN pin "enables spatialization effect at current limit level (limit level is set to maximum by reset)", so the ES938 calls the amount of effect its *limit*. Its pins INT1 and INT2 take 4.7 µF capacitors, and FO and FI connect through an external network of resistors and capacitors. The ES1869 has a single pin for the 3-D effect, CAP3D.

## The same controls on the ES1869

ESS's NT 4.0 driver (AUDDRIVE.SYS, 1997) drives both chips from the same code. For each control, it either sends the ES938 a SysEx message or writes the ES1869's register:

| Control | ES938 | ES1869 |
|---|---|---|
| 3-D on, off | register 6 = 03h, 02h | 50h = 0Ch, 04h, keeping bit 0 |
| 3-D level (h, the slider's high byte) | register 5 = (h OR 2) / 2, so SPC5-SPC0 come from h and R is always 1 | 52h = h / 4 |
| At start, with SpatializerEnable | registers 0 = 49h; 2, 3 and 4 = 00h; 7 = 0Eh | 54h = 8Fh, 56h = 95h, 58h = 94h, 5Ah = 80h |
| At start | | 50h = 3D-Limit (1 or 0), so 3-D is off and held in reset |
| Treble, bass | register 1 | none, because the ES1869 has no tone control |

The ES1869's 52h bits 5:0 are therefore the ES938's SPC5-SPC0, and 50h bits 3:2 do the job of the ES938's register 6 bits 1:0.

At start, the driver gives the ES938's registers 0, 2, 3, 4 and 7 their reset values, which are fixed settings such as the noise gate bits that "must be" set one way. It writes the ES1869's 54h-5Ah at the same place in the same code, so these registers are most likely fixed settings of the same kind for the built-in processor.

## What each driver writes

| Driver | 50h | 52h | 54h-5Ah |
|---|---|---|---|
| ES1869.DRV 4.04, Windows 95 (3:48AC), only with *Enable ES938* | 00h, then 0Ch, then bit 0 from *3D Limit* | 3Fh, then the mixer's 3-D level | 8Fh, 95h, 94h, 80h |
| AUDDRIVE.SYS, NT 4.0 | 3D-Limit alone, then 0Ch or 04h, keeping bit 0 | the 3-D level | 8Fh, 95h, 94h, 80h, with SpatializerEnable |
| ESS.SYS, Windows 98 WDM (`InitSpatializer`) | 0Ch | the 3-D level | never |
| Linux es18xx.c and es1938.c | on: 08h, then 0Ch; off: 00h, then 04h (a reset pulse each time) | the 3-D level | 8Fh, 95h, 94h, 80h, to "Set spatializer parameters to recommended values" |
| NetBSD ess.c | names 04h RESET, 08h ENABLE and 02h MONO, and clears MONO with ENABLE | the 3-D level | never |

ESS's drivers and Linux write the same four values, and Linux calls them "recommended values", which suggests that they come from ESS's own programming notes. Under ESS's Windows 98 driver, 54h-5Ah keep whatever values the chip powers up with.

## The Spatializer processor

Desper Products' patent US 5,412,731 (1995) describes the processor that the ES938 and the ES1869 license. It adds a processed difference signal, left minus right, back to each channel. The difference signal first goes through a high-pass filter of "about 18 decibels per octave and a cutoff frequency of about 300 hertz".

The patent names "Two operating modes: 1-Space) In which the ratio is constant... 2-AutoSpace) In which the ratio is electrically varied... the enhancement is held at a constant average regardless of program material." In AutoSpace, a voltage-controlled amplifier sets the level of the difference signal. The rectified sum and difference signals drive it through "filters 132 and 134 which have adjustable rise and fall ballistics".

A comparator turns the enhancement off for mono material, "to avoid excessive increase in stereo noise", and for that "a threshold ratio is established between the sum and difference information". The difference energy is meant never to exceed the sum energy, so the result stays mono compatible.

A 1996 review of Spatializer's home theater processor in Stereo Review, found through a search engine, describes a *Double Detect and Protect* circuit that turns the effect down when the level and the stereo difference are both high. That circuit is a limiter of the effect.

## What the undocumented bits and registers may be

No document describes these bits and registers. The table gives the evidence for each one and the reading that fits the evidence best, which remains to be tried on the card.

| Register | Evidence | Most likely |
|---|---|---|
| 50h bit 1 | NetBSD names it MONO, and ESS's drivers leave it 0. The ES938 turns "existing stereo and mono audio input signals" into 3-D | a mono mode, which makes a wide sound from mono input |
| 50h bit 0 | ESS's *3D Limit* setting, which is 0 by default and applies to the ES1869 and ES1879 only. The ES938's register 5 is the effect *limit* and *mode* | a limit or mode of the effect, either AutoSpace or a limiter like Double Detect and Protect |
| 54h, 56h, 58h, 5Ah | always written 8Fh, 95h, 94h and 80h, at the place where the ES938 gets its fixed settings. Bit 7 is set in all four, and the low bits are 0Fh, 15h, 14h and 00h | the processor's fixed settings, such as the noise gate threshold, attack and release times, and the filters that the ES938 has as outside parts |

56h and 58h get 95h and 94h, which differ by one bit, as the values of a pair of times or of the two channels of one setting would.

## Finding out on the card

The same steps are in [TESTING.md](TESTING.md#h-ess3d), step 8.

1. Read the power-on values. Boot to DOS without Windows and run `essreg r=boot.txt`, which lists 50h-5Ah. Neither ESSDC.EXE nor any other ESS DOS program writes these registers, so the values are the chip's own reset values. If they are 8Fh, 95h, 94h and 80h, ESS's drivers only put the defaults back.
2. Find out which bits exist. In essctl's *Expert mode*, write FFh and then 00h to each of 54h-5Ah, on the *Raw registers* page or with `ess3d reg 54 FF`, and read each value back. Bits that stay 0 don't exist.
3. Listen. Play music with a wide stereo image, and a mono voice. Try `ess3d mono toggle` and `ess3d limit toggle` with the 3-D level high, then try each register from 00h to FFh with `ess3d reg` or the tray panel's sliders, and note what changes. `ess3d defaults` puts ESS's values back.
4. Measure. Record source 7 of mixer register 1Ch records the 3-D output itself, so playing test tones with a known left and right and recording them back would show what each setting does to the level and the frequency response, without a microphone.

## Sources

* The ES1869 data sheet, SAM0023-122898, in [docs/datasheet](datasheet).
* The ES938 data sheet, SAM0054-090497, and the ES1868 data sheet, in [docs/datasheet](datasheet).
* ESS's drivers: `driver/ES1869.DRV` 4.04, and the ES1868 driver sets for Windows 95, 98 and NT 4.0, which aren't in the repository (see [RE_NOTES.md](RE_NOTES.md)).
* Linux's `sound/isa/es18xx.c` and `sound/pci/es1938.c`, and NetBSD's `sys/dev/isa/ess.c` and `essreg.h`.
* US patent 5,412,731, Stephen W. Desper, "Automatic stereophonic manipulation system and apparatus for image enhancement" (1995), and US 5,896,456, which has the same description.
