# essprobe: the ES1869 measured

`build/essprobe.exe` measures the chip itself, without `esssb16` in front of
it, in real DOS. The Sound Blaster 16 front rests on facts that the data
sheet leaves open or gets wrong, and that only the card can tell: what each
reset clears, how Audio 2 behaves on a 16-bit DMA channel, whether both
engines play cleanly at once, when EMM386 gives the 8237 what a program
programmed, and whether the engines disturb the rest of the machine. Each
case of the probe is one combination of settings, and the report says what
it measured. In a recorded run, each case's sound comes after a DTMF marker,
and [`tools/essprec.py`](../tools/essprec.py) measures every sound in the
recording. [ESS_EXTENDED.md](ESS_EXTENDED.md) gives the sources of what the
cases look for.

## What it measures

The cases come in groups. Each line of the report names its case, such as
`a2.t2_0`, and a `spec` line after it gives the case's settings as keys,
such as `t2=0` for Audio 2's single transfers.

* **env**: the machine and the card. DOS, the memory manager and its
  version, the `CONFIG.SYS` lines that load it, the DPMI host, the PCI
  bridges, the chip's identification, its PnP registers and the channels its
  DMA pins are wired to, the mixer and the controller registers as found,
  and the DMA controllers' registers as a program reads them.
* **rst**: what each reset clears. The probe sets known values in Audio 2's
  registers, the levels and the controller registers, then resets the chip
  with Audio_Base+6 = 3, then with 1, empties the FIFO with 2, and resets
  the mixer, and reads everything back.
* **a1**: Audio 1 alone, as native drivers start it: 16-bit stereo at 44100
  Hz by default, then each format, rates from 8000 to 48000 Hz on both
  clocks and in the new A1h mode, a single block, single and demand
  transfers (B9h, and the 8237's demand mode), the FIFO kept or filled
  first, the pauses through B2h, B8h and D0h, the DMA unmasked after the
  start, each side alone, and the interrupt off or never acknowledged.
* **a2**: Audio 2 alone, on the card's channel, with the same variations,
  its transfer types (78h bits 7 and 6), the 8237's demand mode with demand
  transfers of 4 bytes as ESS's drivers program it (`a2.dmd`), its slaving
  to Audio 1's clock (71h bit 1), 4x oversampling, its filter, its pauses
  through 78h, and a start in two steps, 78h's DMA before its FIFO's way to
  the DAC (`a2.start2`, `a2.left2`). On a 16-bit channel each byte of the
  sound goes into the low byte of a word, as ESS's drivers do; `a2.packed`
  gives it a game's own 16-bit buffer, and `a2.wide` and `a2.wide_packed`
  set 78h bit 5, which the data sheet reserves and Linux calls 16-bit DMA.
  These three run only where the card has Audio 2 on a 16-bit channel.
* **both**: Audio 1 at 294 Hz and Audio 2 at 882 Hz at once, on their own
  clocks or slaved, in mixed formats and transfer types, and with Audio 2
  muted, as `esssb16`'s pace runs it.
* **h1**: the DMA controllers behind the memory manager. When the 8237 gets
  what a program programmed while the channel is masked, and when its mask
  gets the program's mask and unmask (`h1.mask1`, `h1.mask5`, read where the
  chipset gives the mask back), what a program reads of a channel that
  moves, whose flip-flop a program's reads and writes toggle, whether a
  buffer across a page goes through EMM386's own buffer, and the case of
  Pinball Fantasies' driver: a channel carried over before its unmask. Under
  EMM386 the buffer across a page is single-cycle, because EMM386 stops the
  machine ("DMA mode not supported") at an auto-initialized transfer that it
  would have to buffer.
* **h4**: what the engines do to the machine. A busy loop's speed, the VGA's
  vertical retrace, the VGA DAC's entries, guard bytes around the DMA
  buffers, and a channel programmed the wrong way round, writing into memory
  while the chip plays.
* **stream**: a game's refills behind the DMA count, as Pinball Fantasies
  mixes: a glide that rises by 400 Hz a second, refilled every 20 ms behind
  the position the count gives, on Audio 1's own channel, on Audio 2's, and
  through a channel carried over as `esssb16` does under EMM386
  (`stream.mirror`), whose count stands still, so that it loops.
* **fix**: which start keeps a 16-bit stream in step. In the first runs,
  Audio 1 played every 16-bit stream a byte off, as noise, but when its DMA
  began well after its start (`a1.after` under EMM386), and Audio 2 lost its
  step when its DMA began before its FIFO's way to the DAC or resumed after
  a pause. Each case here plays the tone on the left alone, so that a stream
  a byte off is noise and two bytes off plays on the right: Audio 1's DMA
  unmasked 0 to 2000 us after the start (`late=N`), its registers in the
  order of `build/ES1869.DRV`'s player, which empties the FIFO after the
  format (`order=win`), B7h written once (`b7=once`) or its first write
  without bit 0 (`b7=nl`), at other rates and in 8-bit stereo, where a byte
  off swaps the sides; and Audio 2's pauses through 78h bit 0, bit 1 and
  both (`pause=fd`). Each start is a race, so the cases come two or three
  times. The first fix runs kept every stream in step with the DMA unmasked
  20 us or more after the start, which the fronts now do
  ([ESS_EXTENDED.md](ESS_EXTENDED.md#a-16-bit-stream-a-byte-off)). Version
  1.2 added the pause's end with the channels masked over it and unmasked
  100 us after (`hold`), and a start with Audio 1's DMA requests (B2h bit 6)
  on only 100 or 300 us after B8h bit 0, the channel unmasked (`b2late=N`):
  neither kept the step. Version 1.3 pauses through B7h bit 7, the FIFO's
  way to the DAC, so that the FIFO fills and the DMA stops after whole
  frames (`pause=b7`, `fix.pb7a` to `fix.pb7r22`), which kept it every time.
  Version 1.4 pauses as the fronts now do, B7h bit 7 and then B2h bit 6 off,
  and back on the other way round (`pause=b72`, `fix.pb72a` to
  `fix.pb72r22`). Its first run came back with the sides swapped once in
  six, so version 1.5 has three more of each pause in 16-bit stereo, and one
  that turns the DMA requests off only once +Ch bit 5 says the FIFO is full,
  so that no request is cut short (`pause=b7w`, `fix.pb7wa` to `fix.pb7wf`).

Every case uses the DMA channels that the card is configured for, Audio 1's
(PnP 74h) and Audio 2's (75h), since other devices may own the others. An h1
case of channel 5, and the cases that stand for a game's 16-bit channel, use
the card's 16-bit channel, and are skipped where it has none.

Under EMM386 the probe sees the 8237 past EMM386's translation through a
window: a port trap of Audio_Base+Dh, which the chip doesn't decode, through
EMM386's int 2Fh 4A15h. Its handler runs at ring 0, where no port is
trapped, and reads the 8237 for the probe. The probe removes the trap when
it ends; a run that didn't end leaves it to the next run, which removes it
first. `/N` runs without the window.

## Safety

* The probe refuses to run in a Windows DOS box, where the VxD has the chip,
  with `esssb16` resident, whose front would answer instead of the chip, and
  without an ES1869 at `BLASTER`'s base.
* Before it changes anything outside the chip's own registers, it writes the
  old values to `ESSPROBE.RST`: the mixer, the audio device's PnP registers,
  the interrupt mask, and the VGA DAC entries it uses. They go back at the
  end; after a run that didn't end, the next start puts them back first.
* Every case starts and ends with the chip's reset, its DMA channels masked.
  The sounds are quiet tones, about 9 dB under full scale, but the level
  tone of a recorded run is at full scale: turn the speakers down for it.
* Restart before you load `esssb16` again, since the probe leaves the chip
  in its power-on state, not as `esssb16` set it up.

## Running it

1. Start the machine straight to DOS, with EMM386 as usual, and without
   `esssb16`. Set `BLASTER` with the card's base, such as
   `SET BLASTER=A220 I5 D1 H5 P330 T6`.
2. Run `ESSPROBE /S` for the short run, about a minute, then `ESSPROBE` for
   all 179 cases, about five minutes. Each writes `ESSPROBE.TXT`; `/O:file`
   names another, such as `/O:FULL.TXT`. Esc stops at any time. The report
   is on the disk after every line, so a run that stops the machine keeps
   what it measured until then.
3. Run the full probe once more without EMM386, with its `DEVICE=` line
   taken out of `CONFIG.SYS`, as `ESSPROBE /O:REAL.TXT`. Without a memory
   manager the 8237 gets every access at once, which shows what EMM386
   changes.
4. Send the reports back.

The options:
* `/R`: a recorded run.
* `/S`: the short run, the cases marked for it in each group.
* `/A`: the cases without a sound only.
* `/C:names`: only the cases whose names start so, groups or cases,
  comma-separated: `/C:h1,a2.wide`.
* `/N`: no window on the 8237 under EMM386.
* `/O:file`: the report's file, else `ESSPROBE.TXT`.

The exit codes are 1 for a bad option, 2 without an ES1869 at the base, 3 in
a Windows DOS box, 4 with `esssb16` resident, 5 without memory or the
report's file, and 6 when cases failed.

## A recorded run

1. Connect the card's line out to a recorder's line in, in stereo, and
   record a WAV file at 44.1 or 48 kHz, 16 or 24-bit.
2. Run `ESSPROBE /R /S`. A full-scale tone plays: set the recorder's level
   just under clipping, start recording, and press Enter. The cases follow,
   each sound after its marker, then the end marker. Stop recording after
   it.
3. Run `ESSPROBE /R /O:FULLREC.TXT` the same way, into a second file.
4. Send each report with its recording.

For the fix group alone, record `ESSPROBE /R /C:fix /O:FIX.TXT` the same
way, with EMM386 as usual, and once more without it as `/O:FIXREAL.TXT`.
`/C:fix.pb72` runs only the fronts' pause, about 25 seconds, and
`/C:fix.pb7` all three kinds through B7h bit 7, about a minute.
`ESSPROBE /C:h1.mask /O:MASK.TXT` runs the two cases of the 8237's mask in a
second, without a sound or a recording.

`python3 tools/essprec.py ESSPROBE.TXT run.wav` analyzes a recording: it
finds the markers, fits the report's clock to the recording's, and measures
each case's sound by what it should hold. A steady tone gets its pitch, how
clean it is and the rate it played at; a side, how much louder it is than
the other; both engines, each tone's share of the sound; a muted case, its
level; a pause, the quiet in it; a glide, its steps back, where a loop or
stale data plays, and where it stands. The cases that look for what nobody
knows yet get what was heard. At the end, each spec key gets how many of its
cases passed and failed, which shows what a fault depends on.

## The tests

`tests/test_essprobe.py` runs every case on the chip model through the same
code (`src/essp.c`), with EMM386's DMA translation as `tests/emm386dma.py`
has it, and the program itself in the DOS emulator; it makes a recording
from what the model's DACs played and checks that `essprec.py` passes it and
fails it broken ([TESTING.md](TESTING.md)).
