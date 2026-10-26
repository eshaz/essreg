# essreg

This repository holds tools and rebuilt drivers for the ESS ES1869 sound
chip on DOS and Windows 9x. [README.md](README.md) is the user guide.

## Layout

* `src/*.c` holds the DOS programs (`essreg`, `1869opl3`, `esfmpat` and
  `sb16test`) and the shared code: `esshw` (the port protocols), `esscat`
  (the register catalog, from `esscat.tbl`), `profile`, `vxdapi`, `simhw` (a
  simulated ES1869 for the tests), `ess3d` (the commands of `ess3d.exe`),
  `fmrec` (esfmrec's WAV header, file names and levels), `s3dmeas` (ess3d's
  measurement of the 3-D effect, with an effect in `s3dsim` that follows one
  card's measurement, for the tests, /sim and the tray panel's graph),
  `wavestat` (what ES1869.DRV is doing, read from its data segment for
  essctl), `drvinst` (essinst's checks of the drivers, and its staging
  through WININIT.INI) and `sb16t` (sb16test's checks of a Sound Blaster 16,
  which the tests also run on the host).
* `src/win/` holds `essctl.exe`, the 16-bit Windows control panel, and
  `ess3dw.c`, the Windows side of `ess3d.exe`, which sets the 3-D effect
  from the command line for keys. `ess3dtr.c` is ess3d's tray icon and
  panel, with a graph of what the 3-D settings do, whose icons come from
  `tools/ess3dico.py`, and `ess3dms.c` its measurement of the 3-D effect,
  played on Audio 2 and recorded from record source 7. `esfmrec.c` records
  the FM digitally to a WAV file, and `essinst.c` installs the rebuilt
  drivers and restarts Windows. `wheel.c` gives the sliders of essctl and
  ess3d the mouse wheel.
* `src/vxd/` holds `ES1869.VXD` as NASM source, made by `tools/vxd2asm.py`.
  `essext.asm` adds the register API, the DOS box improvements (the virtual
  FM chip, Windows' mixer around a DOS program, the DACs' modes for it, and
  a Sound Blaster 16 in front of the chip), DirectSound's Lock and Unlock on
  a 16-bit DMA channel and the DRQ latch, and `essext.inc` has its layout.
* `src/esfm/` holds `ESFM.DRV` as NASM source, made by `tools/ne2asm.py`.
  `seg1-4.asm` is ESS's code. `esfmfix.asm`, `esfmfile.asm`, `esfmped.asm`
  and `esfmgm.asm` hold the fixes and General MIDI, and `esfmini.asm` their
  SYSTEM.INI switches, all assembled with `ESFM_FIX=1`.
* `src/es1869/` holds `ES1869.DRV` (wave, mixer and aux) as NASM source,
  made by `tools/ne2asm.py` with `names.txt`. The changes are assembled with
  `ES1869_FIX=1`: `a2mode.asm` (the Audio 2 mode), `a1wave.asm` and
  `a1play.asm` (the Audio 1 player's wave messages, and its interrupt code
  in fixed memory), `fmwave.asm` and `fmdac.asm` (the FM recording device:
  its wave-in messages, and mixer 7Fh while it records), `settings.asm`
  (SYSTEM.INI, and the Audio 2 mode after every mixer reset), `fixdata.asm`
  (their data) and `fix.inc` (constants and ESS's device fields).
* `driver/` holds ESS's original drivers, the reference for the
  byte-identical rebuilds, with ESS's ES1869F driver sets for Windows 3.1,
  NT 4.0, Windows 95 and OS/2 in `driver/es1869f/`, and `build/` the
  committed binaries. `third_party/hdpmi32i/` holds `HDPMI32i.EXE` as HX
  released it, for esssb16's protected-mode games, with its origin and
  hashes in its README.
* `src/esssb16.asm` is `esssb16.com`, the DOS TSR that answers games as a
  Sound Blaster 16 and plays their sound on Audio 1, through the port traps
  of EMM386, QPI (QEMM, JEMM with QPIEMU) and HDPMI32i. It is the VxD's
  front (`ESSREG_S16_*` in `essext.asm`) in DOS: the same commands, operand
  table, transfer sequences and mixer mapping, with the game's 16-bit DMA
  channel trapped (QPI, HDPMI32i) or read back from the controller (EMM386)
  instead of VDMAD. The chip takes a byte per DMA cycle on any channel, so
  no transfer may go to a 16-bit channel as words.
* `tools/` holds the builders, the RE tools, `guard.py`, `wineshot.sh`,
  `dualwav.py`, which makes 4-channel files for dual playback, and
  `sb16rec.py`, which analyzes a recording of `SB16TEST /R` against its
  report. `tests/` holds the Python tests (`esfmemu.py`, `vxdemu.py` and
  `drvemu.py` run the drivers in a CPU emulator, `tsremu.py` runs
  `esssb16.com` in one, with DOS, the memory managers and the DPMI host in
  Python, and `dosrun.py` a DOS program, `sb16test.exe`, in front of a
  card), and `tests/host/` the C tests. `sb16.py` is the Sound Blaster 16
  that the fronts are tested against, `esschip.py` the chip's audio behind
  them, `dma8237.py` the DMA controllers, `sbgame.py` and `sbscenes.py` what
  games do, and `sbfronts.py` the fronts as cards, with a model of VDMAD for
  the VxD's.
* `docs/` holds [TESTING.md](docs/TESTING.md),
  [ESFM_MIDI.md](docs/ESFM_MIDI.md), [ESFM_GM.md](docs/ESFM_GM.md),
  [ESFM_BANK.md](docs/ESFM_BANK.md), [VXD_API.md](docs/VXD_API.md),
  [VXD_INTERNALS.md](docs/VXD_INTERNALS.md),
  [SPATIALIZER.md](docs/SPATIALIZER.md),
  [DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md),
  [AUDIO_PIPELINE.md](docs/AUDIO_PIPELINE.md), [AUDIO1.md](docs/AUDIO1.md),
  [SB16.md](docs/SB16.md), [RE_NOTES.md](docs/RE_NOTES.md) and
  [STYLE.md](docs/STYLE.md). `REGISTERS.md` and the `REG_*.md` pages are
  generated, and `docs/datasheet/` has the ES1869, ES938 and ES1868 data
  sheets.

## Environment

On Claude Code on the web,
[`.claude/hooks/session-start.sh`](.claude/hooks/session-start.sh) installs
everything: nasm, unicorn, numpy, flake8, clang-format, Open Watcom v2 in
`/opt/open-watcom`, 32-bit Wine, Xvfb, xdotool and ImageMagick. It exports
`OW2=/opt/open-watcom` and `ESSREG_WINE=1`, so every test runs, and it logs
to `/tmp/essreg-setup.log`. Elsewhere, install the same by hand, following
the hook.

## Commands

```
# every test (about 6 minutes), or one class
python3 tests/run_tests.py
python3 -m unittest tests.test_esfmdrv.SustainTest

# the drivers, then ESS's drivers rebuilt byte for byte
python3 tools/build_esfm.py            # build/ESFM.DRV
python3 tools/build_vxd.py             # build/ES1869.VXD
python3 tools/build_es1869drv.py       # build/ES1869.DRV
python3 tools/build_esssb16.py         # build/esssb16.com
python3 tools/build_esfm.py --stock --verify
python3 tools/build_vxd.py --stock --verify
python3 tools/build_es1869drv.py --stock --verify

# the DOS and Win16 programs, into out/ow2/
tools/ow2build.sh $OW2

# generated files
python3 tools/regdoc.py                # the register docs, from esscat.tbl
python3 tools/gmcheck.py               # build/GMCHECK.MID
python3 tools/ess3dico.py              # the tray icons, src/win/ess3d*.ico

# a 4-channel file for dual playback
python3 tools/dualwav.py same in.wav out.wav

# a recorded sb16test run (SB16TEST /R) against its report
python3 tools/sb16rec.py SB16TEST.TXT run.wav

# style
flake8                                 # Python lint, clean
clang-format --dry-run FILE.c          # C style (.clang-format wants CRLF)
python3 tools/mdfmt.py                 # markdown wrapped for Notepad
tools/guard.py [REV]                   # only comments changed since REV

# screenshots under Wine, and a second program on the same desktop
tools/wineshot.sh start out/ow2 essctl.exe /sim
tools/wineshot.sh run ess3d.exe /sim tray
```

## Rules

* **Byte-identical stock builds.** `build_esfm.py`, `build_vxd.py` and
  `build_es1869drv.py` with `--stock --verify` must stay identical to
  `driver/`. Driver changes go under `%if ESFM_FIX` (ESFM.DRV), under
  `%if ES1869_FIX` (ES1869.DRV) or into the extension (`ESSREG_EXT`, the
  VxD).
* **SYSTEM.INI settings.** Every change to ESS's drivers has a key in the
  driver's section (`[ES1869.VXD]`, `[ES1869.DRV]` or `[ESFM.DRV]`), which
  the driver reads once when it starts, and `0` gives back what ESS's driver
  does there. The exceptions: `Audio2Oversampling`, `Audio2Filter` and
  `Audio1Filter`, where `1` is ESS's setting of a DAC stage and the default
  `0` turns the stage off, and ESFM.DRV's `Bank=`, a file name, read at
  every MIDI open. A new change gets a key with its default, a row in
  section 6 of
  [DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md#6-the-rebuilt-drivers-systemini-settings),
  and tests of both values (`tests/test_vxdini.py`,
  `tests/test_es1869drv.py`, `tests/test_a1play.py`, `SettingsTest` in
  `tests/test_esfmdrv.py`), including the comparison with ESS's driver with
  every key at 0.
* **ES1869.DRV changes.** ESS's code keeps its addresses, as in the VxD: a
  changed instruction keeps its length, and new code goes after ESS's. ESS
  copies its interrupt handler to a fixed block and patches it by offset, so
  moving code would break it. Each changed instruction goes in `HOOKS` in
  `tests/test_es1869drv.py`, which compares ESS's segments byte for byte and
  relocation for relocation.
* **The Audio 1 player** (ES1869.DRV):
  * DSP commands are sent at task time only. ESS's driver never sends one at
    interrupt time, where its bytes would mix with a task's, so the
    interrupt code only moves data and reads the DMA position.
  * The player takes Audio 1 the way ESS's wave input does (`DEV_A1_USER` 1,
    and the DSP through `vxd_acquire`), so each of the two refuses the other
    with `MMSYSERR_ALLOCATED`.
  * Its state goes after ESS's 132h bytes of the device structure (`A1_*` in
    `fix.inc`), and its block starts like ESS's wave instance, so that ESS's
    callback and WOM_DONE code (1:0010, 1:0048) serve it.
  * ESS's routines that it calls clobber AX, BX, CX, DX and ES, so keep what
    is needed across them. `tests/test_a1play.py` compares what the DMA
    takes with what the program wrote.
* **VxD extension hooks.** ESS's code changes only under `%if ESSREG_EXT`,
  with instructions of the same length and never an added one, so ESS's code
  keeps its addresses. `EssCodeTest` (`tests/test_vxdext.py`) checks this,
  and a new hook goes in its `HOOKS` list. The extension's per-device state
  goes after ESS's E9h bytes of the ADI, and a VM's state after ESS's 2Eh
  bytes of its node (`essext.inc`). `tests/test_vxddos.py` runs the DOS box
  paths in the emulator.
* **esssb16.com.** The resident part ends at `resident_end`, makes no DOS
  call, and reaches the chip and the DMA controllers only through `chip_in`
  and `chip_out`, which go through HDPMI32i's untrapped I/O while its
  handler runs and keep AH, which callers use for a register number. The
  shims run the front on its own stack. EMM386 searches the dispatch table
  (`emm_table`) at every trapped access and calls the handlers at ring 0,
  through selectors as long as the resident part, so both stay resident.
  HDPMI's first CS after the DPMI switch is a 16-bit selector whatever the
  client, so that code is 16-bit, with the 32-bit registers a 32-bit client
  passes, and HDPMI's data take the paragraphs after the resident part, over
  the routines between `host_area` and `host_code_end`, which run only
  before the switch. A change to the Sound Blaster 16's translation goes
  into the VxD's front and the TSR alike, and both answer as the model in
  `tests/sb16.py` does ([SB16.md](docs/SB16.md)). A 16-bit playback plays on
  Audio 2 where it has an 8-bit channel of its own (`dma2` in the TSR,
  `EX_S16_A2` in the VxD), and everything else on Audio 1. Test it with a
  scene in `tests/sbscenes.py` (`@dual` for two transfers at once), which
  `tests/test_sb16conf.py` plays on the model and on both fronts, or a check
  in `tests/test_sb16front.py`. The resident part has no fixed limit, and
  `tools/build_esssb16.py` reports its size; the file stays within the 9 KB
  that LH loads, and `tests/test_esssb16.py` checks that `build/esssb16.com`
  is current.
* **sb16test.** Its checks (`src/sb16t.c`) reach the card only through
  `struct sbt_hal`: `src/sb16test.c` on the machine, and
  `tests/test_sb16test.py` on the model and on both fronts. The handler's
  path (`sbt_irq` and the hooks) may run on another program's stack and in
  the middle of the checks' arithmetic, so it takes no address of a local,
  has no varargs and does no floating point. Every check passes on the
  model, and one is KNOWN on a front only for a difference that SB16.md
  lists (`KNOWN` in the test). The test runs `build/sb16test.exe` in the
  emulator too, so copy it from `out/ow2/` after a change. Every listening
  tone is `HZ` (441 Hz) in a buffer of whole cycles. The recorded run's
  lines (`record_one`) are what `tools/sb16rec.py` reads, so a change to
  them goes into both, and `tests/test_sb16rec.py` analyzes recordings made
  from the model's and a front's output.
* **Committed binaries.** After changing their sources, rebuild
  `build/ESFM.DRV`, `build/ES1869.VXD`, `build/ES1869.DRV` or
  `build/esssb16.com`, and copy the changed programs from `out/ow2/` to
  `build/`. `test_build_is_current` checks `ESFM.DRV` and `ES1869.DRV`.
* **ESFM_FIX code:**
  * A label at a relocation site is a `..@` name, so that it doesn't end
    NASM's local label scope.
  * Never free a global block while a segment register holds its selector,
    because protected mode faults when the selector is loaded again.
    `tests/esfmemu.py` checks this.
  * The fixed code runs at interrupt time, so KERNEL and DOS calls happen
    only at open and enable.
  * State that the fixed driver keeps for each device goes after ESS's
    fields (`DEV_GM_*` and `DEV_FIX_SIZE` in `esfmdev.inc`).
  * Watch the short jumps in ESS's code. Code added between a `jmp short`
    and its target can put the target out of range, and adding it right
    after the target's label keeps the distance.
  * Every long message goes back to its program: refused with its flags as
    they were, or with MOM_DONE, also when a close drops it or ESS's code
    refuses it. A program waits for its buffers.
  * DOS calls ask for their errors back (INT 21h 716Ch and 6C00h with BX bit
    13), so that no critical error box appears at open.
* **The status block** (`esfmfixd.asm`, found by `ESFMFIX`) is read by
  essctl at fixed offsets (`src/win/esfmlive.c`). A change to its layout
  bumps `fix_version`, and the next version is 5.
* **ES1869.DRV's status** (`es_status` in `src/es1869/fixdata.asm`, found by
  `ESDRVFIX`, the player's block and the FM recording device's) is read by
  essctl (`src/wavestat.c` and `src/win/wavelive.c`). A change to its layout
  bumps its version word, and the next version is 3.
* **Spatializer registers.** A new one goes in `src/esscat.tbl` as an
  `fx.3d.*` field on `PG_EFFECTS`, and essctl, `ess3d reg` and the tray
  panel pick it up from there (`ess3d_regs()` in `src/ess3d.c`), up to
  `ESS3D_MAX_REGS`. Its value at Windows start goes in `driver_regs`, for
  `ess3d defaults`. What its value does goes in `cat_unit()`
  (`src/esscat.c`), with an `FMT_3D_*` format, so that essctl and the panel
  show it next to the slider.
* **Ranged settings** (levels, and signed and raw values) are a text field
  with a slider on its right, in steps of one, in essctl, in its bit editor
  and in the tray panel. The mouse wheel moves the slider under the pointer
  one step a notch, so a window with sliders gets its messages through
  `wheel_get` (or `wheel_dialog` for a modal one): a 16-bit WM_MOUSEWHEEL
  loses the turn, which USER32's PeekMessageA keeps.
* **The VxD API from essctl.** Of ESS's functions, essctl calls only the
  *info* functions, and 0002 and 0003 as
  [VXD_API.md](docs/VXD_API.md#ownership-and-port-trapping) describes. It
  never calls 0006, 0007, 0009, 000B, 0200 or 0201, because they register
  callbacks into the caller's code.
* **Windows 98 robustness** (essctl, ess3d, esfmrec and the DOS programs):
  * Show no message box where nobody may be looking, such as from the tray,
    a hotkey, a recording or a timer, because it waits behind a full-screen
    game. Log the problem instead, or use ess3d's box.
  * Write a file as `NAME.$$$`, check it, then rename it over the old one
    (profiles, `esfmpat`). `_commit` what has to survive a crash (logs,
    esfmrec's WAV header), and check `close` and the number of bytes
    written, for a full disk.
  * A program that leaves the chip changed while it runs first writes what
    to put back (esfmrec's `ESFMREC.RST`), and deletes that file once the
    chip is back.
  * Waits and long jobs let Windows run, with a timer or `Yield` between
    steps, and never a busy loop that holds the Win16Mutex. A nested message
    loop puts WM_QUIT back with `PostQuitMessage`.
  * Check what can fail (`SetTimer`, `CreateDialog`, `GlobalAlloc`,
    `SetMessageQueue`) and log it.
  * A 16-bit program's queue holds 8 messages and drops the rest. Ask for
    more with `SetMessageQueue`, and never count on one message per event:
    esfmrec takes every finished block when one message comes.
  * Put text into a fixed buffer with `_bprintf` or `_vbprintf`, and a path
    with `%.Ns`.
  * Inside a `winio_begin`/`winio_end` bracket, only access registers: no
    file I/O, message box or yield. Read and parse files first
    (`prof_parse`, then `prof_apply`).
  * Turn interrupts off only around a few port accesses, about 0.5 ms at
    most, because ES1869.DRV's interrupt code has to run. Once the DSP has a
    command's first byte, send the rest (`esshw.c`).
  * DOS programs link with a 4 KB stack (`op STACK=4096`), and their big
    buffers are static.
* **Text** follows [docs/STYLE.md](docs/STYLE.md): documentation in
  complete, plain sentences, terse lowercase comments, the file header on
  new files, no em dashes and no "we". Every markdown file stays readable in
  Windows 98's Notepad: run `python3 tools/mdfmt.py` after editing one, and
  `tests/test_docs.py` checks it.
* **Git.** Keep the configured git user. Commit messages start with `feat:`,
  `fix:`, `docs:`, `refactor:` or `chore:`. Don't commit drivers or listings
  other than those in `driver/`.
