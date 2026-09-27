# ESFM MIDI: hanging notes

Notes sometimes hang on the ESFM synthesizer when the music gets busy, in MIDI files and in games.

**Short version:**
* `ESFM.DRV`, ESS's MIDI driver for the synthesizer, drops a MIDI message that comes in while it's still busy with the previous one.
* A dropped note off leaves the note sounding.
* [`build/ESFM.DRV`](../build) is the same driver with that fixed.

## The driver stack

| Layer | What it does here |
|---|---|
| Program | Media Player (MCI sequencer), a game: `midiOutShortMsg`, `midiOutLongMsg`, `midiStreamOut` |
| `MMSYSTEM.DLL` | Passes the calls to the driver. Its timers and its stream player send MIDI data **from interrupts** |
| `MIDIMAP.DRV` | The MIDI Mapper: passes each message on to the chosen device |
| **`ESFM.DRV`** | `modMessage`: voices, patches, FM register writes. Its code is in a fixed segment, so it can be called at interrupt time |
| `ES1869.VXD` | Gives FM to Windows when the MIDI device opens and takes it back at close (API 0101/0102/0103); DOS box contention. **Not involved while Windows plays** |
| ES1869 | The ESFM synthesizer in native mode: 18 voices, registers written through FM_Base+1/+2/+3 |

* The source of `ESFM.DRV` is in [`src/esfm`](../src/esfm), rebuilt from the ESS driver the same way as `ES1869.VXD`.
* The addresses below are `segment:offset` in `ESFM.DRV`.

## What goes wrong

### 1. Messages dropped while the driver is busy (the main cause)

**The busy counter.**
* `modMessage` (seg1:1442) guards MODM_DATA, MODM_LONGDATA and MODM_RESET with a single counter (DGROUP:0022; seg1:1506, 1554, 1729).
* A message that comes in while another one is being handled gets `MIDIERR_NOTREADY`.
* MMSYSTEM, the MIDI Mapper and the programs don't try again, so the message is gone.

**How a second message gets in while the driver is busy:**
* **From an interrupt.** MMSYSTEM's timers and its stream player send MIDI data at interrupt time, so they can land in the middle of a program's own `midiOutShortMsg`. Games often change volume or pan this way while their music plays.
* **From a callback.** After a long message (SysEx), the driver calls the program back (MOM_DONE, seg1:1711) while it still holds the counter. Anything the program sends from that callback is refused.

**Why busy music makes it worse:**
* A note on for a two-voice patch is about 70 FM register writes, well over a millisecond.
* The more notes per second, the more often two messages overlap.

A dropped note off leaves the voice keyed on until it's stolen for another note or the device is reset. That's the hanging note.

### 2. Close or power suspend with the sustain pedal down

* `all_notes_off` (seg1:011C) runs at close and at power suspend, and sends a note off to every voice.
* While a channel's sustain pedal is down, a note off only marks the voice. So those voices keep sounding after the program has closed the device.
* They stay on until the next program opens it.

### 3. FM register writes split by an interrupt

* A register write is three port writes: address low, address high, data (`fm_write`, seg1:0010).
* Open, close, power suspend and resume write registers without taking the busy counter.
* So a message from an interrupt can land between the address and the data, and the data goes to the wrong register.
* In the emulator this wrote register 24Dh instead of 24Eh and left voices keyed on after the close.

### 4. A sustain pedal left down (found on the hardware)

* **What the dump showed.** `essctl /dump` during music on Windows 98: channel 6's pedal down, and 12 voices *held by pedal*, on in the chip. All 12 were keyed 650 to 720 note ons earlier, in one burst, and channel 6 played nothing after them. Nothing was queued or refused, so this isn't cause 1.
* **ESS's pedal follows the MIDI spec.** Controller 64 at 64 or more puts it down, below 64 lets it up (`sustain`, seg1:0E46). A note off while it's down keeps the voice keyed on.
* **Nothing else lets it up.** Only controller 64, controller 121, MODM_RESET, open and close reset the pedal.
  * A program change only stores the program (seg1:13C0).
  * The long-message parser skips every SysEx byte (seg1:15BF), so a GM, GS or XG reset does nothing.
* **FM doesn't fade.** On a sample synth, a piano note held by a forgotten pedal decays. An ESFM patch with a sustaining envelope sounds until the pedal goes up.
  * So a pedal left down from an earlier part holds every note of the channel after it, forever, and those notes use up voices.

### Checked and fine

* **Driver logic:** voice allocation, voice stealing, retriggering a note that's already playing, controller 64 itself, the controllers (including 120, 121 and 123-127), RPN pitch bend range, running status, the long-message parser apart from SysEx (see 4), and MODM_RESET, which silences everything.
* **The chip:** [ESFMu](https://github.com/Kagamiin/ESFMu), the hardware-accurate ESFM emulator, releases a key off that comes during an envelope delay right away. The chip doesn't hold notes on its own.
* **`ES1869.VXD`:** once Windows owns FM, the VxD doesn't trap the FM ports at all.
* **Not in the repository:** the Microsoft parts (`MMSYSTEM.DLL`, `MIDIMAP.DRV`, `MCISEQ.DRV`) are described here from their documented behaviour.

## Proof

[`tests/esfmemu.py`](../tests/esfmemu.py) runs the real `ESFM.DRV` code in a 16-bit CPU emulator:
* it goes through DRVM_INIT, DRVM_ENABLE and MODM_OPEN like Windows does
* a model of the FM chip records every register write
* it can fire an "interrupt" (a nested `modMessage` call) after any FM write or any instruction

[`tests/test_esfmdrv.py`](../tests/test_esfmdrv.py) checks each case with ESS's driver and with the fixed one:

| Case | ESS `ESFM.DRV` | `build/ESFM.DRV` |
|---|---|---|
| Note off from an interrupt during a note on | refused, the note hangs | played |
| Note off sent from the MOM_DONE callback | refused, the note hangs | played |
| Note on from an interrupt during MODM_CLOSE | wrong register written, voices on after the close | silent |
| Close with the sustain pedal down | voices on after the close | silent |
| Power suspend with the pedal down | voices on | silent |
| Note off fired at every 11th instruction of a note on (937 tries) | 914 hanging notes | 0 |
| Pedal down, a chord let go, then a program change | notes held, pedal down | released, pedal up |
| Pedals down on two channels, then a GM, GM2, GS or XG reset | notes held | released |

## The fix: `build/ESFM.DRV`

* Built with `python3 tools/build_esfm.py` from [`src/esfm`](../src/esfm).
* `--stock --verify` builds ESS's driver instead and checks that it's identical byte for byte.
* The changes are in [`src/esfm/esfmfix.asm`](../src/esfm/esfmfix.asm):
  * **Queue instead of refusing.** A message that comes in while the driver is busy is queued (64 entries).
  * **Draining.** The call that's busy handles the queue, in order, before it returns. `MIDIERR_NOTREADY` only happens if the queue is full.
  * **Long messages** wait in the queue marked as queued, and get their MOM_DONE when they're played.
  * **Open, close and `chip_reset`** hold the driver too, so nothing can come in between the three port writes of a register. Queued messages of a program that has just closed the device are dropped.
  * **Close and power suspend** key off every voice and lift every sustain pedal.
  * **Counters.** A small block of counters at the end of the data segment, starting with `ESFMFIX`, for essctl.
* **Sustain pedal.** [`src/esfm/esfmped.asm`](../src/esfm/esfmped.asm):
  * A program change lets go of the channel's pedal first, as controller 64 with 0 would. The MIDI spec keeps it down, but a pedal still down when a channel changes instrument is left over from the part before, and on FM it holds notes forever.
  * A GM, GM2, GS or XG reset in a long message resets the controllers of every channel (121, the pedal up) and turns their notes off (123), as GM synths do.
* **Bank file.** [`src/esfm/esfmfile.asm`](../src/esfm/esfmfile.asm) lets the driver play a patch bank straight from a file named in `SYSTEM.INI`. It reads the file when a program opens the device, if the file's date or time changed. See [ESFM_BANK.md](ESFM_BANK.md#bank-file-buildesfmdrv).

Everything else is ESS's code, unchanged:
* The bank loader is untouched, so `esfmpat` and essctl's *Load bank* work the same with the fixed driver.
* The built-in patch bank is [`esfm_patch_banks/bnk_com_better_square_wave.bin`](../esfm_patch_banks). `--stock` keeps ESS's `bnk_com.bin`, and `--bank FILE` builds the driver with another bank.

### Installing

`ESFM.DRV` is in use while Windows runs, so copy it from DOS:

1. Start > Shut Down > *Restart the computer in MS-DOS mode*.
2. `copy C:\WINDOWS\SYSTEM\ESFM.DRV C:\WINDOWS\SYSTEM\ESFM.ORG`
3. `copy build\ESFM.DRV C:\WINDOWS\SYSTEM\ESFM.DRV`
4. Type `exit` to go back to Windows.

To go back, copy `ESFM.ORG` over `ESFM.DRV` the same way.

## Checking your machine

**The *ESFM patch bank* page in essctl** shows the driver's 18 voices live: channel, note, and state (*playing*, *held by pedal*, *released*).
* **Chip column.** While a program has the MIDI device open, the Chip column reads the key-on bit back from the synthesizer.
* **Stuck notes.** A voice that's `on` in the chip while the driver has it as free is marked **STUCK**. A voice that keeps *playing* after the music has stopped is a hanging note too.
* **With the fixed driver:** the page also counts the messages that came in while it was busy. ESS's driver would have dropped every one of them.
* **In a file:** `essctl /dump file` writes the same table.

**Stress test** (button on the same page):
* **What it plays.** About 7 seconds of dense music through MMSYSTEM's stream player, which sends it at interrupt time like the MCI sequencer does. At the same time essctl sends volume and expression changes to the same device.
* **What it checks.** Every note has its note off, so no voice should be left sounding at the end.
* **Expected with ESS's driver:** some voices left sounding.
* **Expected with `build/ESFM.DRV`:** none, and a count of the queued messages.
* **Before you run it:** stop other MIDI playback, because only one program can have the ESFM device open.
