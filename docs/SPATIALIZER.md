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

`ess3d measure` measured them on a card ([What one card
showed](#what-one-card-showed)):
* The effect adds to the S, the difference of the two channels, a band-pass
  copy of it, up to about +17 dB at 400 Hz, and leaves the M, their sum, as
  it is. 52h sets this boost in steps of 0.75 dB.
* 50h bit 1, the model, makes the S from the M through the same band-pass
  and drops the program's own S: a stereo effect made from mono, as the
  Solo-1's data sheet describes.
* 50h bit 0, the limit, holds the S out at a level against the M, 5.5 dB
  over it with ESS's values. It moves the boost down and up at fixed rates
  in dB a second, 51.5 down and 5.2 up with ESS's values, so it follows the
  program over a few seconds. It hardly acts under 400 Hz.
* With the limit off, 54h-5Ah change nothing. With it on, they are its
  settings. Bits 6:0 of 54h and 56h add up to raise its level, and 5Ah trims
  the level by about 1 dB either way. The low four bits of 58h set how fast
  the boost falls and its high four bits how much slower it rises, and 54h
  bit 7 clear stops it rising.

essctl, `ess3d` and the tray panel can set every one of these registers, and
call 54h-5Ah the limit's level, level 2, speed and trim.

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
  writes 54h-5Ah. NetBSD's Solo-1 driver, eso.c, has the same two controls.
* **ESS's Solo-1 WDM driver** (ES1969.SYS, as a reconstruction from its
  binary shows it). Its header calls 50h the "Spatializer enable and mode
  control" register. It writes 00h and then 04h to 50h and the level to 52h
  at start, and 0Ch or 04h for its *3D Effect Enable* switch. It never
  writes 54h-5Ah.
* **Linux esssolo1.c (OSS, Solo-1).** It writes the level to 52h, and 08h or
  00h to 50h, which leaves bit 2 clear, so the effect stays in reset. It
  never writes 54h-5Ah.

ESS's drivers and Linux write the same four values, and Linux calls them
"recommended values", which suggests that they come from ESS's own
programming notes. A search of drivers, data sheets and patents in 2026
found no source that names 54h-5Ah or 50h bit 0, or says what they do, so
the measurement below is the only evidence. Under ESS's Windows 98 driver,
54h-5Ah keep whatever values the chip powers up with.

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

## What the undocumented bits and registers do

No document describes these bits and registers. Each item gives the evidence
for one of them, what one card showed ([What one card
showed](#what-one-card-showed)), and what is still open.

* **50h bit 1, the model.** NetBSD names it MONO, and ESS's drivers never
  set it. The data sheet of the Solo-1 (ES1938), which has the same
  processor, says that its Spatializer "also has a mode that generates a
  stereo effect given a mono input", and the ES1946's data sheet calls 50h
  the "3-D Enable and Mode" register. On the card, the bit makes the S out
  of the M, through nearly the same band-pass as the boost and about 3 dB
  lower, and drops the program's own S to between -24 and -44 dB. What you
  hear is the effect's own image of the space, made from the mono sum, in
  place of the program's stereo, and none of 54h-5Ah changes it. With the
  limit on as well, the limit holds this S down too, and the M around 400 Hz
  drops by up to 9 dB with it, so in this mode part of the M also comes
  through the processed path.
* **50h bit 0, the limit.** It holds ESS's *3D Limit* setting, which is 0 by
  default and applies to the ES1869 and ES1879 only. The ES938 calls the
  amount of effect its *limit*, in register 5 and at its SPEN pin. On the
  card, the bit holds the S out at a level against the M, 5.5 dB over it
  with ESS's values, so a program that is already wide gets less boost, and
  one that is wider still gets none. With S and no M, it turns the boost
  off. The level is a ratio to the M, which is what the AutoSpace mode of
  Desper's patent does: it holds the enhancement at a constant ratio to the
  program. The limit follows the S through a high-pass, so it hardly acts on
  bass: with a fast limit it holds the S at +5.4 dB from 1 to 4 kHz, at +6.6
  dB at 630 Hz, +8.3 at 400 Hz and +11.2 at 250 Hz, and not at all at 160 Hz
  and under.
* **58h, the limit's speed.** AutoSpace follows the program through filters
  with "adjustable rise and fall ballistics". On the card, the limit moves
  the boost in a straight line in dB, down while the S is over the level and
  up once it is under, from the moment the S crosses it. Bits 3:0 of 58h, n,
  set the fall, 257.5 / (n + 1) dB a second, and bits 7:4, h, make the rise
  h + 1 times slower. With ESS's 94h, n is 4 and h is 9: the boost falls at
  51.5 dB a second and rises at 5.2, so after a wide passage the effect
  takes about 3 seconds to come back from 14 dB down. With 58h at 00h both
  are 257.5 dB a second, and at FFh 16 and 1. All 14 values of 58h that the
  card was measured with fit these to within 2 percent.
* **54h, 56h and 5Ah, the limit's level.** The drivers that write them
  always write 8Fh, 95h and 80h, at the place where the ES938 gets its fixed
  settings. On the card, with the limit off, none of them changes the effect
  at any frequency, with any bit flipped or at either end, so they don't
  shape its filters. With the limit on, bits 6:0 of 54h and of 56h add, and
  the level grows with their sum: the same bit in either register moves it
  by the same amount. 5Ah trims the level by its value less 80h, from -1.2
  dB at 00h to +1.0 dB at FFh. In dB over the M, the level is close to 10
  log ((1.77 + 0.00216 v)^2 + (0.0176 x)^2), with x the sum and v 5Ah less
  80h, which matches every measured setting to within 0.4 dB:

  | 54h + 56h, bits 6:0    | 5Ah | Level held, dB over the M |
  |------------------------|-----|---------------------------|
  | 21 (54h at 00h)        | 80h | +5.3                      |
  | 36 (ESS's 8Fh and 95h) | 80h | +5.4                      |
  | 52                     | 80h | +6.0                      |
  | 68                     | 80h | +6.7                      |
  | 100                    | 80h | +8.1                      |
  | 142 (56h at FFh)       | 80h | +9.7                      |
  | 148 (54h at FFh)       | 80h | +9.8                      |
  | 36                     | 00h | +4.2                      |
  | 36                     | FFh | +6.4                      |

* **The sign bits.** Bit 7 of 54h, cleared alone, stops the boost rising:
  the limit lowers it and it stays down. Bit 7 of 56h, cleared alone, does
  nothing, and bit 7 of 58h is part of its rise. With bit 7 of 54h, 56h and
  58h all cleared, the level dropped to about -7 dB against the M, and the
  boost rose again. `ess3d measure limit` now clears the three two at a
  time, and 54h and 56h at 00h and 7Fh together, to tell which bits do this.
* **54h, 56h and 58h as one vector.** As signed numbers with bit 7 for plus,
  or as the byte minus 80h, ESS's values are +15, +21 and +20, and 5Ah's 80h
  is 0. The three don't act as one vector, though. With the limit off,
  nothing changes whatever the vector. With it on, 54h and 56h only add, 58h
  is two rates, and halving, doubling or turning the three together does
  what the formulas of each predict.

## Measuring on the card

`ess3d measure` plays tones on the Audio 2 wave device and records them on
Audio 1 from record source 7, which is the effect's output before the master
volume (DS p.59), with the record level at 0 dB and the filters of both DACs
on. Run 0 has the effect off, and every other run is given in dB relative to
it, so the DAC, the mixer and the ADC drop out of the values. The last run
has the effect off again, which shows whether the path moved during the
measurement. While it measures, ess3d mutes the mixer's other inputs and
sets the master volume to its lowest step, where the speakers play the tones
only faintly, and it puts both back at the end. Close the programs that play
or record before you start, and end any DOS game with sound.

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
out where every value is. On the card, the right channel is 0.2 dB lower
than the left between the DAC and the effect, which puts M>S and S>M at
about -38 dB in every run, and the boost raises M>S with it. The summary
leaves out cross paths under -15 dB for that reason.

The full plan has these runs, in about 6 minutes:
1. Run 0, *off*, and run 1, *ess*, which is ESS's setting (50h 0Ch, 52h 3Fh
   and 54h-5Ah 8Fh, 95h, 94h and 80h).
2. The level 52h from 00h to 38h.
3. *model*, *limit*, and both bits together.
4. 54h-5Ah at 00h and FFh, with the limit off, and with the model on. The
   runs with the model on have names that start with *M*.
5. *ratio* and *ratio limit*: 400 Hz in M and 1 kHz in S, with the S from
   -24 to +6 dB relative to the M, which shows the level that the limit
   holds. The *boost* line gives the boost's own gain, 0 dB where the limit
   leaves it as it is.
6. *band* and *band limit*: S at each of the 11 frequencies over 400 Hz in
   M. With ESS's 58h the limit is slow, so each tone carries the last one's
   limit, and *band L 58=00* shows each frequency with a fast limit.
7. *pan* and *pan limit*: 1 kHz panned in five steps from the left (0
   degrees) to the right (90 degrees). *out deg* is where it comes out, from
   the parts of the two channels in phase with the stronger one, so a value
   under 0 or over 90 degrees is past a speaker, where the far channel plays
   in opposite phase. *level* is its level against run 0.
8. *step* and *step limit*: the 1 kHz S tone steps from -12 to 0 dB relative
   to the M at 0 ms and back after 2 s. The lines give the boost's own gain
   every 20 ms for 500 ms and then every 100 ms, to 1.8 s after the step up
   and 3 s after the step down, with *off* for a boost under -50 dB. The
   runs whose names start with *L* have the limit on and one register at 00h
   or FFh, or the level at 20h.
9. The vector runs, *vec neg*, *vec /2*, *vec x2*, *vec rot* and *vec swap*,
   which change 54h, 56h and 58h together, with the limit on. *vec neg*
   clears the three sign bits, *vec /2* and *vec x2* halve and double the
   length, *vec rot* gives 54h 56h's value, 56h 58h's and 58h 54h's, and
   *vec swap* exchanges 54h and 56h.
10. *off again*.

`ess3d measure limit` has each bit of 54h-5Ah flipped from ESS's value, and
each of them at 00h and FFh, in step runs with the limit on, with the ratio
runs and the vector runs, in about 8 minutes. It also clears the sign bits
of 54h, 56h and 58h two at a time, puts 54h and 56h at 00h and at 7Fh
together, repeats the step with every tone 12 dB lower (*L -12 dB*), which
shows whether the level is a ratio to the M or a fixed level, and has a band
run with a fast limit, which shows the level at each frequency. Its report
is bigger than Notepad can open, so the *Open report* button opens it in
WordPad. `ess3d measure quick` runs each kind once or twice in about 2
minutes, and `ess3d measure 58` runs 58h from 00h to FFh in steps of 10h
with the limit on. `ess3d measure quick /sim` shows what a report looks like
without the card, from an effect in `src/s3dsim.c` that follows this card's
measurement and is made up in between.

The summary at the end gives each run's biggest change from the run it
varies, and lines that answer the questions of the readings above:
* The *model* line gives how much the registers change M>S with the model
  on, which is 0 dB if the model is fixed.
* The *ratio limit* line gives the S out against the M where the limit holds
  the boost.
* The *pan* lines give where each place comes out, with the limit off and
  on.
* The *limit* table gives, for each step run, how fast the boost falls after
  the step up, in dB a second, its gain at 0 dB S/M and at -12 dB S/M, the S
  out it holds against the M (*held*), how long it waits after the step down
  before it rises (*hold*), how fast it rises then, and its gain at the end.
  The rates come from a straight line through the windows on the way. The
  *fall*, *rise* and *held* lines name the runs at both ends of each, which
  shows the register that sets it.

Every run with the limit off also checks that each of its tones gives the
same amplitude and phase in both halves of its window. A window whose halves
differ means that the recording or the playback skipped there. ess3d
measures such a run again, logs it in `ESS3D.LOG`, and if it skips again,
the report says from which window on its values may be off. One run of the
card's second measurement had such a skip, before this check existed.

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
4. Measure with `ess3d measure` and `ess3d measure limit`, and keep both
   reports.

## What one card showed

The card is an ES1869 under Windows 98 with the rebuilt drivers, measured
three times: the full plan of an earlier version of ess3d, then the limit
plan and the full plan of the current one.

### The first measurement

In the earlier version, each run changed one register with the limit off,
and the step runs stepped the S from -12 to +6 dB against the M. That
version set the mixer about 0.75 s into each run, so the first windows of
each run had 3.6 dB more level, and about 85 ms of sound went missing around
the 250 Hz tone. The values below leave those windows out. ess3d now sets
the mixer a few ms into each run, before the tones, and the later
measurements confirm these values.

**The boost.** With ESS's setting, S>S rises from +10.9 dB at 100 Hz to
+17.9 dB at 400 Hz and falls to +0.5 dB at 10 kHz, and its phase goes from
+50 degrees to -54. Taken apart, the boost added to the S is a band-pass of
about 6 dB per octave on each side, with corners near 200 Hz and 1 kHz,
+16.7 dB at its peak and turning from +65 degrees at 100 Hz to -104 at 10
kHz. M>M stays at 0 dB, so the M passes as it is.

**52h, the level.** The boost grows by 6 dB for every 8 steps of 52h, 0.75
dB a step: at 400 Hz it is -24.5 dB at 08h, -12.7 at 18h, -0.4 at 28h, +11.4
at 38h and +16.7 at 3Fh. At 00h it is under -30 dB, so S>S is +0.2 dB.

**50h bit 1, the model.** M>S becomes +6.6 dB at 100 Hz, +13.9 at 400 Hz and
-4.5 at 10 kHz, which is the boost's own shape about 2.8 dB lower, and S>S
falls to between -24 and -44 dB. With the model on, none of 54h-5Ah at 00h
or FFh changes M>S.

**With the limit off.** Every bit of 54h-5Ah flipped from ESS's value, each
register at 00h and FFh, and the five vector runs leave every path within
0.1 dB of ESS's setting.

**The pan runs.** With the effect at ESS's setting, a tone panned fully left
comes out at -36 degrees, past the left speaker, and fully right at 127
degrees, about 12.7 dB louder, while the middle stays. The channel imbalance
of the path puts a centered tone about 4 degrees off, since the boost raises
the S that the imbalance makes by about 16 dB. With the limit on, the sides
come out at -26 and 113 degrees.

### The limit, bit by bit

The limit plan stepped the S from -12 to 0 dB against the M and back, with
each bit of 54h-5Ah flipped from ESS's value. Every run in it worked, and it
decoded most of the limit:

**The ratio runs.** S>S at 1 kHz stays at +15.7 dB from S/M -24 to -12 dB,
then falls to +11.4, +5.5 and +0.2 dB at -6, 0 and +6 dB: the limit holds
the S out at +5.4 to +5.5 dB against the M, and turns the boost off once the
input's S alone is over that.

**58h.** The boost's gain falls and rises in straight lines in dB, from the
moment the S crosses the level, with no hold before the rise. The rates fit
257.5 / (n + 1) dB a second down and h + 1 times slower up, with n and h the
low and high four bits of 58h:

| 58h       | Fall, dB/s | Formula | Rise, dB/s | Formula |
|-----------|------------|---------|------------|---------|
| 00h       | 257.5      | 257.5   | 255        | 257.5   |
| 90h       | 257.5      | 257.5   | 25.8       | 25.8    |
| 14h       | 51.6       | 51.5    | 25.7       | 25.8    |
| 84h       | 51.6       | 51.5    | 5.72       | 5.72    |
| 94h (ESS) | 51.5       | 51.5    | 5.14       | 5.15    |
| B4h       | 51.5       | 51.5    | 4.29       | 4.29    |
| D4h       | 51.5       | 51.5    | 3.67       | 3.68    |
| 95h       | 43.0       | 42.9    | 4.28       | 4.29    |
| 96h       | 36.8       | 36.8    | 3.66       | 3.68    |
| 9Ch       | 19.8       | 19.8    | 1.96       | 1.98    |
| FFh       | 16.1       | 16.1    | 0.98       | 1.01    |

**54h, 56h and 5Ah.** Setting bit 6, 5 or 4 of 54h raised the held level by
2.7, 1.3 and 0.6 dB. Setting bit 6 or 5 of 56h raised it by 2.8 and 1.4 dB,
and clearing its bit 4, which ESS's value has set, lowered it by 0.2 dB, so
the two registers add. Their low bits and 5Ah's moved it by tenths of a dB,
5Ah in steps that follow its value less 80h ([the table
above](#what-the-undocumented-bits-and-registers-do)).

**The sign bits.** With 54h bit 7 cleared, the boost fell as with ESS's
value and never rose again. With 56h bit 7 cleared, nothing changed. With
58h bit 7 cleared, the rise was 5 times faster, as its formula says. With
all three cleared (*vec neg*), the limit held the S at about -7 dB against
the M, 12 dB lower, and the boost rose again after the step down, from about
-47 dB, which looks like the least gain the limit leaves.

**The band runs.** With the limit on and 58h at 00h, so that the limit gets
to its level at each tone, the S comes out at +11.2 dB against the M at 250
Hz, +8.3 at 400 Hz, +6.6 at 630 Hz and +5.3 to +5.9 from 1 to 4 kHz. At 160
Hz and under the boost isn't limited at all. The limit follows the S through
a high-pass near 400 Hz.

**The model with the limit.** With both bits on, the limit holds the S that
the model makes from the M down, by up to 13 dB at 400 Hz, and the M around
400 Hz drops by up to 9 dB with it.

### The second full plan

The full plan of the current version confirmed the first measurement, with
the 250 Hz tone and the pan levels now clean. One run, 52h at 38h, had a
skip from the 1 kHz S tone on: its values there were up to 4.5 dB low with
the phases scrambled, while the next runs were normal. ess3d now checks each
window's halves for such skips and measures the run again.

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
* Linux's `sound/isa/es18xx.c`, `sound/pci/es1938.c` and
  `sound/oss/esssolo1.c`, and NetBSD's `sys/dev/isa/ess.c`, `essreg.h` and
  `sys/dev/pci/eso.c`.
* The reconstruction of ESS's ES1969.SYS at github.com/leecher1337/es1969,
  which is decompiled rather than ESS's own source.
* US patent 5,412,731, Stephen W. Desper, "Automatic stereophonic
  manipulation system and apparatus for image enhancement" (1995), and US
  5,896,456, which has the same description.
