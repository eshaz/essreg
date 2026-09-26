# ESFM MIDI: hanging notes

Notes sometimes hang on the ESFM synthesizer when the music gets busy, in
MIDI files and in games.

**Short version:**
- `ESFM.DRV`, ESS's MIDI driver for the synthesizer, drops a MIDI message that arrives while it is still busy with the previous one.
- A dropped note off leaves the note sounding.
- [`build/ESFM.DRV`](../build) is the same driver with that fixed.

## The driver stack

| Layer | What it does here |
|---|---|
| Program | Media Player (MCI sequencer), a game: `midiOutShortMsg`, `midiOutLongMsg`, `midiStreamOut` |
| `MMSYSTEM.DLL` | Passes the calls to the driver. Its timers and its stream player send MIDI data **from interrupts** |
| `MIDIMAP.DRV` | The MIDI Mapper: passes each message on to the chosen device |
| **`ESFM.DRV`** | `modMessage`: voices, patches, FM register writes. Its code is in a fixed segment, so it can be called at interrupt time |
| `ES1869.VXD` | Gives FM to Windows when the MIDI device opens and takes it back at close (API 0101/0102/0103); DOS box contention. **Not involved while Windows plays** |
| ES1869 | The ESFM synthesizer in native mode: 18 voices, registers written through FM_Base+1/+2/+3 |

The source of `ESFM.DRV` is in [`src/esfm`](../src/esfm), rebuilt from the
ESS driver the same way as `ES1869.VXD`. The addresses below are
`segment:offset` in `ESFM.DRV`.

## What goes wrong

### 1. Messages dropped while the driver is busy (the main cause)

**The busy counter.**
- `modMessage` (seg1:1442) guards MODM_DATA, MODM_LONGDATA and MODM_RESET with a single counter (DGROUP:0022; seg1:1506, 1554, 1729).
- A message that comes in while another one is being handled gets `MIDIERR_NOTREADY`.
- MMSYSTEM, the MIDI Mapper and the programs don't try again, so the message is gone.

**How a second message gets in while the driver is busy:**
- **From an interrupt.** MMSYSTEM's timers and its stream player send MIDI data at interrupt time, so they can land in the middle of a program's own `midiOutShortMsg`. Games often change volume or pan this way while their music plays.
- **From a callback.** After a long message (SysEx), the driver calls the program back (MOM_DONE, seg1:1711) while it still holds the counter. Anything the program sends from that callback is refused.

**Why busy music makes it worse:**
- A note on for a two-voice patch is about 70 FM register writes, well over a millisecond.
- The more notes per second, the more often two messages overlap.

A dropped note off leaves the voice keyed on until it is stolen for another
note or the device is reset: that is the hanging note.

### 2. Close or power suspend with the sustain pedal down

- `all_notes_off` (seg1:011C), which runs at close and at power suspend, sends a note off to every voice.
- While a channel's sustain pedal is down, a note off only marks the voice, so those voices keep sounding after the program has closed the device.
- They stay on until the next program opens it.

### 3. FM register writes split by an interrupt

- A register write is three port writes: address low, address high, data (`fm_write`, seg1:0010).
- Open, close, power suspend and resume write registers without taking the busy counter.
- A message from an interrupt can therefore land between the address and the data, and the data goes to the wrong register.
- In the emulator this wrote register 24Dh instead of 24Eh and left voices keyed on after the close.

### Checked and fine

- **Driver logic:** voice allocation, voice stealing, retriggering a note that is already playing, the sustain pedal, the controllers (including 120, 121 and 123–127), RPN pitch bend range, running status, the SysEx and long-message parser, and MODM_RESET, which silences everything.
- **The chip:** [ESFMu](https://github.com/Kagamiin/ESFMu), the hardware-accurate ESFM emulator, releases a key off that comes during an envelope delay right away. The chip does not hold notes on its own.
- **`ES1869.VXD`:** once Windows owns FM, the VxD doesn't trap the FM ports at all.
- **Not in the repository:** the Microsoft parts (`MMSYSTEM.DLL`, `MIDIMAP.DRV`, `MCISEQ.DRV`) are described here from their documented behaviour.

## Proof

[`tests/esfmemu.py`](../tests/esfmemu.py) runs the real `ESFM.DRV` code in a
16-bit CPU emulator:
- it goes through DRVM_INIT, DRVM_ENABLE and MODM_OPEN like Windows does;
- a model of the FM chip records every register write;
- it can fire an "interrupt" (a nested `modMessage` call) after any FM write or any instruction.

[`tests/test_esfmdrv.py`](../tests/test_esfmdrv.py) checks each case with ESS's
driver and with the fixed one:

| Case | ESS `ESFM.DRV` | `build/ESFM.DRV` |
|---|---|---|
| Note off from an interrupt during a note on | refused, the note hangs | played |
| Note off sent from the MOM_DONE callback | refused, the note hangs | played |
| Note on from an interrupt during MODM_CLOSE | wrong register written, voices on after the close | silent |
| Close with the sustain pedal down | voices on after the close | silent |
| Power suspend with the pedal down | voices on | silent |
| Note off fired at every 11th instruction of a note on (937 tries) | 914 hanging notes | 0 |

## The fix: `build/ESFM.DRV`

Built with `python3 tools/build_esfm.py` from [`src/esfm`](../src/esfm).
`--stock --verify` builds ESS's driver instead and checks that it is identical
byte for byte. The changes are in
[`src/esfm/esfmfix.asm`](../src/esfm/esfmfix.asm):

- **Queue instead of refusing.** A message that comes in while the driver is busy is queued (64 entries).
- **Draining.** The call that is busy handles the queue, in order, before it returns. `MIDIERR_NOTREADY` only happens if the queue is full.
- **Long messages** wait in the queue marked as queued, and get their MOM_DONE when they are played.
- **Open, close and `chip_reset`** hold the driver too, so nothing can come in between the three port writes of a register. Queued messages of a program that has just closed the device are dropped.
- **Close and power suspend** key off every voice and lift every sustain pedal.
- **Counters.** A small block of counters at the end of the data segment, starting with `ESFMFIX`, for essctl.

Everything else is ESS's code, unchanged:
- The bank loader is untouched, so `esfmpat` and essctl's *Load bank* work the same with the fixed driver.
- The patch bank is the stock one. `--bank FILE` builds the driver with another bank.

### Installing

`ESFM.DRV` is in use while Windows runs, so copy it from DOS:

1. Start > Shut Down > *Restart the computer in MS-DOS mode*.
2. `copy C:\WINDOWS\SYSTEM\ESFM.DRV C:\WINDOWS\SYSTEM\ESFM.ORG`
3. `copy build\ESFM.DRV C:\WINDOWS\SYSTEM\ESFM.DRV`
4. Type `exit` to go back to Windows.

To go back, copy `ESFM.ORG` over `ESFM.DRV` the same way.

## Checking your machine

**The ESFM page in essctl** shows the driver's 18 voices live: channel, note,
and state (playing, held by the pedal, released).
- **Chip column.** While a program has the MIDI device open, the Chip column reads the key-on bit back from the synthesizer.
- **Stuck notes.** A voice that is `on` in the chip while the driver has it as free is marked **STUCK**. So is a voice that keeps "playing" after the music has stopped.
- **With the fixed driver:** the page also counts the messages that came in while it was busy. Every one of them would have been dropped by ESS's driver.
- **In a file:** `essctl /dump file` writes the same table.

**ESFM > Stress test:**
- **What it plays.** About 7 seconds of dense music through MMSYSTEM's stream player, which sends it at interrupt time like the MCI sequencer does. At the same time essctl sends volume and expression changes to the same device.
- **What it checks.** Every note has its note off, so no voice should be left sounding at the end.
- **Expected with ESS's driver:** some voices left sounding.
- **Expected with `build/ESFM.DRV`:** none, and a count of the queued messages.
- **Before you run it:** stop other MIDI playback, because only one program can have the ESFM device open.
