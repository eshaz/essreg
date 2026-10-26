# The ES1869's Extended mode

This page collects what is known about the ES1869's Extended mode, the
chip's own way of playing and recording on its two audio channels, and about
the DMA controllers and memory managers in front of it. The Sound Blaster 16
fronts, `esssb16` and the extended `ES1869.VXD`, turn a game's commands into
these operations ([SB16.md](SB16.md#the-fronts-on-the-es1869)), and essprobe
measures what the sources leave open ([ESSPROBE.md](ESSPROBE.md)).

Each fact names its source. Where only the card can tell, the fact also
names the essprobe cases that measure it, such as `a2.wide`, and once a
report of the probe has come back, what the card did follows as "Measured".
Nothing on this page is copied from the sources' code.

The measurements so far come from one card, an ES1869 at 220h with
Config_Base 250h in a laptop with Intel's PIIX4, as ESS's PnP setup left it:
three recorded runs of essprobe 1.0 in DOS 7.10, a short and a full one
under MS EMM386 4.95 and a full one without a memory manager. The full run
under EMM386 stopped in the h1 group ([The DMA controllers and the memory
managers](#the-dma-controllers-and-the-memory-managers)).

## Sources

* **The data sheet**, SAM0023-122898, in `docs/datasheet/`, cited as DS
  p.NN.
* **ESS's drivers** in `driver/`, rebuilt from `src/es1869` and `src/vxd`.
  An address such as 6:28AC is a segment and offset in `ES1869.DRV`, as in
  [DRIVER_CONFIG.md](DRIVER_CONFIG.md), and one such as o5:3594 an object
  and offset in `ES1869.VXD`, as in [VXD_INTERNALS.md](VXD_INTERNALS.md).
* **Drivers by others** that program the chip in Extended mode, each at the
  revision given, and named here as they are named in the text:
  * es18xx: Linux's ALSA driver, `sound/isa/es18xx.c` at commit
    2687c848e578, with `sound/core/isadma.c`
    (https://github.com/torvalds/linux/blob/2687c848e578/sound/isa/es18xx.c);
  * es1938: its driver of ESS's PCI Solo-1, a chip with the same two audio
    channels, `sound/pci/es1938.c` at the same commit;
  * NetBSD: `sys/dev/isa/ess.c` revision 1.91, with `essreg.h`
    (https://github.com/NetBSD/src/blob/trunk/sys/dev/isa/ess.c);
  * OSS: Linux 2.4.37.11's `drivers/sound/sb_ess.c`
    (https://github.com/ReliefLabs/EasyTomato/blob/128481a88bef18a8fdf7f581e4cff02eee2bce6d/release/src/linux/linux/drivers/sound/sb_ess.c);
  * Allegro: Allegro 4.4's `src/dos/essaudio.c`, with `sb.c` and `dma.c`
    (https://github.com/liballeg/allegro5/blob/8796dd2d243b1027e7f65a0657f694dbf8d6ec5d/src/dos/essaudio.c);
  * Impulse Tracker: its `SoundDrivers/ES1869.ASM`, which ES1868.ASM
    matches, and ES1688.ASM
    (https://github.com/jthlim/impulse-tracker/blob/master/SoundDrivers/ES1869.ASM).
* **Emulators**: DOSBox-X's `src/hardware/sblaster.cpp` at commit
  5f3ac7de3bfe
  (https://github.com/joncampbell123/dosbox-x/blob/5f3ac7de3bfe0e0a9db4445917e4d4062f0cea99/src/hardware/sblaster.cpp),
  and 86Box's `src/sound/snd_sb.c`, `snd_sb_dsp.c` and `snd_solo1.c` at
  commit 236252e79721
  (https://github.com/86Box/86Box/blob/236252e79721d314b6b019bdbf5e29ed65d63f04/src/sound/snd_sb.c).
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

The data sheet doesn't list E7h, which the drivers ask first. es18xx
(`snd_es18xx_identify`) takes 48h for an ES488 and 68h with a low nibble
under 8 for an ES688, then reads mixer 40h twice: two different bytes are
the chip's number, and the next two its control port. NetBSD and OSS tell
the chips apart with the same reads and with probes of mixer 64h and 70h.
Impulse Tracker's ES1688 driver expects 68h and then 88h to 8Fh. DOSBox-X
answers E7h with 68h and 84h or 89h, and 86Box with 68h and 8Bh, which its
notes give as a real ES1688's. es18xx gives only the ES1869 and the ES1879,
of the 186x and 187x chips, the new rates and the second channel. Probe:
`env.card`.

Measured: E7h answers 68h and 8Bh, and E1h 3 and 1 (`env.card`).

## DMA channels and interrupts

The chip has four DRQ and DACK pin pairs. Card registers 23h (pins B and A)
and 24h (pins D and C) give each pin's ISA channel, a nibble each, and the
chip loads them from its PnP header; an unused pin is given channel 2 (DS
p.30). The audio device's PnP registers 74h and 75h select Audio 1's and
Audio 2's channels, and a pin drives its DRQ only while a device's DMA
register names the pin's channel and the device is active, PnP 30h bit 0 (DS
p.13, p.31-32). essprobe's `env.card` reads all of them.

Measured: card registers 23h = 10h and 24h = 25h wire the pins to channels
0, 1, 5 and 2, PnP 74h and 75h give Audio 1 channel 1 and Audio 2 channel 5,
and card register 25h is 80h, with the DRQ latch on (`env.card`).

The data sheet maps Audio 2 to the 8-bit channels 0, 1 and 3 only, and wants
at least two pins on 8-bit channels, with the 16-bit channels 5, 6 and 7 for
the external DMA source (DS p.13). The chip's data bus to the FIFO is 8 bits
wide (DS p.69, B5h), so it takes one byte per DMA cycle on any channel. On a
16-bit channel the 8237 moves a word per cycle and the chip gets the word's
low byte. ESS's drivers nevertheless play Audio 2 on channel 5, with each
byte of the sound in the low byte of a word of a buffer twice as long:
ES1869.DRV copies the sound so at 1:1B82, and ES1869.VXD at `L5_3309` for
DirectSound (VXD_INTERNALS.md). On such a channel the 8237 counts words,
which are bytes to the chip. Mixer 78h bit 5 is reserved (DS p.65).

The drivers by others set 78h bit 5 for Audio 2 on a 16-bit channel instead:
es18xx writes B3h there, with its counts in bytes and a note that 16-bit DMA
"seems broken" and wasn't tested, NetBSD writes the count in words ("use
word count for 16 bit DMA"), and OSS sets bit 5 and 7Dh with its counts in
bytes. 86Box's model takes a word a DMA cycle on a 16-bit channel and counts
it once, whatever bit 5 says. So there are two models of Audio 2 on a 16-bit
channel: the low byte of each word, as ESS's drivers use it without bit 5,
and the whole word, as NetBSD and 86Box use it with bit 5. DOSBox-X's notes
say that ESS's chips use the 8-bit channel even for 16-bit sound. Probe:
`a2.base` on a card with Audio 2 on a 16-bit channel (the spread buffer),
`a2.packed` (a game's 16-bit buffer as it is), `a2.wide` and
`a2.wide_packed` (78h bit 5 set).

Measured: ESS's way plays cleanly on channel 5, each byte in the low byte of
a word (`a2.base` and the other a2 cases). A game's own 16-bit buffer
without bit 5 plays its low bytes only, as noise an octave up (`a2.packed`).
With bit 5 the chip interrupts half as often for the same count, as if it
counted words, but neither buffer plays: the spread one comes out 45 dB
under a tone's level at half the pitch (`a2.wide`), and a game's buffer as
noise 50 dB under it (`a2.wide_packed`, `stream.a2w`). So bit 5 doesn't make
a game's 16-bit buffer playable on Audio 2, and a front has to carry the
game's channel over to an 8-bit one.

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
reads the port three times and writes 0 (`DSP_Reset`, o5:195C).

The drivers by others reset the chip with 3 and then 0, a few reads apart,
and send C6h after it. es18xx resets the chip only when it identifies it
(`snd_es18xx_identify`), and only the FIFO for a stream (+6 = 2, a read,
then 0). es1938 resets it at its start (`snd_es1938_reset`) and Allegro when
it starts (`ess_init`), and Impulse Tracker and OSS reset it before each
playback and to stop one. OSS's notes say that a reset leaves 7Ah, 7Dh and
7Fh alone, against the data sheet. DOSBox-X ends Extended mode at a reset as
the data sheet does, but 86Box keeps it until C7h. Probe: `rst.ext` (3),
`rst.compat` (1), `rst.fifo` (2) and `rst.mixer` (mixer 00h) set known
values in 70h to 7Ch, the levels and A1h to B9h, then reset, and report what
changed.

Measured: both software resets keep the whole mixer, 70h to 7Ch included, as
OSS's notes say and against the register pages. They set A1h to DCh, A2h to
E7h, A4h and A5h to 00h and F8h, A8h to 02h, B7h to 44h and B9h to 00h, and
clear bit 6 of B1h and B2h (`rst.ext`, `rst.compat`). The FIFO's reset
changes no register (`rst.fifo`). A write to mixer 00h sets 14h and 36h to
88h and 38h, 3Ah and 3Eh to 00h, and keeps 3Ch and 70h to 7Ch (`rst.mixer`).

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

ESS's drivers never play on Audio 1 (AUDIO1.md). The drivers by others do,
in about the data sheet's order, and AUDIO1.md compares es18xx's with
`build/ES1869.DRV` ([Programming the Audio 1
DAC](AUDIO1.md#programming-the-audio-1-dac)):
* **es18xx** writes C6h, B9h = 02h, B1h and B2h = 50h and D1h once, at its
  start (`snd_es18xx_initialize`). For each stream
  (`snd_es18xx_playback2_prepare`) it resets the FIFO, then writes A8h's
  bits 1:0, A1h and A2h, A4h and A5h, B6h, and B7h twice, 51h or 71h and
  then 90h with 40h for mono or 08h for stereo, 04h for 16 bits and 20h for
  signed samples. It starts with B8h = 05h and stops with 00h.
* **es1938** goes the same way, but its second write of 51h or 71h goes to
  B6h, not to B7h, before B7h's format (`snd_es1938_playback2_prepare`).
* **NetBSD** writes B7h once, without B6h or a first write, and in another
  order: A8h, B7h, the 8237, A4h and A5h, B9h, D1h, and B8h with bits 0 and
  2 last (`ess_audio1_trigger_output`). It stops with B8h bit 0 cleared.
* **Allegro** resets the chip, sends C6h, and writes A1h, A2h, B8h = 04h,
  A8h, B6h = 00h, B7h = 71h and then 9Ch, B9h = 02h, B1h and B2h ORed with
  50h, and starts the 8237. Each playback then writes A4h and A5h and B8h =
  05h, and D1h comes 100 ms later (`ess_init`, `ess_play_buffer`). Its
  samples are unsigned, and only the second B7h says so: B6h = 00h and the
  first write of 71h are the values that the others use for signed samples.
* **Impulse Tracker** resets the chip, then sends C6h, B8h = 04h, A8h, B9h =
  02h, A1h, A2h = FEh, A4h and A5h, B6h = 00h, B7h = 71h and then BCh or
  F4h, B1h = 50h, B2h = 50h, D1h, and B8h bit 0 (`StartESS`). It stops with
  a reset.
* **OSS** resets the chip, sends C6h, writes A1h and A2h, B8h = 04h, A8h,
  B9h = 02h, B6h and B7h twice from a table, B1h and B2h, and D1h, and
  starts with A4h and A5h and B8h's bits 0 and 2
  (`ess_audio_prepare_for_output_audio1`). It stops with a reset.

Every one of them writes B9h = 02h and sets B8h bit 0 last, and each
acknowledges an interrupt with a read of Audio_Base+Eh; NetBSD checks +Ch
bit 0 first.

### The registers

* **A1h, the rate** (DS p.67, p.64). With bit 7 clear, the rate is the low
  clock over 128 minus the value; with bit 7 set, twice that clock over 256
  minus the value, for rates above 22050 Hz. The data sheet gives the clocks
  as 397.7 and 795.5 kHz, but the first report measured 396.9 and 793.8 kHz,
  at which 22050 and 44100 Hz come out exactly. With mixer 71h bit 5 set,
  A1h works as 70h does instead. ES1869.DRV's rate routine (6:1FF4) uses the
  high clock above 22050 Hz, and its recording sets 71h bit 5 for 48000 Hz
  only (6:22D3). es18xx takes 397722 and 795444 Hz for the old clocks, and
  on the ES1869 and the ES1879 it sets 71h bit 5, with 793800 Hz for bit 7
  clear and 768000 Hz over 256 minus the value for bit 7 set
  (`snd_es18xx_rate_set`); NetBSD uses the new clocks too, 793800 Hz for
  rates that aren't multiples of 8000 and 768000 Hz for those that are
  (`ess_srtotc`). OSS gives the ES1869 the old clocks, switching at 22000
  Hz, with its write of 71h bit 5 left out ("For ES1869 only???"), Allegro
  the old ones switching at 22094 Hz, and Impulse Tracker only the high one.
  Probe: `a1.r8000` to `a1.r48000`, `a1.lo44100` and `a1.hi22050` (the other
  clock), `a1.new48000` (71h bit 5). Measured: every rate came out as the
  clocks of 396.9 and 793.8 kHz give it, 7938 Hz for 8000 on the low clock
  and 46694 Hz for 48000 on the old clocks, and 48012 Hz in the new mode.
* **A2h, the filter** (DS p.68, p.72). The filter's clock is 7.16 MHz over
  256 minus the value, and its roll-off is the clock over 82, best at 80 to
  90 percent of half the rate. For the high clock's divisors 18 to 35,
  ES1869.DRV takes the value from one of two tables, which its flag for the
  direction picks, one for playback (DGROUP 4Eh) and one for recording
  (60h), and otherwise computes it (6:1FF4, 6:2308). es18xx and es1938 write
  256 minus 7.16 MHz times 20 over 8 times 82 times the rate, a roll-off at
  80 percent, NetBSD 256 minus 200279 over the rate, at 87 percent
  (`ess_srtofc`), OSS and Allegro about 90 percent, and Impulse Tracker
  always FEh.
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
  ORs 50h in (6:2934). Probe: `a1.irq_off` (bit 6 clear). Measured: with bit
  6 clear no interrupt comes, and an interrupt never acknowledged is the
  last (`a1.noack`).
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
  `a1.b9_1` and `a1.b9_3`. Measured: 00, 01 and 10 run at the rate, and with
  11 the DMA runs at nearly twice it (`a1.b9_3`), so 11 is the reserved
  value.

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

The 8237's count gives the position. es18xx's `snd_dma_pointer` masks the
channel and reads the count twice, keeping the larger value, es1938 reads it
until two reads agree, and Allegro reads it twice and again while the two
differ by more than 40h (`_dma_todo`). Impulse Tracker counts the blocks
instead.

The data sheet's command list gives D0h and D4h as the pause and the
continue of the DMA (DS p.73), and its steps for Extended mode count "DMA
pause/continue" among the commands to send during a transfer (DS p.48), but
its comparison of the modes counts only D1h and D3h as safe with Extended
mode transfers (DS p.44). OSS pauses an Extended mode transfer with D0h and
starts it anew to continue (`ess_audio_trigger`). The others never pause. A
front can pause through B2h bit 6, which keeps the chip's state, through B8h
bit 0, the DMA's reset, or perhaps through D0h. Probe: `a1.pause_b2`,
`a1.pause_b8`, `a1.pause_d0`.

Measured: clearing B2h bit 6 or B8h bit 0 stops the DMA within a few bytes,
and the transfer goes on at its rate when the bit is set again
(`a1.pause_b2`, `a1.pause_b8`), though not always in step ([A 16-bit stream
a byte off](#a-16-bit-stream-a-byte-off)). D0h doesn't pause an Extended
mode transfer, whose DMA runs on (`a1.pause_d0`).

### A 16-bit stream a byte off

Measured: every 16-bit stream on Audio 1 played one byte off, so that each
sample took its high byte from the next byte, and noise came out 12 dB
louder than the tone. That held in real mode and under EMM386, at every
rate, in every transfer type and with the FIFO kept or filled first
(`a1.base` to `a1.noack`, `stream.a1`). A tone on one side came out on the
other, loud and noisy, as a stream one byte ahead plays it (`a1.left`,
`a1.right`). 8-bit streams played cleanly, since a byte only delays a mono
stream by a sample. Only `a1.after` under EMM386 played in step, in both
runs: its DMA was unmasked after the start, and EMM386 took tens of
microseconds to pass the unmask on to the 8237. In real mode, where the
unmask reaches the 8237 at once, the same case played a byte off. In the
cases of both engines, Audio 1's stream was in step in some runs and off in
others (`both.f16_8`, `both.pace`).

So the start is a race between the DMA's first bytes and the chip's start.
Audio 2 has the same race, which a single write of 78h wins ([Audio
2](#audio-2)). es18xx's note of Audio 1 garbled at 16-bit stereo may be the
same fault.

Measured by the fix group, run twice in real mode and twice under EMM386
(`fix.*`): with the channel unmasked before B8h bit 0, the stream played a
byte off every time, whatever came before the start: the FIFO emptied after
the format as `build/ES1869.DRV`'s player does it (`fix.win1` to `fix.win3`,
`fix.r22win`, `fix.r11win`), and B7h written once, or its first write
without the load bit (`fix.once7a`, `fix.noload1`). In 8-bit stereo the byte
swapped the sides (`fix.s8base`). With the channel unmasked 20 us or more
after B8h bit 0, every stream played in step, at 44.1 kHz (`fix.late20` to
`fix.late2000`), at 22.05, 11.025 and 8 kHz with 200 to 400 us
(`fix.r22late`, `fix.r11late`, `fix.r8late`), and in 8-bit stereo
(`fix.s8late`). Unmasked right after the start, the stream played in step
under EMM386, which takes that long to pass the unmask on, and a byte off in
real mode (`fix.late0`). The interrupts kept their time in every case: the
lost byte counts. With B2h bit 6 set only 100 or 300 us after B8h bit 0 and
the channel unmasked, the stream played a byte off in all eight runs
(`fix.b2late1` to `fix.b2late3`, `fix.r11b2late`): the byte at risk is the
first of the start's first DMA request, whenever the request comes, and a
channel masked over it wins the race. The fronts hold the channel masked
over the start for at least 32 us and two sample periods
([SB16_FRONT.md](SB16_FRONT.md#a-transfer)), and `tests/esschip.py` loses
the start's first byte when it comes within 10 us of the request. With the
fronts holding the channel, `sb16test`'s recorded run through `esssb16`
under EMM386 played every 16-bit sound in step, 57 to 59 dB clean, its sides
apart by 63 dB (`l.left16`, `l.right16`, `l.clean.16bit`,
`l.clean.stereo16`).

A pause loses the step about half the time, whichever way it pauses and
continues, from an in-step start. Audio 1 resumed in step after B2h bit 6 in
6 of 8 runs, all four under EMM386 and two of four in real mode
(`fix.pb2late` to `fix.pb2late3`), and after B8h bit 0 in 2 of 4
(`fix.pb8late`). With the channel masked over the resume and unmasked 100 us
after, it resumed in step in 1 of 3 runs after B2h bit 6 and 1 of 2 after
B8h bit 0 in real mode (`fix.pb2hold1` to `fix.pb8hold2`). Audio 2 resumed
in step in 6 of 11 runs after 78h bit 0, whose DMA runs on meanwhile
(`a2.pause_f`, `fix.a2pf1`), in 7 of 14 after bit 1 (`a2.pause_d`,
`fix.a2pd1`), in 6 of 12 after both (`fix.a2pfd1`), and with the channel
masked over the resume in 2 of 3 in real mode (`fix.a2pdhold1`). Under
EMM386 the probe's unmask of a held channel went through EMM386, which
started the channel over at the buffer's start ([The DMA controllers and the
memory managers](#the-dma-controllers-and-the-memory-managers)), so those
runs don't count. The offsets were one, two and three bytes.

Paused through B7h bit 7, which takes the FIFO off the DAC, the stream
resumed in step in all 12 runs, in both modes, in 16-bit stereo and mono, in
8-bit stereo and at 22.05 kHz (`fix.pb7a` to `fix.pb7r22`). The DAC takes
nothing from the FIFO in such a pause, and the DMA moved only 4 to 12 bytes
more, as the FIFO filled. So a resume has no race of its own: the step goes
while the DAC plays on in the pause and runs the FIFO down into a frame that
the DMA left half done. The DMA goes on until the FIFO is full, though,
which right after a start can be most of its 256 bytes, and a program that
pauses and programs its channel anew would lose the next transfer's first
bytes to it. The fronts therefore pause through B7h bit 7 and then B2h bit
6, and continue through B2h bit 6 and then B7h bit 7, which `fix.pb72a` to
`fix.pb72r22` measure.

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

### What drivers by others do

The drivers by others write the same registers, with these differences:
* **es18xx** writes 70h, A2h and 72h for the rate, with a note from OSS that
  72h doesn't seem to work, then 74h and 76h in bytes, 7Ah's bits 2:0, and
  78h = 93h to start and 00h to stop (`snd_es18xx_playback1_prepare`,
  `snd_es18xx_playback1_trigger`). 7Ch is written only in a mode that is
  never built, and 71h = 32h at its start sets bit 1, Audio 2's own rate,
  with 4x oversampling and A1h's new mode.
* **es1938** writes 78h = 92h, waits 10 us and writes 93h, because the data
  sheet's single 13h "randomly swaps" the stereo channels, and the two steps
  give the FIFO's way to the DAC time to settle before the DMA's data comes
  (`snd_es1938_playback1_trigger`).
* **NetBSD** writes 78h once, with demand transfers of 8 bytes,
  auto-initialize and bits 1:0, D3h, plus bit 5 on a 16-bit channel
  (`ess_audio2_trigger_output`), and stops by clearing bits 1:0.
* **OSS** writes 78h's demand transfers of 8 bytes and auto-initialize
  first, D0h, and sets bits 1:0 to start.

86Box's model plays while 78h bits 1:0 are set, and suspends while bit 2 is
set or bits 1:0 are clear; 7Ah bits 7:6 both set give a test interrupt, and
an ES1869 with 71h bit 1 clear plays Audio 2 at Audio 1's rate. Probe:
`a2.start2` and `a2.left2` (78h's bit 1 before bit 0, as es1938 starts it).

Measured: a start in one write of 78h, bits 1:0 together, played in step
every time, in every format, rate and transfer type (`a2.base` and the
others). A start in two steps, the DMA 10 us before the FIFO's way to the
DAC as es1938 starts the Solo-1, played one, two or three bytes off in three
of four runs (`a2.start2`, `a2.left2`): on the ES1869 the one write is the
safe start. Clearing 78h bit 0 silences the DAC, but the DMA goes on at
about twice the stream's rate while the FIFO drops its bytes, so the stream
skips what moved meanwhile; it resumes in step (`a2.pause_f`). Clearing bit
1 stops the DMA within a few bytes, but the stream resumed a byte off in one
of two runs (`a2.pause_d`). With 71h bit 1 clear, Audio 2 plays at Audio 1's
rate (`a2.slaved`). On channel 0, which a pin of the card reaches, it played
as on channel 5 (`a2.ch0`, since taken out, because the probe keeps to the
channels the card is configured for).

Probe: `a2.f8m` to `a2.u16` (the formats), `a2.r8000` to `a2.r48000`,
`a2.t2_0`, `a2.t2_1` and `a2.t2_3` (78h bits 7:6, the base case being 10),
`a2.dmd` (the 8237 in demand mode as well, ESS's own setup), `a2.slaved`
(71h bit 1 clear), `a2.x4`, `a2.fb` (the filter), `a2.once`, `a2.pause_f`
and `a2.pause_d` (78h bits 0 and 1), `a2.irq_off` and `a2.noack`.

## Both channels at once

The data sheet's full duplex records on Audio 1 and plays on Audio 2 (DS
p.52). With the same count in A4h and A5h and in 74h and 76h, one interrupt
serves both, and the steps start the playback first, so that its FIFO fills
before the recording's first interrupt. The data sheet describes no playback
on both DACs.

es18xx's notes say that on the ES1869, with both channels playing 16-bit
stereo at 44.1 kHz, Audio 1's DMA slows down irregularly and its sound is
garbled, as it is when Audio 1 records meanwhile; that Windows' driver uses
Audio 1 only to record; and that the ES1869's two rates are independent,
against the data sheet, while the ES1868's are one. OSS's notes say that 78h
survives a reset, so that Audio 2 plays on from Audio 1's DMA channel when
both share it. The ES1869's case of es18xx's notes is the probe's
`both.base`.

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

Measured: under EMM386 both engines kept their rates at 16-bit stereo and
44.1 kHz. In real mode, with the 8237 in single mode, Audio 2's DMA fell
about 12 percent short while the probe read the 8237's counts (`both.base`,
`both.single`), and with the 8237 in demand mode both kept their rates
(`both.dmd`). Mixed formats and rates kept them too (`both.f8_16`,
`both.f16_8`, `both.r22_44`). Muted, slaved, in 8-bit stereo and without its
interrupt, Audio 2 moved its channel's count at Audio 1's rate
(`both.pace`). The recordings of the dual cases depend on Audio 1's stream
being in step ([A 16-bit stream a byte off](#a-16-bit-stream-a-byte-off)).

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

Measured under MS EMM386 4.95: a channel's programming reaches the 8237 at
the program's unmask, on channels 1 and 5 alike, and until then the program
reads back what it wrote (`h1.reach1`, `h1.reach5`). So a channel read back
before the unmask carries over what the 8237 had before (`h1.stale`), which
played another buffer. In real mode the 8237 gets each write at once. The
8237's mask register reads back, 0Fh where the channels 0 to 3 are masked,
and DEh 0Eh with channel 4, the cascade, unmasked (`env.dma`). The first
full run stopped in the h1 group with EMM386's "DMA mode not supported", at
`h1.cross1`, an auto-initialized transfer across a 64 KB page, which EMM386
would have to buffer. Made single-cycle, as the probe now makes it under a
memory manager, the transfer went through a buffer of EMM386's at 120000h,
for channel 1 and channel 5 alike (`h1.cross1`, `h1.cross5`), and the
channel read back before the next unmask still pointed there (`h1.stale`). A
program's reads come from the 8237 while the channel moves (`h1.read1`,
`h1.read5`), and move the 8237's flip-flop, while its writes move EMM386's
own (`h1.ff1`, `h1.ff5`). A program that masks a running channel and unmasks
it has EMM386 program the 8237 anew at the unmask, from what the program
wrote, so the transfer starts over at the buffer's start: the probe's count
jumped back, and the rate it measured came out 4.6 percent high
(`fix.pb2hold1` and the other held cases under EMM386).

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

Measured in real mode: a busy loop keeps 90 percent of its speed with Audio
1 in demand transfers, 84 percent in single transfers, 86 percent with Audio
2, 81 percent with the pace's settings and 76 percent with both engines, and
the VGA's retrace stays every 16564 us within 5 us. No VGA DAC entry and no
guard byte around the buffers changed in any case (`h4.*`), so the engines
and the pace leave the palette and memory alone. A channel programmed the
wrong way round writes FFh, the floating bus, into memory, and halves the
loop's speed (`h4.wrong1`, `h4.wrong2`).

## Where the sources disagree

The data sheet contradicts itself, or ESS's drivers, in these places:
* Table 16's signed and unsigned columns (DS p.49) are the wrong way round
  (AUDIO1.md).
* B9h's encoding differs between the steps (DS p.48) and the register's page
  (DS p.70). Measured: 00, 01 and 10 run at the rate, and 11 doesn't.
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
  Measured: a software reset keeps it.
* The steps turn Audio 2's volume up 100 ms after the start (DS p.52), and
  ES1869.DRV writes it before the start (6:2D6D).
* ES1869.VXD sends C3h, which returns a status byte, and C2h 01h, neither in
  the data sheet, before it restores a DOS box's mixer (VXD_INTERNALS.md).
* D0h and D4h pause and continue an Extended mode transfer by the steps (DS
  p.48) and OSS, but aren't safe with one by the comparison of the modes (DS
  p.44). Probe: `a1.pause_d0`. Measured: D0h doesn't pause one.
* 78h bit 5 is reserved (DS p.65), and es18xx, NetBSD and OSS set it on a
  16-bit channel, where NetBSD counts words and es18xx and OSS bytes. Probe:
  `a2.wide`, `a2.wide_packed`. Measured: with it the chip interrupts as if
  it counted words, but a game's 16-bit buffer doesn't play.
* A reset clears 7Ah (DS p.65), and OSS's notes say it doesn't. Probe:
  `rst.ext`, `rst.compat`. Measured: it doesn't.
* A reset ends Extended mode (DS p.43), as DOSBox-X has it, and 86Box keeps
  it until C7h.
* The data sheet writes 78h's bits 1:0 at once (DS p.52), and es1938 one
  after the other, because the stereo channels can swap otherwise. Probe:
  `a2.start2`, `a2.left2`. Measured: on the ES1869 the one write keeps the
  stream in step, and the two steps lose it.
* The second B7h write's value is the FIFO's format for every driver, but
  es1938 writes the first one's 51h or 71h to B6h, and NetBSD writes no
  first one at all.
