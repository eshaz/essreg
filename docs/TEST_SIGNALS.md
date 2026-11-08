# Test signals

These are WAV files for measuring the ES1869's audio paths on the Armada:
its responses in level and phase, its timebase, its levels and noise, its
distortion, and the internal loop that the live analysis and correction of
Audio 2 build on. `tools/testsig.py` makes every file from one table of
signals, `tools/sigan.py` analyzes a recording of any of them, and the
catalog below comes from the same table. [TEST_LEVELS.md](TEST_LEVELS.md)
says how to set each path's levels.

## Making the files

```
python3 tools/testsig.py                    every file, into out/testsig
python3 tools/testsig.py --only sweep cal   two signals, all their files
python3 tools/testsig.py --only 'noise-*' --rate 32000
python3 tools/testsig.py --side r --only steps-44k
python3 tools/testsig.py --list
```

The default set is about a hundred files and 180 MB, a third of it the
one-side variants, and no file lasts over 32 s; `--only` makes or copies a
part of it. A file's name is the signal's, the side's for a variant, and the
rate's, as in `sweep-44k` or `sweep-l-44k`. `--rate` makes the chosen
signals at another rate and `--side` their left or right side alone.

Every run makes the same bytes, since the noise comes from numpy's legacy
generator with fixed seeds. The output directory also gets `manifest.json`,
with each file's rate, channels, frames, levels, SHA-256 and the layout that
its analysis needs, and `SHA256SUMS`. Copy the files to `/var/tmp/testsig`
on the Armada, whose `/tmp` is a 48 MB tmpfs, and check them there with
`sha256sum -c SHA256SUMS`. sigan.py makes the signal again for each
analysis. A file from another numpy, whose sums differ, goes to the analysis
with `--sig FILE`.

## How the signals are made

**Levels** are AES17 dBFS: a sine's peak, or the rms of anything else plus 3
dB, so that a full-scale sine reads 0. Most signals sit at -20 dBFS. A
signal whose true peak, four times oversampled, reaches -6 dBFS or more is
hot and has `hot` in its name; play a hot one only into a line or Aux input
with the speakers muted.

**Markers** start and end each file: a chirp from 500 Hz to 4 kHz in 0.1 s
at -20 dBFS, on the left channel, then the same on the right 0.15 s later. A
recorder that hears the sides' sum or difference still hears both, and the
gain of each chirp into each recorded channel shows what the recorder hears.
A file runs 0.2 s of silence, the start markers, 0.3 s of silence, the body,
0.3 s of silence, the end markers and 0.2 s of silence. The `chirps` and
`dual-skew` files are trains of such chirps, and `cal` has no markers, since
it is the boot calibration itself.

**Dither** is triangular, 1 LSB at its peak, the same on both sides where
both carry the same body. The exact patterns have none: the LSB squares, the
J-test, the DC steps, the clicks, the MLS and the pilots, whose tables of
whole cycles repeat sample for sample. A periodic signal's period is
quantized once and repeated, so its middle periods are the same samples.

**Rates** are 44.1 kHz for everything, 48 kHz for the main set, and 22050
and 11025 Hz for a few, Audio 1's lower rates. The chip's 44.1 kHz is exact,
but its 48 kHz is 768193.5 / 16 = 48012.09 Hz, 252 ppm high. A signal at 48
kHz keeps its whole cycles in frames, so `pilot-12000` repeats every 4
frames, but in Hz it sounds 252 ppm high: 12003.02 Hz. The analysis takes
every frequency at the player's measured clock.

**One-side variants**, `-l` and `-r`, play one side alone of a signal whose
two sides carry the same body. The table's Sides column lists those made by
default, at 44.1 kHz where the signal has that rate and at its own rate
otherwise, and `--side` makes any other. They serve a recorder that hears
one side, or the sum or difference of both, and a check of one channel. The
signals with independent sides (the noises, the multitones, `cal`,
`program`, `track`, `sweep-lr` and `polarity`) need none, since the analysis
tells each side's part apart.

## The paths

| Path | From             | To                    | Crystals   |
|------|------------------|-----------------------|------------|
| Loop | Audio 2          | Audio 1, record mixer | one        |
| Out  | the line out     | the VX's inputs       | two        |
| In   | the VX's outputs | Audio 1, Aux input    | two        |
| Dig  | Audio 2, FM      | PCM 1, the samples    | one, exact |
| Quad | the dual device  | the VX's inputs       | two        |

Each path measures the product of its stages. The loop is Audio 2's DAC, the
record mixer and the ADC. Out is the same DAC, the output mixer, the master
and the VX's ADC. In is the VX's DAC, the Aux input, the record mixer and
the ADC. Taking the VX's converters as flat, Out gives the ES1869's DAC and
output stage and In its Aux input and ADC. The loop over the product of the
two then leaves the record mixer's share against the output stage's and the
Aux input's, which no single path shows. The DAC and the ADC stay coupled in
the loop, and only the dig path gives Audio 2's samples alone, exactly. A
sweep through the VX's own output into its input, on a cable of its own,
would measure its converters instead of assuming them flat.

The loop runs on one crystal, so a periodic signal comes back exactly
periodic, but only with `a2_resample` at 0 (Off). In the Clock mode, the
default, Audio 2 is resampled to true time and the loop reads that
correction, about +29 ppm on the Armada, as a clock ratio: the analysis
takes it out, but a method that counts on exact periods doesn't.

### Setting up

Stop MPD's output with `mpc stop` first, and check that
`/proc/asound/ES1869/pcm0p/sub0/status` reads `closed`. Then, for the loop,
at its calibrated setting:

```
A='amixer -q -c ES1869 cset'
$A name='Audio 2 Rate Correction' Off
$A name='Master Playback Switch' off
$A name='Capture Source' Mix
$A name='Capture Switch' on
for c in Mic Line FM CD Aux Mono; do $A name="$c Capture Volume" 0; done
$A name='PCM Capture Volume' 14
$A name='Capture Volume' 3
```

For Out, the master goes on at 55 (-6 dB), Audio 2's level at 15 (0 dB) and
Audio 1's at 14 (0 dB), with the VX's Mic input at level 1:

```
$A name='Master Playback Switch' on
$A name='Master Playback Volume' 55
$A name='PCM Playback Volume',index=0 15
$A name='PCM Playback Volume',index=1 14
amixer -q -c VXPocket cset name='Capture Source' Mic
amixer -q -c VXPocket cset name='Mic Capture Volume' 1
```

For In, the record mixer takes the Aux input instead of Audio 2, and the
VX's master goes to -6 dB:

```
$A name='PCM Capture Volume' 0
$A name='Aux Capture Volume' 15
$A name='Capture Volume' 4
amixer -q -c VXPocket cset name='Master Playback Volume' 215
```

Before a path's first measurements, and after any change of its cabling,
play `separation` through it and then `levelcal-hot`, as
[TEST_LEVELS.md](TEST_LEVELS.md) describes. Keep the 3-D effect off
(`3D Control - Switch`): it changes the response on purpose.

### Playing and recording

Start the recording first and give it a second more than the file on each
end, since aplay and arecord start up to half a second apart. The devices:
* Audio 2 plays on `hw:CARD=ES1869,DEV=0,SUBDEV=0`, Audio 1 on
  `hw:CARD=ES1869,DEV=0,SUBDEV=1`, and the dual device on
  `hw:CARD=ES1869,DEV=2` with `dual_playback` at 1.
* Audio 1 records on `hw:CARD=ES1869,DEV=0`. It plays or records, not both.
* The VX plays and records on `plughw:CARD=VXPocket,DEV=0` at 44.1 or 48
  kHz.

```
cd /var/tmp/testsig
arecord -D hw:CARD=ES1869,DEV=0 -f S16_LE -r 44100 -c 2 -d 14 rec.wav &
sleep 1; aplay -D hw:CARD=ES1869,DEV=0,SUBDEV=0 sweep-44k.wav; wait
```

For Out, record with `arecord -D plughw:CARD=VXPocket,DEV=0` instead, and
for In, play with `aplay -D plughw:CARD=VXPocket,DEV=0`. The dig path plays
on Audio 2 with `Audio 2 To FM Playback Switch` on and records PCM device 1,
`hw:CARD=ES1869,DEV=1`, at 49716 Hz, a stream's sample a frame, the sides
swapped and the FM's idle level of +128 added
([ES1869TEST.md](ES1869TEST.md#the-paths)). Analyze it with `--rec-rate` set
to the file's rate; the markers undo the swap.

## Analyzing

```
python3 tools/sigan.py sweep-44k rec.wav -o out/sweep-loop
python3 tools/sigan.py noise-white-44k rec.wav --ref tap.wav
```

The analysis finds the signal anywhere in the recording by its markers,
searching all of it, so any start offset works. The distance between the
start and end markers gives the ratio of the player's clock to the
recorder's, reported in ppm and positive when the player runs fast. The
recording is then mapped onto the signal's frames at that ratio, by a
chirp-z transform that is exact for any ratio, up to the lower of the two
rates' bands. `cal`, which has no markers, is found by its correlation with
the signal and timed by its periods' drift.

The header gives the alignment, the clocks and each start marker's gain into
each recorded channel, and says what each recorded channel hears: one side,
the sum or the difference of the sides. A recorder whose channels hear the
other sides, as the VX into the Aux input does, is swapped back. Recorded
channels that carry one signal are analyzed once. A body that is the same on
both sides cancels in a recorder of their difference, and the analysis then
stops and says so.

Then each family reports what it measures, with every phase taken against
the start marker's time, which already holds the path's delay over the
marker's band; the response tables take the rest of the delay out of the
phase and give it. `-o PREFIX` writes the report to `PREFIX.txt` and the
detail to `PREFIX-*.csv`: responses on their full grids with phase and group
delay, impulse responses, harmonics, tones, timing and noise bands.

`--ref` adds a second recording made at the same time, such as the capture
of what Audio 2 played or a second card's, aligned on its own, and reports
Welch's transfers from it to the first. `--chans A,B` picks two channels of
a recording with more. The analysis needs numpy only.

A coherence estimate reads about 1/K for unrelated signals averaged over K
windows, which the report gives with each Welch analysis. A coherence that
falls with frequency while the level holds points at a clock that drifts
within the windows, which the mapping takes out when its ratio is right and
leaves when the clock wanders within the file: the noise analyses report the
delay over the file in blocks of 0.74 s for that. A sweep's response holds
from half an octave over its start; the 2 and 4 s sweeps are within 0.1 dB
under 50 Hz, the 10 s one to 0.01 dB.

## The catalog

The table gives each signal's rates as its files' names have them, its
length at the first of them, its level in dBFS and its default one-side
variants. A bullet then gives each signal's rates, length and level, what it
is, what its analysis gives and what a good result looks like.

<!-- signals -->

| Signal             | Rates           | Length | Level       | Sides |
|--------------------|-----------------|--------|-------------|-------|
| `sweep`            | 44k 48k 22k 11k | 11.5 s | -20         | l, r  |
| `sweep-quick`      | 44k             | 3.5 s  | -20         | l     |
| `sweep-low`        | 44k             | 11.5 s | -40         | l     |
| `sweep-hot`        | 44k             | 11.5 s | -3          | l     |
| `sweep-lr`         | 44k 48k         | 12.0 s | -20         |       |
| `sweep-hf`         | 44k 48k         | 9.5 s  | -20         | l, r  |
| `sweep-sub`        | 44k             | 13.5 s | -20         | l     |
| `multitone`        | 44k 48k         | 7.4 s  | -20         |       |
| `multitone-pink`   | 44k             | 7.4 s  | -20         |       |
| `noise-white`      | 44k 48k         | 11.5 s | -20         |       |
| `noise-pink`       | 44k             | 11.5 s | -20         |       |
| `noise-white-same` | 44k             | 11.5 s | -20         |       |
| `noise-pink-same`  | 44k             | 11.5 s | -20         |       |
| `mls`              | 44k             | 10.4 s | -20         |       |
| `impulses`         | 44k             | 11.5 s | -12         |       |
| `steps`            | 44k 48k 22k 11k | 10.8 s | -20         | l, r  |
| `steps-hot`        | 44k             | 10.8 s | -3          | l     |
| `bursts`           | 44k             | 3.9 s  | -20         | l     |
| `polarity`         | 44k             | 5.5 s  | -20         |       |
| `pilot-19600`      | 44k             | 16.5 s | -30         | l     |
| `pilot-11025`      | 44k             | 16.5 s | -30         | l     |
| `pilot-12000`      | 48k             | 16.5 s | -30         | l     |
| `chirps`           | 44k             | 14.7 s | -20 pk      |       |
| `dual-skew`        | 44k 22k         | 6.5 s  | -20 pk      |       |
| `levels`           | 44k 48k         | 6.5 s  | -100 to -20 | l, r  |
| `levelcal-hot`     | 44k 48k         | 15.5 s | -60 to -0.1 | l, r  |
| `separation`       | 44k 48k         | 4.6 s  | -20         |       |
| `dr60`             | 44k             | 7.6 s  | -60         | l, r  |
| `silence`          | 44k 48k         | 9.5 s  | zero        |       |
| `dither`           | 44k 48k         | 9.5 s  | -96         |       |
| `lsb`              | 44k             | 14.5 s | 1-8 LSB     | l     |
| `dc-step`          | 44k             | 9.5 s  | -20 DC      | l     |
| `imd-smpte`        | 44k             | 6.5 s  | -12 pk      | l     |
| `imd-smpte-hot`    | 44k             | 6.5 s  | -3 pk       | l     |
| `imd-ccif`         | 44k 48k         | 6.5 s  | -12 pk      | l     |
| `imd-ccif-hot`     | 44k             | 6.5 s  | -3 pk       | l     |
| `jtest`            | 44k             | 6.5 s  | -21.1       | l     |
| `jtest-hot`        | 44k 48k         | 6.5 s  | -3          | l     |
| `nyquist`          | 44k             | 10.0 s | -20         | l     |
| `alias`            | 48k             | 8.9 s  | -20         |       |
| `images`           | 22k 11k         | 7.9 s  | -20         | l     |
| `cal`              | 44k 48k         | 1.9 s  | -20         |       |
| `verify`           | 44k 48k         | 5.5 s  | -20         | l, r  |
| `program`          | 44k 48k         | 16.5 s | -24         |       |
| `program-pilot`    | 44k 48k         | 31.5 s | -24         |       |
| `track`            | 44k             | 31.5 s | -40 to -20  |       |

### Frequency and phase response

* **sweep** (44.1, 48, 22.05 and 11.025 kHz, 11.5 s, -20 dBFS). An
  exponential sweep (Farina's) runs from 10 Hz to 0.95 of half the rate in
  10 s, the same on both channels. Its deconvolution gives the impulse
  response, the magnitude, phase and group delay to 0.92 of half the rate,
  and the 2nd to 5th harmonics against frequency, each from its own impulse
  response. Expect a flat response on the dig path, and at the line out,
  with the DAC's stages off at 44.1 kHz, the DAC's droop: 1.2 dB at 10 kHz
  and 4.2 dB at 20 kHz.
* **sweep-quick** (44.1 kHz, 3.5 s, -20 dBFS). The same sweep lasts 2 s. Its
  analysis gives the same results with about 7 dB more noise than the 10 s
  sweep's, and less room between the harmonics' impulse responses. Expect
  the 10 s sweep's response within its noise.
* **sweep-low** (44.1 kHz, 11.5 s, -40 dBFS). The 10 s sweep plays 20 dB
  lower. Against the sweep at -20 dBFS, its response shows what depends on
  the level. Expect the response at -20 dBFS within 0.1 dB where the noise
  allows, and harmonics that fall with the level.
* **sweep-hot** (44.1 kHz, 11.5 s, -3 dBFS). The 10 s sweep plays at -3
  dBFS, near full scale. Its response and harmonics show the path near
  clipping, against the sweeps at -20 and -40 dBFS. Expect a response within
  0.2 dB of the one at -20 dBFS. Harmonics that rise by much more than the
  17 dB of the level show where a stage runs out of room.
* **sweep-hf** (44.1 and 48 kHz, 9.5 s, -20 dBFS). A linear sweep runs from
  15 kHz to half the rate in 8 s. Its analysis follows the top of the band
  at a resolution of a few hertz: the filter edge, the dip near 18 to 19
  kHz, and any image or alias, which it tracks as other components beside
  the sweep. Expect a smooth roll-off to the edge with the DAC's hold, 4.2
  dB down at 20 kHz at 44.1 kHz, and no notch. An image of a tone at f lies
  at the rate less f.
* **sweep-sub** (44.1 kHz, 13.5 s, -20 dBFS). An exponential sweep runs from
  2 Hz to 2 kHz in 12 s. Its analysis gives the response under 20 Hz, where
  the coupling of the DAC, the line stages and the ADC sets the high-pass
  corners. Expect a high-pass of one or two poles. Its corner, where the
  level is 3 dB under the level at 1 kHz, is the figure to note.
* **multitone** (44.1 and 48 kHz, 7.4 s, -20 dBFS). A periodic multitone
  holds every odd bin of a 16384-point transform from 20 Hz to 20 kHz, the
  bins of 1 mod 4 on the left channel and of 3 mod 4 on the right, all of
  one amplitude, for 16 periods. Its crest factor is about 3 dB, as low as a
  sine's. Its analysis gives the four transfers of the stereo pair at once,
  averaged over 14 periods with no window, and the distortion and noise in
  the bins that no tone excites. Expect the sweep's response at the tones.
  The empty bins read the noise floor, and their rise over the silence's
  floor is the distortion.
* **multitone-pink** (44.1 kHz, 7.4 s, -20 dBFS). The multitone's amplitude
  falls 3 dB an octave, for the same power in every octave. Its analysis
  gives the multitone's results with more of the signal at low frequencies,
  as in music. Expect the multitone's response, with less noise under 1 kHz
  and more above.
* **noise-white** (44.1 and 48 kHz, 11.5 s, -20 dBFS). Gaussian white noise
  plays for 10 s, independent on the two channels. Welch's method gives the
  four transfers of the stereo pair with the coherence of each. The cross
  terms, from one channel to the other, show the coherence's bias for this
  many averages. Expect a coherence near 1 wherever the path passes the
  signal. A coherence that falls with frequency while the level holds means
  a clock that drifts during the analysis.
* **noise-pink** (44.1 kHz, 11.5 s, -20 dBFS). Pink noise, the same power in
  every octave from 20 Hz, plays for 10 s, independent on the two channels.
  Its analysis gives the white noise's results with more of the signal at
  low frequencies. Expect the white noise's response, with a coherence that
  holds lower down and less of it at the top.
* **mls** (44.1 kHz, 10.4 s, -20 dBFS). A maximum-length sequence of order
  16, 65535 samples, plays 6 periods on both channels. Circular correlation
  gives the impulse response, averaged over 4 periods, and the response from
  it. Expect the sweep's response. An MLS spreads distortion as noise over
  the response, so the two differ only where the path is not linear.
* **impulses** (44.1 kHz, 11.5 s, -12 dBFS). A single sample at -12 dBFS
  comes every 8192 frames for 10 s, a click train of an exact period.
  Synchronous averaging gives the impulse response, its shape (the DAC's
  hold and filters) and the response from it, and each click's time against
  the recorder's clock. Expect an impulse response that settles within a few
  milliseconds, and clicks evenly spaced on a line whose slope is the
  clocks' ratio.
* **steps** (44.1, 48, 22.05 and 11.025 kHz, 10.8 s, -20 dBFS). Sines at the
  third-octave frequencies from 20 Hz to 20 kHz, or to 0.45 of the rate,
  play for 0.25 s each on both channels. A sine fit gives the level and
  phase at each frequency, and THD+N and the 2nd to 5th harmonics against
  frequency. Expect the sweep's response at the same frequencies within 0.1
  dB, and THD+N that the noise sets except at the top.
* **steps-hot** (44.1 kHz, 10.8 s, -3 dBFS). The stepped sines play at -3
  dBFS, near full scale. Its analysis gives THD+N and the harmonics against
  frequency at a high level. Expect each harmonic within 10 dB of its level
  at -20 dBFS, plus the 17 dB of the level for a second harmonic and 34 dB
  for a third.
* **bursts** (44.1 kHz, 3.9 s, -20 dBFS). Tone bursts with a Gaussian
  envelope of 3 cycles sound at the octaves from 31.5 Hz to 16 kHz. The
  delay of each burst's envelope gives the group delay at its octave, and
  its level, apart from the transfer function's phase. Expect group delays
  within a few hundred microseconds of each other over 100 Hz. A high-pass
  shows as a longer delay at the bottom.

### Channels and polarity

* **sweep-lr** (44.1 and 48 kHz, 12.0 s, -20 dBFS). The 5 s sweep plays on
  the left channel alone, then on the right alone. Its analysis gives all
  four transfers of the stereo pair, left to left, left to right, right to
  left and right to right, and so the crosstalk against frequency. Expect
  crosstalk well under -60 dB through the line paths and near -66 dB on the
  internal loop. A cross term as large as the direct one means a mono
  capture or a path that sums the sides.
* **noise-white-same** (44.1 kHz, 11.5 s, -20 dBFS). The white noise plays
  the same on both channels. Its analysis sets the two outputs against each
  other, for the difference in level, the phase and the delay between the
  channels against frequency. Expect the channels to differ by under 0.1 dB,
  a few degrees and a few microseconds, unless one channel's path differs.
* **noise-pink-same** (44.1 kHz, 11.5 s, -20 dBFS). The pink noise plays the
  same on both channels. Its analysis gives what the white noise on both
  channels gives, with more of the signal at low frequencies. Expect the
  results of the white noise on both channels.
* **polarity** (44.1 kHz, 5.5 s, -20 dBFS). Positive half-sine pulses of 1
  ms play, 8 on the left channel and then 8 on the right. The correlation
  with the pulse tells which output carries which input, and the sign of
  each. Expect the left pulses on the left output and the right ones on the
  right, both positive.
* **separation** (44.1 and 48 kHz, 4.6 s, -20 dBFS). A tone of 997 Hz plays
  on the left side and one of 1499 Hz on the right, at -20 dBFS, for 3 s.
  Each tone's level in each recorded channel shows which side each channel
  hears and how far apart the sides stay. Run it first on a path whose
  cabling is new. Expect each channel to hear its own tone and the other far
  lower: -66 dB on the internal loop, 66 to 73 dB apart into the VX, and 60
  dB back into the Aux input, whose sides are swapped. A path of one channel
  hears one tone, and a sum or difference hears both.

### Timebase

* **pilot-19600** (44.1 kHz, 16.5 s, -30 dBFS). A sine of 19600 Hz at -30
  dBFS plays for 15 s, 4 cycles in 9 frames, the samples repeating exactly.
  Its phase gives the player's clock against the recorder's in ppm, the time
  error over the file, its Allan deviation and the jitter around the tone,
  as es1869clock reads its pilot. Expect 0 ppm on the internal loop with
  `a2_resample` at 0 and about +29 ppm in the Clock mode, and a few ppm
  against a second card, steady over the file.
* **pilot-11025** (44.1 kHz, 16.5 s, -30 dBFS). A sine of 11025 Hz, a
  quarter of the rate, plays at -30 dBFS for 15 s, 1 cycle in 4 frames. Its
  analysis gives the 19600 Hz pilot's results at a frequency where jitter
  weighs less and the path's response is flatter. Expect the 19600 Hz
  pilot's results.
* **pilot-12000** (48 kHz, 16.5 s, -30 dBFS). A sine of 12000 Hz plays at
  -30 dBFS for 15 s, 1 cycle in 4 frames. The chip's 48 kHz is 48012.09 Hz,
  so the cycles are whole in frames and the tone sounds at 12003.02 Hz. Its
  analysis gives the 19600 Hz pilot's results at 48 kHz. Expect +252 ppm
  against a recorder whose 48 kHz is true, plus the two crystals'
  difference.
* **chirps** (44.1 kHz, 14.7 s, -20 dBFS peak). The marker chirp plays every
  second, 15 times. Each chirp's time, to a hundredth of a frame, gives the
  clocks' ratio from their line and the time error at each. Expect residuals
  of a few microseconds or less. A wandering crystal shows as a curve.
* **dual-skew** (44.1 and 22.05 kHz, 6.5 s, -20 dBFS peak). Four channels
  for the dual device carry the marker chirp on Audio 1's left channel
  (channel 1) and the same chirp downward on Audio 2's (channel 3), together
  every 0.5 s for 6 s, with the right channels silent. The two chirps, found
  apart in one recording of the mixed output, show when each DAC plays the
  same frame, to a hundredth of a frame. Expect a skew that stays the same
  within a run: 1 to 4.5 frames at 44.1 kHz and up to 15 at 22050 Hz on the
  Armada so far.

### Amplitude and noise

* **levels** (44.1 and 48 kHz, 6.5 s, -100 to -20 dBFS). A 997 Hz sine steps
  from -100 to -20 dBFS in steps of 10 dB, 0.5 s each. Its analysis gives
  the gain at each level against the gain at -20 dBFS, which is the
  linearity, and THD+N against level. Expect the gain within 0.1 dB down to
  -60 dBFS and within 1 dB at -80, and THD+N that rises 10 dB a step once
  the noise dominates.
* **levelcal-hot** (44.1 and 48 kHz, 15.5 s, -60 to -0.1 dBFS). The level
  calibration is a 997 Hz sine from -60 dBFS up to -0.1 dBFS, 0.5 s a step,
  in steps of 0.5 dB over the top 6 dB. Its analysis gives the path's gain
  and the level at which a stage starts to clip: the lowest step whose gain
  leaves the small-signal gain by 0.1 dB, or whose harmonics rise 10 dB over
  their trend. It then gives the highest peak that a test signal may have,
  by the rule of TEST_LEVELS.md. Expect a gain of -1.8 dB on the internal
  loop at its calibrated setting, linear to 0.01 dB, with THD+N of -62 dB up
  to -0.5 dBFS. A clip point that the record gain moves belongs to the ADC.
* **dr60** (44.1 kHz, 7.6 s, -60 dBFS). A 997 Hz sine plays at -60 dBFS for
  6 s. Its THD+N, unweighted and A-weighted, plus 60 dB is AES17's dynamic
  range. Expect the noise floor's distance from full scale, some 65 to 70 dB
  through the chip's record paths.
* **silence** (44.1 and 48 kHz, 9.5 s, digital zero). Digital silence lasts
  8 s between the markers. Its analysis gives the noise floor: its level
  unweighted and A-weighted from 20 Hz to 20 kHz, its spectrum, the DC and
  the idle tones above it. Expect the record mixer's floor, which read -68
  dBFS at Capture Volume 15, with a line at 15625 Hz near -84 dBFS. A DC
  offset is the ADC's.
* **dither** (44.1 and 48 kHz, 9.5 s, -96 dBFS). Triangular dither of 1 LSB
  plays alone for 8 s, about -96 dBFS. Against the digital silence, its
  floor shows the idle tones that only digital zero brings out, with the
  DAC's input moving. Expect the silence's floor within 1 dB, since 16-bit
  dither lies some 30 dB under the analog noise.
* **lsb** (44.1 kHz, 14.5 s, 1 to 8 LSB). Square waves of 1, 2, 4 and 8 LSB
  about zero play for 3 s each at 1002 Hz, 44 frames a period, with no
  dither. Each square's fundamental against 4/pi of its height, and its even
  harmonics, show the DAC's linearity at its last bits. Expect each step 6
  dB over the one before, within the fit's noise, which the report gives.
* **dc-step** (44.1 kHz, 9.5 s, DC of -20 dBFS). DC of +0.1 of full scale
  plays for 2 s, then zero for 2 s, -0.1 for 2 s and zero again. Each step's
  decay tells whether the path passes DC, and gives the time constant and
  corner of its high-pass. Expect a decay of one pole whose corner matches
  `sweep-sub`'s, and the recording's own DC between the steps.

### Distortion, images and jitter

* **imd-smpte** (44.1 kHz, 6.5 s, -12 dBFS peak). Tones of 60 Hz and 7 kHz
  play at 4 to 1, with a combined peak of -12 dBFS, on exact bins, for 5 s.
  The sidebands of 7 kHz at multiples of 60 Hz, against 7 kHz, give the
  SMPTE intermodulation. Expect sidebands near the noise. A rise with the
  level points at a stage that the low tone modulates.
* **imd-smpte-hot** (44.1 kHz, 6.5 s, -3 dBFS peak). The SMPTE pair plays
  with a combined peak of -3 dBFS. Its analysis gives the SMPTE
  intermodulation near full scale. Expect sidebands that rise with the
  level, against the pair at -12 dBFS.
* **imd-ccif** (44.1 and 48 kHz, 6.5 s, -12 dBFS peak). Tones of 19 kHz and
  20 kHz play at one amplitude, with a combined peak of -12 dBFS, on exact
  bins, for 5 s. Its analysis gives the twin-tone intermodulation: the
  difference tone at 1 kHz, the products at 18 and 21 kHz, and the folded
  products of a path without an anti-alias filter. Expect products near the
  noise. The ADC with its filter off folds what lies above half the rate
  back into the band.
* **imd-ccif-hot** (44.1 kHz, 6.5 s, -3 dBFS peak). The twin tone plays with
  a combined peak of -3 dBFS. Its analysis gives the twin-tone
  intermodulation near full scale. Expect products that rise with the level,
  against the pair at -12 dBFS.
* **jtest** (44.1 kHz, 6.5 s, -21.1 dBFS). Dunn's J-test plays at a low
  level: a quarter of the rate as samples of +-2048, with an LSB square wave
  at a 192nd of the rate, exact. The tone's sidebands at multiples of a
  192nd of the rate, beyond the file's own, give data-dependent jitter and
  interference as jitter. Expect no sideband over the file's own. A pair of
  sidebands at -100 dBc of an 11 kHz tone is about 0.3 ns of jitter.
* **jtest-hot** (44.1 and 48 kHz, 6.5 s, -3 dBFS). The J-test plays as Dunn
  defined it: +-16384, a sine of -3 dBFS, with the LSB square that flips
  every bit under the top two. Its analysis gives the J-test's results with
  the most bits changing. Expect sidebands no higher than at the low level.
* **nyquist** (44.1 kHz, 10.0 s, -20 dBFS). Sines from 15 kHz to 21.9 kHz
  play for 1 s each. Its analysis gives the level near the top of the band
  and the DAC's images at the rate less f, which a second card at 48 kHz
  hears up to 24 kHz. Expect images some 10 dB or more under the tone with
  the DAC's stages off, and far lower with `a2_oversample` or the filters.
* **alias** (48 kHz, 8.9 s, -20 dBFS). Sines at 48 kHz from 20 kHz to 23.9
  kHz play for 1 s each, for a second card to play into the Aux input. The
  ADC's anti-aliasing at 44.1 kHz shows in each tone over 22.05 kHz, which
  comes back at 44.1 kHz less f. Expect aliases close to the tone's level
  with the Audio 1 filter off, and far lower with it on.
* **images** (22.05 and 11.025 kHz, 7.9 s, -20 dBFS). Sines at fractions of
  the rate from 0.05 to 0.45 play for 1 s each, at the low rates. Recorded
  at 44.1 or 48 kHz, they show the DAC's images at the rate less f, in the
  audible band at these rates. Expect, at 22050 Hz, an image of 5 kHz at
  17050 Hz, about 11 dB under the tone with the stages off, and far lower
  with the filter.

### For the loop's correction

* **cal** (44.1 and 48 kHz, 1.9 s, -20 dBFS). The boot calibration plays 10
  periods of 8192 frames with every odd bin from 20 Hz to 20 kHz, the bins
  of 1 mod 4 on the left channel and of 3 mod 4 on the right, each octave
  over 63 Hz within 1 dB of the same power. Its crest factor is about 3.2
  dB, a ramp covers the first and the last period, and it has no markers.
  Its analysis gives the loop's four transfers from 8 periods averaged with
  no window, the noise and distortion in the empty bins and how far the
  periods agree, and runs the boot method of the section on the loop's
  calibration on the same recording. Expect 0 ppm between the periods,
  agreement near 1, the sweep's response at the bins, and the boot method
  within 0.01 dB of the full analysis.
* **verify** (44.1 and 48 kHz, 5.5 s, -20 dBFS). The exponential sweep lasts
  4 s, for checking a correction. Its analysis gives the response after a
  correction, which should come back flat, and its widest swing from 20 Hz
  to 20 kHz. Expect a response within a few tenths of a dB from 20 Hz to 20
  kHz once a correction applies.
* **program** (44.1 and 48 kHz, 16.5 s, -24 dBFS). Noise with music's
  long-term spectrum plays for 15 s, the channels correlated by 0.6, with a
  crest factor near 16 dB from a slow envelope. Welch's method gives the
  four transfers with correlated inputs, as a live analysis meets them in
  music, and the coherence that a music-like spectrum leaves at the top.
  Expect the white noise's response wherever the coherence is near 1, and a
  coherence that falls at the top, where music is quiet.
* **program-pilot** (44.1 and 48 kHz, 31.5 s, -24 dBFS). The program plays
  for 30 s with a pilot at -40 dBFS, 19600 Hz at 44.1 kHz and 19200 Hz at 48
  kHz, and nothing else within 100 Hz of it. Its analysis gives the loop's
  response and the clocks at once, as a live analysis would watch both under
  music. Expect the program's response, and the pilot's ppm as on its own.
* **track** (44.1 kHz, 31.5 s, -40 to -20 dBFS). Segments of 4 s change the
  spectrum: pink, white, bass, treble, mid band, pink 20 dB lower, 2 s of
  silence and program. Its analysis gives how fast a running estimate of the
  loop settles after each change, how far it strays in the bands a segment
  leaves empty, and what silence does to it. Expect each segment's response
  within 0.5 dB of the white noise's in the bands it excites, and an
  estimate that silence leaves alone.

<!-- /signals -->

## Signals and paths

Each signal's paths, as the table has them: Loop, Out, In and Dig as above,
and Quad the dual device into the VX. Loop and Dig need only the Armada;
Out, In and Quad need the VX, and measure its converters with the chip's. A
signal can go anywhere its levels allow; these are the paths it was made
for.

<!-- paths -->

| Signal             | Loop | Out | In  | Dig | Quad |
|--------------------|------|-----|-----|-----|------|
| `sweep`            | x    | x   | x   | x   |      |
| `sweep-quick`      | x    | x   | x   | x   |      |
| `sweep-low`        | x    | x   | x   |     |      |
| `sweep-hot`        | x    | x   | x   |     |      |
| `sweep-lr`         | x    | x   | x   |     |      |
| `sweep-hf`         | x    | x   | x   | x   |      |
| `sweep-sub`        | x    | x   | x   |     |      |
| `multitone`        | x    | x   | x   | x   |      |
| `multitone-pink`   | x    | x   | x   | x   |      |
| `noise-white`      | x    | x   | x   | x   |      |
| `noise-pink`       | x    | x   | x   | x   |      |
| `noise-white-same` | x    | x   | x   | x   |      |
| `noise-pink-same`  | x    | x   | x   | x   |      |
| `mls`              | x    | x   | x   |     |      |
| `impulses`         | x    | x   | x   |     |      |
| `steps`            | x    | x   | x   |     |      |
| `steps-hot`        | x    | x   | x   |     |      |
| `bursts`           | x    | x   | x   |     |      |
| `polarity`         | x    | x   | x   |     |      |
| `pilot-19600`      | x    | x   | x   |     |      |
| `pilot-11025`      | x    | x   | x   |     |      |
| `pilot-12000`      | x    | x   | x   |     |      |
| `chirps`           | x    | x   | x   |     |      |
| `dual-skew`        |      |     |     |     | x    |
| `levels`           | x    | x   | x   |     |      |
| `levelcal-hot`     | x    | x   | x   | x   |      |
| `separation`       | x    | x   | x   |     |      |
| `dr60`             | x    | x   | x   |     |      |
| `silence`          | x    | x   | x   | x   |      |
| `dither`           | x    | x   | x   | x   |      |
| `lsb`              | x    | x   | x   | x   |      |
| `dc-step`          | x    | x   | x   | x   |      |
| `imd-smpte`        | x    | x   | x   |     |      |
| `imd-smpte-hot`    | x    | x   | x   |     |      |
| `imd-ccif`         | x    | x   | x   |     |      |
| `imd-ccif-hot`     | x    | x   | x   |     |      |
| `jtest`            | x    | x   | x   | x   |      |
| `jtest-hot`        | x    | x   | x   | x   |      |
| `nyquist`          |      | x   |     |     |      |
| `alias`            |      |     | x   |     |      |
| `images`           | x    | x   |     |     |      |
| `cal`              | x    | x   | x   |     |      |
| `verify`           | x    | x   | x   |     |      |
| `program`          | x    | x   | x   |     |      |
| `program-pilot`    | x    | x   | x   |     |      |
| `track`            | x    | x   | x   |     |      |

<!-- /paths -->

The driver's settings that change a path, with the signals that show the
change and what it measures. Measure each with both values, on the paths of
those signals, keeping everything else the same.

<!-- settings -->

* `a2_resample` (0 and 2; `pilot-19600`, `chirps`, `cal`). The loop reads 0
  ppm with 0 (Off). The Clock mode resamples Audio 2 to true time, so the
  loop then reads its correction, about +29 ppm, and a periodic signal no
  longer comes back periodic.
* `a2_treble` (0 and 1; `sweep`, `sweep-hf`, `verify`). It shows the treble
  correction's lift, up to 4 dB near 20 kHz on the dig path. At the line
  out, with the DAC's stages off, the droop comes back to within 0.2 dB.
* `audio2_oversampling` (0 and 1; `sweep`, `sweep-hf`, `nyquist`). It is
  ESS's 4x mode, which takes 1.74 dB off everything, falls 6 dB by 20.4 kHz
  and puts the images 25 dB lower.
* `audio2_filter` (0 and 1; `sweep`, `images`). It puts the
  switched-capacitor filter after Audio 2's DAC, with its corner at
  `filter_corner` below 44.1 kHz.
* `audio1_filter` (0 and 1; `alias`, `sweep`, `imd-ccif`). It is the ADC's
  anti-alias filter. With 0, what lies over half the rate folds back; with
  1, the filter's response takes it.
* `a2_oversample` (0, 1 and 2; `nyquist`, `sweep-hf`, `silence`). It runs
  Audio 2's DAC at 2x or 3x of the stream, which moves the images up and
  takes less of the treble.
* `filter_corner` (875 and others; `sweep`, `images`). It moves the filters'
  corner at the rates under 44.1 kHz, with a filter on.
* "PCM Capture Volume" and "Capture Volume" (the steps; `levelcal-hot`,
  `silence`, `sweep-hot`). They set the internal loop's gain, noise floor
  and clip point. 14 and 3 are its calibrated setting.
* "Aux Capture Volume" (the steps; `levelcal-hot`, `silence`). It is the
  second card's way into the ES1869. 15, with "Capture Volume" at 4, is its
  calibrated setting.
* "Master Playback Volume" (55 and others; `levelcal-hot`, `sweep-hot`). It
  sets the line out's level into the second card, and 55 (-6 dB) keeps the
  VX at "Mic Capture Volume" 1 from clipping.
* `adc_offset` (0 and the saved value; `dc-step`, `silence`). It sets the
  recording's DC.
* `dual_playback` (1; `dual-skew`). The four-channel device opens only with
  it.

<!-- /settings -->

## The loop's calibration and correction

The live analysis compares what Audio 2 played, from a capture device of its
samples, with what Audio 1 records of it through the record mixer. Both run
on one crystal, and the capture holds the samples after the rate correction,
so the comparison has no drift even in the Clock mode. The profile it gives
is the loop's response, for a correction table in place of `a2_treble`'s
fixed one and for a filter on Audio 1's recordings. A short calibration at
boot, before MPD starts, gives the first profile.

### The boot calibration

`cal` is the boot's signal, and sigan.py's analysis of it follows the steps
that C code would:
1. Set the loop's calibrated setting with `a2_resample` at 0, open Audio 1's
   capture and Audio 2's playback at the same rate, 44.1 or 48 kHz, 16-bit
   stereo, and start the capture first.
2. Play the 10 periods of 8192 frames, 1.86 s at 44.1 kHz. A period p holds
   every odd bin k of an 8192-point transform from 20 Hz to 20 kHz: the bins
   with k mod 4 = 1 on the left channel and k mod 4 = 3 on the right, at
   amplitudes falling as one over the square root of k, so that the octaves
   over 63 Hz carry the same power within 1 dB. The phases are Schroeder's,
   then 300 rounds of clipping at 1.4 times the rms and putting the tones'
   amplitudes back, which leaves a crest factor of about 3.2 dB; the
   manifest lists each side's bins and phases. Each side's rms is -20 dBFS
   (AES17), quantized once with dither. The first period ramps up as 0.5 -
   0.5 cos(pi (n + 0.5) / 8192), the last ramps down, and the 8 between are
   the same samples.
3. Find the onset c0: the first frame where either channel passes the larger
   of 8 times the rms of the capture's first 50 ms and a tenth of the whole
   capture's rms.
4. Average the 7 windows of 8192 frames that start at c0 + 8192 + 8192 j,
   for j from 0 to 6, channel by channel. They lie within the 8 steady
   periods wherever the onset falls in the ramp.
5. Take the transforms Y of the average and X of one steady period of each
   side. At a left bin k, Y_L / X_L is the left-to-left transfer and Y_R /
   X_L the left-to-right crosstalk; at a right bin, Y_R / X_R and Y_L / X_R.
6. The windows start an unknown fraction of a period into it, which turns
   every transfer by the phase of a pure delay. The magnitude needs nothing
   more; for the phase, take the delay from the slope of the phase over 300
   Hz to 3 kHz and take it out.
7. The empty bins, even and odd, carry the noise and distortion, and the
   spread of each bin over the 7 windows gives its SNR.

Run on a recording made this way, the analysis reports this method's
transfers within 0.01 dB of its own, which aligns `cal` by correlation,
corrects the clock by the periods' drift and averages all 8 steady periods.
At 44.1 kHz the bins lie 5.38 Hz apart and each side's 21.5 Hz apart, so a
side has three or four tones from 20 to 100 Hz: enough for a correction
there, which is smooth.

### The correction

From the boot's transfers, a correction for each side is 1 / H at its bins,
smoothed over a sixth of an octave in power and set to 0 dB at 1 kHz, with
its lift held to a few dB, since a lift costs headroom and the loop's noise
rises with it. Nothing over 20 kHz or under 20 Hz needs correcting, and a
lift there would only cost headroom. `a2_treble`'s table takes back only the
DAC's response; a table from the loop takes back the record mixer's and the
ADC's too, so the playback correction and the capture filter have to split
it, the DAC's share by Out and the ADC's by In. `verify` then plays through
the corrected loop, and its response should come back flat within a few
tenths of a dB from 20 Hz to 20 kHz, between the bins too.

### The live analysis

es1869scope's `loop.c` takes Hann windows of 2^n, half overlapping, with H =
Sxy / Sxx and the coherence |Sxy|^2 / (Sxx Syy), as sigan.py's Welch
analysis does; a stereo program has correlated sides, so each output takes
both inputs through the 2x2 solve that sigan.py uses. These signals test it:
* `program` is music's long-term spectrum, the sides correlated by 0.6, with
  a crest factor near 16 dB. Its coherence falls at the top, where music is
  quiet, which is where the profile needs the longest average.
* `program-pilot` adds a pilot of 19600 Hz (19200 Hz at 48 kHz), 100 Hz
  clear of the program, to watch the clocks and the loop at once. The
  pilot's bins are left out of the transfers, since the pilot is the same on
  both sides.
* `track` changes its spectrum every 4 s. Each segment's response should
  match the white segment's in the bands it excites, a running estimate with
  forgetting should settle within a fraction of a second, and silence should
  leave it alone, since no input means no update.
* `noise-white`'s cross terms and `silence` show the coherence's floor,
  about 1/K for K windows, under which a bin tells nothing.

## What to run first

1. `separation`, on each path, to see the channels as the cabling has them.
2. `levelcal-hot`, on each path, and the record gains by the rule of
   [TEST_LEVELS.md](TEST_LEVELS.md).
3. `cal-44k` through the loop with `a2_resample` at 0: the clock should read
   0.000 ppm, the response the loop's, and the boot method within 0.01 dB.
   Then the same in the Clock mode, which should read about +29 ppm.
4. `noise-white-44k` through the loop with `a2_resample` at 0: coherence
   near 1 up the band. A first loop measurement in the Clock mode read
   coherence far below 1 over a few hundred Hz; the drift is a likely cause,
   and this run tells.
5. `sweep-44k` through the loop, Out and In, then `sweep-hf-44k` for the top
   of the band and the dip near 18 to 19 kHz.
