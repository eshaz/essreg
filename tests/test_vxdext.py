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


# where the extension changes ESS's code: (object, first byte, last byte),
# each an instruction of the same length or a constant (CLAUDE.md)
HOOKS = [
    (1, 0x03EE, 0x03F1),    # VM_Not_Executeable: ESSREG_VM_Not_Executeable
    (1, 0x0409, 0x040C),    # Sys_Dynamic_Device_Exit: ESSREG_Dynamic_Exit
    (1, 0x0511, 0x0516),    # the DOSMGR hook's last jump: ESSREG_App_End
    (4, 0x00AC, 0x00AC),    # the per-VM node's size
    (4, 0x0236, 0x0237),    # the ADI's size
    (4, 0x09C2, 0x09C5),    # a node removed: ESSREG_Node_Remove
    (5, 0x100E, 0x100E),    # the per-VM node's size
    (5, 0x10B2, 0x10B5),    # Release_Resources, FM: ESSREG_FM_Released
    (5, 0x110C, 0x110F),    # Release_Resources, DSP: ESSREG_DSP_Restore
    (5, 0x122A, 0x122D),    # Acquire_Resources, FM: ESSREG_FM_Reset
    (5, 0x127A, 0x127D),    # Acquire_Resources, DSP: ESSREG_DSP_Save
    (5, 0x1362, 0x1362),    # the API's groups: 0-4
    (5, 0x136D, 0x1370),    # and their table
    (7, 0x0043, 0x0043),    # the per-VM node's size
] + [(6, off, off + 3) for off in (     # the FM ports' trap handler
    0x02D6, 0x02DC, 0x02E2, 0x02E8, 0x0306, 0x030C,
    0x0338, 0x033E, 0x0344, 0x034A)]


def image(le):
    """every object of le with its fixups applied, each at its own base"""
    import struct
    mem = {o.index: bytearray(o.data) for o in le.objects}
    for f in le.fixups:
        src = 0x10000000 * f.obj + f.off
        tgt = 0x10000000 * f.tobj + f.toff
        val = tgt if f.type == 7 else tgt - (src + 4)
        mem[f.obj][f.off:f.off + 4] = struct.pack("<I", val & 0xFFFFFFFF)
    return mem


@unittest.skipUnless(have_nasm(), "needs nasm")
class EssCodeTest(VxDBuilds, unittest.TestCase):
    """ESS's code keeps its place in the extended driver: Windows' sound,
    DirectSound and the interrupt handlers run the same bytes."""

    def test_ess_code_unchanged(self):
        stock, ext = image(self.builds[False][0]), image(self.builds[True][0])
        hooks = {(o, i) for o, a, b in HOOKS for i in range(a, b + 1)}
        for obj, old in stock.items():
            new = ext[obj]
            if obj in (1, 6):       # the extension goes after ESS's LCOD, PDAT
                self.assertGreater(len(new), len(old))
            else:
                self.assertEqual(len(new), len(old), "object %d" % obj)
            moved = ["%d:%04X" % (obj, i) for i in range(len(old))
                     if old[i] != new[i] and (obj, i) not in hooks]
            self.assertEqual(moved, [])
        # and each hook is still there, so the list stays current
        for obj, a, b in HOOKS:
            self.assertNotEqual(stock[obj][a:b + 1], ext[obj][a:b + 1],
                                "%d:%04X" % (obj, a))


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class PcmPathTest(VxDBuilds, unittest.TestCase):
    """Windows' own sound, without DOS programs, does what it does with ESS's
    driver: ES1869.DRV's calls around a wave device, a mixer change, and
    DirectSound taking and giving back the DSP make the same port accesses,
    hardware service calls and owners."""

    # the services that touch the hardware or its trapping
    HW_SERVICES = (0x00030000, 0x00040000, 0x00170000)
    TRAPPING = (0x00010098, 0x0001009A)

    def run_steps(self, ext):
        import vxdemu
        m = self.machine(ext)
        e = m.emu
        e.write32(vxdemu.ADI + 0x4D, 0x7100)    # the DMA channels' handles
        e.write32(vxdemu.ADI + 0x7A, 0x7101)
        # DirectSound's devnode, as its driver's open sets it (L1_0F3C)
        e.write32(m.syms["Global_Flag_0329"], vxdemu.DEVNODE)
        sys_vm = vxdemu.VM_SYS

        def ds(name):
            # DirectSound's buffer takes or gives back the DSP: cdecl, the
            # devnode on the stack
            e.write32(vxdemu.STACK - 0x100 + 4, vxdemu.DEVNODE)
            return e.run(m.syms[name], {"EBX": sys_vm})["EAX"]

        def api(fn):
            # ES1869.DRV: AX = Audio_Base, BX = 1 (the DSP)
            return m.api(sys_vm, fn, EAX=0x220, EBX=1)

        steps = [
            ("wave open", lambda: api(0x0002)),
            ("position", lambda: api(0x0004)),
            ("position", lambda: api(0x0004)),
            ("mixer", lambda: m.outb(sys_vm, 0x224, 0x7C)),
            ("mixer", lambda: m.outb(sys_vm, 0x225, 0x88)),
            ("wave close", lambda: api(0x0003)),
            ("mixer change", lambda: api(0x0002)),
            ("mixer change", lambda: api(0x0003)),
            ("DirectSound", lambda: ds("L1_125C")),
            ("position", lambda: api(0x0004)),
            ("DirectSound", lambda: ds("L1_12BA")),
            # ESSDC.EXE's look at the mixer, while nobody has the DSP
            ("mixer port", lambda: m.inb(sys_vm, 0x224)),
            ("wave close", lambda: api(0x0003)),
        ]
        out = []
        for name, step in steps:
            log, svc = len(m.hw.log), len(e.services)
            result = step()
            if isinstance(result, tuple):       # an API call: AX and CF
                result = (result[0]["EAX"] & 0xFFFF, result[1])
            out.append((name, result, m.hw.log[log:], [
                s for s in e.services[svc:]
                if s & 0xFFFF0000 in self.HW_SERVICES or s in self.TRAPPING],
                [m.adi32(o) for o in (vxdemu.ADI_DSP_OWNER,
                                      vxdemu.ADI_FM_OWNER,
                                      vxdemu.ADI_MPU_OWNER)],
                sorted(e.trap_off)))
        return out

    def test_windows_sound_as_with_ess_driver(self):
        import vxdemu
        stock, ext = self.run_steps(False), self.run_steps(True)
        for s, x in zip(stock, ext):
            self.assertEqual(x, s, s[0])
        # the steps did what they're for: Windows took the DSP and gave it
        # back each time, DirectSound too, and the position moved
        sys_vm = vxdemu.VM_SYS
        self.assertEqual([step[4][0] for step in stock],
                         [sys_vm] * 5 + [0, sys_vm, 0, sys_vm, sys_vm, 0,
                                         sys_vm, 0])
        self.assertEqual([step[1] for step in stock
                          if step[0] in ("wave open", "wave close",
                                         "mixer change", "DirectSound")],
                         [(0, False)] * 4 + [1, 0, (0, False)])
        self.assertNotEqual(stock[1][1], stock[2][1])
        self.assertTrue(stock[5][2])            # the DSP reset of a release


if __name__ == "__main__":
    unittest.main()
