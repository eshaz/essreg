# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Test build/ES1869.DRV, ESS's driver with the Audio 2 DAC not
oversampled and its filter bypassed (tools/build_es1869drv.py).

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


class BuildTest(unittest.TestCase):
    def test_build_is_current(self):
        with open(os.path.join(ROOT, "build", "ES1869.DRV"), "rb") as f:
            self.assertEqual(f.read(), build_es1869drv.build(True),
                             "build/ES1869.DRV is out of date: run "
                             "tools/build_es1869drv.py")

    def test_stock_is_ess_driver(self):
        self.assertEqual(build_es1869drv.build(False),
                         build_es1869drv.original())

    def test_only_the_two_operands(self):
        stock = build_es1869drv.build(False)
        fixed = build_es1869drv.build(True)
        self.assertEqual(len(stock), len(fixed))
        changed = [i for i in range(len(stock)) if stock[i] != fixed[i]]
        want = []
        for at, (_s, _o, old, new, _w) in zip(
                build_es1869drv.file_offsets(stock),
                build_es1869drv.PATCHES):
            want += [at + k for k in range(len(old)) if old[k] != new[k]]
        self.assertEqual(changed, want)

    def test_no_relocation_in_the_way(self):
        ne = NEFile(build_es1869drv.original())
        for seg, off, old, _n, _w in build_es1869drv.PATCHES:
            s = ne.segments[seg - 1]
            data = ne.segment_data(seg)
            sites = set()
            for rtype, rflags, roff, _target in s.relocs:
                size = 4 if rtype == 3 else 2   # far pointer, or a word
                pos = roff
                while True:
                    sites.update(range(pos, pos + size))
                    if rflags & 4:
                        break
                    pos = struct.unpack_from("<H", data, pos)[0]
                    if pos == 0xFFFF:
                        break
            self.assertFalse(sites & set(range(off, off + len(old))),
                             "%d:%04X" % (seg, off))

    def test_other_driver_refused(self):
        data = bytearray(build_es1869drv.original())
        data[-1] ^= 1
        with self.assertRaises(SystemExit):
            build_es1869drv.build(True, bytes(data))


if __name__ == "__main__":
    unittest.main()
