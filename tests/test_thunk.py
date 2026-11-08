# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Run essctl's VxD call thunks (src/win/vxdcall.asm) in a 16-bit CPU emulator.

tests/host/thunkhar.asm calls vxd_raw_call with a fake API entry point and
vxd_get_entry with an emulated INT 2Fh. The tests check the registers the
fake entry point got, the values stored back, the carry flag result and
that every other register survived, upper halves included.

Needs Open Watcom (wasm and wlink, in $OW2 or $WATCOM) and unicorn.
"""

import os
import struct
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX,
                                   UC_X86_REG_CS, UC_X86_REG_DI,
                                   UC_X86_REG_DS, UC_X86_REG_ES,
                                   UC_X86_REG_SP, UC_X86_REG_SS)
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

LOAD_SEG = 0x1000


def find_watcom():
    for var in ("OW2", "WATCOM"):
        path = os.environ.get(var)
        if path and os.path.exists(os.path.join(path, "binl64", "wasm")):
            return path
    return None


def load_mz(data):
    """Load an MZ executable at LOAD_SEG, return (image, cs, ip, ss, sp)."""
    (magic, cblp, cp, crlc, cparhdr, _min, _max, ss, sp, _csum, ip, cs,
     lfarlc) = struct.unpack_from("<2s12H", data)
    assert magic == b"MZ"
    size = cp * 512 - (512 - cblp if cblp else 0)
    image = bytearray(data[cparhdr * 16:size])
    for i in range(crlc):
        off, seg = struct.unpack_from("<HH", data, lfarlc + 4 * i)
        at = seg * 16 + off
        word = struct.unpack_from("<H", image, at)[0]
        struct.pack_into("<H", image, at, (word + LOAD_SEG) & 0xFFFF)
    return image, cs + LOAD_SEG, ip, ss + LOAD_SEG, sp


def symbol(mapfile, name):
    with open(mapfile) as f:
        lines = f.read().splitlines()
    for line in lines:
        parts = line.split()
        if len(parts) >= 2 and parts[-1] == name:
            seg, off = parts[0].rstrip("*+").split(":")
            return (int(seg, 16) + LOAD_SEG) * 16 + int(off, 16)
    raise KeyError(name)


@unittest.skipUnless(HAVE_UNICORN and find_watcom(),
                     "needs unicorn and Open Watcom ($OW2)")
class ThunkTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ow = find_watcom()
        env = dict(os.environ, WATCOM=ow, INCLUDE=os.path.join(ow, "h"),
                   PATH=os.path.join(ow, "binl64") + ":" + os.environ["PATH"])
        cls.tmpdir = tempfile.TemporaryDirectory()
        tmp = cls.tmpdir.name
        src = [os.path.join(ROOT, "src", "win", "vxdcall.asm"),
               os.path.join(ROOT, "tests", "host", "thunkhar.asm")]
        for path in src:
            obj = os.path.join(tmp, os.path.basename(path)[:-4] + ".obj")
            subprocess.run(["wasm", "-q", "-fo=" + obj, path], env=env,
                           check=True, capture_output=True)
        subprocess.run(["wlink", "option", "quiet", "system", "dos", "file",
                        "thunkhar.obj,vxdcall.obj", "name", "thunk.exe",
                        "option", "map=thunk.map"],
                       env=env, cwd=tmp, check=True, capture_output=True)
        with open(os.path.join(tmp, "thunk.exe"), "rb") as f:
            image, cs, ip, ss, sp = load_mz(f.read())

        uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x100000)
        uc.mem_write(LOAD_SEG * 16, bytes(image))
        cls.int2f = []

        def intr(uc, intno, _user):
            if intno == 3:
                uc.emu_stop()
            elif intno == 0x2F:
                cls.int2f.append((uc.reg_read(UC_X86_REG_AX),
                                  uc.reg_read(UC_X86_REG_BX)))
                uc.reg_write(UC_X86_REG_ES, 0x4321)
                uc.reg_write(UC_X86_REG_DI, 0x8765)
            else:
                raise RuntimeError("interrupt %#x" % intno)

        uc.hook_add(UC_HOOK_INTR, intr)
        for reg, val in ((UC_X86_REG_CS, cs), (UC_X86_REG_SS, ss),
                         (UC_X86_REG_SP, sp), (UC_X86_REG_DS, LOAD_SEG - 0x10),
                         (UC_X86_REG_ES, LOAD_SEG - 0x10)):
            uc.reg_write(reg, val)
        uc.emu_start(cs * 16 + ip, 0, count=10000)

        mapfile = os.path.join(tmp, "thunk.map")
        cls.dgroup = symbol(mapfile, "regs") >> 4
        cls.regs = struct.unpack(
            "<6I2H", uc.mem_read(symbol(mapfile, "regs"), 28))
        seen = uc.mem_read(symbol(mapfile, "seen"), 56)
        cls.seen = [struct.unpack_from("<6I2H", seen, 28 * i)
                    for i in range(2)]
        cls.res = struct.unpack(
            "<5I4H3I", uc.mem_read(symbol(mapfile, "results"), 40))

    @classmethod
    def tearDownClass(cls):
        cls.tmpdir.cleanup()

    def test_inputs_reach_the_api(self):
        dg = self.dgroup
        self.assertEqual(self.seen[0], (0x11111111, 0x22222222, 0x33333333,
                                        0x44444444, 0x55555555, 0x66666666,
                                        dg, dg))
        # the second call passes the values the first one stored back
        self.assertEqual(self.seen[1], (0xA1A1A1A1, 0xB2B2B2B2, 0xC3C3C3C3,
                                        0x0404, 0xE5E5E5E5, 0xF6F6F6F6,
                                        dg, dg))

    def test_outputs_stored(self):
        eax, ebx, ecx, edx, esi, edi, es, flags = self.regs
        self.assertEqual((eax, ebx, ecx, edx, esi, edi),
                         (0xA1A1A1A1, 0xB2B2B2B2, 0xC3C3C3C3, 0xD4D4D4D4,
                          0xE5E5E5E5, 0xF6F6F6F6))
        self.assertEqual(es, self.dgroup)
        self.assertEqual(flags & 1, 1)  # second call returned with carry

    def test_carry_is_the_return_value(self):
        self.assertEqual(self.res[0], 0)       # first call: carry clear
        self.assertEqual(self.res[10], 1)      # second call: carry set

    def test_registers_preserved(self):
        (_ret, ebp, esi, edi, ebx, fs, gs, ds, spdiff, entry, _ret2,
         ecx) = self.res
        self.assertEqual(ebp & 0xFFFF0000, 0x77770000)
        self.assertEqual(esi, 0x5A5A0000)
        self.assertEqual(edi, 0xA5A50000)
        self.assertEqual(ebx, 0x3C3C0000)
        self.assertEqual(ecx, 0x9C9C0000)
        self.assertEqual((fs, gs), (0x1111, 0x2222))
        self.assertEqual(ds, self.dgroup)
        self.assertEqual(spdiff, 0)

    def test_get_entry(self):
        self.assertEqual(self.int2f, [(0x1684, 0x3B07)])
        self.assertEqual(self.res[9], 0x43218765)


if __name__ == "__main__":
    unittest.main()
