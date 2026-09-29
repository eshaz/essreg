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

The Audio 2 DAC sounds better without its 4x oversampling and with its
filter bypassed, because the oversampling most likely dulls the treble.
ESS's drivers turn the oversampling on, but `build/ES1869.DRV` and the
extended `ES1869.VXD` play without it, for the reasons given in [The Audio 2
DAC](#the-audio-2-dac-oversampling-and-the-filter).

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
known amount. At 44.1 kHz the loss is 0.75 dB at 10 kHz, 1.7 dB at 15 kHz
and 3.2 dB at 20 kHz.

The data sheet doesn't say how the 4x mode interpolates. An interpolator
that smooths between samples loses more treble than the plain DAC does,
unless it is a long filter. A linear interpolator loses twice as much in dB:
1.5 dB at 10 kHz, 3.4 dB at 15 kHz and 6.3 dB at 20 kHz. A cheap
interpolator can also add noise or distortion of its own. Without the
oversampling, the top octave therefore most likely keeps more of its level,
and nothing is added to the sound. [Measuring the DAC on the
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
bypass* on essctl's *Audio 2 channel* page while music plays. You hear the
change at once. The driver puts its own setting back at the next wave open
and at the next playback start.

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
* **The program sends its next buffer** (the program's own queue). It
  usually has hundreds of ms.

A DMA request that is answered too late makes the sound skip. The FIFO runs
dry while the DAC keeps its pace, so the DAC has nothing to play, and you
hear a click or a short dropout. An interrupt that is handled more than 62
ms late doesn't cause a skip. The DMA plays the old block again instead, and
you hear a repeat or a stutter.

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

Other traffic on the bus delays the answer too, such as another DMA channel,
a bus master, or a PCI device holding the bus. Some BIOSes have settings for
how the chipset shares the bus with ISA DMA, such as *Passive Release* and
*PCI Delayed Transaction*.

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

If the DRQ latch takes a write, ES1869.VXD's extension could set it each
time the device starts. If it doesn't, the card's EEPROM could be changed
instead, by a tool that reads it, saves a copy and rewrites the one header
byte. That is possible only with an EEPROM, not with the internal ROM, and a
bad write can make the card vanish from Plug and Play until the EEPROM is
written again.

The driver's buffering is generous, at 62 ms per block, so changing
ES1869.DRV wouldn't help a FIFO that runs dry. In this path,
`build/ES1869.DRV` changes only the Audio 2 mode, and its Audio 1 player
([AUDIO1.md](AUDIO1.md)) plays through the other DAC. The rebuilt ES1869.VXD
keeps ESS's audio code unchanged and at its addresses, so it plays the same
way as ESS's driver. `EssCodeTest` in `tests/test_vxdext.py` checks the
first and `PcmPathTest` the second. The one difference that DirectX can see
is the certification mark
([VXD_INTERNALS.md](VXD_INTERNALS.md#installing-the-extended-driver)).
