# ESFM.DRV and General MIDI

What General MIDI (GM Level 1) asks of a synthesizer, what ESS's `ESFM.DRV` does, and what [`build/ESFM.DRV`](../build) adds.

* The code is in [`src/esfm/esfmgm.asm`](../src/esfm/esfmgm.asm), assembled into the fixed driver only.
* ESS's code is unchanged, apart from the calls into it (`%if ESFM_FIX` in [`src/esfm/seg1.asm`](../src/esfm/seg1.asm)).

## At a glance

| GM Level 1 | ESS `ESFM.DRV` | `build/ESFM.DRV` |
|---|---|---|
| 16 channels, drums on channel 10 | yes | yes |
| 128 programs, drum notes 35-81 | from the patch bank | from the patch bank |
| 24 voices | 18, the chip's limit | 18, the chip's limit |
| Note on with velocity, note off | yes | yes |
| Pitch bend, 2 semitones by default | yes | yes |
| Program change | yes | yes |
| Controller 1, modulation | ignored | vibrato |
| Controllers 7 and 11, volume and expression | yes | yes |
| Controller 10, pan | the next notes, 3 positions | the notes that sound too, 3 positions |
| Controller 64, sustain | yes | yes, and a program change lets it up |
| Controller 121, reset all controllers | also resets volume, pan and the bend range | as RP-015 |
| Controller 123, all notes off | yes | yes |
| RPN 0, pitch bend range | semitones only | semitones and cents |
| RPN 1, fine tuning | ignored | yes |
| RPN 2, coarse tuning | ignored | yes, not on channel 10 |
| Channel pressure | ignored | vibrato |
| GM System On (and GM2, GS and XG resets) | ignored | every channel back to the defaults |
| Master volume, `F0 7F dd 04 01 ll mm F7` | ignored | turns every voice down |
| Running status | short messages only | long messages too |

## Modulation and channel pressure

* The chip has a vibrato of its own: about 6 Hz, 7 or 14 cents deep, switched on for each operator.
* Controller 1 or channel pressure, whichever is larger, turns it on for the channel:
  * 0: off, only the patch's own vibrato
  * 1-63: the shallow depth, 7 cents
  * 64-127: the deep one, 14 cents
* It's switched on for all 4 operators of the channel's voices, so the pitch wobbles and the sound stays the same. Notes that already sound follow.
* *Note: the chip has only these two depths, so the vibrato doesn't grow smoothly with the wheel.*

## Tuning and the bend range

* **RPN 0, pitch bend range:** controller 6 sets the semitones, controller 38 the cents. Controller 6 also sets the cents back to 0, as the MIDI spec says for a data entry MSB.
* **RPN 1, fine tuning:** -100 to +100 cents, controllers 6 and 38 (2000h is in tune).
* **RPN 2, coarse tuning:** -64 to +63 semitones, controller 6 (40h is in tune).
  * It changes the next notes, not the ones that sound.
  * Channel 10 plays its drums untuned.
* A new bend range or fine tuning moves the notes that sound, like a bend does.
* Bend and fine tuning together reach 24 semitones either way, as far as ESS's pitch table goes.
* RPN 7F7Fh (null) and any NRPN select nothing: data entry is ignored until the next RPN.
* ESS's driver only had RPN 0 in semitones. A range above 24 semitones read past its pitch table.

## Controller 121

* Resets what RP-015 (MIDI Manufacturers Association) says: expression 127, modulation 0, channel pressure 0, the sustain pedal up, pitch bend centered, no RPN selected.
* Keeps volume, pan, the bend range and the tuning.
* ESS's driver also reset volume, pan and the bend range, and left the notes that sound bent until the next pitch bend.

## GM, GS and XG resets

* GM System On (`F0 7E 7F 09 01 F7`), GM2 System On (`09 03`), the GS reset and XG System On, in a long message, set every channel back to the GM defaults:
  * program 0, volume 100, pan center, expression 127
  * bend range 2 semitones, no tuning, no modulation or pressure, no RPN selected
  * the pedal up, all notes off, master volume full
* `midiOutReset` and opening the device reset the same, apart from the programs, which they keep as ESS's driver does.

## Master volume

* `F0 7F dd 04 01 ll mm F7` (any device number) turns down every voice, the notes that sound too.
* The curve is GM's volume curve, 40 log10(v / 127) dB, in the chip's 0.75 dB steps.
* It turns down the operators that volume and expression turn down. The patch says which ones.
* *Note: it doesn't touch the Windows mixer. `midiOutSetVolume` still sets the ES1869's FM volume, as with ESS's driver.*

## Pan

* The chip sends each operator left, right or both, so pan has 3 positions, as with ESS's driver: 0-47 left, 48-80 both, 81-127 right.
* Controllers 10 (pan) and 8 (balance, the same here) now move the notes that sound too.

## Running status in long messages

* In a buffer sent with `midiOutLongMsg`, ESS's parser kept the data bytes of the last message when running status started the next one, and ORed the new ones in: `90 3C 7F 40 7F` played notes 3Ch and 7Ch.
* A note off sent this way turned off the wrong note, and left the right one hanging ([ESFM_MIDI.md](ESFM_MIDI.md#5-running-status-in-long-messages)).

## Voices

* The chip has 18 voices and GM asks for 24. When they run out, ESS's voice stealing reuses one.
* A patch with two voices (layered) takes two.

## Proof

`GMTest` in [`tests/test_esfmdrv.py`](../tests/test_esfmdrv.py) runs both drivers in the CPU emulator ([ESFM_MIDI.md](ESFM_MIDI.md#proof)) and reads the chip's registers:

| Case | ESS `ESFM.DRV` | `build/ESFM.DRV` |
|---|---|---|
| Controller 1 at 40, then 100, on a note that sounds | no change | vibrato on, then deep |
| Channel pressure | no change | vibrato |
| RPN 1 at 2800h (+25 cents) | no change | the pitch of a bend of 2400h |
| RPN 2 at 4Ch | no change | C4 plays as C5 |
| Bend range 1 semitone 50 cents, bend at the top | 1 semitone up | the pitch of range 3 half way up |
| Controller 121 after volume 50, pan left, range 12 | volume 100, pan center, range 2 | volume 50, pan left, range 12 |
| GM, GM2, GS or XG reset after changes | nothing | every default back, notes off |
| Master volume at 64 | no change | voices 11.25 dB quieter |
| Pan on a note that sounds | no change | moves |
| `90 3C 7F 40 7F` in a long message | notes 3Ch and 7Ch | notes 3Ch and 40h |

## Listening on the hardware

[`build/GMCHECK.MID`](../build), made by [`tools/gmcheck.py`](../tools/gmcheck.py), plays each feature in turn. See [TESTING.md](TESTING.md#g3-general-midi).
