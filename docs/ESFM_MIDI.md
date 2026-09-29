# ESFM MIDI: hanging notes

Notes sometimes hang on the ESFM synthesizer when the music gets busy, in
MIDI files as well as in games. The main cause is `ESFM.DRV`, ESS's MIDI
driver for the synthesizer, which drops a MIDI message that arrives while it
is still busy with the previous one. When the dropped message is a note off,
the note keeps sounding. [`build/ESFM.DRV`](../build) is the same driver
with that fixed.

## The driver stack

These are the layers between a program and the synthesizer:

* **The program** is Media Player (through the MCI sequencer) or a game,
  which calls `midiOutShortMsg`, `midiOutLongMsg` or `midiStreamOut`.
* **`MMSYSTEM.DLL`** passes the calls on to the driver. Its timers and its
  stream player send MIDI data from interrupts.
* **`MIDIMAP.DRV`** is the MIDI Mapper, which passes each message on to the
  chosen device.
* **`ESFM.DRV`** handles the voices, the patches and the FM register writes
  in `modMessage`. The code of `modMessage` is in a fixed segment, so it can
  be called at interrupt time.
* **`ES1869.VXD`** gives FM to Windows when the MIDI device opens and takes
  it back when it closes (API 0101/0102/0103), and it handles contention
  with DOS boxes. It isn't involved while Windows plays.
* **The ES1869** has the ESFM synthesizer, used in native mode, with 18
  voices. Its registers are written through FM_Base+1/+2/+3.

The source of `ESFM.DRV` is in [`src/esfm`](../src/esfm). It was rebuilt
from ESS's driver in the same way as the source of `ES1869.VXD`. The
addresses below are `segment:offset` in `ESFM.DRV`.

## What goes wrong

### 1. Messages dropped while the driver is busy (the main cause)

`modMessage` (seg1:1442) guards `MODM_DATA`, `MODM_LONGDATA` and
`MODM_RESET` (seg1:1506, 1554 and 1729) with a single busy counter
(DGROUP:0022). A message that comes in while another one is being handled
gets `MIDIERR_NOTREADY`, and because MMSYSTEM, the MIDI Mapper and the
programs don't try again, the message is lost.

A second message can arrive while the driver is busy in two ways. The first
is from an interrupt: MMSYSTEM's timers and its stream player send MIDI data
at interrupt time, so a message from them can land in the middle of a
program's own `midiOutShortMsg` call. Games often change the volume or pan
with such calls while their music plays. The second is from a callback:
after a long message (SysEx), the driver calls the program back with
`MOM_DONE` (seg1:1711) while it still holds the counter, so anything the
program sends from that callback is refused.

Busy music makes this worse. A note on for a two-voice patch takes about 70
FM register writes, well over a millisecond, and the more notes play each
second, the more often two messages overlap. A dropped note off leaves the
voice keyed on until it is stolen for another note or the device is reset,
and that is the hanging note.

### 2. Close or power suspend with the sustain pedal down

`all_notes_off` (seg1:011C) runs at close and at power suspend, and it sends
a note off to every voice. While a channel's sustain pedal is down, however,
a note off only marks the voice, so those voices keep sounding after the
program has closed the device. They stay on until the next program opens it.

### 3. FM register writes split by an interrupt

A register write takes three port writes: the low byte of the address, the
high byte and the data (`fm_write`, seg1:0010). Open, close, power suspend
and resume write registers without taking the busy counter, so a message
from an interrupt can land between the address and the data, which then goes
to the wrong register. In the emulator, this wrote register 24Dh instead of
24Eh and left voices keyed on after the close.

### 4. A sustain pedal left down (found on the hardware)

An `essctl /dump` taken during music on Windows 98 showed channel 6's pedal
down and 12 voices *held by pedal*, all of them keyed on in the chip. The 12
voices had been keyed 650 to 720 note ons earlier, in one burst, and channel
6 had played nothing after them. Nothing had been queued or refused, so this
wasn't cause 1.

ESS's pedal follows the MIDI spec. Controller 64 at 64 or more puts it down,
and a value below 64 lets it up (`sustain`, seg1:0E46). A note off while the
pedal is down keeps the voice keyed on, and only controller 64, controller
121, `MODM_RESET`, open and close reset the pedal. A program change only
stores the program (seg1:13C0), and a GM, GS or XG reset does nothing,
because the long-message parser skips every SysEx byte (seg1:15BF).

On a sample synth, a piano note held by a forgotten pedal decays, but FM
doesn't fade: an ESFM patch with a sustaining envelope sounds until the
pedal goes up. A pedal left down from an earlier part therefore holds every
note the channel plays after it, forever, and those notes use up voices.

### 5. Running status in long messages

In a long message (`midiOutLongMsg`), when running status starts the next
message, ESS's parser keeps the bytes of the last message and ORs the new
data bytes into them (seg1:16C7). As a result, `80 3C 00 40 00` turns off
note 3Ch and then note 7Ch instead of 40h, which leaves note 40h hanging.
Short messages (`midiOutShortMsg`) in running status are fine, apart from
cause 6.

### 6. Running status after a real-time byte, and between buffers

A real-time byte sent on its own with `midiOutShortMsg`, such as F8h (clock)
or FEh (active sensing), becomes ESS's running status (seg1:1524), so the
next message sent in running status is lost. Sending `90 3C 7F`, `F8` and
then `3C 00` leaves note 3Ch on.

Each long message also starts afresh (seg1:1452), so a message cut between
two buffers is lost, and running status doesn't carry on into the next
buffer.

### What was ruled out

* In ESS's driver, these work as they should: voice allocation, voice
  stealing, retriggering a playing note, controller 64 itself and the other
  controllers (including 120, 121 and 123-127), the RPN for the pitch bend
  range, running status in short messages apart from cause 6, the
  long-message parser apart from SysEx (cause 4) and running status (causes
  5 and 6), and `MODM_RESET`, which silences everything.
* The chip doesn't hold notes on its own. In
  [ESFMu](https://github.com/Kagamiin/ESFMu), the hardware-accurate ESFM
  emulator, a key off that comes during an envelope delay releases the note
  right away.
* `ES1869.VXD` isn't in the path of the notes, because once Windows owns FM,
  the VxD doesn't trap Windows' accesses to the FM ports. It still traps the
  accesses of DOS boxes ([VXD_INTERNALS.md](VXD_INTERNALS.md#dos-boxes)).

The Microsoft parts (`MMSYSTEM.DLL`, `MIDIMAP.DRV` and `MCISEQ.DRV`) aren't
in the repository, so this page describes them from their documented
behavior.

## Proof

[`tests/esfmemu.py`](../tests/esfmemu.py) runs the real `ESFM.DRV` code in a
16-bit CPU emulator. It takes the driver through `DRVM_INIT`, `DRVM_ENABLE`
and `MODM_OPEN` as Windows does, and a model of the FM chip records every
register write. The emulator can also fire an "interrupt", which is a nested
`modMessage` call, after any FM write or any instruction.

[`tests/test_esfmdrv.py`](../tests/test_esfmdrv.py) checks each case with
ESS's driver and with the fixed one, `build/ESFM.DRV`:

* **Note off from an interrupt during a note on.** ESS's driver: refused, so
  the note hangs. `build/ESFM.DRV`: played.
* **Note off sent from the `MOM_DONE` callback.** ESS's driver: refused, so
  the note hangs. `build/ESFM.DRV`: played.
* **Note on from an interrupt during `MODM_CLOSE`.** ESS's driver: the wrong
  register written, voices on after the close. `build/ESFM.DRV`: silent.
* **Close with the sustain pedal down.** ESS's driver: voices on after the
  close. `build/ESFM.DRV`: silent.
* **Power suspend with the pedal down.** ESS's driver: voices on.
  `build/ESFM.DRV`: silent.
* **Note off fired at every 11th instruction of a note on (937 tries).**
  ESS's driver: 914 hanging notes. `build/ESFM.DRV`: 0.
* **Pedal down, a chord let go, then a program change.** ESS's driver: notes
  held, pedal still down. `build/ESFM.DRV`: notes released, pedal up.
* **Pedals down on two channels, then a GM, GM2, GS or XG reset.** ESS's
  driver: notes held. `build/ESFM.DRV`: released.
* **Two notes, then `80 3C 00 40 00` in a long message.** ESS's driver: note
  40h hangs. `build/ESFM.DRV`: released.
* **A clock byte (F8h) between a note on and its note off in running
  status.** ESS's driver: the note hangs. `build/ESFM.DRV`: released.
* **A note on cut between two long messages.** ESS's driver: lost.
  `build/ESFM.DRV`: played.

## The fix: `build/ESFM.DRV`

`python3 tools/build_esfm.py` builds the fixed driver from
[`src/esfm`](../src/esfm). With `--stock --verify`, it builds ESS's driver
instead and checks that the result is identical to it byte for byte.

The main changes are in [`src/esfm/esfmfix.asm`](../src/esfm/esfmfix.asm). A
message that comes in while the driver is busy goes into a queue instead of
being refused. The queue has 256 entries, because the first tick of a GM
file can hold over 100 messages. The call that is busy handles the queue, in
order, before it returns, so `MIDIERR_NOTREADY` only happens when the queue
is full.

Long messages wait in the queue marked as queued, and they get their
`MOM_DONE` when they are played. A program always gets its buffer back:
* A buffer refused because the queue is full keeps the flags it had.
* A buffer that a close drops comes back with `MOM_DONE` before `MOM_CLOSE`.
* A buffer that ESS's code refuses when its turn comes, because the device
  was suspended in the meantime, comes back with `MOM_DONE`.

Open, close and `chip_reset` hold the driver too, so nothing can come in
between the three port writes of a register. Messages still queued for a
program that has closed the device are dropped. Close and power suspend key
off every voice and lift every sustain pedal. At the end of the data
segment, a small block of counters that starts with `ESFMFIX` is there for
essctl to read.

In [`src/esfm/esfmped.asm`](../src/esfm/esfmped.asm), a program change first
lets go of the channel's sustain pedal, as controller 64 with a value of 0
would. The MIDI spec leaves the pedal down through a program change, but a
pedal that is still down when a channel changes instrument is left over from
the part before, and on FM it holds notes forever. A GM, GM2, GS or XG reset
in a long message sets every channel back to the GM defaults, with the pedal
up and the notes off, as GM synths do
([ESFM_GM.md](ESFM_GM.md#gm-gs-and-xg-resets)).

In long messages, each message starts afresh
([`src/esfm/seg1.asm`](../src/esfm/seg1.asm), at seg1:16C7), so no note gets
mixed up with the one before it.
[`src/esfm/esfmgm.asm`](../src/esfm/esfmgm.asm) handles running status
between messages and between buffers. A real-time byte leaves the running
status alone, and a system common byte (F0h-F7h) ends it, as the MIDI spec
says. The long-message parser goes on in the next buffer where the last one
stopped, so a message cut between two buffers plays and running status
carries on. A short message's status also becomes the running status of the
next buffer, as if all the bytes had come one after the other, while
`midiOutReset` and opening the device start afresh.

[`src/esfm/esfmgm.asm`](../src/esfm/esfmgm.asm) also adds what General MIDI
asks for and ESS's code doesn't do: modulation, channel pressure, tuning,
the bend range in cents, master volume, and controller 121 as RP-015 defines
it. [ESFM_GM.md](ESFM_GM.md) describes these changes.

[`src/esfm/esfmfile.asm`](../src/esfm/esfmfile.asm) lets the driver play a
patch bank straight from a file named in `SYSTEM.INI`. The driver reads the
file when a program opens the device, if the file's date or time has changed
([ESFM_BANK.md](ESFM_BANK.md#bank-file-buildesfmdrv)).

Each change can be turned off in `SYSTEM.INI` with its own key under
`[ESFM.DRV]`, which the driver reads at the first `DRV_ENABLE`
([`src/esfm/esfmini.asm`](../src/esfm/esfmini.asm)). `QueueWhileBusy=0`
turns off the queue together with the holding at open, close and chip reset.
[DRIVER_CONFIG.md](DRIVER_CONFIG.md#63-esfmdrv) lists every key.

Everything else is ESS's code, unchanged. Because the bank loader is
untouched, `esfmpat` and essctl's *Load bank* work with the fixed driver as
they do with ESS's. The fixed driver's built-in patch bank is
[`esfm_patch_banks/bnk_com_better_square_wave.bin`](../esfm_patch_banks),
while a build made with `--stock` keeps ESS's `bnk_com.bin` and
`--bank FILE` builds the driver with another bank. The fixed driver also
carries ESS's `bnk_com.bin` as resource 1235, and `BetterSquareWave=0` plays
it instead.

### Installing

Windows keeps `ESFM.DRV` in use while it runs, so install the fixed driver
from DOS:

1. Choose Start > Shut Down > *Restart the computer in MS-DOS mode*.
2. Run `copy C:\WINDOWS\SYSTEM\ESFM.DRV C:\WINDOWS\SYSTEM\ESFM.ORG` to keep
   ESS's driver.
3. Run `copy build\ESFM.DRV C:\WINDOWS\SYSTEM\ESFM.DRV` to copy the fixed
   driver over it.
4. Type `exit` to go back to Windows.

To go back to ESS's driver, copy `ESFM.ORG` over `ESFM.DRV` in the same way.

## Checking your machine

essctl's *ESFM patch bank* page shows the driver's 18 voices live, with the
channel, the note and the state of each: *playing*, *held by pedal* or
*released*. While a program has the MIDI device open, the Chip column reads
the key-on bit back from the synthesizer. A voice that is `on` in the chip
while the driver has it as free is marked **STUCK**, and a voice that keeps
*playing* after the music has stopped is a hanging note too. With the fixed
driver, the page also counts the messages that came in while the driver was
busy, every one of which ESS's driver would have dropped.
`essctl /dump file` writes the same table to a file.

The *Stress test* button on the same page plays about 7 seconds of dense
music through MMSYSTEM's stream player, which sends it at interrupt time as
the MCI sequencer does. At the same time, essctl sends volume and expression
changes to the same device. Every note has its note off, so no voice should
be left sounding at the end. With ESS's driver, expect some voices to be
left sounding. With `build/ESFM.DRV`, expect none, along with a count of the
queued messages. Stop other MIDI playback before you run the test, because
only one program can have the ESFM device open.
