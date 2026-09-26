#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Generate reassemblable NASM source from a Windows 9x VxD (LE file).

usage: vxd2asm.py driver/ES1869.VXD -n src/vxd/names.txt -o src/vxd

The source assembles with nasm -f elf32 and links back into the same VxD,
byte for byte, with tools/lelink.py.  Code is found by recursive descent
from the DDB, the API and control dispatch tables and the names file.
Everything else is data.  Fixups become symbolic references, relative
branches labels and INT 20h dynamic links VxDCall/VxDJmp macros, so the
source can be edited and rebuilt.  The source is then assembled, linked
and compared with the original, and any instruction that comes out
different is emitted as bytes (fixups and branches stay symbolic) until
the rebuild matches.
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

from retools import elf32, vxdsvc  # noqa: E402
from retools.le import OBJ_EXEC, HDR_FIELDS, read_le  # noqa: E402
import lelink  # noqa: E402

COND_BRANCHES = set("""
jo jno jb jc jnae jnb jae jnc jz je jnz jne jbe jna jnbe ja js jns jp jpe
jnp jpo jl jnge jnl jge jle jng jnle jg jcxz jecxz loop loope loopz loopne
loopnz""".split())
STOPS = {"ret", "retn", "retf", "iret", "iretd", "iretw", "hlt", "int3"}
SHORT_ONLY = {"jcxz", "jecxz", "loop", "loope", "loopz", "loopne", "loopnz"}

DDB_PROCS = ((0x18, "Control_Proc"), (0x1C, "V86_API_Proc"),
             (0x20, "PM_API_Proc"))

INDENT = " " * 8

CLIENT_FIELDS = {
    0x00: "Client_EDI", 0x04: "Client_ESI", 0x08: "Client_EBP",
    0x10: "Client_EBX", 0x14: "Client_EDX", 0x18: "Client_ECX",
    0x1C: "Client_EAX", 0x20: "Client_Error", 0x24: "Client_EIP",
    0x28: "Client_CS", 0x2C: "Client_EFlags", 0x30: "Client_ESP",
    0x34: "Client_SS", 0x38: "Client_ES", 0x3C: "Client_DS",
    0x40: "Client_FS", 0x44: "Client_GS",
}

# VxD_Desc_Block fields: (offset, size, name)
DDB_FIELDS = [
    (0x00, 4, "DDB_Next"), (0x04, 2, "DDB_SDK_Version"),
    (0x06, 2, "DDB_Req_Device_Number"), (0x08, 1, "DDB_Dev_Major_Version"),
    (0x09, 1, "DDB_Dev_Minor_Version"), (0x0A, 2, "DDB_Flags"),
    (0x0C, 8, "DDB_Name"), (0x14, 4, "DDB_Init_Order"),
    (0x18, 4, "DDB_Control_Proc"), (0x1C, 4, "DDB_V86_API_Proc"),
    (0x20, 4, "DDB_PM_API_Proc"), (0x24, 4, "DDB_V86_API_CSIP"),
    (0x28, 4, "DDB_PM_API_CSIP"), (0x2C, 4, "DDB_Reference_Data"),
    (0x30, 4, "DDB_Service_Table_Ptr"), (0x34, 4, "DDB_Service_Table_Size"),
    (0x38, 4, "DDB_Win32_Service_Table"), (0x3C, 4, "DDB_Prev"),
    (0x40, 4, "DDB_Size"), (0x44, 4, "DDB_Reserved1"),
    (0x48, 4, "DDB_Reserved2"), (0x4C, 4, "DDB_Reserved3"),
]
COMMENT_COL = 56


class Insn:
    __slots__ = ("off", "size", "text", "kind", "target", "xtarget", "svc")

    def __init__(self, off, size, text, kind, target=None):
        self.off, self.size, self.text, self.kind = off, size, text, kind
        self.target = target      # local branch target
        self.xtarget = None       # (obj, off) cross-object branch target
        self.svc = None           # VxD service dword for int 20h


class Names:
    """Parse the names/hints file."""

    RANGE_KINDS = ("client", "ctlmsg")

    def __init__(self, path):
        self.names = {}       # (obj, off) -> name
        self.kinds = {}       # (obj, off) -> set of kinds
        self.comments = {}    # (obj, off) -> comment
        self.sizes = {}       # (obj, off) -> size of a data item
        self.ranges = []      # (kind, obj, start, end)
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
                obj, off = parts[0].split(":")
                key = (int(obj), int(off, 16))
                kind = parts[1]
                size = int(parts[3], 0) if len(parts) > 3 else None
                if kind in self.RANGE_KINDS:
                    self.ranges.append((kind, key[0], key[1], key[1] + size))
                    continue
                self.kinds.setdefault(key, set()).add(kind)
                if len(parts) > 2 and parts[2] != "-":
                    self.names[key] = parts[2]
                if size is not None:
                    self.sizes[key] = size
                if comment.strip():
                    self.comments[key] = comment.strip()

    def has_kind(self, key, kind):
        return kind in self.kinds.get(key, ())

    def in_range(self, kind, obj, off):
        return any(k == kind and o == obj and a <= off < b
                   for k, o, a, b in self.ranges)


class Disassembler:
    """Recursive-descent disassembly of one VxD using ndisasm."""

    def __init__(self, le, names, tmpdir):
        self.le = le
        self.names = names
        self.tmp = tmpdir
        self.fix = le.fixup_map()
        self.insns = {o.index: {} for o in le.objects}
        self.owner = {o.index: {} for o in le.objects}   # byte -> insn start
        self.data_marks = {}
        for (obj, off), kinds in names.kinds.items():
            if kinds & {"data", "ddb"}:
                size = names.sizes.get((obj, off), 1)
                for b in range(off, off + size):
                    self.data_marks[(obj, b)] = True
        self.warnings = []

    def is_exec(self, obj):
        return bool(self.le.objects[obj - 1].flags & OBJ_EXEC)

    # -- ndisasm ------------------------------------------------------------

    def _ndisasm(self, obj, start, length=2048):
        img = self.le.objects[obj - 1].data
        chunk = bytes(img[start:start + length])
        path = os.path.join(self.tmp, "chunk.bin")
        with open(path, "wb") as f:
            f.write(chunk)
        out = subprocess.run(["ndisasm", "-b", "32", "-o", str(start), path],
                             capture_output=True, text=True, check=True).stdout
        res = []
        for line in out.splitlines():
            if line.startswith(" ") and line.strip().startswith("-"):
                res[-1][1] += line.strip()[1:]
                continue
            m = re.match(r"([0-9A-F]{8})  ([0-9A-F]+)\s+(.*)$", line)
            if m:
                res.append([int(m.group(1), 16), m.group(2), m.group(3).strip()])
        end = start + len(chunk)
        return [(a, len(h) // 2, t) for a, h, t in res if a + len(h) // 2 <= end]

    # -- classification -----------------------------------------------------

    def _classify(self, obj, off, size, text):
        img = self.le.objects[obj - 1].data
        mn = text.split()[0] if text else ""
        ins = Insn(off, size, text, "normal")
        if mn in ("db", "dw", "dd") or text.startswith("(bad)"):
            ins.kind = "bad"
            return ins
        if mn in STOPS:
            ins.kind = "stop"
            return ins
        if text == "int 0x20" and img[off:off + 2] == b"\xcd\x20":
            if off + 6 > len(img):
                ins.kind = "bad"
                return ins
            (svc,) = struct.unpack_from("<I", img, off + 2)
            ins.size, ins.svc = 6, svc
            ins.kind = "vxdjmp" if svc & 0x8000 else "vxd"
            return ins
        m = re.match(r"(\w+) (?:short |near |dword )?(0x[0-9a-f]+)$", text)
        if m and (m.group(1) in COND_BRANCHES or m.group(1) in ("jmp", "call")):
            kind = {"jmp": "jmp", "call": "call"}.get(m.group(1), "jcc")
            ins.kind = kind
            disp_at = off + size - 4
            f = self.fix.get((obj, disp_at))
            if f is not None and f.type == 8 and size >= 5:
                ins.xtarget = (f.tobj, f.toff)
            else:
                ins.target = int(m.group(2), 16)
            return ins
        if mn == "jmp":
            ins.kind = "stop"          # indirect or far jump
        return ins

    # -- discovery ----------------------------------------------------------

    def discover(self, entries, tentative=False):
        """Decode from entries.  With tentative, an entry is only kept when
        everything reachable from it decodes cleanly."""
        if tentative:
            for ent in entries:
                self._try_entry(ent)
            return
        work = list(entries)
        while work:
            obj, start = work.pop()
            if not self.is_exec(obj):
                continue
            if start in self.insns[obj] or (obj, start) in self.data_marks:
                continue
            if start in self.owner[obj]:
                self.warnings.append("entry o%d:%04x inside an instruction" %
                                     (obj, start))
                continue
            pos = start
            while True:
                decoded = self._ndisasm(obj, pos)
                if not decoded:
                    break
                restart = None
                stop = False
                for off, size, text in decoded:
                    if off in self.insns[obj]:
                        stop = True
                        break
                    ins = self._classify(obj, off, size, text)
                    if ins.kind == "bad":
                        self.warnings.append("undecodable at o%d:%04x" % (obj, off))
                        stop = True
                        break
                    span = range(off, off + ins.size)
                    if any(b in self.owner[obj] or (obj, b) in self.data_marks
                           for b in span):
                        stop = True
                        break
                    self.insns[obj][off] = ins
                    for b in span:
                        self.owner[obj][b] = off
                    if ins.target is not None:
                        work.append((obj, ins.target))
                    if ins.xtarget is not None:
                        work.append(ins.xtarget)
                    if ins.kind in ("stop", "jmp", "vxdjmp"):
                        stop = True
                        break
                    if ins.kind == "vxd" or ins.size != size:
                        restart = off + ins.size
                        break
                else:
                    restart = decoded[-1][0] + decoded[-1][1]
                if stop or restart is None:
                    break
                if restart in self.insns[obj]:
                    break
                pos = restart


    def _try_entry(self, ent):
        obj, start = ent
        if not self.is_exec(obj) or start in self.insns[obj]:
            return
        img = self.le.objects[obj - 1].data
        if img[start:start + 2] == b"\0\0" or (obj, start) in self.data_marks:
            return
        saved = ({k: dict(v) for k, v in self.insns.items()},
                 {k: dict(v) for k, v in self.owner.items()}, len(self.warnings))
        self.discover([ent])
        new = [(o, i) for o in self.insns for off, i in self.insns[o].items()
               if off not in saved[0][o]]
        ends = any(i.kind in ("stop", "jmp", "vxdjmp") for _o, i in new)
        suspicious = len(self.warnings) > saved[2] or not new or not ends or any(
            re.search(r"(call|jmp) (far |dword )?0x[0-9a-f]+:0x", i.text) or
            i.text.split()[0] in ("arpl", "insd", "insb", "outsd", "outsb",
                                  "hlt", "into", "bound", "int3", "les", "lds")
            for _o, i in new)
        if suspicious:
            self.insns, self.owner = saved[0], saved[1]
            del self.warnings[saved[2]:]


def initial_entries(le, names, fixmap):
    ents = []
    ddb_obj, ddb_off = le.ddb()
    for off, _name in DDB_PROCS:
        f = fixmap.get((ddb_obj, ddb_off + off))
        if f:
            ents.append((f.tobj, f.toff))
    for key, kinds in names.kinds.items():
        if "code" in kinds:
            ents.append(key)
    for f in le.fixups:
        if f.type == 8:
            ents.append((f.tobj, f.toff))
    return ents


def type8_site_entries(le):
    """The branch instruction around every type-8 fixup is certainly code."""
    ents = []
    for f in le.fixups:
        if f.type != 8:
            continue
        img = le.objects[f.obj - 1].data
        if f.off >= 1 and img[f.off - 1] in (0xE8, 0xE9):
            ents.append((f.obj, f.off - 1))
        elif f.off >= 2 and img[f.off - 2] == 0x0F and \
                0x80 <= img[f.off - 1] <= 0x8F:
            ents.append((f.obj, f.off - 2))
    return ents


def gap_entries(dis):
    """Starts of undecoded gaps in code objects (after alignment padding)."""
    ents = []
    for o in dis.le.objects:
        if not o.flags & OBJ_EXEC:
            continue
        owner = dis.owner[o.index]
        pos = 0
        n = len(o.data)
        while pos < n:
            if pos in owner or (o.index, pos) in dis.data_marks:
                pos += 1
                continue
            start = pos
            while start < n and o.data[start] in (0x90, 0xCC) and \
                    start not in owner:
                start += 1
            if start < n and start not in owner and \
                    (o.index, start) not in dis.data_marks:
                ents.append((o.index, start))
            while pos < n and pos not in owner:
                pos += 1
    return ents


def data_pointer_entries(le):
    """Code addresses stored in data objects (function tables)."""
    ents = []
    for f in le.fixups:
        tobj = le.objects[f.tobj - 1]
        if f.type == 7 and tobj.flags & OBJ_EXEC and \
                not le.objects[f.obj - 1].flags & OBJ_EXEC:
            ents.append((f.tobj, f.toff))
    return ents


def callback_entries(dis):
    """Immediate operands that load code addresses (callbacks)."""
    ents = []
    for obj, insns in dis.insns.items():
        for ins in insns.values():
            if not re.match(r"(push dword |mov e[a-ds][xip],)0x", ins.text):
                continue
            f = dis.fix.get((obj, ins.off + ins.size - 4))
            if f and f.type == 7 and dis.is_exec(f.tobj):
                ents.append((f.tobj, f.toff))
    return ents


# --------------------------------------------------------------------------
# emission


class Emitter:
    def __init__(self, le, names, dis, forced, overrides=None):
        self.le, self.names, self.dis = le, names, dis
        self.fix = dis.fix
        self.forced = forced          # set of (obj, off) instructions to emit as db
        self.overrides = overrides or {}  # (obj, off) -> encoding variant
        self.labels = {}              # (obj, off) -> label name
        self.used_services = {}
        self.linemap = {}             # (file, line) -> (obj, off)
        self.sym_relative = set()     # labels used with "wrt ..sym"
        self._assign_labels()

    def label_name(self, obj, off):
        name = self.names.names.get((obj, off))
        if name:
            return name
        if off in self.dis.insns.get(obj, {}):
            return "L%d_%04X" % (obj, off)
        return "D%d_%04X" % (obj, off)

    def _want(self, obj, off):
        self.labels.setdefault((obj, off), None)

    def _assign_labels(self):
        addend_bases = []
        for f in self.le.fixups:
            if f.type == 7:
                img = self.le.objects[f.obj - 1].data
                (inplace,) = struct.unpack_from("<i", img, f.off)
                self._want(f.tobj, f.toff - inplace)
                if inplace:
                    addend_bases.append((f.tobj, f.toff - inplace))
            else:
                self._want(f.tobj, f.toff)
        for obj, insns in self.dis.insns.items():
            for ins in insns.values():
                if ins.target is not None:
                    self._want(obj, ins.target)
        for key in self.names.kinds:
            self._want(*key)
        ddb = self.le.ddb()
        self.exports = {ddb}
        self._want(*ddb)
        if ddb not in self.names.names:
            self.names.names[ddb] = "AUDDRV_DDB"
        for key in list(self.labels):
            self.labels[key] = self.label_name(*key)
        self.sym_relative = {self.labels[k] for k in addend_bases}

    def sym(self, obj, off):
        return self.labels[(obj, off)]

    def fixup_ref(self, f):
        """Operand text for a type-7 fixup.

        Plain references are section-relative in NASM's ELF output, and
        lelink writes zero in place like LINK did.  The few references whose
        image bytes hold an addend use wrt ..sym so the addend stays there.
        """
        img = self.le.objects[f.obj - 1].data
        (inplace,) = struct.unpack_from("<i", img, f.off)
        if not inplace:
            return self.sym(f.tobj, f.toff)
        base = self.sym(f.tobj, f.toff - inplace)
        sign = "-" if inplace < 0 else "+"
        return "%s%s0x%x wrt ..sym" % (base, sign, abs(inplace))

    def service(self, svc):
        dev, num = svc >> 16, svc & 0x7FFF
        name = vxdsvc.service_name(dev, num)
        self.used_services[name] = (dev << 16) | num
        return name

    # -- instruction text ---------------------------------------------------

    def insn_text(self, obj, ins):
        img = self.le.objects[obj - 1].data
        if ins.kind in ("vxd", "vxdjmp"):
            return "%s %s" % ("VxDJmp" if ins.kind == "vxdjmp" else "VxDCall",
                              self.service(ins.svc))
        if ins.kind in ("jmp", "jcc", "call") and (ins.target is not None or
                                                   ins.xtarget is not None):
            mn = ins.text.split()[0]
            tgt = self.sym(*ins.xtarget) if ins.xtarget else \
                self.sym(obj, ins.target)
            if mn == "call":
                return "call %s" % tgt
            if mn in SHORT_ONLY:
                return "%s %s" % (mn, tgt)
            op = img[ins.off]
            near = op in (0xE9,) or (op == 0x0F and 0x80 <= img[ins.off + 1] <= 0x8F)
            return "%s %s %s" % (mn, "near" if near else "short", tgt)
        text = ins.text
        fixes = [self.fix[(obj, b)] for b in range(ins.off, ins.off + ins.size)
                 if (obj, b) in self.fix]
        for f in sorted(fixes, key=lambda f: -f.off):
            if f.type != 7:
                return None
            text = self._subst(text, f, ins)
            if text is None:
                return None
        if self.names.in_range("client", obj, ins.off):
            text = re.sub(r"\[ebp\+0x([0-9a-f]+)\]", lambda m: "[ebp+%s]" %
                          CLIENT_FIELDS[int(m.group(1), 16)]
                          if int(m.group(1), 16) in CLIENT_FIELDS
                          else m.group(0), text)
        if self.names.in_range("ctlmsg", obj, ins.off):
            m = re.match(r"cmp eax,(byte \+)?0x([0-9a-f]+)$", text)
            if m and int(m.group(2), 16) < len(vxdsvc.CONTROL_MESSAGES):
                text = "cmp eax,%s%s" % ("byte " if m.group(1) else "",
                                         vxdsvc.CONTROL_MESSAGES[
                                             int(m.group(2), 16)])
        return text

    def _subst(self, text, f, ins):
        img = self.le.objects[f.obj - 1].data
        (inplace,) = struct.unpack_from("<I", img, f.off)
        ref = self.fixup_ref(f)
        at_end = f.off + 4 == ins.off + ins.size
        mn, _, ops = text.partition(" ")
        # literal tokens with their bracket depth
        toks = [(m.start(), m.end(), int(m.group(0), 16))
                for m in re.finditer(r"0x[0-9a-f]+", ops)]

        def in_brackets(pos):
            return ops.rfind("[", 0, pos) > ops.rfind("]", 0, pos)
        outside = [t for t in toks if not in_brackets(t[0]) and t[2] == inplace]
        inside = [t for t in toks if in_brackets(t[0]) and t[2] == inplace]
        cand = None
        if at_end and outside:
            cand = outside[-1]
        elif inside:
            cand = inside[-1] if at_end else inside[0]
        if cand is None:
            return None
        s, e, _v = cand
        repl = ref
        # a "+0x4" displacement keeps its sign, a bare absolute gets the symbol
        return mn + " " + ops[:s] + repl + ops[e:]

    # -- data ---------------------------------------------------------------

    @staticmethod
    def _db(bs):
        return "db " + ", ".join("0x%02x" % b for b in bs)

    def data_lines(self, obj, start, end):
        """Emit bytes [start, end) of an object as data lines."""
        img = self.le.objects[obj - 1].data
        out = []
        pos = start
        while pos < end:
            f = self.fix.get((obj, pos))
            if f is not None and f.off == pos:
                if f.type != 7 or pos + 4 > end:
                    raise RuntimeError("cannot emit fixup at o%d:%04x as data" %
                                       (obj, pos))
                out.append((pos, "dd " + self.fixup_ref(f)))
                pos += 4
                continue
            # next boundary: fixup site or end
            nxt = end
            for b in range(pos + 1, end):
                if (obj, b) in self.fix and self.fix[(obj, b)].off == b:
                    nxt = b
                    break
            run = bytes(img[pos:nxt])
            out.extend(self._fmt_run(pos, run))
            pos = nxt
        return out

    def _fmt_run(self, base, run):
        out = []
        i = 0
        while i < len(run):
            # zero fill
            z = i
            while z < len(run) and run[z] == 0:
                z += 1
            if z - i >= 16:
                out.append((base + i, "times %d db 0" % (z - i)))
                i = z
                continue
            # printable string terminated by NUL
            s = i
            while s < len(run) and (0x20 <= run[s] < 0x7F):
                s += 1
            if s - i >= 4 and s < len(run) and run[s] == 0:
                txt = run[i:s].decode("ascii")
                q = '"' if '"' not in txt else "'" if "'" not in txt else None
                if q:
                    out.append((base + i, "db %s%s%s, 0" % (q, txt, q)))
                    i = s + 1
                    continue
            n = min(16, len(run) - i)
            out.append((base + i, self._db(run[i:i + n])))
            i += n
        return out

    def ddb_lines(self, obj, base):
        """The VxD_Desc_Block, one line per field."""
        img = self.le.objects[obj - 1].data
        out = []
        for off, size, name in DDB_FIELDS:
            at = base + off
            f = self.fix.get((obj, at))
            if f is not None and f.off == at:
                val = self.fixup_ref(f)
            elif name == "DDB_Name":
                val = '"%s"' % img[at:at + 8].decode("latin-1")
            else:
                v = int.from_bytes(img[at:at + size], "little")
                val = "0x%0*X" % (size * 2, v)
                raw = bytes(img[at:at + 4])
                if size == 4 and all(0x20 <= c < 0x7F for c in raw):
                    val += "  ; '%s'" % raw[::-1].decode("ascii")
            kw = {1: "db", 2: "dw", 4: "dd", 8: "db"}[size]
            text = "%s %s" % (kw, val)
            pad = max(1, 40 - len(text))
            out.append(("%s%s; %s" % (text, " " * pad, name)
                        if ";" not in text else text + " " + name, at))
        return out

    def forced_insn_lines(self, obj, ins):
        """Emit an instruction as bytes, keeping fixups/branches symbolic."""
        img = self.le.objects[obj - 1].data
        pos, end = ins.off, ins.off + ins.size
        out = []
        if ins.kind in ("jmp", "jcc", "call") and ins.target is not None:
            disp_len = 1 if ins.size - self._oplen(img, ins) == 1 else 4
            opl = ins.size - disp_len
            out.append((pos, self._db(img[pos:pos + opl])))
            tgt = self.sym(obj, ins.target)
            out.append((pos + opl, "%s %s - ($ + %d)" %
                        ("db" if disp_len == 1 else "dd", tgt, disp_len)))
            return out
        if ins.xtarget is not None:
            raise RuntimeError("cannot force cross-object branch o%d:%04x" %
                               (obj, ins.off))
        if ins.kind in ("vxd", "vxdjmp"):
            return [(pos, self.insn_text(obj, ins))]
        return self.data_lines(obj, pos, end)

    @staticmethod
    def _oplen(img, ins):
        op = img[ins.off]
        if op == 0x0F:
            return 2
        return 1

    # -- objects ------------------------------------------------------------

    def object_lines(self, obj):
        o = self.le.objects[obj - 1]
        insns = self.dis.insns[obj]
        labels_here = sorted(off for (ob, off) in self.labels if ob == obj)
        label_set = set(labels_here)
        lines = []   # (offset or None, text, is_label)
        pos = 0
        end = len(o.data)

        def emit_labels(at):
            if at in label_set:
                key = (obj, at)
                if key in self.names.comments:
                    lines.append((None, "; " + self.names.comments[key], "c"))
                lines.append((at, self.labels[key] + ":", "l"))

        while pos < end:
            emit_labels(pos)
            if self.names.has_kind((obj, pos), "ddb"):
                for text, off in self.ddb_lines(obj, pos):
                    lines.append((off, INDENT + text, "d"))
                pos += 0x50
                continue
            ins = insns.get(pos)
            if ins is not None:
                inner = [b for b in range(pos + 1, pos + ins.size) if b in label_set]
                text = None if (obj, pos) in self.forced or inner else \
                    self.insn_text(obj, ins)
                if text is not None and (obj, pos) in self.overrides:
                    text = apply_variant(text, self.overrides[(obj, pos)])
                if text is not None:
                    lines.append((pos, INDENT + text, "i"))
                else:
                    # bytes, with labels placed inside
                    cut = [pos] + inner + [pos + ins.size]
                    if inner:
                        for a, b in zip(cut, cut[1:]):
                            if a != pos:
                                emit_labels(a)
                            for off, t in self.data_lines(obj, a, b):
                                lines.append((off, INDENT + t, "d"))
                    else:
                        for off, t in self.forced_insn_lines(obj, ins):
                            lines.append((off, INDENT + t, "d"))
                pos += ins.size
                continue
            nxt = pos + 1
            while nxt < end and nxt not in insns and nxt not in label_set:
                nxt += 1
            for off, t in self.data_lines(obj, pos, nxt):
                lines.append((off, INDENT + t, "d"))
            pos = nxt
        emit_labels(end)
        return lines


def section_decl(o):
    name = o.name_str
    if o.flags & OBJ_EXEC:
        attrs = "progbits alloc exec nowrite align=1"
    else:
        attrs = "progbits alloc noexec write align=1"
    return "section %s %s" % (name, attrs)


def render_object(em, obj, fname):
    o = em.le.objects[obj - 1]
    lines = em.object_lines(obj)
    out = []
    kind = "code and data" if o.flags & OBJ_EXEC else "data"
    out.append("; %s: object %d of %s (%s, flags %04Xh)" %
               (o.name_str, obj, em.le_name, kind, o.flags))
    out.append("; Generated by tools/vxd2asm.py, then maintained by hand.")
    out.append("; Trailing comments give the original object offset.")
    out.append("")
    out.append(section_decl(o))
    out.append("")
    body = []
    for off, text, kind in lines:
        if kind in ("l", "c"):
            body.append((None, text))
        else:
            pad = max(1, COMMENT_COL - len(text))
            body.append(((obj, off), "%s%s; %04X" % (text, " " * pad, off)))
    exported = [em.labels[k] for k in sorted(em.labels)
                if k[0] == obj and (em.labels[k] in em.sym_relative or
                                    k in em.exports)]
    for i in range(0, len(exported), 6):
        out.append("global " + ", ".join(exported[i:i + 6]))
    if exported:
        out.append("")
    for key, text in body:
        out.append(text)
        if key is not None:
            em.linemap[(fname, len(out))] = key
    return "\n".join(out) + "\n"


def render_services(em):
    out = ["; VxD services referenced by the source (int 20h dynamic links).",
           "; value = (device ID << 16) | service number", ""]
    for name, value in sorted(em.used_services.items(), key=lambda kv: kv[1]):
        out.append("%-40s equ 0x%08X" % (name, value))
    return "\n".join(out) + "\n"


VXD_INC = """\
; vxd.inc -- macros and structures shared by the ES1869.VXD source.

%ifndef VXD_INC
%define VXD_INC

; Dynamic link to a VxD service: int 20h followed by (device << 16 | service).
; VMM patches the six bytes into a direct call on first use.
%macro VxDCall 1
        int     0x20
        dd      %1
%endmacro

; Same, but the service is entered with a jump (bit 15 of the service number).
%macro VxDJmp 1
        int     0x20
        dd      (%1) | 0x8000
%endmacro

%include "services.inc"

; Register-register instructions in the "load" encoding (op reg, r/m), which
; MASM prefers and NASM does not emit by default.  "mov_ ebp,esp" assembles to
; 8B EC where "mov ebp,esp" would give 89 E5; both do the same thing.
%define REG_eax 0
%define REG_ecx 1
%define REG_edx 2
%define REG_ebx 3
%define REG_esp 4
%define REG_ebp 5
%define REG_esi 6
%define REG_edi 7
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
%define WID_eax 32
%define WID_ecx 32
%define WID_edx 32
%define WID_ebx 32
%define WID_esp 32
%define WID_ebp 32
%define WID_esi 32
%define WID_edi 32
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

%macro _rr_load 3                       ; opcode (32-bit form), dst, src
%if WID_%2 == 32
        db      %1, 0xC0 | (REG_%2 << 3) | REG_%3
%elif WID_%2 == 16
        db      0x66, %1, 0xC0 | (REG_%2 << 3) | REG_%3
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

%include "ctlmsg.inc"

; Client_Reg_Struc offsets (EBP points to it in V86/PM API procedures)
Client_EDI      equ 0x00
Client_ESI      equ 0x04
Client_EBP      equ 0x08
Client_EBX      equ 0x10
Client_EDX      equ 0x14
Client_ECX      equ 0x18
Client_EAX      equ 0x1C
Client_Error    equ 0x20
Client_EIP      equ 0x24
Client_CS       equ 0x28
Client_EFlags   equ 0x2C
Client_ESP      equ 0x30
Client_SS       equ 0x34
Client_ES       equ 0x38
Client_DS       equ 0x3C
Client_FS       equ 0x40
Client_GS       equ 0x44
CF_Mask         equ 0x0001

%endif
"""


def render_main(le, files, le_name):
    out = ["; %s -- reassemblable source" % le_name,
           ";",
           "; Build:  python3 tools/build_vxd.py            (extended driver)",
           ";         python3 tools/build_vxd.py --stock    (identical to the original)",
           ";",
           "; ESSREG_EXT=0 reproduces the original ESS driver byte for byte;",
           "; ESSREG_EXT=1 adds the essreg register API (src/vxd/essext.asm).",
           "",
           "%ifndef ESSREG_EXT",
           "%define ESSREG_EXT 1",
           "%endif",
           "",
           '%include "vxd.inc"',
           ""]
    for f in files:
        out.append('%%include "%s"' % f)
    out += ["", "%if ESSREG_EXT", '%include "essext.asm"', "%endif", ""]
    return "\n".join(out)


def make_layout(le, le_name):
    hdr = {}
    for name, _off, fmt in HDR_FIELDS:
        if name in lelink.COMPUTED:
            continue
        v = le.hdr[name]
        if isinstance(v, bytes):
            v = v.hex() if name == "res3" else v.decode("latin-1")
        hdr[name] = v
    layout = {
        "format": 1,
        "source": le_name,
        "stub": "stub.bin",
        "winres": "version.bin" if le.winres else "",
        "gap": "gap.bin",
        "header": hdr,
        "objects": [{"section": o.name_str, "name": o.name_str,
                     "flags": o.flags, "base": o.base} for o in le.objects],
        "orphan_pages": [],
        "resident_names": [[n.decode("latin-1"), o] for n, o in le.resident_names],
        "nonresident_names": [[n.decode("latin-1"), o] for n, o in le.nonres_names],
        "entries": [],
    }
    for ordinal, (obj, off, flags) in sorted(le.entries.items()):
        layout["entries"].append({"ordinal": ordinal, "symbol": None,
                                  "flags": flags, "_at": [obj, off]})
    for after, pages in le.orphan_pages().items():
        name = le.objects[after - 1].name_str if after else ""
        layout["orphan_pages"].append({"after": name,
                                       "file": "slack.bin", "_pages": pages})
    return layout


LST_RE = re.compile(
    r"^\s*(\d+) ([0-9A-F]{8}) ((?:[0-9A-F]|\[[0-9A-F]+\]|\([0-9A-F]+\))+"
    r"(?:<rep [0-9A-F]+h>)?)(-?)\s*(?:<\d+>)?\s?(.*)$")
SECTION_RE = re.compile(r"^\s*\d+\s+(?:<\d+>\s+)?section\s+(\w+)")
OFFSET_COMMENT_RE = re.compile(r";\s*([0-9A-F]{4,6})\s*$")


def _listing_bytes(hx):
    """Decode a NASM listing hex field: list of int or None (relocated)."""
    out = []
    rep = re.search(r"<rep ([0-9A-F]+)h>$", hx)
    if rep:
        hx = hx[:rep.start()]
    for m in re.finditer(r"\[([0-9A-F]+)\]|\(([0-9A-F]+)\)|([0-9A-F]{2})", hx):
        if m.group(1) is not None:
            out += [None] * (len(m.group(1)) // 2)       # abs: checked after link
        elif m.group(2) is not None:
            out += [None] * (len(m.group(2)) // 2)       # rel: not compared
        else:
            out.append(int(m.group(3), 16))
    if rep:
        out = out * int(rep.group(1), 16)
    return out


def check_listing(path, le, sect_obj, masks=None, detail=None):
    """Compare each listed source line that has an original-offset comment
    with the original object bytes, and return the set of (obj, off) that
    differ.  Bytes from a macro expansion count for the line that invoked
    it.  masks maps (obj, off) -> set of byte indexes not to compare
    (branch displacements)."""
    masks = masks or {}
    section = None
    items = []
    cur = None
    with open(path, errors="replace") as f:
        for raw in f:
            raw = raw.rstrip("\n")
            m = LST_RE.match(raw)
            src = m.group(5) if m else raw
            sm = SECTION_RE.match(raw)
            if sm:
                section = sm.group(1)
                cur = None
                continue
            cm = OFFSET_COMMENT_RE.search(src)
            if cm and not re.match(r"\s*\d+\s+<\d+>", raw[:12] + " "):
                cur = {"section": section, "off": int(cm.group(1), 16),
                       "hex": "", "src": src}
                items.append(cur)
            elif re.match(r"^\s*\d+\s+\S*\s*(<\d+>\s*)?[\w.$@?]+:\s*$", raw):
                cur = None           # a label line ends the current item
            if m and cur is not None:
                cur["hex"] += m.group(3)
    bad = set()
    for it in items:
        obj = sect_obj.get(it["section"])
        if obj is None:
            continue
        off = it["off"]
        got = _listing_bytes(it["hex"])
        want = le.objects[obj - 1].data[off:off + len(got)]
        skip = masks.get((obj, off), ())
        if len(want) != len(got) or any(
                g is not None and i not in skip and g != w
                for i, (g, w) in enumerate(zip(got, want))):
            bad.add((obj, off))
            if detail is not None:
                detail[(obj, off)] = (got, bytes(want), it["src"])
    return bad



REG32 = "eax ecx edx ebx esp ebp esi edi".split()
REG16 = "ax cx dx bx sp bp si di".split()
REG8 = "al cl dl bl ah ch dh bh".split()
REGS = set(REG32 + REG16 + REG8)
LOAD_MACROS = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp", "mov"}


def variants(text):
    """Other spellings of an instruction that select other encodings, as a
    list of variant descriptors for apply_variant."""
    out = []
    mn, _, ops = text.partition(" ")
    parts = ops.split(",")
    if mn in LOAD_MACROS and len(parts) == 2 and \
            parts[0] in REGS and parts[1] in REGS:
        out.append(("load",))
    has_mem = "[" in ops
    lit = None
    for m in re.finditer(r"(?<![\w\[+-])(0x[0-9a-f]+)", ops):
        if ops.rfind("[", 0, m.start()) <= ops.rfind("]", 0, m.start()):
            lit = m
    disp_opts = [None]
    if has_mem and not re.search(r"\[(byte|dword) ", ops):
        disp_opts = [None, "byte", "dword"]
    imm_opts = [None]
    if lit is not None and "strict" not in ops:
        imm_opts = [None, "dword", "byte"]
    for d in disp_opts:
        for i in imm_opts:
            if d or i:
                out.append(("size", d, i))
    return out


def apply_variant(text, var):
    mn, _, ops = text.partition(" ")
    if var[0] == "load":
        return "%s_ %s" % (mn, ops)
    _, disp, imm = var
    if imm:
        lit = None
        for m in re.finditer(r"(?<![\w\[+-])(0x[0-9a-f]+)", ops):
            if ops.rfind("[", 0, m.start()) <= ops.rfind("]", 0, m.start()):
                lit = m
        if lit is not None:
            ops = ops[:lit.start()] + "strict %s %s" % (imm, lit.group(1)) + \
                ops[lit.end():]
    if disp:
        ops = re.sub(r"\[", "[%s " % disp, ops, count=1)
    return "%s %s" % (mn, ops)


def search_encodings(tmp, texts, want, masks):
    """Find, for each key, a variant whose encoding matches want[key].

    texts: key -> instruction text, want: key -> original bytes.
    Returns key -> variant descriptor."""
    cands = []
    for key, text in texts.items():
        for var in variants(text):
            cands.append((key, var, apply_variant(text, var)))
    if not cands:
        return {}
    idents = set()
    for _k, _v, t in cands:
        operands = t.split(" ", 1)[1] if " " in t else ""
        for m in re.finditer(r"\b([A-Za-z_][\w.@?$]*)\b", operands):
            w = m.group(1)
            if w not in REGS and not re.match(r"(byte|word|dword|strict|wrt|"
                                              r"near|short|sym|0x)", w):
                idents.add(w)
    src = os.path.join(tmp, "enc.asm")
    lst = os.path.join(tmp, "enc.lst")
    alive = list(range(len(cands)))
    for _ in range(20):
        with open(src, "w") as f:
            f.write('%include "vxd.inc"\nsection .text\n')
            for w in sorted(idents):
                f.write("extern %s\n" % w)
            lines = {}
            n = 2 + len(idents)
            for i in alive:
                f.write("        %s ; %06X\n" % (cands[i][2], i))
                n += 1
                lines[n] = i
        res = subprocess.run(["nasm", "-f", "elf32", "-I", tmp + os.sep,
                              "-l", lst, "-o", os.path.join(tmp, "enc.o"), src],
                             capture_output=True, text=True)
        if res.returncode == 0:
            break
        bad = {lines.get(int(m.group(1))) for m in
               re.finditer(r"enc\.asm:(\d+): error", res.stderr)}
        bad.discard(None)
        if not bad:
            return {}
        alive = [i for i in alive if i not in bad]
    else:
        return {}
    found = {}
    with open(lst, errors="replace") as f:
        cur = None
        got = {}
        for raw in f:
            m = LST_RE.match(raw.rstrip("\n"))
            src_txt = m.group(5) if m else raw
            cm = re.search(r"; ([0-9A-F]{6})\s*$", src_txt)
            if cm:
                cur = int(cm.group(1), 16)
                got[cur] = ""
            if m and cur is not None:
                got[cur] += m.group(3)
    for i, hx in got.items():
        key, var, _t = cands[i]
        if key in found:
            continue
        b = _listing_bytes(hx)
        w = want[key]
        skip = masks.get(key, ())
        if len(b) == len(w) and all(g is None or j in skip or g == x
                                    for j, (g, x) in enumerate(zip(b, w))):
            found[key] = var
    return found


def assemble(srcdir, main, defines, outobj, listing=None):
    cmd = ["nasm", "-f", "elf32", "-I", srcdir + os.sep, "-o", outobj]
    if listing:
        cmd += ["-l", listing]
    for k, v in defines.items():
        cmd.append("-D%s=%s" % (k, v))
    cmd.append(os.path.join(srcdir, main))
    return subprocess.run(cmd, capture_output=True, text=True)


def generate(le_path, names_path, outdir, keep_going=True, verbose=True):
    le = read_le(le_path)
    le_name = os.path.basename(le_path)
    names = Names(names_path)
    tmp = tempfile.mkdtemp(prefix="vxd2asm")
    try:
        dis = Disassembler(le, names, tmp)
        dis.discover(initial_entries(le, names, dis.fix))
        dis.discover(type8_site_entries(le))
        dis.discover(data_pointer_entries(le), tentative=True)
        for _ in range(4):
            before = sum(len(v) for v in dis.insns.values())
            dis.discover(callback_entries(dis), tentative=True)
            dis.discover(gap_entries(dis), tentative=True)
            if sum(len(v) for v in dis.insns.values()) == before:
                break
        if verbose:
            for o in le.objects:
                code = sum(i.size for i in dis.insns[o.index].values())
                print("  %-4s %6d bytes, %5d as code (%d instructions)" %
                      (o.name_str, len(o.data), code, len(dis.insns[o.index])))
            for w in dis.warnings[:20]:
                print("  warning:", w)

        layout = make_layout(le, le_name)
        forced = set()
        overrides = {}
        # branch displacements depend on layout, not on the encoding choice
        masks = {}
        for obj, insns in dis.insns.items():
            for ins in insns.values():
                if ins.target is not None or ins.xtarget is not None:
                    img = le.objects[obj - 1].data
                    opl = 2 if img[ins.off] == 0x0F else 1
                    masks[(obj, ins.off)] = set(range(opl, ins.size))
        os.makedirs(outdir, exist_ok=True)
        for attempt in range(12):
            em = Emitter(le, names, dis, forced, overrides)
            em.le_name = le_name
            files = []
            for o in le.objects:
                fname = o.name_str.lower() + ".asm"
                files.append(fname)
                with open(os.path.join(tmp, fname), "w") as f:
                    f.write(render_object(em, o.index, fname))
            with open(os.path.join(tmp, "services.inc"), "w") as f:
                f.write(render_services(em))
            with open(os.path.join(tmp, "vxd.inc"), "w") as f:
                f.write(VXD_INC)
            with open(os.path.join(tmp, "ctlmsg.inc"), "w") as f:
                f.write("; System control messages (EAX of a VxD Control_Proc)\n\n")
                for n, name in enumerate(vxdsvc.CONTROL_MESSAGES):
                    f.write("%-28s equ 0x%02X\n" % (name, n))
            main = os.path.splitext(le_name)[0].lower() + ".asm"
            with open(os.path.join(tmp, main), "w") as f:
                f.write(render_main(le, files, le_name))
            with open(os.path.join(tmp, "essext.asm"), "w") as f:
                f.write("; placeholder\n")
            # finish the layout: entry symbols
            for e in layout["entries"]:
                e["symbol"] = em.labels[tuple(e["_at"])]
            write_layout(tmp, layout, le)

            res = assemble(tmp, main, {"ESSREG_EXT": 0},
                           os.path.join(tmp, "out.o"),
                           os.path.join(tmp, "out.lst"))
            if res.returncode != 0:
                bad = set()
                for m in re.finditer(r"([\w.]+):(\d+): error", res.stderr):
                    key = em.linemap.get((m.group(1), int(m.group(2))))
                    if key is None:
                        raise RuntimeError(res.stderr[:2000])
                    bad.add(key)
                if verbose:
                    print("  pass %d: %d assembler errors" % (attempt, len(bad)))
                if bad <= forced:
                    raise RuntimeError("assembler errors persist:\n" +
                                       res.stderr[:3000])
                forced |= bad
                continue
            sect_obj = {o.name_str: o.index for o in le.objects}
            detail = {} if os.environ.get("VXD2ASM_DEBUG") else None
            bad = check_listing(os.path.join(tmp, "out.lst"), le, sect_obj,
                                masks, detail)
            if detail:
                with open(os.environ["VXD2ASM_DEBUG"], "w") as dbg:
                    for k in sorted(detail):
                        got, want, src = detail[k]
                        orig = le.objects[k[0] - 1].data[k[1]:k[1] + 16]
                        dbg.write("o%d:%04x %-48s nasm=%s orig=%s\n" % (
                            k[0], k[1], src.split(";")[0].strip(),
                            "".join("??" if g is None else "%02x" % g
                                    for g in got), orig.hex()))
            bad = {k for k in bad if k[1] in dis.insns.get(k[0], {})} | \
                {k for k in bad if k[1] not in dis.insns.get(k[0], {})}
            data_bad = {k for k in bad if k[1] not in dis.insns.get(k[0], {})}
            if data_bad:
                raise RuntimeError("data lines differ: %s" % sorted(
                    "o%d:%04x" % k for k in data_bad)[:10])
            if bad:
                texts, want = {}, {}
                for k in bad:
                    if k in overrides or k in forced:
                        continue
                    ins = dis.insns[k[0]][k[1]]
                    t = em.insn_text(k[0], ins)
                    if t is not None:
                        texts[k] = t
                        want[k] = bytes(le.objects[k[0] - 1].data[
                            ins.off:ins.off + ins.size])
                found = search_encodings(tmp, texts, want, masks)
                overrides.update(found)
                newly = {k for k in bad if k not in found}
                if verbose:
                    print("  pass %d: %d instructions re-encode differently, "
                          "%d fixed by an encoding variant" %
                          (attempt, len(bad), len(found)))
                if newly <= forced and not found:
                    raise RuntimeError("encoding mismatch persists")
                forced |= newly
                continue
            data, objects, fixups = lelink.link(
                elf32.read_elf(os.path.join(tmp, "out.o")),
                json.load(open(os.path.join(tmp, "layout.json"))), tmp)
            bad = diff_objects(le, objects, fixups, dis)
            if not bad and data == le.raw:
                if verbose:
                    print("  pass %d: rebuild is byte-identical (%d instructions "
                          "emitted as bytes)" % (attempt, len(forced)))
                break
            if not bad:
                raise RuntimeError("objects match but the file differs")
            if verbose:
                print("  pass %d: %d instructions differ" % (attempt, len(bad)))
            forced |= bad
        else:
            raise RuntimeError("could not reach a byte-identical rebuild")

        for f in os.listdir(tmp):
            if f in ("chunk.bin", "out.o", "out.lst", "essext.asm", "enc.asm",
                     "enc.lst", "enc.o"):
                continue
            shutil.copy(os.path.join(tmp, f), os.path.join(outdir, f))
        if not os.path.exists(os.path.join(outdir, "essext.asm")):
            shutil.copy(os.path.join(tmp, "essext.asm"),
                        os.path.join(outdir, "essext.asm"))
        return dis, em
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def write_layout(d, layout, le):
    lay = json.loads(json.dumps(layout, default=lambda x: None))
    with open(os.path.join(d, "stub.bin"), "wb") as f:
        f.write(le.stub)
    if le.winres:
        with open(os.path.join(d, "version.bin"), "wb") as f:
            f.write(le.winres)
    with open(os.path.join(d, "gap.bin"), "wb") as f:
        f.write(le.gap)
    for spec, orig in zip(lay["orphan_pages"], layout["orphan_pages"]):
        with open(os.path.join(d, spec["file"]), "wb") as f:
            f.write(b"".join(orig["_pages"]))
        spec.pop("_pages", None)
    for e in lay["entries"]:
        e.pop("_at", None)
    with open(os.path.join(d, "layout.json"), "w") as f:
        json.dump(lay, f, indent=2)
        f.write("\n")


def diff_objects(le, objects, fixups, dis):
    """Return the set of (obj, insn offset) whose bytes or fixups differ."""
    bad = set()
    orig_fix = {(f.obj, f.off): (f.type, f.tobj, f.toff) for f in le.fixups}
    new_fix = {(f.obj, f.off): (f.type, f.tobj, f.toff) for f in fixups}

    def owner(obj, b):
        start = dis.owner[obj].get(b)
        return (obj, start) if start is not None else None

    for o_orig, o_new in zip(le.objects, objects):
        a, b = o_orig.data, o_new.data
        if len(a) != len(b):
            raise RuntimeError("object %s size changed %#x -> %#x" %
                               (o_orig.name_str, len(a), len(b)))
        for i in range(len(a)):
            if a[i] != b[i]:
                k = owner(o_orig.index, i)
                if k is None:
                    raise RuntimeError("data byte o%d:%04x differs" %
                                       (o_orig.index, i))
                bad.add(k)
    for key in set(orig_fix) | set(new_fix):
        if orig_fix.get(key) != new_fix.get(key):
            k = owner(*key)
            if k is None:
                raise RuntimeError("data fixup o%d:%04x differs" % key)
            bad.add(k)
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("vxd")
    ap.add_argument("-n", "--names")
    ap.add_argument("-o", "--outdir", required=True)
    args = ap.parse_args()
    generate(args.vxd, args.names, args.outdir)


if __name__ == "__main__":
    main()
