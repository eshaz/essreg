# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Test build/ES1869.DRV, ESS's driver rebuilt from src/es1869
(tools/build_es1869drv.py): the Audio 2 DAC's mode and the SYSTEM.INI
settings that choose it, and ESS's code at its addresses.

The changed code runs in a CPU emulator inside ESS's own, for ESS's driver
and the changed one: the wave-out open (1:1148) and the end of a playback
start (6:2DDC), against a simulated mixer, and the settings read at the
first enable with an emulated GetPrivateProfileInt.
"""

import os
import struct
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import build_es1869drv  # noqa: E402
from retools.ne import NEFile  # noqa: E402

try:
    from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN,
                         UC_HOOK_INTR)
    from unicorn.x86_const import (UC_X86_REG_CS, UC_X86_REG_IP,
                                   UC_X86_REG_DS, UC_X86_REG_SS,
                                   UC_X86_REG_SP, UC_X86_REG_BP,
                                   UC_X86_REG_AX, UC_X86_REG_EFLAGS,
                                   UC_X86_INS_IN, UC_X86_INS_OUT)
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

SEG_PARA = {i: 0x1000 * i for i in range(1, 8)}
STACK_PARA = 0x8000
STUB_PARA = 0x9000          # the return address (0) and the imports: int 3
AUDIO_BASE = 0x220
DEV = 0x4000                # a device structure in the data segment
DEMAND = 0x80               # dev+2Bh bit 7, demand transfers
PPINT = (1, 127)            # KERNEL.GetPrivateProfileInt
OPT_A2_4X, OPT_A2_FILTER, OPT_READ = 0x0100, 0x0200, 0x8000


def profile_int(value):
    """what Windows' GetPrivateProfileInt makes of a value: its leading
    digits after blanks, 0 without any"""
    digits = ""
    for ch in value.lstrip(" \t"):
        if not ch.isdigit():
            break
        digits += ch
    return int(digits) & 0xFFFF if digits else 0


class Drv:
    """ES1869.DRV's segments at fixed paragraphs, its relocations applied,
    and the mixer at AUDIO_BASE+4/+5. Each import is a stub: only
    GetPrivateProfileInt answers, from ini, {(section, key): value}"""

    def __init__(self, data, syms=None):
        self.ne = ne = NEFile(data)
        self.syms = syms or {}
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x100000)
        for s in ne.segments:
            uc.mem_write(SEG_PARA[s.index] * 16, ne.segment_data(s.index))
        uc.mem_write(STUB_PARA * 16, b"\xCC" * 0x400)
        self.imports = {}           # stub offset -> (module, ordinal)
        self._relocate()
        self.mixer = bytearray(256)
        self.index = 0
        self.writes = []
        self.ini = {}
        self.ppint = []             # (section, key, default, file) asked
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INTR, self._intr)
        self.w16(7, DEV, AUDIO_BASE)
        self.stopped = False

    def _stub(self, target):
        for off, t in self.imports.items():
            if t == target:
                return off
        off = 0x10 + 2 * len(self.imports)
        self.imports[off] = target
        return off

    def _intr(self, uc, intno, _user):
        cs, ip = uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)
        if cs == STUB_PARA and self.imports.get(ip - 1) == PPINT:
            self._profile_int()
            return
        self.stopped = cs == STUB_PARA and ip == 1
        if cs == STUB_PARA and ip - 1 in self.imports:
            self.called = self.imports[ip - 1]
        uc.emu_stop()

    def _profile_int(self):
        """GetPrivateProfileInt(section, key, default, file), far pascal"""
        uc = self.uc
        sp = uc.reg_read(UC_X86_REG_SP)
        w = struct.unpack("<9H", uc.mem_read(STACK_PARA * 16 + sp, 18))
        ret_ip, ret_cs, file_off, file_seg, default = w[:5]
        key_off, key_seg, sec_off, sec_seg = w[5:]
        section = self.cstr(sec_seg, sec_off)
        key = self.cstr(key_seg, key_off)
        self.ppint.append((section, key, default,
                           self.cstr(file_seg, file_off)))
        found = {(a.lower(), b.lower()): v for (a, b), v in self.ini.items()}
        value = found.get((section.lower(), key.lower()))
        uc.reg_write(UC_X86_REG_AX, default if value is None
                     else profile_int(value))
        uc.reg_write(UC_X86_REG_SP, sp + 4 + 14)
        uc.reg_write(UC_X86_REG_CS, ret_cs)
        uc.reg_write(UC_X86_REG_IP, ret_ip)

    def cstr(self, seg, off):
        out = bytearray()
        while True:
            b = self.uc.mem_read(seg * 16 + off + len(out), 1)[0]
            if not b:
                return out.decode("latin-1")
            out.append(b)

    def _relocate(self):
        for s in self.ne.segments:
            base = SEG_PARA[s.index] * 16
            for rtype, rflags, off, target in s.relocs:
                if rflags & 3 == 0 and target[0] & 0xFF != 0xFF:
                    seg, toff = SEG_PARA[target[0] & 0xFF], target[1]
                else:
                    seg, toff = STUB_PARA, self._stub(tuple(target))
                sites = [off]
                if not rflags & 4:
                    sites, pos = [], off
                    while pos != 0xFFFF:
                        sites.append(pos)
                        pos = struct.unpack("<H", self.uc.mem_read(
                            base + pos, 2))[0]
                for site in sites:
                    if rtype == 2:
                        val = struct.pack("<H", seg)
                    elif rtype == 3:
                        val = struct.pack("<HH", toff, seg)
                    elif rtype == 5:
                        val = struct.pack("<H", toff)
                    else:
                        raise ValueError("relocation type %d" % rtype)
                    self.uc.mem_write(base + site, val)

    def w16(self, seg, off, val):
        self.uc.mem_write(SEG_PARA[seg] * 16 + off, struct.pack("<H", val))

    def r16(self, seg, off):
        return struct.unpack("<H", self.uc.mem_read(
            SEG_PARA[seg] * 16 + off, 2))[0]

    def _in(self, uc, port, size, _user):
        if port == AUDIO_BASE + 5:
            return self.mixer[self.index]
        return 0xFF

    def _out(self, uc, port, size, value, _user):
        if port == AUDIO_BASE + 4:
            self.index = value & 0xFF
        elif port == AUDIO_BASE + 5:
            self.mixer[self.index] = value & 0xFF
            self.writes.append((self.index, value & 0xFF))

    def _run(self, seg, ip, sp, bp):
        uc = self.uc
        uc.reg_write(UC_X86_REG_SS, STACK_PARA)
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_BP, bp)
        uc.reg_write(UC_X86_REG_DS, SEG_PARA[7])
        uc.reg_write(UC_X86_REG_EFLAGS, 0x0202)
        uc.reg_write(UC_X86_REG_CS, SEG_PARA[seg])
        uc.reg_write(UC_X86_REG_IP, ip)
        self.stopped, self.called = False, None
        uc.emu_start(SEG_PARA[seg] * 16 + ip, 0, count=100000)
        if self.called:
            raise RuntimeError("import %s called" % (self.called,))
        if not self.stopped:
            raise RuntimeError("didn't return")

    def _stack(self, words):
        sp = 0xFF00 - 2 * len(words)
        self.uc.mem_write(STACK_PARA * 16 + sp,
                          b"".join(struct.pack("<H", w) for w in words))
        return sp

    def call_far(self, name, *args):
        """a far routine of the fixed build, by name, with pascal args"""
        seg, off = self.syms[name]
        sp = self._stack([0, STUB_PARA] + list(reversed(args)))
        self._run(seg, off, sp, 0)

    def wave_open(self):
        """1:1148(dev), far pascal: what the wave-out open, close and
        resume write"""
        sp = self._stack([0, STUB_PARA, DEV])     # return IP, CS, dev
        self._run(1, 0x1148, sp, 0)

    def playback_start_end(self, flags=DEMAND):
        """from 6:2DDC to the end of the playback start: 71h, then 78h,
        with its frame as it is there ([bp+6] dev, [bp-2] a byte)"""
        self.uc.mem_write(SEG_PARA[7] * 16 + DEV + 0x2B, bytes([flags]))
        # [bp-2] local, [bp] old BP, [bp+2] return IP and CS, [bp+6] dev
        sp = self._stack([0, 0, 0, STUB_PARA, DEV])
        self._run(6, 0x2DDC, sp, sp + 2)

    @property
    def opts(self):
        return self.r16(7, self.syms["es_opts"][1])

    @opts.setter
    def opts(self, value):
        self.w16(7, self.syms["es_opts"][1], value)


def a2_mode(old, opts):
    """71h as the fixed driver writes it, from old, at both sites"""
    if opts & OPT_A2_4X:
        v = old | 0x12
        return v if opts & OPT_A2_FILTER else v | 0x08
    v = (old & ~0x10) | 0x02
    return v & ~0x08 if opts & OPT_A2_FILTER else v | 0x08


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class Audio2ModeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stock = build_es1869drv.build(False)
        cls.fixed = build_es1869drv.build(True)
        cls.syms = build_es1869drv.symbols(True)

    def run_both(self, old, step, opts=None):
        out = []
        for data in (self.stock, self.fixed):
            d = Drv(data, self.syms if data is self.fixed else None)
            if opts is not None and data is self.fixed:
                d.opts = opts
            d.mixer[0x71] = old
            step(d)
            out.append(d)
        return out

    def test_defaults(self):
        d = Drv(self.fixed, self.syms)
        self.assertEqual(d.opts, 0)     # not oversampled, filter bypassed

    def test_wave_open(self):
        for old in (0x00, 0x10, 0x20, 0x30, 0x38):
            with self.subTest(old=hex(old)):
                stock, fixed = self.run_both(old, Drv.wave_open)
                self.assertEqual(stock.mixer[0x71], old | 0x12)
                self.assertEqual(fixed.mixer[0x71], (old & ~0x10) | 0x0A)
                # the rest as ESS's: 70h-78h to 0, then 70h and 72h FFh
                self.assertEqual(stock.writes[1:], fixed.writes[1:])
                self.assertEqual(fixed.writes[1:], [
                    (0x70, 0), (0x72, 0), (0x74, 0), (0x76, 0), (0x78, 0),
                    (0x70, 0xFF), (0x72, 0xFF)])

    def test_playback_start(self):
        for old in (0x0A, 0x1A, 0x2A, 0x3A):
            for flags, xfer in ((DEMAND, 0x93), (0, 0x13)):
                with self.subTest(old=hex(old), flags=flags):
                    stock, fixed = self.run_both(
                        old, lambda d: d.playback_start_end(flags))
                    self.assertEqual(stock.writes,
                                     [(0x71, old | 0x12), (0x78, xfer)])
                    self.assertEqual(fixed.writes, [
                        (0x71, (old & ~0x10) | 0x0A), (0x78, xfer)])

    def test_open_then_start(self):
        # what the DAC plays with: ESS's 4x oversampling, here none and
        # the filter bypassed; asynchronous and bit 5 (48 kHz) either way
        for old, want in ((0x00, 0x0A), (0x10, 0x0A), (0x32, 0x2A)):
            with self.subTest(old=hex(old)):
                stock, fixed = self.run_both(old, lambda d: (
                    d.wave_open(), d.playback_start_end()))
                self.assertEqual(stock.mixer[0x71], old | 0x12)
                self.assertEqual(fixed.mixer[0x71], want)

    def test_settings(self):
        # each (Audio2Oversampling, Audio2Filter) at both sites; with
        # both 1 the fixed driver writes what ESS's does
        for opts in (0, OPT_A2_FILTER, OPT_A2_4X, OPT_A2_4X | OPT_A2_FILTER):
            for old in (0x00, 0x08, 0x10, 0x18, 0x22, 0x3A):
                with self.subTest(opts=hex(opts), old=hex(old)):
                    stock, fixed = self.run_both(old, Drv.wave_open, opts)
                    self.assertEqual(fixed.mixer[0x71], a2_mode(old, opts))
                    stock, fixed = self.run_both(
                        old, Drv.playback_start_end, opts)
                    self.assertEqual(fixed.writes[0],
                                     (0x71, a2_mode(old, opts)))
                    if opts == OPT_A2_4X | OPT_A2_FILTER:
                        self.assertEqual(fixed.writes, stock.writes)


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class SettingsTest(unittest.TestCase):
    """[ES1869.DRV] read once at the first enable (es_read_config)"""

    @classmethod
    def setUpClass(cls):
        cls.fixed = build_es1869drv.build(True)
        cls.syms = build_es1869drv.symbols(True)

    def enable(self, ini):
        """the first DRVM_ENABLE's call at 3:4B56, ESS's read_config left
        out (its registry and CONFIGMG calls aren't emulated)"""
        d = Drv(self.fixed, self.syms)
        d.ini = ini
        d.uc.mem_write(SEG_PARA[3] * 16 + 0x3DEC, b"\xCA\x02\x00")
        d.call_far("es_read_config", DEV)
        return d

    def test_first_enable_calls_it(self):
        ne = NEFile(self.fixed)
        code = ne.segment_data(3)
        self.assertEqual(code[0x4B55:0x4B56], b"\x0E")         # push cs
        self.assertEqual(code[0x4B56], 0xE8)
        rel = struct.unpack_from("<h", code, 0x4B57)[0]
        self.assertEqual((0x4B59 + rel) & 0xFFFF,
                         self.syms["es_read_config"][1])

    def test_without_the_keys(self):
        d = self.enable({})
        self.assertEqual(d.opts, OPT_READ)
        self.assertEqual(d.ppint, [
            ("ES1869.DRV", "Audio2Oversampling", 0, "SYSTEM.INI"),
            ("ES1869.DRV", "Audio2Filter", 0, "SYSTEM.INI")])

    def test_each_key(self):
        for key, bit in (("Audio2Oversampling", OPT_A2_4X),
                         ("Audio2Filter", OPT_A2_FILTER)):
            for value, on in (("1", True), ("0", False), (" 2", True),
                              ("yes", False), ("", False)):
                with self.subTest(key=key, value=value):
                    d = self.enable({("ES1869.DRV", key): value})
                    self.assertEqual(d.opts, OPT_READ | (bit if on else 0))

    def test_other_sections_ignored(self):
        d = self.enable({("ES1869.VXD", "Audio2Filter"): "1",
                         ("ESFM.DRV", "Audio2Oversampling"): "1"})
        self.assertEqual(d.opts, OPT_READ)

    def test_read_once(self):
        d = self.enable({("ES1869.DRV", "Audio2Filter"): "1"})
        d.ini = {("ES1869.DRV", "Audio2Oversampling"): "1"}
        d.ppint = []
        d.call_far("es_read_config", DEV)       # a later first enable
        self.assertEqual(d.ppint, [])
        self.assertEqual(d.opts, OPT_READ | OPT_A2_FILTER)

    def test_ess_mode_from_system_ini(self):
        d = self.enable({("ES1869.DRV", "Audio2Oversampling"): "1",
                         ("ES1869.DRV", "Audio2Filter"): "1"})
        d.mixer[0x71] = 0x20
        d.wave_open()
        d.playback_start_end()
        self.assertEqual([v for r, v in d.writes if r == 0x71], [0x32, 0x32])


# ESS's bytes a change may replace: (segment, first, last), each an
# instruction of the same length under ES1869_FIX
HOOKS = [
    (1, 0x1158, 0x1159),    # audio2_init: 71h read through a2_mode_read
    (1, 0x115B, 0x115B),    # and or al,02h (ESS: 12h)
    (3, 0x4B57, 0x4B58),    # the first enable: es_read_config, SYSTEM.INI
    (6, 0x2DE7, 0x2DE8),    # playback start: 71h read through a2_mode_read
    (6, 0x2DEC, 0x2DEC),    # and or al,02h (ESS: 12h)
]


def sites(ne, index):
    """{offset: (type, flags, target)} of every relocation site of a
    segment, the chains followed"""
    data = ne.segment_data(index)
    out = {}
    for rtype, rflags, off, target in ne.segments[index - 1].relocs:
        pos = off
        while True:
            out[pos] = (rtype, rflags, target)
            if rflags & 4:
                break
            pos = struct.unpack_from("<H", data, pos)[0]
            if pos == 0xFFFF:
                break
    return out


def site_bytes(rtype):
    return 4 if rtype == 3 else 1 if rtype == 0 else 2


class BuildTest(unittest.TestCase):
    def test_build_is_current(self):
        with open(os.path.join(ROOT, "build", "ES1869.DRV"), "rb") as f:
            self.assertEqual(f.read(), build_es1869drv.build(True),
                             "build/ES1869.DRV is out of date: run "
                             "tools/build_es1869drv.py")

    def test_stock_is_ess_driver(self):
        self.assertEqual(build_es1869drv.build(False),
                         build_es1869drv.original())

    def test_ess_code_keeps_its_addresses(self):
        """every segment of ESS's is the same, byte for byte and relocation
        for relocation, apart from HOOKS; new code only follows ESS's"""
        stock = NEFile(build_es1869drv.build(False))
        fixed = NEFile(build_es1869drv.build(True))
        self.assertGreaterEqual(len(fixed.segments), len(stock.segments))
        for s in stock.segments:
            i = s.index
            old, new = stock.segment_data(i), fixed.segment_data(i)
            self.assertGreaterEqual(len(new), len(old), "seg%d" % i)
            os_, ns = sites(stock, i), sites(fixed, i)
            skip = set()
            for off, (rtype, _f, _t) in os_.items():
                skip.update(range(off, off + site_bytes(rtype)))
                self.assertEqual(ns.get(off), os_[off],
                                 "seg%d relocation at %04X" % (i, off))
            for seg, a, b in HOOKS:
                if seg == i:
                    skip.update(range(a, b + 1))
            diff = [o for o in range(len(old))
                    if o not in skip and old[o] != new[o]]
            self.assertEqual(diff, [], "seg%d differs at %s" % (
                i, " ".join("%04X" % o for o in diff[:8])))

    def test_new_import(self):
        fixed = NEFile(build_es1869drv.build(True))
        syms = build_es1869drv.symbols(True)
        site = syms["..@ES_I_PPINT"][1]
        self.assertEqual(sites(fixed, 3)[site], (3, 1, (1, 127)))

    def test_hooks_changed_and_clear_of_relocations(self):
        stock = NEFile(build_es1869drv.build(False))
        fixed = NEFile(build_es1869drv.build(True))
        for seg, a, b in HOOKS:
            with self.subTest(seg=seg, at="%04X" % a):
                old = stock.segment_data(seg)[a:b + 1]
                self.assertNotEqual(old, fixed.segment_data(seg)[a:b + 1])
                taken = set()
                for off, (rtype, _f, _t) in sites(stock, seg).items():
                    taken.update(range(off, off + site_bytes(rtype)))
                self.assertFalse(taken & set(range(a, b + 1)))


if __name__ == "__main__":
    unittest.main()
