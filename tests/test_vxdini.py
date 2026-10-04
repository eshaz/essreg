# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The SYSTEM.INI settings of the rebuilt ES1869.VXD, in a CPU emulator:
how they're read (once, at Sys_Dynamic_Device_Init, only while Windows
starts), what each one turns off, and that with all of them at ESS's
setting the extended driver makes the port accesses of ESS's own.

usage: python3 -m unittest tests.test_vxdini   (needs nasm and unicorn)
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_vxdext import HAVE_UNICORN, VxDBuilds, have_nasm  # noqa: E402
import test_vxddos  # noqa: E402
from test_vxddos import adlib, note, opl3  # noqa: E402

WINDOWS = test_vxddos.DosMixerTest.WINDOWS

if HAVE_UNICORN:
    from vxdemu import (VM_SYS, VM_DOS, VM_DOS2, ADI_FM_OWNER,  # noqa: E402
                        ADI_DSP_OWNER, ADI_MPU_OWNER)

VXD, DRV = "ES1869.VXD", "ES1869.DRV"
KEYS = {                            # key: its bit in ESSREG_Opts
    (VXD, "RegisterAPI"): 0x0001,
    (VXD, "VirtualFM"): 0x0002,
    (VXD, "DosTakesFM"): 0x0004,
    (VXD, "DosKeepsFM"): 0x0008,
    (VXD, "DosFMAudible"): 0x0010,
    (VXD, "DosMixerRestore"): 0x0020,
    (VXD, "ResetDosFM"): 0x0040,
    (DRV, "Audio2Oversampling"): 0x0100,
    (DRV, "Audio2Filter"): 0x0200,
    (DRV, "Audio1Filter"): 0x0400,
}
DEFAULT, READ = 0x007F, 0x8000
# every change off: ESS's driver
ESS = {key: "0" for key in KEYS}
ESS.update({(DRV, "Audio2Oversampling"): "1", (DRV, "Audio2Filter"): "1",
            (DRV, "Audio1Filter"): "1"})
WIN_AUDIO = 0x220


def dos_mixer(m, vm=None):
    """a DOS game sets up the mixer its way (test_vxddos)"""
    for reg in WINDOWS:
        m.outb(vm or VM_DOS, 0x224, reg)
        m.outb(vm or VM_DOS, 0x225, 0x00 if reg != 0x0E else 0x02)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class ReadTest(VxDBuilds, unittest.TestCase):
    def test_constants_match(self):
        m = self.machine()
        names = ("OPT_API", "OPT_VFM", "OPT_TAKES_FM", "OPT_KEEPS_FM",
                 "OPT_FM_AUDIBLE", "OPT_DOS_MIXER", "OPT_RESET_FM",
                 "OPT_A2_4X", "OPT_A2_FILTER", "OPT_A1_FILTER")
        self.assertEqual([m.syms[n] for n in names], list(KEYS.values()))
        self.assertEqual(m.syms["OPT_READ"], READ)

    def test_defaults(self):
        m = self.machine()
        self.assertEqual(m.opts(), DEFAULT)     # before the init, too
        self.assertTrue(m.start({}))
        self.assertEqual(m.opts(), DEFAULT | READ)

    def test_each_key(self):
        for key, bit in KEYS.items():
            with self.subTest(key=key[1]):
                m = self.machine()
                m.start({key: "0"})
                self.assertEqual(m.opts(), (DEFAULT & ~bit) | READ)
                m = self.machine()
                m.start({key: "1"})
                self.assertEqual(m.opts(), DEFAULT | bit | READ)

    def test_values_read_as_the_16_bit_drivers_read_them(self):
        # GetPrivateProfileInt: the leading digits, 0 without any
        for value, on in (("1", True), (" 1", True), ("2", True),
                          ("10", True), ("0", False), ("00", False),
                          ("", False), ("yes", False), ("on", False),
                          ("1 ; ESS's", True)):
            with self.subTest(value=value):
                m = self.machine()
                m.start({(VXD, "VirtualFM"): value})
                self.assertEqual(bool(m.opts() & 0x0002), on)

    def test_sections_and_keys_in_any_case(self):
        m = self.machine()
        m.start({("es1869.vxd", "virtualfm"): "0",
                 ("Es1869.Drv", "AUDIO2FILTER"): "1"})
        self.assertEqual(m.opts(), (DEFAULT & ~0x0002) | 0x0200 | READ)

    def test_other_sections_ignored(self):
        m = self.machine()
        m.start({("386Enh", "VirtualFM"): "0", (DRV, "VirtualFM"): "0",
                 ("ESFM.DRV", "VirtualFM"): "0"})
        self.assertEqual(m.opts(), DEFAULT | READ)

    def test_loaded_after_windows_started(self):
        # VMM's profile services are gone: the defaults, and essctl sees
        # that SYSTEM.INI wasn't read (the emulator fails the call)
        m = self.machine()
        self.assertTrue(m.start({(VXD, "VirtualFM"): "0"}, 0x40000000))
        self.assertEqual(m.opts(), DEFAULT)
        m = self.machine()
        self.assertTrue(m.start({(VXD, "RegisterAPI"): "0"}, 0x50000000))
        _out, cf = m.api(VM_SYS, 0x0400)
        self.assertFalse(cf)

    def test_read_once(self):
        m = self.machine()
        m.start({(VXD, "VirtualFM"): "0"})
        calls = m.emu.services.count(0x000100B3)
        self.assertEqual(calls, len(KEYS))
        m.emu.ini = {}                          # changed while Windows runs
        opl3(m, VM_DOS)
        m.api(VM_SYS, 0x0400)
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(m.emu.services.count(0x000100B3), calls)
        self.assertEqual(m.opts(), (DEFAULT & ~0x0002) | READ)

    def test_info_reports_the_settings(self):
        m = self.machine()
        m.start({})
        out, cf = m.api(VM_SYS, 0x0400)
        self.assertFalse(cf)
        self.assertEqual(out["EAX"] & 0xFFFF, 0x0112)
        self.assertEqual(out["EBX"] & 0xFFFF, 0x07FF)
        self.assertEqual(out["ECX"] & 0xFFFF, DEFAULT | READ)
        m = self.machine()
        m.start({(VXD, "VirtualFM"): "0", (VXD, "DosMixerRestore"): "0",
                 (DRV, "Audio2Filter"): "1", (DRV, "Audio1Filter"): "1"})
        out, cf = m.api(VM_SYS, 0x0400)
        self.assertEqual(out["EBX"] & 0xFFFF, 0x07FF & ~0x0580)
        self.assertEqual(out["ECX"] & 0xFFFF, 0x005D | 0x0600 | READ)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class EachSettingTest(VxDBuilds, unittest.TestCase):
    """each setting at 0 gives back what ESS's driver does there"""

    def machine_with(self, ini):
        m = self.machine()
        m.start(ini)
        return m

    def test_register_api(self):
        m = self.machine_with({(VXD, "RegisterAPI"): "0"})
        for fn in (0x0400, 0x0401, 0x040C):
            out, cf = m.api(VM_SYS, fn, EAX=0x1234, EBX=0x52)
            self.assertTrue(cf, hex(fn))
            self.assertEqual(out["EAX"] & 0xFFFF, 0x1234)   # untouched
        out, cf = m.api(VM_SYS, 0x0000)         # ESS's functions stay
        self.assertEqual((cf, out["EAX"] & 0xFFFF), (False, 0x0404))
        _out, cf = m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertFalse(cf)

    def test_virtual_fm(self):
        m = self.machine_with({(VXD, "VirtualFM"): "0"})
        note(m, VM_DOS2)
        self.assertFalse(adlib(m, VM_DOS))      # FFh, as ESS's
        self.assertTrue(m.emu.messages)         # and its "in use" message
        self.assertEqual(m.vfm(VM_DOS), 0)
        self.assertEqual(m.emu.heap, {})
        # a free chip is taken as with ESS's driver
        m = self.machine_with({(VXD, "VirtualFM"): "0"})
        self.assertTrue(opl3(m, VM_DOS))
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_DOS)

    def test_dos_takes_fm(self):
        m = self.machine_with({(VXD, "DosTakesFM"): "0"})
        m.inb(VM_SYS, 0x220)                    # a Windows program reads it
        self.assertFalse(m.adi8(0xEC) & 0x02)
        writes = list(m.hw.fm.writes)
        self.assertTrue(opl3(m, VM_DOS))        # virtual: Windows keeps it
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_SYS)
        self.assertEqual(m.hw.fm.writes, writes)

    def test_dos_keeps_fm(self):
        m = self.machine_with({(VXD, "DosKeepsFM"): "0"})
        m.opl(VM_DOS, 0x20, 0x21)
        resets = m.hw.fm_resets
        m.program_end(VM_DOS)
        m.opl(VM_DOS, 0x40, 0x10)               # reset under it, as ESS's
        self.assertEqual(m.hw.fm_resets, resets + 1)
        self.assertEqual(m.hw.fm.emu[0][0x20], 0x00)

    def test_dos_fm_audible(self):
        m = self.machine_with({(VXD, "DosFMAudible"): "0"})
        m.inb(VM_DOS, 0x388)
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_DOS)
        self.assertEqual((m.hw.mixer[0x7F], m.hw.mixer[0x36]), (0x01, 0x00))
        m.program_end(VM_DOS)
        self.assertEqual((m.hw.mixer[0x7F], m.hw.mixer[0x36]), (0x01, 0x00))

    def test_dos_mixer_restore(self):
        stock = self.machine(False)
        m = self.machine_with({(VXD, "DosMixerRestore"): "0",
                               (DRV, "Audio2Oversampling"): "1",
                               (DRV, "Audio2Filter"): "1"})
        for x in (stock, m):
            for reg, v in WINDOWS.items():
                x.hw.mixer[reg] = v
            dos_mixer(x)
            x.program_end(VM_DOS)
        self.assertEqual(m.hw.mixer, stock.hw.mixer)
        self.assertEqual(m.hw.mixer[0x50], 0x00)    # the game's 3-D stays
        self.assertFalse(m.adi8(0xEC) & 0x01)

    def test_reset_dos_fm(self):
        m = self.machine_with({(VXD, "ResetDosFM"): "0"})
        note(m, VM_DOS)
        m.program_end(VM_DOS)
        resets = m.hw.fm_resets
        _out, cf = m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertFalse(cf)
        self.assertEqual(m.hw.fm_resets, resets)
        self.assertTrue(m.hw.fm.key_on(0, 0))   # the note goes on, as ESS's

    def test_audio2_mode(self):
        # (Audio2Oversampling, Audio2Filter, Audio1Filter): 71h after a VM
        # takes the DSP, from 30h (4x left on, bit 5) and from 38h (the
        # Audio 2 filter bypassed)
        for over, filt, a1, from30, from38 in (
                ("0", "0", "0", 0x2E, 0x2E),    # the default
                ("0", "1", "0", 0x26, 0x26),    # the Audio 2 filter in use
                ("1", "0", "0", 0x3E, 0x3E),    # 4x (no filter anyway)
                ("1", "1", "0", 0x36, 0x3E),    # bit 3 as it was
                ("0", "0", "1", 0x2A, 0x2A),    # the Audio 1 filter in use
                ("1", "1", "1", 0x32, 0x3A)):   # ESS's
            for old, want in ((0x30, from30), (0x38, from38)):
                with self.subTest(over=over, filt=filt, a1=a1, old=hex(old)):
                    m = self.machine_with({(DRV, "Audio2Oversampling"): over,
                                           (DRV, "Audio2Filter"): filt,
                                           (DRV, "Audio1Filter"): a1})
                    m.hw.mixer[0x71] = old
                    m.api(VM_SYS, 0x0002, EAX=WIN_AUDIO, EBX=1)
                    self.assertEqual(m.hw.mixer[0x71], want)
                    stock = self.machine(False)
                    stock.hw.mixer[0x71] = old
                    stock.api(VM_SYS, 0x0002, EAX=WIN_AUDIO, EBX=1)
                    if (over, filt, a1) == ("1", "1", "1"):
                        self.assertEqual(want, stock.hw.mixer[0x71])

    def test_audio1_filter(self):
        # a DOS program's Audio 1 DAC plays through the filter, bit 2 as
        # Windows left it, as with ESS's driver
        m = self.machine_with({(DRV, "Audio1Filter"): "1"})
        m.hw.mixer[0x71] = 0x30
        m.outb(VM_DOS, 0x226, 1)
        m.outb(VM_DOS, 0x226, 0)
        self.assertEqual(m.hw.mixer[0x71], 0x2A)    # the Audio 2 mode only
        m.dma(VM_DOS, 1, 0x58)
        self.assertEqual(m.hw.mixer[0x71], 0x2A)
        self.assertEqual(m.emu.services.count(0x00040004), 0)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class AllOffTest(VxDBuilds, unittest.TestCase):
    """every setting at ESS's value: the same port accesses, owners,
    messages and trapping as ESS's driver, step by step"""

    HW_SERVICES = (0x00030000, 0x00040000, 0x00170000)
    TRAPPING = (0x00010098, 0x0001009A)

    def run_steps(self, ext):
        m = self.machine(ext)
        if ext:
            self.assertTrue(m.start(ESS))
            self.assertEqual(m.opts(), 0x0700 | READ)
        for reg, v in WINDOWS.items():
            m.hw.mixer[reg] = v
        e = m.emu

        def api(fn, **regs):
            out, cf = m.api(VM_SYS, fn, **regs)
            return out["EAX"] & 0xFFFF, cf

        steps = [
            ("register API", lambda: api(0x0400, EAX=0x5555)),
            ("MIDI open", lambda: api(0x0102, EAX=WIN_AUDIO)),
            ("DOS FM while MIDI plays", lambda: adlib(m, VM_DOS)),
            ("MIDI close", lambda: api(0x0103, EAX=WIN_AUDIO)),
            ("DOS FM", lambda: opl3(m, VM_DOS)),
            ("a note", lambda: note(m, VM_DOS)),
            ("another DOS box", lambda: adlib(m, VM_DOS2)),
            ("program ends", lambda: m.program_end(VM_DOS)),
            ("it comes back", lambda: m.opl(VM_DOS, 0x40, 0x10)),
            ("program ends", lambda: m.program_end(VM_DOS)),
            ("Windows sound", lambda: api(0x0002, EBX=1, EAX=WIN_AUDIO)),
            ("Windows done", lambda: api(0x0003, EBX=1, EAX=WIN_AUDIO)),
            ("Windows reads FM", lambda: m.inb(VM_SYS, 0x220)),
            ("DOS FM", lambda: adlib(m, VM_DOS2)),
            ("DOS mixer", lambda: dos_mixer(m)),
            ("DOS plays", lambda: m.dma(VM_DOS, 1, 0x58)),
            ("DOS records", lambda: m.dma(VM_DOS, 1, 0x54)),
            ("DOS Audio 2", lambda: m.dma(VM_DOS, 2, 0x58)),
            ("program ends", lambda: m.program_end(VM_DOS)),
            ("Windows sound", lambda: api(0x0002, EBX=1, EAX=WIN_AUDIO)),
            ("Windows done", lambda: api(0x0003, EBX=1, EAX=WIN_AUDIO)),
            ("box closes", lambda: m.vm_close(VM_DOS2)),
        ]
        out = []
        for name, step in steps:
            log, svc, msgs = len(m.hw.log), len(e.services), len(e.messages)
            result = step()
            out.append((name, result, m.hw.log[log:], [
                s for s in e.services[svc:]
                if s & 0xFFFF0000 in self.HW_SERVICES or s in self.TRAPPING],
                e.messages[msgs:],
                [m.adi32(o) for o in (ADI_DSP_OWNER, ADI_FM_OWNER,
                                      ADI_MPU_OWNER)],
                sorted(e.trap_off)))
        return out, m

    def test_as_ess_driver(self):
        stock, sm = self.run_steps(False)
        ext, em = self.run_steps(True)
        for s, x in zip(stock, ext):
            self.assertEqual(x, s, s[0])
        self.assertEqual(em.hw.mixer, sm.hw.mixer)
        self.assertEqual(em.emu.heap, {})       # no virtual chip either
        # the steps did what they're for: ESS's refusals and message
        self.assertEqual([s[1] for s in stock[:5]],
                         [(0x5555, True), (0, False), False, (0, False),
                          True])
        self.assertTrue(any(s[4] for s in stock))


if __name__ == "__main__":
    unittest.main()
