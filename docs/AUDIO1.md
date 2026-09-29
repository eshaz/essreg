# Audio 1: the ES1869's second DAC

This page describes what the ES1869's first audio channel can do, what ESS's drivers use it for, and how `build/ES1869.DRV` plays sound through it.

Page numbers (DS p.NN) refer to the ES1869 data sheet in `docs/datasheet/`. Addresses such as `6:2D75` are segment and offset in `ES1869.DRV`, written as in [DRIVER_CONFIG.md](DRIVER_CONFIG.md). Registers go by the names of the catalog that essctl shows ([REGISTERS.md](REGISTERS.md)).

## Two channels, three converters

The ES1869 has two DMA audio channels (DS p.19-20, Figure 10).

Audio 1 drives the CODEC, a stereo ADC and a stereo DAC that share one sample clock (controller register A1h) and one filter (A2h). The CODEC works in one direction at a time, which controller register B8h bit 3 selects, and its FIFO holds 256 bytes. The DAC reaches the mixer at the Audio 1 play volume, mixer register 14h, from the moment DSP command D1h switches it on until D3h switches it off.

Audio 2 is a second stereo DAC with its own clock (mixer register 70h), its own filter (72h), a 64-byte FIFO and the play volume 7Ch.

Both DACs play into the same mixer. When mixer register 71h bit 1 is clear, Audio 2 runs from Audio 1's clock and filter.

## What ESS's drivers do with it

ESS's `ES1869.DRV` records on Audio 1 and plays on Audio 2. Wave input takes Audio 1 as its "user 2" (`wid_acquire`, 4:00E0) and wave output takes Audio 2 (`wod_acquire`, 4:0054), so nothing ever plays through the Audio 1 DAC. Each time Audio 2 starts, the driver even switches the Audio 1 DAC off with D3h (6:2D75).

Parts of the driver are nevertheless prepared for playback on Audio 1, although nothing uses them:
* The interrupt handler, which is the `ISR_Stub` of the DDK sample ([RE_NOTES.md](RE_NOTES.md)), dispatches an Audio 1 interrupt to `isr_srv_table[(user − 1) & 3]`. The table (7:00AE) holds `isr_record` for users 1 and 2, and `isr_play`, the Audio 2 routine, for users 3 and 4. ESS's code only ever uses user 2, so the slot for user 1 is free. The slots for users 3 and 4 are not, because Audio 2's dispatch starts at offset 8 of the same table.
* At enable, 3:49DB stores a playback DMA mode for Audio 1 in the device structure: 58h plus the channel number, for single transfers, auto-initialize and memory to device. It sits next to the recording mode 54h, and only the recording mode is ever used.
* The rate routine at 6:1FF4, which sets A1h and A2h for Audio 1, has a table for playback. With its flag at 0, it takes the filter clock from that second table (DGROUP 4Eh) instead of the recording table (60h). ESS's code only calls it for recording.

The driver also lets one kind of recording give way to playback. It keeps a "voice" wave input client apart from the others (device structure +78h), stops it when wave output opens (6:0470), and starts it again when wave output closes (6:0566).

### Telegaming

Audio 1's playback path was built around the data sheet's telegaming mode (DS p.19-20). In that design, a modem or speakerphone DSP connects to the ES1869's serial port through the SE, DCLK, DX, DR, FSX and FSR pins. While the serial port is enabled, by mixer register 48h bit 7 or by the SE pin, the Audio 1 CODEC works for that DSP, with its ADC and DAC connected through the serializer. Telegaming mode (48h bit 1) then sends Audio 1's DMA stream, which carries a game's Sound Blaster sound, to the Audio 2 DAC at the Audio 1 volume (14h). The game stays audible while the modem has the CODEC.

ESS's Windows driver implements only the volume: at enable and resume it writes the registry value "Telegaming Vol" to 14h (6:1FE8). Neither driver reads the INF's "Telegaming" value, and nothing writes 48h bit 7 or bit 1. On a card without a serial DSP, the Audio 1 DAC is therefore free whenever nothing is recording.

## The Windows side

The Windows 95 DDK describes how a wave driver serves programs (`DESGUIDE\MMEDIA.DOC`, "Opening and Closing Devices", and the MSSNDSYS sample). A driver answers `WODM_GETNUMDEVS` with the number of its devices for each devnode, and every later message carries a device number. There are two ways to serve more than one program: a second device ID "for routing to different playback hardware", with its own name in `WAVEOUTCAPS`, or a single device that accepts several `WODM_OPEN`s. When the hardware is busy, `WODM_OPEN` returns `MMSYSERR_ALLOCATED`, and the program or the wave mapper tries another device.

ESS's `wodMessage` (6:18E4) answers only for device 0 and returns `MMSYSERR_BADDEVICEID` for any other, and it answers `WODM_GETNUMDEVS` with 1 for an enabled devnode. DirectSound doesn't go through `wodMessage` at all. `ES1869.VXD` plays it on Audio 2, and it can't take the chip while a wave device holds the DSP (o1:125C checks the VxD's owner).

## The Audio 1 player in build/ES1869.DRV

`build/ES1869.DRV` plays a second wave output stream through the Audio 1 DAC while Audio 2 plays the first. Two keys in the `[ES1869.DRV]` section of `SYSTEM.INI` control it, and both are on by default ([DRIVER_CONFIG.md](DRIVER_CONFIG.md#62-es1869drv)):
* `Audio1Device=1` adds a second device, "ESS AudioDrive Audio 1 (220)", next to ESS's "ESS AudioDrive Playback (220)". Programs choose it like any other wave device, and the Multimedia control panel lists it.
* `SharedWaveOut=1` lets device 0 share. A program that opens device 0 while another program is playing there gets Audio 1 instead of `MMSYSERR_ALLOCATED`. Programs that go through the wave mapper get the same result from `Audio1Device` alone, because the mapper tries device 1 when device 0 is busy.

### Who gets Audio 1

Audio 1 either records or plays, never both, and whichever starts first keeps it. The player takes the channel the way ESS's wave input does (`wid_acquire`). It can't while `ES1869.VXD` has given the chip to DirectSound or a DOS program, or while Audio 1 is recording, and once it has the channel, it holds the DSP through the VxD (function 0002), as ESS's devices do.

While the player is playing, a program that tries to record gets `MMSYSERR_ALLOCATED`, in the same way that ESS's wave input refuses a second recording. While a recording runs, the player refuses to open. A voice recording is the exception: it gives way to the player as it does to wave output, and it resumes when the player closes unless wave output is still playing. Sound Recorder and other full-duplex programs still record on Audio 1 and play on Audio 2, as long as nothing is playing on device 1.

essctl's *Device information* page shows who has each channel, for example "Audio 1: records" or "Audio 2: dual playback", along with the settings the driver read. It takes both from the driver's data segment.

### How it plays

The player's code is in [`src/es1869/a1wave.asm`](../src/es1869/a1wave.asm), which handles the wave messages and sets up the chip at task time, and [`src/es1869/a1play.asm`](../src/es1869/a1play.asm), which runs at interrupt time from fixed code.

When a program opens the device, the player accepts the formats that ESS's device 0 accepts: PCM with 8 or 16 bits, mono or stereo, at 4000 to 49000 Hz. It then puts the chip in extended mode with command C6h. It leaves out the reset that the data sheet calls for first, because that reset would stop Audio 2 as well. The sample rate and filter clock are set through ESS's rate routine (6:1FF4) with its playback table, and the player then writes A8h, B9h (as ESS's recording sets it), B6h and B7h, B1h and B2h, and mixer register 71h bit 2, and sends DSP command D1h.

The DMA plays a ring of two blocks in the Audio 1 DMA buffer, in auto-initialize mode, and the chip interrupts once per block. A block is as long as ESS's recording block, 1/16 s, and never more than half the buffer. ESS's interrupt handler calls the player through the slot for user 1 in `isr_srv_table`, since the player is Audio 1's user 1. The player reads the DMA position (VxD function 0004), returns the headers that the DMA has played past with `WOM_DONE`, and copies queued data into the free part of the ring. A buffer that a program writes while the device is playing goes into the ring at once, as ESS's write does for Audio 2 (6:2FC1), in pieces of 2 KB with interrupts off.

When the program runs out of data, the player writes silence and lets the DMA run on. Stopping the DMA would take DSP commands, and ESS's driver never sends those at interrupt time. New data then goes in 1/128 s ahead of the DMA, and the position that the program reads doesn't count the silence. `WODM_GETPOS` reports the position in bytes, or in samples for any other time format, as ESS's device 0 does. It counts what the DMA has taken, so a buffer comes back once it has been played rather than once it has been copied.

The player supports loops (`WHDR_BEGINLOOP`, `WHDR_ENDLOOP` and `dwLoops`) and `WODM_BREAKLOOP`, as well as pause (B8h bit 0), restart and reset. `waveOutSetVolume` on device 1 sets mixer register 14h, the Audio 1 play volume, with 4 bits for each side. ESS's driver writes "Telegaming Vol" to the same register at enable and resume, and Windows' mixer doesn't show it.

While the device is open, the D3h that ESS's code sends at every Audio 2 start goes through `a1_d3_gate`, which leaves it out, so the Audio 1 DAC stays on. When the program closes the device, the player stops the DMA, sends D3h, clears 71h bit 2 as ESS's driver leaves it, and releases the DSP.

The player also handles the driver's power events. On an APM resume, ESS's resume path (3:4D1B) calls the player after ESS's wave input, and the player sets the chip up again and plays on, without what the ring held. At the last disable, the player's DMA stops together with ESS's recording (3:4E20), before ESS frees its interrupt handler.

### Limits

* Audio 1 has to be on an 8-bit DMA channel (0, 1 or 3), and the VxD must not be in its no-DMA mode. Otherwise `WODM_GETNUMDEVS` stays at 1. For dual playback, Audio 2 may use any channel, including the 16-bit channel 5, because the player programs it as ESS's playback does.
* The CODEC's switched-capacitor filter is bypassed by default (71h bit 2), as the Audio 2 DAC's filter is. `Audio1Filter=1` puts it to use.
* DirectSound can't start while the player holds the DSP, which is also true of ESS's wave output.

## Dual playback

With `DualPlayback=1`, the default, device 1 also accepts a 4-channel stream. Channels 1 and 2 play on the Audio 1 DAC and channels 3 and 4 on the Audio 2 DAC, and both DACs run from Audio 1's clock.

To get one clock, the player takes Audio 2 as well, as its user 2, and clears mixer register 71h bit 1, which makes the Audio 2 DAC run at Audio 1's sample rate and filter clock (DS p.64). Both DACs then convert each frame at the same tick. The player turns 4x oversampling off and sets Audio 2's filter the same way as Audio 1's (`Audio1Filter`). When the device closes, it sets 71h bit 1 again, and Audio 2 returns to its own rate.

The two DMAs play rings of the same size and start together. Audio 2's DMA first fills Audio 2's FIFO while 78h bit 0 is clear. Then, with interrupts off, Audio 1 starts (B8h bit 0) and Audio 2's FIFO is connected to its DAC (78h bit 0) immediately afterwards. Audio 1's interrupt refills both rings, and a pause stops both DMAs and later restarts both from where they stopped.

A program writes the stream to device 1 as 4-channel PCM with 8 or 16 bits, at 4000 to 49000 Hz. Programs that use the wave mapper, such as Media Player, get device 1 for such a stream when it is the preferred playback device (Control Panel > *Multimedia*), and usually also when the first device is preferred, because the first device refuses 4 channels. Dual playback is refused while Audio 2 is playing, for wave output or DirectSound, and while Audio 1 is recording. While it runs, both channels are busy.

`tools/dualwav.py` makes 4-channel files from a WAV file, or from a tone, a sweep or noise that it generates.

### What two DACs can do together

The mixer adds the outputs of the two DACs, each after its own volume (14h and 7Ch). It can neither multiply the signals nor shift their frequencies, so everything a pair of signals can do comes down to their sum:

| `dualwav.py` mode | Audio 1 plays | Audio 2 plays | The sum | Worth hearing for |
|---|---|---|---|---|
| `same` | x | x | 2x, 6 dB up, while the noise of the two DACs adds up to 3 dB more | 3 dB less DAC noise, once both volumes are 6 dB lower |
| `invert` | x | −x | only what differs between the two DACs: level, DC offset, filter and timing | the null test, for lining the DACs up with `--delay2` and `--gain2` until the tone is quietest |
| `split` | x + d | x − d | 2x, with d (a 2 Hz triangle wave) cancelled | each DAC plays different codes, so their code-by-code errors don't add up alike |
| `half` | x | x half a sample later | x through a two-tap filter that falls to zero at half the sample rate | a gentle treble roll-off, as a digital filter would give |
| `hilbert` | x | x shifted by 90° | x's spectrum 3 dB up, with every frequency shifted by 45° | the same sound, since the sum is an all-pass filter. A 90° pair gives a frequency shift only with multipliers, which the mixer doesn't have |

Because the two DACs share one clock, their outputs change at the same instants, so the images above half the sample rate can't cancel each other. Cancelling them would take a second DAC that changes half a sample period later, and the chip has no such clock. A pair of signals half a sample apart filters the samples instead, as any digital filter would.

The two DACs' paths may differ by a fraction of a sample, and their starts by a few microseconds. To line them up, play an `invert` file of a tone and delay whichever DAC is ahead with `--delay1` or `--delay2`, in steps of 0.25 frame, then adjust `--gain2` in steps of 0.1 dB, keeping the values that make the tone quietest. Use those values for every file you make afterwards. A recording of the output ([AUDIO_PIPELINE.md](AUDIO_PIPELINE.md#measuring-the-dac-on-the-card)) measures the null more accurately than listening does.

## Programming the Audio 1 DAC

The table lists the data sheet's steps for playing sound on Audio 1 (DS p.48-49), and shows where drivers that have been tested in the field differ from them.

| Step | Data sheet | Linux `es18xx` (playback2) | `build/ES1869.DRV` |
|---|---|---|---|
| Reset | Audio_Base+6 = 3, which also zeroes 7Ch (p.51) | a FIFO reset only (+6 = 2, then 0) | a FIFO reset only, because a full reset would silence Audio 2 |
| Direction | B8h bit 3 = 0 (DAC), bit 2 = 1 (auto-initialize) | B8h = 05h at the start | B8h bits 3:0 = 0100b, then bit 0 |
| Channels | A8h bits 1:0: 10 for mono, 01 for stereo | the same | the same, read-modify-write |
| Transfers | B9h: 00 for single, 01 or 11 for demand | B9h = 2, once at initialization | as ESS's recording: 02h unless "Single Mode DMA" is set |
| Rate, filter | A1h, A2h | the same | ESS's rate routine (6:1FF4) with its playback filter table |
| Format | Table 16: B6h = 80h and B7h bit 5 clear for signed samples | B6h = 00h and B7h bit 5 set for signed samples | as Linux, which matches ESS's recording (B7h bit 5 set for 16-bit) |
| Interrupt, DRQ | B1h, B2h bits 6 and 4 | 50h at initialization | ORed with 50h, as in ESS's recording |
| Mixer input | D1h 100 ms after the start, D3h 25 ms after the stop | D1h once at initialization | D1h at the open, D3h at the close |
| Stop | B8h bit 0 clear | B8h = 00h | B8h bits 3:0 cleared, the channel masked and the FIFO reset |

The signed and unsigned columns of the data sheet's Table 16 are the wrong way round. Windows' 8-bit samples are unsigned and its 16-bit samples are signed, so 8-bit playback takes B6h = 80h and B7h = 51h, then D0h (mono) or 98h (stereo), and 16-bit playback takes B6h = 00h and B7h = 71h, then F4h (mono) or BCh (stereo).
