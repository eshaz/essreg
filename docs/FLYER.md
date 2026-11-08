# The MOTU PC-MIDI Flyer on Linux

The PC-MIDI Flyer is MOTU's MIDI interface for the parallel port, with two
inputs and two outputs, A and B. It has no supply of its own and runs off
the port's data lines. snd-flyer, in `linux/flyer/`, drives it as one raw
MIDI device, and `linux/tools/flyer.c` reaches it from user space by port
I/O. Both share the unit's protocol in `linux/flyer/proto.h`, which
`tests/test_flyer.py` runs on a model of the unit.

## Using it

The Armada's parallel port is off in its BIOS, and
[LINUX.md](LINUX.md#a-parallel-port-midi-interface) shows how ACPI turns it
on. `linux/tools/flyer-up.sh` does that and loads the module, and with `off`
takes both away again. The unit dies with the port, so the script turns the
port back off when the module doesn't load:

```
make -C linux flyer
sudo linux/tools/flyer-up.sh linux/flyer/snd-flyer.ko
aplaymidi -p "PC-MIDI Flyer:0" song.mid
sudo linux/tools/flyer-up.sh off
```

Where the module is part of the kernel's build, `flyer-up.sh` alone loads
it. The script refuses while parport_pc, snd-mtpav or snd-mqengine is
loaded, because snd-flyer takes the port's I/O and IRQ itself, and
`mqengine-up.sh` refuses while snd-flyer is loaded. The module's parameters
are `port`, 0x378 by default, `irq`, 7 by default or -1 to poll, `index` and
`id`. It refuses to load unless the unit's pulse shows on the port. It
handles no suspend, so unload it and load it again after one.

With the Computer Bypass button in, the unit still pulses and takes bytes,
but no byte passes between the computer and the MIDI ports, so the driver
loads and sends and nothing comes out or in. Bypass is the first thing to
check when a loop brings nothing back.

The two inputs and two outputs are raw MIDI device 0 of the card "Flyer",
subdevice 0 for A and 1 for B, as `hw:CARD=Flyer,DEV=0,SUBDEV=1` for B. The
sequencer's client "PC-MIDI Flyer" has a port for each.

## The driver

Nothing on the port says that the unit has room for another byte, and each
sight of room covers only four bytes, so a timer looks for room every 250 us
while an output has bytes. Room comes back when a busy output has a byte of
320 us left to send, and on the model with both outputs busy a timer of 400
us let them run dry, at 80 % of MIDI's speed. The timer is a high-resolution
one that runs as a softirq, and each byte takes the driver's lock on its
own, so that other interrupts get in between bytes. A byte takes some thirty
port accesses to read, of about a microsecond each.

The port's interrupt says that a byte came in. The driver lets the port
raise it only while an input is open, because the unit pulses the line every
16.4 ms whatever it is doing, and the interrupt only starts the timer, which
reads both inputs. Bytes that no open input takes are read and dropped,
since each byte left waiting raises the interrupt again every 256 us, and
what came in before an input opened is dropped at the open. With `irq=-1`
the timer reads the inputs every 2 ms while one is open, well inside the 4.8
ms that a ring of 15 lasts at full speed.

A status that reads FFh means a port that is off or a unit without its
supply. The driver then sees neither room nor bytes, so an output waits
instead of reading garbage or losing bytes.

## The protocol

The unit answers a protocol of its own. snd-mtpav, which speaks the MIDI
Timepiece AV's, read input B but brought no byte out of either output. The
rules below come from MOTU's `flypxdx.vxd` and `flypxdx.drv`, and
`linux/tools/flyer.c` confirmed them on the unit, except the rests, which
are MOTU's timing. The control lines say which port a byte belongs to, since
no byte carries a port number. Each output has a strobe and each input a
read line, and all of them idle high:

| Control | Lines low      | Meaning             |
|---------|----------------|---------------------|
| 14h     | none           | idle                |
| 15h     | nSTROBE        | a byte for output A |
| 16h     | nAUTOFD        | read input A        |
| 10h     | nINIT          | a byte for output B |
| 1Ch     | nSELECTIN      | read input B        |
| 11h     | nSTROBE, nINIT | the command channel |

Bit 4 of the control register only lets the port interrupt, and no line
carries it to the unit.

**Output.** Status bit 7 says that the unit has room, and four bytes may
then go, to either output. A byte goes on the data lines with its strobe,
and the data lines hold it for a rest after the strobe: three accesses of
the idle control for B and six for A, the timing of MOTU's driver. Room
shows again once none of the four waits behind a byte being sent: 960 us
after four bytes into an idle unit, and every 1.28 ms while one output is
kept busy. The unit keeps seven bytes waiting, and an eighth makes the queue
look empty and loses them: of nine bytes in a row into an idle unit, only
the first went out, and of ten the first and the tenth.

**Input.** Status bit 3 at idle says that a byte waits on some input, and
with that input's read line on it says whether the byte is that input's. The
byte comes two bits at a time on status bits 5 and 4, low bits first, one
pair each time the read line comes back on after a rest. Each input is a
ring of 16 that holds 15, and a 16th byte makes the ring look empty.

**The pulse.** Status bit 6 is the port's interrupt line. It is up for 240
us every 16.4 ms whatever the unit is doing, the overflow of a 16-bit timer
at 4 MHz, and it also rises for 128 us after the unit takes bytes. While a
byte waits it stays up but for a drop of 16 us every 256 us, so a byte that
the driver misses raises it again. The driver's probe looks for a change of
the line for 40 ms, polling every few microseconds.

**The supply.** The data lines feed the unit, so they go back to FFh after
every byte and every command. With them low for a second it loses bytes and
reads FFh until it has been fed again, and MOTU's driver holds them at FFh
for 50 ms before the first access. snd-mtpav leaves the last byte on them.

**Commands.** A byte on the data lines with control 11h is a command. MOTU's
driver sends 07h at every open, which empties the input rings. The unit also
says which model it is: with 11h on the data lines and control 1Fh, then
both read lines on, status bits 5 and 4 read 2 on a Flyer. The question
sends a byte 11h out of output A, so the driver doesn't ask it.

## The tests

`tests/test_flyer.py` builds `proto.h` for the host and runs it through
ctypes against a model of the unit, in which each port access takes a
microsecond: the strobes and read lines, the shared queue with its room
flag, the rings, the pairs, the rests, the pulse and the supply. The
driver's work is the same helpers in a loop that its timer and interrupt
run, at the periods `flyer.c` defines, and it plays 1024 bytes out of each
output into cables back to the inputs, alone and at once, by interrupt and
polled. Each negative control breaks one rule and has to fail on the model:
the byte left on the data lines starves the unit, eight bytes on a sight of
room are lost, pairs read high first and rests that are too short garble
bytes, and a poll of 6 ms overruns a ring. The same file checks that
`tools/flyerpatch.py` and `tools/mqenginepatch.py` apply to a kernel tree in
either order.

## On the Armada

snd-flyer found the unit by its pulse, loaded by IRQ 7 and with `irq=-1`.
With no input open the port raised no interrupt in 2 s, and with one open it
raised 61 a second, the unit's pulse. 1024 bytes went out of A, out of B,
and out of both at once in 330 ms, MIDI's full speed, by interrupt and
polled: the drain returned 380 ms after the write, of which 50 ms are the
raw MIDI core's own wait.

Through a cable from each output to its own input, 1024 bytes of sysex came
back exact on A, on B, and on both at once, by interrupt and polled, and all
95 events of a 37 s MIDI file came back in order through the sequencer, by
interrupt and polled. A reader opened after a byte had come in got nothing
of it. Each loop was read by amidi, with the reader opened first:

```
amidi -p hw:CARD=Flyer,DEV=0,SUBDEV=0 -r got.bin -t 2 &
amidi -p hw:CARD=Flyer,DEV=0,SUBDEV=0 -s sysex.bin
cmp got.bin sysex.bin
```

## The tool

`linux/tools/flyer.c` reaches the unit by port I/O as root, with no driver
and no interrupt, so don't run it while snd-flyer is loaded. `-i 0` leaves
control bit 4 clear, as the driver does while no input is open. `flyer look`
shows the lines at each control state, `flyer loop a 512` sends a sysex out
of an output and reads both inputs, for a cable from that output to an
input, and `flyer recv`, `send`, `lat`, `fill`, `depth`, `pace`, `flood`,
`trace`, `watch`, `id`, `init` and `alive` measure the rest. The usage at
the top of the file lists their arguments.
