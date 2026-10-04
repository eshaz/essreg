# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""DOS boxes and the rebuilt ES1869.VXD, in a CPU emulator: FM detection
in every ownership state, the virtual FM chip and its hand-over, the music
DAC and FM volume of a DOS FM owner, Windows' mixer around a DOS program,
the DACs' filters and modes for a DOS program, and the reset when Windows
uses the card again. Where the stock driver behaves differently, it runs
the same steps to show what changed.

usage: python3 -m unittest tests.test_vxddos   (needs nasm and unicorn)
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_vxdext import HAVE_UNICORN, VxDBuilds, have_nasm  # noqa: E402

if HAVE_UNICORN:
    from vxdemu import (VM_SYS, VM_DOS, VM_DOS2, ADI, ADI_FM,  # noqa: E402
                        ADI_FM_OWNER, ADI_FM_LAST, ADI_DSP_OWNER, DMA1,
                        DMA2)

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
    # what comes back: all of it, 71h with the DACs' mode the driver sets
    # when the DOS VM takes the DSP, before the save (no 4x oversampling,
    # both filters bypassed)
    BACK = {**WINDOWS, 0x71: 0x2E}

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


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class DosDacTest(VxDBuilds, unittest.TestCase):
    """The DACs for a DOS program: both play, and the ADC records, with the
    filters bypassed and Audio 2 not oversampled, also when a transfer
    starts after the program reset the mixer (ESSREG_DMA1, ESSREG_DMA2),
    and Windows has the same mode when it takes the DSP back"""

    PLAY, RECORD = 0x58, 0x54           # auto-init, from or to memory
    MASKED = 0x59

    def game(self, ext=True, ini=None, a71=0x32):
        """a DOS game takes the DSP with a reset, 71h as Windows left it"""
        m = self.machine(ext)
        if ini is not None:
            m.start(ini)
        m.hw.mixer[0x71] = a71
        m.outb(VM_DOS, 0x226, 1)
        m.outb(VM_DOS, 0x226, 0)
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS)
        return m

    def test_dos_program_plays_unfiltered(self):
        # from 32h (4x and bit 5, from Windows): no 4x, both filters
        # bypassed
        m = self.game()
        self.assertEqual(m.hw.mixer[0x71], 0x2E)
        self.assertEqual(self.game(False).hw.mixer[0x71], 0x32)

    def test_records_unfiltered(self):
        m = self.game()
        for mode in (self.RECORD, self.MASKED, self.PLAY):
            m.hw.mixer[0x71] = 0x2A             # the game uses the filter
            m.dma(VM_DOS, 1, mode)
            self.assertEqual(m.hw.mixer[0x71],
                             0x2A if mode == self.MASKED else 0x2E)
        # VDMAD's own handler programs the channel every time
        self.assertEqual(m.emu.dma_default, [(DMA1, VM_DOS)] * 3)

    def test_after_a_mixer_reset(self):
        m = self.game()
        m.outb(VM_DOS, 0x224, 0x71)             # the game clears 71h
        m.outb(VM_DOS, 0x225, 0x00)
        m.dma(VM_DOS, 1, self.PLAY)
        self.assertEqual(m.hw.mixer[0x71], 0x04)
        m.hw.mixer[0x71] = 0x00
        m.dma(VM_DOS, 2, self.PLAY)             # not oversampled, bypassed
        self.assertEqual(m.hw.mixer[0x71], 0x0C)
        self.assertEqual(m.emu.dma_default,
                         [(DMA1, VM_DOS), (DMA2, VM_DOS)])
        self.assertEqual(m.hw.index, 0x71)      # the game's index kept

    def test_only_the_owner(self):
        m = self.game()
        m.hw.mixer[0x71] = 0x00
        for vm in (VM_DOS2, VM_SYS):
            for channel in (1, 2):
                m.dma(vm, channel, self.PLAY)
        self.assertEqual(m.hw.mixer[0x71], 0x00)
        self.assertEqual(len(m.emu.dma_default), 4)
        # nor Windows' own transfers: ES1869.DRV sets the bits itself
        m = self.machine()
        m.api(VM_SYS, 0x0002, EAX=WIN_AUDIO, EBX=1)
        m.hw.mixer[0x71] = 0x00
        m.dma(VM_SYS, 1, self.PLAY)
        m.dma(VM_SYS, 2, self.PLAY)
        self.assertEqual(m.hw.mixer[0x71], 0x00)

    def test_windows_keeps_the_mode(self):
        for restore in ("1", "0"):
            with self.subTest(DosMixerRestore=restore):
                m = self.game(ini={("ES1869.VXD", "DosMixerRestore"):
                                   restore})
                m.program_end(VM_DOS)
                _out, cf = m.api(VM_SYS, 0x0002, EAX=WIN_AUDIO, EBX=1)
                self.assertFalse(cf)
                self.assertEqual(m.hw.mixer[0x71] & 0x1C, 0x0C)

    def test_audio1_filter_kept(self):
        # Audio1Filter=1: bit 2 as the program and Windows leave it
        m = self.game(ini={("ES1869.DRV", "Audio1Filter"): "1"})
        self.assertEqual(m.hw.mixer[0x71], 0x2A)
        for mode in (self.RECORD, self.PLAY):
            m.dma(VM_DOS, 1, mode)
            self.assertEqual(m.hw.mixer[0x71], 0x2A)

    def test_settings_at_1_are_ess(self):
        ini = {("ES1869.DRV", "Audio1Filter"): "1",
               ("ES1869.DRV", "Audio2Oversampling"): "1",
               ("ES1869.DRV", "Audio2Filter"): "1"}
        for ext in (False, True):
            with self.subTest(ext=ext):
                m = self.game(ext, ini if ext else None)
                self.assertEqual(m.hw.mixer[0x71], 0x32)
                for channel, mode in ((1, self.PLAY), (1, self.RECORD),
                                      (2, self.PLAY)):
                    m.hw.mixer[0x71] = 0x00
                    m.dma(VM_DOS, channel, mode)
                    self.assertEqual(m.hw.mixer[0x71], 0x00)
                self.assertEqual(m.emu.services.count(0x00040004), 0)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class RecordingTakesDSPTest(VxDBuilds, unittest.TestCase):
    """A recording in Windows takes the DSP from a DOS program (040D), which
    goes on with a virtual Sound Blaster: it answers as the chip does and
    times its transfers and interrupts without sound, while the chip, its
    channel and the physical interrupt are the recording's (ESSREG_VSB_*)"""

    def game(self, ext=True, ini=None):
        """a DOS game that found the Sound Blaster and plays FM"""
        m = self.machine(ext)
        if ini is not None:
            m.start(ini)
        m.outb(VM_DOS, 0x226, 1)
        m.outb(VM_DOS, 0x226, 0)
        self.assertEqual(m.inb(VM_DOS, 0x22A), 0xAA)
        self.assertTrue(adlib(m, VM_DOS))
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS)
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_DOS)
        return m

    def take(self, m, vm=VM_SYS):
        out, cf = m.api(vm, 0x040D)
        self.mark = len(m.emu.vpicd)    # ESS's release clears its request
        return (cf, out["EAX"] & 0xFFFF) if cf else (cf, 0)

    def vsb(self, m):
        return m.adi32(m.syms["EX_VSB_VM"])

    def cmd(self, m, *data):
        for b in data:
            m.outb(VM_DOS, 0x22C, b)

    def read(self, m):
        self.assertTrue(m.inb(VM_DOS, 0x22E) & 0x80)
        return m.inb(VM_DOS, 0x22A)

    def events(self, m, what="set"):
        """the audio IRQ's VPICD calls since the take"""
        return [vm for w, irq, vm in m.emu.vpicd[self.mark:]
                if w == what and irq == 0x7001]

    def test_the_recording_starts(self):
        m = self.game()
        _out, cf = m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertTrue(cf)                     # as ESS's driver refuses it
        self.assertEqual(self.take(m), (False, 0))
        self.assertEqual(self.vsb(m), VM_DOS)
        self.assertEqual(m.adi32(ADI_DSP_OWNER), 0)
        _out, cf = m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertFalse(cf)
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_SYS)
        # the game's FM goes on to the chip, to be recorded
        note(m, VM_DOS)
        self.assertTrue(m.hw.fm.key_on(0, 0))
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_DOS)

    def test_settings_and_callers(self):
        m = self.game(ini={("ES1869.VXD", "RecordTakesDSP"): "0"})
        self.assertEqual(self.take(m), (True, 2))
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS)
        self.assertEqual(self.vsb(m), 0)
        m = self.game()
        self.assertEqual(self.take(m, VM_DOS2), (True, 2))   # Windows only
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS)
        # nothing to take: free, or Windows' already
        m = self.machine()
        self.assertEqual(self.take(m), (False, 0))
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(self.take(m), (False, 0))
        self.assertEqual(self.vsb(m), 0)
        _out, cf = self.machine(False).api(VM_SYS, 0x040D)
        self.assertTrue(cf)                     # ESS's driver hasn't 040D

    def test_answers_as_the_chip(self):
        m = self.game()
        self.take(m)
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        n = len(m.hw.log)
        m.outb(VM_DOS, 0x226, 1)
        m.outb(VM_DOS, 0x226, 0)
        self.assertEqual(self.read(m), 0xAA)
        self.cmd(m, 0xE1)
        self.assertEqual((self.read(m), self.read(m)), (0x03, 0x01))
        self.cmd(m, 0xE7)                       # the chip's own answer
        self.assertEqual((self.read(m), self.read(m)), (0x68, 0x89))
        self.cmd(m, 0xE0, 0x5A)
        self.assertEqual(self.read(m), 0xA5)
        self.cmd(m, 0xE4, 0x3C, 0xE8)
        self.assertEqual(self.read(m), 0x3C)
        self.cmd(m, 0xD1, 0xD8)
        self.assertEqual(self.read(m), 0xFF)
        self.cmd(m, 0xD3, 0xD8)
        self.assertEqual(self.read(m), 0x00)
        self.cmd(m, 0x20)
        self.assertEqual(self.read(m), 0x80)
        self.assertEqual(m.inb(VM_DOS, 0x22C) & 0x80, 0)    # ready
        self.assertFalse(m.inb(VM_DOS, 0x22E) & 0x80)
        self.cmd(m, 0xC6, 0xA1, 0xF0, 0xC0, 0xA1)            # Extended mode
        self.assertEqual(self.read(m), 0xF0)
        # its mixer is its own: a reset of it leaves Windows' alone
        mixer = list(m.hw.mixer)
        m.outb(VM_DOS, 0x224, 0x22)
        m.outb(VM_DOS, 0x225, 0xEE)
        self.assertEqual(m.inb(VM_DOS, 0x225), 0xEE)
        m.outb(VM_DOS, 0x224, 0x00)
        m.outb(VM_DOS, 0x225, 0x00)
        m.outb(VM_DOS, 0x224, 0x22)
        self.assertEqual(m.inb(VM_DOS, 0x225), 0x00)
        self.assertEqual(m.hw.mixer, mixer)
        # and the chip saw none of it
        self.assertEqual(m.hw.log[n:], [])
        self.assertEqual(m.emu.messages, [])

    def test_a_transfer_interrupts_when_it_would_end(self):
        m = self.game()
        self.take(m)
        self.cmd(m, 0x40, 0xD2)                 # 1000000 / 46: 21739 B/s
        n = 2174                                # 100 ms
        self.cmd(m, 0x14, (n - 1) & 0xFF, (n - 1) >> 8)
        m.advance(99)
        self.assertEqual(self.events(m), [])
        m.advance(2)
        self.assertEqual(self.events(m), [VM_DOS])
        m.inb(VM_DOS, 0x22E)                    # the handler acknowledges
        self.assertEqual(self.events(m, "clear"), [VM_DOS])
        m.eoi(VM_DOS)                           # and ends it: no physical EOI
        self.assertEqual(self.events(m, "eoi"), [])
        m.advance(500)
        self.assertEqual(self.events(m), [VM_DOS])          # once
        # Windows' interrupts still end physically
        m.eoi(VM_SYS)
        self.assertEqual(self.events(m, "eoi"), [VM_SYS])

    def test_auto_initialize_pause_and_stop(self):
        m = self.game()
        self.take(m)
        self.cmd(m, 0x40, 0xD2, 0x48, 0xFF, 0x03, 0x1C)     # 1024 B: 47.1 ms
        m.advance(200)
        self.assertEqual(len(self.events(m)), 4)
        self.cmd(m, 0xD0)                       # paused 47.1 * 5 - 200 short
        self.assertEqual(m.emu.timeouts, {})
        m.advance(1000)
        self.assertEqual(len(self.events(m)), 4)
        self.cmd(m, 0xD4)
        m.advance(37)
        self.assertEqual(len(self.events(m)), 5)
        self.cmd(m, 0xDA)                       # the block that runs, then no more
        m.advance(48)
        self.assertEqual(len(self.events(m)), 6)
        m.advance(1000)
        self.assertEqual(len(self.events(m)), 6)
        self.assertEqual(m.emu.timeouts, {})

    def test_extended_mode_transfer(self):
        m = self.game()
        self.take(m)
        # 795500 / 16 = 49718 Hz, stereo, 16-bit: 198872 B/s; 19887 B is
        # 100 ms, as a two's complement count
        count = 0x10000 - 19887
        self.cmd(m, 0xC6, 0xA1, 0xF0, 0xA8, 0x01, 0xB7, 0x04,
                 0xA4, count & 0xFF, 0xA5, count >> 8, 0xB1, 0x50,
                 0xB8, 0x05)                    # DMA on, auto-initialize
        m.advance(305)
        self.assertEqual(len(self.events(m)), 3)
        self.cmd(m, 0xB8, 0x00)
        m.advance(500)
        self.assertEqual(len(self.events(m)), 3)
        # without B1h bit 6, no interrupt
        self.cmd(m, 0xB1, 0x10, 0xB8, 0x01)
        m.advance(500)
        self.assertEqual(len(self.events(m)), 3)

    def test_the_channel_stays_the_recordings(self):
        m = self.game()
        self.take(m)
        m.dma(VM_DOS, 1, 0x58)                  # the game's transfer
        self.assertEqual(m.emu.dma_default, [])
        m.dma(VM_SYS, 1, 0x54)                  # the recording's
        self.assertEqual(m.emu.dma_default, [(DMA1, VM_SYS)])

    def test_a_running_transfer_goes_on(self):
        m = self.game()
        m.emu.write32(ADI + 0x4D, DMA1)
        m.emu.virt_mode[DMA1] = 0x58            # unmasked: it plays
        self.take(m)
        m.advance(100)                          # 2048 B at 21739 B/s: 94 ms
        self.assertEqual(self.events(m), [VM_DOS])

    def test_the_real_dsp_back_once_windows_lets_go(self):
        m = self.game()
        self.take(m)
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)         # the recording
        self.cmd(m, 0xD1, 0x40, 0xA5, 0x48, 0x34, 0x12)
        n = len(m.hw.log)
        m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)         # ends
        self.assertEqual(self.vsb(m), 0)
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS)
        self.assertIn((VM_DOS, 0x22C), m.emu.trap_off)      # its ports again
        dsp = [v for op, p, v in m.hw.log[n:] if op == "out" and p == 0x22C]
        self.assertEqual(dsp[-6:], [0xD1, 0x40, 0xA5, 0x48, 0x34, 0x12])

    def test_or_at_its_reset_once_it_plays_no_more(self):
        m = self.game()
        self.take(m)
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.cmd(m, 0x48, 0xFF, 0x07, 0x1C)     # auto-initialize runs
        m.outb(VM_DOS, 0x226, 1)                # a reset while Windows has
        m.outb(VM_DOS, 0x226, 0)                # the DSP: the virtual one
        self.assertEqual(self.read(m), 0xAA)
        self.assertEqual(self.vsb(m), VM_DOS)
        self.cmd(m, 0x1C)
        m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(self.vsb(m), VM_DOS)   # it plays: still virtual
        m.outb(VM_DOS, 0x226, 1)
        m.outb(VM_DOS, 0x226, 0)
        self.assertEqual(self.vsb(m), 0)
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS)
        self.assertEqual(m.emu.timeouts, {})
        self.assertEqual(m.inb(VM_DOS, 0x22A), 0xAA)        # the chip's

    def test_ends_with_the_program_or_the_vm(self):
        for end in ("program", "vm"):
            with self.subTest(end=end):
                m = self.game()
                self.take(m)
                self.cmd(m, 0x48, 0xFF, 0x07, 0x1C)
                m.advance(100)
                self.assertTrue(m.emu.timeouts)
                if end == "program":
                    m.program_end(VM_DOS)
                else:
                    m.vm_close(VM_DOS)
                self.assertEqual(self.vsb(m), 0)
                self.assertEqual(m.emu.timeouts, {})
                self.assertIn(VM_DOS, self.events(m, "clear"))

    def test_fm_keeps_the_music_dac_while_recorded(self):
        # FM first, so the game gives FM the music DAC (7Fh bit 0 clear),
        # then the Sound Blaster; the recording sets bit 4, and at the
        # game's end I2S doesn't get the DAC back while it records
        for recording in (False, True):
            with self.subTest(recording=recording):
                m = self.machine()
                self.assertTrue(adlib(m, VM_DOS))
                m.outb(VM_DOS, 0x226, 1)
                m.outb(VM_DOS, 0x226, 0)
                self.take(m)
                self.assertEqual(m.hw.mixer[0x7F] & 1, 0)   # the game's FM
                m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
                if recording:
                    m.hw.mixer[0x7F] |= 0x10
                m.program_end(VM_DOS)
                self.assertEqual(m.hw.mixer[0x7F] & 1, 0 if recording else 1)

    def test_its_channel_follows_once_it_is_back(self):
        # the game programs its channel while the recording has it: none of
        # it reaches the chip until the game has the DSP back, and then an
        # event in its VM gives the channel to VDMAD before the game runs on
        m = self.game()
        self.take(m)
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        m.dma(VM_DOS, 1, 0x58)                  # auto-initialize, unmasked
        m.dma(VM_DOS, 2, 0x58)                  # and Audio 2's: Windows'
        self.assertEqual(m.emu.dma_default, [])
        m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS)
        self.assertEqual([ev[0] for ev in m.emu.vm_events.values()],
                         [VM_DOS])
        self.assertEqual(m.emu.dma_default, [])
        m.inb(VM_DOS, 0x22E)                    # the game runs on
        self.assertEqual(m.emu.dma_default, [(DMA1, VM_DOS)])
        self.assertEqual(m.emu.vm_events, {})
        self.assertEqual(m.adi32(m.syms["EX_VSB_Event"]), 0)

    def test_or_once_it_resets_the_dsp(self):
        m = self.game()
        self.take(m)
        m.dma(VM_DOS, 1, 0x58)
        m.outb(VM_DOS, 0x226, 1)                # nobody has the DSP: the
        m.outb(VM_DOS, 0x226, 0)                # real one, with its channel
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS)
        self.assertEqual(m.inb(VM_DOS, 0x22A), 0xAA)
        self.assertEqual(m.emu.dma_default, [(DMA1, VM_DOS)])

    def test_no_channel_once_windows_took_the_dsp_again(self):
        m = self.game()
        self.take(m)
        m.dma(VM_DOS, 1, 0x58)
        m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)  # back to the game
        self.take(m)                            # and the next recording
        self.assertEqual(m.emu.vm_events, {})   # cancelled
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        m.inb(VM_DOS, 0x22E)
        self.assertEqual(m.emu.dma_default, [])

    def test_the_stereo_bit_comes_back(self):
        # the game's mixer as the chip had it: read back from the virtual
        # Sound Blaster, and its stereo bit on the chip again with the DSP;
        # the levels are Windows' meanwhile and after
        m = self.game()
        for reg, value in ((0x0E, 0x02), (0x22, 0xEE)):
            m.outb(VM_DOS, 0x224, reg)
            m.outb(VM_DOS, 0x225, value)
        self.take(m)
        self.assertEqual(m.hw.mixer[0x0E], 0x00)    # Windows' mixer back
        m.outb(VM_DOS, 0x224, 0x0E)
        self.assertEqual(m.inb(VM_DOS, 0x225), 0x02)
        m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS)
        self.assertEqual(m.hw.mixer[0x0E], 0x02)
        m.program_end(VM_DOS)
        self.assertEqual(m.hw.mixer[0x0E], 0x00)

    def test_removing_the_device_ends_it(self):
        # no time-out or event may outlive the ADI
        for back in (False, True):
            with self.subTest(back=back):
                m = self.game()
                self.take(m)
                self.cmd(m, 0x48, 0xFF, 0x07, 0x1C)
                if back:
                    self.cmd(m, 0xDA)
                    m.advance(200)
                    m.inb(VM_DOS, 0x22E)        # the last block's interrupt
                    m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)
                    self.assertTrue(m.emu.vm_events)
                else:
                    self.assertTrue(m.emu.timeouts)
                for vm in (VM_SYS, VM_DOS, VM_DOS2):
                    m.emu.run(m.syms["ESSREG_Node_Remove"],
                              {"EBX": vm, "EDI": ADI})
                self.assertEqual(self.vsb(m), 0)
                self.assertEqual(m.emu.timeouts, {})
                self.assertEqual(m.emu.vm_events, {})

    def test_a_second_game_ends_the_first_ones(self):
        # one virtual Sound Blaster a device: the first game's transfer runs
        # on when the recording stops, the second game takes the free DSP,
        # and the next recording takes it from the second
        m = self.game()
        self.take(m)
        self.cmd(m, 0x48, 0xFF, 0x07, 0x1C)
        m.outb(VM_DOS2, 0x226, 1)
        m.outb(VM_DOS2, 0x226, 0)
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS2)
        self.assertEqual(self.take(m), (False, 0))
        self.assertEqual(self.vsb(m), VM_DOS2)
        self.assertEqual(m.emu.timeouts, {})
        self.assertEqual(m.adi32(ADI_DSP_OWNER), 0)


VMSTAT_PM_APP = 0x40


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class TimedFMTest(VxDBuilds, unittest.TestCase):
    """A real-mode DOS program's FM plays from a queue on a clock that
    follows its timer ticks (DosFMDelay, ESSREG_FMQ_*): the chip gets its
    writes in order, a delay after their ticks, and evenly, whatever bursts
    Windows hands the ticks over in"""

    DELAY = 30000

    def game(self, ini=None):
        """the VxD started, its clock calibrated, and a DOS game that took
        the FM chip with a write, its FM playing from the queue"""
        m = self.machine()
        self.assertTrue(m.start(ini or {}))
        m.emu.run(m.syms["ESSREG_Clock"], {})
        m.advance(1100)
        m.emu.run(m.syms["ESSREG_Clock"], {})     # calibrated from here
        self.write(m, 0x20, 0x01)
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_DOS)
        return m

    def write(self, m, reg, value, vm=VM_DOS):
        m.outb(vm, 0x388, reg)
        m.outb(vm, 0x389, value)

    def timed(self, m):
        return m.adi32(m.syms["EX_FMQ_VM"]) == VM_DOS

    def chip(self, m, reg):
        """(value, when) of each write of emulation register reg"""
        return [(v, t) for (mode, r, v), t in zip(m.hw.fm.writes,
                                                  m.hw.fm.times)
                if mode == "emu" and r == reg]

    def play(self, m, period, n, gaps=(), start=None):
        """n ticks every period us, each followed by a write of register
        A0h with the tick's number; the VM doesn't run during each gap
        (start, end), and the ticks it missed come in a burst at its end.
        Returns the ticks' times"""
        t0 = m.hw.clock.us + 1000 if start is None else start
        times = []
        for k in range(n):
            t = t0 + int(k * period)
            when = t
            for a, b in gaps:
                if a <= t < b:
                    when = b
            m.advance_to(max(when, m.hw.clock.us))
            self.assertTrue(m.tick(VM_DOS))
            self.write(m, 0xA0, k & 0xFF)
            times.append(t)
        return times

    def test_writes_come_a_delay_after_their_tick(self):
        m = self.game()
        self.assertTrue(self.timed(m))
        self.assertNotIn((VM_DOS, 0x388), m.emu.trap_off)
        self.assertEqual(m.emu.min_int, [1])        # 1 ms time-outs
        times = self.play(m, 1428.6, 64)
        # the writes of the last 30 ms wait in the queue
        due = sum(1 for t in times if t + self.DELAY <= m.hw.clock.us)
        self.assertLessEqual(abs(len(self.chip(m, 0xA0)) - due), 1)
        self.assertLess(due, 50)
        m.advance(50)
        got = self.chip(m, 0xA0)
        self.assertEqual([v for v, t in got], list(range(64)))
        for (v, t), tick in zip(got, times):
            self.assertLessEqual(abs(t - (tick + self.DELAY)), 1500)

    def test_a_burst_plays_evenly(self):
        # 700 Hz; the VM doesn't run for 20 ms, then gets the 14 ticks it
        # missed back to back: each write still plays a delay after its own
        # tick, where the raw ticks came up to 20 ms late
        m = self.game()
        t0 = m.hw.clock.us + 1000
        gap = (t0 + 120000, t0 + 140000)
        times = self.play(m, 1428.6, 160, gaps=[gap], start=t0)
        m.advance(60)
        got = self.chip(m, 0xA0)
        self.assertEqual([v for v, t in got], [k & 0xFF for k in range(160)])
        late = [t - (tick + self.DELAY) for (v, t), tick in
                zip(got[60:], times[60:])]
        self.assertLessEqual(max(abs(x) for x in late), 1500, late)
        burst = [k for k, tick in enumerate(times) if gap[0] <= tick < gap[1]]
        self.assertGreater(len(burst), 10)

    def test_a_slower_tick_and_a_rate_change(self):
        # 140 Hz, then the game sets 700 Hz: the clock locks again on the
        # new rate, and from then on the writes are even again
        m = self.game()
        times = self.play(m, 7142.9, 64)
        times += self.play(m, 1428.6, 200, start=times[-1] + 1429)
        m.advance(60)
        got = self.chip(m, 0xA0)
        self.assertEqual(len(got), 264)
        late = [t - (tick + self.DELAY) for (v, t), tick in
                zip(got[150:], times[150:])]
        self.assertLessEqual(max(abs(x) for x in late), 1500, late)

    def test_reads_come_from_its_virtual_chip(self):
        m = self.game()
        writes = list(m.hw.fm.writes)
        self.assertTrue(adlib(m, VM_DOS))           # its timers, on time
        m.advance(50)
        self.assertGreater(len(m.hw.fm.writes), len(writes))

    def test_the_end_of_the_program_plays_what_is_queued(self):
        m = self.game()
        self.play(m, 1428.6, 20)
        m.advance(2)
        before = len(self.chip(m, 0xA0))
        self.assertLess(before, 20)
        m.program_end(VM_DOS)
        got = self.chip(m, 0xA0)
        self.assertEqual([v for v, t in got], list(range(20)))
        self.assertFalse(self.timed(m))
        self.assertEqual(m.emu.timeouts, {})
        self.assertEqual(m.emu.min_int, [])
        self.assertEqual(m.adi32(m.syms["EX_FMQ_Blk"]), 0)

    def test_a_protected_mode_program_plays_directly(self):
        m = self.machine()
        self.assertTrue(m.start({}))
        m.emu.write32(VM_DOS, VMSTAT_PM_APP)        # CB_VM_Status
        self.write(m, 0x20, 0x01)
        self.assertEqual(m.adi32(ADI_FM_OWNER), VM_DOS)
        self.assertFalse(self.timed(m))
        self.assertIn((VM_DOS, 0x388), m.emu.trap_off)
        self.write(m, 0xA0, 0x55)
        self.assertEqual(self.chip(m, 0xA0), [(0x55, m.hw.clock.us)])

    def test_writes_off_its_ticks_play_directly(self):
        # no tick ever comes: the program's music isn't on interrupt 8, so
        # after 64 writes it plays directly, what was queued first
        m = self.game()
        for k in range(70):
            self.write(m, 0xA0, k)
            m.hw.clock.advance(3000)
        self.assertFalse(self.timed(m))
        self.assertEqual(m.adi32(m.syms["EX_FMQ_Direct"]), VM_DOS)
        self.assertIn((VM_DOS, 0x388), m.emu.trap_off)
        self.assertEqual([v for v, t in self.chip(m, 0xA0)], list(range(70)))
        self.assertEqual(m.emu.timeouts, {})
        # until the program ends: the next one plays from a queue again
        m.program_end(VM_DOS)
        self.assertEqual(m.adi32(m.syms["EX_FMQ_Direct"]), 0)
        self.write(m, 0x20, 0x01)
        self.assertTrue(self.timed(m))

    def test_a_full_queue_loses_nothing(self):
        m = self.game()
        self.play(m, 1428.6, 1)
        for k in range(600):                        # 1200 port writes
            self.write(m, 0xA1, k & 0xFF)
        m.advance(50)
        self.assertEqual([v for v, t in self.chip(m, 0xA1)],
                         [k & 0xFF for k in range(600)])

    def test_delay_setting(self):
        for value, delay in (("100", 100000), ("999", 250000)):
            with self.subTest(value=value):
                m = self.game({("ES1869.VXD", "DosFMDelay"): value})
                times = self.play(m, 1428.6, 20)
                m.advance(300)
                got = self.chip(m, 0xA0)
                self.assertLessEqual(abs(got[0][1] - (times[0] + delay)),
                                     1500)

    def test_off_is_direct(self):
        m = self.machine()
        self.assertTrue(m.start({("ES1869.VXD", "DosFMDelay"): "0"}))
        self.assertEqual(m.emu.v86_hooks.get(8, []), [])
        self.write(m, 0x20, 0x01)
        self.assertFalse(self.timed(m))
        self.assertIn((VM_DOS, 0x388), m.emu.trap_off)
        self.assertEqual(m.emu.min_int, [])

    def test_the_device_or_the_vxd_goes(self):
        m = self.game()
        self.play(m, 1428.6, 10)
        for vm in (VM_SYS, VM_DOS, VM_DOS2):
            m.emu.run(m.syms["ESSREG_Node_Remove"], {"EBX": vm, "EDI": ADI})
        self.assertFalse(self.timed(m))
        self.assertEqual(m.emu.timeouts, {})
        self.assertEqual(m.emu.min_int, [])
        m = self.game()
        self.play(m, 1428.6, 10)
        m.emu.uc.mem_write(m.syms["AUDDRV_Dynamic_Exit"], b"\xF8\xC3")
        m.emu.run(m.syms["ESSREG_Dynamic_Exit"], {"EBX": VM_SYS})
        self.assertEqual(m.emu.v86_hooks.get(8, []), [])
        self.assertEqual(m.emu.timeouts, {})
        self.assertFalse(self.timed(m))


if __name__ == "__main__":
    unittest.main()
