# ESFM.DRV and General MIDI

This page describes what General MIDI (GM Level 1) asks of a synthesizer,
what ESS's `ESFM.DRV` does, and what [`build/ESFM.DRV`](../build) adds.

The new code is in [`src/esfm/esfmgm.asm`](../src/esfm/esfmgm.asm), which is
assembled into the fixed driver only. ESS's code is unchanged apart from the
calls into the new code, which are under `%if ESFM_FIX` in
[`src/esfm/seg1.asm`](../src/esfm/seg1.asm). Each part can be turned off in
`SYSTEM.INI`, and ESS's code then handles it as before. The keys are
`Vibrato`, `Tuning`, `ResetControllers`, `LivePan`, `SysEx` and
`RunningStatus` under `[ESFM.DRV]`
([DRIVER_CONFIG.md](DRIVER_CONFIG.md#63-esfmdrv)).

## At a glance

Each item names what GM Level 1 asks for, and then what ESS's `ESFM.DRV` and
`build/ESFM.DRV` do:

* **16 channels, drums on channel 10.** Both drivers: yes.
* **128 programs, drum notes 35-81.** Both drivers: from the patch bank.
* **24 voices.** Both drivers: 18, the chip's limit.
* **Note on with velocity, note off.** Both drivers: yes.
* **Pitch bend, 2 semitones by default.** Both drivers: yes.
* **Program change.** Both drivers: yes.
* **Controller 1, modulation.** ESS's driver: ignored. `build/ESFM.DRV`:
  vibrato.
* **Controllers 7 and 11, volume and expression.** Both drivers: yes.
* **Controller 10, pan.** ESS's driver: the next notes, in 3 positions.
  `build/ESFM.DRV`: the sounding notes too, in 3 positions.
* **Controller 64, sustain.** ESS's driver: yes. `build/ESFM.DRV`: yes, and
  a program change lets it up.
* **Controller 121, reset all controllers.** ESS's driver: also resets
  volume, pan and the bend range. `build/ESFM.DRV`: as RP-015 defines it.
* **Controller 123, all notes off.** Both drivers: yes.
* **RPN 0, pitch bend range.** ESS's driver: semitones only.
  `build/ESFM.DRV`: semitones and cents.
* **RPN 1, fine tuning.** ESS's driver: ignored. `build/ESFM.DRV`: yes.
* **RPN 2, coarse tuning.** ESS's driver: ignored. `build/ESFM.DRV`: yes,
  except on channel 10.
* **Channel pressure.** ESS's driver: ignored. `build/ESFM.DRV`: vibrato.
* **GM System On (and GM2, GS and XG resets).** ESS's driver: ignored.
  `build/ESFM.DRV`: sets every channel back to the defaults.
* **Master volume, `F0 7F dd 04 01 ll mm F7`.** ESS's driver: ignored.
  `build/ESFM.DRV`: turns every voice down.
* **Running status.** ESS's driver: in short messages, lost after a
  real-time byte. `build/ESFM.DRV`: in short and long messages, and across
  buffers.

## Modulation and channel pressure

The chip has a vibrato of its own, about 6 Hz and 7 or 14 cents deep, which
is switched on for each operator. Controller 1 or channel pressure,
whichever is larger, turns it on for a channel:
* At 0 the vibrato is off, and only the patch's own vibrato remains.
* At 1-63 it has the shallow depth, 7 cents.
* At 64-127 it has the deep one, 14 cents.

The driver switches the vibrato on for all 4 operators of the channel's
voices, so the pitch wobbles while the sound stays the same, and notes that
are already sounding follow the change. Because the chip has only these two
depths, the vibrato doesn't grow smoothly as the wheel moves.

## Tuning and the bend range

Three RPNs set the bend range and the tuning:
* RPN 0, the pitch bend range, takes the semitones from controller 6 and the
  cents from controller 38. Controller 6 also sets the cents back to 0, as
  the MIDI spec says for a data entry MSB.
* RPN 1, fine tuning, goes from -100 to +100 cents, with 2000h in tune, and
  is set with controllers 6 and 38.
* RPN 2, coarse tuning, goes from -64 to +63 semitones, with 40h in tune,
  and is set with controller 6. It changes the next notes, not the ones
  already sounding, and channel 10 plays its drums untuned.

A new bend range or fine tuning moves the notes that are sounding, as a bend
does. Bend and fine tuning together reach 24 semitones either way, which is
as far as ESS's pitch table goes. RPN 7F7Fh (null) and any NRPN select
nothing, so data entry is ignored until the next RPN. ESS's driver only
knows RPN 0, in semitones, and a range above 24 semitones makes it read past
its pitch table.

## Controller 121

In the fixed driver, controller 121 resets what RP-015 of the MIDI
Manufacturers Association lists: expression to 127, modulation and channel
pressure to 0, the sustain pedal up, pitch bend centered and no RPN
selected. It keeps the volume, pan, bend range and tuning. ESS's driver also
resets the volume, pan and bend range, and it leaves the notes that are
sounding bent until the next pitch bend.

## GM, GS and XG resets

GM System On (`F0 7E 7F 09 01 F7`), GM2 System On (`09 03`), the GS reset
and XG System On, sent in a long message, set every channel back to the GM
defaults. Each channel gets program 0, volume 100, pan center, expression
127, a bend range of 2 semitones, no tuning, no modulation or pressure and
no RPN selected. The pedals go up, all notes go off and the master volume
goes back to full.

`midiOutReset` and opening the device reset the same things except the
programs, which they keep, as ESS's driver does.

The reset happens before the rest of its buffer plays. Programs send a reset
as a buffer of its own, which is how a SysEx event of a MIDI file is sent,
so the order only matters for a buffer that mixes a reset with notes. While
the device is suspended, ESS's code refuses every message, and a reset or
master volume in a refused message changes nothing either.

## Master volume

The master volume message, `F0 7F dd 04 01 ll mm F7` with any device number,
turns down every voice, including the notes that are already sounding. It
follows GM's volume curve, 40 log10(v / 127) dB, in the chip's 0.75 dB
steps. It turns down the operators that volume and expression turn down, and
the patch says which ones those are.

The master volume doesn't touch the Windows mixer, so `midiOutSetVolume`
still sets the ES1869's FM volume, as it does with ESS's driver.

## Pan

The chip sends each operator to the left, to the right or to both, so pan
has 3 positions, as with ESS's driver: 0-47 pans left, 48-80 to both sides
and 81-127 right. Controllers 10 (pan) and 8 (balance, which does the same
here) also move the notes that are sounding. A value within the position the
channel already has writes nothing, so a pan sweep rewrites the notes 3
times rather than 128.

## Running status

In a buffer sent with `midiOutLongMsg`, ESS's parser keeps the data bytes of
the last message when running status starts the next one, and it ORs the new
data bytes into them, so `90 3C 7F 40 7F` plays notes 3Ch and 7Ch. A note
off sent this way turns off the wrong note and leaves the right one hanging
([ESFM_MIDI.md](ESFM_MIDI.md#5-running-status-in-long-messages)). The fixed
parser starts each message afresh.

A real-time byte (F8h-FFh) sent with `midiOutShortMsg` becomes ESS's running
status, so the next message in running status is lost. In the fixed driver,
a real-time byte leaves the running status alone, and a system common byte
(F0h-F7h) ends it.

ESS's parser also starts each long buffer afresh. The fixed parser goes on
where the last buffer stopped, and a short message's status becomes the
running status of the next buffer, as if all the bytes had come one after
the other
([ESFM_MIDI.md](ESFM_MIDI.md#6-running-status-after-a-real-time-byte-and-between-buffers)).

## Voices

The chip has 18 voices, and GM asks for 24. When they run out, ESS's voice
stealing reuses one. A layered patch with two voices takes two of them.

## Proof

`GMTest` in [`tests/test_esfmdrv.py`](../tests/test_esfmdrv.py) runs both
drivers in the CPU emulator ([ESFM_MIDI.md](ESFM_MIDI.md#proof)) and reads
the chip's registers:

* **Controller 1 at 40, then 100, on a sounding note.** ESS's driver: no
  change. `build/ESFM.DRV`: vibrato on, then deep.
* **Channel pressure.** ESS's driver: no change. `build/ESFM.DRV`: vibrato.
* **RPN 1 at 2800h (+25 cents).** ESS's driver: no change. `build/ESFM.DRV`:
  the pitch of a bend of 2400h.
* **RPN 2 at 4Ch.** ESS's driver: no change. `build/ESFM.DRV`: C4 plays as
  C5.
* **Bend range of 1 semitone 50 cents, bend at the top.** ESS's driver: 1
  semitone up. `build/ESFM.DRV`: the pitch of range 3, bent halfway up.
* **Controller 121 after volume 50, pan left, range 12.** ESS's driver:
  volume 100, pan center, range 2. `build/ESFM.DRV`: volume 50, pan left,
  range 12.
* **GM, GM2, GS or XG reset after changes.** ESS's driver: nothing.
  `build/ESFM.DRV`: every default back, notes off.
* **Master volume at 64.** ESS's driver: no change. `build/ESFM.DRV`: voices
  11.25 dB quieter.
* **Pan on a sounding note.** ESS's driver: no change. `build/ESFM.DRV`: the
  note moves.
* **`90 3C 7F 40 7F` in a long message.** ESS's driver: notes 3Ch and 7Ch.
  `build/ESFM.DRV`: notes 3Ch and 40h.
* **`90 3C`, then `7F 40 7F` in the next long message.** ESS's driver:
  nothing. `build/ESFM.DRV`: notes 3Ch and 40h.

## Listening on the hardware

[`build/GMCHECK.MID`](../build), which
[`tools/gmcheck.py`](../tools/gmcheck.py) makes, plays each feature in turn.
[TESTING.md](TESTING.md#g3-general-midi) lists what to listen for.
