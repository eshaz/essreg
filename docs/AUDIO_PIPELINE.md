# From a Windows sound to the DAC

This page follows a Windows sound from a program to the ES1869's DAC. It
describes how the DAC plays the sound, how much time each step has, and what
can make samples go missing.

## In short

The step with the least time to spare is the DMA controller's answer to the
chip. Audio 2's FIFO holds only 0.36 ms of 44.1 kHz 16-bit stereo, while
every step in the driver has 62 ms or more. A skip therefore most likely
means that a DMA request was answered too late.

The chip has a feature for this case, the DRQ latch, which holds each DMA
request until the DMA controller answers it. The card's PnP ROM or EEPROM
switches the latch on or off at power-up, and ESS's own example ROM data
leaves it off. ESS's driver offers a second remedy, its "Use single mode
DMA" setting, which replaces demand transfers with single transfers. Neither
remedy needs a change to the driver, and [Finding out on the
card](#finding-out-on-the-card) has the steps for trying them.

ESS's drivers turn the Audio 2 DAC's 4x oversampling on, but
`build/ES1869.DRV` and the extended `ES1869.VXD` play without it and with
the DAC's filter bypassed. Each way has its cost: as the Linux driver
measured it, the 4x stage keeps the DAC flatter up to about 17 kHz and its
images about 25 dB lower, and takes 6 dB more off by 20.4 kHz ([The Audio 2
DAC](#the-audio-2-dac-oversampling-and-the-filter)).

## The path of a sound

1. A program writes buffers of samples with `waveOutWrite`, and ES1869.DRV
   (`wodMessage`, 6:18E4) copies them into a DMA buffer.
2. ES1869.VXD allocates that DMA buffer when the device starts, 32 KB for
   each DMA channel (`src/vxd/pcod.asm` 0631h passes 20h KB, `pnp.asm`
   045E-0507).
3. The driver plays the buffer as two blocks. Each block holds 1/16 s of
   audio and is at most half the buffer (6:23E8-6:2432). At 44.1 kHz 16-bit
   stereo, a block is 11,024 bytes long and lasts 62 ms.
4. The driver programs the 8237 DMA controller itself, in auto-initialize
   mode (6:2C84-6:2D4F). It programs Audio 2 with 78h = 93h, which means
   auto-initialize with demand transfers of 4 bytes (DS p.65).
5. Audio 2 reads the buffer through its FIFO, which is 32 words deep (DS
   p.65), and plays it on its DAC.
6. After each block, Audio 2 raises its interrupt. ES1869.VXD's handler
   (`lcod.asm` D1_054C) turns Audio 2's interrupt off at the chip (7Ah
   bit 6) and passes the interrupt on to Windows.
7. ES1869.DRV's handler, which the driver copies into fixed memory (3:01B9,
   3:02C3), refills the block that has finished playing from the program's
   buffers, with interrupts enabled. It then clears Audio 2's interrupt
   latch (7Ah bit 7) and sends the EOI.
8. On that EOI, ES1869.VXD turns Audio 2's interrupt back on (L1_09E8) and
   ends the interrupt at the PIC.

Both drivers save and restore the mixer index around these accesses to the
mixer.

Recording runs the other way, on Audio 1, with a 256-byte FIFO, demand
transfers of 2 bytes (B9h = 02h) and the same block sizes.

## The path of a DirectSound sound

DirectSound doesn't go through `waveOutWrite`. ES1869.VXD is a DirectSound
driver too ([VXD_INTERNALS.md](VXD_INTERNALS.md#directsound)): DirectSound
mixes every sound in software into the driver's primary buffer, 32 KB of
system memory that the VxD allocated, and the VxD plays that buffer by DMA
on Audio 2, in auto-initialize mode as ES1869.DRV does. DirectSound asks the
VxD for the DMA position and writes a little ahead of it, so a DirectSound
sound has less delay than a Windows sound, and the time each step has is
DirectSound's own lead.

On an 8-bit DMA channel the DMA plays the primary buffer itself. On a 16-bit
channel the chip takes only the low byte of each word, so the VxD plays a
second buffer of twice the size, each byte of the primary buffer in the low
byte of a word. ESS's driver fills it from a time-out: every 20 ms, 80 ms
from wherever the DMA is. The DMA then plays what the last time-out copied,
and a time-out that comes late by more than DirectSound's lead plays what
the buffer held a lap ago, which is heard as a skip. With
`DirectSoundLock=1`, the extended `ES1869.VXD` has DirectSound say what it
wrote (its `Lock` and `Unlock` calls) and copies just that, at once, so the
second buffer always holds what DirectSound has written, however late
Windows runs anything else. It is off by default, until it has been checked
on the card ([DRIVER_CONFIG.md](DRIVER_CONFIG.md#61-es1869vxd)).

## The Audio 2 DAC: oversampling and the filter

Mixer register 71h selects how the Audio 2 DAC turns samples into sound (DS
p.64):
* Bit 4 turns on 4x oversampling. The chip interpolates the samples to 4
  times the sample rate before the DAC, and the switched-capacitor filter
  (SCF) is then always bypassed.
* Bit 3 bypasses the SCF. Without oversampling, the SCF is a low-pass filter
  after the DAC. Register 72h sets its clock, and its corner frequency is
  the clock divided by 82.
* Bit 1 makes Audio 2 asynchronous, so that it runs at its own rate (70h)
  instead of Audio 1's.

ES1869.DRV and ES1869.VXD write 71h in these places:
* ES1869.DRV, at wave output open, close and resume (1:1157).
* ES1869.DRV, at each playback start (6:2DE6).
* ES1869.VXD, at its own Audio 2 start (o5:3603) and when a VM takes the DSP
  (o5:198C).

In each of these places, ESS's drivers set bits 4 and 1, so the DAC plays
the samples interpolated 4x, without the SCF. `build/ES1869.DRV` and the
extended `ES1869.VXD` clear bit 4 and set bits 3 and 1 instead, so the DAC
plays the samples as they are, also without the SCF.

The rebuilt drivers write the mode in two more places, where ESS's drivers
leave 71h alone, so that the chip holds it at all times:
* ES1869.DRV writes it right after the mixer reset of every start and resume
  (3:4897), so the mode is there before the first sound plays.
* ES1869.VXD writes it again each time a DOS program starts a DMA transfer
  on Audio 2's channel (its DMA handler, o1:0870), in case the program reset
  the mixer after it took the DSP.

Neither write happens with ESS's mode, that is, with both keys below at 1.

The writes leave the other bits as they were, such as bit 5 for 48 kHz
recording. Linux's `es18xx` driver turns the oversampling on too (71h =
32h).

Two keys in `SYSTEM.INI` choose the rebuilt drivers' mode, and both drivers
read them when Windows starts
([DRIVER_CONFIG.md](DRIVER_CONFIG.md#62-es1869drv)):
* `[ES1869.DRV] Audio2Oversampling=1` sets bit 4 again, for ESS's 4x
  oversampling.
* `Audio2Filter=1` clears bit 3, which puts the SCF to use while bit 4 is
  clear.

With both keys at 1, the drivers use ESS's mode.

The SCF is out of the way with either setting, because 4x oversampling
bypasses it too. The only difference between ESS's mode and the rebuilt
drivers' default is therefore the 4x interpolation. Without it, the DAC
holds each sample for one sample period, which loses a little treble by a
known amount: at 44.1 kHz, 0.75 dB at 10 kHz, 1.7 dB at 15 kHz and 3.2 dB at
20 kHz.

The hold isn't all of it. The same tones played at twelve rates from 11,025
to 132,300 Hz on both clocks, through a recorder whose own response drops
out of the comparison, fit the hold within 0.27 dB rms and the hold times
1 + 0.062 z^-1 (over 1.062) within 0.05 dB, and within 0.016 dB below 44.1
kHz, where the recorder's own lines are out of the way. So each sample holds
about 6 % of the one before, whatever the rate. A one-pole term fits as
well. That adds 0.43 dB to the loss at 10 kHz, 0.8 dB at 15 kHz and 1.05 dB
at 20 kHz, for 1.2, 2.5 and 4.2 dB in all.

The data sheet doesn't say how the 4x mode interpolates. Measured on the
Armada at 44.1 kHz, it is no linear interpolator but a filter of some
length: it takes 1.74 dB off everything, stays within a dB of a plain DAC at
twice the rate up to about 17 kHz, and then falls away, 6 dB lower by 20.4
kHz. It takes about 25 dB off the images. So it keeps the images down, and
it costs the top of the band. Fed 88.2 kHz, it fails: the level falls by 15
dB and other tones appear. [Measuring the DAC on the
card](#measuring-the-dac-on-the-card) shows how to measure the difference on
your card.

When the SCF is on (bits 4 and 3 clear), ES1869.DRV sets 72h for a corner at
87.5% of half the sample rate, which is 9.7 kHz at 22,050 Hz and 4.9 kHz at
11,025 Hz. At 44.1 and 48 kHz it writes FDh, which puts the corner near 29
kHz, above the audio band. At those rates the SCF filters nothing audible
and only adds its own switching noise.

With neither the oversampling nor the SCF, the DAC plays like a
non-oversampling (NOS) DAC, and its images reach the output. A tone at *f*
also sounds at the sample rate minus *f*, and the lower *f* is, the quieter
its image. At 44.1 and 48 kHz the images lie above 22 kHz, where nobody
hears them. At low sample rates they fall in the audible band, above the
sound's own treble. At 22,050 Hz, for example, a 5 kHz tone also sounds at
17,050 Hz, 11 dB quieter, so Windows sounds recorded at 11 or 22 kHz get
this extra treble. The rebuilt drivers play every sample rate this way,
although the SCF alone (bits 4 and 3 clear) would remove those images.

To try each setting, change *Audio 2 4x oversampling* and *Audio 2 filter
bypass* on essctl's *DACs and ADC* page while music plays. You hear the
change at once. The driver puts its own setting back at the next wave open
and playback start, and when Windows starts or resumes.

The Audio 1 DAC has a switched-capacitor filter of its own, 71h bit 2, which
is also the ADC's filter when Audio 1 records. The rebuilt drivers keep it
bypassed at all times, for playback and recording
([AUDIO1.md](AUDIO1.md#the-filter-of-the-audio-1-codec)).

### The filters, measured

Audio 1 records Audio 2 through the record mixer, on one clock, so the two
can be compared tone by tone under one stream while a register moves. The
Audio 2 filter (72h) and the Audio 1 filter (A2h, which is also the ADC's)
read the same to 0.1 dB. Over the corner *fc*, the clock divided by 82, the
shape doesn't depend on the clock, down to 682 Hz at least:

| f / fc    | Level (dB) |
|-----------|------------|
| up to 0.3 | 0          |
| 0.64      | -1.1       |
| 0.89      | +1.3       |
| 1.05      | -7.6       |
| 1.48      | -35        |
| 1.82      | -58        |

So this is no gentle low-pass. Six poles and four zeros fit it within 0.02
dB: over *fc*, the poles lie at -0.432 +- 0.364j, -0.080 +- 0.938j and
-0.682 +- 0.783j, and a zero near +-1.86j. The pair at 0.94 has a Q of 5.9,
which is its resonance: a step rings there, falling by about a third a
cycle. The group delay is 0.71 cycles of *fc* in the passband and 2.3 at the
peak. ESS's FDh, corner 29 kHz, takes 2.4 dB out at 14.5 kHz, in the audible
band.

The clock is 7,144,200 Hz over 256 - x, which is the 793.8 kHz clock times
9, and not the data sheet's 7.16 MHz. Almost nothing stands in front of the
filter, so that it samples what comes in: a 30 kHz tone at 96 kHz past a
clock of 29767.5 Hz comes out at 240 Hz, 9 dB under the tone. That is a
mixer, and it gives the clock to the Hz. Since the clock is 162 times 44.1
kHz, the divisors 3, 6, 9, 18, 27, 54, 81 and 162 lie on whole multiples of
that rate, and of no rate of the 768 kHz clock. On one of those, the clock's
own aliases fall on DC. On others they fall in the band: a recording at 44.1
kHz through divisor 5 has a line at 8820 Hz at -92 dBFS, and with a tone
there are products of it up to -74 dBFS. FDh, ESS's value at 44.1 kHz, is on
one, and at 48 kHz it isn't.

The clock can be moved under a stream, by 72h directly and by A2h through
the DSP, which took 26 to 90 us a write with no error. A step takes effect
at once. The capacitors keep their charge and the poles move with the clock:
a sampling scope of a tone through steps of 72h matches that model to -31
and -34 dB, and what follows a step is the filter's own ringing, with no
slew of the register's. Turned faster than the audio, it acts as a filter
between its two corners, with sidebands around the tone: between FDh and FBh
under 15 kHz, they are 6 dB down below 7 kHz of swing, 15 dB at 50 kHz and
30 dB at 120 kHz. With nothing going in it makes nothing: on silence, a
swing at any speed stays under -95 dBFS. It adds noise, though: a recording
through FDh has about 2 dB more than one past the filter, and the noise
rises as the clock falls, 13 dB at divisor 162. A change of A1h under a
recording takes effect within a few samples, with no transient of the ADC's
own.

### What adds noise

Audio 1 recorded the chip's own output (record source AOUT, Capture
Volume 15) with nothing playing, once for each mixer setting, with
everything else at a quiet state. That state reads -64.6 dBFS over 20 Hz to
20 kHz, -66.6 dBA, and most of it comes from the record side: it doesn't
move with the master, from 0 to 63. So a setting that shows nothing here
adds less than that floor.

| Setting              | Adds (dB) | What it is             | Quietest  |
|----------------------|-----------|------------------------|-----------|
| Aux volume, 8 and 15 | 5, 14     | Comb on 5512 Hz        | 0         |
| Aux recording, 15    | 19        | The same comb          | 0         |
| Mic Boost (+26 dB)   | 13        | Hiss, hum at 120 Hz    | Off       |
| Mic recording, 15    | 11        | Hiss, 750 Hz           | 0         |
| FM recording, 15     | 8         | Hiss, 15625 Hz         | 0         |
| 3-D, level 32 and 63 | 3, 6      | Hiss, 15625 Hz up 7 dB | Off       |
| CD recording, 15     | 4         | Hiss                   | 0         |
| Mic volume, 15       | 3         | Hiss, 750 Hz           | 0         |
| FM volume, 15        | 2         | Hiss, none at 8        | 8 or less |
| Audio 2's filter     | 2         | Hiss, playing          | Off       |
| Line recording, 15   | 1         | 11025 and 4378 Hz      | 0         |

The master, both PCM volumes, Line, CD and Mono at any level, the beep,
Video, ES689 and the 4x stage add under 1 dB. Capture Volume scales
everything it gets, from -80 dBFS at 0 to -64.6 at 15, and the record
sources differ: CD reads -74, Line -69, the record mixer -68, Master -67.5,
Mic -65 and AOUT -64.6 dBFS.

A line at 15625 Hz, -84 dBFS here, runs through almost every take. The comb
at 5512 Hz is a fraction of the recording's 44.1 kHz. The 750 Hz line came
with the Zoom H2n on USB, whose own buzz has that frequency at 48 kHz, so it
reaches the chip's mic path through the machine's ground.

Record Monitor with AOUT as the record source closes a loop: the recording
reads -8 dBFS of low-frequency oscillation around 30 Hz with nothing
playing.

The noise can also read the filter. With no signal at all, the spectrum of a
second and a half of a recording's noise, over the same thing with the
filter off, finds the corner of A2h to within 1 to 5 % from about 1.6 to 11
kHz. Above that the filter's top lies past what the record side's noise
shows.

### Measuring the DAC on the card

1. Play a WAV file of white noise at 44.1 kHz, 16-bit stereo, in a loop. You
   can make one with Audacity's *Generate > Noise*.
2. In the Volume Control's *Recording* controls, select *Wave* only. Record
   10 s with Sound Recorder at 44.1 kHz, 16-bit stereo.
3. Switch essctl to the other setting and record again, keeping every level
   the same.
4. Compare the spectra of the two recordings, for example with Audacity's
   *Plot Spectrum*. The recording path shapes both recordings the same way,
   so the difference between them comes from the DAC.

## How much time each step has

Each item names a step, gives its buffer in parentheses, and then says how
much time the step has.

* **The DMA controller answers Audio 2's request for Windows playback** (a
  64-byte FIFO). It has 0.36 ms at 44.1 kHz 16-bit stereo.
* **The DMA controller answers Audio 1's request for Windows recording** (a
  256-byte FIFO). It has 1.45 ms at 44.1 kHz 16-bit stereo.
* **The DMA controller answers Audio 1's request for Sound Blaster sound**
  (a 64-byte FIFO). It has 2.9 ms at 22 kHz 8-bit mono.
* **The driver handles the interrupt** (one block). It has 62 ms.
* **ESS's DirectSound time-out copies the next 80 ms** (DirectSound's lead,
  on a 16-bit DMA channel). It has DirectSound's lead, some tens of ms. The
  extended driver copies as DirectSound writes, so this step is gone.
* **The program sends its next buffer** (the program's own queue). It
  usually has hundreds of ms.

A DMA request that is answered too late makes the sound skip. When the FIFO
runs dry, the DAC stops until the next sample comes, so you hear a click or
a short gap, and the sound goes on that much later with nothing lost, as the
Linux driver measured it. An interrupt that is handled more than 62 ms late
doesn't cause a skip. The DMA plays the old block again instead, and you
hear a repeat or a stutter.

The FIFO sizes are on DS p.10 and p.44. In Sound Blaster mode, which most
DOS games use, Audio 1's FIFO is 64 bytes and the chip's firmware runs it.

## What makes a DMA answer late

Without the DRQ latch, a DMA request can go low again before the DMA
controller answers it. The latch keeps the request up: "each of the four
audio DRQs is latched high until one of the following occurs: A DACK low
pulse ... A hardware reset ... 8-16 milliseconds elapse while DRQ is low"
(DS p.15). Card register 25h bit 7 switches the latch on. After a PnP reset,
the chip loads 25h from the 8-byte header of its PnP data, which is in an
internal ROM or an external EEPROM (DS p.24, p.30). ESS's example ROM data
has "DRQ latching off" (DS p.88).

Demand transfers are another cause. With 78h = 93h, the chip asks for 4
bytes at a time and holds DRQ for all of them. For chipsets that handle
demand transfers badly, ESS's driver offers single transfers instead, with
one byte per request. It then writes 78h = 13h for Audio 2 and leaves B9h as
the DSP reset sets it for Audio 1 ([DRIVER_CONFIG.md](DRIVER_CONFIG.md)).
Single transfers take more of the bus, though: with both channels running,
Audio 2 fell about 12 percent short with them
([ESS_EXTENDED.md](ESS_EXTENDED.md#both-channels-at-once)).

Other traffic on the bus delays the answer too, such as another DMA channel,
a bus master, or a PCI device holding the bus. On the laptop under Linux, a
PC Card sound card recording by programmed I/O through the CardBus bridge
stopped Audio 2's DAC twice in 40 seconds, for up to 314 us, and the IDE
channel's PCI latency timer at 10h, where it is 40h, stopped it 202 times.
Some BIOSes have settings for how the chipset shares the bus with ISA DMA,
such as *Passive Release* and *PCI Delayed Transaction*.

## Finding out on the card

Change one thing at a time, and put it back if the skips get worse. A
restart undoes a change to the DRQ latch, and single mode DMA goes off when
you clear its check box and restart.

1. In essctl, look at *DRQ latch* on the *Plug and Play* page and at *PnP
   data source* (internal ROM or EEPROM) on the *Status & interrupts* page.
2. If the DRQ latch is off, turn on *Expert mode* and switch the latch on.
   * If it stays on when essctl reads it back, listen for whether the skips
     go away. The write lasts until the computer restarts.
   * If essctl says the chip returns 0, the chip takes the latch setting
     only from its ROM or EEPROM.
3. Switch on "Use single mode DMA" under Control Panel > Multimedia >
   *Advanced* > the ES1869 audio device > *Properties* > *Settings*, then
   restart Windows and listen.
4. In Device Manager, note the ES1869's DMA channels and what else uses DMA.
5. Note when the skips happen, whether in Windows sounds (Audio 2), in DOS
   games (Sound Blaster, Audio 1) or in both, and what else is busy at the
   time (disk, CD-ROM, network).
6. If the BIOS has *Passive Release* or *PCI Delayed Transaction*, try
   switching each of them, one at a time.

## What software can do about it

If the DRQ latch takes a write, `DrqLatch=1` under `[ES1869.VXD]` in
`SYSTEM.INI` makes the extended `ES1869.VXD` set it each time a VM takes the
DSP, so it is on for every Windows sound, every DirectSound game and every
DOS program, and again after anything reset the chip
([DRIVER_CONFIG.md](DRIVER_CONFIG.md#61-es1869vxd)). The key is off unless
you set it, because the data sheet has the register as read-only and ESS's
own ROM data leaves the latch off: check with essctl first that the chip
takes the write, and listen for whether the skips go away. If the chip
doesn't take it, the card's EEPROM could be changed instead, by a tool that
reads it, saves a copy and rewrites the one header byte. That is possible
only with an EEPROM, not with the internal ROM, and a bad write can make the
card vanish from Plug and Play until the EEPROM is written again.

The driver's buffering is generous, at 62 ms per block, so changing
ES1869.DRV wouldn't help a FIFO that runs dry. In this path,
`build/ES1869.DRV` changes only the Audio 2 mode, and its Audio 1 player
([AUDIO1.md](AUDIO1.md)) plays through the other DAC. The rebuilt ES1869.VXD
keeps ESS's audio code unchanged and at its addresses, so it plays the same
way as ESS's driver. `EssCodeTest` in `tests/test_vxdext.py` checks the
first and `PcmPathTest` the second. The one difference that DirectX can see
is the certification mark
([VXD_INTERNALS.md](VXD_INTERNALS.md#installing-the-extended-driver)).
