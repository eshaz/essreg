# The 3-D effect (Spatializer)

What the ES1869's 3-D effect is, where its registers come from, and what is still unknown about the undocumented ones.

## In short

* The ES1869's 3-D effect is the processor of ESS's ES938 chip, built in: Spatializer 3-D stereo from Desper Products.
* The data sheet documents three controls: mixer 50h bit 3 (on), 50h bit 2 (released from reset) and 52h (the level).
* Six things are undocumented: 50h bits 1 and 0, and the registers 54h, 56h, 58h and 5Ah.
  * ESS's Windows 95 and NT drivers, and Linux, write 54h-5Ah with 8Fh, 95h, 94h and 80h.
  * ESS's Windows 95 and NT drivers set 50h bit 0 from a setting called *3D Limit*.
  * NetBSD calls 50h bit 1 *MONO*.
* No document found says what any of them do. The ES938's data sheet and Desper's patent point to where they may come from: an effect *limit* and *mode*, a noise gate, and an automatic level control with attack and release times.
* essctl, `ess3d` and the tray panel have every one of them, to try on the card.

## Where it comes from

* **The ES1869 data sheet** ([DS p.25](datasheet)): "The ES1869 incorporates an embedded Spatializer® VBX™ stereo audio processor provided by Desper Products, Inc., a subsidiary of Spatializer Audio Laboratories, Inc. It is positioned between the output of the playback mixer and the master volume controls and it produces a wider perceived stereo effect."
  * "The 3-D effect is enabled by register 50h bit 3. The amount of effect is controlled by directly programming 3-D Level register 52h." (DS p.25)
  * 50h: bit 3 enables it, bit 2 releases it from reset, and bits 7:4, 1 and 0 are "Reserved. Always write 0." (DS p.62)
  * Pin 42, CAP3D, is a "Bypass capacitor to analog ground for 3-D effects" (DS p.6).
  * Record source 7 of mixer 1Ch records the output of the 3-D effect, before the master volume (DS p.59).
* **The ES1868** has no 3-D effect. Its data sheet (72 pages) has no mixer registers between 4Eh and 64h.
* **The ES938** is ESS's separate "3-D Audio Effects Processor" for ES1868-era cards, with bass and treble controls (ES938 data sheet SAM0054-090497, 1997).
  * The host programs it with MIDI System Exclusive messages sent out the MPU-401 port: `F0 00 00 7B 7F <register> <data> [<register> <data> ...] F7`. 00 00 7Bh is ESS's MIDI ID; 7Fh writes, 7Eh reads.
* **The ES1869 and ES1879 have the ES938's 3-D processor built in.** ESS's drivers say so:
  * The Windows 95 setting that turns on the ES1869's own 3-D is *Enable ES938* ([DRIVER_CONFIG.md](DRIVER_CONFIG.md)).
  * ESS's NT 4.0 driver notes `3D-Limit` and `SpatializerEnable` as "1869 1879" in its CONFIG.INI.
  * Linux gives the Spatializer to the ES1869 and ES1879 only. Its Solo-1 driver (ES1938, ES1946, ES1969) writes the same four registers.

## The ES938's registers

| Register | Name in the data sheet | Bits | Reset |
|---|---|---|---|
| 0 | Miscellaneous Control | bit 6: 1; 5:4: tone input level 0, -3, -6 or -9 dB; 3:2: noise gate, "must be NG0=0, NG1=1"; 1:0: "must be" 01b | 49h |
| 1 | Tone Control | 6:4 treble, 2:0 bass, -9 to +12 dB in 3 dB steps | 33h |
| 2, 3, 4 | Reserved (Write 0) | | |
| 5 | Spatializer Effect Limit/Mode | 6:1 SPC5-SPC0, the space control; 0: R. "00h: no effect, 47h: minimum effect level, 5Bh: medium effect level, 67h: maximum effect level" | |
| 6 | Spatializer Processor Enable | 1:0: 02h disabled, 03h enabled | 00h |
| 7 | Power Control | 2: pull-ups when powered down; 1: output enable, 0 = bypass; 0: power down | 0Eh |

* Its SPEN pin "enables spatialization effect at current limit level (limit level is set to maximum by reset)". So the ES938 calls the amount of effect its *limit*.
* Pins INT1 and INT2 take 4.7 µF capacitors, and FO and FI an external resistor and capacitor network. The ES1869 has one pin for the 3-D effect, CAP3D.

## The same controls on the ES1869

ESS's NT 4.0 driver (AUDDRIVE.SYS, 1997) drives both chips from the same code. For each control it sends the ES938 a SysEx message, or writes the ES1869's register:

| Control | ES938 | ES1869 |
|---|---|---|
| 3-D on, off | register 6 = 03h, 02h | 50h = 0Ch, 04h, keeping bit 0 |
| 3-D level (the slider's high byte h) | register 5 = (h OR 2) / 2: SPC5-SPC0 from h, R always 1 | 52h = h / 4 |
| At start, with SpatializerEnable | registers 0 = 49h; 2, 3 and 4 = 00h; 7 = 0Eh | 54h = 8Fh, 56h = 95h, 58h = 94h, 5Ah = 80h |
| At start | | 50h = 3D-Limit (1 or 0): 3-D off, held in reset |
| Treble, bass | register 1 | none: the ES1869 has no tone control |

* So 52h bits 5:0 are the ES938's SPC5-SPC0, and 50h bits 3:2 its register 6 bits 1:0.
* The ES938 gets its registers 0, 2, 3, 4 and 7 at their reset values: fixed settings like the noise gate bits that "must be" set one way. The ES1869's 54h-5Ah are written in the same place of the same code, so they're most likely the same kind of fixed settings of the built-in processor.

## What each driver writes

| Driver | 50h | 52h | 54h-5Ah |
|---|---|---|---|
| ES1869.DRV 4.04, Windows 95 (3:48AC) | 00h, then 0Ch, then bit 0 from *3D Limit* | 3Fh, then the mixer's 3-D level | 8Fh, 95h, 94h, 80h, only with *Enable ES938* |
| AUDDRIVE.SYS, NT 4.0 | 3D-Limit alone, then 0Ch or 04h keeping bit 0 | the 3-D level | 8Fh, 95h, 94h, 80h, with SpatializerEnable |
| ESS.SYS, Windows 98 WDM (`InitSpatializer`) | 0Ch | the 3-D level | never |
| Linux es18xx.c and es1938.c | on: 08h then 0Ch; off: 00h then 04h (a reset pulse each time) | the 3-D level | 8Fh, 95h, 94h, 80h: "Set spatializer parameters to recommended values" |
| NetBSD ess.c | names 04h RESET, 08h ENABLE and 02h MONO; clears MONO with ENABLE | the 3-D level | never |

* The same four values in ESS's drivers and in Linux, which calls them "recommended values", suggest they come from ESS's own programming notes.
* Under ESS's Windows 98 driver, 54h-5Ah keep whatever value the chip powers up with.

## The Spatializer processor

Desper Products' patent US 5,412,731 (1995) describes the processor the ES938 and ES1869 license:

* It adds a processed difference signal (left minus right) back to each channel. The difference signal first goes through a high-pass filter of "about 18 decibels per octave and a cutoff frequency of about 300 hertz".
* "Two operating modes: 1-Space) In which the ratio is constant... 2-AutoSpace) In which the ratio is electrically varied... the enhancement is held at a constant average regardless of program material."
  * In AutoSpace a voltage controlled amplifier sets the difference level, driven by the rectified sum and difference signals through "filters 132 and 134 which have adjustable rise and fall ballistics".
* A comparator turns the enhancement off for mono material, "to avoid excessive increase in stereo noise": "a threshold ratio is established between the sum and difference information".
* The difference energy is meant never to exceed the sum energy, so the result stays mono compatible.
* A 1996 review of Spatializer's home theater processor (Stereo Review, as found by a search engine) describes a *Double Detect and Protect* circuit that turns the effect down when the level and the stereo difference are both high: a limiter of the effect.

## What the undocumented bits and registers may be

None of this is documented. It's what fits the evidence above, to be tried on the card.

| Register | Evidence | Most likely |
|---|---|---|
| 50h bit 1 | NetBSD names it MONO. ESS's drivers leave it 0. The ES938 turns "existing stereo and mono audio input signals" into 3-D | a mono mode: a wide sound from mono input |
| 50h bit 0 | ESS's *3D Limit* setting, 0 by default, for the ES1869 and ES1879 only. The ES938's register 5 is the effect *limit* and *mode* | a limit or mode of the effect: AutoSpace, or a Double Detect and Protect style limiter |
| 54h, 56h, 58h, 5Ah | always written 8Fh, 95h, 94h, 80h, where the ES938 gets its fixed settings. Bit 7 is set in all four; the low bits are 0Fh, 15h, 14h and 00h | the processor's fixed settings: noise gate threshold, attack and release times, the filters that the ES938 has as outside parts |

* 56h and 58h differ by one bit, 95h and 94h, as a pair of times or the two channels of one setting would.

## Finding out on the card

The steps are also in [TESTING.md](TESTING.md#h-ess3d), step 8.

1. **The power-on values.** Boot to DOS without Windows and run `essreg r=boot.txt`. It lists 50h-5Ah. Neither ESSDC.EXE nor any other ESS DOS program writes them, so these are the chip's own reset values. If they are 8Fh, 95h, 94h and 80h, ESS's drivers only put the defaults back.
2. **Which bits exist.** In essctl's Expert mode, write FFh and then 00h to each of 54h-5Ah (Raw registers, or `ess3d reg 54 FF`) and read them back. Bits that stay 0 don't exist.
3. **By ear.** Play music with a wide stereo image, and a mono voice. Try `ess3d mono toggle` and `ess3d limit toggle` at a high level, then each register from 00h to FFh with `ess3d reg` or the tray panel's sliders, and note what changes. `ess3d defaults` puts ESS's values back.
4. **Measuring.** Record source 7 of mixer 1Ch records the 3-D output itself. Playing test tones with a known left and right, and recording them back, would show what each setting does to the level and frequency response without a microphone.

## Sources

* ES1869 data sheet SAM0023-122898, [docs/datasheet](datasheet).
* ES938 data sheet SAM0054-090497 and ES1868 data sheet, [docs/datasheet](datasheet).
* ESS drivers: `driver/ES1869.DRV` 4.04; the ES1868 driver sets for Windows 95, 98 and NT 4.0 (not in the repository, see [RE_NOTES.md](RE_NOTES.md)).
* Linux `sound/isa/es18xx.c` and `sound/pci/es1938.c`; NetBSD `sys/dev/isa/ess.c` and `essreg.h`.
* US patent 5,412,731, Stephen W. Desper, "Automatic stereophonic manipulation system and apparatus for image enhancement" (1995); US 5,896,456, the same description.
