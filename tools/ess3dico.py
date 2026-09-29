#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Write the tray icons of ess3d, src/win/ess3don.ico and ess3doff.ico.

usage: ess3dico.py
"""

import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# "3D" in 16 x 16, the 32 x 32 image is the same doubled, so the tray's
# shrinking of the 32 x 32 one (16-bit programs only load that size)
# gives back this one.  O outline, X letters
ART = [
    "................",
    "................",
    "................",
    ".OOOOOO.OOOOO...",
    ".OXXXXO.OXXXXO..",
    ".OOOOXO.OXOOXXO.",
    "...OOXO.OXO.OXO.",
    ".OOXXXO.OXO.OXO.",
    ".OXXXOO.OXO.OXO.",
    ".OOOOXO.OXO.OXO.",
    "...OOXO.OXO.OXO.",
    ".OOOOXO.OXOOXXO.",
    ".OXXXXO.OXXXXO..",
    ".OOOOOO.OOOOO...",
    "................",
    "................",
]

# the 16 colors of the VGA palette, index: (r, g, b)
PALETTE = [
    (0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128),
    (128, 0, 128), (0, 128, 128), (192, 192, 192), (128, 128, 128),
    (255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 255),
    (0, 255, 255), (255, 255, 255)]
BLACK, GRAY, GREEN = 0, 8, 10


def image(size, letters):
    """BITMAPINFOHEADER, palette, 4-bit color bits and the AND mask."""
    k = size // 16
    rows = [[ART[y // k][x // k] for x in range(size)] for y in range(size)]
    color = {"O": BLACK, "X": letters, ".": BLACK}
    xor = bytearray()
    mask = bytearray()
    for row in reversed(rows):              # bottom-up
        nib = [color[c] for c in row]
        line = bytes((nib[i] << 4) | nib[i + 1] for i in range(0, size, 2))
        xor += line + bytes(-len(line) % 4)
        bits = 0
        line = bytearray()
        for i, c in enumerate(row):
            bits = (bits << 1) | (c == ".")   # 1 = transparent
            if i % 8 == 7:
                line.append(bits)
                bits = 0
        mask += line + bytes(-len(line) % 4)
    head = struct.pack("<IiiHHIIiiII", 40, size, 2 * size, 1, 4, 0,
                       len(xor) + len(mask), 0, 0, 16, 0)
    pal = b"".join(bytes((b, g, r, 0)) for r, g, b in PALETTE)
    return head + pal + bytes(xor) + bytes(mask)


def ico(letters):
    images = [image(s, letters) for s in (32, 16)]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, data in zip((32, 16), images):
        out += struct.pack("<BBBBHHII", s, s, 16, 0, 1, 4, len(data), offset)
        offset += len(data)
    return out + b"".join(images)


def main():
    for name, letters in (("ess3don.ico", GREEN), ("ess3doff.ico", GRAY)):
        path = os.path.join(ROOT, "src", "win", name)
        with open(path, "wb") as f:
            f.write(ico(letters))
        print(os.path.relpath(path, ROOT))


if __name__ == "__main__":
    main()
