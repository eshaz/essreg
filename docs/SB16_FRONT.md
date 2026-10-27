# The Sound Blaster 16 front as a state machine

Both fronts, `esssb16.com` in DOS and the extended `ES1869.VXD` in a Windows
DOS box, answer a game as a Sound Blaster 16 and play its sound on the
ES1869 in Extended mode. This page gives that translation as a state
machine: the events that a game causes, the operations that the front sends
the chip, the state that the front keeps, and the transitions between them.
[SB16.md](SB16.md) describes the card that the game expects, and
[ESS_EXTENDED.md](ESS_EXTENDED.md) the chip that the operations program.

`src/esssb16.asm` and `ESSREG_S16_*` in `src/vxd/essext.asm` implement the
machine in the same structure, with the same flags (`F_*` and `S16_*`).
`tests/sbfsm.py` is its executable reference, in Python, with the TSR's
names. The tests run every scene of `tests/sbscenes.py` on it and on both
fronts ([Checking the fronts](#checking-the-fronts)).

## The events

A game causes these events through the ports that the front traps:
* **The reset port** (Audio_Base+6): a write of 1, then of 0.
* **A byte to the command port** (+Ch): a command, or an operand of the last
  one. The front parses the bytes as DSP 4.05's firmware does, so that each
  command arrives with its operands as one event.
* **A read of the data port** (+Ah), of the status ports (+Ch and +Eh), and
  of the 16-bit acknowledge (+Fh).
* **The mixer** (+4 and +5): an index, a write, a read.
* **The 16-bit DMA channel**, the `H` of `BLASTER`: its programming
  (address, count, page and mode), its mask and unmask, and the reads of its
  address and count. Only some backends see them ([The
  backends](#the-backends)).

The chip causes two more: Audio 1's interrupt at the end of a block of its
counter, and Audio 2's, its latch in mixer 7Ah bit 7. Both fronts learn of
them through the game's handler, which reads mixer 82h and acknowledges, and
the VxD also sets the count that the game reads at the interrupt's end
(`ESSREG_S16_Position`).

## The operations

The front programs the chip with these operations, which ESS_EXTENDED.md
gives with their sources, and which `tests/sbops.py` records from the models
of the chip and the DMA controllers:
* **R(r, v)**: register r set to v, with r as a DSP command and v as its
  operand. The front reads a register it doesn't know with C0h, r.
* **M(r, v)**: mixer register r set to v. The front puts the game's index
  back after each access of its own.
* **D(c, ...)**: a command of the chip's firmware, with its operands: C6h
  for Extended mode, D1h for the DAC, F2h for the processor's interrupt, and
  in Compatibility mode only, the firmware's transfers, the direct DAC and
  ADC, and MIDI.
* **RESET**: the chip's software reset (+6 = 3, three reads, 0, then AAh),
  which ends Extended mode and clears 7Ah and 7Ch. **FIFO**: Audio 1's FIFO
  emptied (+6 = 2, then 0).
* **ACK**: a read of +Eh, which acknowledges Audio 1. Audio 2's acknowledge
  is M(7Ah) with bit 7 clear.
* **DMA(ch, ...)**: a DMA channel programmed: its mode, address, count and
  page, as the chip's first cycle on it finds them.
* **HOLD** and **SETTLE**, around Audio 1's start: HOLD masks the channel
  that plays, and SETTLE polls +Ch 64 + 22528 / v times, v the rate
  generator's value, then unmasks what HOLD masked. The game's own channel
  is held only where its mask reads back unmasked at 0Fh, and the chipset
  gives the mask back, with DEh bit 0, the cascade, clear. The VxD holds
  through VDMAD (VDMAD_Mask_Channel and VDMAD_UnMask_Channel), for the
  program's channel while its virtual state is unmasked.
* **PACE**: Audio 2 runs on the game's 16-bit channel, muted, so that the
  channel's count moves while Audio 1 plays: M(71h) with bits 4, 1 and 0
  clear, for Audio 1's clock without 4x or the FM mix, M(7Ch, 0), M(7Ah)
  with bit 1 for stereo as A8h has it and no interrupt, and M(78h, 13h),
  single transfers, auto-initialized, the DMA and the FIFO to the DAC. Each
  byte that Audio 2 takes is a word of the channel. **PACE off** is M(78h,
  0).

## The state

The front's state has these parts:
* **The chip's mode**:
  * FRESH after the chip's own reset, with Extended mode off. The installer
    leaves the chip there.
  * EXT once C6h and D1h went to the chip (`F_EXT`), for the front's
    transfers.
  * COMP once the front put the chip in Compatibility mode for a command of
    the firmware (`S_COMP`, `S16S_COMP`), with D1h sent.
  * PASS once the game itself used Extended mode, with C6h followed by a
    register command (`F_ESS`). Every access then goes to the chip until the
    game's next reset.
  * SICK once the chip took no byte within the front's 400h polls (`sick`,
    `EX_S16_Sick`). No byte goes to the chip until its own reset.
* **Audio 1's engine** (`flags`): IDLE, or RUN (`F_RUN`) with its kind, 8 or
  16-bit (`F_16`), playback or recording, and auto-initialized (`F_AUTO`) or
  not, and its format in B6h and B7h. Two marks go with RUN: LAST
  (`F_LAST`), a single-cycle block queued behind the auto-initialized one,
  and PAUSED (`F_PAUSE`).
* **Audio 2's engine** (`flags2`): IDLE, or RUN with AUTO, LAST and PAUSED.
  It plays only 16-bit sound.
* **The halves** (`halves`, `EX_S16_Halves`) of each engine's 16-bit block
  of more than 32768 samples, which has more bytes than the chip's counter
  holds: ON while the block runs in halves, SECOND while its second half
  plays, and ONCE when the block is the last, so that the chip's
  auto-initialize ends with its first half.
* **The chip's registers** that the front wrote or read since the chip's
  last reset (`ctrl` and `known`), so that a transfer writes only what
  changes.
* **The 16-bit channel** (`vbits`): its programming as the game wrote it,
  and five marks: MASKED, the flip-flop of the game's reads and writes, NEW
  when the game programmed it since it was carried over, LIVE while the
  engine's channel holds what was carried over, and PACED while Audio 2
  paces it. Which backend the front has decides how it sees the channel:
  TRAPPED or READBACK.
* **The DSP**: the parser (the operands still to come), the read latch and
  the answer that it gives a byte at a time, a byte held while an answer is
  read, the MIDI modes (`S_UART`, `S_MIDI`), D1h's flag (`S_SPKR`), whether
  the processor's interrupt is the 16-bit one (`S_T16`), and whether the
  next reset is warm (`S_WARM`).
* **The rate** (`rate_v`, the rate generator's value of 40h, 41h and 42h),
  **the 8-bit block** (48h), **the test register** (E4h) and **the mixer's
  view**, the Sound Blaster 16's 24 registers 30h-47h as the game wrote
  them.

## The transitions

Each transition gives the event, the condition, the operations in order, and
the state after it.

### The game's reset

A write of 1 to the reset port halts the DSP (`halt`, `ESSREG_S16_Halt`):
1. Audio 1 stops, with PACE off if it is PACED, R(B8h) with bit 0 clear if
   it runs, and its FIFO empties, FIFO.
2. A running Audio 2 stops, M(78h) with bits 4 and 1 clear. Where Audio 2
   has a channel, M(7Ah) clears bits 7:3, the latch and the interrupt, and
   keeps the format, so that what its FIFO holds plays out.
3. A read of +Eh acknowledges Audio 1, and a SICK chip gets RESET and
   becomes FRESH.
4. The parser and the latches empty, and both engines are IDLE.

The chip's mode doesn't change otherwise: the front doesn't reset the chip
for a game's reset, because the reset would end Extended mode.

The write of 0 boots the DSP (`boot`). In the MIDI UART mode, and in COMP,
RESET comes first, because only the chip's own reset ends them, and the chip
is FRESH. AAh goes into the read latch. A cold boot, one not after a
high-speed or MIDI UART command, sets the rate to 38h, the 8-bit block to
2048 bytes and the speaker flag off.

In PASS, both writes go to the chip itself. After the write of 0, the front
reads the chip's AAh, forgets the registers it knew, and boots: the chip is
FRESH.

### A transfer

The transfer commands are 14h, 1Ch, 24h, 2Ch, 90h, 91h, 98h and 99h, with
the 8-bit block or a length of their own, and B0h to CFh, with a mode byte
and a length. Each becomes a transfer of its kind (`xfer`,
`ESSREG_S16_Xfer`):
1. **A queued block.** A single-cycle command, other than 91h and 99h, while
   an auto-initialized transfer of its kind runs marks the transfer LAST.
   Its length goes into the counter for the next block: R(A4h) and R(A5h) on
   Audio 1, M(74h) and M(76h) on Audio 2. Nothing else changes, as on the
   card, where the firmware queues the block.
2. **Which engine.** A 16-bit playback goes to Audio 2 where Audio 2 has an
   8-bit channel of its own and the game a 16-bit channel. Everything else
   goes to Audio 1. A 16-bit transfer on Audio 1 stops Audio 2's first, and
   one on Audio 2 stops Audio 1's 16-bit one.
3. **Audio 1.** Unless it runs a single-cycle block, Audio 1 stops first,
   R(B8h) with bit 0 clear if it runs, then FIFO. COMP and SICK lead to
   RESET, and a chip that isn't EXT gets C6h and D1h. Then come R(A4h) and
   R(A5h) with minus the bytes, R(B8h) stopped, with auto-initialize (bit
   2), the ADC (bit 3) and the DMA's direction (bit 1), R(B9h) for the
   transfer type, R(A8h) bits 1:0 for mono or stereo, R(A1h) and R(A2h) for
   the rate, and B6h and B7h for the format. A changed format starts with
   FIFO, then R(B6h), R(B7h) 51h (71h for B6h 00h), and R(B7h) with the
   format. Then R(B1h) and R(B2h) with bits 6 and 4 set, mixer 71h for the
   DACs' mode, the DMA with HOLD, R(B8h) with bit 0 set, SETTLE, and PACE
   where it paces ([The 16-bit channel](#the-16-bit-channel)). Of the
   registers, only those that change are written. Audio 1 is RUN.
4. **Audio 2.** M(78h, 0), M(74h) and M(76h) with minus the bytes, M(70h)
   and M(72h) for the rate, M(7Ch) for the voice level, M(7Ah) for the
   interrupt and the format, mixer 71h for the DACs' mode and Audio 2's own
   clock, the DMA, and M(78h) with the DMA, the FIFO to the DAC,
   auto-initialize as the command says and the transfer type. Audio 2 is
   RUN.

An engine stays RUN when its single-cycle block ends: the chip stops by
itself, and the engine is IDLE again only with the next transfer that stops
it or the game's reset. A single-cycle block that follows one in the same
format on Audio 1 goes on from it without FIFO, because the FIFO still holds
the last block's end, and 82h names the ended block's interrupt by its kind.

HOLD and SETTLE keep the DMA's bytes away from Audio 1 while its start
settles. The start's first byte, when it comes within a few microseconds of
the start's first DMA request, is lost but counted, and the stream plays a
byte off from there: noise in 16-bit sound, the sides swapped in 8-bit
stereo (`fix.base1`, `fix.late0`, `fix.b2late1`,
[ESS_EXTENDED.md](ESS_EXTENDED.md#a-16-bit-stream-a-byte-off)). With the
channel unmasked 20 us or more after the start, every rate measured stayed
in step. The game's own channel that the game unmasks after the command gets
SETTLE's time too, since the trap returns only after it.

A 16-bit block of more than 32768 samples starts in halves (`half_count`):
the counter gets half the block's bytes, the chip auto-initializes also for
a single-cycle block, and the halves are ON, with ONCE for a single-cycle
block. A queued block keeps the counter as its bytes fit, and the halves
end.

### Pause, continue and the end of auto-initialize

D0h and D5h pause the engine of their kind, D4h and D6h continue it, and D9h
and DAh end its auto-initialize with the block that plays. The 16-bit kind
is Audio 2's while Audio 2 runs. A command for a kind that doesn't run does
nothing:
* **Audio 1**: the pause clears B7h bit 7, so that the FIFO stops feeding
  the DAC and keeps whole frames, then B2h bit 6, so that the chip asks for
  no more DMA, and when PACED, 78h bit 0. The continue sets the rate again,
  then B2h bit 6, B7h bit 7 and 78h bit 0. The end of auto-initialize clears
  B8h bit 2. A pause through B2h bit 6 alone came back in step only about
  half the time, since the DAC played on and ran the FIFO down into a
  half-done frame (`fix.pb2late`), and one through B7h bit 7 every time
  (`fix.pb7a`).
* **Audio 2**: the pause clears 78h bit 0, which silences the DAC, though
  the card's DMA runs on meanwhile and the stream skips what it moved
  (`a2.pause_f`), and the continue sets the rate again, then 78h bit 0. The
  end of auto-initialize clears 78h bit 4.

In the first half of a block in halves, the end of auto-initialize sets ONCE
instead (`half_last`), so that the block plays to its end.

### The interrupts and their acknowledges

A read of mixer 82h answers 20h with the pending interrupts (`irq_src`,
`ESSREG_S16_Pending`): bit 0 for Audio 1's counter or the processor's
interrupt of the 8-bit kind, bit 1 for those of the 16-bit kind and for
Audio 2's latch, and bit 2 for the MPU-401's. Audio 1's counter interrupt is
its transfer's kind. The processor's (F2h, the firmware's transfers, MIDI
input) is the 8-bit kind unless F3h asked for the 16-bit one.

An interrupt in the first half of a block in halves is the front's own
(`a1_half`, `a2_half`). When the game reads 82h or acknowledges, the front
acknowledges the chip itself, ACK or M(7Ah) with bit 7 clear, and the second
half begins (SECOND). With ONCE, the chip's auto-initialize ends, R(B8h) or
M(78h) with bit 2 or bit 4 clear, so that the chip stops at the block's end.
82h names no interrupt of that engine then.

A read of +Eh acknowledges the 8-bit kind, and one of +Fh the 16-bit kind
(`ack`, `ESSREG_S16_Ack`): ACK when Audio 1's pending interrupt is of that
kind, and M(7Ah) with bit 7 clear for Audio 2's. The acknowledge ends
SECOND, an acknowledge of Audio 1 at the end of a block that doesn't
auto-initialize turns PACE off, and a LAST engine ends its auto-initialize:
Audio 1 with B8h bit 2 cleared, Audio 2 with 78h bit 4 cleared.

### The firmware's commands

The direct DAC and ADC (10h, 20h), ADPCM (16h, 17h, 1Fh, 74h to 77h, 7Dh,
7Fh), silence (80h) and MIDI (30h to 38h) have no counterpart in Extended
mode, so the chip runs them in Compatibility mode. From EXT, RESET ends
Extended mode, which also ends Audio 1's transfer, with PACE off before it.
While Audio 2 runs, the front reads 7Ah and 7Ch before the reset and writes
them back after it. Then D1h, and the chip is COMP. A transfer of the
firmware's gets the time constant, 256 minus 1 MHz over the rate (40h), then
the command and its length. An auto-initialized one, which the chip doesn't
have, plays the 8-bit block once. The next transfer of the front's brings
the chip back to EXT.

### The 16-bit channel

What the game programs of its 16-bit channel goes to the channel of the
engine that plays it (`mirror`, `ESSREG_S16_DMA_State`): the word address
and the page as a byte address and page, the word count as a byte count, the
transfer type that the engine's B9h or 78h asks for, the direction, and
auto-initialize as the game's channel has it. An 8-bit transfer on Audio 1
runs on the game's own programming of Audio 1's channel, so what was carried
over there is gone (LIVE clear).

With TRAPPED, the front follows the game's writes: a 16-bit transfer carries
the channel over when it is NEW or not LIVE, and waits masked while the
game's channel is MASKED. The engine's channel follows the game's mask and
unmask while a 16-bit transfer runs, and an unmask after new programming
carries it over. The game's reads of the address and count answer those of
the engine's channel, in words, the high byte from the read of the low byte.

With READBACK, the front reads the game's channel back at each 16-bit
command, with the controller's flip-flop cleared before and after, so the
game has to program the channel before the command. The engine's channel
auto-initializes, since the chip asks for a block's bytes only. A
single-cycle block whose channel still holds what was carried over goes on
from it. The count that the game reads stands still, but where Audio 2's
channel is the game's 16-bit one (`pace_ch`): there an auto-initialized
16-bit playback on Audio 1 gets PACE after SETTLE, and is PACED until Audio
1 stops, the chip's RESET, or the acknowledge of a block that doesn't
auto-initialize. Audio 2's FIFO holds 64 bytes, a word of the channel each,
fewer frames than Audio 1's 256 bytes, so the count stays behind Audio 1's
channel, and a game that mixes up to it writes only what Audio 1 has taken.
An auto-initialized command on the channel carried over last, which PACE
moved along its line (the page the same, and the address on as far as the
count back), takes back the start it had and goes on where Audio 1's channel
is, unless the channel moved less than 64 words.

### The mixer

The Sound Blaster 16's registers 30h-47h and the Sound Blaster Pro's 04h,
0Ah, 22h, 26h, 28h and 2Eh, views of them, keep the game's values in the
bits that the CT1745 keeps, and give the chip the levels that they stand
for. 0Ch and 0Eh read 0 and ignore writes. 80h and 81h answer the card's
interrupt and channels, a write of 81h selects the 16-bit channel, and 82h
answers the interrupts. A write of 00h resets the view to the card's
defaults and gives them to the chip. Every other register is the chip's.

## Invariants

The machine keeps these:
* The chip sees nothing that the data sheet leaves undefined: no
  Compatibility mode command but D1h, D3h and the safe ones while Extended
  mode is on, no register command without C6h, and no read of the FIFO port
  (`tests/esschip.py`'s violations).
* Every DMA cycle moves data in the direction of its transfer
  (`tests/dma8237.py`).
* B8h bit 0 is the last write of Audio 1's start, and 78h the last of Audio
  2's. No DMA byte reaches Audio 1 within SETTLE's polls of its start
  (`tests/esschip.py`'s RACE_US). Audio 2 starts with one write of 78h, its
  DMA and its FIFO at once, which stayed in step on the card where two steps
  did not (`a2.start2`).
* The front sends DSP commands only while the game waits for the port access
  that caused them: in the trap's handler, never from an interrupt of its
  own.

## The backends

The fronts run the same machine with different ways to see the game's DMA
programming and the chip's interrupts:

| Front | Memory manager   | 16-bit channel | Interrupts         |
|-------|------------------|----------------|--------------------|
| TSR   | QPI (QEMM, JEMM) | TRAPPED        | the game's handler |
| TSR   | HDPMI32i         | TRAPPED        | the game's handler |
| TSR   | EMM386           | READBACK, PACE | the game's handler |
| VxD   | Windows (VDMAD)  | VDMAD          | the hardware's     |

In a DOS box, VDMAD has the game's channel virtualized for the front, and
the front's carrying over goes through VDMAD's state of the engine's channel
([VXD_INTERNALS.md](VXD_INTERNALS.md)).

## Checking the fronts

`tests/test_sbfsm.py` runs every scene on the reference and compares:
* the reference's port accesses with those of `esssb16.com`, access for
  access, with the game's 16-bit channel trapped (QPIEMU) and read back
  (EMM386), and with Audio 2 on channel 3 and on the 16-bit channel 5. Only
  the values that the DMA controllers ignore are left out, those written to
  their flip-flop, master clear and mask clear ports. What the game saw has
  to be what the Sound Blaster 16 model shows it, as for the TSR
  ([TESTING.md](TESTING.md)).
* the operations of the VxD's front for its transfers with the reference's,
  under QPIEMU, the levels and the record source left out, because the VxD
  keeps its view of the mixer around Windows' own. The accesses of ESS's
  code in the VxD aren't the front's: its interrupt and EOI procedures, its
  DMA handlers and its taking of the DSP for the DOS box.

A difference shows the first operation where the two part, with the ones
before it. A change to the machine goes into the reference and both fronts
alike.

## What the probe decided

These transitions follow the card's answers ([ESSPROBE.md](ESSPROBE.md),
[ESS_EXTENDED.md](ESS_EXTENDED.md)):
* `a2.wide_packed`: Audio 2 doesn't play a game's own 16-bit buffer on the
  game's 16-bit channel with 78h bit 5, so a 16-bit playback goes to Audio 2
  only where Audio 2 has an 8-bit channel of its own.
* `h1.reach5` and `h1.stale`: EMM386 gives the 8237 a channel's programming
  at the program's unmask, so READBACK carries over what the channel had
  before when a game unmasks its channel after the command, as Pinball
  Fantasies' driver does.
* `fix.late20` and the others: HOLD and SETTLE around Audio 1's start.
* `both.pace` and `h4.pace`: Audio 2, muted, on Audio 1's clock, 8-bit and
  without its interrupt, moved its 16-bit channel's count at Audio 1's rate,
  and left the machine alone, so READBACK paces where Audio 2's channel is
  the game's 16-bit one.
* `a1.pause_b2` and `fix.pb7a`: B2h bit 6 stops Audio 1's DMA within a few
  bytes, but the stream came back in step only about half the time, held or
  not (`fix.pb2hold1`); with B7h bit 7, which takes the FIFO off the DAC, it
  came back in step every time, though the DMA fills the FIFO first. So the
  pause takes the FIFO off the DAC and then stops the DMA, which `fix.pb72a`
  to `fix.pb72r22` measure.
* `a1.b9_0` to `a1.b9_3`: B9h 00, 01 and 10 run at the rate and 11 runs
  ahead of it, so the fronts write 10, or 00 with `/S`.
