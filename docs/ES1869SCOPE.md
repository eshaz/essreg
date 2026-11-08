# es1869clock and es1869scope

es1869clock logs the ES1869's clock against the machine's clocks, real time
and other cards' crystals. es1869scope draws that log on the framebuffer of
a virtual terminal and follows it as it grows. Both are in
`linux/tools/es1869scope/`, under the GPL version 3 or later, and `make`
there builds them, with alsa-lib and zlib.

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
es1869clock [-o LOG] [-t SECONDS] [-l DB] [-I] [-e PLAY]
            [-a CAPTURE|none] [-A NAME]
            [-p PLAY|none] [-c CAPTURE] [-B NAME]
```

* `-o` is the log, `/var/tmp/es1869clock.log` by default, and `-t` the
  length of the run, until a signal by default.
* `-e` plays the chip's pilot, Audio 1 by default, at `-l` dBFS, -30 by
  default.
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
```

It takes a virtual terminal's framebuffer, `/dev/fb0` by default, gives the
terminal back at a switch away and redraws at a switch back, and puts text
mode back at the end, also after a signal. Its font is the first console
font it finds in `/usr/share/consolefonts`. `q` quits, space holds the
picture and `r` redraws. `-s` draws the log once into a PPM picture, at `-W`
by `-H`, 800 by 600 by default.

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

## On the Armada

With the USB WiFi stick in, 12 % of the chip's interrupts come 1 to 3 ms
late and a few up to 12 ms, against a median of 47 us for the rest. The late
ones keep the WiFi's beacons, 102.4 ms, and above all every fifth, 512 ms.
They are the stick's work at each beacon, on IRQ 11 with the USB
controllers, which the PC's interrupt controller serves before the chip's
IRQ 5. The Clock mode doesn't feel them, since its line goes through the
earliest interrupt of each second, but a client with buffers of a few
milliseconds would.

The Focusrite, a Scarlett 2i4 on a CardBus USB 2.0 card, streamed nothing in
the first run and then stopped answering on USB until it was unplugged.

chronyd on the Armada takes its time over the USB WiFi from NTS servers
(Cloudflare, System76), NIST's stratum 1 and the pool. A survey of them gave
each 0.6 to 2 ms of spread a sample, and NIST sat 6 ms from all the others,
most likely a route that is longer one way than the other. Real time is
known to a few milliseconds that way, and its rate to hundredths of a ppm
over hours.
