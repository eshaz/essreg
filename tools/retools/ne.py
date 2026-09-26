# SPDX-License-Identifier: GPL-3.0-or-later
"""Reader for 16-bit Windows NE executables and drivers (ESFM.DRV,
ES1869.DRV, ESSMPU.DRV, essctl.exe).

  ne = NEFile(data)
  ne.segments[i]          .offset .length .flags .minalloc .relocs
  ne.resources            list of Resource(type, id, offset, length, flags)
  ne.entries              {ordinal: (segment, offset, flags)}
  ne.resident / nonresident names, ne.modules (imported module names)

Only reading is needed by the tools; esfmpat edits files itself (in C).
"""

import struct

SEG_DATA = 0x0001
SEG_MOVEABLE = 0x0010
SEG_PRELOAD = 0x0040
SEG_RELOCINFO = 0x0100
SEG_DISCARDABLE = 0x1000

RT_NAMES = {1: "CURSOR", 2: "BITMAP", 3: "ICON", 4: "MENU", 5: "DIALOG",
            6: "STRING", 7: "FONTDIR", 8: "FONT", 9: "ACCELERATOR",
            10: "RCDATA", 12: "GROUP_CURSOR", 14: "GROUP_ICON",
            16: "VERSION"}


class Segment:
    def __init__(self, index, offset, length, flags, minalloc):
        self.index = index          # 1-based
        self.offset = offset        # file offset of the data, 0 if none
        self.length = length        # bytes in the file
        self.flags = flags
        self.minalloc = minalloc
        self.relocs = []            # (type, flags, offset, target tuple)

    @property
    def is_data(self):
        return bool(self.flags & SEG_DATA)

    def __repr__(self):
        return "Segment(%d, off=%#x, len=%#x, flags=%#06x, min=%#x)" % (
            self.index, self.offset, self.length, self.flags, self.minalloc)


class Resource:
    def __init__(self, rtype, rid, offset, length, flags, entry_pos):
        self.type = rtype           # int (numeric) or str (named)
        self.id = rid               # int or str
        self.offset = offset        # file offset
        self.length = length        # bytes
        self.flags = flags
        self.entry_pos = entry_pos  # file offset of the table entry

    def __repr__(self):
        return "Resource(type=%r, id=%r, off=%#x, len=%#x)" % (
            self.type, self.id, self.offset, self.length)


class NEFile:
    def __init__(self, data):
        self.data = bytes(data)
        d = self.data
        if d[:2] != b"MZ":
            raise ValueError("not an MZ executable")
        self.ne = ne = struct.unpack_from("<I", d, 0x3C)[0]
        if d[ne:ne + 2] != b"NE":
            raise ValueError("no NE header")
        h = struct.unpack_from("<2sBBHHIHHHHIIHHHHHHHHIHHHBBHHHH", d, ne)
        (_, self.linker_ver, self.linker_rev, self.entry_off, self.entry_len,
         self.crc, self.flags, self.autodata, self.heap, self.stack,
         self.csip, self.sssp, self.nseg, self.nmod, self.nonres_len,
         self.seg_off, self.res_off, self.resname_off, self.modref_off,
         self.impname_off, self.nonres_off, self.nmovable, self.align,
         self.nres_seg, self.target_os, self.other_flags, self.gang_off,
         self.gang_len, self.swap_min, self.expected_ver) = h
        self.segments = self._segments()
        self.resources, self.res_align = self._resources()
        self.modules = self._modules()
        self.resident = self._names(ne + self.resname_off)
        self.nonresident = self._names(self.nonres_off) \
            if self.nonres_len else []
        self.entries = self._entries()

    # --- tables ------------------------------------------------------------

    def _segments(self):
        d, ne = self.data, self.ne
        segs = []
        for i in range(self.nseg):
            off, length, flags, minalloc = struct.unpack_from(
                "<HHHH", d, ne + self.seg_off + 8 * i)
            seg = Segment(i + 1, off << self.align,
                          length or (0x10000 if off else 0), flags, minalloc)
            if off and flags & SEG_RELOCINFO:
                pos = seg.offset + seg.length
                count = struct.unpack_from("<H", d, pos)[0]
                for k in range(count):
                    rtype, rflags, roff, a, b = struct.unpack_from(
                        "<BBHHH", d, pos + 2 + 8 * k)
                    seg.relocs.append((rtype, rflags, roff, (a, b)))
            segs.append(seg)
        return segs

    def _string_at(self, table, offset):
        n = self.data[table + offset]
        return self.data[table + offset + 1:table + offset + 1 + n].decode(
            "latin-1")

    def _resources(self):
        d, ne = self.data, self.ne
        if self.res_off == self.resname_off:
            return [], 0
        base = ne + self.res_off
        shift = struct.unpack_from("<H", d, base)[0]
        pos = base + 2
        res = []
        while True:
            rtype = struct.unpack_from("<H", d, pos)[0]
            if rtype == 0:
                break
            count = struct.unpack_from("<H", d, pos + 2)[0]
            tname = rtype & 0x7FFF if rtype & 0x8000 else \
                self._string_at(base, rtype)
            pos += 8
            for _ in range(count):
                off, length, flags, rid = struct.unpack_from("<HHHH", d, pos)
                name = rid & 0x7FFF if rid & 0x8000 else \
                    self._string_at(base, rid)
                res.append(Resource(tname, name, off << shift,
                                    length << shift, flags, pos))
                pos += 12
        return res, shift

    def _modules(self):
        d, ne = self.data, self.ne
        out = []
        for i in range(self.nmod):
            off = struct.unpack_from("<H", d, ne + self.modref_off + 2 * i)[0]
            out.append(self._string_at(ne + self.impname_off, off))
        return out

    def _names(self, pos):
        d = self.data
        out = []
        while d[pos]:
            n = d[pos]
            name = d[pos + 1:pos + 1 + n].decode("latin-1")
            out.append((name, struct.unpack_from("<H", d, pos + 1 + n)[0]))
            pos += n + 3
        return out

    def _entries(self):
        d = self.data
        pos = self.ne + self.entry_off
        end = pos + self.entry_len
        ordinal = 1
        out = {}
        while pos < end:
            count, seg = d[pos], d[pos + 1]
            pos += 2
            if count == 0:
                break
            for _ in range(count):
                if seg == 0:
                    pass
                elif seg == 0xFF:
                    flags, _int3f, segno, off = struct.unpack_from(
                        "<BHBH", d, pos)
                    out[ordinal] = (segno, off, flags)
                    pos += 6
                else:
                    flags, off = struct.unpack_from("<BH", d, pos)
                    out[ordinal] = (seg, off, flags)
                    pos += 3
                ordinal += 1
        return out

    # --- helpers -----------------------------------------------------------

    def resource(self, rtype, rid):
        for r in self.resources:
            if r.type == rtype and r.id == rid:
                return r
        return None

    def resource_data(self, rtype, rid):
        r = self.resource(rtype, rid)
        return None if r is None else self.data[r.offset:r.offset + r.length]

    def segment_data(self, index):
        s = self.segments[index - 1]
        return self.data[s.offset:s.offset + s.length]

    @property
    def expected_version(self):
        return (self.expected_ver >> 8, self.expected_ver & 0xFF)

    @property
    def module_name(self):
        return self.resident[0][0] if self.resident else ""
