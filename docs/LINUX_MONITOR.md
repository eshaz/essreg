# The Audio 2 monitor

PCM device 3 of snd-es1869 records the frames that Audio 2's DAC took. The
driver reads them back from what Audio 2's DMA reads, just behind the DMA,
so they are exactly the samples the DAC converted, with everything the
driver did to the stream on the way: the rate correction (`a2_resample`),
the treble correction (`a2_treble`), the oversampling (`a2_oversample`) and
dmix's mixing in the buffer.

The monitor is the reference for measuring the chip's analog path from
whatever music plays. Audio 2's DAC feeds the record mixer, and Audio 1's
ADC records that loop while Audio 2 plays, on the same crystal. The dig path
(Audio 2 To FM and PCM device 1) also gives the DAC's samples, but it takes
Audio 1, so it can't run beside the loop's recording. The monitor takes no
engine at all.

## What it delivers

The monitor delivers 16-bit stereo (S16_LE) at the rate the DMA runs through
Audio 2's ring: the stream's rate times `a2_oversample`'s factor, so that a
44.1 kHz stream at 2x records at 88200 Hz. An 8-bit sample comes back in the
high byte, unsigned samples come back signed, and a mono stream comes back
on both sides, so every value is the DAC's exactly.

On a 16-bit DMA channel, such as the Armada's DMA 5, the monitor reads the
ring of words that mirror.c fills. On an 8-bit channel the chip reads the
application's buffer, and the monitor reads that instead. The Armada has no
8-bit channel for Audio 2, so that path is untested on a chip.

With Audio 2 To FM on (mixer 71h bit 0), the chip takes the frames at the
music DAC's 49716 Hz, whatever rate the stream and the monitor say. The
frames are the same; only their timing differs. es1869test's `mon.clock`
uses this to hold the monitor against PCM device 1.

## How it follows Audio 2

The monitor follows Audio 2 and never holds it up. It opens at any time,
needs neither engine, and doesn't wake a chip that is powered down.
`monitor_device=0` keeps it closed, and it refuses with ENODEV where Audio 2
has no DMA channel. When Audio 2 is prepared, the open allows only the
ring's rate, so that a program that asks for any rate gets the right one.
When Audio 2 is closed, any rate of the chip's clocks is allowed.

A started monitor takes every frame the DMA takes, from its own start, or
from Audio 2's next start if Audio 2 isn't playing. While Audio 2 doesn't
play, nothing comes, and a blocking read waits for up to a day, so that
`arecord` started before the music waits for it. A program that polls is
woken at each of the monitor's periods as the frames come.

Anything that breaks Audio 2's frames off stops the monitor with an xrun:
Audio 2's stop, a pause, an underrun, the end of a drain, a suspend, and the
restart that a write of `a2_oversample` makes under a playing stream. The
usual recovery, a prepare and a start, goes on with the frames Audio 2 plays
next. As with any capture's xrun, the prepare drops the frames the program
hadn't read yet. A change of the rate correction's mode or trim under a
playing stream is no break, since the stream doesn't restart: the frames
carry the change from where it took effect.

When Audio 2 starts at a rate other than the monitor's, the monitor stops
with an xrun, and its prepare then fails with EBUSY for as long as Audio 2
stays prepared at that rate. The program has to reopen the monitor, which
then allows the new rate. A program that reads too slowly gets an overrun,
also as an xrun, unless its stop threshold is the boundary, in which case
the monitor writes over the oldest frames as ALSA's capture does. The dual
device's Audio 2 isn't followed: the monitor waits through it.

## Timing

Audio 2's position comes from the 8237's count, and every reading of it
takes what the DMA took since the last one. That has to happen before the
ring's frames behind the DMA are filled with the next lap, which the driver
does as soon as a reading has moved hw_ptr past them and the application has
written. So the monitor takes at every reading of Audio 2's position, at its
interrupts, its timer and its pointer, and before every fill that starts
from an older hw_ptr. A timer of the monitor's own takes the rest, at each
of the monitor's period boundaries and at least every 512 frames, and wakes
the reader at the boundaries. A take holds interrupts off for at most about
512 frames' copy, four bytes a frame.

The monitor's position is the DMA's, which runs ahead of the DAC by Audio
2's FIFO of 64 bytes: 16 frames of 16-bit stereo, 0.36 ms at 44.1 kHz. A
stop drops what the FIFO holds without playing it, so the last frames before
a break may not have played. The monitor's pointer reads the 8237 right
before ALSA takes its timestamp, so that `snd_pcm_status()` gives a frame,
the frames read plus those available, and when the DMA took it. Audio 1's
recording gives the same for the frames its DMA wrote, so the two streams
line up on the timestamps, to within the two FIFOs and the analog filters'
delay, and exactly on the samples once a cross-correlation has found the
lag. Both run on the chip's master clock, so the lag doesn't drift.

The monitor costs nothing while it is closed. While it runs, it reads the
8237 once more for each fill of Audio 2's ring and at each of its timer's
ticks, and copies four bytes a frame. `/proc/asound/ES1869/es1869` has a
line for it: its state, the frames taken since the driver loaded, and the
breaks counted on Audio 2's side and as the reader's overruns.

## Measuring the analog loop

Audio 2's DAC reaches Audio 1's ADC through the record mixer: record source
5 (Mix), with Audio 2 into it at PCM Capture Volume (69h). The master can
stay muted, so that nothing reaches the speakers. Recorded at Audio 2's own
rate, Audio 1's frames and the monitor's correspond one for one:

```
amixer -c ES1869 cset name='Capture Source' Mix
amixer -c ES1869 cset name='Capture Switch' on
amixer -c ES1869 cset name='Capture Volume' 4,4
amixer -c ES1869 cset name='PCM Capture Volume' 13,13
amixer -c ES1869 cset name='Master Playback Switch' off
arecord -D hw:ES1869,3 -f S16_LE -c 2 -r 44100 dac.wav &
arecord -D hw:ES1869,0 -f S16_LE -c 2 -r 44100 loop.wav
```

Capture Volume 4 puts B4h at 0 dB, and PCM Capture Volume 13 puts Audio 2 3
dB down into the record mixer. Audio 1 records only one thing at a time, so
the FM's digital recording (PCM device 1) can't run beside the loop.

es1869test's `mon.loop` makes this measurement on noise, with the master
muted: the monitor exact, the loop's correlation with it, the lag's drift
from the first second to the last, which has to be a frame at most, and the
delay between the two by their timestamps, which it reports.

## Tests

es1869test's `mon` group ([ES1869TEST.md](ES1869TEST.md#the-groups)) plays
on Audio 2 and records the monitor at once:
* `mon.exact` and `mon.fmt.u8m` play a pattern of LFSR words, 16-bit stereo
  and 8-bit mono, with the rate correction off, and every frame has to come
  back exactly, from the stream's first, with no xrun. The monitor's
  timestamps have to run at the ring's rate within 100 ppm, and lie on that
  line within 500 us rms.
* `mon.trim` plays noise through the rate correction at +1000 ppm, and
  `mon.over.2` at 30 ppm with the ring at 2x, where the monitor records at
  88200 Hz. Every frame has to be what the same filter's C code makes here.
* `mon.clock` plays noise in the Clock mode on the dig path, where PCM
  device 1 records what the DMA took through the chip itself, and the
  monitor and PCM device 1 have to agree frame for frame.
* `mon.pause` pauses Audio 2 for 0.3 s: one xrun, the frames in order up to
  it, and after it from where Audio 2 went on, a FIFO back at most, and at
  most a buffer of the monitor's ahead, the frames its reader hadn't read.
* `mon.loop` is the measurement above.

On the simulated chip the monitor gets Audio 2's ring as the driver's filter
makes it, and two faults fail the group: a monitor that reads the ring a lap
late, after the next lap's fill, fails every case, and one that reads the
application's samples instead of the ring's fails the cases through the
filter.
