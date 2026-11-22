# The ESFM.DRV synthesis engine

This page specifies how `ESFM.DRV`, ESS's Windows MIDI driver for the
ES1869's ESFM synthesizer, turns MIDI messages into register writes. It is
written for a reimplementation of the engine as new code, such as the
General MIDI synthesizer of the Linux driver ([LINUX.md](LINUX.md)), so that
nobody has to read the assembly. ESS's driver is `driver/ESFM.DRV`, whose
source is [`src/esfm`](../src/esfm). The rebuilt driver, `build/ESFM.DRV`,
adds the fixes of [ESFM_MIDI.md](ESFM_MIDI.md) and the General MIDI features
of [ESFM_GM.md](ESFM_GM.md). Each section describes ESS's behavior first and
then what the rebuilt driver does differently.

Addresses are `segment:offset` in `ESFM.DRV`: seg1 is the fixed code with
the whole engine, seg3 the code of open and close, and DGROUP the data
segment. Names such as `note_on` are the labels of `src/esfm/seg1.asm`, and
the rebuilt driver's code is named by its labels in `src/esfm/esfmgm.asm`,
`esfmfix.asm` and `esfmped.asm`. Channels are numbered 0-15 as in the status
byte, so the drum channel, MIDI channel 10, is channel 9. Numbers with an
`h` are hexadecimal, and a division written as `floor(a / b)` rounds toward
minus infinity, as the code's arithmetic shifts do.

## How this page was checked

Every rule of sections 1 to 9 about what the driver writes, apart from the
message queue and the byte parser, was written into a Python model of the
engine, which then ran side by side with the driver's own code in
[`tests/esfmemu.py`](../tests/esfmemu.py). For each message, the model's
register writes had to equal the driver's port writes, register for register
and in order, and the model's voice table had to equal the driver's. These
runs matched completely:
* ESS's driver ran with ESS's bank, `esfm_patch_banks/bnk_com.bin`, through
  100,000 random messages (notes on all 16 channels, every controller the
  driver knows, program changes, pitch bends, channel pressure,
  `midiOutReset`, close and open). It also ran 45,000 messages that keep all
  18 voices busy on 5 channels including the drums, and 34,400 notes and
  bends on a test bank that sweep every detune value, five bend ranges and
  the whole keyboard.
* The rebuilt driver ran with its default settings on the three banks of
  `esfm_patch_banks` through 150,000 random messages, with GM, GM2, GS and
  XG resets, the master volume and bend ranges up to 127 semitones, and
  through 6,000 more that sweep the tuning.
* The rebuilt driver ran 6,000 messages with each switch of section 9 that
  changes the engine at 0 (`Vibrato`, `Tuning`, `ResetControllers`,
  `LivePan`, `SysEx`, `PedalRelease` and `SilenceOnClose`), and 6,000 with
  all seven at 0.

The Linux driver's engine, `linux/es1869/esfm_gm.c`, was written from this
page, and it makes the rebuilt driver's writes in the same emulator
([`tests/test_esfmgm.py`](../tests/test_esfmgm.py)). On the Armada's ES1869
it played `build/GMCHECK.MID`, recorded digitally (mixer 7Fh bit 4) and
through the output: every step sounded as
[TESTING.md](TESTING.md#g3-general-midi) describes it, with the pitch, the
vibrato and the tuning where sections 6 and 9 put them, apart from the pan,
which the chip plays the other way round (section 8).

The references in parentheses say how each rule is known. *Verified* means
that these runs, or a run of that case alone, showed it. *Read* means that
the rule was taken from the code without running it. *ESFMu* marks what the
chip does with a register, which comes from
[ESFMu](https://github.com/Kagamiin/ESFMu), the hardware-accurate ESFM
emulator, because the emulator here records the writes but doesn't
synthesize sound.

## 1. The chip interface

### Native mode

The synthesizer starts in its OPL3 emulation mode. ESS's driver switches it
to the native mode, where each of 18 voices has four operators of its own,
at the start of every reset (`chip_reset`, seg1:009A). It writes 00h to
FM_Base+0 and then register 005h = 80h with the protocol below. In the
emulation mode, FM_Base+2 = 05h latches register 105h, FM_Base+3 = 00h
writes 00h to it, and FM_Base+1 = 80h writes 80h to it, and bit 7 of 105h
turns the native mode on. In the native mode, a write to FM_Base+0 leaves
it, so the same sequence ends in the native mode from either mode (port
sequence verified, its effect ESFMu). The Linux driver's
`es1869_fm_native()` writes only FM_Base+2 = 05h and FM_Base+3 = 80h, which
has the same effect from the emulation mode.

ESS's driver never leaves the native mode. At close it keys the voices off
(section 5) and gives FM back to ES1869.VXD (API 0103) as it is. To go back
to the emulation mode, write to FM_Base+0 (ESFMu), or reset the synthesizer
with Audio_Base+7 bit 5 as the Linux driver's `es1869_fm_reset()` does.

### Writing a register

`fm_write` (seg1:0010) writes a native register with three port writes and
reads of FM_Base+0 after each of them as a delay:
1. FM_Base+2 gets bits 7:0 of the register number, followed by 2 reads.
2. FM_Base+3 gets bits 15:8, followed by 6 reads.
3. FM_Base+1 gets the data, followed by 6 reads.

The 6 is `fm_delay` (DGROUP:001A), which nothing changes. The driver never
reads a register back, and it doesn't compare a register with what the chip
holds: each step below writes what it says, even a value that the chip
already has. The three port writes of a register must not mix with another
writer's, which is cause 3 of
[ESFM_MIDI.md](ESFM_MIDI.md#3-fm-register-writes-split-by-an-interrupt).
`es1869_fm_write()` in `linux/es1869/hw.c` paces its writes the same way
(verified).

### Registers

Each of the 18 voices has 20h registers from voice x 20h, eight for each of
its four operators, so register n (0-7) of operator k (0-3) of voice v
(0-17) is v x 20h + k x 8 + n. The operators of a voice form a chain:
operator 0 takes its own feedback as its modulation input, operator k takes
the output of operator k-1, and each operator adds its output to the mix at
its own output level (ESFMu). These are the fields of the eight registers
(ESFMu, `ESFM_slot_write`):
* **Register 0** holds the tremolo in bit 7, the vibrato in bit 6, the
  sustaining envelope in bit 5, the key scale rate in bit 4 and the
  frequency multiplier in bits 3:0.
* **Register 1** holds the key scale level (KSL) in bits 7:6 and the
  attenuation in bits 5:0, in 0.75 dB steps.
* **Register 2** holds the attack rate in bits 7:4 and the decay rate in
  bits 3:0.
* **Register 3** holds the sustain level in bits 7:4 and the release rate in
  bits 3:0.
* **Register 4** holds bits 7:0 of the F-number.
* **Register 5** holds the envelope delay after the key on in bits 7:5, the
  block in bits 4:2 and bits 9:8 of the F-number in bits 1:0.
* **Register 6** holds the deep tremolo in bit 7, the deep vibrato in bit 6,
  the outputs in bits 5:4 and the modulation input level in bits 3:1, where
  0 is none. Bit 5 sounds on the chip's left output and bit 4 on its right,
  the other way round from ESFMu and from ESS's pan (section 8). Bit 0 is
  unused in the native mode.
* **Register 7** holds the output level in bits 7:5, where 0 is silent, a
  noise mode that only operator 3 has in bits 4:3, and the waveform in bits
  2:0.

The key-on register of voice v (0-15) is 240h + v, whose bit 0 keys all four
operators on. Voices 16 and 17 have two each: 250h keys operators 0 and 1 of
voice 16 on and 251h its operators 2 and 3, and 252h and 253h do the same
for voice 17 (ESFMu). The driver writes 01h to key a voice on and 00h to key
it off, and for voice 16 or 17 it writes both registers, the lower one first
(`voice_on` seg1:049C, `voice_off` seg1:0146, verified). It writes no
register above 253h.

## 2. The patch bank

### Format

The bank starts with 256 little-endian 16-bit offsets from its start, and
the patches follow at 200h. Entries 0-127 are programs 0-127, and entries
128-255 are the drum notes 0-127 of channel 9. An offset of 0 means that the
program or drum is silent, and a note on for it is ignored. A patch is one
or two 36-byte *patch voices*, each a 4-byte header followed by four 8-byte
operator records, operator 0 first. ESS's driver doesn't check its bank. The
rebuilt driver takes a bank file only if it passes the checks of
`bank_check` in `src/esfmbank.c`: the 512-byte table, at most 7FF0h bytes,
and every nonzero offset at least 200h, with room for the whole patch.

Byte 0 of the header holds these fields:
* **Bits 7:4** make operators fixed-frequency, bit 4 + k for operator k. The
  operator's bytes 4 and 5 then go to registers 4 and 5 as they are, and
  neither the note nor the pitch bend changes them (seg1:0B44, 0830, 0A72
  and 0EF7, verified).
* **Bit 3** is unused, and it is 0 in every patch of ESS's banks.
* **Bits 2:1** say how the patch plays, and only the first patch voice's
  bits count (seg1:0C98, verified). With 0 it has one patch voice. With 1 it
  has two, and the second plays only if a chip voice is free for it. With 2
  both always play, and with 3 the entry is ignored.
* **Bit 0** keeps the patch voice off chip voices 16 and 17 (seg1:020C and
  03DA, verified). [ESFM_BANK.md](ESFM_BANK.md) lists this bit as unknown.
  Why ESS keeps some patch voices off voices 16 and 17 isn't known, because
  ESFMu models nothing special about them but their split key on.

Header bytes 1 and 2 are never read. Byte 3 holds a 2-bit *level mode* for
each operator, operator 0 in bits 1:0 up to operator 3 in bits 7:6, which
says how velocity and volume scale that operator (section 7).

Byte n of an operator record goes to the operator's register n:
* **Bytes 0, 2, 3 and 7** are written as they are, but register 7 is first
  written as 00h (section 4), and the rebuilt driver can add bit 6 to
  register 0 (section 9).
* **Byte 1** keeps its KSL in bits 7:6, and its attenuation in bits 5:0 is
  raised by velocity, volume and expression (section 7).
* **Bytes 4 and 5** of an operator that isn't fixed-frequency hold a pitch
  offset rather than a frequency (section 6). Bits 6:0 of
  `(byte5 << 2) | (byte4 & 3)` are a signed 7-bit number of semitones (-64
  to +63), bits 7:2 of byte 4 are a signed 6-bit detune (-32 to +31), and
  bits 7:5 of byte 5 are the envelope delay, which the driver keeps in
  register 5.
* **Byte 6** is written as it is, except that the pan replaces bits 5:4
  (section 8), and the rebuilt driver can add bit 6 (section 9).

### Loading

ESS's driver copies its bank from resource 256, ID 1234, into a global block
at `DRV_ENABLE` (`bank_load`, seg3:0662). The rebuilt driver's resource 1234
is `esfm_patch_banks/bnk_com_better_square_wave.bin`, which differs from
ESS's `bnk_com.bin` in program 80 only, and `BetterSquareWave=0` plays ESS's
bank, which it carries as resource 1235. With `Bank=` in `[ESFM.DRV]`, the
rebuilt driver plays a bank file instead, which it reads at `MODM_OPEN` when
the file has changed ([ESFM_BANK.md](ESFM_BANK.md#bank-file-buildesfmdrv)).
Every note on reads its patch from the bank, so a new bank plays from the
next note on (read, and `BankFileTest` in `tests/test_esfmdrv.py`).

## 3. The engine's state

Each channel has the state below, and a reset (`chip_reset`) sets it to the
value in the second column. The rebuilt driver adds the rows marked GM,
which `fix_gm_init` resets.

| State                | After a reset   | Changed by            |
|----------------------|-----------------|-----------------------|
| program              | kept            | Cn, a GM reset (0)    |
| pitch bend           | 2000h           | En                    |
| bend range           | 2 semitones     | RPN 0                 |
| RPN 0 selected       | no              | controllers 98-101    |
| sustain pedal        | up              | controller 64         |
| volume               | 100             | controller 7          |
| expression           | 127             | controller 11         |
| pan                  | center          | controller 10 (and 8) |
| GM: modulation       | 0               | controller 1          |
| GM: pressure         | 0               | Dn                    |
| GM: RPN selected     | none (7F7Fh)    | controllers 98-101    |
| GM: bend range cents | 0               | RPN 0                 |
| GM: fine tuning      | 2000h (in tune) | RPN 1                 |
| GM: coarse tuning    | 40h (in tune)   | RPN 2                 |

The programs start at 0 when the driver allocates its device structure, and
neither a reset nor close and open changes them (verified). The rebuilt
driver also keeps a master volume for the device, 0 after a reset.

Each chip voice has flags, where 1 is keyed on, 2 keyed off, 4 a note off
held by the sustain pedal and 8 the second patch voice of a patch of mode 1.
A voice also keeps the channel and the MIDI note it plays, before any tuning
or offset, header bytes 0 and 3 of its patch voice, and the *clock* value of
its last key on or key off. For each operator, it keeps the attenuation byte
after velocity (section 7), and the F-number and block before the bend
(section 6). The rebuilt driver also keeps bytes 0 and 6 of each operator's
record.

The clock is a 32-bit counter of the device that counts every key off and
every chip voice programmed. The *age* of a voice is the clock minus the
voice's clock value, modulo 2^32. In the device structure
(`src/esfm/esfmdev.inc`), the clock is at 1Ch and the voices at 70h, 21h
bytes each, with the flags at +0, the clock at +1, the channel at +5 and the
note at +6. essctl reads the voices there, and the rebuilt driver's
`ESFMFIX` block changes the engine only through `fix_opts`, its switches.

### Reset

`chip_reset` (seg1:009A) runs at `MODM_OPEN` (seg3:01D7), at `MODM_RESET`
(`midiOutReset`, seg1:1736), at power resume (seg3:03EC) and at
`DRVM_DISABLE` (seg3:05AD), where the driver takes FM for the reset and
gives it back. It enters the native mode, writes 00h to registers 000h-253h
in that order, which keys every voice off and silences every operator, and
sets the channels to the values above. It sets the flags and clock values of
the voices to 0, leaving their channel and note, and the clock to 0
(verified).

## 4. Note on

`short_msg` (seg1:110C) masks both data bytes with 7Fh, and a note on with
velocity 0 is a note off (seg1:115A). `note_on` (seg1:0C48) then plays the
note in the steps below.

### The patch

Channel 9 uses entry 128 + note, whatever its program, and every other
channel uses the entry of its program. The note is ignored when the entry's
offset is 0 or the mode in bits 2:1 of header byte 0 is 3 (seg1:0C50-0CAE,
verified). A program change only stores the program (seg1:13C0), so notes
that sound keep their patch.

### Free voices

`find_voices` (seg1:020C) goes through chip voices 0-15, then 16 and then
17, and does two things with each. If the voice is keyed on with the same
channel and note, including a voice held by the pedal, it keys the voice off
with `voice_off`, so a repeated note never stacks voices. Then, if the
voice's flag 1 is clear, the voice is free and competes with its age, taken
from the clock as it is at that moment, so a key off earlier in the same
pass already counts. A free voice whose age is at least the best one's takes
the first place and moves the best one to the second place, and one whose
age is only at least the second's takes the second place. The first place is
for the first patch voice and the second place for the second.

Voices 16 and 17 follow bit 0 of the patch voices' headers. For the first
place, voice 16 or 17 counts only if the first patch voice's bit 0 is clear,
and for the second place only if the second patch voice's is clear. When
voice 17 takes the first place from voice 16 and the second patch voice may
not use voice 16, the second place stays as it was (seg1:03A1).

So after a reset, when every age is equal, notes take the voices from 17
down to 0, or from 15 down to 0 for a patch voice with bit 0 set. The voice
keyed off last has the smallest age and is taken last, so a release goes on
as long as possible (verified).

### Stealing

`steal_voice` (seg1:03DA) takes a voice when no free one was found for the
first patch voice, and in mode 2 also for the second. Its candidates are
voices 0-15 when that patch voice's bit 0 is set and voices 0-17 when it is
clear. It goes through them in order and keeps one choice:
* A voice with flag 8, the optional second patch voice of a mode 1 patch,
  always beats a voice without it, and once one is chosen, only voices with
  flag 8 compete.
* Among voices of the same kind, the higher priority wins. Channel 9 has
  priority 1 and every other channel c has c + 2, so channel 15 loses its
  voices first and the drums last.
* With equal priority, the larger age wins, which for a keyed voice is the
  earlier key on, and with equal age the voice chosen first stays.

The chosen voice is keyed off with `voice_off` and then used (verified).

### Programming a chip voice

`program_voice` (seg1:0B10) programs operators 0-3 in order with
`program_operator` (seg1:0808), and each operator gets nine writes: register
7 = 00h, which takes the operator's output out of the mix while it changes,
and then registers 0, 1, 2, 3, 4, 5, 6 and 7 with the values of sections 6
to 8. Then the voice's flags become 1, its clock value becomes the clock,
the clock goes up by 1, and the voice keeps its channel, note and header
bytes 0 and 3 (verified).

### Keying on

The key ons come after all of a note's voices are programmed (seg1:0CB1,
0D02, 0D99, verified):
* **Mode 0.** The voice of the first place, or a stolen one, is programmed
  and keyed on.
* **Mode 1.** The first patch voice is programmed into the first place or a
  stolen voice. If the second place has a voice, the second patch voice
  (header + 24h) is programmed into it, gets flag 8 and is keyed on. The
  first patch voice is keyed on last.
* **Mode 2.** Both patch voices are programmed, each into its place or a
  stolen voice, and the first is keyed on before the second.

This is ESS's bank's program 0 (piano), note 60, velocity 127, right after a
reset. Its first patch voice goes to chip voice 15 (registers 1E0h-1FFh),
the second to voice 14, and then 24Eh = 01h and 24Fh = 01h key them on:

| Voice 15   | 0   | 1   | 2   | 3   | 4   | 5   | 6   | 7   |
|------------|-----|-----|-----|-----|-----|-----|-----|-----|
| operator 0 | 00  | 61  | 56  | F6  | AE  | 0E  | 30  | 00  |
| operator 1 | 03  | C4  | B3  | B6  | AD  | 0E  | 18  | 60  |
| operator 2 | 01  | 9B  | F3  | C6  | AE  | 0E  | 2A  | E0  |
| operator 3 | 01  | 0D  | E3  | F6  | AE  | 0E  | 3E  | C0  |

Operator 3's attenuation is the patch's 07h plus 6 for volume 100, operator
1's F-number is 2ADh because of its detune of -4, and the other operators
have no level mode, so velocity and volume leave them alone.

### Drums

Channel 9 differs from the other channels in three ways only: its patch is
entry 128 + note, its program changes are stored but unused, and it has the
lowest priority when voices are stolen. The rebuilt driver also leaves out
its coarse tuning. Everything else applies to it as to any channel: the
pedal, volume, pan and pitch bend, and the note off by channel and note.
ESS's bank has drum patches for notes 35-81 only. Most of their operators
are fixed-frequency, so they sound the same whatever the note and bend, but
an operator that isn't follows the note number and the bend as on any other
channel, like operators 0 and 1 of note 49, the crash cymbal (verified).

## 5. Note off, the pedal and all notes off

`note_off` (seg1:01BE) looks at every keyed voice with the channel and note.
While the channel's pedal is down, the voice gets flag 4 and keeps sounding,
and otherwise `voice_off` keys it off. The release velocity is ignored.
`voice_off` writes the key off, sets the flags to 2 alone, stores the clock
value and adds 1 to the clock (verified).

Controller 64 (`sustain`, seg1:0E46) puts the pedal down at 64-127. At 0-63
it lets the pedal up and keys off every voice of the channel with flag 4
(verified).

The channel mode controllers act on the channel's keyed voices, and their
value doesn't matter (verified):
* **120, 126 and 127** (seg1:12FB) key off every one of them, pedal or not.
* **123, 124 and 125** (seg1:1393) key off the ones without flag 4. With the
  pedal down, a note still held by its key stops, while a note held by the
  pedal alone sounds on. The MIDI spec has both sound on until the pedal
  goes up.

ESS's controller 121 (seg1:1324) first keys off the voices held by the
pedal, if it is down. It then sets volume 100, expression 127, the pedal up,
the bend to 2000h, the pan to the center and the bend range to 2, without
writing a register. Notes that sound keep their bend and level until a later
message rewrites them, and the RPN selection stays (verified). The rebuilt
driver's controller 121 is in section 9.

At `MODM_CLOSE` (seg3:028E) and at power suspend (seg3:03AD),
`all_notes_off` (seg1:011C) calls `note_off` with the channel and note of
every chip voice in turn, so the voices held by a pedal keep sounding after
the close. The rebuilt driver keys every voice off instead (section 9).

The engine ignores polyphonic key pressure, system messages and every
controller that this page doesn't name, such as bank select (0 and 32),
portamento (5 and 65), sostenuto (66), soft (67), the effect depths (91 and
93), data increment and decrement (96 and 97) and local control (122). ESS's
driver also ignores channel pressure (seg1:1130-1157 and 1178-11E4,
verified).

## 6. Pitch

### The note's block and F-number

`program_operator` computes the pitch of each operator that isn't
fixed-frequency in these steps (seg1:0862-0A70, verified):
1. Start from the note. The rebuilt driver adds the channel's coarse tuning
   minus 40h first, except on channel 9 (`fix_coarse`).
2. Add the operator's signed semitone offset (section 2).
3. Fold the note into 19-114: below 19 it goes up by 12 x floor((30 - note)
   / 12), and above 114 it goes down by 12 x floor((note - 103) / 12). A
   note that leaves this range keeps its pitch class but not its octave, so
   at the ends of the keyboard the operators of a patch lose their octave
   offsets.
4. The block is floor((note - 19) / 12), 0-7, and the semitone s is (note -
   19) mod 12, where s = 0 is G.
5. The F-number is `FNUM[s]` plus e for the operator's detune d (-32 to
   +31), where e = floor(`RATIO[s]` x d / 256), and for a block above 1, e
   becomes floor(e / 2^(block - 1)).
6. The voice keeps this F-number and register 5's delay and block for the
   bend, and the bend (below) gives the F-number and block written.
7. Register 4 gets bits 7:0 of the F-number, and register 5 gets the patch's
   delay (byte 5 bits 7:5), the block in bits 4:2 and bits 9:8 of the
   F-number.

```
FNUM:  202h 220h 241h 263h 287h 2AEh 2D7h 302h 330h 360h 394h 3CAh
RATIO: 256  242  228  215  203  192  181  171  161  152  144  136
```

`FNUM` is at seg1:0082, and `RATIO`, at DGROUP:0024, is round(256 x 2^(-s /
12)). Note 60 is block 3 and s = 5, so its F-number is 2AEh. From block 1
up, the division makes a detune the same number of hertz in every octave,
and `RATIO` makes it smaller from G up to F# within each octave.

The frequency of an operator is F-number x f x 2^(block - 20) times its
multiplier (register 0 bits 3:0, where 1 is x1), and f is the chip's output
rate. `FNUM` matches equal temperament within 1.1 cents at f = 50000 Hz: it
is AdLib's table. At an OPL3's 49716 Hz (14.31818 MHz / 288) every note is 9
to 11 cents flat of A4 = 440 Hz. The ES1869's FM runs at its 23.814 MHz
master over 480, 49612.5 Hz, which essprobe measured in DOS and es1869test's
`clock.fm` measures through the record mixer against the chip's 793.8 kHz
clock, so on the chip ESS's notes play 12.5 to 14.6 cents flat. Note 60 of
GM program 81, whose operators are detuned, reads 259.3 Hz, 15.7 cents flat.

The Linux driver's `esfm_a440`, on by default, takes the F-numbers of A4 =
440 Hz on that clock in place of `FNUM`, 440 x 2^((n - 69) / 12) x 2^20 /
49612.5 for notes 19 to 30, as ESFM.DRV's OPL3 device does:

```
A440:  518  549  581  616  652  691  732  776  822  871  923  977
```

Every note is then within 1.3 cents of equal temperament, and the rest of
the arithmetic, the detune and the bend, is ESS's.

### Pitch bend

The bend is the 14-bit value of `En`, data 2 x 128 + data 1 (seg1:13D2), and
R is the channel's bend range in semitones. `calc_pitch` (seg1:04EE) and
`calc_block_fnum` (seg1:07C6) bend an F-number F of block b like this
(verified):
1. If the bend is 2000h, F stays, which steps 3-5 would give too.
2. A bend of 3F80h-3FFFh counts as 4000h, a whole range up.
3. t = floor((bend - 2000h) x R / 32), the offset in 1/256 semitones.
4. u = t + 6144, i = floor(u / 256) and j = floor(u / 4) mod 64.
5. ratio = floor(`SEMI[i]` x `FINE[j]` / 1024), and F becomes floor((ratio x
   F + 512) / 1024).
6. While F is 400h or more, F is halved (rounding down) and b goes up by 1.
   If b is then above 7, b becomes 7 and F stays halved.

```
SEMI: 0100h 010Fh 011Fh 0130h 0143h 0156h 016Ah 0180h 0196h 01AFh 01C8h
      01E3h 0200h 021Eh 023Fh 0261h 0285h 02ABh 02D4h 02FFh 032Dh 035Dh
      0390h 03C7h 0400h 043Dh 047Dh 04C2h 050Ah 0557h 05A8h 05FEh 0659h
      06BAh 0721h 078Dh 0800h 087Ah 08FBh 0983h 0A14h 0AAEh 0B50h 0BFDh
      0CB3h 0D74h 0E41h 0F1Ah 1000h
FINE: 0400h 0401h 0402h 0403h 0404h 0405h 0406h 0406h 0407h 0408h 0409h
      040Ah 040Bh 040Ch 040Dh 040Eh 040Fh 0410h 0411h 0412h 0413h 0414h
      0415h 0415h 0416h 0417h 0418h 0419h 041Ah 041Bh 041Ch 041Dh 041Eh
      041Fh 0420h 0421h 0422h 0423h 0424h 0425h 0426h 0427h 0428h 0429h
      0429h 042Ah 042Bh 042Ch 042Dh 042Eh 042Fh 0430h 0431h 0432h 0433h
      0434h 0435h 0436h 0437h 0438h 0439h 043Ah 043Bh 043Ch
```

`calc_pitch` builds both tables on its stack. `SEMI[i]` is round(1024 x
2^((i - 24) / 12)), the ratio of -24 to +24 semitones, and `FINE[j]` is
round(1024 x 2^(j / 768)), steps of 1/64 semitone. The bend therefore has a
resolution of 1/64 semitone. With bend 3000h and range 2, note 60's 2AEh
becomes 2D7h, the F-number of the next semitone.

With a range above 24, i passes 48 and ESS's driver reads past `SEMI` into
its stack frame, which gives a wrong pitch (read). Step 6 keeps the block at
7, so a note that a bend takes above block 7 sounds an octave low: note 114
bent up two semitones needs block 8, which the chip doesn't have, and plays
F-number 220h in block 7 (verified).

A pitch bend message (`pitch_bend`, seg1:0E94) stores the channel's bend and
bends every voice of the channel whose flag 1 is set, the voices held by the
pedal included. For each operator that isn't fixed-frequency, it bends the
F-number and block that the voice kept at its note on and writes register 5
and then register 4, the reverse of the note on's order. Keyed off voices in
their release don't follow the bend (verified).

### Bend range in ESS's driver

ESS's driver knows RPN 0 only, in semitones (seg1:11E7-12F8, verified). The
channel has two bits: controller 101 at 0 sets the first and at 1-127 clears
it, controller 100 does the same with the second, and controllers 99 and 98
clear the first and the second. Controller 6, data entry, sets the bend
range to its value when both bits are set, without a check, and controller
38 is ignored. A new range applies from the next bend or note, and the
selection stays after the data entry.

### Tuning in the rebuilt driver

With `Tuning=1`, the default, `fix_control` handles controllers 6, 38 and
98-101 instead of ESS's code (verified):
* Controller 101 sets the selected RPN's MSB and controller 100 its LSB, and
  98 or 99 selects none (7F7Fh), which a reset and controller 121 do too.
* Data entry with controller 6 sets the bend range to its value and the
  cents to 0 for RPN 0, the fine tuning to value x 128 for RPN 1, and the
  coarse tuning to its value for RPN 2.
* Controller 38 sets the cents of the bend range for RPN 0, and bits 6:0 of
  the fine tuning for RPN 1.
* With another RPN, an NRPN or none selected, data entry does nothing.
* A change of RPN 0 or 1 bends the channel's sounding notes again with the
  channel's bend. The coarse tuning moves only the next notes (step 1 of the
  note's pitch).

`fix_calc_pitch` replaces step 3 of the bend. With C the range in cents, the
range x 100 plus the cents but at most 12700, and the bend already moved to
4000h by step 2:
1. t = trunc((bend - 2000h) x C / 3200) + floor((fine - 2000h) / 32), where
   trunc rounds toward 0.
2. t is clamped to -6144..6143, 24 semitones each way.
3. From 6048 up, t becomes 6144, exactly 24 semitones up.

Steps 4-6 then follow with this t. The code gets there by handing
`calc_pitch` a range of 24 and a bend of 2000h + trunc((4t + 2) / 3), or
trunc(4t / 3) for a negative t, which gives t back for every t, except that
from 6048 up the bend reaches 3F80h (verified, and the round trip computed
for every t).

## 7. Level

Register 1 of an operator comes from its byte 1 in two steps. The 2-bit
level mode m of the operator, from header byte 3, picks the curve of both,
with this function of a MIDI value x of 0-127:
* **m = 0** gives 0, so neither velocity nor volume changes the operator.
* **m = 1** gives floor((127 - x) / 16), 0-7.
* **m = 2** gives floor((127 - x) / 8), 0-15.
* **m = 3** gives floor((127 - x) / 4) from 64 up, 0-15, and floor((63 - x)
  / 2) + 16 below 64, 16-47.

This table shows the attenuation that each curve adds, in steps of 0.75 dB:

| x     | 127 | 112 | 100 | 96  | 80  | 64  | 63  | 48  | 32  | 16  | 1   |
|-------|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|
| m = 1 | 0   | 0   | 1   | 1   | 2   | 3   | 4   | 4   | 5   | 6   | 7   |
| m = 2 | 0   | 1   | 3   | 3   | 5   | 7   | 8   | 9   | 11  | 13  | 15  |
| m = 3 | 0   | 3   | 6   | 7   | 11  | 15  | 16  | 23  | 31  | 39  | 47  |

At the note on, the velocity raises the attenuation, the patch's bits 5:0,
by the curve of the velocity, up to 3Fh, and the KSL bits stay
(seg1:08BF-0927). The voice keeps this byte. The volume and expression then
add the curve of the volume plus the curve of the expression, again up to
3Fh (`calc_level`, seg1:0F98), and that goes to register 1. In mode 0 they
add nothing. While the channel's volume is 0, though, register 1 of every
operator of the channel is 3Fh, whatever its mode, and its KSL bits are 0
too (seg1:0FAA). The rebuilt driver adds its master volume after the volume
and expression for modes 1-3 (`fix_master`, section 9). All of this was
verified, every velocity and every volume with six expressions on a test
bank.

Controllers 7 and 11 store the value and then rewrite register 1 of all four
operators of every keyed voice of the channel, in voice order, from the byte
each voice kept (`update_volume`, seg1:1068, verified). A voice in its
release keeps its level. `midiOutSetVolume` doesn't change the operators:
the driver hands the volume to the callback it gets through ES1869.VXD
(seg1:1785, `DEV_MIXER_CB`), which sets the FM volume of the mixer
([ESFM_GM.md](ESFM_GM.md#master-volume), read).

The level mode, not the chip's algorithm, decides which operators volume
scales, and the patch's author set it. In ESS's bank, 463 of the 864
operators have a level mode. 104 operators that sound have none, so velocity
and volume leave them alone, and 29 that don't reach the output have one, 19
of which modulate the next operator, so that velocity and volume change the
timbre there. Controller 7 also stores an entry of a table at seg1:0062 in a
field of the channel (2C2h), but nothing reads that field.

## 8. Pan

Controller 10 sets the channel's pan, and so does controller 8 in ESS's code
(seg1:123F, verified). A value above 80 pans right (20h), below 48 left
(10h), and 48-80 to the center (30h). At the note on, an operator whose byte
6 has an output, bits 5:4 other than 0, gets the pan in bits 5:4 of register
6 when the pan isn't the center (seg1:0AB8, verified). In the center,
register 6 keeps the patch's outputs, so a patch that sends an operator to
one side keeps it there, and an operator without an output stays without
one.

ESS's values take bit 4 for the left and bit 5 for the right, as ESFMu does,
but the chip plays them the other way round. On the Armada's ES1869,
recorded digitally (mixer 7Fh bit 4) and through the output, bit 4 of
register 6 sounds on the right output and bit 5 on the left, and so do bits
4 and 5 of C0h-C8h in the OPL3 mode, an OPL3's channels A and B. So ESS's
driver plays a channel panned left on the right output, and what a patch
sends left of its own, such as drum 41, the low floor tom, on the right too.
The rebuilt driver's `TrueSides` and the Linux driver's `fm_sides` swap the
two bits in each write that sends an operator to one side only, the pan's
and the patch's own alike ([LINUX.md](LINUX.md#midi)).

ESS's driver pans only the next notes. The rebuilt driver also pans the
sounding notes (section 9).

## 9. The rebuilt driver

Each change of `build/ESFM.DRV` can be turned off with its key under
`[ESFM.DRV]` in SYSTEM.INI, which the driver reads at its first `DRV_ENABLE`
(`fix_read_settings` in `esfmini.asm`). A missing key is 1, and 0 gives back
ESS's behavior in that place
([DRIVER_CONFIG.md](DRIVER_CONFIG.md#63-esfmdrv)).

### Modulation and channel pressure (Vibrato)

Controller 1 and channel pressure (`Dn`, `fix_pressure`) each store their
value, and the larger of the two sets the channel's vibrato (`fix_vibrato`,
verified):
* At 0 the vibrato is off.
* At 1-63 it is on: bit 6 of register 0 is set in every operator.
* At 64-127 it is deep: bit 6 of register 6 is set too.

The bits are ORed into the patch's bytes, so a patch's own vibrato stays.
When the channel's vibrato changes, `fix_refresh` writes registers 0 and
then 6 of each operator of every keyed voice of the channel again, from the
patch's bytes with the vibrato and the pan. New notes get the bits in
`program_operator` (`fix_op_reg0`, `fix_op_reg6`). The chip's vibrato is
about 6 Hz, 7 or 14 cents deep ([ESFM_GM.md](ESFM_GM.md)). With `Vibrato=0`,
controller 1 and pressure are ignored.

### Pan of the sounding notes (LivePan)

Controllers 8 and 10 set the pan as in section 8, and when the channel's pan
position changes, `fix_refresh` rewrites the sounding notes as above. A
value in the position that the channel already has writes nothing
(verified). With `LivePan=0`, ESS's code pans the next notes only.

### Tuning (Tuning)

Section 6 describes the RPNs. With `Tuning=0`, ESS's RPN 0 handling and bend
apply, and a range above 24 reads past `SEMI` again.

### Controller 121 (ResetControllers)

`fix_ctl_reset` resets what RP-015 lists, in this order (verified):
1. The pedal goes up as with controller 64 at 0, which keys off the voices
   it holds.
2. Expression 127, modulation 0, pressure 0, no RPN selected.
3. `update_volume` rewrites register 1 of the channel's keyed voices.
4. `fix_vibrato` turns the vibrato off on them if it was on.
5. The bend goes to 2000h, and `pitch_bend` rewrites registers 5 and 4.

Volume, pan, the bend range and the tuning stay. With `ResetControllers=0`,
ESS's controller 121 applies (section 5).

### Program change (PedalRelease)

After storing the program, `fix_program` lets the channel's pedal up as
controller 64 at 0 would, if it is down, which keys off the voices it holds
(verified). A pedal that is still down when a channel changes instrument is
left over from an earlier part, and on FM it holds notes forever
([ESFM_MIDI.md](ESFM_MIDI.md#4-a-sustain-pedal-left-down-found-on-the-hardware)).
With `PedalRelease=0`, the pedal stays down.

### Resets and the master volume (SysEx)

`fix_process` looks through a long message for these before ESS's parser
plays it (verified), where `dd` is any device number and `n` any value:
* GM System On, `F0 7E dd 09 01 F7`, and GM2 System On, `F0 7E dd 09 03 F7`.
* The GS reset, `F0 41 dd 42 12 40 00 7F 00 41 F7`.
* XG System On, `F0 43 1n 4C 00 00 7E 00 F7`.
* The master volume, `F0 7F dd 04 01 ll mm F7`.

A reset (`fix_gm_reset`) sends controllers 121 and 123 to each channel in
turn, which lets the pedal up and keys off all of the channel's voices. It
then sets the channel's program to 0, volume 100, pan center, bend range 2
semitones and 0 cents, and both tunings in tune, without writing a register,
and after the last channel the master volume to full. With
`ResetControllers=0`, controller 121 is ESS's, so modulation, pressure and
the RPN selection stay through a reset.

The master volume takes v = mm x 128 + ll. The device's master attenuation
becomes `MASTER[floor(v / 512)]`, in steps of 0.75 dB, and `update_volume`
rewrites register 1 of every keyed voice of every channel
(`fix_master_volume`). Each value is round(40 log10(127 / (4i + 3)) / 0.75)
up to 63, GM's volume curve for the top value of each group of four:

```
MASTER: 63 63 57 49 44 40 36 33 30 27 25 23 21 19 18 16
        15 13 12 11 10  9  8  7  6  5  4  3  2  2  1  0
```

The master attenuation adds to modes 1-3 only, like the volume. With
`SysEx=0`, the driver ignores all of these messages, as ESS's parser skips
every SysEx byte.

### Close and suspend (SilenceOnClose)

At close and power suspend, `fix_all_off` lets every channel's pedal up and
keys off all 18 voices with `voice_off`, each of which writes its key off
(verified). With `SilenceOnClose=0`, ESS's `all_notes_off` of section 5
applies, and voices held by a pedal keep sounding.

### One message at a time (QueueWhileBusy, RunningStatus)

ESS's driver refuses a message that arrives while it handles another one,
and a lost note off hangs. The rebuilt driver queues such a message and
plays it before the call that holds the driver returns, so the engine always
handles one message to the end before the next one starts
([ESFM_MIDI.md](ESFM_MIDI.md#the-fix-buildesfmdrv)). An implementation that
receives events from several sources needs the same: hold a lock across all
the register writes of one event. `QueueWhileBusy=0` gives back the
refusals. `RunningStatus` changes only the parser of the raw MIDI bytes,
which an implementation fed with decoded events doesn't need. With
`RunningStatus=0`, a message in running status in a long buffer ORs its data
bytes into the last message's, and a real-time byte becomes the running
status ([ESFM_GM.md](ESFM_GM.md#running-status)).

### Sides (TrueSides)

`TrueSides=1` swaps bits 4 and 5 in each write of an operator's register 6
that sends it to one side only (`fix_fm_value`), so that ESS's left, the
pan's and a patch's own, plays on the chip's left output (section 8). The
Linux engine's `sides` makes the same writes, which `test_true_sides` in
`tests/test_esfmgm.py` checks byte for byte.

### The other switch

`BetterSquareWave=0` plays ESS's bank, as section 2 describes. With every
key at 0, the rebuilt driver writes the ports exactly as ESS's driver does,
which `SettingsTest` in `tests/test_esfmdrv.py` checks.

## 10. Timing

The engine has no timer of its own. It does all its work while it handles a
message, and it never waits except for the status reads of each register
write. A register write is 17 port accesses, 3 writes and 14 reads, so on an
ISA bus, where an access takes about 1 us, it takes about 17 us (estimated,
not measured).

A note on of a one-voice patch writes 37 registers, 36 for the operators and
1 for the key on (2 on voice 16 or 17), and a two-voice patch 74, besides
the key offs before them. A pitch bend writes 2 registers for each operator
that isn't fixed-frequency in each keyed voice of the channel, and a volume
or expression change 4 for each keyed voice.

Nothing waits between a key off and a key on. A repeated note keys its old
voice off in `find_voices` and goes to another free voice if there is one,
because the old one has the smallest age. A stolen voice is keyed off by
`steal_voice` and keyed on again at least 36 register writes later, about
0.6 ms on ISA. While each of its operators is written, register 7 = 00h
takes that operator out of the mix, and at the key on its envelope starts
its attack from the level that the release reached (ESFMu). An envelope
delay d, register 5 bits 7:5, holds the operator's envelope back after the
key on for 256 x 2^d samples of the chip, about 10 ms to 0.66 s at 49612.5
Hz (ESFMu), and the driver keeps the delay in every bend.

## 11. ESS's behavior that the rebuilt driver keeps

These are kept in both drivers, because the rebuilt driver changes only what
ESFM_MIDI.md and ESFM_GM.md describe. A new engine can keep them to sound
the same, or fix them:
* **A two-voice patch on one voice.** In mode 2, `steal_voice` doesn't skip
  the chip voice of the first patch voice, and a voice that it stole keeps
  its channel and so its priority, while its flag 8 is gone. When no voice
  has flag 8 and that chip voice has the highest priority, the second steal
  takes it again. Both patch voices are then programmed into one chip voice,
  which is keyed on twice, and only the second patch voice sounds. With 17
  voices on channel 0, a free voice last used by channel 15 and a mode 2
  patch on channel 1, ESS's driver did exactly that (verified). Skipping the
  first place's voice in the second steal fixes it.
* **An octave low at the top.** A bend above block 7 drops an octave, and
  the folding of notes above 114 drops the operators' octave offsets
  (section 6, verified).
* **All notes off with the pedal down.** Controllers 123-125 stop the notes
  that keys still hold and keep those that the pedal holds, and 126 and 127
  act as all sound off (section 5, verified).
* **Volume 0** sets register 1 of every operator of the channel to 3Fh,
  modulators included, and loses the KSL bits until the volume comes back
  (section 7, verified).
* **Tuning.** The F-numbers are 9 to 11 cents flat at 49716 Hz, and 12.5 to
  14.6 cents flat at the chip's 49612.5 Hz (section 6). The Linux driver's
  `esfm_a440` tunes them.
* **Pan.** Left and right are the other way round on the chip (section 8),
  unless `TrueSides` or the Linux driver's `fm_sides` turns them round.

## 12. Open points

These weren't settled:
* What bit 0 of a patch voice's header is for. The code only keeps the patch
  voice off voices 16 and 17, and nothing in ESFMu makes those voices
  different in the native mode but their split key on.
* Whether the chip needs the 2 and 6 status reads of `fm_write`. They are
  ESS's values, and nobody tried fewer on the hardware.
* The meaning of the register bits comes from ESFMu, not from ESS, whose
  data sheet doesn't describe the native mode, apart from register 6's
  outputs, which the hardware plays the other way round (section 8).
* The long-message parser, the queue, the bank file and power suspend were
  read and are covered by `tests/test_esfmdrv.py`, but the model here didn't
  run them, apart from the SysEx messages, `midiOutReset` and close.
