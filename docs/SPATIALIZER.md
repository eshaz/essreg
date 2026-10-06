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
* 50h bit 0, the limit, holds the S out under a level, 5.5 dB over the M
  with ESS's values and an M at -24 dBFS. It moves the boost down and up at
  fixed rates in dB a second, 51.5 down and 5.15 up with ESS's values, so it
  follows the program over a few seconds. It hardly acts under 400 Hz.
* With the limit off, 54h-5Ah change nothing. With it on, they are its
  settings. 54h and 56h each set a level of the S out: the boost falls while
  the S out is over both levels, and rises while it is under 54h's. Each
  level has a part that follows the M, 4.3 dB over it with the register's
  bit 7 set and 12.8 dB under it with bit 7 clear, and a fixed part from the
  register's bits 6:0, which 5Ah scales. The fixed part lets quiet sound
  keep more width: with ESS's values, an M under about -50 dBFS gets the
  full boost. The low four bits of 58h set how fast the boost falls, and its
  high four bits how much slower it rises.

essctl, `ess3d` and the tray panel can set every one of these registers, and
call 54h-5Ah the limit's rise, fall, speed and offset.

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
  card, the bit holds the S out under a level, 5.5 dB over the M with ESS's
  values and an M at -24 dBFS, so a program that is already wide gets less
  boost, and one that is wider still gets none. With S and no M, it turns
  the boost off. Most of the level is a ratio to the M, which is what the
  AutoSpace mode of Desper's patent does: it holds the enhancement at a
  constant ratio to the program. The rest is a fixed level, so quiet sound
  keeps more width than loud. The limit follows the S through a high-pass,
  so it hardly acts on bass: with a fast limit it holds the S at +5.3 to
  +5.6 dB from 1 to 2.5 kHz, at +6.6 dB at 630 Hz, +8.4 at 400 Hz and +11.2
  at 250 Hz, and not at all at 160 Hz and under.
* **58h, the limit's speed.** AutoSpace follows the program through filters
  with "adjustable rise and fall ballistics". On the card, the limit moves
  the boost in a straight line in dB, down while the S is over the level and
  up once it is under, from the moment the S crosses it. Bits 3:0 of 58h, n,
  set the fall, 257.5 / (n + 1) dB a second, and bits 7:4, h, make the rise
  h + 1 times slower. With ESS's 94h, n is 4 and h is 9: the boost falls at
  51.5 dB a second and rises at 5.15, so after a wide passage the effect
  takes about 3 seconds to come back from 14 dB down. With 58h at 00h both
  are 257.5 dB a second, and at FFh 16 and 1. All 14 values of 58h that the
  card was measured with fit these to within 2 percent.
* **54h and 56h, the limit's two levels.** The drivers that write them
  always write 8Fh and 95h, at the place where the ES938 gets its fixed
  settings. On the card, with the limit off, neither changes the effect at
  any frequency, with any bit flipped or at either end, so they don't shape
  its filters. With the limit on, each sets a level of the S out, K M + D,
  where M is the M in. K is 1.643, +4.3 dB, with the register's bit 7 set,
  and 0.228, -12.8 dB, with it clear. D is a fixed part, the register's bits
  6:0 times 5Ah / 80h, each step 0.0112 of an M at -24 dBFS, or about -63
  dBFS. The boost falls while the S out is over both levels, and rises while
  it is under 54h's, which the rise takes about 0.7 dB higher. So the boost
  settles at the higher of the two levels. Where 56h's is the higher by more
  than that, a passage whose S drops to between the two leaves the boost
  where it is. With ESS's values and an M at -24 dBFS, 54h's level is +5.2
  dB over the M and 56h's +5.5, and the boost settles at +5.5. The model
  matches every setting that the card was measured with to within 0.2 dB,
  and 0.06 dB on average:

  | 54h | 56h | 5Ah | M, dBFS | Held, dB over the M | Model |
  |-----|-----|-----|---------|---------------------|-------|
  | 8Fh | 95h | 80h | -24     | +5.5                | +5.5  |
  | 8Fh | 95h | 80h | -36     | +8.3                | +8.2  |
  | 8Fh | 95h | 00h | -24     | +4.2                | +4.3  |
  | 8Fh | 95h | FFh | -24     | +6.4                | +6.5  |
  | CFh | 95h | 80h | -24     | +8.1                | +8.0  |
  | FFh | 95h | 80h | -24     | +9.8                | +9.7  |
  | 8Fh | FFh | 80h | -24     | +9.7                | +9.7  |
  | 9Eh | AAh | 80h | -24     | +6.4                | +6.5  |
  | 0Fh | 15h | 80h | -24     | -6.6                | -6.7  |
  | 7Fh | 7Fh | 80h | -24     | +4.3                | +4.3  |

* **5Ah, the scale of the fixed parts.** It multiplies the fixed part of
  both levels by its value over 80h. At 00h the levels have no fixed part
  and are ratios to the M alone, 4.3 dB over it with both bits 7 set, and at
  FFh the fixed parts are twice ESS's. With ESS's values, the fixed part
  adds 1.2 dB to the level with an M at -24 dBFS and 3.9 dB at -36, and
  under about -50 dBFS the boost keeps its full gain even at 0 dB S/M.
* **The sign bits.** Bit 7 of 54h and of 56h sets the part of its level that
  follows the M. With 54h's clear alone, 54h's level drops to 8 dB under the
  M, so once the limit has held a passage, the boost rises again only after
  the S out drops under that: a step 12 dB down left it down. With 56h's
  clear alone, 54h's level holds the S, 0.3 dB lower with ESS's values. With
  both clear, the limit holds the S out at -6.6 dB against the M with ESS's
  bits 6:0, at +4.3 dB with both registers at 7Fh, and with both at 00h it
  turns the boost off even at -12 dB S/M. Bit 7 of 58h is part of its rise.
* **54h, 56h and 58h as one vector.** As signed numbers with bit 7 for plus,
  or as the byte minus 80h, ESS's values are +15, +21 and +20, and 5Ah's 80h
  is 0. The three don't act as one vector, though. With the limit off,
  nothing changes whatever the vector. With it on, 54h and 56h are two
  levels of which the higher counts, 58h is two rates, and halving, doubling
  or turning the three together does what the formulas of each predict.

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
WordPad.

`ess3d measure window` tests the two levels in about 3.5 minutes, with step
runs that have the limit on:
* ESS's values with every tone 6, 12 and 18 dB lower (*L -6 dB* to *L -18
  dB*), for the fixed part.
* 5Ah at 00h, 40h and 7Fh, and at 00h with every tone 12 dB lower, and 54h
  at FFh with 5Ah at 00h, which should have no fixed part.
* 54h at AFh, at two levels of the M, and 54h and 56h at 80h, at two levels,
  which should have no fixed part either.
* 54h and 56h both at AAh, and each alone at AAh with the other at 80h,
  which should hold the same level if the higher one counts.
* The *W* runs, which step the S back to only 3 dB under the M. With ESS's
  values, and with 54h's level over 56h's, the boost should rise back to
  where it was held. With 56h's level well over 54h's, it should stay.
* 54h and 56h with both sign bits clear at 7Fh and 00h, 00h and 7Fh, and 3Fh
  each.

`ess3d measure quick` runs each kind once or twice in about 2 minutes, and
`ess3d measure 58` runs 58h from 00h to FFh in steps of 10h with the limit
on. `ess3d measure quick /sim` shows what a report looks like without the
card, from an effect in `src/s3dsim.c` that follows this card's measurement
and is made up in between.

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
  before it rises (*hold*), how fast it rises then, its gain at the end, and
  the S out where it stopped rising (*rose to*). That is the lower of the
  two levels, or `<` the S out where it didn't rise at all, or `>` the S out
  where it rose to its full gain or was still rising. The rates come from a
  straight line through the windows on the way. The *fall*, *rise* and
  *held* lines name the runs at both ends of each, which shows the register
  that sets it.
* The *levels* table takes the runs that have the same registers with the M
  at different levels, and fits their held S out to K M + D: K in dB over
  the M, and D in dBFS, with a dash for a part that moves the level by less
  than 0.25 dB.

Every run with the limit off also checks that each of its tones gives the
same amplitude and phase in both halves of its window. A window whose halves
differ means that the recording or the playback skipped there. In a step
run, the limit moves the S out by at most 5.2 dB in 20 ms, so a window that
jumps further from the one before means that the step came early or late,
after a skip. ess3d measures such a run again, logs it in `ESS3D.LOG`, and
if it skips again, the report says from which window on its values may be
off. One run of the card's second measurement and one of its third had such
a skip, before these checks existed.

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
4. Measure with `ess3d measure`, `ess3d measure limit` and
   `ess3d measure window`, and keep the reports.

## What one card showed

The card is an ES1869 under Windows 98 with the rebuilt drivers. It was
measured four times: with the full plan of an earlier version of ess3d, with
the limit plan and the full plan of the next, and with the limit plan once
more, with runs added for the sign bits, the M's level and the band with a
fast limit.

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
and clearing its bit 4, which ESS's value has set, lowered it by 0.2 dB.
Their low bits and 5Ah's moved it by tenths of a dB. This looked like the
two registers adding, until the third measurement showed that the higher
level counts ([the table
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

### The third limit plan

The limit plan again gave the same values to within 0.2 dB, run for run, and
58h's rates fit their formula as before. The runs added to it answer the
open questions:

**The level and the M.** With every tone 12 dB lower, the limit held the S
out at +8.3 dB against the M instead of +5.5, so the S out fell by only 9.2
dB. A level of 1.643 M plus a fixed 0.240 of the M at -24 dBFS fits both.

**54h and 56h don't add.** *vec x2*, with 54h at 9Eh and 56h at AAh, held
+6.4 dB in both limit plans, where adding 30 and 42, as the earlier formula
did, gives +6.8, and the higher of the two alone gives +6.5. Over the 82
runs of both limit plans with both sign bits set, a level of 1.643 M, plus
0.01116 of the M at -24 dBFS times 5Ah / 80h times the higher of the two
registers' bits 6:0, matches every run to within 0.18 dB, 0.06 dB on
average. Its part that follows the M, 1.643 or +4.3 dB, comes out the same
as from the runs 12 dB apart, and with 5Ah at 00h, which leaves no fixed
part, the limit held the S at +4.2 dB.

**The sign bits.** Clearing bit 7 of 54h and 56h together held the S at -6.6
dB against the M, as with all three cleared, while clearing 58h's along with
either one changed only the rise. With both bits clear, 54h and 56h at 7Fh
held the S at +4.3 dB, and at 00h the boost was off even at -12 dB S/M. Both
fit a level that follows the M at -12.8 dB, plus the same fixed part.

**The rise.** In run 32, 58h at 90h, the fall of 257.5 dB a second overshot
to +5.0 dB, under 54h's level of +5.2, and the boost rose back to +5.5 dB,
56h's level. In *vec neg* it rose from off and stopped at -7.3 dB, 0.7 dB
over 54h's level of -8.0 there and under 56h's of -6.7. A rise that compares
with 54h's level made 0.7 dB higher fits both. The overshoot also shows that
the limit's detector is about 4 ms late.

**The band with a fast limit.** With 58h at 00h, the S out held at +11.2 dB
at 250 Hz, +8.4 at 400 Hz, +6.6 at 630 Hz and +5.3 to +5.6 from 1 to 2.5
kHz, while 100 Hz, 160 Hz and 4 kHz, where the boost alone gives +5.9,
weren't limited at all. The limit follows the S through a first-order
high-pass near 400 Hz, and a little less of it over 2.5 kHz.

**A skip.** In run 40 the step down came about 85 ms late, so its first four
windows read the boost 2.4 dB over its full gain. ess3d now checks the step
runs for such jumps and measures the run again.

`ess3d measure window` tests the model's predictions: K M + D at more levels
of the M, D gone with 5Ah at 00h at any level, the higher of 54h and 56h
against their sum, and the boost staying where it is after a small drop of
the S when 56h's level is well over 54h's.

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
