#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Link a NASM ``-f elf32`` object into a Windows 9x VxD (LE) file.

Each ELF section named in the layout becomes one LE object.  Relocations
become LE internal fixups:

* ``R_386_32``   -> type 7 (32-bit offset); the image keeps the in-place
  addend relative to the symbol (the original LINK convention);
* ``R_386_PC32`` -> type 8 (32-bit self-relative) for cross-object calls
  and jumps; the image bytes are zero, as LINK wrote them.

Relocations should reference named (global) symbols so that the in-place
addend is the displacement from that symbol.  References through section
symbols are accepted, but their image bytes are written as zero.

usage: lelink.py OBJECT.o LAYOUT.json -o OUT.VXD
"""

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from retools import elf32  # noqa: E402
from retools.le import (Fixup, LEObject, build_le, FIX_OFF32,  # noqa: E402
                        FIX_REL32, HDR_FIELDS)

COMPUTED = {
    "mpages", "lastpagesize", "fixupsize", "fixupsum", "ldrsize", "ldrsum",
    "objtab", "objcnt", "objmap", "itermap", "rsrctab", "rsrccnt", "restab",
    "enttab", "dirtab", "dircnt", "fpagetab", "frectab", "impmod",
    "impmodcnt", "impproc", "pagesum", "datapage", "preload", "nrestab",
    "cbnrestab", "nressum", "winresoff", "winreslen",
}


class LinkError(Exception):
    pass


def encode_entry_table(entries):
    """entries: list of (ordinal, object, offset, flags) -> raw LE entry table."""
    out = bytearray()
    ordinal = 1
    entries = sorted(entries)
    i = 0
    while i < len(entries):
        o = entries[i][0]
        while ordinal < o:  # skip unused ordinals
            n = min(255, o - ordinal)
            out += bytes((n, 0))
            ordinal += n
        obj = entries[i][1]
        bundle = []
        while (i < len(entries) and entries[i][1] == obj and
               entries[i][0] == ordinal + len(bundle) and len(bundle) < 255):
            bundle.append(entries[i])
            i += 1
        out += bytes((len(bundle), 3)) + struct.pack("<H", obj)
        for _o, _obj, off, flags in bundle:
            out += struct.pack("<BI", flags, off)
        ordinal += len(bundle)
    out.append(0)
    return bytes(out)


def _read_blob(base, name):
    if not name:
        return b""
    with open(os.path.join(base, name), "rb") as f:
        return f.read()


def link(elf, layout, layout_dir):
    """Return (le_bytes, objects, fixups)."""
    secidx = {}
    objects = []
    for n, desc in enumerate(layout["objects"], 1):
        sec = elf.section(desc["section"])
        if sec is None:
            raise LinkError("section %s missing from the object" % desc["section"])
        o = LEObject(n, len(sec.data), desc.get("base", 0), desc["flags"],
                     0, 0, desc["name"].encode("latin-1").ljust(4, b"\0")[:4])
        o.data = bytearray(sec.data)
        objects.append(o)
        secidx[sec.index] = n

    for sec in elf.sections:
        if sec.relocs and sec.index not in secidx and sec.type == elf32.SHT_PROGBITS:
            raise LinkError("section %s has relocations but no object" % sec.name)

    fixups = []
    for sec in elf.sections:
        obj = secidx.get(sec.index)
        if obj is None:
            continue
        data = objects[obj - 1].data
        for off, rtype, symidx in sec.relocs:
            sym = elf.symbols[symidx]
            tobj = secidx.get(sym.shndx)
            if tobj is None:
                raise LinkError("unresolved symbol %r referenced from %s+%#x" %
                                (sym.name, sec.name, off))
            is_section_sym = sym.type == 3
            (addend,) = struct.unpack_from("<i", data, off)
            if rtype == elf32.R_386_32:
                toff = sym.value + addend
                inplace = 0 if is_section_sym else addend
                ftype = FIX_OFF32
            elif rtype == elf32.R_386_PC32:
                if tobj == obj:
                    raise LinkError("same-object PC32 relocation at %s+%#x" %
                                    (sec.name, off))
                toff = sym.value + addend + 4
                inplace = 0 if is_section_sym else addend + 4
                ftype = FIX_REL32
            else:
                raise LinkError("unsupported relocation type %d" % rtype)
            if not 0 <= toff <= len(objects[tobj - 1].data):
                raise LinkError("fixup target outside object at %s+%#x" %
                                (sec.name, off))
            struct.pack_into("<I", data, off, inplace & 0xFFFFFFFF)
            fixups.append(Fixup(obj, off, ftype, tobj, toff))

    syms = elf.symbol_map()
    entries = []
    for e in layout.get("entries", []):
        if e["symbol"] not in syms:
            raise LinkError("entry symbol %s not defined" % e["symbol"])
        shndx, value = syms[e["symbol"]]
        entries.append((e["ordinal"], secidx[shndx], value, e.get("flags", 3)))

    hdr = {}
    for name, _off, fmt in HDR_FIELDS:
        if name in COMPUTED:
            hdr[name] = 0
            continue
        v = layout["header"][name]
        if fmt.endswith("s"):
            v = bytes.fromhex(v) if name == "res3" else v.encode("latin-1")
        hdr[name] = v

    orphan = {}
    for spec in layout.get("orphan_pages", []):
        raw = _read_blob(layout_dir, spec["file"])
        ps = hdr["pagesize"]
        pages = [raw[i:i + ps] for i in range(0, len(raw), ps)]
        after = 0
        for n, desc in enumerate(layout["objects"], 1):
            if desc["name"] == spec["after"]:
                after = n
        orphan[after] = pages

    def names(key):
        return [(n.encode("latin-1"), o) for n, o in layout.get(key, [])]

    data = build_le(
        _read_blob(layout_dir, layout["stub"]), hdr, objects, fixups,
        names("resident_names"), encode_entry_table(entries),
        names("nonresident_names"), _read_blob(layout_dir, layout.get("winres")),
        gap=_read_blob(layout_dir, layout.get("gap")), orphan_pages=orphan)
    return data, objects, fixups


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("object")
    ap.add_argument("layout")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()
    with open(args.layout) as f:
        layout = json.load(f)
    try:
        data, _o, _f = link(elf32.read_elf(args.object), layout,
                            os.path.dirname(os.path.abspath(args.layout)))
    except LinkError as e:
        sys.exit("lelink: error: %s" % e)
    with open(args.output, "wb") as f:
        f.write(data)


if __name__ == "__main__":
    main()
