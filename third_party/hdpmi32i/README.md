# HDPMI32i

`HDPMI32i.EXE` is HDPMI 3.20, the DPMI host of Japheth's HX DOS extender, in
the build that runs its programs with IOPL 0, so that it can trap their port
accesses. [`esssb16`](../../README.md#esssb16com) registers its port traps
with it for protected-mode games, as
[SBEMU](https://github.com/crazii/sbemu) does, and this copy saves the
download: [Installing esssb16](../../docs/ESSSB16.md#installing-esssb16)
says where it goes.

It is the one file of `HDPMI32i.zip` from release v0.1-beta4fix2 of crazii's
HX fork, <https://github.com/crazii/HX/releases/tag/v0.1-beta4fix2>, the
newest release at the time of the copy, with a fix of four bytes:
* `HDPMI32i.zip`: 25173 bytes, SHA-256
  `69a559f02a954afa2550bf8840adfaa27dad7eeeab93a12b2642c283bf3d5bcb`, the
  hash that SBEMU's release script checks too.
* `HDPMI32i.EXE` in the zip: 38646 bytes, dated 2024-03-02, SHA-256
  `f6913b490ef30b709d94323889e2f857aa0d1bee0638e09ba6d9a917d3cb34e8`.
* `HDPMI32i.EXE` here, with the fix: 38646 bytes, SHA-256
  `9c76879685a84649c8bd2e835c0151530647a2367ef5323e8adac1c97613cb0c`.

## The fix

A program's STI faults at IOPL 0, and HDPMI32i emulates it (`emuprivinstr`
in `Src/HDPMI/HDPMI.ASM`). An STI with interrupts off that comes right
before an IRETD, as at the end of a hardware interrupt's handler, has the
host do the IRETD too, from the program's stack. The code pushes EDI first
and then reaches the fault's frame at the offsets it has without the push, 4
bytes short: it takes the program's EFLAGS and the low word of its ESP for
the stack's offset and selector, and faults at ring 0, which stops the
machine. Absolute Pinball's `SETUP.EXE` and `ABS.EXE` end their interrupt
handlers that way, so with the release's file the laptop stopped at the
first interrupt of `SETUP`'s test sound. Every release of the fork from
v0.1-beta2 to v0.1-beta4fix2 has this code.

The fix gives the four instructions after the push the offsets they need,
each 4 more: at 2F70h, 2F79h, 2F85h and 2F89h in the file, 20h to 24h, 18h
to 1Ch, 1Ch to 20h and 20h to 24h. The instructions keep their lengths.
[`tools/hdpmifix.py`](../../tools/hdpmifix.py) makes it from the release's
file, finding the code by its bytes, so it fixes the same build of the other
releases too, and `--check` tells the two apart.
[`tests/test_hdpmifix.py`](../../tests/test_hdpmifix.py) runs the emulation
from both files.

## License

HX is freeware, copyright 1996-2020 Japheth (section 5 of `HXsrc.txt` in its
source), and no part of essreg's GPL-licensed code. The source of this build
is the fork's repository at that tag, where `Src/HDPMI/HDPMI.TXT` documents
the options, among them `-r`, which keeps HDPMI resident, and `-x`, which
makes it report 256 MB of memory at most.
