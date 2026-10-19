# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Reader and writer for the LE (Linear Executable) files of Windows 9x VxDs.

The reader decodes every structure of a VxD and keeps enough raw data (MZ
stub, header, linker slack) for LEFile.to_bytes() to rebuild the file byte
for byte.  tools/lelink.py builds a VxD from reassembled objects with the
same writer, so its fixup encoding follows Microsoft LINK /VXD:

- every fixup is an internal reference with an 8-bit object number and a
  16-bit target offset (the 32-bit/16-bit flags are only set when needed)
- per page, fixups are visited in ascending source order.  A fixup whose
  (type, target) matches an earlier record of the same page is added to
  that record's source list, otherwise a new record goes in front of the
  others
- a fixup that straddles a page boundary is also recorded in the next page
  with a negative source offset
- the image bytes at a fixup site hold the addend of the original
  reference (almost always zero), which the loader overwrites
"""

import struct

LE_HDR_SIZE = 0xC4

# (name, offset, struct format) of every LE header field, VxD variant
HDR_FIELDS = [
    ("sig", 0x00, "2s"), ("border", 0x02, "B"), ("worder", 0x03, "B"),
    ("level", 0x04, "I"), ("cpu", 0x08, "H"), ("os", 0x0A, "H"),
    ("modver", 0x0C, "I"), ("mflags", 0x10, "I"), ("mpages", 0x14, "I"),
    ("startobj", 0x18, "I"), ("eip", 0x1C, "I"), ("stackobj", 0x20, "I"),
    ("esp", 0x24, "I"), ("pagesize", 0x28, "I"), ("lastpagesize", 0x2C, "I"),
    ("fixupsize", 0x30, "I"), ("fixupsum", 0x34, "I"), ("ldrsize", 0x38, "I"),
    ("ldrsum", 0x3C, "I"), ("objtab", 0x40, "I"), ("objcnt", 0x44, "I"),
    ("objmap", 0x48, "I"), ("itermap", 0x4C, "I"), ("rsrctab", 0x50, "I"),
    ("rsrccnt", 0x54, "I"), ("restab", 0x58, "I"), ("enttab", 0x5C, "I"),
    ("dirtab", 0x60, "I"), ("dircnt", 0x64, "I"), ("fpagetab", 0x68, "I"),
    ("frectab", 0x6C, "I"), ("impmod", 0x70, "I"), ("impmodcnt", 0x74, "I"),
    ("impproc", 0x78, "I"), ("pagesum", 0x7C, "I"), ("datapage", 0x80, "I"),
    ("preload", 0x84, "I"), ("nrestab", 0x88, "I"), ("cbnrestab", 0x8C, "I"),
    ("nressum", 0x90, "I"), ("autodata", 0x94, "I"), ("debuginfo", 0x98, "I"),
    ("debuglen", 0x9C, "I"), ("instpreload", 0xA0, "I"),
    ("instdemand", 0xA4, "I"), ("heapsize", 0xA8, "I"), ("res3", 0xAC, "12s"),
    ("winresoff", 0xB8, "I"), ("winreslen", 0xBC, "I"), ("devid", 0xC0, "H"),
    ("ddkver", 0xC2, "H"),
]

# object flags
OBJ_READ, OBJ_WRITE, OBJ_EXEC, OBJ_RSRC = 0x1, 0x2, 0x4, 0x8
OBJ_DISCARD, OBJ_SHARED, OBJ_PRELOAD, OBJ_INVALID = 0x10, 0x20, 0x40, 0x80
OBJ_BIG = 0x2000

# fixup source types
FIX_OFF32 = 7  # 32-bit offset
FIX_REL32 = 8  # 32-bit self-relative
FIXUP_WIDTH = {7: 4, 8: 4, 5: 2, 2: 2, 3: 4, 6: 6, 0: 1}

DATAPAGE_ALIGN = 0x1000


class LEObject:
    """One object (segment) of the image."""

    def __init__(self, index, vsize, base, flags, page_index, npages, name):
        self.index = index            # 1-based object number
        self.vsize = vsize
        self.base = base              # relocation base (0 for VxDs)
        self.flags = flags
        self.page_index = page_index  # 1-based index into the page map
        self.npages = npages
        self.name = name              # 4 raw bytes kept in the reserved field
        self.data = bytearray()       # image bytes (vsize bytes)

    @property
    def name_str(self):
        return self.name.rstrip(b"\0").decode("latin-1")

    def __repr__(self):
        return "<LEObject %d %s vsize=%#x flags=%#x pages=%d@%d>" % (
            self.index, self.name_str, self.vsize, self.flags, self.npages,
            self.page_index)


class Fixup:
    """An internal fixup: obj:off (source) refers to tobj:toff."""

    __slots__ = ("obj", "off", "type", "tobj", "toff")

    def __init__(self, obj, off, ftype, tobj, toff):
        self.obj, self.off, self.type, self.tobj, self.toff = (
            obj, off, ftype, tobj, toff)

    def key(self):
        return (self.obj, self.off)

    def __repr__(self):
        return "<Fixup o%d:%04x t%d -> o%d:%04x>" % (
            self.obj, self.off, self.type, self.tobj, self.toff)


class FixupRecord:
    """One encoded fixup record of a page (possibly with a source list)."""

    def __init__(self, src, flags, srcoffs, tobj, toff):
        self.src, self.flags = src, flags
        self.srcoffs, self.tobj, self.toff = srcoffs, tobj, toff

    @property
    def type(self):
        return self.src & 0x0F

    def encode(self):
        flags = self.flags & ~0x53  # recompute size flags / target type
        if self.tobj > 0xFF:
            flags |= 0x40
        if self.toff is not None and self.toff > 0xFFFF:
            flags |= 0x10
        src = self.type | (0x20 if len(self.srcoffs) > 1 else 0) | (
            self.src & 0xD0)
        out = bytearray((src, flags))
        if len(self.srcoffs) > 1:
            out.append(len(self.srcoffs))
        else:
            out += struct.pack("<h", self.srcoffs[0])
        out += struct.pack("<H" if flags & 0x40 else "<B", self.tobj)
        if self.type != 2:
            out += struct.pack("<I" if flags & 0x10 else "<H", self.toff)
        if len(self.srcoffs) > 1:
            for so in self.srcoffs:
                out += struct.pack("<h", so)
        return bytes(out)


def _cstr_table(data, off):
    """Parse a length-prefixed (name, ordinal) table ending with a 0 byte."""
    names = []
    while True:
        n = data[off]
        if n == 0:
            return names, off + 1
        name = data[off + 1:off + 1 + n]
        (ordinal,) = struct.unpack_from("<H", data, off + 1 + n)
        names.append((name, ordinal))
        off += 1 + n + 2


def _encode_name_table(names):
    out = bytearray()
    for name, ordinal in names:
        out.append(len(name))
        out += name + struct.pack("<H", ordinal)
    out.append(0)
    return bytes(out)


def encode_fixup_pages(pages, fixups, pagesize):
    """Encode fixups into per-page record lists (MS LINK conventions).

    pages has one entry per page-map entry: (obj_index, page_in_object),
    or None for a page that belongs to no object.  Returns a list of
    FixupRecord lists, one per page.
    """
    by_obj = {}
    for f in fixups:
        by_obj.setdefault(f.obj, []).append(f)
    for lst in by_obj.values():
        lst.sort(key=lambda f: f.off)
    out = []
    for entry in pages:
        records = []
        if entry is not None:
            obj, pno = entry
            start = pno * pagesize
            index = {}
            for f in by_obj.get(obj, ()):
                width = FIXUP_WIDTH[f.type]
                if f.off + width <= start or f.off >= start + pagesize:
                    continue
                so = f.off - start
                key = (f.type, f.tobj, f.toff)
                rec = index.get(key)
                if rec is None:
                    rec = FixupRecord(f.type, 0, [so], f.tobj, f.toff)
                    index[key] = rec
                    records.insert(0, rec)
                else:
                    rec.srcoffs.append(so)
        out.append(records)
    return out


class LEFile:
    """A parsed LE executable."""

    def __init__(self, data):
        self.raw = bytes(data)
        d = self.raw
        if d[:2] != b"MZ":
            raise ValueError("not an MZ executable")
        (self.lfanew,) = struct.unpack_from("<I", d, 0x3C)
        if d[self.lfanew:self.lfanew + 2] != b"LE":
            raise ValueError("not an LE executable")
        self.stub = d[:self.lfanew]
        h = self.lfanew
        self.hdr = {}
        for name, off, fmt in HDR_FIELDS:
            (self.hdr[name],) = struct.unpack_from("<" + fmt, d, h + off)
        hd = self.hdr
        self.pagesize = hd["pagesize"]

        # object table
        self.objects = []
        for i in range(hd["objcnt"]):
            vs, base, fl, pti, npe, res = struct.unpack_from(
                "<6I", d, h + hd["objtab"] + i * 24)
            self.objects.append(
                LEObject(i + 1, vs, base, fl, pti, npe, struct.pack("<I", res)))

        # page map (LE format: 3-byte big-endian page number + flags)
        self.pagemap = []
        for j in range(hd["mpages"]):
            b = d[h + hd["objmap"] + j * 4:h + hd["objmap"] + j * 4 + 4]
            self.pagemap.append(((b[0] << 16) | (b[1] << 8) | b[2], b[3]))

        self.resident_names, _ = _cstr_table(d, h + hd["restab"])
        self.entry_raw = d[h + hd["enttab"]:h + hd["fpagetab"]] \
            if hd["enttab"] else b""
        self.entries = self._parse_entries(h + hd["enttab"])

        # page data
        self.pages = []
        for j in range(hd["mpages"]):
            pn, _ = self.pagemap[j]
            size = hd["lastpagesize"] if pn == hd["mpages"] else self.pagesize
            off = hd["datapage"] + (pn - 1) * self.pagesize
            self.pages.append(d[off:off + size])

        for o in self.objects:
            buf = bytearray()
            for p in range(o.npages):
                buf += self.pages[o.page_index - 1 + p]
            buf = buf[:o.vsize]
            buf += bytes(max(0, o.vsize - len(buf)))
            o.data = buf

        # fixups
        self.page_records = []
        for j in range(hd["mpages"]):
            s, e = struct.unpack_from("<II", d, h + hd["fpagetab"] + j * 4)
            self.page_records.append(
                self._parse_records(h + hd["frectab"] + s, h + hd["frectab"] + e))
        self.fixups = self._flatten_fixups()

        # everything after the fixup/import section up to the first page
        imp_end = h + hd["impproc"]
        self.gap = d[imp_end:hd["datapage"]]
        data_end = hd["datapage"] + (hd["mpages"] - 1) * self.pagesize + \
            hd["lastpagesize"] if hd["mpages"] else hd["datapage"]
        self.nonres_names, _ = _cstr_table(d, hd["nrestab"]) \
            if hd["nrestab"] else ([], 0)
        self.nonres_raw = d[hd["nrestab"]:hd["nrestab"] + hd["cbnrestab"]]
        self.winres = d[hd["winresoff"]:hd["winresoff"] + hd["winreslen"]] \
            if hd["winresoff"] else b""
        self.data_end = data_end

    # -- parsing helpers ----------------------------------------------------

    def _parse_entries(self, off):
        d = self.raw
        entries = {}
        ordinal = 1
        if not self.hdr["enttab"]:
            return entries
        while True:
            cnt = d[off]
            if cnt == 0:
                break
            etype = d[off + 1]
            off += 2
            if etype == 0:
                ordinal += cnt
                continue
            (obj,) = struct.unpack_from("<H", d, off)
            off += 2
            for _ in range(cnt):
                if etype == 3:
                    fl, eoff = struct.unpack_from("<BI", d, off)
                    off += 5
                elif etype == 1:
                    fl, eoff = struct.unpack_from("<BH", d, off)
                    off += 3
                else:
                    raise ValueError("unsupported entry bundle type %d" % etype)
                entries[ordinal] = (obj, eoff, fl)
                ordinal += 1
        return entries

    def _parse_records(self, r, end):
        d = self.raw
        recs = []
        while r < end:
            src, flags = d[r], d[r + 1]
            r += 2
            if src & 0x20:
                cnt = d[r]
                r += 1
            else:
                (so,) = struct.unpack_from("<h", d, r)
                r += 2
            if flags & 0x03:
                raise ValueError("import fixups are not supported")
            if flags & 0x40:
                (tobj,) = struct.unpack_from("<H", d, r)
                r += 2
            else:
                tobj = d[r]
                r += 1
            if src & 0x0F == 2:
                toff = None
            elif flags & 0x10:
                (toff,) = struct.unpack_from("<I", d, r)
                r += 4
            else:
                (toff,) = struct.unpack_from("<H", d, r)
                r += 2
            if src & 0x20:
                offs = list(struct.unpack_from("<%dh" % cnt, d, r))
                r += 2 * cnt
            else:
                offs = [so]
            recs.append(FixupRecord(src, flags, offs, tobj, toff))
        return recs

    def page_owner(self):
        """Map page-map index (0-based) -> (object index, page in object)."""
        owner = [None] * len(self.pagemap)
        for o in self.objects:
            for p in range(o.npages):
                owner[o.page_index - 1 + p] = (o.index, p)
        return owner

    def _flatten_fixups(self):
        owner = self.page_owner()
        seen = {}
        for j, recs in enumerate(self.page_records):
            if owner[j] is None:
                continue
            obj, pno = owner[j]
            for rec in recs:
                for so in rec.srcoffs:
                    off = pno * self.pagesize + so
                    k = (obj, off)
                    f = Fixup(obj, off, rec.type, rec.tobj, rec.toff)
                    if k in seen and (seen[k].tobj, seen[k].toff, seen[k].type) \
                            != (f.tobj, f.toff, f.type):
                        raise ValueError("conflicting fixups at %r" % (k,))
                    seen[k] = f
        return sorted(seen.values(), key=Fixup.key)

    # -- convenience --------------------------------------------------------

    def ddb(self):
        """Return (object, offset) of the DDB (export ordinal 1)."""
        obj, off, _ = self.entries[1]
        return obj, off

    def fixup_map(self):
        return {f.key(): f for f in self.fixups}

    # -- writing ------------------------------------------------------------

    def to_bytes(self, keep_gap=True):
        """Serialize the (possibly edited) model."""
        return build_le(self.stub, self.hdr, self.objects, self.fixups,
                        self.resident_names, self.entry_raw, self.nonres_names,
                        self.winres, gap=self.gap if keep_gap else b"",
                        orphan_pages=self.orphan_pages())

    def orphan_pages(self):
        """Pages that belong to no object, keyed by the object they follow."""
        owner = self.page_owner()
        runs = {}
        prev_obj = 0
        for j, entry in enumerate(owner):
            if entry is None:
                runs.setdefault(prev_obj, []).append(self.pages[j])
            else:
                prev_obj = entry[0]
        return runs


def build_le(stub, hdr_in, objects, fixups, resident_names, entry_raw,
             nonres_names, winres, gap=b"", orphan_pages=None):
    """Build an LE/VxD file from its parts.

    hdr_in supplies the fields that don't come from the contents
    (signature, CPU/OS, module flags, page size, device ID, DDK version...).
    orphan_pages maps an object index to a list of raw pages placed right
    after that object (0 = before the first object).
    """
    orphan_pages = orphan_pages or {}
    hdr = dict(hdr_in)
    pagesize = hdr["pagesize"]

    # lay out pages
    pages = []
    owner = []
    for raw in orphan_pages.get(0, []):
        pages.append(bytes(raw))
        owner.append(None)
    for o in objects:
        n = max(1, (len(o.data) + pagesize - 1) // pagesize) if o.data else 0
        o.vsize = len(o.data)
        o.page_index = len(pages) + 1
        o.npages = n
        for p in range(n):
            chunk = bytes(o.data[p * pagesize:(p + 1) * pagesize])
            pages.append(chunk + bytes(pagesize - len(chunk)))
            owner.append((o.index, p))
        for raw in orphan_pages.get(o.index, []):
            pages.append(bytes(raw))
            owner.append(None)

    # the image bytes of the last page are truncated to their used length
    last = pages[-1]
    if owner[-1] is not None:
        o = objects[owner[-1][0] - 1]
        used = len(o.data) - owner[-1][1] * pagesize
        # the header holds the used size, the file data is dword padded
        lastpagesize = used
        last = last[:(used + 3) & ~3]
    else:
        last = last.rstrip(b"\0") or last[:1]
        lastpagesize = len(last)
    pages[-1] = last

    # loader section
    h = len(stub)
    objtab = LE_HDR_SIZE
    obj_bytes = bytearray()
    for o in objects:
        obj_bytes += struct.pack("<5I", o.vsize, o.base, o.flags, o.page_index,
                                 o.npages) + o.name
    objmap = objtab + len(obj_bytes)
    map_bytes = bytearray()
    for j in range(len(pages)):
        pn = j + 1
        map_bytes += bytes(((pn >> 16) & 0xFF, (pn >> 8) & 0xFF, pn & 0xFF, 0))
    restab = objmap + len(map_bytes)
    res_bytes = _encode_name_table(resident_names)
    enttab = restab + len(res_bytes)
    fpagetab = enttab + len(entry_raw)

    # fixup section
    records = encode_fixup_pages(owner, fixups, pagesize)
    rec_bytes = bytearray()
    table = []
    for recs in records:
        table.append(len(rec_bytes))
        for rec in recs:
            rec_bytes += rec.encode()
    table.append(len(rec_bytes))
    fpt_bytes = struct.pack("<%dI" % len(table), *table)
    frectab = fpagetab + len(fpt_bytes)
    impmod = frectab + len(rec_bytes)

    loader_end = h + impmod
    # LINK /VXD starts the data pages on a 4 KB file boundary
    datapage = (loader_end + DATAPAGE_ALIGN - 1) // DATAPAGE_ALIGN * \
        DATAPAGE_ALIGN
    if gap and len(gap) == datapage - loader_end:
        gap_bytes = bytes(gap)
    else:
        gap_bytes = bytes(datapage - loader_end)

    page_data = b"".join(pages)
    nrestab = datapage + len(page_data)
    nonres_bytes = _encode_name_table(nonres_names)
    winresoff = nrestab + len(nonres_bytes) if winres else 0

    hdr.update(
        mpages=len(pages), lastpagesize=lastpagesize,
        fixupsize=impmod - fpagetab, fixupsum=0, ldrsize=fpagetab - objtab,
        ldrsum=0, objtab=objtab, objcnt=len(objects), objmap=objmap,
        itermap=0, rsrctab=0, rsrccnt=0, restab=restab, enttab=enttab,
        dirtab=0, dircnt=0, fpagetab=fpagetab, frectab=frectab, impmod=impmod,
        impmodcnt=0, impproc=impmod, pagesum=0, datapage=datapage,
        preload=sum(o.npages for o in objects if o.flags & OBJ_PRELOAD),
        nrestab=nrestab, cbnrestab=len(nonres_bytes), nressum=0,
        winresoff=winresoff, winreslen=len(winres))

    hdr_bytes = bytearray(LE_HDR_SIZE)
    for name, off, fmt in HDR_FIELDS:
        struct.pack_into("<" + fmt, hdr_bytes, off, hdr[name])

    out = bytearray(stub)
    out += hdr_bytes + obj_bytes + map_bytes + res_bytes + entry_raw
    out += fpt_bytes + rec_bytes + gap_bytes
    assert len(out) == datapage
    out += page_data + nonres_bytes + winres
    return bytes(out)


def read_le(path):
    with open(path, "rb") as f:
        return LEFile(f.read())
