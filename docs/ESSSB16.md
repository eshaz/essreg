# esssb16: a Sound Blaster 16 for DOS games

This page is the guide to `build/esssb16.com`: what it does, the memory
managers it needs, its options and limits, and how to install it.
[SB16.md](SB16.md) has the Sound Blaster 16 it answers as and how it is
tested.

## What it does

A Sound Blaster 16 in front of the ES1869 for DOS games, in DOS itself. The
chip answers a DOS program as a Sound Blaster Pro, version 3.01, and plays
16-bit sound only in ESS's own Extended mode, so a game written for the
Sound Blaster 16 plays 8-bit sound on it at best. `esssb16` stays resident,
in upper memory, which it moves itself into, and answers the game as a Sound
Blaster 16 does, DSP version 4.05 with the CT1745 mixer, command for command
([SB16.md](SB16.md)). It works like the Windows driver's Sound Blaster 16:
* Every transfer plays or records in ESS's Extended mode, at the rate the
  game asked for, up to 48 kHz: 8-bit sound and recordings on Audio 1, and
  16-bit sound on Audio 2, so both play at once, as on the card. The game's
  DSP reset never reaches the chip, which it would take out of Extended
  mode: `esssb16` answers it itself.
* The chip takes one byte per DMA cycle whatever the channel (DS p.14), so
  16-bit sound takes two bytes a sample on an 8-bit channel. When the game
  programs its 16-bit channel, the `H` of `BLASTER`, `esssb16` carries the
  programming over to Audio 2's channel, in bytes, and the count that the
  game reads moves with the sound. Where Audio 2 has no 8-bit channel of its
  own, 16-bit sound plays on Audio 1, one transfer at a time. A game that
  picks the 8-bit channel for 16-bit sound through mixer 81h, as a Sound
  Blaster 16 allows, programs that channel itself.
* Each start of Audio 1 keeps its DMA channel masked for a moment: the chip
  loses a byte that comes right at its start, and 16-bit sound would play a
  byte off, as noise
  ([ESS_EXTENDED.md](ESS_EXTENDED.md#a-16-bit-stream-a-byte-off)). The
  game's own channel is masked only where the chipset reads its mask back,
  as Intel's PIIX4 does, and the channel is unmasked. On the card,
  `sb16test`'s recorded run then played every 16-bit sound clean. A pause
  (D0h, D5h) takes the FIFO off the DAC before it stops the DMA, so that the
  sound goes on in step after the continue (D4h, D6h).
* The direct DAC and ADC, ADPCM, silence and MIDI, which Extended mode
  lacks, go to the chip in its Compatibility mode.
* The Sound Blaster 16's mixer registers, 30h to 47h, show the levels the
  chip has when `esssb16` loads, also those a mixer program set, and set the
  chip's levels and record source. The Sound Blaster Pro's registers are
  views of them, as on the CT1745, which has no 0Ch or 0Eh. A mixer reset
  sets the Sound Blaster 16's levels and leaves the chip's other settings,
  such as the 3-D effect. Registers 80h to 82h give the card's IRQ, its
  channels and which transfer's interrupt is pending.
* A program that uses the chip's Extended mode itself, such as `essreg`,
  gets the chip as it is, with ESS's own commands and mixer, until its next
  reset. One that asks for the chip's identification with E7h finds a Sound
  Blaster 16, which has no E7h.

In a Windows DOS box the rebuilt
[`ES1869.VXD`](../README.md#es1869vxd-with-a-register-interface-and-better-dos-boxes)
does the same, and `esssb16` refuses to load there.

Nothing in DOS sees a port access unless the processor traps it, so
`esssb16` needs a memory manager that traps ports for it:
* MS-DOS's `EMM386.EXE` 4.46 or later, the one of MS-DOS 6.22 and of Windows
  95 and 98, with or without `NOEMS`, through its port trap interface (int
  2Fh 4A15h, as [SoftMPU](https://github.com/bjt42/softmpu) uses it);
* QEMM 7.03 or later, through
  [QPI](https://www.cs.cmu.edu/~ralf/papers/qpi.txt);
* [JEMM386 or JEMMEX](https://github.com/Baron-von-Riedesel/Jemm) with
  `QPIEMU.DLL` loaded (`JLOAD QPIEMU.DLL`), which gives JEMM the same
  interface.

These trap the accesses of real-mode games. A protected-mode game, such as
one with DOS/4GW, runs under a DPMI host and leaves the memory manager's
traps behind. For those, load HDPMI32i resident before `esssb16`: it is
[HX](https://github.com/crazii/HX)'s DPMI host built to trap ports for
[SBEMU](https://github.com/crazii/sbemu), and `esssb16` registers its traps
with it too. Without it, protected-mode games play 8-bit sound, as before.

As it loads, `esssb16` prints the card it found and the `BLASTER` line to
set: the base, the IRQ and the 8-bit channel as the card's PnP registers
have them, and `H5` for the game's 16-bit channel, with Audio 2's channel
that plays it. It says which memory manager traps for it, with EMM386's
version, and when none does, why: no EMM386 device, one older than 4.46, or
one that refused the traps. [Installing esssb16](#installing-esssb16) below
has the steps. The options:
* `/A220`: the card's base, otherwise `BLASTER`'s `A`, or 220h.
* `/H5`, `/H6` or `/H7`: the game's 16-bit channel, otherwise `BLASTER`'s
  `H`, or 5. `/H0` offers none: the game then plays 16-bit sound through the
  8-bit channel, as a Sound Blaster 16 does without an `H`. With `esssb16`
  in memory already, `/H` gives that copy the channel instead, as a game
  picks it through mixer 81h, so that a game can be tried both ways without
  a restart; the other options take a restart. Run again, with or without
  `/H`, the copy also says the last buffer that it carried over from the
  game's 16-bit channel to an 8-bit one since it last said one, and whether
  the buffer crosses a 64 KB page.
* `/20`, `/21` or `/23`: Audio 2's DMA channel, otherwise the card's, if a
  DMA pin of the card is wired to it. `/2-` plays 16-bit sound on Audio 1.
* `/S`: single DMA transfers instead of demand transfers of two bytes, ESS's
  own *Single Mode DMA* setting, if sound breaks up.
* `/F0` or `/F1`: the DACs' filters off or on (mixer 71h bits 2 and 3) at
  each transfer. Without it they stay as they are.
* `/O0` or `/O1`: Audio 2's 4x oversampling off or on (mixer 71h bit 4) at
  each transfer. Without it, it stays as it is.
* `/3`: the 3-D effect kept as Windows left it. Without it, `esssb16` turns
  it off, since a Sound Blaster 16 has none.

The exit codes are 1 for a bad option, 2 when no ES1869 answers at the base,
3 without port traps, 4 in a Windows DOS box and 5 when it is loaded
already. The limits: EMM386 doesn't trap the DMA controllers, so under it
the game must program its 16-bit channel before the transfer command, as
Creative's guide says, and its count stands still, but where Audio 2's DMA
channel (PnP 75h) is the game's 16-bit one, as on the card of the reports
with `H5`. There Audio 2 runs on that channel, muted, while Audio 1 plays
the sound, so that the count moves as on a Sound Blaster 16 (the pace), and
`esssb16` says so as it loads. A game that mixes up to that count, such as
Pinball World's driver, plays then, as on the laptop of the reports, and
elsewhere repeats its first sound. Such a game has to be set up with the
same channel: Pinball World's `SETSND` keeps the DMA that it saved before,
and with DMA 1 there while `esssb16` has 5, it repeated what channel 5 last
played. Pinball Fantasies' Sound Blaster 16 driver, which Pinball Dreams and
Pinball Dreams II share, programs its channel only after the command, so
under EMM386 it plays what the channel held before: use `/H0`, or its Sound
Blaster Pro driver. With QEMM, JEMM and HDPMI32i neither applies. A 16-bit
buffer longer than 64 KB or across a 64 KB boundary plays wrong, because the
8-bit channel that carries it reaches 64 KB in one page, which `esssb16` run
again tells of the last buffer, E2h writes no byte through the DMA, and FM
plays as before.

It was tested in a CPU emulator, and [`sb16test`](../README.md#sb16testexe)
checks it on real hardware ([docs/TESTING.md](TESTING.md#d2-esssb16-dos)).

## Installing esssb16

The steps for a Windows 98 machine; on MS-DOS 6.22 they are the same with
`C:\DOS` in place of `C:\WINDOWS`.

1. Copy `build\esssb16.com` to a folder on the machine, such as `C:\ESS`.
2. For protected-mode games, copy `third_party\hdpmi32i\HDPMI32i.EXE` next
   to it. It is the file of `HDPMI32i.zip` from the releases page of
   [HX](https://github.com/crazii/HX/releases), unchanged, and
   [third_party/hdpmi32i/README.md](../third_party/hdpmi32i/README.md) says
   which release and gives its hashes. Real-mode games do without it.
3. In `CONFIG.SYS`, load EMM386 after HIMEM.SYS, with upper memory:
   ```
   DEVICE=C:\WINDOWS\HIMEM.SYS
   DEVICE=C:\WINDOWS\EMM386.EXE NOEMS
   DOS=HIGH,UMB
   ```
   Windows 98 doesn't load EMM386 on its own, so the line is usually new;
   `RAM` in place of `NOEMS` also gives games expanded memory. With JEMMEX
   the lines are `DEVICE=C:\JEMM\JEMMEX.EXE` and `DOS=HIGH,UMB`, and
   `JLOAD C:\JEMM\QPIEMU.DLL` runs before `esssb16`; with QEMM,
   `DEVICE=C:\QEMM\QEMM386.SYS RAM`.
4. Set `BLASTER` and load the programs, HDPMI32i before `esssb16`, in
   `AUTOEXEC.BAT` or in a batch file such as `C:\ESS\SB16.BAT`, to run at
   the prompt before a game:
   ```
   SET BLASTER=A220 I5 D1 H5 P330 T6
   LH C:\ESS\HDPMI32I.EXE -r -x
   C:\ESS\ESSSB16.COM
   ```
   `-r` keeps HDPMI32i resident, for `esssb16` to find, and `-x` makes it
   report 256 MB of memory at most, which some games need. Leave its line
   out without the file. `esssb16` needs no `LH`: it moves its resident
   part, 5 KB, into the last free block of upper memory with room for it,
   from where DOS loaded it, and gives that memory back. So it stays above
   B000h-B7FFh, which `I=B000-B7FF` gives to upper memory, while there is
   room above: a program that finds memory there may write into it as a
   monochrome screen's, as the Sound Blaster drivers of Pinball Fantasies
   and Pinball Dreams do. If the card's base, IRQ or 8-bit channel differ
   from this `BLASTER` line, `esssb16` prints the line to set.
5. Start DOS with these files. On Windows 98, hold Ctrl as the machine
   starts, or press F8 at *Starting Windows 98*, and choose *Command prompt
   only*: `CONFIG.SYS` and `AUTOEXEC.BAT` run, and Windows doesn't. From
   Windows, *Restart in MS-DOS mode* in the Shut Down box ends Windows and
   runs `C:\WINDOWS\DOSSTART.BAT`, the place for the lines of step 4 then,
   with EMM386 still loaded from `CONFIG.SYS`. The batch file keeps
   `esssb16` to DOS sessions, since `AUTOEXEC.BAT` also runs before Windows,
   where the VxD traps the ports itself. Loaded there all the same,
   `esssb16` does nothing while Windows runs, and at the first access after
   Windows ends it takes the chip back from Windows' driver, with a reset of
   the chip and the 3-D effect off again unless `/3`. Run again, as in
   `DOSSTART.BAT`, it says `already loaded` when that copy still answers
   through its traps, and otherwise that they are gone. HDPMI32i belongs
   with it in `DOSSTART.BAT` or the batch file. The other way is an MS-DOS
   mode shortcut with its own files: right-click the game, or
   `C:\COMMAND.COM` for a prompt, choose *Properties* > *Program* >
   *Advanced*, tick *MS-DOS mode* and *Specify a new MS-DOS configuration*,
   and put the lines of steps 3 and 4 into its `CONFIG.SYS` and
   `AUTOEXEC.BAT` boxes. Windows restarts into DOS with them when the
   shortcut runs, and comes back when the program ends. *Restart in MS-DOS
   mode* does the same when its shortcut, `Exit To Dos.pif` in `C:\WINDOWS`,
   has such settings.
6. As it loads, `esssb16` prints the card, the `BLASTER` line, `traps:
   EMM386 4.95 for real-mode games, HDPMI32i for protected-mode games` and
   `resident in upper memory`; the HDPMI32i part is there only when HDPMI32i
   was resident first. With Audio 2 on the game's 16-bit channel it also
   prints `Audio 2 keeps the game's DMA 5 moving under EMM386, muted`.
   `MEM /C` lists `ESSSB16` in upper memory, 5.1 KB, or 5.6 KB with
   HDPMI32i. `resident in conventional memory: no upper memory block was
   free` means that `DOS=UMB` is missing or that the blocks are full, which
   `MEM /C` shows; a game such as Pinball World then may find too little
   memory for its sound. `EMM386 refused traps in upper memory` in its place
   means that EMM386 took the traps only for a copy in conventional memory.
   `EMM386 refused the port traps` stops the load; after `essprobe`, restart
   first. A game's setup program detects a Sound Blaster 16, DSP version
   4.05, and offers 16-bit sound. To take `esssb16` out again, restart
   without its line.

## Checking it on the machine

`build\ESSCHECK.BAT` runs the checks of a machine in one go and keeps what
they find in a folder, `RESULTS`, to send back:
1. Copy `ESSCHECK.BAT`, `esssb16.com`, `essprobe.exe` and `sb16test.exe`
   from `build`, and `HDPMI32i.EXE` from `third_party\hdpmi32i`, to
   `C:\ESS`.
2. Start DOS without `esssb16`, as step 5 above says but without its line,
   with the recorder on the card's line out if you record.
3. Run `C:\ESS\ESSCHECK`. It finds Pinball World and Pinball Dreams in
   `C:\21STCENT`, in `C:\` or in `C:\GAMES`; else give their folders, as in
   `ESSCHECK C:\PW C:\PD`.

It keeps the memory as `MEM` shows it, `CONFIG.SYS`, `AUTOEXEC.BAT`,
`DOSSTART.BAT` and the environment, then runs `essprobe` on the chip: a
game's refills behind the DMA count, on DMA 5 as `esssb16` plays it under
EMM386, recorded when you press R and then Enter at the level tone. Where
`essprobe`'s report is there from a run before, S goes on without it, and R
runs it again. It loads HDPMI32i and `esssb16`, shows what `esssb16` said,
stops if it didn't load, keeps the memory again and runs `sb16test`'s
automatic checks, then again with the game's 16-bit DMA on 5 (`ESSSB16 /H5`,
`SB16TEST /H:5`). Then the games: Pinball World's `SETSND`, where you choose
the Sound Blaster 16 with the DMA that the batch names, 1 and then 5, and
play its test tune, and the game itself, first with the game's 16-bit DMA on
none (`ESSSB16 /H0`), then on 5 (`ESSSB16 /H5`), each followed by Y or N for
how it sounded and by `esssb16` run again, which logs the buffer that the
game last had carried over, and last, if you like, Pinball Dreams'
`SETSOUND`, which restarted the machine before, with the 16-bit DMA on none,
after the results so far are on the disk. With `esssb16` in memory already,
as after *Restart in MS-DOS mode* with it in `AUTOEXEC.BAT`, it skips the
probe and checks that copy instead. Send `C:\ESS\RESULTS` back, with the
recording.

`build\DREAMS.BAT` finds what stops the machine in Pinball Dreams'
`SETSOUND`: copy it to `C:\ESS` as well, and run `C:\ESS\DREAMS` right after
a restart, then again after each restart that it asks for or that a stop
makes. Its first run plays `SETSOUND` with nothing loaded, then with
HDPMI32i, and the next runs with `esssb16` alone and with both, as
`ESSCHECK` loads them. `RESULTS\DREAMS.TXT` keeps where it got to: the setup
that started and never came back is the one that stopped the machine.
