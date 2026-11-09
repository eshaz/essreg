# The clock correction

The ES1869 has one clock input and no trim, so a stream's rate can only be
corrected in its samples. snd-es1869's Clock mode resamples Audio 2's stream
so that it plays true to the clock that chronyd steers onto real time.
`es1869scope -E` measures what the stream then does from outside the driver,
and trims what the driver's own fit misses. This page describes both, and
reports what the Armada showed on 2026-11-08.
[LINUX.md](LINUX.md#rate-correction) describes the driver's settings, and
[LOOP_TRANSFORM.md](LOOP_TRANSFORM.md) the measurement of the DAC that -E
runs beside this one.

## The clocks

Three clocks take part:
* **The chip's master** runs at 23.814 MHz, from the board's 14.318 MHz
  crystal through a ratio of 800/481. 44.1 kHz is the master over 540, and
  Audio 2 at 2x runs its DAC at the master over 270.
* **The raw clock** (`CLOCK_MONOTONIC_RAW`) counts the same crystal, so the
  chip holds a fixed +0.873 ppm against it, the rounding of 800/481. That
  was measured on 2026-11-03 and held through the hour of 2026-11-08 to a
  few ppb, both with the ACPI PM timer as the clock source. Booted with
  `processor.max_cstate=1`, the Armada takes the CPU's time stamp counter
  instead, whose rate the kernel calibrates at boot, and on 2026-11-09 the
  chip read +0.490 ppm against that raw clock over an hour. The driver's
  fits follow whichever clock the system has.
* **The steered clock** (`CLOCK_MONOTONIC` and `CLOCK_BOOTTIME`) is the raw
  clock with chronyd's correction, so its rate against the raw clock is
  chrony's frequency, about 29 to 31 ppm on the Armada. That frequency moves
  with the board's temperature, by about 0.23 ppm a degree and an hour late
  ([LINUX.md](LINUX.md#rate-correction)).

Uncorrected, 44.1 kHz therefore plays about 28.7 ppm slow of real time on
the Armada.

## The Clock mode

With `a2_resample` at 2, the driver's resampler takes Audio 2's stream to
its ring at a step of one plus the ratio it applies, which
`/proc/asound/card0/es1869` shows as "Audio 2's stream takes X ppb more of
itself a frame". The ratio is the negative of the chip's error against the
steered clock, plus `a2_trim_ppb`.

The driver measures that error as a chain of two fits
(`linux/es1869/resample.c`), each a line by recursive least squares whose
gains are a true least-squares line's until a limit of samples, and a line
that forgets from there:
* **The chip against the raw clock.** Every stream interrupt is a known
  count of master ticks, and the handler stamps it with the raw clock as it
  begins. The earliest interrupt of each group of about a second (2^30 ns)
  is the sample, since interrupts only ever come late. The line keeps 256
  samples, and a sample over 500 us off it stands out, until three in a row
  move the line as a slip of the stream.
* **The raw clock against the steered one.** Each sample of the first fit
  reads both system clocks together, and this line keeps 1024 samples, about
  a quarter of an hour, so that chronyd's corrections of its own offset
  don't reach the pitch.

The two are measured apart and nothing feeds back from the resampler into
either, so the correction can't hunt. Audio 2 feeds the first fit while it
plays and Audio 1 otherwise, and a change of source or a new stream starts a
new stretch that keeps the slopes.

## Measured from outside

`es1869scope -E` reads the monitor, PCM device 3, which delivers the frames
Audio 2's DMA read from the ring. The driver reads the 8237 for the
monitor's position right before the ALSA core stamps it, so each status call
gives a frame count and its time on `CLOCK_MONOTONIC`. The chip's rate on
the steered clock is the slope of those positions:
* A line goes through each run of the music, from a break to the next, after
  the run's first second, which may still hold what the monitor took up
  behind the DMA. The positions are kept less where the nominal rate would
  have them, so the sums stay small.
* The runs' lines are pooled by their own means over the block, at least 256
  positions, which gives the block's rate in ppm against nominal.
* The scatter of the positions about the lines is the `tie` of the log,
  about 5.5 us rms on the Armada. It is the DMA's steps through the chip's
  FIFO and the stamps' own timing, which hide the crystal's jitter.

Audio 1's position would not do, because with its correction filter on, the
capture pointer moves a period at a time.

With the ratio the driver applies, read from its /proc page at the block's
end, the chip's rate gives the stream's residual against the steered clock:
the chip's error plus the applied ratio. Less the trim, the residual is the
driver's own error, the bias. -E keeps the bias's trend, a line through its
last 16 blocks, and predicts it a block ahead. `a2_trim_ppb` takes the
prediction back whole, 0.5 ppm a block at most, and moves only for 10 ppb or
more. A block's chip rate 0.5 ppm off its own trend, as a stop of the DAC
makes one, is left out until a second block agrees with it.

The bias is modelled instead of the residual because the residual contains
the trim's own steps. A trend through the residual sees each step as a
trend, extrapolates it and overshoots: on the Armada that version swung the
trim from -0.65 to +0.23 ppm over eight blocks while the driver's error
stayed near +1.2 ppm, and then stalled with 0.25 ppm left.

## On the Armada

Measured on 2026-11-08 with test build 4, program noise on Audio 2 at 2x in
the Clock mode, and 20-second blocks:

| Figure                             | Value                           |
|------------------------------------|---------------------------------|
| chip on the steered clock, a block | -28.73 ppm, 0.02 between blocks |
| chip on the raw clock, from it     | +0.87 ppm, with chrony's 29.60  |
| time error of the positions        | 5.5 to 6.6 us rms               |
| the stream with the trim           | within 0.05 ppm, mostly 0.02    |
| the driver's own error, steady     | -0.04 to -0.28 ppm              |
| the driver's own error, at 20:00   | +1.2 to +1.4 ppm                |

The chip's rate on the steered clock is the Nov 3 relation exactly: +0.873
ppm on the raw clock less chrony's 29.60 ppm.

From at least 20:00 until 20:15 the driver's own error sat at +1.0 to +1.4
ppm. The /proc page at 20:02 read the master at -350 ppb on the raw clock,
where it holds +873 ppb, while the fit of the steered clock read chrony's
frequency within 0.03 ppm. So the error was in the fit of the chip. Over one
unbroken stream from 20:14 it fell from +1.19 ppm to zero in about six
minutes, by two thirds every three minutes, as a line of 256 samples
recovers from a disturbance. A fresh stream at 20:23 started at -0.04 ppm.

What disturbed it is not known. Two causes are ruled out:
* **Restarts of the stream.** A replay of the driver's fit through restarts
  every 16.5 s, with the first interrupt of each up to 0.4 ms early, stayed
  within 20 ppb of the truth.
* **A faulty build of es1869scope.** That evening it read the monitor in a
  loop that never ended, at 82 % of the CPU in the kernel, from 19:42 to
  19:49 and from 20:01 to 20:03, so the interrupts' stamps could have come
  late by an amount that changed with the load. On 2026-11-09 the same loop
  ran on purpose for 5 minutes beside one stream in the Clock mode, with the
  fit read every 10 s. The fit stood at +0.483 ppm before it, on that boot's
  raw clock, rose 20 ppb during it and fell 30 ppb after it. The interrupts
  came earlier during the spin, a median of 3 us late against 26 us before,
  since the CPU never sat idle. The same run on a boot with C2 held the fit
  within 30 ppb of +0.873 ppm.

es1869test's `rs.clock.long44` had earlier read 0.197 ppm over half an hour,
against its limit of 0.1, under its own loads.

The trim follows such an error within a few blocks, since -E measures the
chip on the steered clock directly in every block. Without -E, the stream
carries the driver's error until its fits recover.

The same evening's hour of es1869clock, with a pilot from Audio 1 into the
VX, gave the ES1869's crystal against the VX's an Allan deviation of 1.0e-8
at 1 s and 2.5e-8 at 10 s ([ES1869SCOPE.md](ES1869SCOPE.md#on-the-armada)).
That is the crystals' short-term stability, which no correction of the rate
changes.

## What it doesn't correct

The correction is one of the rate. Jitter is in the DAC's clock at the
moment of each conversion, after all of this, and resampling the stream
can't move it. The loop of [LOOP_TRANSFORM.md](LOOP_TRANSFORM.md) can't see
it either, because Audio 1's ADC runs off the same crystal: what one
converter gains, the other gains too. Only a second clock shows it, as the
VX's crystal does through es1869clock's pilot, to about 2 ns a tenth of a
second.

## Open questions

* What disturbed the chip's fit on 2026-11-08. The faulty read loop, run on
  purpose, moved it by 30 ppb at most with C2 and without.
* The fit of the steered clock lags chrony's frequency by about 20 minutes
  by design, so that chronyd's offset corrections don't reach the pitch. A
  faster fit would follow temperature sooner but carry more of those
  corrections, and with es1869eq running the trim covers both.
