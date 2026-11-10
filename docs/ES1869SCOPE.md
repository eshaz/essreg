# es1869clock and es1869scope

es1869clock logs the ES1869's clock against the machine's clocks, real time
and other cards' crystals. es1869scope draws that log on the framebuffer of
a virtual terminal and follows it as it grows. Both are in
`linux/tools/es1869scope/`, under the GPL version 3 or later, and `make`
there builds them, with alsa-lib and zlib. Its text needs a console font of
kbd's, 8 by 16.

`make install` puts both programs in PREFIX's bin (`/usr/local` unless
given), and `make install-services` puts two OpenRC services in
`/etc/init.d`, pointed at the programs there, with the rotation of
es1869eq's log in `/etc/logrotate.d`. A package passes `DESTDIR` and
`PREFIX=/usr`. The services run at boot once added to the default runlevel:
* **es1869cal** calibrates the record path before MPD starts ([The
  calibration](#the-calibration-of-audio-1s-correction));
* **es1869eq** follows the corrections over time beside MPD ([Corrections
  over time](#corrections-over-time)).

```
make -C linux/tools/es1869scope install install-services PREFIX=/usr
rc-update add es1869cal default
rc-update add es1869eq default
```

`make -C linux dist` packs these with es1869test.

## What is measured

Four comparisons run at once:
* The chip against the raw system clock. snd-es1869 stamps every interrupt
  that feeds its Clock mode with the raw clock, as the handler begins, and
  lists the last 512 in `/proc/asound/card0/es1869_irqs`. Against the line
  of the master clock's ticks, each one's lateness shows.
* The raw clock against real time, as chronyd steers the system clock: the
  three clocks and adjtimex each second, and `chronyc -c tracking` each
  minute.
* The chip against a second card's crystal: a pilot of 19600 Hz played on
  Audio 1, which the second card records. Its phase, in blocks of 0.1 s of
  the capture, is a time error to about 2 ns.
* The second card against a third: a pilot of 12000 Hz from the second
  card's outputs 3 and 4, recorded by the third card.

The chip and the system timer very likely run off one crystal, so the first
comparison shows only interrupt timing. The crystal's own wander shows
against real time, coarsely, and against the other cards' crystals, finely.
Two pairs measured at once that share a card give each crystal's own noise,
the three-cornered hat: the shared card's Allan variance is minus the Allan
covariance of the two pairs.

## es1869clock

```
es1869clock [-o LOG] [-t SECONDS] [-l DB] [-V LEVEL] [-I] [-e PLAY]
            [-a CAPTURE|none] [-A NAME]
            [-p PLAY|none] [-c CAPTURE] [-B NAME]
```

* `-o` is the log, `/var/tmp/es1869clock.log` by default, and `-t` the
  length of the run, until a signal by default.
* `-e` plays the chip's pilot, Audio 1 by default, at `-l` dBFS, -30 by
  default. The pilot leaves the chip through Audio 1's volume, which the
  Armada's saved mixer holds at 0: `-V 12` sets it for the run and puts the
  old value back at the end, also into the log in case the end is a crash.
* `-a` records it, the Focusrite by default (`plughw:CARD=USB,DEV=0`), as
  the pair `-A`, `ef` by default.
* `-p` plays the second pilot on its outputs 3 and 4, the Focusrite by
  default, and `-c` records it, the VX Pocket by default, as the pair `-B`,
  `fv` by default.
* `-a none` and `-p none` leave a pair out. `-a plughw:CARD=VXPocket,DEV=0
  -A ev -p none` times the chip against the VX alone, and `-a none -p none`
  plays the chip's pilot for its interrupts and records nothing.
* `-I` leaves the interrupts out of the log.

A pilot is a table of whole cycles at its DAC's nominal rate, so that its
pitch is that DAC's clock. A stream with 20 errors in ten seconds is closed
for the rest of the run, so that a failing USB device isn't asked for a
prepare after every one.

The log has a line for each thing, a letter and then fields:
* `s RAW REAL BOOT FREQ_PPB TICK STATUS ESTERR_US`, each second.
* `k MASTER_PPB STEERED_PPB STEP_PPB`, the driver's clock lines.
* `i SEQ SRC TICKS RAW`, each stamped interrupt.
* `p PAIR RAW X_NS AMP`, each pilot block: X_NS grows while the first card
  of the pair runs ahead of the second.
* `t CSV`, chronyc's tracking.
* `w TEXT`, what went wrong.

The clocks are in ns. A block's RAW is its middle, on its capture's line
against the raw clock.

## es1869scope

```
es1869scope [-l LOG] [-d FB] [-s FILE.ppm] [-W WIDTH] [-H HEIGHT]
            [-v 1|2|3] [-c CAPTURE] [-r RATE] [-i FILE.wav]
            [-x PLAYED.wav] [-p PLAY] [-a CAPTURE] [-k CTL] [-T SECONDS]
            [-m MONITOR] [-L DBFS] [-G PCM,ADC] [-C] [-E] [-O LOG]
            [-P STATE] [-A STATE] [-S STAGE]
```

It takes a virtual terminal's framebuffer, `/dev/fb0` by default, gives the
terminal back at a switch away and redraws at a switch back, and puts text
mode back at the end, also after a signal, a crash or a quit (Ctrl-\);
Ctrl-Z does nothing while the picture shows, since a stopped program would
leave the terminal in graphics. Its font is the first console font it finds
in `/usr/share/consolefonts`, `/usr/share/kbd/consolefonts` or
`/lib/kbd/consolefonts`. `q` quits, space holds the picture and `r` redraws.
`-s` draws the log once into a PPM picture, at `-W` by `-H`, 800 by 600 by
default. `-T` counts on the screen as in a picture: `-C` fits its taps after
`-T` seconds of the loop, and the blocks of `-E` last `-T` seconds.

What goes wrong, such as a device another program holds or a stream that
stopped, is said across the bottom of the screen for 15 seconds, since text
written to the terminal under the picture isn't seen. Each message also goes
to stderr, the service's log among them, and those that came while the
picture showed are printed again once the terminal is text. A message the
same as the one before is counted rather than printed again, so that a
stream refused once a second doesn't fill a log.

With no clock log yet, the clock view draws empty and says why in its status
line, and it shows the log once es1869clock writes one.

The clock view has four panels:
* Each pair's time error over the run, with its line taken out.
* The Allan deviations of the pairs, and of each crystal where two pairs
  share a card.
* The lateness of each stamped interrupt, on a log scale, with how many are
  pulses, beyond eight median deviations, and the period they keep, if any.
* The spectrum of the first pair's time error, where a long resonance would
  stand out.

A stream's restart, or a stall of its DAC, leaves a gap that the master's
ticks don't count, so the lateness is taken against a line for each stretch
between them. The line goes through the earliest interrupt of each second,
since lateness only ever adds.

## On the console beside es1869eq

While es1869eq runs, `es1869scope` with no options attaches to it: it maps
the state that the engine shares (below), asks it to watch every few
seconds, and opens no audio device, since the service holds them. A member
of the audio group can run it on any console; `-A STATE` attaches to another
state. Its views:
* **5, the chain** (the first): what the audio goes through, from Audio 2's
  stream to the line out and back on Audio 1, a stage a row with what it
  does now and the key that switches it. Beside the list, the stage that `j`
  and `k` (or the arrows) choose shows what it does to the sound, with the
  driver's own description of its setting under it.
* **1, the clock:** the chip's rate on the steered clock, what the driver
  applies and the residual, a block at a time, and how late each of Audio
  2's newest interrupts came, from `/proc/asound/ES1869/es1869_irqs`.
* **2, the spectrogram:** what the DAC took, at the DAC's own rate, over
  what came back on Audio 1, a row ten times a second.
* **3, the loop:** the response against 1 kHz, the phase past the delay,
  what isn't the path, and the impulse response, once a second.
* **4, over time:** each band's deviation a block at a time as colours, and
  the correction's change at 20 kHz and the trim under it.

The stages and what each picture shows:
* **Audio 2's stream:** its rate, format and state; what plays and what
  comes back, the newest spectrum of each.
* **[r] rate correction** (Off, Trim, Clock): the clock as the clock view
  has it.
* **[d] rounding dither:** the floor under the music.
* **[t] treble correction:** the DAC's droop at the rate in use, the
  correction's live taps, and what comes out of both.
* **[o] output rate** (1x, 2x, 3x): what the DAC took up to half its own
  rate, where the images of the stream's top would lie, with the hold's
  droop.
* **[z] zero step:** the DAC's level against its code near 0, alone and with
  the remap, over how often the music comes to each code.
* **The DAC's own stages:** ESS's oversampling and filters, and the volumes,
  shown only.
* **[h] live corrections:** the loop's deviation at the start and in the
  last block, and the correction and the trim over the blocks. `h` holds the
  engine's refits and trim, or lets them go on.
* **[a] Audio 1's correction:** the record path as calibrated, the
  correction's taps, and their product.
* **[l] Audio 1's linearity:** what the record path adds to a sample about
  its centre and what the stage leaves of it, and the gain the stage leaves
  by envelope in each band of the instantaneous frequency
  ([LOOP_TRANSFORM.md](LOOP_TRANSFORM.md#audio-1s-own-curve)).
* **The engine:** its state, why it stands still if it does, and its run.

A key switches its stage's control as the user, and the row reads `pending`
until the control and the engine agree with it, or the bar under the views
says it wasn't taken. The first switch of the sound holds the engine's
refits and trim, so that only the switch changes what is heard during a
comparison. 3x first asks the engine to rest Audio 1, since the DAC stalls
while Audio 1 records. When the viewer quits, the switches go back to what
they were when it attached, and the hold it made ends, unless `K` keeps
them. `R` draws everything again, space holds the picture, and `q` quits.
The panels are drawn again at most twice a second and the spectrogram a row
at a time, so that the viewer stays light beside MPD and the engine. `-s`
with `-A` draws one view of a state into a picture, `-v` choosing the view
and `-S` the chain's stage, from 0.

## The spectrogram

`es1869scope -v 2`, or the key 2, shows a capture as a spectrogram: `-c`
names the device, the VX by default, and `-r` its rate, 48000 Hz by default,
and `-i FILE.wav` takes a recording instead. The capture is opened only
while the view shows, so it doesn't hold a device es1869clock needs. The
frequency runs across to half the rate and the time down, in a waterfall
that wraps, with a line at the newest row and the latest spectrum under it.
A block of 1024 samples goes through a fixed-point FFT, `fft.c`, whose
butterflies run two at a time in MMX: one pmaddwd gives the real parts of
both products and another the imaginary ones. Its C loop does the same
arithmetic, and `tests/test_es1869scope.py` holds the two to the bit. The
magnitudes go to dB through a bit scan and a table, with no floating point
per bin, from 0 dB for a full-scale sine down to the colour map's black at
-100 dB. A 1024-point FFT takes 8 us on Ethan's PC; the status line shows
its time on the machine it runs on.

## The loop view

`es1869scope -v 3`, or the key 3, measures the ES1869's internal loop:
* **What it plays:** noise on Audio 2 (`-p`, `hw:CARD=ES1869,DEV=0,SUBDEV=0`
  by default), independent between the channels, at -12 dBFS rms.
* **What it records:** Audio 1 (`-a`, `hw:CARD=ES1869,DEV=0`), through the
  record mixer of the card `-k`.

While the view shows, the record mixer takes Audio 2 alone:
* record source Mix, 69h at 14 and the ADC's level at 3, the calibrated
  levels of [TEST_LEVELS.md](TEST_LEVELS.md), -1.8 dB in all;
* every other record input at 0, the mic boost off;
* the master switched off, so that nothing reaches the speakers;
* Audio 2's rate correction off, so that what plays is what was written;
* Audio 2's output rate at 2x at most, since 3x can't run while Audio 1
  records.

Each control's old value goes to `/var/tmp/es1869scope.UID.mixer` before it
changes. The view puts the controls back when it closes, also after a
signal, and the next start puts back what a run that ended without doing so
left in that file. The device that is busy, as Audio 2 is while MPD plays,
stops the view before anything changes.

`-L` sets the noise's rms level, -12 dBFS by default. `-G PCM,ADC` sets the
record mixer's two gains, "PCM Capture Volume" (69h) and "Capture Volume",
14 and 3 by default, the calibrated levels.

`-m MONITOR` takes what Audio 2 plays anyway as what went in, from the
monitor, PCM device 3 (`hw:CARD=ES1869,DEV=3`,
[LINUX_MONITOR.md](LINUX_MONITOR.md)), in place of the view's own noise. The
view then plays nothing:
* **What stays:** the music plays on, and the master, Audio 2's rate
  correction and its treble correction stay as they are, so the keys `t` and
  `o` do nothing.
* **What it sets:** only the record mixer, as above.
* **3x:** Audio 2 at 3x would stall while Audio 1 records, so the view
  doesn't start then.
* **2x:** the view takes every other frame of the monitor. That folds back
  only the images the driver's filter leaves between 22.05 and 24.1 kHz, to
  above 20 kHz.
* **Pauses:** a pause or a stop of the music stops the monitor, and the view
  starts again once it plays.
* **The lag:** it comes from the latest 65536 frames of each stream, once
  the music has played that long, and again every 32768 frames while the
  music is too quiet or too narrow to give one.
* **Music in mono:** where the two sides of the music are nearly one, the
  four paths can't be told apart. Each side's path is then taken alone, and
  the crosstalk isn't shown at those frequencies.

The view finds the lag between the two streams once, from the
cross-correlation of their first 65536 frames, either way up, since this
loop inverts. Both streams run from one crystal, so the lag holds. An xrun,
or a change of a setting, moves it, and the view then starts again.

Each segment of 8192 frames, half over the last, gives the spectra of what
went in and what came back, two channels at a time through one complex FFT
(`loop.c`). The four paths, each output from each input, are solved together
against the inputs' cross-spectrum. Taken one at a time, the crosstalk would
read the small correlation that two independent noises still have over a few
seconds, about -16 dB after 4 s, instead of the path's -60 dB and below.

The four panels:
* the magnitude of each side against its gain at 1 kHz, with its figures at
  10, 16, 20 and 22 kHz: how much filter the loop has;
* the phase past the delay, which is the line fitted to the phase from 200
  Hz to 3 kHz, weighted by the coherence;
* what isn't the path: 1 - the coherence, the noise's share of each bin, and
  the crosstalk, each side's against its own path;
* the impulse response, from the response by the inverse FFT, upright where
  the loop inverts.

The status line shows the gains at 1 kHz, the lag and the delay past it in
frames, and how long the right side lags the left. The keys work as follows:
* `c` starts again;
* `t` turns Audio 2's treble correction on or off;
* `o` switches Audio 2's output rate between 1x and 2x.

The view comes back with each, under its new setting.

`-x PLAYED.wav -i BACK.wav` takes what was played and what came back from
two WAV files (16 or 24-bit, one or two channels, the same rate) instead.
`-s FILE.ppm` draws the picture once. It does that for the files, or after
`-T` seconds of a live loop, 12 by default. It then writes the figures to
standard output, a line each:
* the lag and the delay;
* whether each side inverts;
* the gains;
* the magnitude, phase, incoherence and crosstalk at a list of frequencies;
* the impulse response's middle taps.

`tests/test_es1869scope.py` reads a known path back from two files that way:
a filter on one side, a gain on the other, both inverted, late by 1234
frames, with crosstalk at -40 dB.

On the Armada, with Audio 2 at 1x and 2x:
* **Coherence:** 0.99998 or better from 50 Hz to 20 kHz, which is what one
  crystal for both converters gives.
* **The level:** -9.2 dB at 1 kHz on the left, -9.0 on the right.
* **The low end:** a corner near 7 Hz, -0.4 dB at 20 Hz.
* **Crosstalk:** -58 to -71 dB.
* **Between the sides:** the right lags the left by 2.2 us.

At 1x:
* **Without the treble correction:** -1.5 dB at 10 kHz and -3.9 dB at 20 kHz
  on the left, close to what the DAC's hold loses alone.
* **With it:** the left stays within +0.25 dB to 22 kHz, and the right is
  1.2 dB low from 14 kHz up.

At 3x:
* **The stalls:** Audio 1's recording costs the DAC stops, so the delay
  jumps by up to 16 frames at a time and the coherence falls.

At 1x the ADC samples in step with the DAC and folds the DAC's images back
into the band. The top of the loop's response therefore depends on where in
a frame the two sample:
* **That place:** the delay past the lag in the status line.
* **When it moved:** three starts in a row read 0.100 of a frame and gave
  the same response to 0.01 dB. One start right after a 3x stream read 0.54,
  and 20 kHz came out 12.8 dB lower.

So a correction taken from the loop at 1x holds only for that place, and the
view shows the place it was taken at.

## The calibration of Audio 1's correction

`es1869scope -C` calibrates the correction filter on Audio 1's recordings
(`a1c_filter`, `capfilt.c` and `a1filter.c` in the driver).

What it measures and why:
* **The record path alone:** it plays noise on Audio 2 at 2x with the treble
  correction on. The DAC is then flat to 20 kHz within 0.02 dB, and at 2x
  the DAC's images lie far above the ADC's band. So the loop's response is
  the record path's own: 69h, the switched-capacitor filter (whose corner
  ESS sets at 0.875 of half the rate, register A2h) and the ADC.
* **The settings:** the record mixer takes Audio 2 alone at the calibrated
  levels, with the master off, as in the loop view. Before the measurement
  the correction filter goes off.

What it does then:
1. After `-T` seconds (12 by default, 3 at boot) it fits each channel's 33
   taps by weighted least squares to the inverse of the measured response
   against 1 kHz. The fit runs up to 20 kHz, holds that value above, and
   lifts 12 dB at most.
2. It loads them into "Audio 1 Correction Capture Taps" and switches "Audio
   1 Correction Capture Switch" on.
3. It measures the loop again for `-T` seconds, through the filter.
4. It puts back every other control it set. The filter and its taps stay.

With `-s` it writes the verification's picture, and the report gives the
taps, each channel's largest gain, and the response before and after at 1,
5, 10, 15, 18 and 20 kHz. From two files (`-x`, `-i`) it fits and shows the
taps without loading them.

`linux/config/es1869cal.initd` is the OpenRC service that runs it at boot,
after alsasound and before MPD, so that MPD finds Audio 2 free. It writes
`/var/lib/es1869/cal.txt` and `cal.ppm`. A run that finds no loop leaves the
filter as it was and says so.

`es1869scope -C -T 6` on the Armada, 2026-11-08, with test build 4 (dB
against 1 kHz):

| record path  | 5 kHz | 10 kHz | 15 kHz | 18 kHz | 20 kHz |
|--------------|-------|--------|--------|--------|--------|
| left before  | -0.19 | -0.90  | -2.18  | -3.25  | -4.09  |
| left after   | -0.01 | -0.01  | -0.00  | -0.02  | -0.05  |
| right before | -0.29 | -1.28  | -2.93  | -4.26  | -5.27  |
| right after  | -0.01 | -0.01  | -0.00  | -0.01  | -0.05  |

The taps lift the left by 4.2 dB at most and the right by 5.4 dB.

## Corrections over time

[LOOP_TRANSFORM.md](LOOP_TRANSFORM.md) explains this measurement and its
differential logic in full, and [CLOCK_CORRECTION.md](CLOCK_CORRECTION.md)
the clock's.

`es1869scope -E` follows the corrections while the music plays. It reads
what Audio 2's DAC took through the monitor and what came back on Audio 1
through the record mixer, in blocks of `-T` seconds (a minute from the
service). Fast changes are not its business: each correction answers a
trend, a block later.

The music it measures can change under it, and it follows:
* **The rate:** it reads the stream's own rate from Audio 2's `hw_params` in
  /proc and records Audio 1 at that rate, 44.1 or 48 kHz.
* **3x:** at 3x, which stalls the DAC while Audio 1 records, or at a rate
  Audio 1 can't record, Audio 1 rests while the monitor runs on. Its reader
  watches the controls' events and stops Audio 1 the moment Audio 2 goes to
  3x, since a step of the measurement can take 0.4 s.
* **What counts:** music goes into the trends and the refits only as it was
  measured at their start: at 44.1 kHz, where es1869cal calibrated the
  record path, at 2x, since at 1x the top of the loop moves from run to run,
  and with Audio 1's correction as it was. Other music is measured and
  shown, a note in the log says why it isn't counted, and it counts again
  once the setup comes back.
* **The trim** moves only in the Clock mode, for the 44.1 kHz family, whose
  clock its trend follows; otherwise the clock line says it is held.
* **Audio 1's correction** or its linearity switched starts Audio 1 again,
  since Audio 1 takes the switch at its next start.
* **Another program's changes:** a control that es1869eq set and another
  program changed stays as that program set it, also after es1869eq stops,
  and a DAC correction that another program loads becomes the refits' start.

After each block:
* **Audio 1's linearity:** with 400,000 frames a side behind it, the record
  path's curve and what the stage left, from the analytic signals of what
  should have come back and of what came
  ([LOOP_TRANSFORM.md](LOOP_TRANSFORM.md#audio-1s-own-curve)), and the
  stage's next tables loaded, a `lin` line in the log: each side's centre,
  the path at five knots and what the stage left at most, LSB. Whether Audio
  1 takes the stage is `a1c_linear`'s; the tables are loaded either way, and
  while the stage is off -E runs it in software to measure what it would
  leave.
* **The DAC:** the loop's deviation against 1 kHz goes into 24 bands from
  250 Hz to 20 kHz, each band weighted by its coherence, since music may
  leave a band empty. The monitor's frames carry Audio 2's DAC correction,
  so the loop against them doesn't see it: through the record path that
  `es1869scope -C` calibrated, the loop is the DAC itself. The first 3
  blocks give each band's start, when the calibration had just made the
  chain flat. Each band keeps a trend, a line through its last 16 blocks,
  predicted a block ahead. Once the trend has 3 blocks more than the start,
  the correction (the treble correction's table at the rate in use) takes
  back what it drifts from the start:
  * a step of 0.5 dB at most an update;
  * within 3 dB of where it began;
  * nothing for less than 0.05 dB.

  The table's 16 taps are fitted to the bands by least squares. Most bands
  lie low, where every tap's response looks alike, so the fit alone is all
  but singular; a ridge towards the taps before keeps the table where the
  bands leave it free, above 20 kHz among them. The table is loaded live,
  and the driver swaps it in between the filter's runs. Without a calibrated
  record path, the deviation is the record path's own loss, and the
  correction stays.
* **The clock:** the monitor's positions give the chip's rate against the
  steered clock. The driver reads each from the 8237 right before the core
  stamps it on the monotonic clock. A line goes through each run of the
  music between its breaks, after the run's first second, and the runs'
  slopes are pooled. Audio 1's own position would not do: once its
  correction filter runs, it moves a period at a time. With the ratio that
  the Clock mode applies on top, from the driver's /proc page, the chip's
  rate gives the stream's residual against the steered clock. Less the trim,
  the residual is the driver's own error. That error keeps a trend,
  predicted a block ahead, and `a2_trim_ppb` takes it back whole (as root),
  0.5 ppm a block at most. A trend of the residual itself would follow the
  trim's own steps: on the Armada it overshot from -0.65 to +0.18 ppm in
  eight blocks while the error stayed at +1.2 ppm. The log's `tie` is the
  positions' time error about the runs' lines, in us rms. It is the DMA's
  steps through its FIFO and the stamps' timing, which hide the crystal's
  own jitter: that needs a second clock (es1869clock).

* **The noise:** what came back that the music, through the loop's measured
  response, doesn't explain: each output's power times one less its multiple
  coherence, in each band, its density in dB a Hz against a full-scale sine.
  The top band reaches past 20 kHz, where at 2x the monitor's every other
  frame and the ADC fold the filter's images back differently, so its noise
  is mostly that. It holds the ADC's noise, the DAC's and the record path's,
  the music's distortion and any interference, which the loop can't tell
  apart: the DAC's own needs a second ADC on the same output, whose noise is
  its own.

* **The overlay:** each segment's input and output, both channels summed,
  are compared by their Pearson correlation at the lag and a frame either
  way. Its size at the lag says whether the two still overlay: a segment
  under 0.5 is left out of the sums, and 8 in a row end the run, after which
  the lag is found again. A stop of the DAC or a lost block moves one
  against the other that way: on the Armada one such block, before the
  check, read the DAC 0.9 dB low at 7.7 kHz, and a refit followed it. The
  parabola through the three gives the delay between frames, segment by
  segment; within the loop it holds still, since the DAC and the ADC share
  one crystal. A block's clock rate 0.5 ppm off its trend is left out until
  a second agrees with it.

Every block writes its lines to `-O LOG` and, with `-s`, the picture every
fifth block. The first `base` line gives each band's start:
```
63 base 250:-0.01 443:-0.01 ... 13663:-0.62 20000:-1.58
210 clock chip -28.9778 tie 5.7 applied +29.0550 resid +0.0772 ...
210 overlay r 0.99550 delay +0.101 sd 0.011 missed 0
210 eq 250:-0.01 ... 20000:-2.08 drift 20000:-0.28 corr +0.28 (refitted)
210 noise 250:-115.0 443:-115.4 ... 13663:-118.7 20000:-107.5
```

The fields, by line:
* **clock:** the chip's rate on the steered clock (ppm), the time error
  (us), the driver's ratio and the residual (ppm), then the driver's own
  error, its trend a block ahead and the trim in ppb. This one goes on `bias
  -0.1838 pred -0.2818 trim 282 (moved)`. Outside the Clock mode, or on the
  48 kHz family, the line ends at the trim with why it is held.
* **overlay:** |r| at the lag, the delay between frames, its spread, and the
  segments left out.
* **eq:** the deviation against 1 kHz, the largest predicted drift, and the
  correction's change at 20 kHz since it began.
* **noise:** the density in dB a Hz.
* **zstep:** the loop's step at the sign of the DAC's codes on each side in
  LSB, from the music, logged only. Through Audio 1 it is the DAC's gap less
  what the record path's own soft dead zone takes back, near 0 on the Armada
  ([LOOP_TRANSFORM.md](LOOP_TRANSFORM.md#the-dacs-gap-at-its-sign)), so the
  driver's `a2_zstep_l` and `a2_zstep_r` are never told it.
* **note:** a message, as the screen or stderr would show it, such as `1834
  note nothing played for 10 s, Audio 1 rests until the music comes`.

On the Armada (2026-11-08, test build 4, 20-s blocks), the record path was
tilted halfway through a run: Audio 1's correction taps, refitted 0.5 dB
lower at 20 kHz. -E read the step as the DAC's: -0.50 dB at 20 kHz and -0.24
dB at 13.7 kHz. The DAC's correction took it back in 5 blocks (+0.11, +0.28,
+0.40, +0.48 and +0.54 dB at 20 kHz). It overshot to +0.67 dB at 9 blocks,
the trend's line through a step, and was back to +0.55 dB at 18. A slow
drift, which the line follows, gives no such overshoot. Through it all the
overlay held |r| at 0.995 and the delay at +0.10 frames, spread 0.01.

It holds Audio 2 at 2x with the treble correction on, and puts both back
when it stops. 3x stalls while Audio 1 records, and at 1x the top of the
loop's response moves with where the ADC and the DAC sample in a frame.

A break of the music (a pause, a stop, the next file of a player that closes
the device between files) stops the monitor. The view starts the monitor
again on its own, at the rate it ran, while Audio 1 records on, and finds
the lag again. Each start puts the DAC and the ADC another fraction of a
frame apart, and the monitor's every other frame at 2x half a frame either
way. So the cross-spectra of two runs don't add at the top, where their
phases differ by up to 80 degrees: mixed, they took up to 2.3 dB off 20 kHz
on the Armada. Each run is measured alone, and its deviation in each band
goes into the block, weighted by its segments: a block is `-T` seconds of
music, however broken. A change of the DAC's rate starts the block afresh.
Music at 48 kHz isn't followed, since Audio 1 records at 44.1 kHz. A thread
of its own reads both streams, so that the work of a block, which can take
the Celeron longer than Audio 1's buffer lasts, holds up neither.

After 10 s with no frame from the monitor, nothing plays: the run ends,
Audio 1 and the reader stop, and the view only asks the monitor twice a
second whether frames have come, which start both streams again as after a
break. Beside an idle MPD on the Armada, with Audio 1 recording on, es1869eq
had taken 6 % of the CPU and more than doubled the CPU's entries into C2,
129 a second against 47. Resting, it takes 0.7 % and 47 entries a second, as
the machine without it. It rested 10 s after the music ended and came back
with the music's first frames.

`linux/config/es1869eq.initd` runs it as a service after es1869cal and MPD.

### -E over files

`es1869scope -E -x MONITOR.wav -i RECORDING.wav` replays two files recorded
at once, what the monitor took (at once or twice the recording's rate) and
what Audio 1 recorded, as the gap study's takes have them: the rings are fed
as the reader feeds them, from about where the two line up, through the same
segments, three passes over, each with the stage's tables from the one
before, and the `lin` lines go to `-O` or stdout. It opens no device and
loads nothing. `LV_LIN_TABLES=FILE` in the environment writes the tables the
passes came to, a side a line (the centre, the curve's 33 values, the 40
gains and the 40 turns), and `LV_LIN_FROM=FILE` starts from such a file.

## Shared with a viewer

`-E` shares what it knows in a file that a viewer maps: `-P STATE`, or
`/run/es1869/eq.state` when that directory is writable, as es1869eq's script
makes it (root's, with the audio group). The engine writes it between two
steps of a sequence count, and a viewer copies it whole, taking the copy
only if the count was even and the same before and after. The engine holds a
lock on the file for as long as it runs, so a file that a crash left behind
reads as stale, and the lock gives the engine's pid. The layout is
`eqstate.h`, of 32-bit fields only, so the i386 engine and a 64-bit host lay
it out alike.

It holds, all as the engine applied them:
* the state (measuring, finding the lag, resting, Audio 1 at rest and why);
* the stream's rate, the DAC's factor, Audio 1's rate and correction, the
  rate correction's mode, the treble correction, and whether the refits are
  held;
* the run's lag, delay, overlay, segments and blocks;
* while a viewer watches, ten times a second, the levels of the monitor and
  of Audio 1 and a row of each one's spectrogram: the monitor's at the DAC's
  own rate, from all its frames, so that 2x and 3x show above 22 kHz; once a
  second, the run's response at 128 points from 20 Hz to 20 kHz, with its
  coherence and impulse response;
* the DAC's correction now and at the start, Audio 1's taps, and how often
  the monitor's codes came to each value from -16 to 16;
* the last 64 blocks, as the log has them, and the bands as they stand.

A FIFO beside it, `eq.cmd`, which the audio group may write, takes a command
a line, a word and a number, whose number the state echoes once it is done:
* `watch N`: the live parts for the next 10 s;
* `hold N` and `resume N`: the refits and the trim held, or going on;
* `rest N`: Audio 1 at rest now, before a viewer sets 3x, so that the DAC
  doesn't stall while the reader sees the change; the monitor's next start
  decides again, or 10 s later;
* `restart N`: a new run;
* `rebase N`: the trends start again from the next blocks, after a change of
  setup that is to stay.

Nothing in a command carries a value or a path. The audio group may already
write every control; holding the trim is the one power a command adds.

## The DAC and the ADC apart

The loop gives the DAC and the ADC together. The VX Pocket, cabled both
ways, splits them. The setup:
* **Cabling:** the ES1869's output to the VX's input, L to L and R to R. The
  VX's output goes to the ES1869's Aux input (AUXB), which arrives with left
  and right swapped.
* **The anchor:** Audio 2 at 2x, whose DAC's response below 20 kHz is known
  from the hold and the 0.062 of the last sample that each sample keeps
  ([AUDIO_PIPELINE.md](AUDIO_PIPELINE.md)).
* **The method:** dividing that model out of the loop at 2x leaves the
  record path. Dividing it out of the VX's recording at 2x leaves the
  ES1869's output stage with the VX's input. The DAC at 1x is then the VX's
  recording at 1x over that chain.

Measured 2026-11-08, with noise independent between the channels at -12 dBFS
rms and a 2 x 2 solve, at the calibrated levels of
[TEST_LEVELS.md](TEST_LEVELS.md) (in dB against 1 kHz, the left channel):

|                     | 10 kHz | 15 kHz | 18 kHz | 20 kHz |
|---------------------|--------|--------|--------|--------|
| 1x DAC, measured    | -1.19  | -2.49  | -3.43  | -4.11  |
| 1x DAC, the model   | -1.16  | -2.51  | -3.49  | -4.21  |
| 1x DAC, treble on   | -0.01  | +0.10  | +0.15  | -0.07  |
| record path, 69h    | -0.93  | -2.23  | -3.34  | -4.21  |
| output and VX input | -0.65  | -1.45  | -1.95  | -2.43  |

The right channel agrees for the DAC within 0.05 dB. Its record path rolls
off more: -1.30, -2.97, -4.31 and -5.35 dB.

So the DAC follows the model within 0.1 dB to 20 kHz. The treble correction,
as it was before its refit to 22.05 kHz, held it within 0.15 dB, which was
its design. The record path keeps 4 to 5 dB of loss at 20 kHz, which Audio
1's correction filter takes back.

The refitted treble correction (test build 4), measured the same way later
that evening, the output chain divided out (dB against 1 kHz, L / R):

|         | 5 kHz       | 15 kHz      | 19 kHz      | 20 kHz      |
|---------|-------------|-------------|-------------|-------------|
| 1x, off | -0.30/-0.31 | -2.47/-2.47 | -3.78/-3.78 | -4.13/-4.10 |
| 1x, on  | +0.06/+0.07 | +0.03/+0.03 | +0.15/+0.16 | +0.04/+0.18 |
| 2x, on  | +0.02/+0.02 | -0.01/+0.01 | +0.03/+0.02 | +0.00/+0.01 |

Above 20 kHz, where the chain isn't known, the correction's own gain at 1x
(on over off) was +4.17 to +5.20 dB to 22 kHz. With the DAC's model that
leaves -0.09 to +0.23 dB to 22 kHz. 3x can't be measured this way: the VX's
programmed-I/O capture takes the bus each of its periods (2048 frames, 42.7
ms), long enough to stall the DAC at 3x. A 1 kHz tone came back at 986.6 Hz
with sidebands every 23.4 Hz, where 1x and 2x gave it clean. es1869test's
`rs.treble` checks 3x on the chip's own paths.

## On the Armada

With the USB WiFi stick in, 12 % of the chip's interrupts come 1 to 3 ms
late and a few up to 12 ms, against a median of 47 us for the rest. The late
ones keep the WiFi's beacons, 102.4 ms, and above all every fifth, 512 ms.
They are the stick's work at each beacon, on IRQ 11 with the USB
controllers, which the PC's interrupt controller serves before the chip's
IRQ 5. The Clock mode doesn't feel them, since its line goes through the
earliest interrupt of each second, but a client with buffers of a few
milliseconds would.

On the wired NIC, booted with `processor.max_cstate=2` (2026-11-08, an hour
of es1869clock against the VX, with a minute of load at half time):
* **Lateness:** outside the load, the interrupts came a median of 38 us
  late, 54 us at the 99th percentile, and only 4 over 1 ms, minutes apart.
  An idle CPU sleeps in C2 and pays its wake. Under the load (a CPU hog and
  a synced 200 MB write) the median was 15 us, with none over 1 ms.
* **Against the VX:** the ES1869's crystal ran 22 ppm from the VX's, with an
  Allan deviation of 1.0e-8 at 1 s, 2.5e-8 at 10 s, 1.9e-7 at 100 s and
  6.6e-7 at 1000 s, where the VX's own crystal wanders as the laptop warms.
* **The smallest period:** es1869test's `latency.min` ran both DACs without
  an xrun at 32-frame periods in a 128-frame buffer, 2.9 ms.

The Focusrite, a Scarlett 2i4 on a CardBus USB 2.0 card, streamed nothing in
the first run and then stopped answering on USB until it was unplugged.

chronyd on the Armada takes its time over the USB WiFi from NTS servers
(Cloudflare, System76), NIST's stratum 1 and the pool. A survey of them gave
each 0.6 to 2 ms of spread a sample, and NIST sat 6 ms from all the others,
most likely a route that is longer one way than the other. Real time is
known to a few milliseconds that way, and its rate to hundredths of a ppm
over hours.
