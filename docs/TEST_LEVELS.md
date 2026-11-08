# Levels for the test signals

This page says how to set each path's levels so that no test signal clips
and each uses as much of the recorder's range as it can.
[TEST_SIGNALS.md](TEST_SIGNALS.md) describes the signals, the paths and the
analysis.

## The calibration step

Every path gets the same first step: play `levelcal-hot` through it, then
analyze the recording.

```
python3 tools/sigan.py levelcal-hot-44k rec.wav
```

The signal is a 997 Hz sine in 25 steps of 0.5 s, from -60 dBFS up to -0.1
dBFS, 0.5 dB apart over the top 6 dB. For each step the analysis fits the
tone and reports the gain, which is the recorded level less the played one,
and the harmonics. The median gain of the steps from -40 to -20 dBFS is the
path's small-signal gain G. The clip point C is the lowest step from -30
dBFS up whose gain leaves G by more than 0.1 dB, or whose harmonics pass -40
dB and lie 10 dB over their median from -30 to -14 dBFS. When no step does
either, C is the top step.

The signal is hot, as its name says: play it only into a line or Aux input
with the speakers muted.

## The rule

With a margin M of 3 dB, a test signal whose true peak in the file is P dBFS
may be played on a path when both of these hold:
* P is at most C - M, so that every stage before the recorder stays M dB
  under its clip point.
* P + G is at most -M, so that the recorder stays M dB under its own full
  scale.

The analysis prints the highest P that the rule allows. Where a path's
response rises over its level at 1 kHz, add that rise to G for a broadband
signal: a sweep or noise reaches the recorder at the response's highest
point.

The same reading sets the record gain. R = C + G is the recorder's level at
the clip point:
1. When R lies within 1 dB of full scale, the recorder clips first. Lower
   the record gain, and C moves up by as much, until another stage clips
   first or C reaches the player's full scale.
2. When R lies more than M under full scale, a stage before the recorder
   clips first, or nothing clips at all. Raise the record gain by up to -M -
   R dB, in the control's steps, which brings the recorder's full scale to M
   dB over that stage's clip point.
3. Run `levelcal-hot` again at the new setting.

Two runs at record gains a step apart tell which stage clips. A clip point
that moves with the record gain belongs to a stage after it, which is the
ADC. One that stays where it is belongs to a stage before it, such as the
record mixer or the DAC.

## The calibrated settings

These were measured on 2026-11-08 with a stepped 997 Hz tone at levels from
-30 to -0.5 dBFS. A chain gain is the recorded dBFS less the played dBFS at
997 Hz.

### The internal loop

Audio 2 plays into Audio 1 through the record mixer:
* Record source Mix, "PCM Capture Volume" (69h) at 14 (-1.5 dB), "Capture
  Volume" at 3 (-1.5 dB), every other record input at 0 and the master off.
* The chain gain is -1.8 dB, linear to 0.01 dB, with THD+N of -62 dB up to
  -0.5 dBFS played.
* With 69h at 15 (0 dB), THD+N rises by about 4 dB over the last 0.5 dB
  under full scale, whatever the ADC's gain, so the record mixer's stage is
  the one that clips there.
* The earlier setting, 69h at 8 and "Capture Volume" at 4, has a chain gain
  of -9.2 dB.

### The ES1869 into the VX

The ES1869's line out reaches the VX's inputs in stereo, the sides 66 to 73
dB apart:
* The VX's "Capture Source" at Line, or at Mic with "Mic Capture Volume" at
  0, which is the same, gives a chain gain of -26 dB and a THD+N floor of
  -50 dB from the VX's own input noise.
* Mic with "Mic Capture Volume" at 1 adds 29 dB, for a chain gain of +3.0
  and +3.35 dB on the two sides, and the VX then clips once the ES1869
  passes -3 dBFS.
* The calibrated setting is the VX's Mic at 1 with the ES1869's "Master
  Playback Volume" at 55 (-6 dB), a chain gain of about -3.0 and -2.65 dB.

ALSA's dB scale for the VX's "Mic Capture Volume" (-0.21 dB, 0.03 dB a step)
is wrong: level 1 lies 29 dB over level 0.

### The VX into the ES1869's Aux input

The VX's outputs reach the ES1869's Aux input (AUXB, the record mixer's "Aux
Capture Volume") in stereo, the sides 60 dB apart, with left and right
swapped: the VX's left arrives on the ES1869's right channel. The analysis
sees the swap in the markers and swaps the channels back. The ES1869's
microphone input isn't connected.
* The VX's "Master Playback Volume" at 215 (-6 dB). On the ES1869, record
  source Mix, "Aux Capture Volume" at 15 (0 dB), "Capture Volume" at 4 (0
  dB) and every other record input at 0.
* The chain gain is -1.2 and -1.4 dB, with THD+N of -62 dB up to -0.5 dBFS.
* Keep the VX's master at -6 dB or lower: at 0 dB its full scale would sit
  about 5 dB over the ES1869's input range.

## The tables

`python3 tools/testsig.py --doc` makes these tables from the signal table.
For each path, at the gain of its calibrated setting (the higher side's,
where the sides differ), they list every signal the path plays: the level of
its body (AES17 dBFS), its true peak in the file (dBFS, four times
oversampled, the markers included), that peak at the recorder, and the room
left under the recorder's full scale. A room under the 3 dB margin reads
"short". The short ones are the hot signals, which go near the clip point on
purpose. The one-side variants have their signal's levels.

<!-- levels -->

### Loop, -1.80 dB

| Signal             | Level  | Peak  | At rec | Room      |
|--------------------|--------|-------|--------|-----------|
| `sweep`            | -20.1  | -20.0 | -21.8  | 21.8      |
| `sweep-quick`      | -20.0  | -20.0 | -21.8  | 21.8      |
| `sweep-low`        | -40.1  | -20.0 | -21.8  | 21.8      |
| `sweep-hot`        | -3.1   | -3.0  | -4.8   | 4.8       |
| `sweep-lr`         | -23.3  | -20.0 | -21.8  | 21.8      |
| `sweep-hf`         | -20.0  | -19.2 | -21.0  | 21.0      |
| `sweep-sub`        | -20.1  | -20.0 | -21.8  | 21.8      |
| `multitone`        | -20.4  | -16.9 | -18.7  | 18.7      |
| `multitone-pink`   | -20.4  | -16.1 | -17.9  | 17.9      |
| `noise-white`      | -20.0  | -8.7  | -10.5  | 10.5      |
| `noise-pink`       | -20.0  | -9.4  | -11.2  | 11.2      |
| `noise-white-same` | -20.0  | -9.2  | -11.0  | 11.0      |
| `noise-pink-same`  | -20.0  | -9.5  | -11.3  | 11.3      |
| `mls`              | -21.0  | -14.2 | -16.0  | 16.0      |
| `impulses`         | -48.1  | -12.0 | -13.8  | 13.8      |
| `steps`            | -20.8  | -20.0 | -21.8  | 21.8      |
| `steps-hot`        | -3.8   | -3.0  | -4.8   | 4.8       |
| `bursts`           | -28.5  | -20.0 | -21.8  | 21.8      |
| `polarity`         | -47.0  | -20.0 | -21.8  | 21.8      |
| `pilot-19600`      | -30.0  | -20.0 | -21.8  | 21.8      |
| `pilot-11025`      | -30.0  | -20.0 | -21.8  | 21.8      |
| `pilot-12000`      | -30.0  | -20.0 | -21.8  | 21.8      |
| `chirps`           | -31.4  | -20.0 | -21.8  | 21.8      |
| `levels`           | -29.5  | -20.0 | -21.8  | 21.8      |
| `levelcal-hot`     | -5.5   | -0.1  | -1.9   | 1.9 short |
| `separation`       | -20.1  | -20.0 | -21.8  | 21.8      |
| `dr60`             | -60.0  | -20.0 | -21.8  | 21.8      |
| `silence`          | -240.0 | -20.0 | -21.8  | 21.8      |
| `dither`           | -93.3  | -20.0 | -21.8  | 21.8      |
| `lsb`              | -74.4  | -20.0 | -21.8  | 21.8      |
| `dc-step`          | -20.0  | -18.9 | -20.7  | 20.7      |
| `imd-smpte`        | -13.7  | -12.0 | -13.8  | 13.8      |
| `imd-smpte-hot`    | -4.7   | -3.0  | -4.8   | 4.8       |
| `imd-ccif`         | -15.0  | -12.0 | -13.8  | 13.8      |
| `imd-ccif-hot`     | -6.0   | -3.0  | -4.8   | 4.8       |
| `jtest`            | -21.1  | -20.0 | -21.8  | 21.8      |
| `jtest-hot`        | -3.0   | -2.9  | -4.7   | 4.7       |
| `images`           | -20.2  | -20.0 | -21.8  | 21.8      |
| `cal`              | -20.6  | -16.2 | -18.0  | 18.0      |
| `verify`           | -20.1  | -20.0 | -21.8  | 21.8      |
| `program`          | -24.0  | -11.2 | -13.0  | 13.0      |
| `program-pilot`    | -23.9  | -10.4 | -12.2  | 12.2      |
| `track`            | -21.4  | -9.0  | -10.8  | 10.8      |

### Out, -2.65 dB

| Signal             | Level  | Peak  | At rec | Room      |
|--------------------|--------|-------|--------|-----------|
| `sweep`            | -20.1  | -20.0 | -22.6  | 22.6      |
| `sweep-quick`      | -20.0  | -20.0 | -22.6  | 22.6      |
| `sweep-low`        | -40.1  | -20.0 | -22.6  | 22.6      |
| `sweep-hot`        | -3.1   | -3.0  | -5.6   | 5.6       |
| `sweep-lr`         | -23.3  | -20.0 | -22.6  | 22.6      |
| `sweep-hf`         | -20.0  | -19.2 | -21.9  | 21.9      |
| `sweep-sub`        | -20.1  | -20.0 | -22.6  | 22.6      |
| `multitone`        | -20.4  | -16.9 | -19.5  | 19.5      |
| `multitone-pink`   | -20.4  | -16.1 | -18.7  | 18.7      |
| `noise-white`      | -20.0  | -8.7  | -11.4  | 11.4      |
| `noise-pink`       | -20.0  | -9.4  | -12.0  | 12.0      |
| `noise-white-same` | -20.0  | -9.2  | -11.9  | 11.9      |
| `noise-pink-same`  | -20.0  | -9.5  | -12.1  | 12.1      |
| `mls`              | -21.0  | -14.2 | -16.8  | 16.8      |
| `impulses`         | -48.1  | -12.0 | -14.6  | 14.6      |
| `steps`            | -20.8  | -20.0 | -22.6  | 22.6      |
| `steps-hot`        | -3.8   | -3.0  | -5.6   | 5.6       |
| `bursts`           | -28.5  | -20.0 | -22.6  | 22.6      |
| `polarity`         | -47.0  | -20.0 | -22.6  | 22.6      |
| `pilot-19600`      | -30.0  | -20.0 | -22.6  | 22.6      |
| `pilot-11025`      | -30.0  | -20.0 | -22.6  | 22.6      |
| `pilot-12000`      | -30.0  | -20.0 | -22.6  | 22.6      |
| `chirps`           | -31.4  | -20.0 | -22.6  | 22.6      |
| `levels`           | -29.5  | -20.0 | -22.6  | 22.6      |
| `levelcal-hot`     | -5.5   | -0.1  | -2.7   | 2.7 short |
| `separation`       | -20.1  | -20.0 | -22.6  | 22.6      |
| `dr60`             | -60.0  | -20.0 | -22.6  | 22.6      |
| `silence`          | -240.0 | -20.0 | -22.6  | 22.6      |
| `dither`           | -93.3  | -20.0 | -22.6  | 22.6      |
| `lsb`              | -74.4  | -20.0 | -22.6  | 22.6      |
| `dc-step`          | -20.0  | -18.9 | -21.5  | 21.5      |
| `imd-smpte`        | -13.7  | -12.0 | -14.6  | 14.6      |
| `imd-smpte-hot`    | -4.7   | -3.0  | -5.6   | 5.6       |
| `imd-ccif`         | -15.0  | -12.0 | -14.6  | 14.6      |
| `imd-ccif-hot`     | -6.0   | -3.0  | -5.6   | 5.6       |
| `jtest`            | -21.1  | -20.0 | -22.6  | 22.6      |
| `jtest-hot`        | -3.0   | -2.9  | -5.6   | 5.6       |
| `nyquist`          | -20.2  | -20.0 | -22.6  | 22.6      |
| `images`           | -20.2  | -20.0 | -22.6  | 22.6      |
| `cal`              | -20.6  | -16.2 | -18.9  | 18.9      |
| `verify`           | -20.1  | -20.0 | -22.6  | 22.6      |
| `program`          | -24.0  | -11.2 | -13.9  | 13.9      |
| `program-pilot`    | -23.9  | -10.4 | -13.0  | 13.0      |
| `track`            | -21.4  | -9.0  | -11.7  | 11.7      |

### In, -1.20 dB

| Signal             | Level  | Peak  | At rec | Room      |
|--------------------|--------|-------|--------|-----------|
| `sweep`            | -20.1  | -20.0 | -21.2  | 21.2      |
| `sweep-quick`      | -20.0  | -20.0 | -21.2  | 21.2      |
| `sweep-low`        | -40.1  | -20.0 | -21.2  | 21.2      |
| `sweep-hot`        | -3.1   | -3.0  | -4.2   | 4.2       |
| `sweep-lr`         | -23.3  | -20.0 | -21.2  | 21.2      |
| `sweep-hf`         | -20.0  | -19.2 | -20.4  | 20.4      |
| `sweep-sub`        | -20.1  | -20.0 | -21.2  | 21.2      |
| `multitone`        | -20.4  | -16.9 | -18.1  | 18.1      |
| `multitone-pink`   | -20.4  | -16.1 | -17.3  | 17.3      |
| `noise-white`      | -20.0  | -8.7  | -9.9   | 9.9       |
| `noise-pink`       | -20.0  | -9.4  | -10.6  | 10.6      |
| `noise-white-same` | -20.0  | -9.2  | -10.4  | 10.4      |
| `noise-pink-same`  | -20.0  | -9.5  | -10.7  | 10.7      |
| `mls`              | -21.0  | -14.2 | -15.4  | 15.4      |
| `impulses`         | -48.1  | -12.0 | -13.2  | 13.2      |
| `steps`            | -20.8  | -20.0 | -21.2  | 21.2      |
| `steps-hot`        | -3.8   | -3.0  | -4.2   | 4.2       |
| `bursts`           | -28.5  | -20.0 | -21.2  | 21.2      |
| `polarity`         | -47.0  | -20.0 | -21.2  | 21.2      |
| `pilot-19600`      | -30.0  | -20.0 | -21.2  | 21.2      |
| `pilot-11025`      | -30.0  | -20.0 | -21.2  | 21.2      |
| `pilot-12000`      | -30.0  | -20.0 | -21.2  | 21.2      |
| `chirps`           | -31.4  | -20.0 | -21.2  | 21.2      |
| `levels`           | -29.5  | -20.0 | -21.2  | 21.2      |
| `levelcal-hot`     | -5.5   | -0.1  | -1.3   | 1.3 short |
| `separation`       | -20.1  | -20.0 | -21.2  | 21.2      |
| `dr60`             | -60.0  | -20.0 | -21.2  | 21.2      |
| `silence`          | -240.0 | -20.0 | -21.2  | 21.2      |
| `dither`           | -93.3  | -20.0 | -21.2  | 21.2      |
| `lsb`              | -74.4  | -20.0 | -21.2  | 21.2      |
| `dc-step`          | -20.0  | -18.9 | -20.1  | 20.1      |
| `imd-smpte`        | -13.7  | -12.0 | -13.2  | 13.2      |
| `imd-smpte-hot`    | -4.7   | -3.0  | -4.2   | 4.2       |
| `imd-ccif`         | -15.0  | -12.0 | -13.2  | 13.2      |
| `imd-ccif-hot`     | -6.0   | -3.0  | -4.2   | 4.2       |
| `jtest`            | -21.1  | -20.0 | -21.2  | 21.2      |
| `jtest-hot`        | -3.0   | -2.9  | -4.1   | 4.1       |
| `alias`            | -20.2  | -19.9 | -21.1  | 21.1      |
| `cal`              | -20.6  | -16.2 | -17.4  | 17.4      |
| `verify`           | -20.1  | -20.0 | -21.2  | 21.2      |
| `program`          | -24.0  | -11.2 | -12.4  | 12.4      |
| `program-pilot`    | -23.9  | -10.4 | -11.6  | 11.6      |
| `track`            | -21.4  | -9.0  | -10.2  | 10.2      |

### Dig, +0.00 dB

| Signal             | Level  | Peak  | At rec | Room      |
|--------------------|--------|-------|--------|-----------|
| `sweep`            | -20.1  | -20.0 | -20.0  | 20.0      |
| `sweep-quick`      | -20.0  | -20.0 | -20.0  | 20.0      |
| `sweep-hf`         | -20.0  | -19.2 | -19.2  | 19.2      |
| `multitone`        | -20.4  | -16.9 | -16.9  | 16.9      |
| `multitone-pink`   | -20.4  | -16.1 | -16.1  | 16.1      |
| `noise-white`      | -20.0  | -8.7  | -8.7   | 8.7       |
| `noise-pink`       | -20.0  | -9.4  | -9.4   | 9.4       |
| `noise-white-same` | -20.0  | -9.2  | -9.2   | 9.2       |
| `noise-pink-same`  | -20.0  | -9.5  | -9.5   | 9.5       |
| `levelcal-hot`     | -5.5   | -0.1  | -0.1   | 0.1 short |
| `silence`          | -240.0 | -20.0 | -20.0  | 20.0      |
| `dither`           | -93.3  | -20.0 | -20.0  | 20.0      |
| `lsb`              | -74.4  | -20.0 | -20.0  | 20.0      |
| `dc-step`          | -20.0  | -18.9 | -18.9  | 18.9      |
| `jtest`            | -21.1  | -20.0 | -20.0  | 20.0      |
| `jtest-hot`        | -3.0   | -2.9  | -2.9   | 2.9 short |

<!-- /levels -->
