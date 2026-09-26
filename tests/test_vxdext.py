# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the essreg register API (V86/PM API group 4) of the rebuilt
ES1869.VXD in a CPU emulator against a simulated ES1869.

Run: python3 -m unittest tests.test_vxdext   (needs nasm and unicorn)
"""

import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    import unicorn  # noqa: F401
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False


def have_nasm():
    try:
        subprocess.run(["nasm", "-v"], capture_output=True, check=True)
        return True
    except (OSError, subprocess.CalledProcessError):
        return False


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class ExtensionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import build_vxd
        from retools import elf32
        from retools.le import LEFile
        cls.tmp = tempfile.TemporaryDirectory()
        builds = {}
        for ext in (True, False):
            out = os.path.join(cls.tmp.name, "ext.vxd" if ext else "stock.vxd")
            data, _o, _f = build_vxd.build(ext, out, cls.tmp.name)
            elf = elf32.read_elf(os.path.join(
                cls.tmp.name, "es1869-%s.o" % ("ext" if ext else "stock")))
            builds[ext] = (LEFile(data), elf)
        cls.builds = builds

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def emu(self, ext=True, dsp_owner=0, fm_owner=0, mpu_owner=0):
        import vxdemu
        le, elf = self.builds[ext]
        secobj = {}
        order = [o.name_str for o in le.objects]
        for s in elf.sections:
            if s.name in order:
                secobj[s.index] = order.index(s.name) + 1
        syms = {}
        for sym in elf.symbols:
            if sym.name and sym.shndx in secobj:
                syms[sym.name] = vxdemu.obj_base(secobj[sym.shndx]) + sym.value
        hw = vxdemu.FakeES1869()
        emu = vxdemu.VxDEmu(le, hw)
        # one device instance in ADI_List; no config-device list
        emu.write32(syms["ADI_List"], 0x5000)
        emu.set_list(0x5000, [vxdemu.LISTS + 0x100])
        emu.write32(vxdemu.LISTS + 0x100, vxdemu.ADI)
        adi = bytearray(0xE9)
        adi[0x06:0x08] = (0x220).to_bytes(2, "little")
        adi[0x08:0x0A] = (0x388).to_bytes(2, "little")
        adi[0x0C:0x0E] = (0x330).to_bytes(2, "little")
        adi[0x04:0x06] = (0x0100).to_bytes(2, "little")
        for off, v in ((0x35, dsp_owner), (0x3D, fm_owner), (0x45, mpu_owner),
                       (0x55, vxdemu.DEVNODE)):
            adi[off:off + 4] = v.to_bytes(4, "little")
        emu.uc.mem_write(vxdemu.ADI, bytes(adi))
        emu.api = syms["AUDDRV_API_Proc"]
        return emu, hw

    def api(self, emu, fn, **regs):
        import vxdemu
        client = {"EDX": fn, "ECX": vxdemu.DEVNODE}
        client.update(regs)
        out = emu.call(emu.api, client)
        return out, bool(out["EFlags"] & 1)

    # -- tests --------------------------------------------------------------

    def test_stock_rejects_group4_and_keeps_group0(self):
        emu, _hw = self.emu(ext=False)
        out, cf = self.api(emu, 0x0400)
        self.assertTrue(cf)
        out, cf = self.api(emu, 0x0000)
        self.assertFalse(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 0x0404)

    def test_info(self):
        emu, _hw = self.emu()
        out, cf = self.api(emu, 0x0400)
        self.assertFalse(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 0x0100)
        self.assertEqual(out["EBX"] & 0xFFFF, 0x007F)
        self.assertEqual(out["EDX"] & 0xFFFF, 13)
        out, cf = self.api(emu, 0x0000)          # stock functions still work
        self.assertEqual((cf, out["EAX"] & 0xFFFF), (False, 0x0404))
        _out, cf = self.api(emu, 0x040D)         # past the end of group 4
        self.assertTrue(cf)
        _out, cf = self.api(emu, 0x0500)         # no group 5
        self.assertTrue(cf)

    def test_mixer_read_write_restore_index(self):
        emu, hw = self.emu()
        hw.mixer[0x52] = 0x2A
        hw.index = 0x36                          # someone else's selection
        out, cf = self.api(emu, 0x0401, EBX=0x52)
        self.assertFalse(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 0x2A)
        self.assertEqual(hw.index, 0x36)
        _out, cf = self.api(emu, 0x0402, EBX=0x0C50)   # 50h <- 0Ch
        self.assertFalse(cf)
        self.assertEqual(hw.mixer[0x50], 0x0C)
        self.assertEqual(hw.index, 0x36)

    def test_bad_devnode(self):
        emu, _hw = self.emu()
        out, cf = self.api(emu, 0x0401, EBX=0x52, ECX=0x1234)
        self.assertTrue(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 1)

    def test_controller_read_write(self):
        emu, hw = self.emu()
        hw.ctrl[0xBA] = 0x13
        out, cf = self.api(emu, 0x0403, EBX=0xBA)
        self.assertFalse(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 0x13)
        dsp_writes = [v for op, p, v in hw.log if op == "out" and p == 0x22C]
        self.assertEqual(dsp_writes, [0xC6, 0xC0, 0xBA])
        self.assertFalse(any(op == "in" and p == 0x22E for op, p, v in hw.log),
                         "must not read Audio_Base+Eh (clears the IRQ)")
        _out, cf = self.api(emu, 0x0404, EBX=0x05BB)   # BBh <- 05h
        self.assertFalse(cf)
        self.assertEqual(hw.ctrl[0xBB], 0x05)
        out, cf = self.api(emu, 0x0403, EBX=0x40)
        self.assertTrue(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 3)

    def test_controller_timeout(self):
        emu, hw = self.emu()
        hw.ext_mode = False
        hw._dsp = lambda v: None               # DSP never answers
        out, cf = self.api(emu, 0x0403, EBX=0xA1)
        self.assertTrue(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 5)

    def test_controller_refused_when_other_vm_owns_dsp(self):
        import vxdemu
        emu, hw = self.emu(dsp_owner=vxdemu.VM_DOS)
        out, cf = self.api(emu, 0x0403, EBX=0xBA)
        self.assertTrue(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 2)
        self.assertFalse(any(p == 0x22C for op, p, v in hw.log))
        hw.mixer[0x7D] = 0x08                   # mixer access still works
        out, cf = self.api(emu, 0x0401, EBX=0x7D)
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x08))

    def test_controller_allowed_for_own_vm(self):
        import vxdemu
        emu, hw = self.emu(dsp_owner=vxdemu.VM_SYS)
        hw.ctrl[0xB4] = 0x88
        out, cf = self.api(emu, 0x0403, EBX=0xB4)
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x88))

    def test_audio_ports(self):
        emu, hw = self.emu()
        hw.ports[7] = 0x0B
        out, cf = self.api(emu, 0x0405, EBX=0x07)
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x0B))
        _out, cf = self.api(emu, 0x0406, EBX=0x0807)
        self.assertEqual((cf, hw.ports[7]), (False, 0x08))
        out, cf = self.api(emu, 0x0405, EBX=0x10)
        self.assertEqual((cf, out["EAX"] & 0xFFFF), (True, 3))

    def test_config_ports_via_id_sequence(self):
        emu, hw = self.emu()
        hw.cfg_ports[6] = 0x21
        out, cf = self.api(emu, 0x0407, EBX=0x06)
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x21))
        _out, cf = self.api(emu, 0x0408, EBX=0x5507)
        self.assertEqual((cf, hw.cfg_ports[7]), (False, 0x55))

    def test_pnp_registers_restore_index_and_ldn(self):
        emu, hw = self.emu()
        hw.pnp[(1, 0x60)] = 0x02
        hw.pnp[(None, 0x22)] = 0x44
        hw.cfg_index, hw.ldn = 0x25, 3
        out, cf = self.api(emu, 0x0409, EBX=0x6001)    # LDN 1, reg 60h
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x02))
        self.assertEqual((hw.cfg_index, hw.ldn), (0x25, 3))
        out, cf = self.api(emu, 0x0409, EBX=0x22FF)    # card level 22h
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x44))
        _out, cf = self.api(emu, 0x040A, EBX=0x7001, EAX=0x05)
        self.assertFalse(cf)
        self.assertEqual(hw.pnp[(1, 0x70)], 0x05)
        self.assertEqual((hw.cfg_index, hw.ldn), (0x25, 3))

    def test_mixer_block(self):
        import vxdemu
        emu, hw = self.emu()
        for r in range(256):
            hw.mixer[r] = (r * 7) & 0xFF
        hw.index = 0x14
        _out, cf = self.api(emu, 0x040B, ES=0x1234, EDI=0x10)
        self.assertFalse(cf)
        buf = bytes(emu.uc.mem_read(vxdemu.BUFFER + 0x10, 0x80))
        want = bytes(0 if r == 0x40 else (r * 7) & 0xFF for r in range(0x80))
        self.assertEqual(buf, want)
        self.assertEqual(hw.index, 0x14)
        _out, cf = self.api(emu, 0x040B, ES=0x9999, EDI=0)  # unmappable
        self.assertTrue(cf)

    def test_owners(self):
        import vxdemu
        emu, _hw = self.emu(dsp_owner=vxdemu.VM_SYS, fm_owner=0,
                            mpu_owner=vxdemu.VM_DOS)
        out, cf = self.api(emu, 0x040C)
        self.assertFalse(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 0x0001)   # DSP mine, FM none
        self.assertEqual(out["EBX"] & 0xFF, 2)          # MPU another VM
        self.assertEqual(out["EDX"] & 0xFFFF, 0x0100)   # ADI flags


if __name__ == "__main__":
    unittest.main()
