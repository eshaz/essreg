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

import sbgame  # noqa: E402

if HAVE_UNICORN:
    from vxdemu import (VM_SYS, VM_DOS, VM_DOS2, ADI, ADI_FM,  # noqa: E402
                        ADI_FM_OWNER, ADI_FM_LAST, ADI_DSP_OWNER, DMA1,
                        DMA2, ADI_IRQ, ADI_DMA2_CHANNEL)
    from sbfronts import VxDCard, clocks, play16  # noqa: E402

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
        self.cmd(m, 0xE1)                       # a Sound Blaster 16's version
        self.assertEqual((self.read(m), self.read(m)), (0x04, 0x05))
        self.cmd(m, 0xE7)                       # the chip's own answer
        self.assertEqual((self.read(m), self.read(m)), (0x68, 0x89))
        self.cmd(m, 0xE1)                       # and now its version
        self.assertEqual((self.read(m), self.read(m)), (0x03, 0x01))
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

    def test_its_blocks_end_on_1_ms_interrupts(self):
        # VTD's timer period while the virtual Sound Blaster exists, so a
        # block's time-out comes when it's due and not at the timer's next
        # 55 ms tick; given back when it ends
        m = self.game()
        before = list(m.emu.min_int)
        self.take(m)
        self.assertEqual(m.emu.min_int, before + [1])
        m.program_end(VM_DOS)
        self.assertEqual(self.vsb(m), 0)
        self.assertEqual(m.emu.min_int, [])

    def test_its_mpu_401_interrupt_ends_physically(self):
        # the program also owns the MPU-401, on the shared IRQ: ESS's
        # handler delivered a physical interrupt to it (ADI+4 bit 1000h),
        # which its EOI ends at the PIC, as with ESS's driver
        m = self.game()
        self.take(m)
        m.emu.write32(ADI + 0x45, VM_DOS)
        m.emu.write32(ADI + 4, m.adi32(4) | 0x3000)
        m.eoi(VM_DOS)
        self.assertEqual(self.events(m, "eoi"), [VM_DOS])
        self.assertEqual(m.adi32(4) & 0x1000, 0)
        # without one pending, the virtual interrupt's EOI ends nothing
        m.eoi(VM_DOS)
        self.assertEqual(self.events(m, "eoi"), [VM_DOS])

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
        self.assertIn((VM_DOS, 0x22D), m.emu.trap_off)      # its ports again,
        self.assertNotIn((VM_DOS, 0x22C), m.emu.trap_off)   # with the front
        self.assertEqual(m.adi32(m.syms["EX_S16_VM"]), VM_DOS)
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
        # a Sound Blaster Pro game's mixer as the chip had it: read back
        # from the virtual Sound Blaster, and its stereo bit on the chip
        # again with the DSP; the levels are Windows' meanwhile and after.
        # (The Sound Blaster 16's front has no stereo bit: test_sb16front)
        m = self.game(ini={("ES1869.VXD", "SoundBlaster16"): "0"})
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
class SoundBlaster16Test(VxDBuilds, unittest.TestCase):
    """A DOS program written for the Sound Blaster 16 finds one in front of
    the chip (ESSREG_S16_*), and what only the VxD's front does: the front
    for a DOS VM alone, VDMAD's channels, the card's registers as the ADI
    has them, and the program's settings while a recording has the DSP.
    What the fronts do with the chip is test_sb16front's, and what the game
    sees is test_sb16conf's, both on sbfronts.VxDCard"""

    PLAY, MASKED = 0x58, 0x59           # auto-init, from memory; masked

    def game(self, ini=None, irq=5, dma=1, dma2=None, demand=True,
             mixer=None):
        """a DOS game resets the DSP, on a card at IRQ irq with Audio 1 on
        channel dma and Audio 2 on dma2 (None: none, ESS's FFh), ESS's
        demand transfers on unless Single Mode DMA, the chip's mixer as
        `mixer` has it"""
        m = self.machine()
        if ini is not None:
            m.start(ini)
        for reg, v in (mixer or {}).items():
            m.hw.mixer[reg] = v
        m.emu.uc.mem_write(ADI + ADI_IRQ, bytes((irq, dma)))
        m.emu.uc.mem_write(ADI + ADI_DMA2_CHANNEL,
                           bytes((0xFF if dma2 is None else dma2,)))
        m.emu.write32(ADI + 0x4D, DMA1)         # the channels' handles
        m.emu.write32(ADI + 0x7A, DMA2)
        if demand:
            m.emu.uc.mem_write(ADI + 0x13, b"\x01")     # ESS's flag 10000h
        m.outb(VM_DOS, 0x226, 1)
        m.outb(VM_DOS, 0x226, 0)
        self.assertEqual(m.inb(VM_DOS, 0x22A), 0xAA)
        self.assertEqual(m.adi32(ADI_DSP_OWNER), VM_DOS)
        return m

    def card(self, **kw):
        """the game on the chip model and VDMAD's model, after its reset"""
        c = VxDCard(**kw)
        self.assertTrue(sbgame.reset(c))
        return c

    def front(self, m):
        return m.adi32(m.syms["EX_S16_VM"])

    def cmd(self, m, *data):
        for b in data:
            self.assertEqual(m.inb(VM_DOS, 0x22C) & 0x80, 0)
            m.outb(VM_DOS, 0x22C, b)

    def read(self, m):
        self.assertTrue(m.inb(VM_DOS, 0x22E) & 0x80)
        return m.inb(VM_DOS, 0x22A)

    def mixer(self, m, reg, value=None):
        m.outb(VM_DOS, 0x224, reg)
        if value is None:
            return m.inb(VM_DOS, 0x225)
        m.outb(VM_DOS, 0x225, value)
        return None

    def test_the_front_for_a_dos_program(self):
        m = self.game()
        self.assertEqual(self.front(m), VM_DOS)
        for port in (0x224, 0x225, 0x226, 0x22A, 0x22C, 0x22E, 0x22F):
            self.assertNotIn((VM_DOS, port), m.emu.trap_off, hex(port))
        for port in (0x227, 0x22B, 0x22D):
            self.assertIn((VM_DOS, port), m.emu.trap_off, hex(port))
        # not for Windows, nor with the setting off
        m = self.machine()
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(self.front(m), 0)
        m = self.game(ini={("ES1869.VXD", "SoundBlaster16"): "0"})
        self.assertEqual(self.front(m), 0)
        self.assertIn((VM_DOS, 0x22C), m.emu.trap_off)
        self.assertEqual(m.emu.dma_virt, {})

    def test_the_16_bit_channel(self):
        """the VxD takes channel 5 from VDMAD once, the reads of its count
        told; the program's programming goes to Audio 1's channel in bytes
        and demand transfers, its count is Audio 1's in words, and the
        program's end lets go of both"""
        c = self.card()
        m = c.m
        handle, _callback = m.channel16()
        self.assertEqual(handle, 0x7205)
        self.assertEqual(m.emu.dma_callbacks[handle], 1)
        self.assertEqual(sbgame.mixer(c, 0x81), 0x22)
        play16(c)
        self.assertEqual(c.chip.dma[1], {"mask": False, "mode": 0x19,
                                         "addr": 0x4680, "count": 0x1FFF,
                                         "page": 0x02})
        c.wait(5000)
        sbgame.write(c, 0xD5)                   # paused: the count stays
        c.wait(1000)
        left = c.chip.dmac.ch[1].count
        self.assertLess(left, 0x1FFF)
        self.assertEqual(sbgame.dma_count(c, 5), ((left + 1) >> 1) - 1)
        # masked: Audio 1's channel too; unmasked: carried over again
        c.out(0xD4, 0x05)
        self.assertTrue(c.chip.dma[1]["mask"])
        c.out(0xD4, 0x01)
        self.assertFalse(c.chip.dma[1]["mask"])
        # the program ends: Audio 1's channel masked, the reads not told
        m.program_end(VM_DOS)
        self.assertEqual(self.front(m), 0)
        self.assertTrue(c.chip.dma[1]["mask"])
        self.assertEqual(m.emu.dma_callbacks[handle], 0)
        self.assertEqual(m.adi32(m.syms["EX_S16_Last"]), 0)
        # the VxD lets go of the channel when it unloads (ESS's exit left
        # out, as its init is)
        m.vm_close(VM_DOS)
        m.emu.uc.mem_write(m.syms["AUDDRV_Dynamic_Exit"], b"\xF8\xC3")
        m.emu.run(m.syms["AUDDRV_Control"], {"EAX": 0x1C, "EBX": VM_SYS})
        self.assertEqual(m.emu.dma_virt, {})
        self.assertEqual(c.errors, [])

    def test_a_channel_programmed_before(self):
        """a program that programmed its 16-bit channel before the front
        had it: the transfer takes the channel's state from VDMAD"""
        c = VxDCard()
        sbgame.program_dma(c, 5, 0x24680, 8192)
        self.assertEqual(c.m.emu.dma_virt, {})
        self.assertTrue(sbgame.reset(c))
        sbgame.write(c, 0x41, 0xAC, 0x44, 0xB6, 0x30, 0xFF, 0x07)
        self.assertEqual(c.chip.dma[1], {"mask": False, "mode": 0x19,
                                         "addr": 0x4680, "count": 0x1FFF,
                                         "page": 0x02})
        c = VxDCard()                           # masked: Audio 1's waits
        sbgame.program_dma(c, 5, 0x24680, 8192)
        c.out(0xD4, 0x05)
        self.assertTrue(sbgame.reset(c))
        sbgame.write(c, 0x41, 0xAC, 0x44, 0xB6, 0x30, 0xFF, 0x07)
        self.assertTrue(c.chip.dma[1]["mask"])
        c.out(0xD4, 0x01)
        self.assertFalse(c.chip.dma[1]["mask"])

    def test_single_mode_dma(self):
        m = self.game(demand=False)
        self.cmd(m, 0x41, 0xAC, 0x44, 0xB6, 0x30, 0xFF, 0x0F)
        m.dma16(VM_DOS, self.PLAY)
        self.assertEqual(m.emu.dma_states[0][4], 0x58)  # the program's mode
        c = self.card(demand=False)
        play16(c)
        self.assertEqual(c.chip.dma[1]["mode"], 0x59)
        self.assertEqual(c.chip.ctrl[0xB9], 0x00)

    def test_audio_2s_channel_as_the_16_bit_one(self):
        m = self.game(dma2=5)
        self.assertEqual(m.channel16(), (DMA2, m.syms["L1_0870"]))
        self.assertEqual(m.emu.dma_virt, {})    # not taken from ESS's code
        self.assertEqual(m.emu.dma_callbacks[DMA2], 1)
        self.cmd(m, 0x41, 0xAC, 0x44, 0xB6, 0x30, 0xFF, 0x0F)
        m.dma16(VM_DOS, self.PLAY)
        self.assertEqual(m.emu.dma_states[-1][:2], (DMA1, VM_DOS))
        self.assertEqual(m.emu.dma_default, [(DMA1, VM_DOS)])
        m.emu.dma_count = 0x0801 + 0x123
        self.assertEqual(m.dma16_count(VM_DOS), 0x0802)
        # Windows' own reads of its count change nothing
        n = len(m.emu.dma_default)
        m.emu.current_vm = VM_SYS
        m.emu.run(m.syms["L1_0870"], {"EAX": DMA2, "EBX": VM_SYS, "EDX": 1})
        self.assertEqual(len(m.emu.dma_default), n)
        m.program_end(VM_DOS)
        self.assertEqual(m.emu.dma_callbacks[DMA2], 0)
        # another channel for the Sound Blaster 16: the VxD's own
        m = self.game(ini={("ES1869.VXD", "SoundBlaster16DMA"): "7"}, dma2=5)
        self.assertEqual(m.channel16()[0], 0x7207)
        self.assertEqual(self.mixer(m, 0x81), 0x82)
        m = self.game(ini={("ES1869.VXD", "SoundBlaster16DMA"): "0"})
        self.assertIsNone(m.channel16())
        self.assertEqual(self.mixer(m, 0x81), 0x02)
        self.cmd(m, 0x41, 0xAC, 0x44, 0xB6, 0x30, 0xFF, 0x0F)   # still plays
        self.assertEqual(m.hw.ctrl[0xB8], 0x05)

    def test_the_channel_the_program_picks(self):
        """mixer 81h: the channel SoundBlaster16DMA names, or none, when
        16-bit sound plays on the 8-bit channel as the program programs
        it; another channel only reads back"""
        c = self.card()
        sbgame.mixer(c, 0x81, 0x02)
        self.assertEqual(sbgame.mixer(c, 0x81), 0x02)
        play16(c, ch=1)
        self.assertEqual(c.chip.dma[1], {"mask": False, "mode": 0x59,
                                         "addr": 0x4680, "count": 0x1FFF,
                                         "page": 0x02})
        self.assertEqual(c.flags() & 0x10, 0x10)       # 16-bit, on channel 1
        sbgame.mixer(c, 0x81, 0x42)
        self.assertEqual(sbgame.mixer(c, 0x81), 0x42)
        self.assertEqual(c.m.adi8(c.m.syms["EX_S16_Chan"]), 0)
        sbgame.mixer(c, 0x81, 0x22)
        self.assertEqual(sbgame.mixer(c, 0x81), 0x22)
        self.assertEqual(c.m.adi8(c.m.syms["EX_S16_Chan"]), 5)
        self.assertEqual(c.chip.mixer[0x81], 0)        # never the chip's

    def test_the_card_registers(self):
        """80h and 81h say the card's IRQ and channels as the ADI has them,
        with the CT1745's high bits"""
        for irq, bits in ((5, 0xF2), (7, 0xF4), (9, 0xF1), (10, 0xF8),
                          (11, 0xF0)):
            c = self.card(irq=irq)
            self.assertEqual(sbgame.mixer(c, 0x80), bits, irq)
        for dma8, bits in ((0, 0x21), (1, 0x22), (3, 0x28)):
            c = self.card(dma8=dma8)
            self.assertEqual(sbgame.mixer(c, 0x81), bits, dma8)
        c = self.card(ini={("ES1869.VXD", "SoundBlaster16DMA"): "6"})
        self.assertEqual(sbgame.mixer(c, 0x81), 0x42)
        # the index port reads back the program's index
        sbgame.mixer(c, 0x22, 0xEE)
        self.assertEqual((c.chip.index, c.inp(0x224)), (0x22, 0x22))

    def test_the_dacs_modes(self):
        """mixer 71h at a transfer: bit 5 off, so that A1h is the front's,
        and the Audio 1 filter bypassed (bit 2) unless Audio1Filter=1"""
        c = self.card(mixer={0x71: 0x20})
        play16(c)
        self.assertEqual(c.chip.mixer[0x71] & 0x24, 0x04)
        c = self.card(ini={("ES1869.DRV", "Audio1Filter"): "1"},
                      mixer={0x71: 0x20})
        bit2 = c.chip.mixer[0x71] & 0x04
        play16(c)
        self.assertEqual(c.chip.mixer[0x71] & 0x24, bit2)

    # Windows' levels when a game starts: master 3Ah, muted on the right,
    # wave ABh, FM CDh, CD 12h, line 34h, mic 50h, PC speaker 5, the record
    # source line; and the chip's own registers a mixer reset would clear
    WINDOWS_MIXER = {0x60: 0x3A, 0x62: 0x7A, 0x14: 0xAB, 0x36: 0xCD,
                     0x38: 0x12, 0x3E: 0x34, 0x1A: 0x55, 0x3C: 0x05,
                     0x1C: 0x16, 0x50: 0x0C, 0x52: 0x2A, 0x71: 0x2E,
                     0x7C: 0x99, 0x0E: 0x22, 0x00: 0x77}

    def test_the_mixer_a_program_finds(self):
        """a Sound Blaster 16 shows the levels it has: the chip's, in the
        bits the CT1745 has (the PC speaker's two of the chip's three)"""
        m = self.game(mixer=self.WINDOWS_MIXER)
        view = [self.mixer(m, r) for r in range(0x30, 0x48)]
        self.assertEqual(view, [
            0xE8, 0x00,                 # master, the right muted
            0xA0, 0xB0, 0xC0, 0xD0,     # voice, MIDI
            0x10, 0x20, 0x30, 0x40,     # CD, line
            0x50, 0x80,                 # mic, PC speaker
            0x1F, 0x10, 0x08,           # the switches: line records
            0, 0, 0, 0, 0, 0x80, 0x80, 0x80, 0x80])
        # writing back what it read changes nothing but the speaker's lost
        # bit; each register goes to its own, so the muted right master
        # stays muted
        before = list(m.hw.mixer)
        for r, v in zip(range(0x30, 0x48), view):
            if r != 0x31:               # the muted master can't come back
                self.mixer(m, r, v)     # muted
        self.assertEqual([(r, v) for r, v in enumerate(m.hw.mixer)
                          if v != before[r]], [(0x3C, 0x04)])

    def test_a_mixer_reset_is_the_sound_blaster_16s(self):
        """a program's mixer reset gives the chip the Sound Blaster 16's
        reset levels and leaves the chip's other registers"""
        m = self.game(mixer=self.WINDOWS_MIXER)
        self.mixer(m, 0x30, 0x10)
        self.mixer(m, 0x00, 0x00)
        self.assertEqual(m.hw.index, 0x00)  # the program's index kept
        hw = m.hw.mixer
        self.assertEqual(hw[0x00], 0x77)                # never the chip's reset
        self.assertEqual(self.mixer(m, 0x30), 0xC0)     # the view's reset
        self.assertEqual(self.mixer(m, 0x3C), 0x1F)
        self.assertEqual((hw[0x60], hw[0x62]), (0x30, 0x30))  # C0h, 6 bits
        self.assertEqual((hw[0x14], hw[0x36]), (0xCC, 0xCC))
        self.assertEqual((hw[0x38], hw[0x3E], hw[0x1A], hw[0x3C]),
                         (0, 0, 0, 0))
        self.assertEqual(hw[0x1C], 0x15)    # mic, CD and line: the record mix
        self.assertEqual(hw[0x0E], 0x20)    # the Sound Blaster Pro's stereo off
        for reg in (0x50, 0x52, 0x71, 0x7C):
            self.assertEqual(hw[reg], self.WINDOWS_MIXER[reg], hex(reg))

    def test_a_recording_keeps_the_view(self):
        """a recording takes the DSP: the view keeps the program's levels,
        and they go back to the chip when the program has the DSP again"""
        m = self.game(mixer=self.WINDOWS_MIXER)
        self.mixer(m, 0x26, 0x33)                       # MIDI
        self.assertEqual(m.hw.mixer[0x36], 0x33)
        m.api(VM_SYS, 0x040D)
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(m.hw.mixer[0x36], 0xCD)        # Windows' level
        m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(self.front(m), VM_DOS)
        self.assertEqual(m.hw.mixer[0x36], 0x33)        # its level again
        self.assertEqual(self.mixer(m, 0x26), 0x33)

    def test_the_chips_own_volumes(self):
        """a program that sets the chip's own registers: they reach the
        chip as they come, and the view follows them"""
        m = self.game(mixer=self.WINDOWS_MIXER)
        n = len(m.hw.log)
        for reg, v in ((0x60, 0x20), (0x14, 0x5A), (0x1A, 0x99),
                       (0x1C, 0x12)):
            self.mixer(m, reg, v)
            self.assertEqual(m.hw.mixer[reg], v)
        self.assertEqual([e for e in m.hw.log[n:] if e[0] == "in"], [])
        self.assertEqual([self.mixer(m, r) for r in
                          (0x30, 0x31, 0x32, 0x33, 0x3A, 0x3D, 0x3E)],
                         [0x80, 0x00, 0x50, 0xA0, 0x90, 0x04, 0x02])
        # a recording takes the DSP: the view keeps the program's levels
        self.mixer(m, 0x14, 0x33)
        m.api(VM_SYS, 0x040D)
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(m.hw.mixer[0x14], 0xAB)        # Windows' level
        m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(self.front(m), VM_DOS)
        self.assertEqual(m.hw.mixer[0x14], 0x33)        # its level again

    def test_a_returning_program_keeps_its_settings(self):
        # a recording takes the DSP and lets go of it: the program's rate,
        # its mixer view and that it knows the chip stay with its front
        m = self.game()
        self.cmd(m, 0x41, 0xAC, 0x44)
        self.mixer(m, 0x30, 0xFF)
        m.api(VM_SYS, 0x040D)
        self.assertEqual(self.front(m), 0)
        m.api(VM_SYS, 0x0002, EBX=1, EAX=WIN_AUDIO)
        m.api(VM_SYS, 0x0003, EBX=1, EAX=WIN_AUDIO)
        self.assertEqual(self.front(m), VM_DOS)
        self.assertEqual(self.mixer(m, 0x30), 0xF8)     # 5 bits
        self.assertEqual(m.hw.mixer[0x60], 0x3E)        # its level again
        self.cmd(m, 0xB6, 0x30, 0xFF, 0x0F)
        self.assertEqual(m.hw.ctrl[0xA1], clocks(248)[0])  # 44100 Hz
        # a new program in the VM starts over, from Windows' levels and the
        # DSP's cold boot
        m.program_end(VM_DOS)
        m.hw.mixer[0x60] = 0x2D
        m.outb(VM_DOS, 0x226, 1)
        m.outb(VM_DOS, 0x226, 0)
        self.assertEqual(self.mixer(m, 0x30), 0xB0)
        self.cmd(m, 0xB6, 0x30, 0xFF, 0x0F)
        self.assertEqual(m.hw.ctrl[0xA1], clocks(0x38)[0])  # 9958 Hz

    def test_the_virtual_sound_blaster_is_one_too(self):
        # a recording took the DSP: the program's Sound Blaster 16 commands
        # time its transfers and interrupts the same way
        m = self.game()
        m.api(VM_SYS, 0x040D)
        mark = len(m.emu.vpicd)

        def events(what="set"):
            return [vm for w, irq, vm in m.emu.vpicd[mark:]
                    if w == what and irq == 0x7001]
        self.cmd(m, 0xE1)
        self.assertEqual((self.read(m), self.read(m)), (0x04, 0x05))
        self.assertEqual(self.mixer(m, 0x80), 0xF2)
        self.assertEqual(self.mixer(m, 0x81), 0x22)
        self.cmd(m, 0x41, 0xAC, 0x44)           # 4096 samples of 2 bytes at
        self.cmd(m, 0xB6, 0x30, 0xFF, 0x0F)     # 176400 B/s: 46.4 ms
        m.advance(46)
        self.assertEqual(events(), [])
        m.advance(1)
        self.assertEqual(events(), [VM_DOS])
        self.assertEqual(self.mixer(m, 0x82), 0x02)
        m.inb(VM_DOS, 0x22F)                    # the 16-bit acknowledge
        self.assertEqual(events("clear"), [VM_DOS])
        self.assertEqual(self.mixer(m, 0x82), 0x00)
        self.cmd(m, 0xD5)                       # paused, and on again
        m.advance(500)
        self.assertEqual(len(events()), 1)
        self.cmd(m, 0xD6)
        m.advance(50)
        self.assertEqual(len(events()), 2)
        self.cmd(m, 0xD9)
        m.advance(500)
        self.assertEqual(len(events()), 3)
        self.cmd(m, 0xC6, 0x00, 0xFF, 0x07)     # 2048 bytes: 46.4 ms, 8-bit
        m.advance(47)
        self.assertEqual(len(events()), 4)
        self.assertEqual(self.mixer(m, 0x82), 0x01)
        m.inb(VM_DOS, 0x22E)
        self.cmd(m, 0xF3)                       # the 16-bit interrupt
        self.assertEqual(len(events()), 5)
        self.assertEqual(self.mixer(m, 0x82), 0x02)
        # a program that knows the chip: Bxh and Cxh are its registers
        m = self.game()
        self.cmd(m, 0xC6, 0xA1, 0x6E)
        m.api(VM_SYS, 0x040D)
        self.cmd(m, 0xE1)
        self.assertEqual((self.read(m), self.read(m)), (0x03, 0x01))
        self.cmd(m, 0xC6, 0xB6, 0x80, 0xC0, 0xB6)
        self.assertEqual(self.read(m), 0x80)


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

    def test_a_burst_before_the_first_tick_stays_on_the_queue(self):
        # a program sets up every register as soon as it finds the chip,
        # before its timer's first tick: within the timer's default 55 ms
        # tick of the start, its writes aren't off its ticks
        m = self.game()
        for reg in range(0x20, 0xA0):
            self.write(m, reg, 0)
            m.hw.clock.advance(10)
        self.assertTrue(self.timed(m))
        self.assertEqual(m.adi32(m.syms["EX_FMQ_Direct"]), 0)
        self.assertNotIn((VM_DOS, 0x388), m.emu.trap_off)
        # then its ticks come, and its music plays a delay after each
        times = self.play(m, 1428.6, 64)
        m.advance(50)
        got = self.chip(m, 0xA0)
        self.assertEqual([v for v, t in got], list(range(64)))
        for (v, t), tick in zip(got, times):
            self.assertLessEqual(abs(t - (tick + self.DELAY)), 1500)
        # writes that go on without a tick past that are off its ticks
        m = self.game()
        m.hw.clock.advance(40000)
        for k in range(70):
            self.write(m, 0xA0, k)
            m.hw.clock.advance(10)
        self.assertFalse(self.timed(m))

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
