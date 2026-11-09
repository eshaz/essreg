# The loop transform

es1869scope measures Audio 2's DAC through the chip's own record path while
music plays: what the DAC took, read back from the driver, against what
Audio 1 recorded of it. This page describes that transform from end to end,
and every comparison built on it, as `es1869scope -E` runs it beside MPD
(`linux/tools/es1869scope/loopview.c` and `eq.c`).
[ES1869SCOPE.md](ES1869SCOPE.md) is the guide to the program and its
services, and [CLOCK_CORRECTION.md](CLOCK_CORRECTION.md) covers the clock,
which -E measures and trims beside the DAC.

## The two ends

The input is the monitor, PCM device 3. The driver delivers there the frames
Audio 2's DMA read from its ring, so they carry everything the driver did to
the stream: the rate correction, the treble correction's table and the
oversampling. At 2x the ring runs at 88.2 kHz, and the view takes every
other frame.

The output is Audio 1's recording at 44.1 kHz, S16 stereo. The record mixer
takes Audio 2 through 69h ("PCM Capture Volume" 14, "Capture Volume"
3) with every other input at 0, which gives a chain of -1.8 dB that stays
   clean to -0.5 dBFS ([TEST_LEVELS.md](TEST_LEVELS.md)). Audio 1's
   correction filter, which es1869cal fits at boot, flattens the record path
   to 20 kHz.

Audio 1 and Audio 2 run from one crystal, so they never drift apart. Each
start puts them a different fraction of a frame apart, though, and at 1x the
ADC samples in step with the DAC and folds the DAC's images back into the
band, so the top of the response moves with that fraction. At 2x the images
lie far above the band, and 3x stalls the DAC while Audio 1 records, so -E
holds Audio 2 at 2x.

## The lag

The lag between the two streams comes from the newest 32768 frames of each,
so it doesn't matter when either began. A coarse search correlates sums of 8
frames by FFT, and a fine one tries 16 frames either way at the full rate. A
lag counts when the correlation there reaches 0.3 of what a perfect match
would give. The search runs again after a break of the music, after a
restart of Audio 1, and when the segments stop overlaying (see [The
overlay](#the-overlay)).

## The transform

The streams go through Welch's method:
* **Segments.** 8192 frames each, half overlapping, under a Hann window,
  which at 44.1 kHz gives bins 5.4 Hz apart.
* **Spectra.** One complex FFT carries two real segments, the left channel
  as the real part and the right as the imaginary, which symmetry splits
  afterwards. Two FFTs a segment cover both channels of the input and the
  output.
* **Sums.** Each bin sums the inputs' and outputs' powers and their cross
  spectra, for four paths (each output from each input) and the inputs'
  cross-spectrum.

The four paths are solved together, as a 2 x 2 system in each bin against
the inputs' cross-spectrum. Music correlates the channels, and even
independent noise correlates a little over a finite time, so taken one by
one the crosstalk would read that correlation: about -16 dB after 4 s of
noise, where the solve reads the path's -66 dB. Where the inputs are too
alike to solve, a bin falls back to each output from its own input.

From the solved paths come, for each output:
* the magnitude against 1 kHz, in dB;
* the phase, less a delay fitted between 200 Hz and 3 kHz;
* the multiple coherence, the share of the output that both inputs explain;
* the impulse response, by the inverse FFT, 32 taps either side of 0.

More resolution would not help here. The DAC's response is smooth, and the
variance of an estimate depends on how much time and bandwidth it averages,
not on the width of a bin. The cost of a segment grows only with the log of
its length, so the length is set by memory and by how soon a block ends.

## The overlay

Each segment is checked before it counts, by the Pearson correlation of its
input against its output, both channels summed, at the lag and at a frame
either way:
* A segment counts when the correlation at the lag reaches 0.5.
* Eight in a row that don't end the run, and the lag is found again.
* A quiet segment, under 3 steps rms, says nothing either way.
* The parabola through the three correlations gives the delay between
  frames, segment by segment, which the log reports a block at a time.

A stop of the DAC or a lost block moves one stream against the other, and
the segments after it no longer overlay. On the Armada one such block,
before this check, read the DAC at 7.7 kHz 0.9 dB below the blocks around
it, and a refit followed it. With the check, the overlay held its
correlation at 0.995 and its delay at +0.10 frame with a spread of 0.01
frame, block after block.

Within the loop the delay holds still, because the two converters share a
crystal. Against a second card's clock the same correlation would move with
the drift between the two crystals.

## Runs and blocks

A run is the time between breaks: a pause, a stop or the next file of the
music, which stop the monitor; a restart of Audio 1; or a lost overlay. Each
start puts the DAC and the ADC another fraction of a frame apart, and the
monitor's every other frame at 2x half a frame either way. So the cross
spectra of two runs differ in phase at the top, by up to 80 degrees at 20
kHz, and their sum cancels: mixed, they took up to 2.3 dB off 20 kHz on the
Armada. So each run is measured alone:
* At a run's end, its sums give its figures, and each of 24 bands from 250
  Hz to 20 kHz takes the mean of its bins' magnitudes over both channels.
* Each bin weighs by the lower of the two channels' coherences, and a bin
  under 0.5 is left out, since music may leave a band empty.
* A band's value goes into the block weighted by the run's segments, and a
  run of under 32 segments is left out.

A block is `-T` seconds of measured segments, a minute in the service,
however broken the music was. A change of the DAC's rate starts a block
afresh.

## The noise

What came back that the music, through the measured loop, doesn't explain is
each output's power times one less its multiple coherence. The block reports
it in each band as a density, in dB a Hz against a full-scale sine, the
band's power over its width, where Hann's window keeps 3/8 of a segment's
power and the bins sum one side of the spectrum.

It holds the ADC's noise, the DAC's and the record path's, the music's
distortion and any interference, which the loop can't tell apart. On the
Armada it lay at -115 to -120 dB a Hz across the band. The top band reads
about -106. It reaches to 21.9 kHz, into the band where at 2x the monitor's
every other frame and the ADC fold the resampler's images back in different
ways.

### The DAC apart from the ADC

A second recorder on the line out tells them apart: each recorder's own
noise is its own, and only what the DAC put out reaches both. On 2026-11-09
the VX recorded the line out while Audio 1 recorded the loop and Audio 2
played `residual`, 20 s of program and then 20 s of digital silence
([TEST_SIGNALS.md](TEST_SIGNALS.md#analyzing)). sigan.py took the two
recordings together, three takes at 1x and three at 2x, and gave each part
in dB a Hz at Audio 2's input:

| Octave  | The DAC, in the silence | Audio 1's own | The VX's own |
|---------|-------------------------|---------------|--------------|
| 31.5 Hz | under -113              | -104          | -89          |
| 125 Hz  | under -122              | -108          | -97          |
| 1 kHz   | under -132              | -116          | -102         |
| 4 kHz   | under -137              | -118          | -104         |
| 16 kHz  | under -140              | -116          | -108         |

The DAC's own noise stayed under twice its uncertainty in every octave, at
1x and at 2x alike. It lies at least 9 dB under Audio 1's own at 31.5 Hz, 16
dB at 1 kHz, 19 dB at 4 kHz and 24 dB at 16 kHz, so the noise that -E logs
is the record path's: the mixer's stage, the ADC and its correction filter.
With the music, Audio 1's own noise rose 1 to 2 dB over 1 kHz and what both
heard didn't, so that rise is the record path's too, distortion and noise
that follow the level.

Steady lines stood out of the silence:
* 15625 Hz, which both heard alike in every take, at -98 dBFS in Audio 1's
  recording and -80 in the VX's: about 19 dB more on the line out than in
  the record path, against the music's level in each. It lies within 0.13
  ppm of 15625 Hz on the system's crystal, so something clocked from that
  crystal makes it, and it joins mostly at the line out.
* Audio 1's own: 5512.5 Hz, an eighth of the rate, near -101 dBFS, and lines
  near -100 at 3458.8, 11025 and 16408 Hz.
* The VX's own: 64.6 Hz, and a comb of 750 Hz, its rate over 64, near -80
  dBFS.

Those takes ran at 09:48. At 11:16 the line out still carried the 15625 Hz
line at -81 dBFS, but Audio 1's left heard it 12 dB weaker and its own noise
on the left lay 8 dB lower, while its right was as before. So how much of
that source the record path takes changes over time, and with it how much of
the loop's noise is the source's.

Audio 1 can follow the line, since both recorders heard it alike: what the
line out carries is what Audio 1 hears times a ratio, and a tone added at
the ring through the line out's path could cancel it there. Over the six
takes in five minutes the ratio held within 0.15 dB and 7 degrees, which
allows some 15 to 20 dB of cancellation. By 11:16 it had moved 10 dB on the
left and 1.4 dB on the right, so a recorder on the line out would have to
set it again from time to time. A pause of the DAC's DMA, as the takes had
while their own recordings went to the disk
([TEST_SIGNALS.md](TEST_SIGNALS.md#analyzing)), moves such a tone's phase
against the line's, which keeps running, until Audio 1 measures it again.

## What the loop measures

Name the parts of the chain in the frequency domain:
* `C`, the treble correction's table, and `Filt`, the resampler's own
  filter, both applied before the ring;
* `D`, the DAC itself, its hold and the 0.062 of the last sample it keeps;
* `R`, the record path, and `F`, Audio 1's correction filter.

A listener hears `Filt C D`. The loop against the monitor measures `D R F`,
because the monitor already carries `Filt C`. A refit of `C` against that
loop would never see its own effect, and would climb to its limit. So the
absolute deviation is not what -E corrects.

es1869cal measures the loop against the noise it writes itself, before `C`,
so it sees `Filt C D R`, and fits `F` as its inverse. It assumes that
`Filt C D` is flat at that moment, which the treble correction was built
for. Through the VX, that held within 0.18 dB to 20 kHz at 1x and within
0.03 dB at 2x ([ES1869SCOPE.md](ES1869SCOPE.md#the-dac-and-the-adc-apart)).
After the calibration `Filt C D = 1`, so `F` is `1 / R` and the loop against
the monitor reads `D R F = D`, the DAC itself, as long as the record path
holds. The DAC's change since its first blocks is the measure of drift, and
`C` takes it back.

## The differential logic

Every correction -E makes answers a difference from where the system stood
when it began, measured in a way that the correction itself doesn't move:
1. **The start.** The first 3 blocks give each band's start, the mean of
   their values, when the calibration had just made the chain flat.
2. **The trend.** Each band keeps its last 16 blocks, and a line through
   them by least squares, predicted a block ahead. The correction lands a
   block later at the earliest, so it answers where the trend will be, not
   where the last block was. While a band has fewer than three blocks, its
   mean stands in.
3. **The drift.** The prediction less the start, once the trend has 3 blocks
   more than the start.
4. **The correction.** In each band the table's gain moves towards its start
   less the drift, 0.5 dB a block at most and within 3 dB of where it began,
   and nothing moves for less than 0.05 dB. The low end is held where it is,
   at 50, 100 and 150 Hz with a fifth of a band's weight.
5. **The fit.** The table's 16 taps each side, a stream frame apart, give a
   response of `1 + 2 sum s_k (cos(k w) - 1)`, whose gain at 0 Hz stays
   1. That is linear in the taps, and least squares over the bands is a 16 x
      16 system. Most bands lie low, where every tap's response looks alike,
      so the system alone has a condition of 1e16, and its taps would go
      anywhere along the directions the bands leave free, out of the
      driver's range of 2^30. A ridge of 1e-6 towards the taps before keeps
      them there: the bands are met as closely, and above the top band the
      response stays as it was. The driver swaps the new table in between
      the resampler's runs, under a playing stream.
6. **The clock.** The same differential holds for the clock: the residual
   less the trim is the driver's own error, which the trim takes back
   ([CLOCK_CORRECTION.md](CLOCK_CORRECTION.md#measured-from-outside)).

The line through 16 blocks follows a slow drift exactly, which is what it is
for, and overshoots a step. On the Armada the record path was tilted 0.5 dB
lower at 20 kHz halfway through a run. -E read the step as the DAC's (-0.50
dB at 20 kHz, -0.24 dB at 13.7 kHz) and took it into the correction in 5
blocks of 20 s: +0.11, +0.28, +0.40, +0.48 and +0.54 dB. It overshot to
+0.67 dB at 9 blocks and was back to +0.55 dB at 18, as the line came level.
Without the drift, the DAC at 20 kHz held to 0.01 dB from block to block.

## On the Celeron

A thread of its own reads both streams every 10 ms into rings of 131072
frames, so that the figures, a drawing or a block's refit, which can take
longer than Audio 1's buffer of 16384 frames lasts, hold up neither. A read
ends when it comes back short of a chunk: the monitor's position follows the
DMA as it is read, so a loop that read until there was nothing left would
never end, and it spun in the kernel at 82 % of the CPU until that was
found. The analysis takes one segment a step, and skips to the newest when
it falls four behind.

Beside a stream, -E takes about 45 % of the Armada's Celeron, 5 % of it the
reader. Its slowest steps, a block's figures and a drawing every fifth
block, took up to 0.4 s beside an unbroken stream with no frame lost. In a
run with 25 breaks of the music, steps of up to 0.7 s cost Audio 1 one
overrun, and the recording started again on its own.

## Limits

* The loop can't tell the DAC from the ADC: a drift of the record path reads
  as the DAC's. A second ADC on the same output can, as it did for the noise
  ([The DAC apart from the ADC](#the-dac-apart-from-the-adc)), but -E would
  need it beside the loop all the time, and the VX's capture loads the
  Celeron.
* Music at 48 kHz isn't followed, because Audio 1 records at 44.1 kHz.
* Random noise can't be predicted, so nothing added before the DAC removes
  it. What could be cancelled is deterministic, and the loop shows little of
  it: the DAC's own noise lies far under the record path's, and what the
  music adds is the record path's own. The one steady line both recorders
  heard, 15625 Hz, could be followed from Audio 1 and cancelled at the line
  out, with the ratio between the two set by a recorder on the line out
  whenever it moves ([The DAC apart from the
  ADC](#the-dac-apart-from-the-adc)).
