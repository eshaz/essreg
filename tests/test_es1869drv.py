# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Test build/ES1869.DRV, ESS's driver rebuilt from src/es1869
(tools/build_es1869drv.py): the Audio 2 DAC not oversampled and its filter
bypassed, and ESS's code at its addresses.

The two changed instructions run in a CPU emulator inside ESS's own code,
for ESS's driver and the changed one: the wave-out open (1:1148) and the
end of a playback start (6:2DDC), against a simulated mixer.
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
                                   UC_X86_REG_EFLAGS, UC_X86_INS_IN,
                                   UC_X86_INS_OUT)
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

SEG_PARA = {i: 0x1000 * i for i in range(1, 8)}
STACK_PARA = 0x8000
STUB_PARA = 0x9000          # imports and the return address: int 3
AUDIO_BASE = 0x220
DEV = 0x4000                # a device structure in the data segment
DEMAND = 0x80               # dev+2Bh bit 7, demand transfers


class Drv:
    """ES1869.DRV's segments at fixed paragraphs, its relocations applied,
    and the mixer at AUDIO_BASE+4/+5"""

    def __init__(self, data):
        self.ne = ne = NEFile(data)
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x100000)
        for s in ne.segments:
            uc.mem_write(SEG_PARA[s.index] * 16, ne.segment_data(s.index))
        uc.mem_write(STUB_PARA * 16, b"\xCC")
        self._relocate()
        self.mixer = bytearray(256)
        self.index = 0
        self.writes = []
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INTR, self._intr)
        self.w16(7, DEV, AUDIO_BASE)
        self.stopped = False

    def _intr(self, uc, intno, _user):
        self.stopped = uc.reg_read(UC_X86_REG_CS) == STUB_PARA
        uc.emu_stop()

    def _relocate(self):
        for s in self.ne.segments:
            base = SEG_PARA[s.index] * 16
            for rtype, rflags, off, target in s.relocs:
                if rflags & 3 == 0 and target[0] & 0xFF != 0xFF:
                    seg, toff = SEG_PARA[target[0] & 0xFF], target[1]
                else:
                    seg, toff = STUB_PARA, 0    # an import: never called
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
        self.stopped = False
        uc.emu_start(SEG_PARA[seg] * 16 + ip, 0, count=100000)
        if not self.stopped:
            raise RuntimeError("didn't return")

    def _stack(self, words):
        sp = 0xFF00 - 2 * len(words)
        self.uc.mem_write(STACK_PARA * 16 + sp,
                          b"".join(struct.pack("<H", w) for w in words))
        return sp

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


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class Audio2ModeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stock = build_es1869drv.build(False)
        cls.fixed = build_es1869drv.build(True)

    def run_both(self, old, step):
        out = []
        for data in (self.stock, self.fixed):
            d = Drv(data)
            d.mixer[0x71] = old
            step(d)
            out.append(d)
        return out

    def test_wave_open(self):
        for old in (0x00, 0x10, 0x20, 0x30):
            with self.subTest(old=hex(old)):
                stock, fixed = self.run_both(old, Drv.wave_open)
                self.assertEqual(stock.mixer[0x71], old | 0x12)
                self.assertEqual(fixed.mixer[0x71], old | 0x0A)
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
                    self.assertEqual(fixed.writes,
                                     [(0x71, old & 0xEF), (0x78, xfer)])

    def test_open_then_start(self):
        # what the DAC plays with: ESS's 4x oversampling, here none and
        # the filter bypassed; asynchronous and bit 5 (48 kHz) either way
        for old, want in ((0x00, 0x0A), (0x10, 0x0A), (0x32, 0x2A)):
            with self.subTest(old=hex(old)):
                stock, fixed = self.run_both(old, lambda d: (
                    d.wave_open(), d.playback_start_end()))
                self.assertEqual(stock.mixer[0x71], old | 0x12)
                self.assertEqual(fixed.mixer[0x71], want)


# ESS's bytes a change may replace: (segment, first, last), each an
# instruction of the same length under ES1869_FIX
HOOKS = [
    (1, 0x115A, 0x115B),    # audio2_init: or al,0Ah (ESS: or al,12h)
    (6, 0x2DEB, 0x2DEC),    # playback start: and al,0EFh (ESS: or al,12h)
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
