# The 3-D effect (Spatializer)

This page describes what the ES1869's 3-D effect is, where its registers
come from, what the undocumented ones most likely do, and how
`ess3d measure` finds out on the card.

## In short

The ES1869 has the processor of ESS's ES938 chip built in as its 3-D effect,
which is Spatializer 3-D stereo from Desper Products. The data sheet
documents three controls. Mixer register 50h bit 3 turns the effect on, 50h
bit 2 releases it from reset, and 52h sets its level.

Six more are undocumented: 50h bits 1 and 0, and the registers 54h, 56h, 58h
and 5Ah. ESS's Windows 95 and NT drivers and Linux write 8Fh, 95h, 94h and
80h to 54h-5Ah. ESS's Windows 95 and NT drivers also set 50h bit 0 from a
setting called *3D Limit*, and NetBSD calls 50h bit 1 *MONO*.

The reading that fits them best is a model and a limit. 50h bit 1 most
likely switches the effect to its model: its own rendering of the space,
made from the sum of the two channels, which is what the Solo-1's data sheet
calls a mode that makes a stereo effect from a mono input. With the bit set,
you hear the model in place of the program's own image, so you can compare a
tuning with it. 50h bit 0, the limit, most likely lets the effect widen the
image toward the model and no further than the 3-D level, as the AutoSpace
mode of Desper's patent does. The limit may also learn: follow the program
over a window of time and keep the effect where the program has been. The
registers 54h-5Ah would then shape the effect, as the edges of its filters,
its levels or its times, or tune the limit's window. Another reading takes
54h, 56h and 58h together as the three coordinates of one 3-D vector.
`ess3d measure` tests each of these readings on the card ([Measuring on the
card](#measuring-on-the-card)), and essctl, `ess3d` and the tray panel can
set every one of the registers.

## Where it comes from

The ES1869 data sheet describes the effect as follows ([DS
p.25](datasheet)): "The ES1869 incorporates an embedded Spatializer VBX
stereo audio processor provided by Desper Products, Inc., a subsidiary of
Spatializer Audio Laboratories, Inc. It is positioned between the output of
the playback mixer and the master volume controls and it produces a wider
perceived stereo effect." The same page goes on: "The 3-D effect is enabled
by register 50h bit 3. The amount of effect is controlled by directly
programming 3-D Level register 52h." In register 50h, bit 3 enables the
effect, bit 2 releases it from reset, and bits 7:4, 1 and 0 are "Reserved.
Always write 0." (DS p.62). Pin 42, CAP3D, is a "Bypass capacitor to analog
ground for 3-D effects" (DS p.6), and record source 7 of mixer register 1Ch
records the output of the 3-D effect, before the master volume (DS p.59).

The ES1868 has no 3-D effect, and its data sheet (72 pages) has no mixer
registers between 4Eh and 64h.

The ES938 is ESS's separate "3-D Audio Effects Processor" for cards of the
ES1868 era, and it also has bass and treble controls (ES938 data sheet
SAM0054-090497, 1997). The host programs it with MIDI System Exclusive
messages sent out of the MPU-401 port, in the form `F0 00 00 7B
7F <register> <data> [<register> <data> ...] F7`. 00 00 7Bh is ESS's MIDI
ID, and the command 7Fh writes registers while 7Eh reads them.

ESS's drivers show that the ES1869 and ES1879 have the ES938's 3-D processor
built in. The Windows 95 setting that turns on the ES1869's own 3-D effect
is called *Enable ES938* ([DRIVER_CONFIG.md](DRIVER_CONFIG.md)), and ESS's
NT 4.0 driver marks `3D-Limit` and `SpatializerEnable` as "1869 1879" in its
CONFIG.INI. Linux, too, gives the Spatializer to the ES1869 and ES1879 only,
and its Solo-1 driver (ES1938, ES1946, ES1969) writes the same four
registers.

## The ES938's registers

Each item gives a register's number and its name in the data sheet, its
reset value in parentheses where the data sheet gives one, and then its
bits.

* **Register 0, Miscellaneous Control** (reset value 49h). Bit 6 is 1. Bits
  5:4 set the tone input level to 0, -3, -6 or -9 dB. Bits 3:2 are the noise
  gate, which "must be NG0=0, NG1=1", and bits 1:0 "must be" 01b.
* **Register 1, Tone Control** (reset value 33h). Bits 6:4 set the treble
  and bits 2:0 the bass, from -9 to +12 dB in 3 dB steps.
* **Registers 2, 3 and 4, Reserved (Write 0).**
* **Register 5, Spatializer Effect Limit/Mode.** Bits 6:1 are SPC5-SPC0, the
  space control, and bit 0 is R. The data sheet gives the values "00h: no
  effect, 47h: minimum effect level, 5Bh: medium effect level, 67h: maximum
  effect level".
* **Register 6, Spatializer Processor Enable** (reset value 00h). Bits 1:0
  are 02h for disabled and 03h for enabled.
* **Register 7, Power Control** (reset value 0Eh). Bit 2 enables pull-ups
  when powered down. Bit 1 is the output enable, with 0 for bypass. Bit 0
  powers down.

The ES938's SPEN pin "enables spatialization effect at current limit level
(limit level is set to maximum by reset)", so the ES938 calls the amount of
effect its *limit*. Its pins INT1 and INT2 take 4.7 uF capacitors, and FO
and FI connect through an external network of resistors and capacitors. The
ES1869 has a single pin for the 3-D effect, CAP3D.

## The same controls on the ES1869

ESS's NT 4.0 driver (AUDDRIVE.SYS, 1997) drives both chips from the same
code. For each control, it either sends the ES938 a SysEx message or writes
the ES1869's register. Each item names a control and says what the driver
writes to the ES938 and then to the ES1869.

* **3-D on and off.** The driver sets the ES938's register 6 to 03h or 02h,
  and the ES1869's 50h to 0Ch or 04h, keeping bit 0.
* **3-D level**, where h is the slider's high byte. The driver sets the
  ES938's register 5 to (h OR 2) / 2, so SPC5-SPC0 come from h and R is
  always 1. It sets the ES1869's 52h to h / 4.
* **At start, with SpatializerEnable.** The driver sets the ES938's register
  0 to 49h, registers 2, 3 and 4 to 00h, and register 7 to 0Eh. It sets the
  ES1869's 54h to 8Fh, 56h to 95h, 58h to 94h and 5Ah to 80h.
* **At start.** The driver sets the ES1869's 50h to 3D-Limit (1 or 0), so
  3-D is off and held in reset.
* **Treble and bass.** The driver sets the ES938's register 1, and nothing
  on the ES1869, because the ES1869 has no tone control.

The ES1869's 52h bits 5:0 are therefore the ES938's SPC5-SPC0, and 50h bits
3:2 do the job of the ES938's register 6 bits 1:0.

At start, the driver gives the ES938's registers 0, 2, 3, 4 and 7 their
reset values, which are fixed settings such as the noise gate bits that
"must be" set one way. It writes the ES1869's 54h-5Ah at the same place in
the same code, so these registers are most likely fixed settings of the same
kind for the built-in processor.

## What each driver writes

Each item names a driver and says what it writes to 50h, to 52h and to
54h-5Ah.

* **ES1869.DRV 4.04, Windows 95** (3:48AC), only with *Enable ES938*. It
  writes 00h to 50h, then 0Ch, then bit 0 from *3D Limit*. It writes 3Fh to
  52h, then the mixer's 3-D level, and 8Fh, 95h, 94h and 80h to 54h-5Ah.
* **AUDDRIVE.SYS, NT 4.0.** It writes 3D-Limit alone to 50h, then 0Ch or
  04h, keeping bit 0, and the 3-D level to 52h. With SpatializerEnable, it
  writes 8Fh, 95h, 94h and 80h to 54h-5Ah.
* **ESS.SYS, Windows 98 WDM** (`InitSpatializer`). It writes 0Ch to 50h and
  the 3-D level to 52h, and it never writes 54h-5Ah.
* **Linux es18xx.c and es1938.c.** To turn the effect on, they write 08h and
  then 0Ch to 50h, and to turn it off, 00h and then 04h (a reset pulse each
  time). They write the 3-D level to 52h, and 8Fh, 95h, 94h and 80h to
  54h-5Ah, to "Set spatializer parameters to recommended values".
* **NetBSD ess.c.** For 50h, it names 04h RESET, 08h ENABLE and 02h MONO,
  and clears MONO with ENABLE. It writes the 3-D level to 52h and never
  writes 54h-5Ah.

ESS's drivers and Linux write the same four values, and Linux calls them
"recommended values", which suggests that they come from ESS's own
programming notes. Under ESS's Windows 98 driver, 54h-5Ah keep whatever
values the chip powers up with.

## The Spatializer processor

Desper Products' patent US 5,412,731 (1995) describes the processor that the
ES938 and the ES1869 license. It adds a processed difference signal, left
minus right, back to each channel. The difference signal first goes through
a high-pass filter of "about 18 decibels per octave and a cutoff frequency
of about 300 hertz".

The patent names "Two operating modes: 1-Space) In which the ratio is
constant... 2-AutoSpace) In which the ratio is electrically varied... the
enhancement is held at a constant average regardless of program material."
In AutoSpace, a voltage-controlled amplifier sets the level of the
difference signal. The rectified sum and difference signals drive it through
"filters 132 and 134 which have adjustable rise and fall ballistics".

A comparator turns the enhancement off for mono material, "to avoid
excessive increase in stereo noise", and for that "a threshold ratio is
established between the sum and difference information". The difference
energy is meant never to exceed the sum energy, so the result stays mono
compatible.

A 1996 review of Spatializer's home theater processor in Stereo Review,
found through a search engine, describes a *Double Detect and Protect*
circuit that turns the effect down when the level and the stereo difference
are both high. That circuit is a limiter of the effect.

## What the undocumented bits and registers most likely do

No document describes these bits and registers. Each item gives the evidence
for one of them, the reading that fits it best, and what the reading looks
like in the report of `ess3d measure`.

* **50h bit 1, the model.** NetBSD names it MONO, and ESS's drivers never
  set it. The data sheet of the Solo-1 (ES1938), which has the same
  processor, says that its Spatializer "also has a mode that generates a
  stereo effect given a mono input", and the ES1946's data sheet calls 50h
  the "3-D Enable and Mode" register. The ES938 turns "existing stereo and
  mono audio input signals" into 3-D. In a mode for mono input, the effect
  can't work from the program's own L - R, so what comes out is its model of
  the space, made from the sum of the channels. As far as is known, that
  model is fixed in the chip, and listening to it is a way to compare a
  tuning of the other registers with it. In the report, the model shows as
  M>S, which is S coming out of a tone that is the same in both channels. If
  the runs that change 54h-5Ah with the model on leave M>S as it is, the
  model is fixed.
* **50h bit 0, the limit.** It holds ESS's *3D Limit* setting, which is 0 by
  default and applies to the ES1869 and ES1879 only. The ES938 calls the
  amount of effect its *limit*, in register 5 and at its SPEN pin. The bit
  most likely makes the effect widen the image toward the model, with the
  3-D level as the limit, following the program as the AutoSpace mode of
  Desper's patent does. It may instead limit the effect like *Double Detect
  and Protect*. In the report, a limit shows in the ratio runs as an S>S
  gain that falls as the program's S rises, in the band runs as the
  frequencies where that happens, and in the step runs as the time the gain
  takes to follow a jump.
* **The limit as learning over a tuned window.** The patent's AutoSpace
  follows the program through filters with "adjustable rise and fall
  ballistics", which is a window of time over which it learns the program's
  levels. If the limit works this way, the step runs show the window: S>S
  takes time to come back after the program's S falls, and the time is the
  window's length. A limit that learns more than a level may also not come
  back to where it was before the step, and the step lines give the level
  before and after. The step runs with each register at 00h and FFh show
  whether one of them tunes the window, and the pan runs show whether the
  limit pulls panned sounds toward a fixed place, as a model it works toward
  would.
* **54h, 56h, 58h and 5Ah, the shapes.** The drivers that write them always
  write 8Fh, 95h, 94h and 80h, at the place where the ES938 gets its fixed
  settings. Bit 7 is set in all four, and the low bits are 0Fh, 15h, 14h and
  00h. They most likely shape the effect: the high-pass on L - R (about 300
  Hz and 18 dB per octave in the patent), the edges of other filters, the
  noise gate's threshold, and the attack and release times that the ES938
  sets with outside parts. 56h and 58h get 95h and 94h, which differ by one
  bit, as the values of a pair of times or of the two channels of one
  setting would. In the report, a filter edge moves the frequency where S>S
  rises, a level moves S>S up or down at every frequency, and a time changes
  only the ratio, band and step runs.
* **54h, 56h and 58h as one vector.** The three registers may not be three
  settings but the coordinates of one 3-D vector, a direction or a shape
  that places the image. Read as signed numbers with bit 7 for plus, or as
  the byte minus 80h, ESS's values are +15, +21 and +20, a vector about 33
  long, and 5Ah's 80h is 0, which fits a fourth value at rest. The report's
  vector runs test this. *vec neg* clears the three sign bits together, and
  the summary compares it with the sum of each sign bit cleared alone: the
  two are the same where each register acts by itself, one after another,
  and differ where the three make one thing. *vec /2* and *vec x2* halve and
  double the length and keep the direction, and *vec rot* and *vec swap*
  keep the length and turn the direction (*vec rot* gives 54h 56h's value,
  56h 58h's and 58h 54h's, and *vec swap* exchanges 54h and 56h). If only
  the direction counts, *vec /2* and *vec x2* change nothing while *vec rot*
  and *vec swap* do. A sign bit that turns a part of the effect around shows
  as a change of about 180 degrees in the S>S phase.

## Measuring on the card

`ess3d measure` plays tones on the Audio 2 wave device and records them on
Audio 1 from record source 7, which is the effect's output before the master
volume (DS p.59), with the record level at 0 dB and the filters of both DACs
on. Run 0 has the effect off, and every other run is given in dB relative to
it, so the DAC, the mixer and the ADC drop out of the values. While it
measures, ess3d mutes the mixer's other inputs and sets the master volume to
its lowest step, so the speakers stay quiet, and it puts both back at the
end. Close the programs that play or record before you start, and end any
DOS game with sound.

Each sweep run has a line for each path, with a value for each of 11
frequencies from 100 Hz to 10 kHz:
* **M>M** is a tone that is the same in both channels (M) coming out as M.
* **S>S** is a tone in opposite phase in the two channels (S) coming out as
  S. This is where the effect widens the image.
* **M>S** is S coming out of an M tone, relative to run 0's M>M. Width made
  from mono shows here.
* **S>M** is M coming out of an S tone, relative to run 0's S>S.

Under them come the phases, in degrees. *S>S deg* is the phase that the
effect adds to S against M, measured from the S tone's S and the M tone's M
at the same frequency, so the time at which the recording starts drops out.
A part of the effect whose sign flips turns it by about 180 degrees where
that part is bigger than the sound that passes straight through. *M>S deg*
and *S>M deg* give the phase of a cross path against the direct path of the
same tone.

A dot is a value under the noise floor, and the M>S and S>M lines are left
out where every value is. The full plan has these runs, in about 9 minutes:
1. Run 0, *off*, and run 1, *ess*, which is ESS's setting (50h 0Ch, 52h 3Fh
   and 54h-5Ah 8Fh, 95h, 94h and 80h).
2. The level 52h from 00h to 38h.
3. *model*, *limit*, and both bits together.
4. Each of 54h-5Ah with one bit of ESS's value flipped (*b7* to *b0*), and
   at 00h and FFh.
5. The vector runs, *vec neg*, *vec /2*, *vec x2*, *vec rot* and *vec swap*,
   which change 54h, 56h and 58h together.
6. 54h-5Ah at 00h, at FFh and with bit 7 of ESS's value flipped, with the
   model on. These are the runs whose names start with *M*.
7. *ratio* and *ratio limit*: 400 Hz in M and 1 kHz in S, with the S from
   -24 to +6 dB relative to the M, which shows whether the gain follows the
   program.
8. *band* and *band limit*: S at each of the 11 frequencies over 400 Hz in
   M, which shows in which band the limit holds the effect down. The runs
   whose names start with *band L* have the limit on and one register at 00h
   or FFh, which shows whether that register moves the band.
9. *pan* and *pan limit*: 1 kHz panned in five steps from the left (0
   degrees) to the right (90 degrees). *out deg* is where it comes out, from
   the parts of the two channels in phase with the stronger one, so a value
   under 0 or over 90 degrees is past a speaker, where the far channel plays
   in opposite phase. *level* is its level against run 0.
10. *step* and *step limit*: the S tone jumps from -12 to +6 dB at 0 ms and
    back after 2 s, measured every 20 ms for 500 ms and then every 100 ms to
    1.8 s, which shows how fast the gain follows and how long it takes to
    come back. The runs whose names start with *step L* have the limit on
    and one register at 00h or FFh, or the level at 20h, which shows whether
    a register tunes the window.

The summary at the end gives each run's biggest change from the run it
varies, and lines that answer the questions of the readings above:
* The *model* line gives how much the registers change M>S with the model
  on, which is about 0 dB if the model is fixed.
* The *vector* lines give what clearing the three sign bits together does,
  how far that is from the sum of each cleared alone, and what each of the
  other vector runs changes.
* The *ratio limit* line gives how much S>S falls from S/M -24 to +6 dB with
  the limit on, against the *ratio* line with it off.
* The *pan* lines give where each place comes out, with the limit off and
  on.
* Each step run's line gives how long S>S takes to settle within 1 dB after
  the step up, and to come back within 1 dB of the level before it after the
  step down, and the low level before and after. The *window* line gives the
  fastest and the slowest return among the *step L* runs, with their names,
  which shows the register that tunes the window, if any.

`ess3d measure quick` runs only 00h and FFh of each register, two vector
runs and each of the other kinds with the limit off and on, in about 2
minutes. `ess3d measure 56` runs 56h from 00h to FFh in steps of 10h, which
shows the shape that one register sets more finely, and then the model, band
and step runs with 56h at its ends. `ess3d measure quick /sim` shows what a
report looks like without the card, from a made-up effect in `src/s3dsim.c`
that is not a model of the real chip.

The same steps are in [TESTING.md](TESTING.md#h-ess3d), step 8, along with
two that need no measurement:
1. Read the power-on values. Boot to DOS without Windows and run
   `essreg r=boot.txt`, which lists 50h-5Ah. Neither ESSDC.EXE nor any other
   ESS DOS program writes these registers, so the values are the chip's own
   reset values. If they are 8Fh, 95h, 94h and 80h, ESS's drivers only put
   the defaults back.
2. Find out which bits exist. In essctl's *Expert mode*, write FFh and then
   00h to each of 54h-5Ah, on the *Raw registers* page or with
   `ess3d reg 54 FF`, and read each value back. Bits that stay 0 don't
   exist.
3. Listen. Play music with a wide stereo image, and a mono voice. Try
   `ess3d model toggle` and `ess3d limit toggle` with the 3-D level high,
   then try each register from 00h to FFh with `ess3d reg` or the tray
   panel's sliders, and note what changes. `ess3d defaults` puts ESS's
   values back.
4. Measure with `ess3d measure`, and keep `ESS3D.TXT`.

## Sources

* The ES1869 data sheet, SAM0023-122898, in [docs/datasheet](datasheet).
* The ES938 data sheet, SAM0054-090497, and the ES1868 data sheet, in
  [docs/datasheet](datasheet).
* The Solo-1 (ES1938) data sheet, SAM0090-012398, and the ES1946 data sheet,
  SAM0219-051998, quoted as a search engine's index shows them. They aren't
  in the repository.
* ESS's drivers: `driver/ES1869.DRV` 4.04, and the ES1868 driver sets for
  Windows 95, 98 and NT 4.0, which aren't in the repository (see
  [RE_NOTES.md](RE_NOTES.md)).
* Linux's `sound/isa/es18xx.c` and `sound/pci/es1938.c`, and NetBSD's
  `sys/dev/isa/ess.c` and `essreg.h`.
* US patent 5,412,731, Stephen W. Desper, "Automatic stereophonic
  manipulation system and apparatus for image enhancement" (1995), and US
  5,896,456, which has the same description.
