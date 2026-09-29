# Style guide

This guide describes how essreg's code comments, file headers and documentation are written. The comments follow the voice of essreg's original code (`src/regs.c`, `src/esfmpat.c`, `src/1869opl3.c`, `src/debug.h` and `src/main.c`). The documentation is written in plain, complete English, as described in [Documentation](#documentation).

## Comments in code

Comments are short and plain. The first word is lowercase unless it is a proper noun or the name of a register or chip (DSP, ESS, FM_Base, ES1869.VXD, Audio_Base+Ch), and a comment of a single phrase has no period at the end.

A comment says what the next lines do, like a step in a list:

```
// set config register to audio logical device (1)
// write the patch at the offset
// poll for low bit 7 of audiobase + Ch until clear
# save read data (previous overlap + last block data)
```

Fields and table rows take trailing comments, with the data sheet's names in Title Case:

```
0x06, // Interrupt Status Register
unsigned char get_digital_power_down(); // audio_base + 6, bit 3
```

When something needs explaining, such as a hardware quirk or an offset in a driver, one or two short lines are enough: `// reading base+E clears the audio interrupt, so poll base+C instead`. Keep every fact that matters: offsets, the meaning of bits, why an order is required and what breaks if it changes. Leave out essays, "Note:", sentences chained with semicolons, em dashes, "we", and anything that only restates the code.

Each language has its own comment syntax:
* **C** uses `//` inside code and after fields. `/* */` is only for the file header, for `#endif /* NAME_H */`, for macro lines that end in a backslash (where a `//` would swallow the next line) and for `src/esscat.tbl`, whose `/*` lines `tools/regdoc.py` reads as notes.
* **Resource scripts** (`.rc`) use `/* */`, which the resource compiler's preprocessor understands.
* **Assembly** (NASM and WASM) uses `;` comments in the same voice.
* **Python** uses `#` comments in the same voice. A docstring is one short line, or a few when it carries usage. argparse takes its description from the first line of a module's docstring, so keep that line wherever it exists.

## File headers

C files start with a block like the one in `src/esfmpat.c`:

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

* The first lines say what the file is, in plain words ("Calls into the ES1869.VXD API ..."), without a "name.c -- " prefix.
* A short `Notes:` or `Usage:` block may follow.
* Files added since the 2024 release carry the year 2026. `esfmpat.c` and `1869opl3.c` keep 2024, and `regs.c`, `main.c` and `debug.c` have no header and should stay that way.
* A line holding only ` *` separates the copyright line from "Licensed under GPL Version 3.0".
* Assembly files use the same block with `;`, and shell scripts and `.mk1` files use it with `#`.
* Code disassembled from ESS's drivers (`src/vxd/*.asm`, `src/esfm/seg*.asm`) has no copyright line.

In Python the shebang stays on the first line:

```
#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Build ES1869.VXD from the source in src/vxd.

usage: build_vxd.py [--stock] [--verify] [-o OUTPUT]
"""
```

## Documentation

The documentation is written as a careful engineer would write for another engineer: in complete sentences, in plain words, and with nothing that doesn't help the reader.

**Write whole sentences.** Every sentence has a subject and a verb. Don't drop articles, verbs or connecting words to save space. "The format as ESS's device 0 takes it: PCM, 8 or 16 bits" becomes "The player accepts the formats ESS's first device accepts: PCM samples of 8 or 16 bits".

**Explain in paragraphs and list in bullets.** A paragraph is the place to say how something works or why it matters. Bullets are for items that are parallel and separate, such as steps, options, files or cases. When a bullet needs sub-bullets to make sense, it usually wants to be a paragraph.

**Connect the ideas.** Words such as *because*, *so*, *which*, *when*, *until*, *instead* and *but* show how one fact leads to the next. A colon doesn't join two fragments into a sentence, and a sentence has one colon at most, which introduces a list, an example or an explanation.

**Keep the essentials out of parentheses.** Parentheses hold references: a register (mixer 71h bit 1), a data sheet page (DS p.64) or an address in a driver (6:1FF4). Anything the reader needs in order to follow the text belongs in the sentence itself.

**Use labels sparingly.** A bold label at the start of a paragraph or bullet helps in a reference list that readers scan, but not on every item. Headings carry the structure.

**Make asides part of the text.** Instead of an italic *Note:*, write the caveat as a sentence in the place where it applies. A separate note is only for a warning the reader must not miss.

**Be precise and plain.** Call each thing by the same name every time ("the Audio 1 DAC", "ESS's driver", "the rebuilt driver") and prefer the specific word to the general one. Leave out filler and hype such as *robust*, *seamless*, *comprehensive*, *leverage*, *ensure*, *crucial*, *simply*, *just* or *it's worth noting*.

**Say each thing once.** Put it where the reader will look for it, and link to it from elsewhere. Cut sentences that restate the heading or sum up what was just said. Don't hedge: when something is uncertain, say what is known and how it was found out.

**Voice.** Use the present tense and the active voice. Address the reader as "you" and give steps in the imperative. Never write "we", and never use em dashes: use a comma, parentheses, a colon or a new sentence instead.

**Form.** Headings are in sentence case. Code, commands, file names, registers named by key and identifiers go in backticks, procedures in numbered lists, and commands and their output in fenced blocks. Tables may use phrases rather than sentences in their cells, as long as the phrases read naturally.

**Keep the facts.** A rewrite keeps every table, fact, address, register, step and link.

For example, this bullet:

> * **No data.** The player writes silence and the DMA keeps running: stopping takes DSP commands, which ESS's driver never sends at interrupt time. The next data goes 1/128 s ahead of the DMA, and the position leaves the silence out.

reads better as a paragraph:

> When the program runs out of data, the player writes silence and lets the DMA run on. Stopping the DMA would take DSP commands, and ESS's driver never sends those at interrupt time. New data then goes in 1/128 s ahead of the DMA, and the position the program reads doesn't count the silence.

## What a rewording never changes

* Code, identifiers, numbers, macro names, labels, string literals, and any text the user sees: messages, menus, dialogs, INI keys and command-line switches.
* Generated files, except through their generator. `docs/REGISTERS.md` comes from `tools/regdoc.py` and the notes in `src/esscat.tbl`.
* The object-offset comments (`; 0D28`) in `src/vxd/*.asm` and `src/esfm/seg*.asm`. Only the prose comments there may change.
* Text that the tests check, profile keys, `layout.json` and the `*.bin` files.
* Heading anchors that other files link to. A renamed heading means updating every link to it.

`tools/guard.py` proves that a rewording changed only comments and docstrings.
