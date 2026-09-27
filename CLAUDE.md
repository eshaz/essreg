# essreg

Tools and rebuilt drivers for the ESS ES1869 sound chip on DOS and Windows 9x. [README.md](README.md) is the user guide.

## Layout

* `src/*.c`: the DOS programs (`essreg`, `1869opl3`, `esfmpat`) and the shared code: `esshw` (port protocols), `esscat` (register catalog, `esscat.tbl`), `profile`, `vxdapi`, `simhw` (a simulated ES1869 for the tests), `ess3d` (the commands of `ess3d.exe`).
* `src/win/`: `essctl.exe`, the 16-bit Windows control panel, and `ess3dw.c`, the Windows side of `ess3d.exe` (the 3-D effect from the command line, for keys).
* `src/vxd/`: `ES1869.VXD` as NASM source, from `tools/vxd2asm.py`. `essext.asm` adds the register API.
* `src/esfm/`: `ESFM.DRV` as NASM source, from `tools/ne2asm.py`. `seg1-4.asm` is ESS's code. `esfmfix.asm`, `esfmfile.asm`, `esfmped.asm` and `esfmgm.asm` are the fixes and General MIDI, assembled with `ESFM_FIX=1`.
* `driver/`: ESS's original drivers, the reference for the byte-identical rebuilds.
* `build/`: the committed binaries.
* `tools/`: builders, RE tools, `guard.py`, `wineshot.sh`. `tests/`: Python tests, `tests/host/`: C tests.
* `docs/`: [TESTING.md](docs/TESTING.md), [ESFM_MIDI.md](docs/ESFM_MIDI.md), [ESFM_GM.md](docs/ESFM_GM.md), [ESFM_BANK.md](docs/ESFM_BANK.md), [VXD_API.md](docs/VXD_API.md), [VXD_INTERNALS.md](docs/VXD_INTERNALS.md), [RE_NOTES.md](docs/RE_NOTES.md), [STYLE.md](docs/STYLE.md). `REGISTERS.md` is generated.

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
flake8                                            # Python lint, clean
clang-format --dry-run FILE.c                     # C style (.clang-format wants CRLF)
tools/guard.py [REV]                              # only comments changed since REV
tools/wineshot.sh start out/ow2 essctl.exe /sim   # screenshots under Wine
```

## Rules

* **Byte-identical stock builds.** `build_esfm.py --stock --verify` and `build_vxd.py --stock --verify` must stay identical to `driver/`. Driver changes go under `%if ESFM_FIX` (ESFM.DRV) or in the extension (`ESSREG_EXT`, the VxD).
* **Committed binaries.** After changing their sources, rebuild `build/ESFM.DRV` or `build/ES1869.VXD`, and copy the changed programs from `out/ow2/` to `build/`. `test_build_is_current` checks `ESFM.DRV`.
* **ESFM_FIX code:**
  * A label at a relocation site is a `..@` name, so it doesn't end NASM's local label scope.
  * Never free a global block while a segment register holds its selector: protected mode faults when the selector is loaded again. `tests/esfmemu.py` checks this.
  * The fixed code runs at interrupt time. KERNEL and DOS calls only happen at open and enable.
  * State the fixed driver keeps for each device goes after ESS's fields (`DEV_GM_*`, `DEV_FIX_SIZE` in `esfmdev.inc`).
  * Watch the short jumps of ESS's code: code added between a `jmp short` and its target can put it out of range. Adding it right after the target's label keeps the distance.
* **The status block** (`esfmfixd.asm`, found by `ESFMFIX`) is read by essctl at fixed offsets (`src/win/esfmlive.c`). A layout change bumps `fix_version`. The next one is 4.
* **Ranged settings** (levels, signed and raw values) are a text field with a slider on its right, in steps of one, in essctl and its bit editor.
* **The VxD API from essctl:** only the *info* functions, and 0002 and 0003 as [VXD_API.md](docs/VXD_API.md#ownership-and-port-trapping) describes. Never 0006, 0007, 0009, 000B, 0200 or 0201: they register callbacks into the caller's code.
* **Text:** [docs/STYLE.md](docs/STYLE.md). Docs in the README voice, terse lowercase comments, the file header on new files, no em dashes, no "we".
* **Git:** keep the configured git user. Commit messages start with `feat:`, `fix:`, `docs:`, `refactor:` or `chore:`. Don't commit other drivers or listings than `driver/`'s.
