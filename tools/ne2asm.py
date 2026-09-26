#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Generate reassemblable NASM source from a 16-bit Windows NE module.

usage: ne2asm.py driver/ESFM.DRV -n src/esfm/names.txt -o src/esfm

Written for ESFM.DRV.  The source assembles with nasm -f bin and links
back into the same file, byte for byte, with tools/nelink.py:

  <name>.asm      top file: one section per segment, then the link trailer
  segN.asm        segment N: code and data, then its relocation table
  link.inc        segment lengths and exported offsets for the linker
  ne16.inc        macros
  layout.json     MZ stub, header fields, tables, resources, file order

Code is found by recursive descent from the entry table, the start
address, far calls between segments, jump tables and the names file.
Everything else is data.  Relocation sites keep their chain links as
labels (Rs_OOOO: dw next site) and far calls into other segments name
their target, so code can be inserted and the module relinked.  The
source is then assembled and compared with the original, and an
instruction that comes out different is tried with another encoding,
else emitted as bytes, until the rebuild matches.
"""

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from retools.ne import NEFile, SEG_DATA  # noqa: E402
import nelink  # noqa: E402

COND = set("""jo jno jb jc jnae jnb jae jnc jz je jnz jne jbe jna jnbe ja js
jns jp jpe jnp jpo jl jnge jnl jge jle jng jnle jg""".split())
LOOPS = {"loop", "loope", "loopz", "loopne", "loopnz", "jcxz"}
STOPS = {"ret", "retf", "iret", "hlt"}
LOAD_MACROS = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp", "mov"}
REG16 = ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"]
REG8 = ["al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"]
INDENT = " " * 8
COMMENT_COL = 56
SITE_SIZE = {0: 1, 2: 2, 3: 4, 5: 2}

# names of the imports used by the ESS drivers (ordinals of Windows 95)
IMPORT_NAMES = {
    "KERNEL": {1: "FatalExit", 3: "GetVersion", 4: "LocalInit",
               5: "LocalAlloc", 6: "LocalReAlloc", 7: "LocalFree",
               10: "LocalSize", 15: "GlobalAlloc", 16: "GlobalReAlloc",
               17: "GlobalFree", 18: "GlobalLock", 19: "GlobalUnlock",
               20: "GlobalSize", 23: "LockSegment", 24: "UnlockSegment",
               48: "GetModuleUsage", 49: "GetModuleFileName",
               60: "FindResource", 61: "LoadResource", 62: "LockResource",
               63: "FreeResource", 88: "lstrcpy", 91: "InitTask",
               102: "DOS3Call", 111: "GlobalWire", 112: "GlobalUnWire",
               131: "GetDOSEnvironment", 137: "FatalAppExit",
               178: "__WINFLAGS", 191: "GlobalPageLock",
               192: "GlobalPageUnlock", 353: "lstrcpyn"},
    "USER": {176: "LoadString", 255: "DefDriverProc", 420: "wsprintf",
             471: "lstrcmpi"},
    "MMSYSTEM": {31: "DriverCallback", 216: "midiOutMessage",
                 1210: "mmioOpen", 1211: "mmioClose", 1212: "mmioRead",
                 1223: "mmioDescend"},
}


class Insn:
    __slots__ = ("off", "size", "text", "kind", "target", "far", "table")

    def __init__(self, off, size, text, kind):
        self.off, self.size, self.text, self.kind = off, size, text, kind
        self.target = None    # near branch target in the same segment
        self.far = None       # (segment, offset) of a far call or jump
        self.table = None     # offset of a jump table


class Site:
    __slots__ = ("seg", "off", "rtype", "rflags", "target", "link", "size")

    def __init__(self, seg, off, rtype, rflags, target, link):
        self.seg, self.off, self.rtype, self.rflags = seg, off, rtype, rflags
        self.target, self.link = target, link   # link None: additive
        self.size = SITE_SIZE.get(rtype, 2)


class Names:
    """names.txt: seg:off kind name [size] ; comment

    kinds: code (an entry point), data (bytes, with a size), var (a DGROUP
    variable, absolute operands [off] are written with the name), imm (the
    immediate of the instruction at seg:off is the offset of the label
    given as name), label (a name only)."""

    def __init__(self, path):
        self.names, self.kinds, self.comments, self.sizes = {}, {}, {}, {}
        self.imms = {}
        if not path or not os.path.exists(path):
            return
        with open(path) as f:
            for line in f:
                line = line.rstrip("\n")
                comment = ""
                if ";" in line:
                    line, comment = line.split(";", 1)
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                seg, off = parts[0].split(":")
                key = (int(seg), int(off, 16))
                kind = parts[1]
                name = parts[2] if len(parts) > 2 and parts[2] != "-" else None
                if kind == "imm":
                    self.imms[key] = name
                    continue
                self.kinds.setdefault(key, set()).add(kind)
                if name:
                    self.names[key] = name
                if len(parts) > 3:
                    self.sizes[key] = int(parts[3], 0)
                if comment.strip():
                    self.comments[key] = comment.strip()


def build_sites(ne):
    """Relocation sites, following the chains of non-additive records."""
    sites = {s.index: {} for s in ne.segments}
    for s in ne.segments:
        data = ne.segment_data(s.index)
        for rtype, rflags, off, target in s.relocs:
            if rflags & 4:
                sites[s.index][off] = Site(s.index, off, rtype, rflags, target,
                                           None)
                continue
            pos, seen = off, set()
            while pos != 0xFFFF:
                if pos in seen or pos + 2 > len(data):
                    raise ValueError("bad relocation chain in segment %d" %
                                     s.index)
                seen.add(pos)
                nxt = struct.unpack_from("<H", data, pos)[0]
                sites[s.index][pos] = Site(s.index, pos, rtype, rflags,
                                           target, nxt)
                pos = nxt
    return sites


class Disassembler:
    def __init__(self, ne, names, sites, tmp):
        self.ne, self.names, self.sites, self.tmp = ne, names, sites, tmp
        self.data = {s.index: ne.segment_data(s.index) for s in ne.segments}
        self.code = {s.index for s in ne.segments if not s.flags & SEG_DATA}
        self.insns = {s.index: {} for s in ne.segments}
        self.owner = {s.index: {} for s in ne.segments}
        self.tables = {s.index: {} for s in ne.segments}    # off -> count
        self.marks = set()
        for key, kinds in names.kinds.items():
            if "data" in kinds:
                for b in range(key[1], key[1] + names.sizes.get(key, 1)):
                    self.marks.add((key[0], b))
        self.warnings = []

    def site_at(self, seg, off):
        return self.sites[seg].get(off)

    def _ndisasm(self, seg, start, length=512):
        chunk = self.data[seg][start:start + length]
        path = os.path.join(self.tmp, "chunk.bin")
        with open(path, "wb") as f:
            f.write(chunk)
        out = subprocess.run(["ndisasm", "-b", "16", "-o", str(start), path],
                             capture_output=True, text=True,
                             check=True).stdout
        res = []
        for line in out.splitlines():
            if line.startswith(" ") and line.strip().startswith("-"):
                res[-1][1] += line.strip()[1:]
                continue
            m = re.match(r"([0-9A-F]{8})  ([0-9A-F]+)\s+(.*)$", line)
            if m:
                res.append([int(m.group(1), 16), m.group(2),
                            m.group(3).strip()])
        end = start + len(chunk)
        return [(a, len(h) // 2, t) for a, h, t in res
                if a + len(h) // 2 <= end]

    def _classify(self, seg, off, size, text, recent):
        mn = text.split()[0] if text else ""
        ins = Insn(off, size, text, "normal")
        if mn in ("db", "dw", "dd") or text.startswith("(bad)"):
            ins.kind = "bad"
            return ins
        if mn in STOPS:
            ins.kind = "stop"
            return ins
        m = re.match(r"(\w+) (?:short |near )?(0x[0-9a-f]+)$", text)
        if m and (m.group(1) in COND or m.group(1) in LOOPS or
                  m.group(1) in ("jmp", "call")):
            ins.kind = {"jmp": "jmp", "call": "call"}.get(m.group(1), "jcc")
            ins.target = int(m.group(2), 16)
            return ins
        m = re.match(r"(call|jmp) (?:far )?(0x[0-9a-f]+):(0x[0-9a-f]+)$", text)
        if m:
            ins.kind = "callf" if m.group(1) == "call" else "jmpf"
            s = self.site_at(seg, off + 3)
            if s is not None and s.rtype == 2 and not s.rflags & 3:
                ins.far = (s.target[0] & 0xFF, int(m.group(3), 16))
            return ins
        m = re.match(r"jmp \[cs:(bx|si|di)\+(0x[0-9a-f]+)\]$", text)
        if m:
            ins.kind = "stop"
            ins.table = int(m.group(2), 16)
            count = None
            for prev in reversed(recent[-8:]):
                c = re.match(r"cmp (ax|bx|si|di),(?:byte \+)?(0x[0-9a-f]+)$",
                             prev.text)
                if c:
                    count = int(c.group(2), 16) + 1
                    break
            key = (seg, ins.table)
            if key in self.names.sizes:
                count = self.names.sizes[key]
            if count is None:
                self.warnings.append("jump table %d:%04x without a count" % key)
                return ins
            self.tables[seg][ins.table] = count
            return ins
        if mn == "jmp":
            ins.kind = "stop"
        return ins

    def discover(self, entries):
        work = list(entries)
        while work:
            seg, start = work.pop()
            if seg not in self.code:
                continue
            if start in self.insns[seg] or (seg, start) in self.marks:
                continue
            if start in self.owner[seg]:
                self.warnings.append("entry %d:%04x inside an instruction" %
                                     (seg, start))
                continue
            pos, recent = start, []
            while True:
                decoded = self._ndisasm(seg, pos)
                if not decoded:
                    break
                stop, restart = False, None
                for off, size, text in decoded:
                    if off in self.insns[seg]:
                        stop = True
                        break
                    ins = self._classify(seg, off, size, text, recent)
                    if ins.kind == "bad":
                        self.warnings.append("undecodable at %d:%04x" %
                                             (seg, off))
                        stop = True
                        break
                    span = range(off, off + ins.size)
                    if any(b in self.owner[seg] or (seg, b) in self.marks
                           for b in span):
                        stop = True
                        break
                    self.insns[seg][off] = ins
                    recent.append(ins)
                    for b in span:
                        self.owner[seg][b] = off
                    if ins.target is not None:
                        work.append((seg, ins.target))
                    if ins.far is not None:
                        work.append(ins.far)
                    if ins.table is not None and ins.table in self.tables[seg]:
                        tab, count = ins.table, self.tables[seg][ins.table]
                        for i in range(count):
                            self.marks.add((seg, tab + 2 * i))
                            self.marks.add((seg, tab + 2 * i + 1))
                        for i in range(count):
                            t = struct.unpack_from("<H", self.data[seg],
                                                   tab + 2 * i)[0]
                            work.append((seg, t))
                    if ins.kind in ("stop", "jmp", "jmpf"):
                        stop = True
                        break
                else:
                    restart = decoded[-1][0] + decoded[-1][1]
                if stop or restart is None or restart in self.insns[seg]:
                    break
                pos = restart


class Emitter:
    def __init__(self, ne, names, dis, sites, forced, variants):
        self.ne, self.names, self.dis, self.sites = ne, names, dis, sites
        self.forced, self.variants = forced, variants
        self.labels = {}
        self.linemap = {}
        dgroup = ne.autodata
        self.vars = {off: name for (seg, off), name in names.names.items()
                     if seg == dgroup and "var" in names.kinds[(seg, off)]}
        self._collect_labels()

    def _collect_labels(self):
        want = set()
        for seg, insns in self.dis.insns.items():
            for ins in insns.values():
                if ins.target is not None:
                    want.add((seg, ins.target))
                if ins.far is not None:
                    want.add(ins.far)
                if ins.table is not None:
                    want.add((seg, ins.table))
                    for i in range(self.dis.tables[seg][ins.table]):
                        want.add((seg, struct.unpack_from(
                            "<H", self.dis.data[seg], ins.table + 2 * i)[0]))
        for ordinal, (seg, off, _fl) in self.ne.entries.items():
            want.add((seg, off))
        want.add((self.ne.csip >> 16, self.ne.csip & 0xFFFF))
        for key in self.names.names:
            want.add(key)
        for key, name in self.names.imms.items():
            pass
        for key in want:
            seg, off = key
            if key in self.names.names:
                self.labels[key] = self.names.names[key]
            elif off in self.dis.tables.get(seg, {}):
                self.labels[key] = "JT%d_%04X" % key
            else:
                self.labels[key] = "L%d_%04X" % key
        for seg, table in self.sites.items():
            for off in table:
                self.labels.setdefault(("site", seg, off), "R%d_%04X" %
                                       (seg, off))

    def label(self, seg, off):
        return self.labels.get((seg, off))

    def site_label(self, seg, off):
        return self.labels[("site", seg, off)]

    def link_value(self, site):
        if site.link is None:
            return None
        if site.link == 0xFFFF:
            return "0xFFFF"
        return self.site_label(site.seg, site.link)

    def site_comment(self, site):
        a, b = site.target
        if site.rflags & 3 == 0:
            return "seg%d" % (a & 0xFF) if (a & 0xFF) != 0xFF else \
                "entry %d" % b
        if site.rflags & 3 in (1, 2):
            mod = self.ne.modules[a - 1]
            if site.rflags & 3 == 1:
                return "%s.%s" % (mod, IMPORT_NAMES.get(mod, {}).get(
                    b, "#%d" % b))
            return "%s.(name)" % mod
        return "osfixup"

    # -- instructions -------------------------------------------------------

    def insn_sites(self, seg, ins):
        return [self.sites[seg][o] for o in range(ins.off, ins.off + ins.size)
                if o in self.sites[seg]]

    def insn_text(self, seg, ins):
        """NASM text for an instruction, or None to emit bytes."""
        if (seg, ins.off) in self.forced:
            return None
        if self.insn_sites(seg, ins):
            return None
        text = ins.text
        img = self.dis.data[seg]
        if ins.target is not None:
            lab = self.label(seg, ins.target)
            mn = text.split()[0]
            if mn in LOOPS or mn == "call":
                return "%s %s" % (mn, lab)
            if mn == "jmp":
                return "jmp %s %s" % ("short" if ins.size == 2 else "near", lab)
            return "%s %s %s" % (mn, "short" if ins.size == 2 else "near", lab)
        if ins.table is not None:
            return re.sub(r"0x[0-9a-f]+\]", self.label(seg, ins.table) + "]",
                          text)
        imm = self.names.imms.get((seg, ins.off))
        if imm:
            m = list(re.finditer(r"(?<![\w\[+-])0x[0-9a-f]+(?![^\[]*\])", text))
            if not m:
                raise ValueError("imm hint at %d:%04x: no immediate in %r" %
                                 (seg, ins.off, text))
            text = text[:m[-1].start()] + imm + text[m[-1].end():]
        if self.vars and not re.search(r"\[(cs|ds|es|ss):", text):
            def sub(m):
                off = int(m.group(1), 16)
                return "[%s]" % self.vars[off] if off in self.vars else m.group(0)
            text = re.sub(r"\[(0x[0-9a-f]+)\]", sub, text)
        var = self.variants.get((seg, ins.off))
        if var == "load":
            mn, _, ops = text.partition(" ")
            text = "%s_ %s" % (mn, ops)
        elif var == "word":
            text = strict_word(text)
        if img[ins.off] in (0x26, 0x2E, 0x36, 0x3E) and \
                not re.search(r"\[(cs|ds|es|ss):", text):
            return None     # a segment prefix ndisasm didn't show
        return text

    def emit_insn(self, seg, ins, out):
        img = self.dis.data[seg]
        text = self.insn_text(seg, ins)
        cmt = "%04X" % ins.off
        if text is not None:
            out.append(self.fmt(INDENT + text, cmt))
            return
        sites = self.insn_sites(seg, ins)
        raw = img[ins.off:ins.off + ins.size]
        if ins.far is not None and ins.size == 5 and len(sites) == 1 and \
                sites[0].off == ins.off + 3:
            s = sites[0]
            lab = self.label(*ins.far)
            op = "callf" if ins.kind == "callf" else "jmpf"
            out.append(self.fmt(INDENT + "%s %s, %s, %s" % (
                op, lab, self.site_label(seg, s.off), self.link_value(s)),
                "%s far seg%d" % (cmt, ins.far[0])))
            return
        if 0xB8 <= raw[0] <= 0xBF and ins.size == 3 and len(sites) == 1 and \
                sites[0].off == ins.off + 1 and sites[0].rtype == 2:
            s = sites[0]
            out.append(self.fmt(INDENT + "movsel %s, %s, %s" % (
                REG16[raw[0] - 0xB8], self.site_label(seg, s.off),
                self.link_value(s)), "%s %s" % (cmt, self.site_comment(s))))
            return
        if raw[0] == 0x9A and ins.size == 5 and len(sites) == 1 and \
                sites[0].off == ins.off + 1 and sites[0].rtype == 3:
            s = sites[0]
            link = self.link_value(s)
            if link is None:
                link = "0x%04X" % struct.unpack_from("<H", raw, 1)[0]
            seg_word = struct.unpack_from("<H", raw, 3)[0]
            out.append(self.fmt(INDENT + "callp %s, %s, 0x%04X" % (
                self.site_label(seg, s.off), link, seg_word),
                "%s %s" % (cmt, self.site_comment(s))))
            return
        # generic: bytes, with the relocation sites as labelled words
        pos = ins.off
        end = ins.off + ins.size
        first = True
        note = "%s %s" % (cmt, ins.text)
        for s in sites:
            if s.off > pos:
                out.append(self.fmt(INDENT + "db " + ", ".join(
                    "0x%02X" % b for b in img[pos:s.off]), note if first
                    else ""))
                first = False
            out.append(self.site_line(s, img, note if first else
                                      self.site_comment(s)))
            first = False
            pos = s.off + s.size
        if pos < end:
            out.append(self.fmt(INDENT + "db " + ", ".join(
                "0x%02X" % b for b in img[pos:end]), note if first else ""))

    def site_line(self, s, img, comment):
        link = self.link_value(s)
        words = []
        if s.rtype == 0:
            val = "0x%02X" % img[s.off]
            return self.fmt("%s: db %s" % (self.site_label(s.seg, s.off), val),
                            comment)
        if link is None:
            link = "0x%04X" % struct.unpack_from("<H", img, s.off)[0]
        words.append(link)
        if s.size == 4:
            words.append("0x%04X" % struct.unpack_from("<H", img, s.off + 2)[0])
        return self.fmt("%s: dw %s" % (self.site_label(s.seg, s.off),
                                       ", ".join(words)), comment)

    @staticmethod
    def fmt(text, comment):
        if not comment:
            return text
        pad = max(COMMENT_COL - len(text), 1)
        return text + " " * pad + "; " + comment

    # -- segments -----------------------------------------------------------

    def render_segment(self, seg):
        spec = self.ne.segments[seg - 1]
        img = self.dis.data[seg]
        insns = self.dis.insns[seg]
        tables = self.dis.tables[seg]
        sites = self.sites[seg]
        out = []
        kind = "data" if spec.flags & SEG_DATA else "code"
        out.append("; segment %d: %s, %d bytes, flags %04Xh" %
                   (seg, kind, len(img), spec.flags))
        out.append("")
        breaks = set(insns) | set(sites) | {k[1] for k in self.labels
                                            if k[0] == seg}
        for t, n in tables.items():
            breaks |= {t + 2 * i for i in range(n)}
        for s in sites.values():
            breaks.add(s.off + s.size)
        for (sg, off), size in self.names.sizes.items():
            if sg == seg and off not in tables:
                breaks.add(off + size)
        for o in list(insns):
            breaks.add(o + insns[o].size)
        pos = 0
        while pos < len(img):
            lab = self.label(seg, pos)
            if lab:
                c = self.names.comments.get((seg, pos))
                if c:
                    out.append("")
                    out.append("; " + c)
                elif pos in insns and insns[pos].off == pos and out[-1] != "":
                    out.append("")
                out.append(lab + ":")
            if pos in insns:
                self.linemap_mark(seg, pos, len(out))
                self.emit_insn(seg, insns[pos], out)
                pos += insns[pos].size
                continue
            if pos in tables:
                for i in range(tables[pos]):
                    t = struct.unpack_from("<H", img, pos + 2 * i)[0]
                    out.append(self.fmt(INDENT + "dw " + self.label(seg, t),
                                        "%04X" % (pos + 2 * i)))
                pos += 2 * tables[pos]
                continue
            if pos in sites:
                s = sites[pos]
                out.append(self.site_line(s, img, "%04X %s" %
                                          (pos, self.site_comment(s))))
                pos += s.size
                continue
            end = pos + 1
            while end < len(img) and end not in breaks:
                end += 1
            for chunk_pos, chunk in split_data(img[pos:end], pos):
                out.append(self.data_line(chunk, chunk_pos))
            pos = end
        out.append("")
        out.append("seg%d_data_end:" % seg)
        if spec.relocs:
            out.append("")
            out.append("; relocation table")
            out.append(INDENT + "dw (seg%d_rel_end - seg%d_rel_start) / 8" %
                       (seg, seg))
            out.append("seg%d_rel_start:" % seg)
            for rtype, rflags, off, target in spec.relocs:
                out.append(self.reloc_line(seg, rtype, rflags, off, target))
            out.append("seg%d_rel_end:" % seg)
        out.append("seg%d_end:" % seg)
        out.append("")
        return "\n".join(out)

    def linemap_mark(self, seg, off, line):
        self.linemap[("seg%d.asm" % seg, line + 1)] = (seg, off)

    def reloc_line(self, seg, rtype, rflags, off, target):
        a, b = target
        site = self.site_label(seg, off)
        if rflags & 3 == 0 and (a & 0xFF) != 0xFF:
            tseg = a & 0xFF
            tgt = self.label(tseg, b) if b and rtype != 2 else None
            val = tgt or "0x%04X" % b
            return self.fmt(INDENT + "reloc %d, %d, %s, 0x%04X, %s" % (
                rtype, rflags, site, a, val), "seg%d" % tseg)
        s = Site(seg, off, rtype, rflags, target, None)
        return self.fmt(INDENT + "reloc %d, %d, %s, 0x%04X, 0x%04X" % (
            rtype, rflags, site, a, b), self.site_comment(s))

    @staticmethod
    def data_line(chunk, pos):
        if is_text(chunk):
            text = INDENT + "db '%s'" % chunk.decode("latin-1")
        else:
            text = INDENT + "db " + ", ".join("0x%02X" % c for c in chunk)
        return Emitter.fmt(text, "%04X" % pos)


def is_text(chunk):
    return len(chunk) >= 4 and all(32 <= c < 127 and c != 39 for c in chunk)


def split_data(data, pos):
    """Split a data run into lines: printable text up to 48 characters,
    other bytes 16 to a line."""
    out = []
    i = 0
    while i < len(data):
        j = i
        while j < len(data) and 32 <= data[j] < 127 and data[j] != 39 and \
                j - i < 48:
            j += 1
        if j - i >= 4:
            out.append((pos + i, data[i:j]))
            i = j
            continue
        j = i + 1
        while j < len(data) and j - i < 16:
            k = j
            while k < len(data) and 32 <= data[k] < 127 and data[k] != 39:
                k += 1
            if k - j >= 4:
                break
            j += 1
        out.append((pos + i, data[i:j]))
        i = j
    return out


NE16_INC = """\
; Macros for the reassemblable NE source (tools/ne2asm.py)

%ifndef NE16_INC
%define NE16_INC

; far call/jump into another segment: target offset, label of the selector
; word (a relocation site), chain link (next site or 0xFFFF)
%macro callf 3
        db      0x9A
        dw      %1
%2:     dw      %3
%endmacro
%macro jmpf 3
        db      0xEA
        dw      %1
%2:     dw      %3
%endmacro

; mov r16, selector: register, label of the selector word, chain link
%macro movsel 3
        db      0xB8 + REG_%1
%2:     dw      %3
%endmacro

; far call through a relocated far pointer (an import): label of the
; pointer, chain link, selector word
%macro callp 3
        db      0x9A
%1:     dw      %2, %3
%endmacro

; relocation record: type, flags, first site, target words
%macro reloc 5
        db      %1, %2
        dw      %3, %4, %5
%endmacro

; reg, reg forms with the "load" opcode (reg field = destination)
%define REG_ax 0
%define REG_cx 1
%define REG_dx 2
%define REG_bx 3
%define REG_sp 4
%define REG_bp 5
%define REG_si 6
%define REG_di 7
%define REG_al 0
%define REG_cl 1
%define REG_dl 2
%define REG_bl 3
%define REG_ah 4
%define REG_ch 5
%define REG_dh 6
%define REG_bh 7
%define WID_ax 16
%define WID_cx 16
%define WID_dx 16
%define WID_bx 16
%define WID_sp 16
%define WID_bp 16
%define WID_si 16
%define WID_di 16
%define WID_al 8
%define WID_cl 8
%define WID_dl 8
%define WID_bl 8
%define WID_ah 8
%define WID_ch 8
%define WID_dh 8
%define WID_bh 8
%macro _rr_load 3                       ; 16-bit opcode, dst, src
%if WID_%2 == 16
        db      %1, 0xC0 | (REG_%2 << 3) | REG_%3
%else
        db      (%1) - 1, 0xC0 | (REG_%2 << 3) | REG_%3
%endif
%endmacro
%macro add_ 2
        _rr_load 0x03, %1, %2
%endmacro
%macro or_ 2
        _rr_load 0x0B, %1, %2
%endmacro
%macro adc_ 2
        _rr_load 0x13, %1, %2
%endmacro
%macro sbb_ 2
        _rr_load 0x1B, %1, %2
%endmacro
%macro and_ 2
        _rr_load 0x23, %1, %2
%endmacro
%macro sub_ 2
        _rr_load 0x2B, %1, %2
%endmacro
%macro xor_ 2
        _rr_load 0x33, %1, %2
%endmacro
%macro cmp_ 2
        _rr_load 0x3B, %1, %2
%endmacro
%macro mov_ 2
        _rr_load 0x8B, %1, %2
%endmacro

%endif
"""


def render_main(ne, name, segfiles):
    out = ["; %s -- reassemblable source (tools/ne2asm.py)" % name, ";",
           "; Build: python3 tools/build_esfm.py [--stock]", "",
           "%ifndef ESFM_FIX", "%define ESFM_FIX 1", "%endif", "",
           "        bits 16", '%include "ne16.inc"', ""]
    prev = None
    for s, fname in zip(ne.segments, segfiles):
        where = "start=0" if prev is None else "follows=seg%d" % prev
        out.append("        section seg%d progbits %s vstart=0 align=1" %
                   (s.index, where))
        out.append('%%include "%s"' % fname)
        out.append("")
        prev = s.index
    out.append("        section link progbits follows=seg%d vstart=0 align=1"
               % prev)
    out.append('%include "link.inc"')
    out.append("")
    return "\n".join(out)


def render_link(ne, symbols):
    out = ["; lengths and exported offsets for tools/nelink.py", "",
           "        db 'NELINK', 1, 0"]
    for s in ne.segments:
        out.append("        dw seg%d_data_end, seg%d_end" % (s.index, s.index))
    for sym in symbols:
        out.append("        dw %s" % sym)
    out.append("")
    return "\n".join(out)


def make_layout(ne, em, stub_name, res_files):
    d = ne.data
    entries_by_seg = []
    # rebuild the entry bundles from the raw table (keeps unused ordinals)
    pos = ne.ne + ne.entry_off
    ordinal = 1
    symbols = []
    while True:
        count, segi = d[pos], d[pos + 1]
        pos += 2
        if count == 0:
            break
        bundle = {"count": count, "segment": segi, "movable": segi == 0xFF,
                  "entries": []}
        for _ in range(count):
            if segi == 0:
                pass
            elif segi == 0xFF:
                flags, _i, sno, off = struct.unpack_from("<BHBH", d, pos)
                pos += 6
                sym = em.label(sno, off)
                bundle["entries"].append({"ordinal": ordinal, "flags": flags,
                                          "segment": sno, "symbol": sym})
                symbols.append(sym)
            else:
                flags, off = struct.unpack_from("<BH", d, pos)
                pos += 3
                sym = em.label(segi, off)
                bundle["entries"].append({"ordinal": ordinal, "flags": flags,
                                          "symbol": sym})
                symbols.append(sym)
            ordinal += 1
        entries_by_seg.append(bundle)
    cs_seg, ip = ne.csip >> 16, ne.csip & 0xFFFF
    start_sym = em.label(cs_seg, ip)
    if start_sym not in symbols:
        symbols.append(start_sym)
    # file order
    items = []
    for s in ne.segments:
        if s.offset:
            items.append((s.offset, "seg%d" % s.index))
    for r in ne.resources:
        items.append((r.offset, "res:%s:%s" % (r.type, r.id)))
    items.sort()
    res_types = []
    for r in ne.resources:
        t = next((x for x in res_types if x["type"] == r.type), None)
        if t is None:
            t = {"type": r.type, "entries": []}
            res_types.append(t)
        t["entries"].append({"id": r.id, "flags": r.flags,
                             "file": res_files[(r.type, r.id)]})
    layout = {
        "module": ne.module_name,
        "stub": stub_name,
        "align": ne.align,
        "resource_align": ne.res_align,
        "first_item": items[0][0],
        "header": {"linker_ver": ne.linker_ver, "linker_rev": ne.linker_rev,
                   "flags": ne.flags, "autodata": ne.autodata,
                   "heap": ne.heap, "stack": ne.stack, "sssp": ne.sssp,
                   "entry": [cs_seg, start_sym], "target_os": ne.target_os,
                   "other_flags": ne.other_flags, "swap_min": ne.swap_min,
                   "expected_ver": ne.expected_ver},
        "segments": [{"index": s.index, "flags": s.flags,
                      "minalloc": "length" if s.minalloc == s.length & 0xFFFF
                      else s.minalloc} for s in ne.segments],
        "resources": res_types,
        "resident_names": ne.resident,
        "nonresident_names": ne.nonresident,
        "modules": ne.modules,
        "entry_bundles": entries_by_seg,
        "symbols": symbols,
        "order": [{"item": k, "offset": o} for o, k in items],
    }
    if ne.gang_len:
        start = ne.gang_off << ne.align
        end = start + (ne.gang_len << ne.align)
        until = next((k for o, k in items if o == end), None)
        if until is None:
            raise ValueError("gang-load area does not end at an item")
        layout["gangload"] = {"start": start, "until": until}
    return layout


def assemble(srcdir, main, defines, out, listing=None):
    cmd = ["nasm", "-f", "bin", "-I", srcdir + os.sep, "-o", out]
    if listing:
        cmd += ["-l", listing]
    for k, v in defines.items():
        cmd.append("-D%s=%s" % (k, v))
    cmd.append(os.path.join(srcdir, main))
    return subprocess.run(cmd, capture_output=True, text=True)


def strict_word(text):
    """The instruction with its last immediate forced to 16 bits."""
    mn, _, ops = text.partition(" ")
    m = list(re.finditer(r"(?<![\w\[+-])(0x[0-9a-f]+)(?![^\[]*\])", ops))
    if not m or "strict" in ops or mn in ("int", "ret", "retf", "enter",
                                          "in", "out"):
        return text
    ops = ops[:m[-1].start()] + "strict word " + m[-1].group(1) + \
        ops[m[-1].end():]
    return "%s %s" % (mn, ops)


def _batch(tmp, texts):
    """Assemble instruction texts one after another and return their bytes."""
    lines = ["        bits 16", '%include "ne16.inc"']
    for i, text in enumerate(texts):
        lines.append("I%d: %s" % (i, text))
    lines.append("I%d:" % len(texts))
    for i in range(0, len(texts) + 1, 16):
        lines.append("        dw " + ", ".join(
            "I%d" % k for k in range(i, min(i + 16, len(texts) + 1))))
    src = os.path.join(tmp, "enc.asm")
    with open(src, "w") as f:
        f.write("\n".join(lines) + "\n")
    res = subprocess.run(["nasm", "-f", "bin", "-I", tmp + os.sep, "-o",
                          os.path.join(tmp, "enc.bin"), src],
                         capture_output=True, text=True)
    if res.returncode != 0:
        errs = {int(m.group(1)) - 3 for m in
                re.finditer(r"enc\.asm:(\d+): error", res.stderr)}
        return None, errs
    with open(os.path.join(tmp, "enc.bin"), "rb") as f:
        out = f.read()
    n = len(texts) + 1
    pos = struct.unpack_from("<%dH" % n, out, len(out) - 2 * n)
    return [out[pos[i]:pos[i + 1]] for i in range(len(texts))], set()


def check_encodings(tmp, dis, em, forced, variants, verbose):
    """Check the encoding of every plain instruction in one batch.  Where
    NASM picks another encoding, try the load form of reg,reg instructions
    and a 16-bit immediate, else emit the instruction as bytes."""
    cands = []
    for seg, insns in dis.insns.items():
        img = dis.data[seg]
        for off, ins in sorted(insns.items()):
            if ins.target is not None or ins.far is not None or \
                    ins.table is not None or em.insn_sites(seg, ins):
                continue
            cands.append(((seg, off), ins.text, bytes(img[off:off + ins.size])))

    def run(items, make):
        texts = [make(t) for _k, t, _w in items]
        got, errs = _batch(tmp, texts)
        while got is None:
            if not errs:
                raise RuntimeError("encoding check: nasm failed")
            items = [c for i, c in enumerate(items) if i not in errs]
            texts = [t for i, t in enumerate(texts) if i not in errs]
            got, errs = _batch(tmp, texts)
        ok = {c[0] for c, b in zip(items, got) if b == c[2]}
        return ok

    def load(text):
        mn, _, ops = text.partition(" ")
        return "%s_ %s" % (mn, ops)

    ok = run(cands, lambda t: t)
    bad = [c for c in cands if c[0] not in ok]
    loadable, wordable = [], []
    for c in bad:
        mn, _, ops = c[1].partition(" ")
        parts = ops.split(",")
        if mn in LOAD_MACROS and len(parts) == 2 and \
                parts[0] in REG16 + REG8 and parts[1] in REG16 + REG8:
            loadable.append(c)
        elif strict_word(c[1]) != c[1]:
            wordable.append(c)
        else:
            forced.add(c[0])
    ok_load = run(loadable, load) if loadable else set()
    ok_word = run(wordable, strict_word) if wordable else set()
    for c in loadable:
        if c[0] in ok_load:
            variants[c[0]] = "load"
        else:
            forced.add(c[0])
    for c in wordable:
        if c[0] in ok_word:
            variants[c[0]] = "word"
        else:
            forced.add(c[0])
    if verbose:
        print("  encodings: %d checked, %d as disassembled, %d with the load "
              "form, %d with a 16-bit immediate, %d as bytes" % (
                  len(cands), len(ok), len(ok_load), len(ok_word),
                  len(bad) - len(ok_load) - len(ok_word)))


def generate(path, names_path, outdir, verbose=True, res_map=None):
    with open(path, "rb") as f:
        raw = f.read()
    ne = NEFile(raw)
    names = Names(names_path)
    sites = build_sites(ne)
    tmp = tempfile.mkdtemp(prefix="ne2asm")
    try:
        dis = Disassembler(ne, names, sites, tmp)
        entries = [(seg, off) for seg, off, _f in ne.entries.values()]
        entries.append((ne.csip >> 16, ne.csip & 0xFFFF))
        entries += [k for k, kinds in names.kinds.items() if "code" in kinds]
        dis.discover(entries)
        if verbose:
            for s in ne.segments:
                code = sum(i.size for i in dis.insns[s.index].values())
                print("  seg%d %5d bytes, %5d as code (%d instructions)" %
                      (s.index, s.length, code, len(dis.insns[s.index])))
            for w in dis.warnings[:20]:
                print("  warning:", w)
        name = os.path.splitext(os.path.basename(path))[0].lower()
        stub_name = "stub.bin"
        res_files = {}
        for r in ne.resources:
            key = (r.type, r.id)
            if res_map and key in res_map:
                res_files[key] = res_map[key]
            else:
                res_files[key] = "res_%s_%s.bin" % (r.type, r.id)
        forced, variants = set(), {}
        segfiles = ["seg%d.asm" % s.index for s in ne.segments]
        with open(os.path.join(tmp, "ne16.inc"), "w") as f:
            f.write(NE16_INC)
        em = Emitter(ne, names, dis, sites, forced, variants)
        check_encodings(tmp, dis, em, forced, variants, verbose)
        for attempt in range(20):
            em = Emitter(ne, names, dis, sites, forced, variants)
            for s, fname in zip(ne.segments, segfiles):
                with open(os.path.join(tmp, fname), "w") as f:
                    f.write(em.render_segment(s.index))
            layout = make_layout(ne, em, stub_name, res_files)
            with open(os.path.join(tmp, "link.inc"), "w") as f:
                f.write(render_link(ne, layout["symbols"]))
            main = name + ".asm"
            with open(os.path.join(tmp, main), "w") as f:
                f.write(render_main(ne, os.path.basename(path), segfiles))
            with open(os.path.join(tmp, stub_name), "wb") as f:
                f.write(raw[:ne.ne])
            for r in ne.resources:
                fn = "res_%s_%s.bin" % (r.type, r.id)
                with open(os.path.join(tmp, fn), "wb") as f:
                    f.write(raw[r.offset:r.offset + r.length])
                if res_map and (r.type, r.id) in res_map:
                    with open(os.path.join(outdir, res_map[(r.type, r.id)]),
                              "rb") as f:
                        if f.read() != raw[r.offset:r.offset + r.length]:
                            raise RuntimeError("%s differs from resource "
                                               "%s:%s" % (res_map[(r.type,
                                                  r.id)], r.type, r.id))
            res = assemble(tmp, main, {"ESFM_FIX": 0},
                           os.path.join(tmp, "out.bin"))
            if res.returncode != 0:
                bad = set()
                for m in re.finditer(r"([\w.]+):(\d+): error", res.stderr):
                    key = em.linemap.get((m.group(1), int(m.group(2))))
                    if key is None:
                        raise RuntimeError(res.stderr[:3000])
                    bad.add(key)
                if verbose:
                    print("  pass %d: %d assembler errors" % (attempt, len(bad)))
                if bad <= forced:
                    raise RuntimeError(res.stderr[:3000])
                forced |= bad
                continue
            with open(os.path.join(tmp, "out.bin"), "rb") as f:
                binary = f.read()
            segs, _syms = nelink.split_sections(binary, layout)
            bad = set()
            for s in ne.segments:
                data, rel = segs[s.index]
                want = ne.segment_data(s.index)
                rel_want = b""
                if s.relocs:
                    end = s.offset + s.length
                    rel_want = raw[end:end + 2 + 8 * len(s.relocs)]
                if data == want:
                    if rel != rel_want:
                        raise RuntimeError("relocation table of seg%d differs"
                                           % s.index)
                    continue
                n = max(len(data), len(want))
                first = next(i for i in range(n) if i >= len(data) or
                             i >= len(want) or data[i] != want[i])
                # later bytes are shifted by the first difference
                owner = dis.owner[s.index].get(first)
                if owner is None:
                    raise RuntimeError("data differs at seg%d:%04x" %
                                       (s.index, first))
                bad.add((s.index, owner))
            if not bad:
                check = json.loads(json.dumps(layout))
                for t in check["resources"]:
                    for r in t["entries"]:
                        if res_map and (t["type"], r["id"]) in res_map:
                            r["file"] = "res_%s_%s.bin" % (t["type"], r["id"])
                data = nelink.link(binary, check, tmp)
                if data != raw:
                    n = next((i for i in range(min(len(data), len(raw)))
                              if data[i] != raw[i]), min(len(data), len(raw)))
                    raise RuntimeError("segments match but the file differs "
                                       "at %#x" % n)
                if verbose:
                    print("  pass %d: rebuild is byte-identical (%d "
                          "instructions as bytes, %d re-encoded)" %
                          (attempt, len(forced), len(variants)))
                break
            if verbose:
                print("  pass %d: %d instructions differ: %s" % (
                    attempt, len(bad), ", ".join("%d:%04x" % k
                                                 for k in sorted(bad))))
            forced |= bad
        else:
            raise RuntimeError("no byte-identical rebuild after 20 passes")

        os.makedirs(outdir, exist_ok=True)
        mapped = {"res_%s_%s.bin" % k for k in (res_map or {})}
        for f in os.listdir(tmp):
            if f in ("chunk.bin", "out.bin", "enc.asm", "enc.bin") or \
                    f in mapped:
                continue
            shutil.copy(os.path.join(tmp, f), os.path.join(outdir, f))
        with open(os.path.join(outdir, "layout.json"), "w") as f:
            json.dump(layout, f, indent=2)
            f.write("\n")
        return dis, em
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("module")
    ap.add_argument("-n", "--names")
    ap.add_argument("-o", "--outdir", required=True)
    ap.add_argument("--resource", action="append", default=[],
                    metavar="TYPE:ID=FILE",
                    help="use FILE (relative to the output directory) for a "
                    "resource instead of extracting it")
    args = ap.parse_args()
    res_map = {}
    for spec in args.resource:
        key, fn = spec.split("=", 1)
        t, i = key.split(":")
        res_map[(int(t), int(i))] = fn
    generate(args.module, args.names, args.outdir, res_map=res_map or None)


if __name__ == "__main__":
    main()
