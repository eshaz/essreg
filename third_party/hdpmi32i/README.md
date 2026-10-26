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
newest release at the time of the copy, unchanged:
* `HDPMI32i.zip`: 25173 bytes, SHA-256
  `69a559f02a954afa2550bf8840adfaa27dad7eeeab93a12b2642c283bf3d5bcb`, the
  hash that SBEMU's release script checks too.
* `HDPMI32i.EXE`: 38646 bytes, dated 2024-03-02 in the zip, SHA-256
  `f6913b490ef30b709d94323889e2f857aa0d1bee0638e09ba6d9a917d3cb34e8`.

HX is freeware, copyright 1996-2020 Japheth (section 5 of `HXsrc.txt` in its
source), and no part of essreg's GPL-licensed code. The source of this build
is the fork's repository at that tag, where `Src/HDPMI/HDPMI.TXT` documents
the options, among them `-r`, which keeps HDPMI resident, and `-x`, which
makes it report 256 MB of memory at most.
