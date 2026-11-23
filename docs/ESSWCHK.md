# esswchk: the drivers' fixes checked in Windows

`build/esswchk.exe` checks in Windows 98 that the fixes of the rebuilt
drivers work on the card, and writes what it found to `WCHECK.TXT`, to send
back. Started without options, after an OK, it runs all the checks of the
machine ([ESSSB16.md](ESSSB16.md#checking-it-on-the-machine)): essinst puts
the drivers of its folder in, Windows ends for `ESSCHECK.BAT` in DOS and
comes back by itself, and ESSCHECK's batch in Windows' StartUp folder starts
esswchk's checks. The batch deletes itself as soon as esswchk is started,
and esswchk deletes it too as its run starts, if it's still there, so the
next start of Windows doesn't run the checks again. They ask nothing and
show no message box: problems go to `ESSWCHK.LOG`, next to it, and its
window shows each check and, at the end, *Done* with the report's name.

## What it checks

Each line of the report is a check: PASS, FAIL, KNOWN (a difference that
ESS's own drivers have), INFO (what was measured) or SKIP. The measures and
verdicts are [`src/wchk.c`](../src/wchk.c)'s.

* **env**: the VxD and its extension, whether ES1869.DRV and ESFM.DRV are
  the rebuilt ones and their settings, `DmaCountTwice`, `Opl3Driver`,
  `TrueSides` and `Opl3Device` from `SYSTEM.INI`, and the wave and MIDI
  devices. `env.banks` gives the patch banks that `Bank=` and `Opl3Bank=`
  name, which the box of the patch banks writes
  ([ESFM_BANK.md](ESFM_BANK.md#choosing-a-bank-in-the-multimedia-control-panel)).
  `env.once` says what became of the StartUp batch. `env.midi` gives the
  MIDI list of the Multimedia control panel, an entry a line with its key,
  name, `Driver`, `Port`, `Active` and `DevNode`, `env.mapper` the MIDI
  mapper's `CurrentInstrument`, and `env.card` the card's MIDI drivers,
  where essinst adds ESFMOPL3.DRV, the OPL3 device's driver
  ([ESFM_OPL3.md](ESFM_OPL3.md#the-multimedia-control-panel)).
* **opl.list**: with `Opl3Device=2`, which essinst writes with the driver,
  ESFMOPL3.DRV's entry in the MIDI list as Windows keeps it after a start:
  there, active on the card's devnode, the driver among the card's MIDI
  drivers and loaded, and the OPL3 device once among the MIDI devices. The
  first of those that isn't so is the FAIL's reason. Two `env.opl3` lines
  before it give all of it, whatever the verdict: the entry's `Active` and
  `DevNode`, whether the card's drivers have it, whether ES1869.VXD told
  Windows that the driver is there (function 040E, from the extension 1.27),
  whether ESFMOPL3.DRV is loaded and, if it isn't, whether the copy in the
  `SYSTEM` folder loads, with the error if it doesn't, whether ESFM.DRV
  leaves the OPL3 device to it, and how many MIDI devices have the device's
  name. SKIP with another `Opl3Device`: the OPL3 device is then ESFM.DRV's
  device 1, without an entry of its own.
* **vxd.count2** and **vxd.count1**: while each DAC plays silence, 100,000
  readings of its DMA count through VxD function 0004, which ES1869.DRV's
  positions and DirectSound's come from. A reading taken while a byte came
  between the reads of the count's low and high byte is 256 off. With the
  extension 1.26 or later and `DmaCountTwice=1`, the VxD reads until two
  readings agree within 32, so no reading may be torn: PASS or FAIL. ESS's
  VxD reads once, which is INFO with how often a reading was torn. The
  readings come in bursts of 250 with Windows' messages between them, and
  each burst is judged by itself, broken where the count jumps up as the
  driver's ring of two blocks wraps, so neither the gaps nor the ring's size
  matter; a burst that took over 40 ms is left out. The count has to move,
  and may not step back.
* **a1.position**: the Audio 1 device streams for a minute while esswchk
  asks for its position in bursts of 250. The position never steps back, and
  never moves faster than the stream plays. A torn count in the player would
  put it 256 bytes ahead for one reading, and the next reading would step
  back.
* **a1.drain**: eight headers of 500 bytes at 8 kHz, 500 ms of sound,
  written at once, three times. The last header comes back once the DAC
  played it, 500 ms or more after the first write, about 531 ms with the
  fixed player. The player before the fix gave it back once the DMA took its
  end, about 468 ms, a FIFO's worth too early, and a program that closed the
  device then cut the end off. Later than 600 ms is INFO.
* **fm.pan0** and **fm.pan127**: a note on the ESFM device panned left, then
  right, recorded through the FM recording device. With ESFM.DRV's
  `TrueSides`, the note sounds on its side; ESS's driver plays it on the
  other side, which is KNOWN.
* **opl.pan0** and **opl.pan127**: the same on ESFM.DRV's OPL3 device
  ([ESFM_OPL3.md](ESFM_OPL3.md)), which plays the note on the chip's OPL3
  mode, with the same verdicts. SKIP when ESFM.DRV has no OPL3 device: ESS's
  driver, or `Opl3Device=0`.
* **fm.rate**: the FM recording device's blocks over 30 seconds, fitted to
  the clock: the music DAC's rate within 500 ppm of 49,612.5 Hz, the 23.814
  MHz master over 480, which essprobe measured in DOS. The laptop reads
  about 49,605 Hz here, since Windows 98's millisecond is 1,193 counts of
  the PIT and runs 153 ppm fast.
* **opl.rate**: the same over 20 seconds while the OPL3 device has the chip
  in its OPL3 mode, as INFO with how far it is from 49,612.5 Hz. The
  laptop's runs measured both modes at the same rate.
* **sb.box**: `SB16BOX.BAT`, which esswchk writes next to itself, runs
  `sb16test /A` in a DOS box on the VxD's Sound Blaster 16, with `BLASTER`
  from the VxD's resources and `H` from `SoundBlaster16DMA`. esswchk closes
  its devices first and waits up to 10 minutes for `SB16BOX.TXT`, next to
  the report, idle between a timer's messages, so that the DOS box gets the
  processor: PASS when its summary has no FAIL, and its `ctl.poll16` line
  comes along. SKIP when the VxD's Sound Blaster 16 is off, or when
  `SB16TEST.EXE` isn't next to esswchk.

## Running it

Without options, esswchk asks for an OK and then runs
`essinst /y /norestart` from its folder, which stages the drivers there for
Windows' next start, and ends Windows with ExitWindowsExec for
`COMSPEC /C ESSCHECK.BAT /W` and its folder. When ESSCHECK ends, Windows
comes back by itself, with the drivers in place, and ESSCHECK's StartUp
batch runs `esswchk /auto /R`. Where Windows doesn't end, a box says to
restart in MS-DOS mode and run ESSCHECK. To run only the checks in Windows,
put `sb16test.exe` next to it, close the programs that play sound and run
`esswchk /auto`. The checks take about 10 minutes.

The options:
* `/go`: the whole run, without the OK.
* `/auto`: every check with no question; the window stays open with the
  summary.
* `/R`: a recorded run (below).
* `/once=file`: delete the file when the run ends, as ESSCHECK's StartUp
  batch wants.
* `/out=file`: the report, else `WCHECK.TXT` in esswchk's folder.
* `/log=file`: the log, else `ESSWCHK.LOG`.
* `/nobox`: no DOS box; the report says what to run in one.
* `/q`: the window closes when the run ends.
* `/sim`: made-up measures of the fixed drivers, without the card, and
  `/sim=old` those of the drivers before the fixes.
* `/base=220`, `/cfg=800`, `/novxd`: as for essctl.

The exit codes are 0 done, 1 stopped, 2 could not run, 3 a bad command line
and 4 when a check failed.

## A recorded run

With `/R`, after its checks, esswchk plays a 441 Hz level tone on Audio 2's
device, then the start marker (DTMF BADC), each sound after its marker (#,
its number in three digits, *) and the end marker (CDAB). essprobe's markers
differ, so ESSCHECK's one recording holds both runs.
* `p.start44a` to `c` and `p.start22`: 441 Hz on the left alone, through the
  Audio 1 device, each from a fresh open: a 16-bit stream that started a
  byte off plays as noise, and two bytes off on the right.
* `p.restart`: the same with a pause of 400 ms in it.
* `p.dual44a` to `c`: a 4-channel file on the Audio 1 device, dual playback,
  with 441 Hz on Audio 1's left and on Audio 2's right, which start together
  in the player's dual start.
* `p.fm0` and `p.fm127`: the ESFM's note panned left and right.

Each Audio 1 sound also has a `times` line: when its writes came back, when
its pause was called, and when each of its headers came back, in ms after
its first write. Version 1.6 adds it, after a run where the last header came
back 740 ms after the end of the sound at 44.1 kHz, though 50 ms at 22,050
Hz.

`python3 tools/wchkrec.py WCHECK.TXT run.wav` finds the markers, fits the
report's clock to the recording's, and measures each sound: a tone on its
side, as `tools/essprec.py` measures it, naming a stream a byte off; the
pause; the dual sounds' delay of the right against the left, in frames at
44,100 Hz, within one frame when they started in step; and the FM's note on
its side by its level.

## The tests

`tests/host/t_wchk.c` checks the measures and verdicts, on /sim's card for
the fixed drivers and the old ones. `tests/test_wine.py` runs esswchk under
Wine with `/sim` and `/sim=old`, with `DmaCountTwice=0`, with canned
`sb16test` reports and with none coming, without a card, ended by force,
which leaves the report as `WCHECK.$$$`, and with `/go /sim`, which logs the
whole run's steps. `tests/test_wchkrec.py` makes recordings to a report and
breaks them, and under Wine a recording made to esswchk's own `/R` report
passes.
