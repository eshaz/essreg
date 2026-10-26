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
recording.

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
  first, the pauses through B2h and B8h, the DMA unmasked after the start,
  each side alone, and the interrupt off or never acknowledged.
* **a2**: Audio 2 alone, on the card's channel, with the same variations,
  its transfer types (78h bits 7 and 6), the 8237's demand mode with demand
  transfers of 4 bytes as ESS's drivers program it (`a2.dmd`), its slaving
  to Audio 1's clock (71h bit 1), 4x oversampling, its filter, and its
  pauses through 78h. On a 16-bit channel each byte of the sound goes into
  the low byte of a word, as ESS's drivers do; `a2.packed` gives it a game's
  own 16-bit buffer, and `a2.wide` and `a2.wide_packed` set 78h bit 5, which
  the data sheet reserves and Linux calls 16-bit DMA. The `a2.chN` cases
  move Audio 2 to another channel where one of the card's DMA pins is wired
  to it.
* **both**: Audio 1 at 294 Hz and Audio 2 at 882 Hz at once, on their own
  clocks or slaved, in mixed formats and transfer types, and with Audio 2
  muted, as `esssb16`'s withdrawn pace ran it.
* **h1**: the DMA controllers behind the memory manager. When the 8237 gets
  what a program programmed while the channel is masked, what a program
  reads of a channel that moves, whose flip-flop a program's reads and
  writes toggle, whether a buffer across a page goes through EMM386's own
  buffer, and the case of Pinball Fantasies' driver: a channel carried over
  before its unmask.
* **h4**: what the engines do to the machine. A busy loop's speed, the VGA's
  vertical retrace, the VGA DAC's entries, guard bytes around the DMA
  buffers, and a channel programmed the wrong way round, writing into memory
  while the chip plays.
* **stream**: a game's refills behind the DMA count, as Pinball Fantasies
  mixes: a glide that rises by 400 Hz a second, refilled every 20 ms behind
  the position the count gives, on Audio 1's own channel, on Audio 2's, and
  through a channel carried over as `esssb16` does under EMM386
  (`stream.mirror`), whose count stands still, so that it loops.

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
   all 102 cases, about three minutes. Each writes `ESSPROBE.TXT`; `/O:file`
   names another, such as `/O:FULL.TXT`. Esc stops at any time.
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
