# Audio 1: the ES1869's second DAC

What the ES1869's first audio channel can do, what ESS's drivers use it for, and how `build/ES1869.DRV` plays through it.

* Page numbers are the ES1869 data sheet's (`docs/datasheet/`). Addresses like `6:2D75` are `ES1869.DRV`'s, as in [DRIVER_CONFIG.md](DRIVER_CONFIG.md).
* Register names are the catalog's, as essctl shows them ([REGISTERS.md](REGISTERS.md)).

## Two channels, three converters

The ES1869 has two DMA audio channels (DS p.19-20, Figure 10):
* **Audio 1** runs the CODEC: a stereo ADC and a stereo DAC with one sample clock (A1h) and one filter (A2h). It's one or the other at a time: controller register B8h bit 3 picks the direction. Its FIFO holds 256 bytes. Its DAC reaches the mixer at the Audio 1 play volume, mixer 14h, while command D1h has it on (D3h turns it off).
* **Audio 2** is a second stereo DAC with its own clock (mixer 70h) and filter (72h), a 64-byte FIFO, and the play volume 7Ch.
* Both DACs play into the same mixer. Mixer 71h bit 1 clear slaves Audio 2 to Audio 1's clock and filter.

## What ESS's drivers do with it

* **ES1869.DRV records on Audio 1 and plays on Audio 2.** Wave-in takes Audio 1 as its "user 2" (`wid_acquire`, 4:00E0), wave-out takes Audio 2 (`wod_acquire`, 4:0054). Nothing plays through the Audio 1 DAC.
* **Every Audio 2 start turns the Audio 1 DAC off** with D3h (6:2D75).
* **The ISR has room for more.** The interrupt handler is the DDK sample's `ISR_Stub` ([RE_NOTES.md](RE_NOTES.md)). It calls `isr_srv_table[(user − 1) & 3]` for an Audio 1 interrupt, and the table (7:00AE) holds `isr_record` for users 1 and 2 and `isr_play` for 3 and 4, the Audio 2 routines. ESS's code only ever sets user 2, so slot 1 is free. The same table also serves Audio 2 (from +8), so users 3 and 4 aren't free.
* **The device structure has a playback DMA mode for Audio 1.** At enable, 3:49DB stores 58h plus the channel (single, auto-initialize, memory to device) next to the recording mode 54h. Only the recording mode is used.
* **The rate routine has a playback filter table.** 6:1FF4 sets A1h and A2h for Audio 1, and with its flag at 0 it takes the filter clock from a second table (DGROUP 4Eh) instead of the recording one (60h). ESS's code only calls it for recording.
* **A voice recording gives way to a playback.** A wave-in client ESS keeps apart (device structure +78h) is stopped when wave-out opens (6:0470) and started again when it closes (6:0566).

### Telegaming

The data sheet's telegaming mode (DS p.19-20) is what Audio 1's playback path was built around:
* A modem or speakerphone DSP connects to the ES1869's serial port (SE, DCLK, DX, DR, FSX, FSR). While the serial port is on (48h bit 7, or the SE pin), the Audio 1 CODEC works for that DSP: its ADC and DAC go through the serializer.
* **Telegaming** (48h bit 1) then sends Audio 1's DMA stream, a game's Sound Blaster sound, to the Audio 2 DAC at the Audio 1 volume (14h), so the game is heard while the modem uses the CODEC.
* ESS's Windows driver only sets the volume: it reads "Telegaming Vol" into 14h at enable and resume (6:1FE8). The INF's "Telegaming" value is read by neither driver, and nothing writes 48h bits 7 or 1.
* On a card without a serial DSP, Audio 1's DAC is free whenever nothing records.

## The Windows side

From the Windows 95 DDK (`DESGUIDE\MMEDIA.DOC`, "Opening and Closing Devices", and the MSSNDSYS sample):
* A wave driver reports its devices with `WODM_GETNUMDEVS`, per devnode, and every later message carries the device number.
* **Two ways to serve more than one program.** A second device ID "for routing to different playback hardware", with its own `WAVEOUTCAPS` name, or one device that accepts several `WODM_OPEN`s.
* `WODM_OPEN` answers `MMSYSERR_ALLOCATED` when the hardware is busy, and a program or the wave mapper tries elsewhere.
* ES1869.DRV's `wodMessage` (6:18E4) answers device 0 only (`MMSYSERR_BADDEVICEID` otherwise), and `WODM_GETNUMDEVS` 1 for an enabled devnode.
* DirectSound doesn't go through `wodMessage`: ES1869.VXD plays it on Audio 2. It can't take the chip while a wave device holds the DSP (o1:125C checks the VxD's owner).

## The Audio 1 player in build/ES1869.DRV

`build/ES1869.DRV` plays a second wave-out stream through the Audio 1 DAC, while Audio 2 plays the first. Two `SYSTEM.INI` keys under `[ES1869.DRV]` turn it on, both on by default ([DRIVER_CONFIG.md](DRIVER_CONFIG.md#62-es1869drv)):
* **`Audio1Device=1`: a second device**, "ESS AudioDrive Audio 1 (220)", next to ESS's "ESS AudioDrive Playback (220)". A program picks it like any wave device, and the Multimedia control panel lists it.
* **`SharedWaveOut=1`: device 0 shares.** A program that opens device 0 while another one plays there gets Audio 1 instead of `MMSYSERR_ALLOCATED`. Programs that use the wave mapper get the same from `Audio1Device`.

### Who gets Audio 1

Audio 1 records or plays, one at a time, first come first served:
* The player takes it as ESS's wave-in does (`wid_acquire`): not while ES1869.VXD gave the chip away, not while it records, and it holds the DSP through the VxD (0002) like ESS's devices.
* While it plays, a recording is refused with `MMSYSERR_ALLOCATED`, as ESS's wave-in refuses a second recording. While a recording runs, the player's open is refused.
* A voice recording (see above) gives way to it, as it does for wave-out, and goes on at its close unless wave-out still plays.
* Sound Recorder and other full-duplex programs record on Audio 1 and play on Audio 2 as before, as long as nothing plays on device 1.

### How it plays

The code: [`src/es1869/a1wave.asm`](../src/es1869/a1wave.asm) (the wave messages, the chip set up at task time) and [`src/es1869/a1play.asm`](../src/es1869/a1play.asm) (the interrupt, fixed code).
* **Open.** The format as ESS's device 0 takes it: PCM, 8 or 16 bits, mono or stereo, 4000 to 49000 Hz. Then extended mode (C6h, without the data sheet's reset: it would stop Audio 2 too), the rate and filter clock through ESS's 6:1FF4 with its playback table, A8h, B9h as ESS records, B6h and B7h, B1h and B2h, 71h bit 2, and D1h.
* **The ring.** The DMA plays two blocks of the Audio 1 DMA buffer, auto-initialize, and the chip interrupts once a block. The block is ESS's recording block, 1/16 s, at most half the buffer.
* **The interrupt.** ESS's handler calls the player through `isr_srv_table` slot 1, as the player is Audio 1's user 1. The player reads the DMA position (VxD 0004), gives back the headers the DMA played past (`WOM_DONE`), and copies what's queued into the free part of the ring.
* **Writes while it plays** go into the ring at once, as ESS's write does for Audio 2 (6:2FC1), 2 KB at a time with interrupts off.
* **No data.** The player writes silence and the DMA keeps running: stopping takes DSP commands, which ESS's driver never sends at interrupt time. The next data goes 1/128 s ahead of the DMA, and the position leaves the silence out.
* **Position.** `WODM_GETPOS` in bytes, or in samples for any other type, as ESS's device 0. It counts what the DMA took, so a buffer comes back when it has played, not when it was copied.
* **Loops** (`WHDR_BEGINLOOP`, `WHDR_ENDLOOP`, `dwLoops`) and `WODM_BREAKLOOP`, pause (B8h bit 0), restart and reset.
* **Close.** The DMA stops, D3h, 71h bit 2 back, the DSP released.
* **Volume.** `waveOutSetVolume` on device 1 sets mixer 14h, the Audio 1 play volume, 4 bits a side. ESS's driver writes "Telegaming Vol" there at enable and resume, and Windows' mixer doesn't show it.
* **The Audio 1 DAC stays on.** ESS's Audio 2 start sends D3h through `a1_d3_gate`, which leaves it out while the player is open.
* **APM resume.** ESS's resume path (3:4D1B) calls the player after ESS's wave-in: it sets the chip up again and plays on, without what the ring held.
* **The last disable** stops the player's DMA with ESS's recording (3:4E20), before ESS frees its interrupt handler.

### Its limits

* Audio 1 on an 8-bit DMA channel (0, 1 or 3), and not in the VxD's no-DMA mode. Otherwise `WODM_GETNUMDEVS` stays 1.
* `Audio1Filter=1` uses the CODEC's switched-capacitor filter. By default it's bypassed (71h bit 2), as the Audio 2 DAC's is.
* DirectSound can't start while the player holds the DSP, as with ESS's wave-out.

## Programming the Audio 1 DAC

The data sheet's steps (DS p.48-49), and where the field-tested drivers differ:

| Step | Data sheet | Linux `es18xx` (playback2) | `build/ES1869.DRV` |
|---|---|---|---|
| Reset | Audio_Base+6 = 3, which also zeroes 7Ch (p.51) | FIFO reset only (+6 = 2, then 0) | FIFO reset only: a full reset would silence Audio 2 |
| Direction | B8h bit 3 = 0 (DAC), bit 2 = 1 (auto-initialize) | B8h = 05h at the start | B8h bits 3:0 = 0100b, then bit 0 |
| Channels | A8h bits 1:0: 10 mono, 01 stereo | the same | the same, read-modify-write |
| Transfers | B9h: 00 single, 01 or 11 demand | B9h = 2 once at init | ESS's recording choice: 02h unless "Single Mode DMA" |
| Rate, filter | A1h, A2h | the same | ESS's rate routine (6:1FF4) with its playback filter table |
| Format | Table 16: B6h = 80h and B7h bit 5 clear for signed | B6h = 00h and B7h bit 5 set for signed | as Linux, which matches ESS's recording (B7h bit 5 set for 16-bit) |
| Interrupt, DRQ | B1h, B2h bits 6 and 4 | 50h at init | ORed with 50h, as ESS's recording |
| Mixer input | D1h 100 ms after the start, D3h 25 ms after the stop | D1h once at init | D1h at the open, D3h at the close |
| Stop | B8h bit 0 clear | B8h = 00h | B8h bits 3:0 clear, the channel masked, the FIFO reset |

*Note: the data sheet's Table 16 has the signed and unsigned columns the wrong way round. Windows' 8-bit samples are unsigned and its 16-bit samples signed: B6h = 80h, B7h = 51h then D0h (mono) or 98h (stereo) for 8-bit; B6h = 00h, B7h = 71h then F4h or BCh for 16-bit.*
