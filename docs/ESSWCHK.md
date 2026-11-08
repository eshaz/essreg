# esswchk: the drivers' fixes checked in Windows

`build/esswchk.exe` checks in Windows 98 that the fixes of the rebuilt
drivers work on the card, and writes what it found to `WCHECK.TXT`, to send
back. [`ESSCHECK.BAT`](ESSSB16.md#checking-it-on-the-machine) starts it by
itself once its DOS part is done, through a batch in Windows' StartUp
folder, which esswchk deletes when it ends. It asks nothing and shows no
message box: problems go to `ESSWCHK.LOG`, next to it, and its window shows
each check and, at the end, *Done* with the report's name.

## What it checks

Each line of the report is a check: PASS, FAIL, KNOWN (a difference that
ESS's own drivers have), INFO (what was measured) or SKIP. The measures and
verdicts are [`src/wchk.c`](../src/wchk.c)'s.

* **env**: the VxD and its extension, whether ES1869.DRV and ESFM.DRV are
  the rebuilt ones and their settings, `DmaCountTwice` and `TrueSides` from
  `SYSTEM.INI`, and the wave and MIDI devices.
* **vxd.count2** and **vxd.count1**: while each DAC plays silence, 100,000
  readings of its DMA count through VxD function 0004, which ES1869.DRV's
  positions and DirectSound's come from. A reading taken while a byte came
  between the reads of the count's low and high byte is 256 off. With the
  extension 1.26 or later and `DmaCountTwice=1`, the VxD reads until two
  readings agree within 32, so no reading may be torn: PASS or FAIL. ESS's
  VxD reads once, which is INFO with how often a reading was torn.
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
* **fm.rate**: the FM recording device's blocks over 30 seconds, fitted to
  the clock: the music DAC's rate within 500 ppm of 49,819.5 Hz, which
  Linux's driver measured.
* **sb.box**: `SB16BOX.BAT`, which esswchk writes next to itself, runs
  `sb16test /A` in a DOS box on the VxD's Sound Blaster 16, with `BLASTER`
  from the VxD's resources and `H` from `SoundBlaster16DMA`. esswchk closes
  its devices first and waits up to 5 minutes for `SB16BOX.TXT`, next to the
  report: PASS when its summary has no FAIL, and its `ctl.poll16` line comes
  along. SKIP when the VxD's Sound Blaster 16 is off.

## Running it

ESSCHECK starts it with `/auto`, and `/R` where the run is recorded. To run
it by hand, close the programs that play sound and run `esswchk /auto`. The
checks take about 10 minutes.

The options:
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
`sb16test` reports, without a card, and ended by force, which leaves the
report as `WCHECK.$$$`. `tests/test_wchkrec.py` makes recordings to a report
and breaks them, and under Wine a recording made to esswchk's own `/R`
report passes.
