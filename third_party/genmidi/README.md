# GENMIDI

`GENMIDI.lmp` holds the General MIDI instruments of the Freedoom project for
the OPL2 and OPL3, in the format of the DMX sound library that Doom plays
its music with. `build/ESFM.DRV` carries its instruments for its OPL3
device, which plays MIDI on the ES1869's OPL3 compatibility mode
([ESFM_OPL3.md](../../docs/ESFM_OPL3.md)).

It is the `GENMIDI` lump of Freedoom 0.13.0, as Ubuntu 24.04 packages it:
* `freedoom_0.13.0-1_all.deb`: 19944248 bytes, SHA-256
  `b7dd84233d088c2089f06764f2546ca68d663054799cc0df97ac1195eafa77ae`.
* `freedoom2.wad` in the package: 28787752 bytes, SHA-256
  `5a9315fb7dedac6d11b42034d15b7f6828826b94c84c14078087fa4c97d88474`.
  `freedoom1.wad` holds the same lump.
* `GENMIDI.lmp` here, the lump as it is in the WAD: 11908 bytes, SHA-256
  `0bea035cca1ef715c8bbbc00a7eb207084f0f8c47f8900dc053e9b1923a7d770`.

Freedoom's `CREDITS` names Matt Cadirao (horncomposer) for the genmidi
instruments.

## The format

* 8 bytes: `#OPL_II#`.
* 175 instruments of 36 bytes: the 128 General MIDI programs, then the drums
  of notes 35 to 81 on channel 10.
* 175 names of 32 bytes, which the driver leaves out.

An instrument has a word of flags (1: fixed pitch, 4: two voices), a byte of
fine tuning for its second voice, its fixed note, and two voices of 16
bytes. A voice is the modulator's registers 20h, 60h, 80h and E0h, its key
scaling (40h bits 7:6) and level, the channel's register C0h (feedback and
connection), the carrier's six bytes in the same order, a byte that isn't
used and a signed word, the base note offset.

## License

Freedoom is distributed under the BSD license below (the `copyright` file of
the package, `Files: *`), which allows its use in essreg's GPL-licensed
driver as long as the notice stays with it.

```
Copyright (c) 2001-2024 Contributors to the Freedoom project
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of the Freedoom project nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
