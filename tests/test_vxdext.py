# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Run the essreg register API (V86/PM API group 4) of the rebuilt
ES1869.VXD in a CPU emulator against a simulated ES1869.

usage: python3 -m unittest tests.test_vxdext   (needs nasm and unicorn)
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


class VxDBuilds:
    """the extended and the stock ES1869.VXD, built once per class"""

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

    def machine(self, ext=True):
        import vxdemu
        le, elf = self.builds[ext]
        return vxdemu.Machine(le, elf, ext)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class ExtensionTest(VxDBuilds, unittest.TestCase):
    def emu(self, ext=True, dsp_owner=0, fm_owner=0, mpu_owner=0):
        import vxdemu
        m = self.machine(ext)
        m.set_owner(vxdemu.ADI_DSP_OWNER, dsp_owner)
        m.set_owner(vxdemu.ADI_FM_OWNER, fm_owner)
        m.set_owner(vxdemu.ADI_MPU_OWNER, mpu_owner)
        m.emu.uc.mem_write(vxdemu.ADI + vxdemu.ADI_FLAGS, b"\x00\x01")
        return m, m.hw

    def api(self, m, fn, vm=None, **regs):
        import vxdemu
        return m.api(vm or vxdemu.VM_SYS, fn, **regs)

    # -- tests --------------------------------------------------------------

    def test_stock_rejects_group4_and_keeps_group0(self):
        m, _hw = self.emu(ext=False)
        out, cf = self.api(m, 0x0400)
        self.assertTrue(cf)
        out, cf = self.api(m, 0x0000)
        self.assertFalse(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 0x0404)

    def test_info(self):
        m, _hw = self.emu()
        out, cf = self.api(m, 0x0400)
        self.assertFalse(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 0x0110)
        self.assertEqual(out["EBX"] & 0xFFFF, 0x01FF)
        self.assertEqual(out["EDX"] & 0xFFFF, 13)
        out, cf = self.api(m, 0x0000)            # stock functions still work
        self.assertEqual((cf, out["EAX"] & 0xFFFF), (False, 0x0404))
        _out, cf = self.api(m, 0x040D)           # past the end of group 4
        self.assertTrue(cf)
        _out, cf = self.api(m, 0x0500)           # no group 5
        self.assertTrue(cf)

    def test_mixer_read_write_restore_index(self):
        m, hw = self.emu()
        hw.mixer[0x52] = 0x2A
        hw.index = 0x36                          # someone else's selection
        out, cf = self.api(m, 0x0401, EBX=0x52)
        self.assertFalse(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 0x2A)
        self.assertEqual(hw.index, 0x36)
        _out, cf = self.api(m, 0x0402, EBX=0x0C50)   # 50h <- 0Ch
        self.assertFalse(cf)
        self.assertEqual(hw.mixer[0x50], 0x0C)
        self.assertEqual(hw.index, 0x36)

    def test_bad_devnode(self):
        m, _hw = self.emu()
        out, cf = self.api(m, 0x0401, EBX=0x52, ECX=0x1234)
        self.assertTrue(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 1)

    def test_controller_read_write(self):
        m, hw = self.emu()
        hw.ctrl[0xBA] = 0x13
        out, cf = self.api(m, 0x0403, EBX=0xBA)
        self.assertFalse(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 0x13)
        # C7h at the end: nobody owns the DSP, so it's left as after a reset
        self.assertEqual(hw.dsp_writes(), [0xC6, 0xC0, 0xBA, 0xC7])
        self.assertFalse(hw.ext_mode)
        self.assertFalse(any(op == "in" and p == 0x22E for op, p, v in hw.log),
                         "must not read Audio_Base+Eh (clears the IRQ)")
        _out, cf = self.api(m, 0x0404, EBX=0x05BB)   # BBh <- 05h
        self.assertFalse(cf)
        self.assertEqual(hw.ctrl[0xBB], 0x05)
        out, cf = self.api(m, 0x0403, EBX=0x40)
        self.assertTrue(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 3)

    def test_controller_timeout(self):
        m, hw = self.emu()
        hw._dsp = lambda v: None               # DSP never answers
        out, cf = self.api(m, 0x0403, EBX=0xA1)
        self.assertTrue(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 5)

    def test_controller_refused_when_other_vm_owns_dsp(self):
        import vxdemu
        m, hw = self.emu(dsp_owner=vxdemu.VM_DOS)
        out, cf = self.api(m, 0x0403, EBX=0xBA)
        self.assertTrue(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 2)
        self.assertFalse(any(p == 0x22C for op, p, v in hw.log))
        hw.mixer[0x7D] = 0x08                   # mixer access still works
        out, cf = self.api(m, 0x0401, EBX=0x7D)
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x08))

    def test_controller_allowed_for_own_vm(self):
        import vxdemu
        m, hw = self.emu(dsp_owner=vxdemu.VM_SYS)
        hw.ctrl[0xB4] = 0x88
        hw.ext_mode = True                      # ES1869.DRV's
        out, cf = self.api(m, 0x0403, EBX=0xB4)
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x88))
        # the owner uses Extended mode: no C7h
        self.assertEqual(hw.dsp_writes(), [0xC6, 0xC0, 0xB4])

    def test_controller_powered_down_fails_at_once(self):
        import vxdemu
        m, hw = self.emu()
        m.emu.uc.mem_write(vxdemu.ADI + vxdemu.ADI_FLAGS, b"\x80\x01")
        out, cf = self.api(m, 0x0403, EBX=0xB4)
        self.assertEqual((cf, out["EAX"] & 0xFFFF), (True, 4))
        self.assertEqual(hw.log, [])

    def test_controller_busy_dsp(self):
        m, hw = self.emu()
        hw.busy = True
        out, cf = self.api(m, 0x0404, EBX=0x05BB)
        self.assertEqual((cf, out["EAX"] & 0xFFFF), (True, 4))
        self.assertEqual(hw.dsp_writes(), [])

    def test_controller_drops_a_stale_byte(self):
        m, hw = self.emu()
        hw.ctrl[0xA1] = 0x55
        hw.out = [0x99]                         # an answer nobody read
        out, cf = self.api(m, 0x0403, EBX=0xA1)
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x55))

    def test_audio_ports(self):
        m, hw = self.emu()
        hw.ports[7] = 0x0B
        out, cf = self.api(m, 0x0405, EBX=0x07)
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x0B))
        _out, cf = self.api(m, 0x0406, EBX=0x0807)
        self.assertEqual((cf, hw.ports[7]), (False, 0x08))
        out, cf = self.api(m, 0x0405, EBX=0x10)
        self.assertEqual((cf, out["EAX"] & 0xFFFF), (True, 3))

    def test_audio_ports_of_another_vms_dsp(self):
        import vxdemu
        m, hw = self.emu(dsp_owner=vxdemu.VM_DOS)
        for off in (0x0A, 0x0E, 0x0F):          # its data and interrupt
            out, cf = self.api(m, 0x0405, EBX=off)
            self.assertEqual((cf, out["EAX"] & 0xFFFF), (True, 2), off)
        for off in (0x06, 0x0C, 0x0F):          # its reset and commands
            out, cf = self.api(m, 0x0406, EBX=off | 0x0100)
            self.assertEqual((cf, out["EAX"] & 0xFFFF), (True, 2), off)
        self.assertEqual(hw.log, [])
        out, cf = self.api(m, 0x0405, EBX=0x0C)   # its status can be read
        self.assertFalse(cf)
        _out, cf = self.api(m, 0x0406, EBX=0x0807)
        self.assertFalse(cf)

    def test_fm_ports_of_another_vm(self):
        import vxdemu
        m, hw = self.emu(fm_owner=vxdemu.VM_DOS)
        out, cf = self.api(m, 0x0406, EBX=0x2000)
        self.assertEqual((cf, out["EAX"] & 0xFFFF), (True, 2))
        self.assertEqual(hw.fm.writes, [])
        out, cf = self.api(m, 0x0405, EBX=0x00)   # the status can be read
        self.assertFalse(cf)
        m.set_owner(vxdemu.ADI_FM_OWNER, 0)
        m.set_owner(vxdemu.ADI_FM_LAST, vxdemu.VM_DOS)
        _out, cf = self.api(m, 0x0406, EBX=0x2000)
        self.assertFalse(cf)
        # the chip changed: a returning DOS VM gets it reset
        self.assertEqual(m.adi32(vxdemu.ADI_FM_LAST), 0xFFFFFFFF)

    def test_config_ports_via_id_sequence(self):
        m, hw = self.emu()
        hw.cfg_ports[6] = 0x21
        hw.index = 0x14
        out, cf = self.api(m, 0x0407, EBX=0x06)
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x21))
        self.assertEqual(hw.index, 0x14)        # the mixer index is back
        _out, cf = self.api(m, 0x0408, EBX=0x5507)
        self.assertEqual((cf, hw.cfg_ports[7]), (False, 0x55))

    def test_config_eeprom_ports_never_written(self):
        m, hw = self.emu()
        for off in (2, 3, 4):
            out, cf = self.api(m, 0x0408, EBX=0x0100 | off)
            self.assertEqual((cf, out["EAX"] & 0xFFFF), (True, 3))
        self.assertFalse(any(op == "out" and 0x252 <= p <= 0x254
                             for op, p, v in hw.log))
        out, cf = self.api(m, 0x0407, EBX=0x03)   # reads are fine
        self.assertFalse(cf)

    def test_config_port_is_checked(self):
        m, hw = self.emu()
        hw.id_cfg = 0x1869                      # nonsense from the sequence
        out, cf = self.api(m, 0x0407, EBX=0x06)
        self.assertEqual((cf, out["EAX"] & 0xFFFF), (True, 6))
        hw.id_cfg = 0x0253                      # not a multiple of 8
        out, cf = self.api(m, 0x0407, EBX=0x06)
        self.assertEqual((cf, out["EAX"] & 0xFFFF), (True, 6))
        hw.id_cfg = 0x0250
        hw.pnp[(1, 0x61)] = 0x40                # another chip's (240h)
        out, cf = self.api(m, 0x0407, EBX=0x06)
        self.assertEqual((cf, out["EAX"] & 0xFFFF), (True, 6))
        hw.pnp[(1, 0x61)] = 0x20                # checked again every time
        out, cf = self.api(m, 0x0407, EBX=0x06)
        self.assertFalse(cf)

    def test_pnp_registers_restore_index_and_ldn(self):
        m, hw = self.emu()
        hw.pnp[(None, 0x22)] = 0x44
        hw.cfg_index, hw.ldn = 0x25, 3
        out, cf = self.api(m, 0x0409, EBX=0x6001)    # LDN 1, reg 60h
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x02))
        self.assertEqual((hw.cfg_index, hw.ldn), (0x25, 3))
        out, cf = self.api(m, 0x0409, EBX=0x22FF)    # card level 22h
        self.assertEqual((cf, out["EAX"] & 0xFF), (False, 0x44))
        _out, cf = self.api(m, 0x040A, EBX=0x7001, EAX=0x05)
        self.assertFalse(cf)
        self.assertEqual(hw.pnp[(1, 0x70)], 0x05)
        self.assertEqual((hw.cfg_index, hw.ldn), (0x25, 3))

    def test_mixer_block(self):
        import vxdemu
        m, hw = self.emu()
        for r in range(256):
            hw.mixer[r] = (r * 7) & 0xFF
        hw.index = 0x14
        _out, cf = self.api(m, 0x040B, ES=0x1234, EDI=0x10)
        self.assertFalse(cf)
        buf = bytes(m.emu.uc.mem_read(vxdemu.BUFFER + 0x10, 0x80))
        want = bytes(0 if r == 0x40 else (r * 7) & 0xFF for r in range(0x80))
        self.assertEqual(buf, want)
        self.assertEqual(hw.index, 0x14)
        _out, cf = self.api(m, 0x040B, ES=0x9999, EDI=0)  # unmappable
        self.assertTrue(cf)

    def test_mixer_block_buffer_checks(self):
        import vxdemu
        m, _hw = self.emu()
        e = m.emu
        e.add_selector(0x2237, vxdemu.BUFFER, 0x17F)            # 384 bytes
        e.add_selector(0x223F, vxdemu.BUFFER, 0xFFFF, 0xF0)     # read-only
        e.add_selector(0x2247, vxdemu.BUFFER, 0xFFFF, 0xF6)     # expand-down
        e.add_selector(0x224F, vxdemu.BUFFER, 0xFFFF, 0x72)     # not present

        def block(es, di, eflags=0):
            out, cf = self.api(m, 0x040B, ES=es, EDI=di, EFlags=eflags)
            return cf, out["EAX"] & 0xFFFF
        self.assertEqual(block(0x2237, 0x100), (False, 0))
        self.assertEqual(block(0x2237, 0x101), (True, 3))    # past the limit
        for sel in (0x223F, 0x2247, 0x224F, 0x0000, 0x0003):
            self.assertEqual(block(sel, 0), (True, 3), hex(sel))
        # Map_Flat uses only DI for a 16-bit program: EDI's high word
        # doesn't matter, what Map_Flat used is checked
        self.assertEqual(block(0x2237, 0xABCD0100), (False, 0))
        e.use32.add(vxdemu.VM_SYS)
        self.assertEqual(block(0x2237, 0xABCD0100), (True, 3))
        e.use32.discard(vxdemu.VM_SYS)
        # V86 mode: 64K segments, DI up to FF80h
        self.assertEqual(block(0x1000, 0xFF80, 0x20000), (False, 0))
        self.assertEqual(block(0xFFFF, 0xFF81, 0x20000), (True, 3))

    def test_owners(self):
        import vxdemu
        m, _hw = self.emu(dsp_owner=vxdemu.VM_SYS, fm_owner=0,
                          mpu_owner=vxdemu.VM_DOS)
        out, cf = self.api(m, 0x040C)
        self.assertFalse(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 0x0001)   # DSP mine, FM none
        self.assertEqual(out["EBX"] & 0xFF, 2)          # MPU another VM
        self.assertEqual(out["EDX"] & 0xFFFF, 0x0100)   # ADI flags


if __name__ == "__main__":
    unittest.main()
