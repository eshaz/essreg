# Where skipped samples come from

What happens to a Windows sound between a program and the ES1869's DAC, how much time each step has, and what can make samples go missing.

## In short

* The step with the least time to spare is the DMA controller's answer to the chip. Audio 2's FIFO holds only 0.36 ms of 44.1 kHz 16-bit stereo. Every step of the driver has 62 ms or more.
* So a skip most likely means a DMA request answered too late.
* The chip has a feature for this, the **DRQ latch**: it holds each DMA request until the DMA controller answers it. The card's PnP ROM or EEPROM switches it on or off at power-up, and ESS's own example ROM data leaves it off.
* ESS's driver has a second knob: **single transfers** instead of demand transfers ("Use single mode DMA").
* Neither needs a driver change. [Finding out on the card](#finding-out-on-the-card) has the steps.

## The path of a sound

1. A program writes blocks with `waveOutWrite`. ES1869.DRV (`wodMessage`, 6:18E4) copies them into a DMA buffer.
2. ES1869.VXD allocates that buffer when the device starts: 32 KB for each DMA channel (`src/vxd/pcod.asm` 0631h passes 20h KB, `pnp.asm` 045E-0507).
3. The driver plays it as two blocks of 1/16 s of audio, at most half the buffer each (6:23E8-6:2432). At 44.1 kHz 16-bit stereo, a block is 11,024 bytes, 62 ms.
4. It programs the 8237 in auto-initialize mode itself (6:2C84-6:2D4F), and Audio 2 with 78h = 93h: auto-initialize, demand transfers of 4 bytes (DS p.65).
5. Audio 2 reads the buffer through its FIFO, 32 words deep (DS p.65), and plays it on its DAC.
6. After each block, Audio 2 raises its interrupt.
   * ES1869.VXD's handler (`lcod.asm` D1_054C) turns Audio 2's interrupt off at the chip (7Ah bit 6) and passes the interrupt to Windows.
   * ES1869.DRV's handler, which it copies into fixed memory (3:01B9, 3:02C3), refills the block that just played from the program's buffers with interrupts on. Then it clears Audio 2's interrupt latch (7Ah bit 7) and sends the EOI.
   * On that EOI, ES1869.VXD turns Audio 2's interrupt back on (L1_09E8) and ends the interrupt at the PIC. Both save and restore the mixer index around their mixer accesses.

Recording runs the other way, on Audio 1: a 256-byte FIFO, demand transfers of 2 bytes (B9h = 02h), and the same block sizes.

## How much time each step has

| Step | Has | Lasts |
|---|---|---|
| The DMA controller answers a request, Windows playback (Audio 2) | a 64-byte FIFO | 0.36 ms at 44.1 kHz 16-bit stereo |
| The DMA controller answers a request, Windows recording (Audio 1) | a 256-byte FIFO | 1.45 ms at 44.1 kHz 16-bit stereo |
| The DMA controller answers a request, Sound Blaster (Audio 1) | a 64-byte FIFO | 2.9 ms at 22 kHz 8-bit mono |
| The driver handles the interrupt | one block | 62 ms |
| The program sends its next buffer | its own queue | usually hundreds of ms |

* A DMA request answered too late skips. The FIFO runs dry while the DAC keeps its pace, so it has nothing to play: a click or a short dropout.
* An interrupt handled more than 62 ms late doesn't skip. The DMA plays the old block again: a repeat, a stutter.
* The FIFO sizes are on DS p.10 and p.44. In Sound Blaster mode, which most DOS games use, Audio 1's FIFO is 64 bytes and the chip's firmware runs it.

## What makes a DMA answer late

* **A request that goes away unanswered.** Without the DRQ latch, a request can go low again before the DMA controller answers it. The latch keeps it up: "each of the four audio DRQs is latched high until one of the following occurs: A DACK low pulse ... A hardware reset ... 8-16 milliseconds elapse while DRQ is low" (DS p.15).
  * Card register 25h bit 7 switches it on. The chip loads 25h from the 8-byte header of its PnP data after a PnP reset: an internal ROM, or an external EEPROM (DS p.24, p.30).
  * ESS's example ROM data has "DRQ latching off" (DS p.88).
* **Demand transfers.** With 78h = 93h the chip asks for 4 bytes at a time and holds DRQ for them. ESS's driver offers single transfers instead, for chipsets that handle demand transfers badly: one byte per request, 78h = 13h for Audio 2, and B9h left as the DSP reset sets it for Audio 1 ([DRIVER_CONFIG.md](DRIVER_CONFIG.md)).
* **Other traffic on the bus**, like another DMA channel, a bus master, or a PCI device holding the bus, delays the answer too. Some BIOSes have settings for how the chipset shares the bus with ISA DMA, like *Passive Release* and *PCI Delayed Transaction*.

## Finding out on the card

1. In essctl, *Plug and Play* shows *DRQ latch*, and *Status & interrupts* shows *PnP data source* (internal ROM or EEPROM).
2. If the DRQ latch is off, turn on Expert mode and switch it on.
   * If it stays on (essctl reads it back), listen: do the skips go? A write lasts until the computer restarts.
   * If essctl says the chip returns 0, the chip only takes it from its ROM or EEPROM.
3. Switch on "Use single mode DMA": Control Panel > Multimedia > *Advanced* > the ES1869 audio device > *Properties* > *Settings*. Restart Windows and listen.
4. In Device Manager, note the ES1869's DMA channels, and what else uses DMA.
5. Note when the skips happen: in Windows sounds (Audio 2), in DOS games (Sound Blaster, Audio 1), or both, and what else is busy (disk, CD-ROM, network).
6. If the BIOS has *Passive Release* or *PCI Delayed Transaction*, try switching each one, one at a time.

## What software can do about it

* If the DRQ latch takes a write, ES1869.VXD's extension could set it each time the device starts.
* If it doesn't, the card's EEPROM could be changed: a tool that reads it, saves a copy, and rewrites the one header byte. That's only possible with an EEPROM (not the internal ROM), and a bad write can make the card vanish from Plug and Play until the EEPROM is written again.
* The driver's buffering is generous (62 ms per block), so changing ES1869.DRV wouldn't help a FIFO that runs dry. ES1869.DRV isn't rebuilt from source yet anyway.
* The rebuilt ES1869.VXD has ESS's audio code unchanged, so it plays the same way as ESS's.
