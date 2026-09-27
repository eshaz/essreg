# Style guide

How essreg's code comments, file headers and docs are written. It follows essreg's original code (`src/regs.c`, `src/esfmpat.c`, `src/1869opl3.c`, `src/debug.h`, `src/main.c`) and the [README](../README.md).

## Comments in code

* Short and plain. The first word is lowercase, except proper nouns and register or chip names (DSP, ESS, FM_Base, ES1869.VXD, Audio_Base+Ch).
* No period at the end of a one-phrase comment.
* Say what the next lines do, like a list of steps:
  ```
  // set config register to audio logical device (1)
  // write the patch at the offset
  // poll for low bit 7 of audiobase + Ch until clear
  # save read data (previous overlap + last block data)
  ```
* Trailing comments on fields and table rows. Datasheet names in Title Case:
  ```
  0x06, // Interrupt Status Register
  unsigned char get_digital_power_down(); // audio_base + 6, bit 3
  ```
* When something needs explaining (a hardware quirk, a driver offset), say it in one or two short lines: `// reading base+E clears the audio interrupt, so poll base+C instead`.
* Keep every fact that matters: offsets, bit meanings, why an order is required, what breaks if it changes.
* No essays, no "Note:", no sentences chained with semicolons, no em dashes, no "we", no restating the code.

**By language:**
* C: `//` inside code and after fields. `/* */` only for the file header, `#endif /* NAME_H */`, macro lines ending in a backslash (a `//` there would swallow the next line), and `src/esscat.tbl` (`tools/regdoc.py` reads its `/*` lines as notes).
* `.rc` files: `/* */` (the resource compiler's preprocessor).
* asm (NASM and WASM): `;` comments, same voice.
* Python: `#` comments, same voice. Docstrings are one short line, or a few when they carry usage. argparse uses the first line of a module docstring, so keep one wherever it exists.

## File headers

C, as in `src/esfmpat.c`:

```
/*
 * ESFM Patch is a program that writes custom patch
 * banks into esfm.drv.
 *
 * Usage:
 *   `esfmpat "c:\path\to\esfm.drv" "c:\path\to\patch.bin"`
 *
 * (c) 2024 Ethan Halsall <ethan.s.halsall@gmail.com>
 *
 * Licensed under GPL Version 3.0
 */
```

* The first lines say what the file is, in plain words ("Calls into the ES1869.VXD API ..."). No "name.c -- " prefix.
* An optional short `Notes:` or `Usage:` block.
* The year is 2026 for files added since the 2024 release. `esfmpat.c` and `1869opl3.c` keep 2024. `regs.c`, `main.c` and `debug.c` have no header, leave them without one.
* A blank ` *` line between the copyright line and "Licensed under GPL Version 3.0".
* asm: the same block with `;`. Shell scripts and `.mk1` files: with `#`.
* Code disassembled from ESS's drivers (`src/vxd/*.asm`, `src/esfm/seg*.asm`) gets no copyright line.

Python (the shebang stays first):

```
#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Build ES1869.VXD from the source in src/vxd.

usage: build_vxd.py [--stock] [--verify] [-o OUTPUT]
"""
```

## Docs (the README voice)

* Headings, then bullets. Short sentences. Plain words. Casual is fine, sparingly.
* *Note: ...* in italics for asides.
* Code and usage in fenced blocks, with an `Example:` line where it helps.
* Keep every table, fact, address, register, step and link. Cut filler, hedging and repetition.
* No em dashes (use a comma, a colon or a new sentence). No "we".

## Never change when rewording

* Code, identifiers, numbers, macro names, labels, string literals, and every text the user sees: messages, menus, dialogs, INI keys, command-line switches.
* Generated files, other than through their generator: `docs/REGISTERS.md` comes from `tools/regdoc.py` and the notes in `src/esscat.tbl`.
* The object-offset comments (`; 0D28`) of `src/vxd/*.asm` and `src/esfm/seg*.asm`. Only the prose comments there can change.
* Text the tests check, profile keys, `layout.json` and `*.bin`.
* `tools/guard.py` proves a rewording changed comments and docstrings only.
