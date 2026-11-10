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
on the left lay 8 dB lower, while its right was as before. At 12:14, after a
reboot, four takes found the left as at 09:48 again, the line at -98 dBFS
and Audio 1's own noise within 0.5 dB of the table, and the DAC's still
under it. So how much of that source the record path takes changes over
time, and with it how much of the loop's noise is the source's. The fan was
off and the battery full each time.

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

A tone added through Audio 2 tested it, 0.025 Hz off the line at the level
the line out has it, so that the two beat once in 40 s. Recorded to
/dev/shm, two takes held the line's phase on the DAC's clock and gave Audio
1's prediction of the line out's line within 3 to 16 %, through the ratio
taken from the 1.4 s of silence before the tone, which is 16 to 31 dB of
cancellation. At the line out the sum fell 5 to 10 dB under the line alone
as the beat passed, no further: the tone came out 3.6 dB stronger than the
line. At 2 to 5 LSB, the tone's level through the treble correction wasn't
proportional to the file's, with the ring's rounding dithered or not, so a
cancellation would make its tone in the driver and set it from what Audio 1
hears, a loop that corrects its own level, rather than from a table.

## The DAC's gap at its sign

Audio 2's DAC puts its codes from 0 up about 8 LSB higher than those below,
a gap between -1 and 0 where its sign changes, the error an R-2R ladder
makes at its largest carry. Fitted as a function of the code, what the loop
and the VX heard of the program (`residual`, both 1x takes of 2026-11-09)
less the paths' response to the codes stepped from about -3 LSB below 0 to
about +4 from 0 up, flat on each side to 64 LSB, 7.6 and 8.0 LSB apart on
the loop's sides and alike on the line out's within their noise. Audio 1
adds an S-curve of its own, about 3 LSB at 4096, which the VX doesn't hear;
that one is the record path's and isn't taken back. The gap adds about 2/pi
of itself to every small signal's fundamental, which is what made the beat
test's tones of 2.4 and 5.4 LSB come out 8.7 and 5.1 dB louder than the
path.

The driver takes it back: with `a2_zstep` on, each code goes to the ring
moved about the gap, `a2_zstep_l` and `a2_zstep_r` of it in 1/256 LSB. A
sample more than half the gap from 0 then comes out where it asks, within
half an LSB, and one within that goes to the gap's nearer edge, since no
code reaches inside it. The monitor reads the codes moved, which are what
the DAC takes.

On the Armada (2026-11-09, test build be8c00c), `lsb` and `levels` played
through the correction, heard at once by the VX on the line out and by the
loop, in the plain copy at 1x and in the Clock mode at 2x alike. With the
correction off, the loop heard the squares of 1, 2 and 4 LSB about 0 come
out 10.3, 5.9 and 2.4 dB over that of 8 LSB, a gap of 7.5 to 7.7 LSB, and
the VX's gains at -90 to -60 dBFS against -30 dBFS's, 13.3, 8.1, 3.4 and 1.2
dB, matched those of a gap of 8.0 in a simulation of the signal. With the
correction at 7.6 LSB, the squares of 8 and 4 LSB came back to the path's
gain, -1.56 and -1.27 dB on the loop where the simulation has -1.58 and
-1.37, and those of 1 and 2 LSB, which lie within the gap, went to its
edges. The tone's harmonics on the line out, H2 to H5 under the tone on the
VX's left side in the Clock mode at 2x, two takes each:

| Tone     | Off      | 7.6 LSB  | 8.0 LSB  |
|----------|----------|----------|----------|
| -60 dBFS | -25.9 dB | -36.1 dB | -40.3 dB |
| -50 dBFS | -34.4 dB | -45.8 dB | -49.0 dB |
| -40 dBFS | -43.9 dB | -54.8 dB | -56.2 dB |

The right side came within 1.5 dB of these, and the plain copy at 1x within
3 dB. The gain at -60 dBFS came within 0.1 dB of -30 dBFS's, from 1.2 dB
over. A tone under about -75 dBFS lies within the gap, where no code
reaches, and stays as loud as before: at -80 dBFS it came out 4 to 6 dB over
the path, from 8 dB.

The loop can't learn the gap from the music. Audio 1's record path has a
soft dead zone of its own about its zero, which falls within the DAC's gap.
What the loop heard of the program, less its linear part, steps by about 5.5
LSB at the codes' sign as the gap does, but comes back within some 100 LSB
either side, where the VX's stays. Once the music swings past that knee, the
dead zone takes back what the gap puts in. -E's fit of the step through
Audio 1 (below) read -0.5 and +0.9 LSB over 160 s of program noise, and a
copy of it in Python read the same in every band from 30 Hz to 19 kHz, where
it reads 7.0 on a simulated loop with a gap of 7. Tones agree: through the
loop, -60 and -50 dBFS read 0.8 and 0.3 dB under the VX's against -30 dBFS,
with the correction off and on alike, and the squares of 1 to 8 LSB, which
stay within the knee, show the gap about 5 % small. The gap comes from a
recorder on the line out, or from small signals through the loop with those
5 % allowed for. Smooth functions of the code beside the sign in the fit
(tanh of the code over 16 to 256 LSB) brought the loop's reading to 6.2 to
6.7 LSB, with a wide spread over the bins.

-E fits the step all the same, and logs it as the loop's. Each kept frame of
the monitor carries the share of the DAC's codes from 0 up that it stands
for, less a half, each side's sign, which goes into the segments as a third
input beside both sides' codes. The sign follows the music, so the three are
solved together in each bin; the sign's transfer over the side's own path is
the step. Low down the sign is mostly the music, so the step comes from 1 to
8 kHz, as the median over the bins, measured in each run and weighed into
the block by its segments. The fit sees the DAC's own codes, moved or not,
so the gap that it would find is the DAC's either way. On a simulated DAC
with a gap of 7.4 LSB, through a delayed lowpass and noise, it read 7.33 and
7.46 from 20 s of music.

## Audio 1's own curve

The record path has a curve of its own, which a recorder on the line out
doesn't hear. Fitted on the gap study's program takes (2026-11-09), against
what should have come back, the DAC's codes with their gap through the
loop's linear path, what Audio 1 added to a sample is a soft compression:
about +4.3 LSB for a value well under the centre and -4.3 well over it,
moving between the two over some 50 LSB either side, about a centre 14 to 18
LSB under the recording's mean. Small signals lose the most: a 997 Hz tone
of -60 dBFS came back 0.8 to 0.9 dB under the VX's against -30 dBFS, -50
dBFS 0.37 dB under, -40 dBFS 0.08. It also turns a small signal's phase
back, 0.5 to 0.8 degrees at -60 dBFS beyond the VX's own, 0.2 at -50: a
quadrature error of about 0.35 LSB whatever the level, as a little
hysteresis would make. The curve is deeper for quieter music, which a fixed
curve can't follow: one fitted to the loud program takes -50 dBFS back
within 0.05 dB of the VX but leaves -0.3 to -0.6 dB at -60.

snd-es1869 takes it back with `a1c_linear`, a stage after Audio 1's
correction filter in the same pass (capfilt.c). A curve about each side's
centre adds what the path took, between knots at 0, 2, 4, 6, 8, 12, 16 and
on in half octaves to 512 LSB. Then the analytic signal, from a Hilbert FIR
of 31 taps on the filter's own window, gives the envelope and the
instantaneous frequency, which pick a gain and a turn of the phase from
tables of 4 bands by 10 envelope bins an octave apart. The frames come 16
late, as through the filter.

-E learns the tables from the music. Audio 1 and Audio 2 run from one
crystal, so what came back sits at a fixed place against what the DAC took
for the whole of a run, and what should have come back is one linear
operator on the DAC's frames: every frame of the monitor, at its own rate
(2x included), each code with the DAC's gap from 0 up, low-passed to Audio
1's grid in the spectrum, through the run's path, fitted by Welch. Under 32
Hz, where a segment's path can't follow the AC coupling, the recording's own
spectrum stands in, its DC with it. The analytic signals of what should have
come and of what the stage gave come from the same spectra. From them:
* the path's curve, as what came back raw less what should have, against
  what should have, which carries no noise, so the ADC's noise gives the fit
  no bias; the stage's curve is its inverse at each raw knot, in one step;
* the gain and phase the stage leaves, by its own band and envelope, which
  the next tables take back, the turn by a Newton step on how far a turn
  moves each bin's phase, measured, since the short FIR hardly turns the
  bass while it turns the treble riding on it whole. A bin moves only with a
  coherence of 0.8, so that silence, where what came back is the ADC's
  noise, moves nothing.

Replayed over the gap study's takes at 1x and 2x, the path's curve comes out
alike in both, and what the stage leaves falls from 4.5 LSB to about 1 over
three passes of 20 s. The loud program has no quiet passages, so the
envelope's bins at -60 dBFS get nothing from it; music with quiet passages
fills them, and the chip's tones show what they take back.

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
* Music at 48 kHz is measured at 48 kHz but not counted, because the record
  path was calibrated at 44.1 kHz.
* The DAC's gap at its sign isn't learned: Audio 1's own soft dead zone
  takes it back once the music swings past its knee ([The DAC's gap at its
  sign](#the-dacs-gap-at-its-sign)).
* Random noise can't be predicted, so nothing added before the DAC removes
  it. What could be cancelled is deterministic, and the loop shows little of
  it: the DAC's own noise lies far under the record path's, and what the
  music adds is the record path's own. The one steady line both recorders
  heard, 15625 Hz, could be followed from Audio 1 and cancelled at the line
  out, with the ratio between the two set by a recorder on the line out
  whenever it moves ([The DAC apart from the
  ADC](#the-dac-apart-from-the-adc)).
