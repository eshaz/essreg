# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""DOS boxes and the rebuilt ES1869.VXD, in a CPU emulator: FM detection
in every ownership state, the virtual FM chip and its hand-over, the music
DAC and FM volume of a DOS FM owner, Windows' mixer around a DOS program,
and the reset when Windows uses the card again. Where the stock driver
behaves differently, it runs the same steps to show what changed.

usage: python3 -m unittest tests.test_vxddos   (needs nasm and unicorn)
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_vxdext import HAVE_UNICORN, VxDBuilds, have_nasm  # noqa: E402

if HAVE_UNICORN:
    from vxdemu import (VM_SYS, VM_DOS, VM_DOS2, ADI, ADI_FM,  # noqa: E402
                        ADI_FM_OWNER, ADI_FM_LAST, ADI_DSP_OWNER)

WIN_AUDIO = 0x220          # AX of 0002/0003 (Audio_Base) and 0102/0103
EXF_FM_SOFT, EXF_DOS_FM = 0x02, 0x08


def adlib(m, vm, base=0x388):
    """the AdLib detection of the AdLib programming guide"""
    m.opl(vm, 4, 0x60, base)
    m.opl(vm, 4, 0x80, base)
    s1 = m.inb(vm, base)
    m.opl(vm, 2, 0xFF, base)
    m.opl(vm, 4, 0x21, base)
    for _ in range(100):                        # at least 80 us
        m.inb(vm, base)
    s2 = m.inb(vm, base)
    m.opl(vm, 4, 0x60, base)
    m.opl(vm, 4, 0x80, base)
    return (s1 & 0xE0) == 0 and (s2 & 0xE0) == 0xC0


def opl3(m, vm, base=0x388):
    """OPL3 after AdLib: the status port's bits 2:1 read 0 (OPL2: 3)"""
    return adlib(m, vm, base) and (m.inb(vm, base) & 0x06) == 0


def native(m, vm, base=0x388):
    """ESFM native mode: 105h bit 7, a register written and read back"""
    m.outb(vm, base + 2, 0x05)
    m.outb(vm, base + 3, 0x80)
    m.outb(vm, base + 2, 0x08)                  # latch 008h
    m.outb(vm, base + 3, 0x00)
    m.outb(vm, base + 1, 0x5A)
    m.outb(vm, base + 2, 0x08)
    m.outb(vm, base + 3, 0x00)
    return m.inb(vm, base + 1) == 0x5A


def note(m, vm, base=0x388):
    """an instrument on channel 0 and a key-on"""
    for reg, v in ((0x20, 0x01), (0x40, 0x10), (0x60, 0xF0), (0x80, 0x77),
                   (0x23, 0x01), (0x43, 0x00), (0x63, 0xF0), (0x83, 0x77),
                   (0xA0, 0x98), (0xB0, 0x31)):
        m.opl(vm, reg, v, base)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class DosFMTest(VxDBuilds, unittest.TestCase):
    def midi_open(self, m):
        """ESFM.DRV's MODM_OPEN: Windows takes FM with 0102"""
        _out, cf = m.api(VM_SYS, 0x0102, EAX=WIN_AUDIO)
        self.assertFalse(cf)

    def midi_close(self, m):
        _out, cf = m.api(VM_SYS, 0x0103, EAX=WIN_AUDIO)
        self.assertFalse(cf)

    def test_detection_on_a_free_chip(self):
        for ext in (True, False):
            m = self.machine(ext)
            self.assertTrue(opl3(m, VM_DOS), ext)
            self.assertEqual(m.adi32(ADI_FM_OWNER), VM_DOS)
            # the real chip answered: the program's timer writes reached it
            self.assertIn(("emu", 0x002, 0xFF), m.hw.fm.writes)

    def test_detection_while_windows_midi_has_fm(self):
        m = self.machine(False)
        self.midi_open(m)
        self.assertFalse(adlib(m, VM_DOS))      # ESS's driver: FFh
        m = self.machine()
        self.midi_open(m)
        writes = list(m.hw.fm.writes)
        self.assertTrue(opl3(m, VM_DOS))
        self.assertTrue(native(m, VM_DOS))
        self.assertEqual(m.hw.fm.writes, writes)   # Windows' chip untouched
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_SYS)
        self.assertEqual(m.emu.messages, [])

    def test_detection_while_another_dos_box_has_fm(self):
        m = self.machine(False)
        note(m, VM_DOS2)
        self.assertFalse(adlib(m, VM_DOS))
        m = self.machine()
        note(m, VM_DOS2)
        writes = list(m.hw.fm.writes)
        for base in (0x388, 0x220):             # the alias and Audio_Base
            self.assertTrue(opl3(m, VM_DOS, base), hex(base))
        self.assertTrue(adlib(m, VM_DOS, 0x228))    # the SB Pro ports
        self.assertEqual(m.hw.fm.writes, writes)
        self.assertTrue(m.hw.fm.key_on(0, 0))   # the other box's note plays

    def test_virtual_timers(self):
        m = self.machine()
        self.midi_open(m)
        m.opl(VM_DOS, 4, 0x60)
        m.opl(VM_DOS, 4, 0x80)
        m.opl(VM_DOS, 3, 0x00)                  # timer 2: 256 x 320 us
        m.opl(VM_DOS, 4, 0x42)                  # start it, timer 1 masked
        m.hw.clock.advance(50000)
        self.assertEqual(m.inb(VM_DOS, 0x388), 0x00)
        m.hw.clock.advance(40000)
        self.assertEqual(m.inb(VM_DOS, 0x388), 0xA0)
        m.opl(VM_DOS, 4, 0x80)                  # flags reset
        self.assertEqual(m.inb(VM_DOS, 0x388), 0x00)
        # a masked timer sets no flag
        m.opl(VM_DOS, 2, 0xF0)
        m.opl(VM_DOS, 4, 0x41)
        m.hw.clock.advance(5000)
        self.assertEqual(m.inb(VM_DOS, 0x388), 0x00)

    def test_timer_waited_for_with_the_clock(self):
        # a program that waits a millisecond by other means, then reads
        m = self.machine()
        self.midi_open(m)
        m.opl(VM_DOS, 2, 0xFF)
        m.opl(VM_DOS, 4, 0x80)
        m.opl(VM_DOS, 4, 0x21)
        m.hw.clock.advance(1000)
        self.assertEqual(m.inb(VM_DOS, 0x388) & 0xE0, 0xC0)

    def test_time_stamp_counter_calibration(self):
        m = self.machine()
        self.midi_open(m)
        m.inb(VM_DOS, 0x388)
        m.hw.clock.advance(1500000)
        m.inb(VM_DOS, 0x388)
        self.assertEqual(m.emu.read8(m.syms["ESSREG_TSC_State"]), 3)
        self.assertEqual(m.emu.read32(m.syms["ESSREG_TSC_MHz"]), 100)
        m.opl(VM_DOS, 3, 0x00)
        m.opl(VM_DOS, 4, 0x42)
        m.hw.clock.advance(81000)               # just short of 81920 us
        self.assertEqual(m.inb(VM_DOS, 0x388), 0x00)
        m.hw.clock.advance(2000)
        self.assertEqual(m.inb(VM_DOS, 0x388), 0xA0)

    def test_no_time_stamp_counter(self):
        m = self.machine()
        m.emu.has_tsc = False
        self.midi_open(m)
        self.assertTrue(adlib(m, VM_DOS))
        m.hw.clock.advance(1500000)
        self.assertTrue(adlib(m, VM_DOS))
        self.assertEqual(m.emu.read8(m.syms["ESSREG_TSC_State"]), 1)

    def test_handover_when_windows_midi_closes(self):
        m = self.machine()
        self.midi_open(m)
        note(m, VM_DOS)                         # into the virtual chip
        m.outb(VM_DOS, 0x388, 0x43)             # and an address for later
        fm = m.hw.fm
        self.assertEqual(fm.writes, [])
        self.midi_close(m)
        m.outb(VM_DOS, 0x389, 0x3F)             # its next access takes it
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_DOS)
        self.assertEqual(fm.emu[0][0x20], 0x01)
        self.assertEqual(fm.emu[0][0xA0], 0x98)
        self.assertTrue(fm.key_on(0, 0))
        self.assertLess(fm.writes.index(("emu", 0xA0, 0x98)),
                        fm.writes.index(("emu", 0xB0, 0x31)))  # key-on last
        self.assertEqual(fm.emu[0][0x43], 0x3F)   # at the address it had set
        # the virtual chip went to the real one: nothing replays twice
        self.assertEqual(m.emu.read8(m.vfm(VM_DOS)), 0)

    def test_handover_keeps_the_music_dac(self):
        # ESFM.DRV's MODM_CLOSE releases FM (0103), then tells ES1869.DRV,
        # which gives I2S the music DAC and IIS the FM volume
        m = self.machine()
        hw = m.hw
        self.midi_open(m)
        hw.mixer[0x7F], hw.mixer[0x36] = 0x00, 0xCC    # Windows' MIDI
        note(m, VM_DOS)
        self.midi_close(m)
        m.inb(VM_DOS, 0x388)                    # the DOS game takes it
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_DOS)

        def es1869_drv(r7f, r36):
            m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
            for reg, v in ((0x7F, r7f), (0x36, r36)):
                m.outb(VM_SYS, 0x224, reg)
                m.outb(VM_SYS, 0x225, v)
            m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)
        es1869_drv(0x01, 0x00)                  # its close notification
        self.assertEqual((hw.mixer[0x7F], hw.mixer[0x36]), (0x00, 0xFF))
        es1869_drv(0x00, 0x00)                  # the user mutes it later
        self.assertEqual(hw.mixer[0x36], 0x00)
        m.program_end(VM_DOS)                   # Windows' state again
        self.assertEqual((hw.mixer[0x7F], hw.mixer[0x36]), (0x01, 0x00))

    def test_handover_in_native_mode(self):
        m = self.machine()
        self.midi_open(m)
        self.assertTrue(native(m, VM_DOS))
        for reg, v in ((0x100, 0x42), (0x240, 0x01)):
            m.outb(VM_DOS, 0x38A, reg & 0xFF)
            m.outb(VM_DOS, 0x38B, reg >> 8)
            m.outb(VM_DOS, 0x389, v)
        self.midi_close(m)
        m.inb(VM_DOS, 0x388)
        fm = m.hw.fm
        self.assertTrue(fm.native)
        self.assertEqual((fm.nat[0x008], fm.nat[0x100], fm.nat[0x240]),
                         (0x5A, 0x42, 0x01))
        nat = [r for mode, r, v in fm.writes if mode == "nat"]
        self.assertEqual(nat[-1], 0x240)        # key-on last
        self.assertEqual(fm.latch, 0x240)

    def test_handover_from_another_dos_box(self):
        m = self.machine()
        note(m, VM_DOS2)
        m.opl(VM_DOS, 0x21, 0x02)               # virtual
        m.program_end(VM_DOS2)                  # the other game ends
        self.assertEqual(m.adi32(ADI_FM_OWNER), 0)
        m.inb(VM_DOS, 0x388)
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_DOS)
        self.assertEqual(m.hw.fm.emu[0][0x21], 0x02)
        self.assertFalse(m.hw.fm.key_on(0, 0))  # the chip was reset for it

    def test_windows_port_access_gives_way_to_dos(self):
        m = self.machine()
        m.inb(VM_SYS, 0x220)                    # a Windows program reads it
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_SYS)
        self.assertTrue(m.adi8(0xEC) & EXF_FM_SOFT)
        self.assertIn((VM_SYS, 0x220), m.emu.trap_off)
        self.assertTrue(opl3(m, VM_DOS))        # a DOS program takes it
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_DOS)
        self.assertNotIn((VM_SYS, 0x220), m.emu.trap_off)
        writes = list(m.hw.fm.writes)
        m.outb(VM_SYS, 0x220, 0x20)             # Windows: its virtual chip
        m.outb(VM_SYS, 0x221, 0x01)
        self.assertEqual(m.hw.fm.writes, writes)
        # the stock driver keeps it for Windows until a MIDI program closes
        m = self.machine(False)
        m.inb(VM_SYS, 0x220)
        self.assertFalse(adlib(m, VM_DOS))

    def test_midi_keeps_fm_it_got_by_a_port_access(self):
        m = self.machine()
        m.inb(VM_SYS, 0x220)
        self.midi_open(m)                       # ESFM.DRV opens
        self.assertFalse(m.adi8(0xEC) & EXF_FM_SOFT)
        self.assertTrue(adlib(m, VM_DOS))       # virtual, Windows keeps it
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_SYS)

    def test_returning_program_keeps_its_chip(self):
        m = self.machine()
        m.opl(VM_DOS, 0x20, 0x21)
        resets = m.hw.fm_resets
        m.program_end(VM_DOS)                   # a child program ends
        self.assertEqual(m.adi32(ADI_FM_OWNER), 0)
        m.opl(VM_DOS, 0x40, 0x10)               # the game goes on
        self.assertEqual(m.hw.fm_resets, resets)
        self.assertEqual(m.hw.fm.emu[0][0x20], 0x21)
        # ESS's driver resets the chip under it
        m = self.machine(False)
        m.opl(VM_DOS, 0x20, 0x21)
        m.program_end(VM_DOS)
        m.opl(VM_DOS, 0x40, 0x10)
        self.assertEqual(m.hw.fm.emu[0][0x20], 0x00)

    def test_chip_reset_after_another_owner(self):
        m = self.machine()
        m.opl(VM_DOS, 0x20, 0x21)
        m.program_end(VM_DOS)
        m.opl(VM_DOS2, 0x20, 0x31)
        m.program_end(VM_DOS2)
        resets = m.hw.fm_resets
        m.opl(VM_DOS, 0x40, 0x10)
        self.assertEqual(m.hw.fm_resets, resets + 1)
        self.assertEqual(m.hw.fm.emu[0][0x20], 0x00)

    def test_dos_fm_is_audible(self):
        m = self.machine()
        hw = m.hw
        m.inb(VM_DOS, 0x388)
        self.assertEqual((hw.mixer[0x7F], hw.mixer[0x36]), (0x00, 0xFF))
        m.program_end(VM_DOS)
        self.assertEqual((hw.mixer[0x7F], hw.mixer[0x36]), (0x01, 0x00))
        # a level set meanwhile stays
        m.inb(VM_DOS, 0x388)
        m.api(VM_SYS, 0x0402, EBX=0x8836)
        m.program_end(VM_DOS)
        self.assertEqual((hw.mixer[0x7F], hw.mixer[0x36]), (0x01, 0x88))
        # FM already audible: nothing to change
        m = self.machine()
        m.hw.mixer[0x7F], m.hw.mixer[0x36] = 0x00, 0xCC
        m.inb(VM_DOS, 0x388)
        m.program_end(VM_DOS)
        self.assertEqual((m.hw.mixer[0x7F], m.hw.mixer[0x36]), (0x00, 0xCC))
        # ESS's driver leaves an FM-only program without the music DAC
        m = self.machine(False)
        m.inb(VM_DOS, 0x388)
        self.assertEqual((m.hw.mixer[0x7F], m.hw.mixer[0x36]), (0x01, 0x00))

    def test_program_end_lets_go_of_virtual_notes(self):
        m = self.machine()
        self.midi_open(m)
        note(m, VM_DOS)
        m.program_end(VM_DOS)
        self.midi_close(m)
        m.inb(VM_DOS, 0x388)                    # the next program takes it
        self.assertEqual(m.hw.fm.emu[0][0x20], 0x01)
        self.assertFalse(m.hw.fm.key_on(0, 0))

    def test_closing_the_box_frees_its_virtual_chip(self):
        m = self.machine()
        self.midi_open(m)
        m.inb(VM_DOS, 0x388)
        self.assertNotEqual(m.vfm(VM_DOS), 0)
        self.assertEqual(len(m.emu.heap), 1)
        m.vm_close(VM_DOS)
        self.assertEqual(m.emu.heap, {})
        self.assertEqual(m.vfm(VM_DOS), 0)

    def test_removing_the_device_frees_virtual_chips(self):
        m = self.machine()
        self.midi_open(m)
        m.inb(VM_DOS, 0x388)
        m.inb(VM_DOS2, 0x388)
        self.assertEqual(len(m.emu.heap), 2)
        for vm in (VM_DOS, VM_DOS2):            # what ESS does per VM
            m.emu.run(m.syms["ESSREG_Node_Remove"], {"EBX": vm, "EDI": ADI})
        self.assertEqual(m.emu.heap, {})

    def test_no_fm_port(self):
        m = self.machine()
        m.emu.uc.mem_write(ADI + ADI_FM, b"\xff\xff")
        self.assertFalse(adlib(m, VM_DOS))      # as ESS's driver


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class DosMixerTest(VxDBuilds, unittest.TestCase):
    # Windows' mixer: ES1869.DRV's start-up values and some levels
    WINDOWS = {0x0E: 0x00, 0x14: 0x88, 0x1A: 0x00, 0x1C: 0x05, 0x36: 0x00,
               0x38: 0x88, 0x3A: 0x00, 0x3C: 0x00, 0x3E: 0x88, 0x50: 0x0C,
               0x52: 0x3F, 0x54: 0x8F, 0x56: 0x95, 0x58: 0x94, 0x5A: 0x80,
               0x64: 0x0F, 0x68: 0x99, 0x69: 0x99, 0x6A: 0x99, 0x6B: 0x99,
               0x6C: 0x99, 0x6D: 0x00, 0x6E: 0x99, 0x6F: 0x00, 0x71: 0x32,
               0x7C: 0x00, 0x7D: 0x06, 0x7F: 0x01, 0x60: 0x2A, 0x62: 0x2A}
    # what comes back: all of it, 71h with the Audio 2 mode the driver sets
    # when the DOS VM takes the DSP, before the save (no 4x oversampling,
    # the filter bypassed)
    BACK = {**WINDOWS, 0x71: 0x2A}

    def dos_mixer(self, m, vm=VM_DOS):
        """a DOS game sets up the mixer its way"""
        for reg in self.WINDOWS:
            m.outb(vm, 0x224, reg)
            m.outb(vm, 0x225, 0x00 if reg != 0x0E else 0x02)

    def test_windows_mixer_back_after_a_dos_game(self):
        m = self.machine()
        for reg, v in self.WINDOWS.items():
            m.hw.mixer[reg] = v
        self.dos_mixer(m)
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS)
        m.program_end(VM_DOS)
        self.assertEqual(m.adi32(ADI_DSP_OWNER), 0)
        got = {reg: m.hw.mixer[reg] for reg in self.WINDOWS}
        self.assertEqual(got, self.BACK)
        # ESS's driver puts back 11 of them: the 3-D effect, the record
        # source and levels and the wave volume stay as the game left them
        m = self.machine(False)
        for reg, v in self.WINDOWS.items():
            m.hw.mixer[reg] = v
        self.dos_mixer(m)
        m.program_end(VM_DOS)
        self.assertEqual((m.hw.mixer[0x50], m.hw.mixer[0x1C],
                          m.hw.mixer[0x14]), (0x00, 0x00, 0x00))

    def test_fm_then_mixer(self):
        m = self.machine()
        for reg, v in self.WINDOWS.items():
            m.hw.mixer[reg] = v
        m.inb(VM_DOS, 0x388)                    # FM first: 7Fh, 36h change
        self.dos_mixer(m)
        m.program_end(VM_DOS)
        got = {reg: m.hw.mixer[reg] for reg in self.WINDOWS}
        self.assertEqual(got, self.BACK)

    def test_a_setting_made_meanwhile_is_kept(self):
        m = self.machine()
        for reg, v in self.WINDOWS.items():
            m.hw.mixer[reg] = v
        self.dos_mixer(m)
        m.api(VM_SYS, 0x0402, EBX=0x0450)       # essctl: 3-D off
        m.program_end(VM_DOS)
        self.assertEqual(m.hw.mixer[0x50], 0x04)
        self.assertEqual(m.hw.mixer[0x52], 0x3F)

    def test_windows_sound_resets_fm_left_by_dos(self):
        m = self.machine()
        fm = m.hw.fm
        note(m, VM_DOS)
        m.program_end(VM_DOS)
        self.assertTrue(fm.key_on(0, 0))        # the note goes on
        self.assertTrue(m.adi8(0xEC) & EXF_DOS_FM)
        resets = m.hw.fm_resets
        # Windows plays a sound, or the volume control changes a level
        _out, cf = m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertFalse(cf)
        self.assertEqual(m.hw.fm_resets, resets + 1)
        self.assertFalse(fm.key_on(0, 0))
        self.assertEqual(m.adi32(ADI_FM_LAST), 0xFFFFFFFF)
        self.assertFalse(m.adi8(0xEC) & EXF_DOS_FM)
        m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(m.hw.fm_resets, resets + 1)   # once
        # the same game starting again gets a clean chip
        m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)
        m.inb(VM_DOS, 0x388)
        self.assertEqual(m.hw.fm_resets, resets + 2)

    def test_midi_open_resets_fm_left_by_dos(self):
        m = self.machine()
        note(m, VM_DOS)
        m.program_end(VM_DOS)
        _out, cf = m.api(VM_SYS, 0x0102, EAX=WIN_AUDIO)
        self.assertFalse(cf)
        self.assertFalse(m.hw.fm.key_on(0, 0))

    def test_no_reset_while_dos_still_plays(self):
        m = self.machine()
        note(m, VM_DOS)
        _out, cf = m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertFalse(cf)                    # FM and the DSP are separate
        self.assertTrue(m.hw.fm.key_on(0, 0))


if __name__ == "__main__":
    unittest.main()
