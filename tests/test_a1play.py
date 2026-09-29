# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Test the Audio 1 player of build/ES1869.DRV (src/es1869/a1play.asm,
a1wave.asm): a second wave-out device through the Audio 1 DAC, and device
0 falling back to it when Audio 2 is busy.

The driver runs in tests/drvemu.py against a model of the chip. Its
interrupts go through ESS's own handler, and the bytes the model's DMA
takes from the ring are compared with what the program wrote.
"""

import os
import struct
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_es1869drv  # noqa: E402

try:
    import drvemu
    from drvemu import (DrvEmu, WODM_GETNUMDEVS, WODM_GETDEVCAPS,
                        WODM_CLOSE, WODM_PAUSE, WODM_RESTART, WODM_RESET,
                        WODM_BREAKLOOP, WODM_GETVOLUME, WODM_SETVOLUME,
                        WODM_PREPARE, WOM_OPEN, WOM_CLOSE, WHDR_DONE,
                        WHDR_PREPARED, WHDR_BEGINLOOP, WHDR_ENDLOOP,
                        WHDR_INQUEUE, TIME_BYTES, TIME_SAMPLES, DEVNODE)
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

A1_MAGIC = 0xA1A1
OPT_A1_DEVICE, OPT_A1_SHARED, OPT_A1_FILTER = 0x0001, 0x0002, 0x0004
OPT_DUAL = 0x0008
OPT_A2_4X, OPT_A2_FILTER, OPT_READ = 0x0100, 0x0200, 0x8000
MMSYSERR_BADDEVICEID, MMSYSERR_NOTENABLED = 2, 3
MMSYSERR_ALLOCATED, MMSYSERR_NOTSUPPORTED = 4, 8
MMSYSERR_INVALPARAM = 11
WAVERR_BADFORMAT, WAVERR_STILLPLAYING, WAVERR_UNPREPARED = 32, 33, 34
A1_BASE = 0x132
A1_STATE, A1_BLOCK, A1_RING = A1_BASE + 0x12, A1_BASE + 0x1A, A1_BASE + 0x1C


def pattern(n, seed=1):
    """n bytes that don't repeat within a ring"""
    out = bytearray()
    x = seed
    while len(out) < n:
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out += struct.pack("<H", (x >> 8) & 0xFFFF)
    return bytes(out[:n])


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class A1Case(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixed = build_es1869drv.build(True)
        cls.stock = build_es1869drv.build(False)
        cls.syms = build_es1869drv.symbols(True)

    def emu(self, opts=None, stock=False):
        e = DrvEmu(self.stock if stock else self.fixed,
                   None if stock else self.syms)
        if opts is not None:
            e.w16(drvemu.DGROUP, self.syms["es_opts"][1], opts)
        return e

    def opened(self, **kw):
        e = self.emu(kw.pop("opts", None))
        r, user = e.open(**kw)
        self.assertEqual(r, 0)
        return e, user

    def play(self, e, blocks, step=None):
        """the chip takes step bytes (a block) and interrupts, blocks times"""
        step = step or e.dev16(A1_BLOCK)
        for _i in range(blocks):
            e.dma(step)
            e.interrupt()


class DeviceTest(A1Case):
    def test_two_devices(self):
        self.assertEqual(self.emu().wod(0, WODM_GETNUMDEVS, 0, DEVNODE), 2)

    def test_ess_driver_has_one(self):
        e = self.emu(stock=True)
        self.assertEqual(e.wod(0, WODM_GETNUMDEVS, 0, DEVNODE), 1)

    def test_one_with_the_key_off(self):
        e = self.emu(OPT_A1_SHARED)
        self.assertEqual(e.wod(0, WODM_GETNUMDEVS, 0, DEVNODE), 1)

    def test_not_on_a_16_bit_channel(self):
        e = self.emu()
        e.set_dev16(0x06, 5)
        self.assertEqual(e.wod(0, WODM_GETNUMDEVS, 0, DEVNODE), 1)

    def test_caps(self):
        e = self.emu()
        caps = e.alloc(b"\0" * 0x30)
        ex = e.alloc(struct.pack("<II", 0x30, caps))
        self.assertEqual(e.wod(1, WODM_GETDEVCAPS, 0, ex, DEVNODE), 0)
        mid, pid, ver = struct.unpack("<HHH", e.rd(e.far_lin(caps), 6))
        name = e.rd(e.far_lin(caps) + 6, 32).split(b"\0")[0]
        fmts, chans, support = struct.unpack(
            "<IHI", e.rd(e.far_lin(caps) + 38, 10))
        self.assertEqual((mid, pid, ver), (0x2E, 0x28, 0x404))
        self.assertEqual(name, b"ESS AudioDrive Audio 1 (220)")
        self.assertEqual((fmts, chans, support), (0xFFF, 2, 0x2C))

    def test_device_0_caps_are_ess(self):
        caps = []
        for stock in (True, False):
            e = self.emu(stock=stock)
            buf = e.alloc(b"\0" * 0x30)
            ex = e.alloc(struct.pack("<II", 0x30, buf))
            self.assertEqual(e.wod(0, WODM_GETDEVCAPS, 0, ex, DEVNODE), 0)
            caps.append(e.rd(e.far_lin(buf), 0x30))
        self.assertEqual(caps[0], caps[1])
        self.assertIn(b"ESS AudioDrive Playback (220)", caps[1])

    def test_caps_short_buffer(self):
        e = self.emu()
        caps = e.alloc(b"\xEE" * 0x30)
        ex = e.alloc(struct.pack("<II", 8, caps))
        self.assertEqual(e.wod(1, WODM_GETDEVCAPS, 0, ex, DEVNODE), 0)
        self.assertEqual(e.rd(e.far_lin(caps) + 8, 4), b"\xEE" * 4)

    def test_device_2_is_ess_answer(self):
        e = self.emu()
        self.assertEqual(e.open(dev_id=2)[0], MMSYSERR_BADDEVICEID)


class OpenTest(A1Case):
    def test_open_16_bit_stereo(self):
        e, user = self.opened(rate=44100, channels=2, bits=16)
        self.assertEqual(user >> 16, A1_MAGIC)
        self.assertEqual(user & 0xFFFF, drvemu.DEV_OFF + A1_BASE)
        self.assertEqual(e.dev8(0x5D), 1)           # the Audio 1 user
        self.assertIn((0x0002, 1), e.vxd_calls)     # the DSP acquired
        self.assertEqual(e.callbacks[-1][:2], (WOM_OPEN, 0x11112222))
        c = e.chip
        self.assertTrue(c.ext)
        self.assertTrue(c.a1_dac)                   # D1h
        self.assertEqual(c.ext_errors, [])
        self.assertEqual(c.regs[0xA8] & 3, 1)       # stereo
        self.assertEqual(c.regs[0xB9], 2)           # demand, as ESS records
        self.assertEqual(c.regs[0xB6], 0x00)
        writes = [(r, v) for k, r, v in
                  [x for x in c.log if x[0] == "reg"] if r == 0xB7]
        self.assertEqual(writes, [(0xB7, 0x71), (0xB7, 0xBC)])
        self.assertEqual(c.regs[0xB1] & 0x50, 0x50)
        self.assertEqual(c.regs[0xB2] & 0x50, 0x50)
        self.assertTrue(c.mixer[0x71] & 0x04)       # the filter bypassed
        block = e.dev16(A1_BLOCK)
        self.assertEqual(e.dev16(A1_RING), 2 * block)
        self.assertEqual(block % 4, 0)
        self.assertLessEqual(2 * block, drvemu.BUF_SIZE)

    def test_open_8_bit_mono(self):
        e, _u = self.opened(rate=11025, channels=1, bits=8)
        c = e.chip
        self.assertEqual(c.regs[0xA8] & 3, 2)
        self.assertEqual(c.regs[0xB6], 0x80)
        writes = [v for k, r, v in
                  [x for x in c.log if x[0] == "reg"] if r == 0xB7]
        self.assertEqual(writes, [0x51, 0xD0])

    def test_rate_as_ess_records(self):
        # rate_setup (6:1FF4) with the playback filter table
        e, _u = self.opened(rate=22050, channels=2, bits=16)
        a1 = [v for k, r, v in [x for x in e.chip.log if x[0] == "reg"]
              if r == 0xA1]
        self.assertEqual(len(a1), 1)
        self.assertIn(0xA2, [x[1] for x in e.chip.log if x[0] == "reg"])

    def test_filter_with_the_key(self):
        e, _u = self.opened(opts=OPT_A1_DEVICE | OPT_A1_FILTER)
        self.assertFalse(e.chip.mixer[0x71] & 0x04)

    def test_bad_formats(self):
        for kw in ({"channels": 3}, {"bits": 12}, {"rate": 3999},
                   {"rate": 49001}):
            with self.subTest(**kw):
                e = self.emu()
                self.assertEqual(e.open(**kw)[0], WAVERR_BADFORMAT)
                self.assertEqual(e.dev8(0x5D), 0)

    def test_query(self):
        e = self.emu()
        self.assertEqual(e.open(flags=1)[0], 0)
        self.assertEqual(e.dev8(0x5D), 0)
        self.assertEqual(e.callbacks, [])

    def test_one_at_a_time(self):
        e, _u = self.opened()
        self.assertEqual(e.open()[0], MMSYSERR_ALLOCATED)

    def test_not_while_recording(self):
        e = self.emu()
        e.set_dev8(0x5D, 2)
        e.set_dev8(0x6F, 1)
        self.assertEqual(e.open()[0], MMSYSERR_ALLOCATED)
        self.assertEqual(e.dev8(0x5D), 2)

    def test_not_while_the_vxd_has_it(self):
        e = self.emu()
        e.w8(drvemu.DGROUP, drvemu.DEV_OFF + 0x127, 1)
        self.assertEqual(e.open()[0], MMSYSERR_ALLOCATED)

    def test_not_when_the_dsp_is_refused(self):
        e = self.emu()
        e.vxd_fail.add(0x0002)
        self.assertEqual(e.open()[0], MMSYSERR_ALLOCATED)
        self.assertEqual(e.dev8(0x5D), 0)

    def test_recording_refused_while_it_plays(self):
        e, _u = self.opened()
        self.assertEqual(e.call((drvemu.SEG_PARA[4], 0x00E0),
                                (drvemu.DEV_OFF,)) & 0xFFFF, 0xFFFF)


class PlayTest(A1Case):
    def test_plays_what_was_written(self):
        e, user = self.opened(rate=22050)
        block = e.dev16(A1_BLOCK)
        data = pattern(block * 7 + 100)
        hdrs = [e.header(data[i:i + 3000])
                for i in range(0, len(data), 3000)]
        for h in hdrs:
            self.assertEqual(e.write(user, h), 0)
            self.assertEqual(e.hdr_flags(h) & (WHDR_INQUEUE | WHDR_DONE),
                             WHDR_INQUEUE)
        c = e.chip
        d = c.dma[drvemu.A1_DMA]
        self.assertFalse(d["mask"])
        self.assertEqual(d["mode"], 0x19)           # demand, auto, read
        self.assertEqual(d["count"], 2 * block - 1)
        self.assertEqual(c.regs[0xB8] & 0x0F, 0x05)
        self.assertEqual((c.regs[0xA5] << 8) | c.regs[0xA4],
                         (0x10000 - block) & 0xFFFF)
        self.play(e, 12)
        self.assertEqual(bytes(e.played[:len(data)]), data)
        self.assertEqual(set(e.played[len(data):]), {0})
        self.assertEqual(e.done(), hdrs)
        for h in hdrs:
            self.assertEqual(e.hdr_flags(h) & (WHDR_INQUEUE | WHDR_DONE),
                             WHDR_DONE)
        self.assertEqual(e.getpos(user)[1:], (TIME_BYTES, len(data)))

    def test_done_only_once_played(self):
        e, user = self.opened(rate=22050)
        block = e.dev16(A1_BLOCK)
        first, second = e.header(pattern(block)), e.header(pattern(block, 2))
        e.write(user, first)
        e.write(user, second)
        e.dma(block - 4)
        e.interrupt()
        self.assertEqual(e.done(), [])
        e.dma(4)
        e.interrupt()
        self.assertEqual(e.done(), [first])
        e.dma(block)
        e.interrupt()
        self.assertEqual(e.done(), [first, second])

    def test_eight_bit_silence(self):
        e, user = self.opened(rate=11025, channels=1, bits=8)
        block = e.dev16(A1_BLOCK)
        data = pattern(block // 2)
        e.write(user, e.header(data))
        self.play(e, 4)
        self.assertEqual(bytes(e.played[:len(data)]), data)
        self.assertEqual(set(e.played[len(data):]), {0x80})

    def test_huge_buffer(self):
        # a header whose data crosses a 64 KB segment boundary, as a huge
        # pointer: the next part is __AHINCR paragraphs on
        e, user = self.opened(rate=44100)
        data = pattern(0x11000)
        e.app = (e.app + 0xFFFF) & ~0xFFFF
        buf = e.alloc(b"")
        lin = e.far_lin(buf)
        e.wr(lin + 0xF000, data)
        ptr = ((lin >> 4) << 16) | 0xF000
        hdr = e.alloc(struct.pack("<IIIIIIII", ptr, len(data), 0, 0,
                                  WHDR_PREPARED, 0, 0, 0))
        e.write(user, hdr)
        self.play(e, len(data) // e.dev16(A1_BLOCK) + 3)
        self.assertEqual(bytes(e.played[:len(data)]), data)
        self.assertEqual(e.done(), [hdr])

    def test_underrun_and_gap(self):
        e, user = self.opened(rate=22050)
        block = e.dev16(A1_BLOCK)
        one, two = pattern(block // 2), pattern(block, 3)
        e.write(user, e.header(one))
        self.play(e, 5)                             # silence plays
        before = e.getpos(user)[2]
        self.assertEqual(before, len(one))
        start = len(e.played)
        e.write(user, e.header(two))
        self.play(e, 4)
        out = bytes(e.played[start:])
        at = out.find(two[:64])
        self.assertGreater(at, 0)
        self.assertEqual(out[at:at + len(two)], two)
        self.assertEqual(set(out[:at]), {0})
        self.assertEqual(e.getpos(user)[2], len(one) + len(two))

    def test_position_never_goes_back(self):
        e, user = self.opened(rate=8000, channels=1, bits=8)
        block = e.dev16(A1_BLOCK)
        last = 0
        for n in (block // 3, block * 2, 17, block):
            e.write(user, e.header(pattern(n, n)))
            for _i in range(6):
                e.dma(block // 3)
                if _i % 2:
                    e.interrupt()
                pos = e.getpos(user)[2]
                self.assertGreaterEqual(pos, last)
                last = pos

    def test_samples(self):
        e, user = self.opened(rate=22050, channels=2, bits=16)
        e.write(user, e.header(pattern(4000)))
        self.play(e, 3)
        self.assertEqual(e.getpos(user, TIME_SAMPLES)[1:],
                         (TIME_SAMPLES, 1000))
        self.assertEqual(e.getpos(user, 0x20)[1:], (TIME_SAMPLES, 1000))

    def test_getpos_size(self):
        e, user = self.opened()
        mmt = e.alloc(b"\0" * 8)
        self.assertEqual(e.wod(0, drvemu.WODM_GETPOS, user, mmt, 6),
                         MMSYSERR_INVALPARAM)

    def test_unprepared(self):
        e, user = self.opened()
        h = e.header(b"\0" * 16, flags=0)
        self.assertEqual(e.write(user, h), WAVERR_UNPREPARED)

    def test_loop(self):
        e, user = self.opened(rate=22050)
        a, b, c = pattern(1000, 5), pattern(700, 6), pattern(300, 7)
        ha = e.header(a, flags=WHDR_PREPARED | WHDR_BEGINLOOP, loops=3)
        hb = e.header(b, flags=WHDR_PREPARED | WHDR_ENDLOOP)
        hc = e.header(c)
        for h in (ha, hb, hc):
            e.write(user, h)
        self.play(e, 6)
        want = (a + b) * 3 + c
        self.assertEqual(bytes(e.played[:len(want)]), want)
        self.assertEqual(e.done(), [ha, hb, hc])
        self.assertEqual(e.getpos(user)[2], len(want))

    def test_breakloop(self):
        e, user = self.opened(rate=22050)
        block = e.dev16(A1_BLOCK)
        a = pattern(block, 8)
        ha = e.header(a, flags=WHDR_PREPARED | WHDR_BEGINLOOP |
                      WHDR_ENDLOOP, loops=1000)
        e.write(user, ha)
        self.play(e, 4)
        self.assertEqual(e.done(), [])
        self.assertEqual(e.wod(0, WODM_BREAKLOOP, user), 0)
        self.play(e, 4)
        self.assertEqual(e.done(), [ha])
        out = bytes(e.played)
        end = out.rstrip(b"\0")
        self.assertEqual(len(end) % len(a), 0)
        self.assertEqual(end, a * (len(end) // len(a)))

    def test_pause_restart(self):
        e, user = self.opened(rate=22050)
        block = e.dev16(A1_BLOCK)
        data = pattern(block * 4)
        e.write(user, e.header(data))
        self.play(e, 1)
        self.assertEqual(e.wod(0, WODM_PAUSE, user), 0)
        self.assertEqual(e.chip.regs[0xB8] & 1, 0)
        pos = e.getpos(user)[2]
        self.assertEqual(e.getpos(user)[2], pos)
        self.assertEqual(e.wod(0, WODM_RESTART, user), 0)
        self.assertEqual(e.chip.regs[0xB8] & 1, 1)
        self.play(e, 6)
        self.assertEqual(bytes(e.played[:len(data)]), data)

    def test_paused_before_the_first_write(self):
        e, user = self.opened()
        e.wod(0, WODM_PAUSE, user)
        e.write(user, e.header(pattern(1000)))
        self.assertTrue(e.chip.dma[drvemu.A1_DMA]["mask"])
        e.wod(0, WODM_RESTART, user)
        self.assertFalse(e.chip.dma[drvemu.A1_DMA]["mask"])

    def test_reset(self):
        e, user = self.opened(rate=22050)
        block = e.dev16(A1_BLOCK)
        hdrs = [e.header(pattern(block, i)) for i in range(4)]
        for h in hdrs:
            e.write(user, h)
        self.play(e, 1)
        self.assertEqual(e.wod(0, WODM_RESET, user), 0)
        self.assertEqual(e.done(), hdrs)
        self.assertTrue(e.chip.dma[drvemu.A1_DMA]["mask"])
        self.assertEqual(e.getpos(user)[2], 0)
        self.assertFalse(e.dev8(A1_STATE) & 2)
        # and it plays again from the start
        data = pattern(500, 9)
        start = len(e.played)
        e.write(user, e.header(data))
        self.play(e, 2)
        self.assertEqual(bytes(e.played[start:start + len(data)]), data)

    def test_close(self):
        e, user = self.opened()
        h = e.header(pattern(3000))
        e.write(user, h)
        self.assertEqual(e.wod(0, WODM_CLOSE, user), WAVERR_STILLPLAYING)
        self.assertEqual(e.wod(0, WODM_RESET, user), 0)
        self.assertEqual(e.wod(0, WODM_CLOSE, user), 0)
        self.assertEqual(e.callbacks[-1][0], WOM_CLOSE)
        self.assertEqual(e.dev8(0x5D), 0)
        self.assertIn((0x0003, 1), e.vxd_calls)
        self.assertFalse(e.chip.a1_dac)             # D3h
        self.assertFalse(e.chip.mixer[0x71] & 0x04)
        self.assertEqual(e.dev8(A1_STATE), 0)
        # the device again, and wave-in may have Audio 1 now
        self.assertEqual(e.call((drvemu.SEG_PARA[4], 0x00E0),
                                (drvemu.DEV_OFF,)) & 0xFFFF, 0)

    def test_not_supported(self):
        e, user = self.opened()
        self.assertEqual(e.wod(0, WODM_PREPARE, user, 0, 0),
                         MMSYSERR_NOTSUPPORTED)

    def test_volume(self):
        e = self.emu()
        e.chip.mixer[0x14] = 0x8C
        out = e.alloc(b"\0" * 4)
        self.assertEqual(e.wod(1, WODM_GETVOLUME, 0, out, DEVNODE), 0)
        self.assertEqual(e.r32(out >> 16, out & 0xFFFF), 0xCCCC8888)
        self.assertEqual(e.wod(1, WODM_SETVOLUME, 0, 0x3000F000, DEVNODE),
                         0)
        self.assertEqual(e.chip.mixer[0x14], 0xF3)

    def test_interrupt_slot(self):
        e = self.emu()
        self.assertEqual(e.isr_slot(0), tuple(self.syms["a1_isr"]))
        # ESS's recording and playback services stay in the other slots
        self.assertEqual(e.isr_slot(1), (1, 0x19B4))
        self.assertEqual(e.isr_slot(2), (1, 0x1754))

    def test_interrupt_while_stopped(self):
        e, user = self.opened()
        e.interrupt()
        self.assertEqual(e.done(), [])


class SharedTest(A1Case):
    def busy_audio2(self, e):
        # wave-out open: Audio 2's user and the wave-out flag (4:0054)
        e.w8(drvemu.DGROUP, drvemu.DEV_OFF + 0x100, 1)
        e.w8(drvemu.DGROUP, drvemu.DEV_OFF + 0x70, 1)

    def test_device_0_falls_back(self):
        e = self.emu()
        self.busy_audio2(e)
        r, user = e.open(dev_id=0)
        self.assertEqual(r, 0)
        self.assertEqual(user >> 16, A1_MAGIC)
        self.assertTrue(e.dev8(A1_STATE) & 0x08)
        data = pattern(2000)
        e.write(user, e.header(data))
        self.play(e, 3)
        self.assertEqual(bytes(e.played[:len(data)]), data)

    def test_ess_answer_without_the_key(self):
        for opts in (OPT_A1_DEVICE, 0):
            e = self.emu(opts)
            self.busy_audio2(e)
            self.assertEqual(e.open(dev_id=0)[0], MMSYSERR_ALLOCATED)
            self.assertEqual(e.dev8(0x5D), 0)

    def test_query_isnt_busy(self):
        e = self.emu()
        self.busy_audio2(e)
        self.assertEqual(e.open(dev_id=0, flags=1)[0], 0)
        self.assertEqual(e.dev8(0x5D), 0)

    def test_both_busy(self):
        e = self.emu()
        self.busy_audio2(e)
        e.w8(drvemu.DGROUP, drvemu.DEV_OFF + 0x5D, 2)
        e.w8(drvemu.DGROUP, drvemu.DEV_OFF + 0x6F, 1)
        self.assertEqual(e.open(dev_id=0)[0], MMSYSERR_ALLOCATED)

    def test_ess_instances_go_to_ess(self):
        # an ESS instance whose serial number is A1A1h: its first word is
        # the device, not its own address
        e = self.emu()
        inst = 0x2800
        e.w16(drvemu.DGROUP, inst, drvemu.DEV_OFF)
        e.w16(drvemu.DGROUP, drvemu.DEV_OFF + 0x101, inst)
        e.w16(drvemu.DGROUP, drvemu.DEV_OFF + 0x103, A1_MAGIC)
        e.w8(drvemu.DGROUP, drvemu.DEV_OFF + 0x100, 1)
        pos = e.alloc(struct.pack("<HI", TIME_BYTES, 0) + b"\0\0")
        s = self.emu(stock=True)
        s.w16(drvemu.DGROUP, inst, drvemu.DEV_OFF)
        s.w16(drvemu.DGROUP, drvemu.DEV_OFF + 0x101, inst)
        s.w16(drvemu.DGROUP, drvemu.DEV_OFF + 0x103, A1_MAGIC)
        s.w8(drvemu.DGROUP, drvemu.DEV_OFF + 0x100, 1)
        spos = s.alloc(struct.pack("<HI", TIME_BYTES, 0) + b"\0\0")
        user = (A1_MAGIC << 16) | inst
        self.assertEqual(e.wod(0, drvemu.WODM_GETPOS, user, pos, 8),
                         s.wod(0, drvemu.WODM_GETPOS, user, spos, 8))


def quad(a, b, width):
    """4-channel frames: each frame's first half from a, second from b"""
    out = bytearray()
    for i in range(0, len(a), width):
        out += a[i:i + width] + b[i:i + width]
    return bytes(out)


class DualTest(A1Case):
    def dual(self, bits=16, rate=44100, a2_dma=None, **kw):
        e = self.emu(kw.pop("opts", None))
        if a2_dma is not None:
            e.set_a2_dma(a2_dma)
        r, user = e.open(rate=rate, channels=4, bits=bits)
        self.assertEqual(r, 0)
        return e, user

    def test_open(self):
        e, user = self.dual()
        self.assertEqual(e.dev8(0x100), 2)         # Audio 2's user: ours
        self.assertEqual(e.dev8(0x5D), 1)
        m = e.chip.mixer
        self.assertFalse(m[0x71] & 0x02)            # slaved to Audio 1
        self.assertFalse(m[0x71] & 0x10)            # no 4x oversampling
        self.assertEqual(bool(m[0x71] & 0x08), bool(m[0x71] & 0x04))
        self.assertEqual(m[0x7A], 0x07)             # 16-bit signed stereo
        block = e.dev16(A1_BLOCK)
        self.assertEqual((m[0x76] << 8) | m[0x74], (0x10000 - block) & 0xFFFF)
        self.assertEqual(e.chip.regs[0xA8] & 3, 1)  # Audio 1 in stereo

    def test_both_play_in_step(self):
        for bits, width in ((16, 4), (8, 2)):
            with self.subTest(bits=bits):
                e, user = self.dual(bits=bits, rate=22050)
                block = e.dev16(A1_BLOCK)
                a, b = pattern(block * 5, 11), pattern(block * 5, 12)
                data = quad(a, b, width)
                for i in range(0, len(data), 7000):
                    e.write(user, e.header(data[i:i + 7000]))
                log = [x for x in e.chip.log
                       if x[:2] in (("mixer", 0x78), ("reg", 0xB8))]
                go = 0x93                           # demand transfers
                self.assertEqual(log[-3:], [("mixer", 0x78, go & ~1),
                                            ("reg", 0xB8, 0x05),
                                            ("mixer", 0x78, go)])
                self.play(e, 8)
                self.assertEqual(len(e.played), len(e.played2))
                self.assertEqual(bytes(e.played[:len(a)]), a)
                self.assertEqual(bytes(e.played2[:len(b)]), b)
                self.assertEqual(e.getpos(user)[1:], (TIME_BYTES, len(data)))
                self.assertEqual(e.getpos(user, TIME_SAMPLES)[2],
                                 len(a) // width)

    def test_16_bit_audio2_channel(self):
        e, user = self.dual(a2_dma=5)
        block = e.dev16(A1_BLOCK)
        a, b = pattern(block * 3, 13), pattern(block * 3, 14)
        e.write(user, e.header(quad(a, b, 4)))
        d = e.chip.dma[5]
        self.assertEqual(d["count"], block - 1)     # words
        self.assertEqual(d["addr"], (drvemu.A2_BUF_PARA * 16 >> 1) & 0xFFFF)
        self.play(e, 5)
        self.assertEqual(bytes(e.played[:len(a)]), a)
        self.assertEqual(bytes(e.played2[:len(b)]), b)

    def test_pause_keeps_them_in_step(self):
        e, user = self.dual(rate=22050)
        block = e.dev16(A1_BLOCK)
        a, b = pattern(block * 6, 15), pattern(block * 6, 16)
        e.write(user, e.header(quad(a, b, 4)))
        e.dma(block + 100)
        e.interrupt()
        e.dma(300)
        self.assertEqual(e.wod(0, WODM_PAUSE, user), 0)
        self.assertTrue(e.chip.dma[drvemu.A1_DMA]["mask"])
        self.assertTrue(e.chip.dma[drvemu.A2_DMA]["mask"])
        pos = e.getpos(user)[2]
        self.assertEqual(pos, 2 * (block + 400))
        self.assertEqual(e.wod(0, WODM_RESTART, user), 0)
        self.play(e, 8)
        self.assertEqual(bytes(e.played[:len(a)]), a)
        self.assertEqual(bytes(e.played2[:len(b)]), b)

    def test_close(self):
        e, user = self.dual()
        e.write(user, e.header(quad(pattern(4000), pattern(4000, 2), 4)))
        e.wod(0, WODM_RESET, user)
        self.assertEqual(e.wod(0, WODM_CLOSE, user), 0)
        self.assertEqual(e.dev8(0x100), 0)
        self.assertTrue(e.chip.mixer[0x71] & 0x02)  # at its own rate again
        self.assertEqual(e.chip.mixer[0x78], 0)
        self.assertTrue(e.chip.dma[drvemu.A2_DMA]["mask"])
        # and ESS's wave-out can have Audio 2
        self.assertEqual(e.call((drvemu.SEG_PARA[4], 0x0054),
                                (drvemu.DEV_OFF,)) & 0xFFFF, 0)

    def test_wave_out_refused_while_dual(self):
        e, _u = self.dual()
        self.assertEqual(e.call((drvemu.SEG_PARA[4], 0x0054),
                                (drvemu.DEV_OFF,)) & 0xFFFF, 0xFFFF)
        self.assertEqual(e.open(dev_id=0)[0], MMSYSERR_ALLOCATED)

    def test_refused(self):
        e = self.emu()
        self.assertEqual(e.open(dev_id=0, channels=4)[0], WAVERR_BADFORMAT)
        e = self.emu(OPT_A1_DEVICE | OPT_A1_SHARED)
        self.assertEqual(e.open(channels=4)[0], WAVERR_BADFORMAT)
        e = self.emu()
        e.set_dev8(0x100, 1)                        # wave-out plays
        e.set_dev8(0x70, 1)
        self.assertEqual(e.open(channels=4)[0], MMSYSERR_ALLOCATED)
        self.assertEqual(e.dev8(0x5D), 0)
        e = self.emu()
        e.set_dev8(0x5D, 2)                         # wave-in records
        e.set_dev8(0x6F, 1)
        self.assertEqual(e.open(channels=4)[0], MMSYSERR_ALLOCATED)
        self.assertEqual(e.dev8(0x100), 0)

    def test_disable_stops_both(self):
        e, user = self.dual()
        e.write(user, e.header(quad(pattern(4000), pattern(4000, 2), 4)))
        e.call_name("a1_disable", drvemu.DEV_OFF)
        self.assertTrue(e.chip.dma[drvemu.A1_DMA]["mask"])
        self.assertTrue(e.chip.dma[drvemu.A2_DMA]["mask"])
        self.assertEqual(e.chip.mixer[0x78], 0)


class GateTest(A1Case):
    def gate(self, e):
        e.chip.log = []
        e.call_name("a1_d3_gate", drvemu.DEV_OFF, 0xD3)
        return e.chip.log

    def test_d3_site(self):
        from retools.ne import NEFile
        code = NEFile(self.fixed).segment_data(6)
        self.assertEqual(code[0x2D78], 0x9A)
        self.assertEqual(struct.unpack_from("<H", code, 0x2D79)[0],
                         self.syms["a1_d3_gate"][1])

    def test_ess_d3h_while_closed(self):
        e = self.emu()
        self.assertEqual(self.gate(e), [("cmd", 0xD3)])

    def test_dac_kept_while_open(self):
        e, _u = self.opened()
        self.assertEqual(self.gate(e), [])
        self.assertTrue(e.chip.a1_dac)


class ResumeTest(A1Case):
    def test_resume_starts_it_again(self):
        e, user = self.opened(rate=22050)
        block = e.dev16(A1_BLOCK)
        data = pattern(block * 6)
        e.write(user, e.header(data))
        self.play(e, 2)
        # an APM resume: the chip reset under the driver
        e.chip.ext = False
        e.chip.a1_dac = False
        e.chip.regs[0xB8] = 0
        e.chip.mixer[0x71] = 0
        e.call_name("es_wid_resume", drvemu.DEV_OFF)
        self.assertTrue(e.chip.ext)
        self.assertTrue(e.chip.a1_dac)
        self.assertEqual(e.chip.regs[0xB8] & 0x0F, 0x05)
        self.assertTrue(e.chip.mixer[0x71] & 0x04)
        start = len(e.played)
        self.play(e, 8)
        out = bytes(e.played[start:])
        # what the ring held is dropped, the rest plays
        tail = data[-block:]
        self.assertIn(tail, out)
        self.assertEqual(len(e.done()), 1)

    def test_resume_without_the_player(self):
        e = self.emu()
        e.call_name("es_wid_resume", drvemu.DEV_OFF)
        self.assertEqual(e.chip.log, [])


class DisableTest(A1Case):
    def test_site(self):
        from retools.ne import NEFile
        code = NEFile(self.fixed).segment_data(3)
        self.assertEqual(struct.unpack_from("<H", code, 0x4E21)[0],
                         self.syms["a1_disable"][1])

    def test_last_disable_stops_the_player(self):
        # the handler is freed after this: no Audio 1 interrupt may come
        e, user = self.opened()
        e.write(user, e.header(pattern(3000)))
        self.assertFalse(e.chip.dma[drvemu.A1_DMA]["mask"])
        e.call_name("a1_disable", drvemu.DEV_OFF)
        self.assertTrue(e.chip.dma[drvemu.A1_DMA]["mask"])
        self.assertEqual(e.chip.regs[0xB8] & 0x0F, 0)
        self.assertFalse(e.dev8(A1_STATE) & 2)

    def test_as_ess_without_the_player(self):
        logs = []
        for name in ("a1_disable", None):
            e = self.emu()
            if name:
                e.call_name(name, drvemu.DEV_OFF)
            else:
                e.call((drvemu.SEG_PARA[1], 0x1602), (drvemu.DEV_OFF,))
            logs.append((e.chip.log, e.chip.dma_log))
        self.assertEqual(logs[0], logs[1])


class SettingsTest(A1Case):
    def enable(self, ini):
        e = self.emu()
        e.ini = ini
        e.uc.mem_write(drvemu.SEG_PARA[3] * 16 + 0x3DEC, b"\xCA\x02\x00")
        e.call_name("es_read_config", drvemu.DEV_OFF)
        return e

    def opts(self, e):
        return e.r16(drvemu.DGROUP, self.syms["es_opts"][1])

    def test_defaults(self):
        e = self.enable({})
        self.assertEqual(self.opts(e), OPT_READ | OPT_A1_DEVICE |
                         OPT_A1_SHARED | OPT_DUAL)
        self.assertEqual([k for _s, k, _d, _f in e.ppint], [
            "Audio1Device", "SharedWaveOut", "Audio1Filter",
            "DualPlayback", "Audio2Oversampling", "Audio2Filter"])

    def test_each_key(self):
        for key, bit, default in (("Audio1Device", OPT_A1_DEVICE, 1),
                                  ("SharedWaveOut", OPT_A1_SHARED, 1),
                                  ("Audio1Filter", OPT_A1_FILTER, 0),
                                  ("DualPlayback", OPT_DUAL, 1)):
            for value, on in (("1", True), ("0", False), ("yes", False)):
                with self.subTest(key=key, value=value):
                    e = self.enable({("ES1869.DRV", key): value})
                    self.assertEqual(bool(self.opts(e) & bit), on)
                    asked = [d for _s, k, d, _f in e.ppint if k == key]
                    self.assertEqual(asked, [default])

    def test_all_off_is_ess(self):
        """every key at 0: the messages answer and touch the chip as in
        ESS's driver"""
        results = []
        for stock in (True, False):
            e = self.emu(0 if not stock else None, stock=stock)
            out = []
            out.append(e.wod(0, WODM_GETNUMDEVS, 0, DEVNODE))
            out.append(e.open(dev_id=1)[0])
            e.w8(drvemu.DGROUP, drvemu.DEV_OFF + 0x100, 1)
            e.w8(drvemu.DGROUP, drvemu.DEV_OFF + 0x70, 1)
            out.append(e.open(dev_id=0)[0])
            buf = e.alloc(b"\0" * 0x30)
            ex = e.alloc(struct.pack("<II", 0x30, buf))
            out.append(e.wod(1, WODM_GETDEVCAPS, 0, ex, DEVNODE))
            vol = e.alloc(b"\0" * 4)
            out.append(e.wod(1, WODM_GETVOLUME, 0, vol, DEVNODE))
            # ESS's playback start sends D3h through the site at 6:2D78
            e.chip.log = []
            site = e.r16(drvemu.SEG_PARA[6], 0x2D79)
            e.call((drvemu.SEG_PARA[1], site), (drvemu.DEV_OFF, 0xD3))
            out.append(list(e.chip.log))
            results.append((out, e.vxd_calls, e.callbacks))
        self.assertEqual(results[0], results[1])


if __name__ == "__main__":
    unittest.main()
