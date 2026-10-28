#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Link a 16-bit Windows NE module from nasm -f bin output.

usage: data = link(binary, layout, srcdir)

The source (see tools/ne2asm.py) assembles every segment into its own
section, which holds the segment's bytes and then its relocation table,
exactly as they are in the file.  The last section, link, holds the
lengths the linker needs and the offsets of the exported entry points:

    db 'NELINK', 1, 0
    dw <for each segment: data length, data + relocation table length>
    dw <for each symbol in layout["symbols"]: its offset>

layout.json describes the rest: the MZ stub, the NE header fields, segment
flags, resources, the name and entry tables, and the order of the segments
and resources in the file.
"""

import os
import struct

MAGIC = b"NELINK\x01\x00"


class LinkError(Exception):
    pass


def _pstr(s):
    b = s.encode("latin-1")
    if len(b) > 255:
        raise LinkError("name too long: %r" % s)
    return bytes([len(b)]) + b


def split_sections(binary, layout):
    """Return {segment index: (data, relocation table)} and the symbols."""
    pos = binary.rfind(MAGIC)
    if pos < 0:
        raise LinkError("no NELINK trailer")
    nseg = len(layout["segments"])
    words = struct.unpack_from("<%dH" % (2 * nseg + len(layout["symbols"])),
                               binary, pos + len(MAGIC))
    segs = {}
    at = 0
    for i, spec in enumerate(layout["segments"]):
        data_len, total = words[2 * i], words[2 * i + 1]
        if total < data_len:
            raise LinkError("segment %d: bad lengths" % spec["index"])
        chunk = binary[at:at + total]
        segs[spec["index"]] = (chunk[:data_len], chunk[data_len:])
        at += total
    if at != pos:
        raise LinkError("sections do not add up (%d != %d)" % (at, pos))
    symbols = dict(zip(layout["symbols"], words[2 * nseg:]))
    return segs, symbols


def _resource_data(res, srcdir):
    with open(os.path.join(srcdir, res["file"]), "rb") as f:
        data = f.read()
    return data


def link(binary, layout, srcdir):
    segs, symbols = split_sections(binary, layout)
    shift = layout["align"]
    unit = 1 << shift
    rshift = layout["resource_align"]

    # --- file items -----------------------------------------------------------
    items = {}
    for spec in layout["segments"]:
        data, rel = segs[spec["index"]]
        if rel and not spec["flags"] & 0x0100:
            raise LinkError("segment %d has relocations but no RELOCINFO flag"
                            % spec["index"])
        items["seg%d" % spec["index"]] = data + rel
    res_bytes = {}
    for t in layout["resources"]:
        for r in t["entries"]:
            key = "res:%s:%s" % (t["type"], r["id"])
            data = _resource_data(r, srcdir)
            pad = (-len(data)) % (1 << rshift)
            res_bytes[key] = data + b"\0" * pad
            items[key] = res_bytes[key]

    # place the items in file order, keeping each original gap and shifting
    # everything after an item that grew
    order = layout["order"]
    offsets = {}
    delta = 0
    end_prev = layout["first_item"]
    for entry in order:
        key, orig = entry["item"], entry["offset"]
        off = orig + delta
        if off < end_prev:
            off = (end_prev + unit - 1) & ~(unit - 1)
            delta = off - orig
        if off % unit:
            raise LinkError("%s: offset %#x not aligned" % (key, off))
        offsets[key] = off
        end_prev = off + len(items[key])

    # --- tables ---------------------------------------------------------------
    seg_table = b""
    for spec in layout["segments"]:
        data, _rel = segs[spec["index"]]
        length = len(data)
        if length > 0x10000:
            raise LinkError("segment %d too large" % spec["index"])
        minalloc = spec.get("minalloc", "length")
        if minalloc == "length":
            minalloc = length
        seg_table += struct.pack("<HHHH", offsets["seg%d" % spec["index"]]
                                 >> shift, length & 0xFFFF, spec["flags"],
                                 minalloc & 0xFFFF)

    res_table = struct.pack("<H", rshift)
    for t in layout["resources"]:
        res_table += struct.pack("<HHI", 0x8000 | t["type"], len(t["entries"]),
                                 0)
        for r in t["entries"]:
            key = "res:%s:%s" % (t["type"], r["id"])
            res_table += struct.pack("<HHHHI", offsets[key] >> rshift,
                                     len(res_bytes[key]) >> rshift,
                                     r["flags"], 0x8000 | r["id"], 0)
    res_table += b"\0\0"

    resident = b""
    for name, ordinal in layout["resident_names"]:
        resident += _pstr(name) + struct.pack("<H", ordinal)
    resident += b"\0"

    impnames = b"\0"
    modref = b""
    for mod in layout["modules"]:
        modref += struct.pack("<H", len(impnames))
        impnames += _pstr(mod)
    for extra in layout.get("imported_names", []):
        impnames += _pstr(extra)

    entry = b""
    for bundle in layout["entry_bundles"]:
        ents = bundle["entries"]
        if bundle["segment"] == 0:
            entry += bytes([bundle["count"], 0])
            continue
        entry += bytes([len(ents), 0xFF if bundle["movable"]
                        else bundle["segment"]])
        for e in ents:
            off = symbols[e["symbol"]] if "symbol" in e else e["offset"]
            if bundle["movable"]:
                entry += struct.pack("<BHBH", e["flags"], 0x3FCD, e["segment"],
                                     off)
            else:
                entry += struct.pack("<BH", e["flags"], off)
    entry += b"\0"

    nonres = b""
    for name, ordinal in layout["nonresident_names"]:
        nonres += _pstr(name) + struct.pack("<H", ordinal)
    nonres += b"\0"

    hdr_len = 0x40
    seg_off = hdr_len
    res_off = seg_off + len(seg_table)
    resname_off = res_off + len(res_table)
    modref_off = resname_off + len(resident)
    impname_off = modref_off + len(modref)
    entry_off = impname_off + len(impnames)
    tables_end = entry_off + len(entry)

    with open(os.path.join(srcdir, layout["stub"]), "rb") as f:
        stub = f.read()
    ne_at = len(stub)
    nonres_abs = ne_at + tables_end
    if nonres_abs + len(nonres) > layout["first_item"]:
        raise LinkError("header tables overlap the first segment")

    h = layout["header"]
    cs_seg, cs_sym = h["entry"]
    csip = (cs_seg << 16) | symbols[cs_sym]
    gang = layout.get("gangload")
    gang_off = gang_len = 0
    if gang:
        gang_off = gang["start"] >> shift
        gang_len = (offsets[gang["until"]] - gang["start"]) >> shift
    movable = sum(1 for b in layout["entry_bundles"] if b.get("movable")
                  for _ in b["entries"])
    header = struct.pack(
        "<2sBBHHIHHHHIIHHHHHHHHIHHHBBHHHH", b"NE", h["linker_ver"],
        h["linker_rev"], entry_off, len(entry), 0, h["flags"], h["autodata"],
        h["heap"], h["stack"], csip, h["sssp"], len(layout["segments"]),
        len(layout["modules"]), len(nonres), seg_off, res_off, resname_off,
        modref_off, impname_off, nonres_abs, movable, shift, 0,
        h["target_os"], h["other_flags"], gang_off, gang_len, h["swap_min"],
        h["expected_ver"])
    out = bytearray(stub + header + seg_table + res_table + resident + modref +
                    impnames + entry + nonres)
    for entry_ in order:
        key = entry_["item"]
        off = offsets[key]
        if len(out) < off:
            out += b"\0" * (off - len(out))
        elif len(out) > off:
            raise LinkError("%s overlaps the previous item" % key)
        out += items[key]
    return bytes(out)
