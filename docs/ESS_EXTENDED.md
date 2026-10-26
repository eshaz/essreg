# The ES1869's Extended mode

This page collects what is known about the ES1869's Extended mode, the
chip's own way of playing and recording on its two audio channels, and about
the DMA controllers and memory managers in front of it. The Sound Blaster 16
fronts, `esssb16` and the extended `ES1869.VXD`, turn a game's commands into
these operations ([SB16.md](SB16.md#the-fronts-on-the-es1869)), and essprobe
measures what the sources leave open ([ESSPROBE.md](ESSPROBE.md)).

Each fact names its source. Where only the card can tell, the fact also
names the essprobe cases that measure it, such as `a2.ch5`, and once a
report of the probe has come back, what the card did follows as "Measured".
Nothing on this page is copied from the sources' code.

## Sources

* **The data sheet**, SAM0023-122898, in `docs/datasheet/`, cited as DS
  p.NN.
* **ESS's drivers** in `driver/`, rebuilt from `src/es1869` and `src/vxd`.
  An address such as 6:28AC is a segment and offset in `ES1869.DRV`, as in
  [DRIVER_CONFIG.md](DRIVER_CONFIG.md), and one such as o5:3594 an object
  and offset in `ES1869.VXD`, as in [VXD_INTERNALS.md](VXD_INTERNALS.md).
* **Linux's ALSA driver**, `sound/isa/es18xx.c`, which programs the chip in
  Extended mode, as [AUDIO1.md](AUDIO1.md#programming-the-audio-1-dac)
  compares it.
* **The first report** of `sb16test` from the card, which measured the rates
  of A1h ([SB16.md](SB16.md#rates)).

## Ports

The audio device takes 16 ports from Audio_Base, the `A` of `BLASTER` (DS
p.38, p.40-41):

| Port | Use                                                |
|------|----------------------------------------------------|
| +0-3 | FM synthesizer                                     |
| +4   | mixer index, which reads back                      |
| +5   | mixer data                                         |
| +6   | reset (write), reset and activity flags (read)     |
| +7   | power management, with the FM reset in bit 5       |
| +8-9 | FM synthesizer, 11 voices                          |
| +Ah  | the controller's answer                            |
| +Ch  | command or data (write), status (read)             |
| +Eh  | read status, whose read clears Audio 1's interrupt |
| +Fh  | the FIFO, for programmed I/O in Extended mode      |

The data sheet lists no +Bh and no +Dh. essprobe's window under EMM386 traps
+Dh for that reason.

Writing Audio_Base+6 bit 0 holds the chip in reset, and bit 1 holds the FIFO
in reset; bits 7:2 are written as 0, and bit 1 does nothing in Compatibility
mode (DS p.40). Reading it gives three activity flags in bits 7:5, which a
read sets and later port and DMA accesses clear, the digital section's
power-down in bit 3, the MIDI mode in bit 2, and the two reset bits.

Reading Audio_Base+Ch gives the status (DS p.41):
* bit 7, busy: the write buffer is full or the chip is busy;
* bit 6, a byte waits at +Ah, the same flag as +Eh bit 7;
* bits 5, 4 and 3: Extended mode's FIFO full (256 bytes), empty, and half
  empty, which means fewer than 128 bytes during playback and 128 or more
  during recording (DS p.51);
* bit 2, the firmware's interrupt, such as the end of a Compatibility mode
  transfer;
* bit 1, the interrupt of the FIFO's half-empty flag, for programmed I/O;
* bit 0, the interrupt of the transfer counter's overflow in Extended mode.

Any read of Audio_Base+Eh clears whatever interrupt Audio 1 has pending (DS
p.17, p.41).

The configuration device has 8 ports from Config_Base, which the third and
fourth reads of mixer 40h give (DS p.38-39, p.43). +0 and +1 are the PnP
index and data, +5 the PnP state, +6 the interrupt status and +7 the
interrupt mask. In +6 and +7, bit 0 is Audio 1, bit 1 Audio 2, bit 2 the
hardware volume, bit 3 the MPU-401, and bits 4 to 6 the CD-ROM, modem and
GPI inputs. The status shows Audio 2's request ANDed with 7Ah bit 6, the
hardware volume's with 64h bit 1 and the MPU-401's with 64h bit 6, but
before the mask, which a hardware reset sets to all ones (DS p.17-18).

## Identification

Four successive reads of mixer 40h return 18h, 69h and then Config_Base's
high and low bytes (DS p.43). DSP command E1h returns 3 and 1, the Sound
Blaster Pro's version (DS p.74).

## DMA channels and interrupts

The chip has four DRQ and DACK pin pairs. Card registers 23h (pins B and A)
and 24h (pins D and C) give each pin's ISA channel, a nibble each, and the
chip loads them from its PnP header; an unused pin is given channel 2 (DS
p.30). The audio device's PnP registers 74h and 75h select Audio 1's and
Audio 2's channels, and a pin drives its DRQ only while a device's DMA
register names the pin's channel and the device is active, PnP 30h bit 0 (DS
p.13, p.31-32). essprobe's `env.card` reads all of them.

The data sheet maps Audio 2 to the 8-bit channels 0, 1 and 3 only, and wants
at least two pins on 8-bit channels, with the 16-bit channels 5, 6 and 7 for
the external DMA source (DS p.13). The chip's data bus to the FIFO is 8 bits
wide (DS p.69, B5h), so it takes one byte per DMA cycle on any channel. On a
16-bit channel the 8237 moves a word per cycle and the chip gets the word's
low byte. ESS's drivers nevertheless play Audio 2 on channel 5, with each
byte of the sound in the low byte of a word of a buffer twice as long:
ES1869.DRV copies the sound so at 1:1B82, and ES1869.VXD at `L5_3309` for
DirectSound (VXD_INTERNALS.md). On such a channel the 8237 counts words,
which are bytes to the chip. Mixer 78h bit 5 is reserved (DS p.65). Probe:
`a2.ch5` (the spread buffer), `a2.packed` (a game's 16-bit buffer as it is),
`a2.wide` and `a2.wide_packed` (78h bit 5 set).

Card register 25h bit 7 turns on the DRQ latch, which holds each audio DRQ
high until a DACK pulse comes while the chip's own request is low, a
hardware reset, or 8 to 16 ms pass with the request low (DS p.15, p.30).
Card register 26h bits 4:2 mask the external, Audio 2 and Audio 1 DMA
requests, for channels that two devices share (DS p.15, p.31).

Card registers 20h to 22h give the interrupt pins' IRQs, and the audio
device's PnP registers 70h and 72h select Audio 1's and Audio 2's interrupts
(DS p.17, p.31). Controller registers B1h and B2h read back Audio 1's
interrupt and channel in their low nibbles (DS p.69):

| Low nibble | B1h, the IRQ | B2h, the channel |
|------------|--------------|------------------|
| 0000       | 2, 9, others | others           |
| 0101       | 5            | 0                |
| 1010       | 7            | 1                |
| 1111       | 10           | 3                |

## Modes and resets

After any reset the chip is in Compatibility mode, a Sound Blaster Pro in
which the firmware stands between the bus and the CODEC, with a FIFO of 64
bytes (DS p.43). Extended mode adds the controller registers, and DSP
command C6h enables them; it is needed after every reset, and C7h disables
them again (DS p.47, p.73). In Extended mode the FIFO holds 256 bytes and
dedicated logic runs the DMA, while the firmware is mostly idle. The time
base of the sample rate is 800 or 400 kHz in Extended mode, against 1 or 1.5
MHz in Compatibility mode (DS p.44, Table 13).

The data sheet advises against mixing the two modes: of the Compatibility
mode commands only D1h and D3h, the Audio 1 DAC's way into the mixer, are
safe with Extended mode transfers, and Extended mode commands may set up the
DMA and interrupt channels before Compatibility mode is used (DS p.44).
Compressed sound, ADPCM and ESPCM, plays in Compatibility mode only (DS
p.44). A stereo transfer in Compatibility mode takes mixer 0Eh bit 1 and
twice the rate, and its 8-bit stream starts with the right channel (DS
p.44). In Extended mode, and on Audio 2, a stereo stream starts with the
left channel, and a 16-bit sample with its low byte (DS p.45).

A controller register is written with its number as a DSP command, Axh or
Bxh, followed by the value. C0h followed by the register's number reads it
back: the value comes at +Ah once +Eh bit 7 is set (DS p.47-48).

### The software reset

The data sheet's software reset writes 1 to Audio_Base+6, waits a moment,
such as a read of the port, writes 0, and then reads +Ah whenever +Eh bit 7
is set until AAh comes, for at least a millisecond (DS p.43). In Extended
mode the reset writes 3, so that bit 1 empties the FIFO too (DS p.48, p.49,
p.51). Both resets, hardware and software (DS p.43):
* end Extended mode;
* set the timer and the filter for 8 kHz;
* stop any transfer and clear any interrupt request;
* turn the Audio 1 DAC's way into the mixer off, as D3h does;
* set both modes' transfer counters to 2048 bytes;
* set the CODEC to the DAC direction, at the middle level;
* set the 8-bit recording's AGC gain to the maximum and the 16-bit
  recording's gain to the middle.

A hardware reset also resets the mixer. A software reset leaves the mixer
alone, and writing any value to mixer 00h resets it (DS p.53), apart from
the record levels 68h to 6Fh, which only a hardware reset clears (DS
p.63-64). The register pages add (DS p.64-70):
* 7Ah, Audio 2's format and interrupt, is cleared by both resets;
* 70h, 71h, 72h and 7Ch are zeroed by a hardware reset, and the steps for
  Audio 2 say that a reset zeroes 7Ch (DS p.51);
* A4h and A5h, Audio 1's count, are 00h and F8h after a reset, 2048 bytes;
* BAh and BBh, the ADC's offsets, keep their values through a software
  reset.

ES1869.DRV resets the chip with 3 and then 0 and sends C6h (1:11A0), which
clears the controller registers (DRIVER_CONFIG.md). ES1869.VXD writes 1,
reads the port three times and writes 0 (`DSP_Reset`, o5:195C). Probe:
`rst.ext` (3), `rst.compat` (1), `rst.fifo` (2) and `rst.mixer` (mixer 00h)
set known values in 70h to 7Ch, the levels and A1h to B9h, then reset, and
report what changed.

## Audio 1

Audio 1 drives the CODEC, an ADC and a DAC with one sample clock, A1h, and
one filter, A2h. The CODEC is a stereo DAC, a stereo ADC or a mono CODEC, so
it can't play and record in stereo at once (DS p.15). [AUDIO1.md](AUDIO1.md)
describes what ESS's drivers do with it and how `build/ES1869.DRV` plays on
it.

### The data sheet's steps for playback

The data sheet plays on Audio 1 in these steps (DS p.48-49):
1. Reset with Audio_Base+6 = 3, which also turns the DAC's way into the
   mixer off, so that the setup makes no pop.
2. Send C6h.
3. Write B8h with bit 3 clear for the DAC and bit 2 set for auto-initialize,
   A8h's bits 1:0 for mono or stereo, keeping its other bits, and B9h for
   the transfer type.
4. Write A1h and A2h for the rate and the filter, and A4h and A5h with minus
   the block's length in bytes.
5. Write B6h and then B7h twice, as Table 16 gives them for the format.
6. Set bits 6 and 4 of B1h and B2h, and clear bits 7 and 5.
7. Program the interrupt controller and the DMA controller.
8. Set B8h bit 0, keeping its other bits, to start.
9. After about 100 ms, send D1h to let the DAC into the mixer.
10. In auto-initialize mode, read Audio_Base+Eh at each interrupt and send
    nothing else. In normal mode, program the next block in the 8237, and in
    A4h and A5h if its length changes, then clear B8h bit 0 and set it
    again.
11. To stop at once, clear B8h bit 0. To stop at the end of the block that
    plays, clear B8h bit 2, wait for the interrupt, then clear bit 0.
12. Afterwards, wait until +Ch says that the FIFO is empty, wait 25 ms for
    the filter to settle, send D3h, and reset the chip.

After a reset the Audio 1 DAC is kept out of the mixer, so that the setup
makes no pop. D1h lets it in, D3h keeps it out, and D8h answers 00h or FFh
for the two (DS p.15). To play without a reset, the data sheet sends D3h,
sets the DAC to its middle with 10h 80h, waits 25 ms and sends D1h, and it
ends each transfer with 10h 80h, so that the next one starts from the middle
level (DS p.15-16).

ESS's drivers never play on Audio 1 (AUDIO1.md). Linux's driver starts Audio
1 in this order too, and differs in the details that AUDIO1.md compares with
`build/ES1869.DRV` ([Programming the Audio 1
DAC](AUDIO1.md#programming-the-audio-1-dac)): a FIFO reset (+6 = 2, then 0)
in place of the full reset, B9h = 02h, B6h and B7h for signed samples the
other way round from Table 16, B1h and B2h written as 50h, and D1h sent
once, at initialization.

### The registers

* **A1h, the rate** (DS p.67, p.64). With bit 7 clear, the rate is the low
  clock over 128 minus the value; with bit 7 set, twice that clock over 256
  minus the value, for rates above 22050 Hz. The data sheet gives the clocks
  as 397.7 and 795.5 kHz, but the first report measured 396.9 and 793.8 kHz,
  at which 22050 and 44100 Hz come out exactly. With mixer 71h bit 5 set,
  A1h works as 70h does instead. ES1869.DRV's rate routine (6:1FF4) uses the
  high clock above 22050 Hz, and its recording sets 71h bit 5 for 48000 Hz
  only (6:22D3). Probe: `a1.r8000` to `a1.r48000`, `a1.lo44100` and
  `a1.hi22050` (the other clock), `a1.new48000` (71h bit 5).
* **A2h, the filter** (DS p.68, p.72). The filter's clock is 7.16 MHz over
  256 minus the value, and its roll-off is the clock over 82, best at 80 to
  90 percent of half the rate. For the high clock's divisors 18 to 35,
  ES1869.DRV takes the value from one of two tables, which its flag for the
  direction picks, one for playback (DGROUP 4Eh) and one for recording
  (60h), and otherwise computes it (6:1FF4, 6:2308).
* **A4h and A5h, the count** (DS p.68). They reload a 16-bit counter, which
  counts up once for each byte that the DMA moves, so a block is at most
  65536 bytes. At its overflow the counter reloads; in auto-initialize mode
  the DMA goes on, and in normal mode the requests stop until the next
  start. Either way the overflow interrupts if B1h bit 6 is set. Probe:
  `a1.once` (one block, normal mode).
* **A8h, analog control** (DS p.68). Bits 1:0 are 01 for stereo and 10 for
  mono, and bit 3 turns the record monitor on. The data sheet wants bit 4
  written as 1 and bits 7:5 and 2 as 0, but ES1869.DRV writes F5h or F6h
  (6:291D).
* **B1h, the interrupt** (DS p.68). Bit 6 enables the counter's interrupt
  and bit 5 the FIFO half-empty interrupt of programmed I/O; bit 4 does
  nothing, and bit 7 belongs to Compatibility mode. ES1869.DRV reads it and
  ORs 50h in (6:2934). Probe: `a1.irq_off` (bit 6 clear).
* **B2h, the DMA request** (DS p.69). Bit 6 enables the DRQ for Extended
  mode DMA, and with it clear the FIFO takes programmed I/O through +Fh. Bit
  5 belongs to Compatibility mode, and bit 4 does nothing. ES1869.DRV ORs
  50h in (6:296E). The VxD's front pauses a transfer by clearing bit 6
  (VXD_INTERNALS.md). Probe: `a1.pause_b2`.
* **B4h**, the record level, 4 bits for each side (DS p.69).
* **B5h and B6h**, the DAC's holding register, which B7h bit 0 copies to the
  DAC at the next sample (DS p.69-70). Table 16 writes B6h first, which also
  prepares the CODEC for the DAC direction (DS p.48).
* **B7h, the FIFO's format** (DS p.69-70). Bit 7 connects the FIFO to the
  CODEC, bit 6 is written as the opposite of bit 3, bit 5 means signed
  samples, bit 4 is written as 1, bit 3 means stereo and bit 2 16-bit
  samples. Table 16 writes 51h or 71h first and then the format: D0h, F0h,
  D4h or F4h for mono, 98h, B8h, 9Ch or BCh for stereo. Its signed and
  unsigned columns are the wrong way round: unsigned samples take B6h = 80h
  and B7h = 51h, signed ones B6h = 00h and B7h = 71h (AUDIO1.md). Probe:
  `a1.f8m`, `a1.f8s`, `a1.f16m`, `a1.s8` and `a1.u16` (the formats),
  `a1.prime` (bit 7 set 20 ms after the start).
* **B8h, the control** (DS p.70). Bit 3 selects the ADC, bit 2
  auto-initialize and bit 1 a transfer into memory; bit 0 is the DMA's
  active-low reset, which lets the transfer run while it is set. Probe:
  `a1.pause_b8`, whether a transfer that bit 0 stopped goes on where it
  stopped.
* **B9h, the transfer type**. The steps say 00 for single transfers and 01
  or 11 for demand transfers of 2 or 4 bytes (DS p.48), but the register's
  page says 01 for single transfers, 10 and 11 for demand transfers of 2 and
  4 bytes, and 00 is reserved (DS p.70). ES1869.DRV writes 02h, unless
  "Single Mode DMA" is set (6:28BF). Probe: the base case (02h), `a1.b9_0`,
  `a1.b9_1` and `a1.b9_3`.

### The FIFO and the interrupt

The FIFO holds 256 bytes, and +Ch bits 5 to 3 give its state (DS p.41,
p.43). While it is full the chip drops its DMA request (DS p.45). So the
counter, which counts what the DMA brings, reaches the end of a block when
the block's last byte enters the FIFO, up to 256 bytes before the DAC has
played it, and the interrupt comes that early. Audio_Base+6 bit 1 empties
the FIFO (DS p.40). Probe: `a1.keep` (no FIFO reset before the start).

In auto-initialize mode the chip plays on whether or not its interrupt was
acknowledged (SB16.md). The interrupt sets +Ch bit 0 and Config_Base+6 bit
0, and a read of +Eh clears it (DS p.17, p.41). Probe: `a1.noack` (never
acknowledged).

The data sheet has no pause in Extended mode: its D0h and D4h pause and
continue Compatibility mode's DMA (DS p.73). A front can pause through B2h
bit 6, which keeps the chip's state, or through B8h bit 0, the DMA's reset.

### Commands while the DMA runs

The busy flag, +Ch bit 7, opens a window for a command only while the FIFO
is full during playback, and a command has to follow the poll within 13 us
or the chip takes it for DMA data (DS p.45). The data sheet therefore sends
only Dxh commands during a transfer, such as D1h, D3h, D0h and D4h, with
interrupts off from the poll to the write (DS p.48). That is the rule of
Compatibility mode, where the DMA's bytes come through the same buffer. In
Extended mode, ES1869.DRV reads A8h with C0h about 100 ms after it started a
recording (6:2A3D), and the VxD's front writes B2h while Audio 1 plays.

### Programmed I/O

With B2h bit 6 clear, the program moves the samples itself, with `rep outsb`
to Audio_Base+Fh, and the FIFO's half-empty flag paces it (DS p.51). During
playback the flag is set while the FIFO holds fewer than 128 bytes, and with
B1h bit 5 set and bit 6 clear, the flag's rising edge interrupts, so that
each interrupt can take 128 bytes. The FIFO needs more than 128 bytes before
the first interrupt can come, and since the DAC takes bytes out while the
program fills it, the data sheet polls the flag to see it go low, or sends a
second 128 bytes. The steps are those of DMA, with B9h left alone and B2h
bits 7:5 clear, and the transfer starts with B8h bit 0 and the first
`rep outsb`. The data sheet rates block I/O for an ISA bus of up to 8.33
MHz.

### Recording

Recording follows the same steps, with these differences (DS p.49-51): B8h
bit 3 is set first, A8h bit 3 turns the record monitor on after 100 ms, B4h
sets the level, Table 17 writes only B7h, and mixer 1Ch and 7Ah bits 4:3
select the source. ES1869.DRV records in this order (6:28A4-6:2A09): a FIFO
reset, B9h, A4h and A5h, A8h, B1h, B2h, then B7h once with 90h or 98h plus
24h for 16 bits, and B8h with its bits 3:0 all set, which turns the CODEC to
the ADC and starts the DMA in one write. With mixer 7Fh bit 4 set, Audio 1
records the music DAC's samples at 49716 Hz, whatever its own rate (DS p.65,
AUDIO1.md).

## Audio 2

Audio 2 is a second stereo DAC with a FIFO of 32 words, which plays only by
DMA (DS p.13, p.52, p.65). The mixer registers 70h to 7Ch program it (DS
p.64-65):
* **70h, the rate.** Bit 7 selects a clock of 768 kHz, for 48000 Hz and its
  divisors, or 793.8 kHz, for 44100 Hz and its divisors, and the rate is
  that clock over 128 minus bits 6:0.
* **71h, the mode.** Bit 5 makes A1h work as 70h does, bit 4 turns on 4x
  oversampling, bit 3 bypasses Audio 2's filter and bit 2 Audio 1's, and bit
  0 slaves Audio 2's DMA to the FM synthesizer, which mixes it in digitally.
  Bit 1 lets Audio 2 run at its own rate; with it clear, Audio 2 runs at
  Audio 1's rate and filter.
* **72h, the filter**, as A2h for Audio 1, used only when Audio 2 runs at
  its own rate.
* **74h and 76h, the count**, minus the block's length in bytes.
* **78h, the control.** Bits 7:6 give the transfer type: 00 single, 01, 10
  and 11 demand transfers of 2, 4 and 8 bytes. Bit 4 means auto-initialize,
  bit 1 lets the DMA fill the FIFO, and bit 0 connects the FIFO to the DAC.
  Bit 5 is reserved. At the counter's overflow in normal mode, the chip
  clears bit 1 and stops.
* **7Ah, the format and the interrupt.** Bit 7 is the interrupt's latch,
  which the overflow sets and a write of 0 clears, and bit 6 lets the latch
  interrupt. Bit 2 means signed samples, bit 1 stereo and bit 0 16-bit
  samples.
* **7Ch, the volume**, 4 bits for each side.

The data sheet's steps for Audio 2 reset the chip with 3, write 78h's
transfer type and bit 4, then 70h, 72h, 74h and 76h, 7Ah's format and bit 6,
program the controllers, set 78h bits 1:0 to start, and turn the volume up
after about 100 ms (DS p.51-52). A normal-mode block starts by clearing 78h
bits 1:0 and setting them again. To stop at the end of the block, the steps
clear 78h bit 4, wait for the interrupt, then clear bits 1:0.

Three of those steps don't fit the register pages. To stop at once, they
clear B8h bit 0, which is Audio 1's. At each interrupt they read
Audio_Base+Eh, which clears Audio 1's interrupt, while Audio 2's latch is
7Ah bit 7 (DS p.17). And the interrupt sources say that Audio 2 "must share
the same sample rate as the first DMA channel" (DS p.17), as the full-duplex
steps do (DS p.52), which 70h and 71h bit 1 contradict.

### What ESS's drivers do

ES1869.DRV starts Audio 2 for a wave output in this order
(DRIVER_CONFIG.md):
1. It programs the 8237 and unmasks the channel (6:2C6E-6:2D4F), in demand
   mode, 18h plus the channel, or in single mode, 58h, with "Single Mode
   DMA".
2. It writes 7Ch, the volume (6:2D6D), and sends D3h (6:2D78).
3. It writes 74h and 76h with minus the block's length (6:2D98, 6:2DA7).
4. It writes 7Ah = 40h, plus 05h for 16-bit signed samples and 02h for
   stereo (6:2DD7).
5. It sets 71h bits 4 and 1, for 4x oversampling and Audio 2's own rate
   (6:2DEE).
6. It writes 78h = 93h, auto-initialized demand transfers of 4 bytes, or
   13h, single transfers, with "Single Mode DMA" (6:2E0C).

70h and 72h come from the rate routine at 6:2436, and at a stop the driver
clears 78h bit 4, writes 00h to 78h, clears 7Ah bit 7 and writes 00h to 7Ch
(1:16FE-1:1744). The interrupt handler clears 7Ah bit 7 at each interrupt
(3:0300).

ES1869.VXD plays DirectSound's buffer on Audio 2 in the same order, with the
8237 in the same modes (o5:32F2), but without 7Ch and with 7Ah bit 6 clear,
so its Audio 2 never interrupts: DirectSound reads the position instead
(o5:3594). It writes 70h with whichever of the two clocks comes nearer to
the rate, or 80h, 6000 Hz, for any rate up to 6000 Hz, and 72h with 256
minus 199582 over the rate, rounded down, or FDh at 44100 and 48000 Hz
(o5:34F6). When the DSP changes hands it sets 71h bits 4 and 1 and clears
74h, 76h, 78h and 7Ah (VXD_INTERNALS.md).

Probe: `a2.f8m` to `a2.u16` (the formats), `a2.r8000` to `a2.r48000`,
`a2.t2_0`, `a2.t2_1` and `a2.t2_3` (78h bits 7:6, the base case being 10),
`a2.dmd` (the 8237 in demand mode as well, ESS's own setup), `a2.slaved`
(71h bit 1 clear), `a2.x4`, `a2.fb` (the filter), `a2.once`, `a2.pause_f`
and `a2.pause_d` (78h bits 0 and 1), `a2.irq_off`, `a2.noack`, and `a2.ch0`
to `a2.ch3` (Audio 2 on each 8-bit channel that a pin is wired to).

## Both channels at once

The data sheet's full duplex records on Audio 1 and plays on Audio 2 (DS
p.52). With the same count in A4h and A5h and in 74h and 76h, one interrupt
serves both, and the steps start the playback first, so that its FIFO fills
before the recording's first interrupt. The data sheet describes no playback
on both DACs.

ESS's drivers play only on Audio 2. `build/ES1869.DRV`'s dual playback plays
on both, with Audio 2 slaved to Audio 1's clock through 71h bit 1, 4x
oversampling off, and a start that fills Audio 2's FIFO first with 78h bit 0
clear, then sets B8h bit 0 and 78h bit 0 with interrupts off
([AUDIO1.md](AUDIO1.md#dual-playback)). The fronts play an 8-bit and a
16-bit transfer at once on the two DACs, each at its own rate, where Audio 2
has an 8-bit channel of its own. Probe: `both.base` (each on its own clock),
`both.slaved`, `both.f8_16` and `both.f16_8` (mixed formats), `both.r22_44`
(two rates), `both.t2_0` and `both.single` (single transfers), `both.dmd`
(both channels in the 8237's demand mode), `both.muted2` and `both.pace`
(Audio 2 muted, as `esssb16`'s withdrawn pace ran it).

## The DMA controllers and the memory managers

The chip asks for a byte at a time, or 2, 4 or 8 in demand transfers, and
its counters count bytes; the 8237's count gives the position. C0h reads
back A4h and A5h, which hold the reload value, so the chip offers no
position of its own. ESS's drivers program the 8237 in auto-initialize mode,
around a ring of two blocks, and in demand mode by default
(AUDIO_PIPELINE.md).

A program in V86 mode reaches the 8237 through its memory manager:
* **Windows' VDMAD** virtualizes every channel. ESS's VxD programs the
  chip's channels from VDMAD's state, and the VxD's front answers a DOS
  program's reads of its 16-bit channel
  ([VXD_INTERNALS.md](VXD_INTERNALS.md#a-sound-blaster-16-for-dos-programs)).
* **EMM386** traps the DMA controllers' ports for itself, and its int 2Fh
  function 4A15h lets another program trap only ports 0100h to FFFFh, so a
  TSR can't see a game's DMA programming ([ESSSB16.md](ESSSB16.md)). By the
  research, EMM386 keeps what a program writes of a channel and programs the
  8237 with it when the program unmasks the channel, copying a buffer that
  crosses a 64 KB page through one of its own, and the program's reads come
  from the 8237 itself. `tests/emm386dma.py` models all of it but the copy.
* **HDPMI32i and QPI** let a TSR trap the DMA ports, so the front sees the
  programming as it happens ([ESSSB16.md](ESSSB16.md)).

Probe: `h1.reach1` and `h1.reach5` (when the 8237 gets a masked channel's
programming), `h1.read1` and `h1.read5` (what a program reads of a moving
channel), `h1.ff1` and `h1.ff5` (whose byte flip-flop the program's reads
and writes move), `h1.cross1` and `h1.cross5` (a buffer across a page),
`h1.stale` and `h1.fresh` (a channel carried over before its unmask, as
Pinball Fantasies' driver has it, and the same after the unmask).

## What the engines do to the machine

A DMA cycle takes the ISA bus, and a channel programmed the wrong way round
writes into memory while the chip plays. Under an earlier `esssb16` build,
which ran Audio 2 muted on the game's 16-bit channel, Pinball Fantasies'
table turned black and white. SB16.md gives one possible cause, the game
driver's message in segment B000h, and these cases look for the other kind.
Probe: `h4.idle` to `h4.both` (a busy loop's speed, the VGA's retrace and
DAC entries, and guard bytes around the buffers, with each engine and the
pace's settings), `h4.wrong1` and `h4.wrong2` (a channel that writes into a
scratch area).

## Where the sources disagree

The data sheet contradicts itself, or ESS's drivers, in these places:
* Table 16's signed and unsigned columns (DS p.49) are the wrong way round
  (AUDIO1.md).
* B9h's encoding differs between the steps (DS p.48) and the register's page
  (DS p.70).
* A1h's clocks are 397.7 and 795.5 kHz on the register's page (DS p.67) and
  measured 396.9 and 793.8 kHz.
* Audio 2 maps to channels 0, 1 and 3 (DS p.13), and ESS's drivers play it
  on channel 5.
* Audio 2's steps stop it through B8h and clear its interrupt through
  Audio_Base+Eh (DS p.52), both Audio 1's.
* Audio 2 shares Audio 1's rate (DS p.17, p.52), while 70h and 71h bit 1
  give it its own (DS p.64).
* A8h's reserved bits are written as 1 by ES1869.DRV (DRIVER_CONFIG.md).
* The FIFO holds 32 levels of 16-bit samples or 64 of 8-bit ones (DS p.13),
  which is Compatibility mode's, and 256 bytes in Extended mode (DS p.43).
* 7Ch is zeroed "on reset" in the steps (DS p.51), and by a hardware reset
  on the register's page (DS p.65). Probe: `rst.ext` and `rst.compat`.
* The steps turn Audio 2's volume up 100 ms after the start (DS p.52), and
  ES1869.DRV writes it before the start (6:2D6D).
* ES1869.VXD sends C3h, which returns a status byte, and C2h 01h, neither in
  the data sheet, before it restores a DOS box's mixer (VXD_INTERNALS.md).
