# essreg

Tools and rebuilt drivers for the ESS ES1869 sound chip on DOS and Windows 9x. [README.md](README.md) is the user guide.

## Layout

* `src/*.c`: the DOS programs (`essreg`, `1869opl3`, `esfmpat`) and the shared code: `esshw` (port protocols), `esscat` (register catalog, `esscat.tbl`), `profile`, `vxdapi`, `simhw` (a simulated ES1869 for the tests), `ess3d` (the commands of `ess3d.exe`), `fmrec` (esfmrec's WAV header, file names and levels).
* `src/win/`: `essctl.exe`, the 16-bit Windows control panel, and `ess3dw.c`, the Windows side of `ess3d.exe` (the 3-D effect from the command line, for keys). `ess3dtr.c` is ess3d's tray icon and panel. Its icons come from `tools/ess3dico.py`. `esfmrec.c` records the FM digitally to a WAV file.
* `src/vxd/`: `ES1869.VXD` as NASM source, from `tools/vxd2asm.py`. `essext.asm` adds the register API and the DOS box improvements (the virtual FM chip, Windows' mixer around a DOS program); `essext.inc` has its layout.
* `src/esfm/`: `ESFM.DRV` as NASM source, from `tools/ne2asm.py`. `seg1-4.asm` is ESS's code. `esfmfix.asm`, `esfmfile.asm`, `esfmped.asm` and `esfmgm.asm` are the fixes and General MIDI, assembled with `ESFM_FIX=1`.
* `driver/`: ESS's original drivers, the reference for the byte-identical rebuilds.
* `build/`: the committed binaries.
* `tools/`: builders, RE tools, `guard.py`, `wineshot.sh`. `tests/`: Python tests, `tests/host/`: C tests.
* `docs/`: [TESTING.md](docs/TESTING.md), [ESFM_MIDI.md](docs/ESFM_MIDI.md), [ESFM_GM.md](docs/ESFM_GM.md), [ESFM_BANK.md](docs/ESFM_BANK.md), [VXD_API.md](docs/VXD_API.md), [VXD_INTERNALS.md](docs/VXD_INTERNALS.md), [SPATIALIZER.md](docs/SPATIALIZER.md), [DRIVER_CONFIG.md](docs/DRIVER_CONFIG.md), [AUDIO_PIPELINE.md](docs/AUDIO_PIPELINE.md), [RE_NOTES.md](docs/RE_NOTES.md), [STYLE.md](docs/STYLE.md). `REGISTERS.md` is generated. `docs/datasheet/` has the ES1869, ES938 and ES1868 data sheets.

## Environment

* On Claude Code on the web, [`.claude/hooks/session-start.sh`](.claude/hooks/session-start.sh) installs everything: nasm, unicorn, flake8, clang-format, Open Watcom v2 in `/opt/open-watcom`, 32-bit Wine, Xvfb, xdotool and ImageMagick.
* It exports `OW2=/opt/open-watcom` and `ESSREG_WINE=1`, so every test runs. Its log is `/tmp/essreg-setup.log`.
* Elsewhere, install the same by hand (see the hook).

## Commands

```
python3 tests/run_tests.py                        # every test, about 30 s
python3 -m unittest tests.test_esfmdrv.SustainTest  # one class
python3 tools/build_esfm.py                       # build/ESFM.DRV
python3 tools/build_esfm.py --stock --verify      # ESS's driver, byte for byte
python3 tools/build_vxd.py                        # build/ES1869.VXD
python3 tools/build_vxd.py --stock --verify
tools/ow2build.sh $OW2                            # DOS and Win16 programs, into out/ow2/
python3 tools/regdoc.py                           # docs/REGISTERS.md from src/esscat.tbl
python3 tools/gmcheck.py                          # build/GMCHECK.MID
python3 tools/ess3dico.py                         # the tray icons, src/win/ess3d*.ico
flake8                                            # Python lint, clean
clang-format --dry-run FILE.c                     # C style (.clang-format wants CRLF)
tools/guard.py [REV]                              # only comments changed since REV
tools/wineshot.sh start out/ow2 essctl.exe /sim   # screenshots under Wine
tools/wineshot.sh run ess3d.exe /sim tray         # a second program on its desktop
```

## Rules

* **Byte-identical stock builds.** `build_esfm.py --stock --verify` and `build_vxd.py --stock --verify` must stay identical to `driver/`. Driver changes go under `%if ESFM_FIX` (ESFM.DRV) or in the extension (`ESSREG_EXT`, the VxD).
* **VxD extension hooks:** ESS's code changes only under `%if ESSREG_EXT`, with instructions of the same length, never an added one: ESS's code keeps its addresses. `EssCodeTest` (`tests/test_vxdext.py`) checks it, and a new hook goes in its `HOOKS` list. The extension's per-device state goes after ESS's E9h bytes of the ADI, and a VM's after ESS's 2Eh bytes of its node (`essext.inc`). `tests/test_vxddos.py` runs the DOS box paths in the emulator.
* **Committed binaries.** After changing their sources, rebuild `build/ESFM.DRV` or `build/ES1869.VXD`, and copy the changed programs from `out/ow2/` to `build/`. `test_build_is_current` checks `ESFM.DRV`.
* **ESFM_FIX code:**
  * A label at a relocation site is a `..@` name, so it doesn't end NASM's local label scope.
  * Never free a global block while a segment register holds its selector: protected mode faults when the selector is loaded again. `tests/esfmemu.py` checks this.
  * The fixed code runs at interrupt time. KERNEL and DOS calls only happen at open and enable.
  * State the fixed driver keeps for each device goes after ESS's fields (`DEV_GM_*`, `DEV_FIX_SIZE` in `esfmdev.inc`).
  * Watch the short jumps of ESS's code: code added between a `jmp short` and its target can put it out of range. Adding it right after the target's label keeps the distance.
  * Every long message goes back to its program: refused with its flags as they were, or with MOM_DONE, also when a close drops it or ESS's code refuses it. A program waits for its buffers.
  * DOS calls ask for their errors back (INT 21h 716Ch and 6C00h with BX bit 13): no critical error box at open.
* **The status block** (`esfmfixd.asm`, found by `ESFMFIX`) is read by essctl at fixed offsets (`src/win/esfmlive.c`). A layout change bumps `fix_version`. The next one is 4.
* **Spatializer registers:** a new one goes in `src/esscat.tbl` as an `fx.3d.*` field on `PG_EFFECTS`. essctl, `ess3d reg` and the tray panel pick it up from there (`ess3d_regs()` in `src/ess3d.c`), up to `ESS3D_MAX_REGS`. Its value at Windows start goes in `driver_regs` for `ess3d defaults`.
* **Ranged settings** (levels, signed and raw values) are a text field with a slider on its right, in steps of one, in essctl, its bit editor and the tray panel.
* **The VxD API from essctl:** only the *info* functions, and 0002 and 0003 as [VXD_API.md](docs/VXD_API.md#ownership-and-port-trapping) describes. Never 0006, 0007, 0009, 000B, 0200 or 0201: they register callbacks into the caller's code.
* **Windows 98 robustness** (essctl, ess3d, esfmrec and the DOS programs):
  * No message box where nobody may be looking (the tray, a hotkey, a recording, a timer): it waits behind a full-screen game. Log it, or use ess3d's box.
  * Files: write `NAME.$$$`, check it, then rename it over the old one (profiles, `esfmpat`). `_commit` what has to survive a crash (logs, esfmrec's WAV header), and check `close` and the bytes written, for a full disk.
  * A program that leaves the chip changed while it runs first writes what to put back (esfmrec's `ESFMREC.RST`), and deletes it once it's back.
  * Waits and long jobs let Windows run: a timer or `Yield` between steps, never a busy loop holding the Win16Mutex. A nested message loop puts WM_QUIT back with `PostQuitMessage`.
  * Check what can fail (`SetTimer`, `CreateDialog`, `GlobalAlloc`, `SetMessageQueue`) and log it.
  * A 16-bit program's queue holds 8 messages and drops the rest. Ask for more (`SetMessageQueue`), and never count on one message per event: esfmrec takes every done block when one message comes.
  * Text into a fixed buffer: `_bprintf`/`_vbprintf`, and `%.Ns` for a path.
  * Inside a `winio_begin`/`winio_end` bracket: register accesses only, no file I/O, message box or yield. Read and parse files first (`prof_parse`, then `prof_apply`).
  * Interrupts off only around a few port accesses (about 0.5 ms at most): ES1869.DRV's interrupt code has to run. Once the DSP has a command's first byte, the rest follows (`esshw.c`).
  * DOS programs link with a 4 KB stack (`op STACK=4096`), and big buffers are static.
* **Text:** [docs/STYLE.md](docs/STYLE.md). Docs in the README voice, terse lowercase comments, the file header on new files, no em dashes, no "we".
* **Git:** keep the configured git user. Commit messages start with `feat:`, `fix:`, `docs:`, `refactor:` or `chore:`. Don't commit other drivers or listings than `driver/`'s.
