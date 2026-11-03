# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0 or later
"""The Linux driver's ESFM General MIDI engine (linux/es1869/esfm_gm.c),
built for the host, against the fixed ESFM.DRV running in esfmemu: the same
MIDI messages make the same native register writes, in the same order, and
leave the same voice table (docs/ESFM_ENGINE.md).

usage: python3 -m unittest tests.test_esfmgm   (needs gcc and unicorn)
"""

import ctypes
import os
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_vxdext import HAVE_UNICORN  # noqa: E402

if HAVE_UNICORN:
    from esfmemu import ESFMEmu, DRV_ENABLE  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRV = os.path.join(ROOT, "linux", "es1869")
HOST = os.path.join(ROOT, "tests", "host")
BANKS = os.path.join(ROOT, "esfm_patch_banks")
FIXED = os.path.join(ROOT, "build", "ESFM.DRV")
# the engine's switches, as lx_esfm_new numbers them, and their keys
SWITCHES = ["Vibrato", "Tuning", "ResetControllers", "LivePan", "SysEx",
            "PedalRelease", "SilenceOnClose"]
ALL = (1 << len(SWITCHES)) - 1
# the Linux engine's own: pan left with register 6 bit 5, the chip's left
SIDES = 1 << len(SWITCHES)

_LIB = {}


def library():
    if "lib" not in _LIB:
        tmp = _LIB["tmp"] = tempfile.TemporaryDirectory()
        out = os.path.join(tmp.name, "libesfmgm.so")
        subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-O1", "-fPIC", "-shared", "-I" + DRV, "-o", out,
                        os.path.join(DRV, "esfm_gm.c"),
                        os.path.join(HOST, "lx_esfm.c")],
                       check=True, capture_output=True)
        lib = ctypes.CDLL(out)
        P, u8, u16, u32 = (ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint16,
                           ctypes.c_uint32)
        sigs = {
            "lx_esfm_new": ([ctypes.c_char_p, u32, ctypes.c_uint], P),
            "lx_esfm_free": ([P], None),
            "lx_esfm_writes": ([ctypes.POINTER(u16), ctypes.POINTER(u8),
                                ctypes.c_uint], ctypes.c_uint),
            "lx_esfm_voice": ([P, ctypes.c_uint, ctypes.POINTER(u8)], None),
            "esfm_bank_check": ([ctypes.c_char_p, u32], ctypes.c_int),
            "esfm_gm_short": ([P, u8, u8, u8], None),
            "esfm_gm_sysex": ([P, ctypes.c_char_p, u32], None),
            "esfm_gm_close": ([P], None),
        }
        for name, (args, res) in sigs.items():
            fn = getattr(lib, name)
            fn.argtypes, fn.restype = args, res
        _LIB["lib"] = lib
    return _LIB["lib"]


def bank(name="bnk_com.bin"):
    with open(os.path.join(BANKS, name), "rb") as f:
        return f.read()


class Engine:
    """the C engine on a bank, and the register writes it made"""

    def __init__(self, data, opts=ALL):
        self.lib = library()
        self.data = data
        self.g = self.lib.lx_esfm_new(data, len(data), opts)

    def writes(self):
        reg = (ctypes.c_uint16 * 65536)()
        val = (ctypes.c_uint8 * 65536)()
        n = self.lib.lx_esfm_writes(reg, val, 65536)
        return [(reg[i], val[i]) for i in range(n)]

    def short(self, msg):
        self.lib.esfm_gm_short(self.g, msg & 0xFF, (msg >> 8) & 0x7F,
                               (msg >> 16) & 0x7F)

    def voices(self):
        out = []
        for v in range(18):
            t = (ctypes.c_uint8 * 3)()
            self.lib.lx_esfm_voice(self.g, v, t)
            out.append(tuple(t))
        return out


def emulator(data, opts=ALL):
    """the fixed ESFM.DRV, enabled with the switches' keys, playing data"""
    with open(FIXED, "rb") as f:
        e = ESFMEmu(f.read(), bank=data)
    for i, key in enumerate(SWITCHES):
        e.ini[("esfm.drv", key.lower())] = "1" if opts >> i & 1 else "0"
    assert e.driverproc(DRV_ENABLE) & 0xFFFF == 1
    # the bank in place of the driver's own, as esfmpat loads one
    sel = e.galloc(len(data), data)
    e.w16(4, 0x14, sel)
    e.w16(4, 0x12, 0)
    e.open()
    return e


def decode(events):
    out, a = [], 0
    for _dev, off, v in events:
        if off == 2:
            a = (a & 0xFF00) | v
        elif off == 3:
            a = (a & 0xFF) | (v << 8)
        elif off == 1:
            out.append((a, v))
    return out


def random_message(rng):
    ch = rng.randrange(16)
    r = rng.random()
    if r < 0.35:
        return 0x90 | ch | rng.randrange(128) << 8 | rng.randrange(1, 128) << 16
    if r < 0.6:
        return 0x80 | ch | rng.randrange(128) << 8
    if r < 0.63:
        return 0x90 | ch | rng.randrange(128) << 8
    if r < 0.85:
        c = rng.choice([1, 6, 7, 8, 10, 11, 38, 64, 98, 99, 100, 101, 120,
                        121, 123, 124, 125, 126, 127, 0, 32, 7, 11, 64, 100,
                        101, 6, 38])
        v = rng.randrange(128)
        if c == 6:
            v = rng.randrange(25)
        if c in (100, 101) and rng.random() < 0.7:
            v = rng.choice([0, 0, 1, 2])
        return 0xB0 | ch | c << 8 | v << 16
    if r < 0.9:
        return 0xC0 | ch | rng.randrange(128) << 8
    if r < 0.97:
        b = rng.randrange(0x4000)
        if rng.random() < 0.2:
            b = rng.choice([0, 0x2000, 0x3FFF, 0x3F80, 0x3F7F, 1])
        return 0xE0 | ch | (b & 0x7F) << 8 | (b >> 7) << 16
    return 0xD0 | ch | rng.randrange(128) << 8


@unittest.skipUnless(shutil.which("gcc"), "needs gcc")
class BankTest(unittest.TestCase):
    def test_banks_pass(self):
        lib = library()
        for name in ("bnk_com.bin", "bnk_com_better_square_wave.bin",
                     "bnk_NT4.bin"):
            data = bank(name)
            self.assertEqual(lib.esfm_bank_check(data, len(data)), 0, name)

    def test_cut_bank_fails(self):
        data = bank()[:0x400]
        self.assertNotEqual(library().esfm_bank_check(data, len(data)), 0)

    def test_reset_is_ess(self):
        """chip_reset: 00h to every register from 000h to 253h"""
        e = Engine(bank())
        self.assertEqual(e.writes(), [(r, 0) for r in range(0x254)])


@unittest.skipUnless(shutil.which("gcc") and HAVE_UNICORN,
                     "needs gcc and unicorn")
class EsfmDrvTest(unittest.TestCase):
    """random messages, the C engine against ESFM.DRV"""

    def play(self, data, opts, n, seed):
        rng = random.Random(seed)
        c = Engine(data, opts)
        e = emulator(data, opts)
        self.assertEqual(decode(e.chip.events)[-0x254:], c.writes())
        for i in range(n):
            msg = random_message(rng)
            before = len(e.chip.events)
            e.data(msg)
            c.short(msg)
            want = decode(e.chip.events[before:])
            self.assertEqual(c.writes(), want, "message %d, %06X" % (i, msg))
            self.assertEqual(c.voices(), e.voices(), "voices after %d" % i)
        return c, e

    def test_every_switch_on(self):
        for seed, name in enumerate(["bnk_com.bin", "bnk_NT4.bin",
                                     "bnk_com_better_square_wave.bin"]):
            self.play(bank(name), ALL, 1500, seed)

    def test_each_switch_off(self):
        for i, key in enumerate(SWITCHES):
            with self.subTest(switch=key):
                self.play(bank(), ALL & ~(1 << i), 500, 100 + i)

    def test_resets_and_master_volume(self):
        c, e = self.play(bank(), ALL, 200, 7)
        for sysex in (bytes([0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, 0x30, 0xF7]),
                      bytes([0xF0, 0x7E, 0x10, 0x09, 0x01, 0xF7]),
                      bytes([0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F,
                             0x00, 0x41, 0xF7]),
                      bytes([0xF0, 0x43, 0x12, 0x4C, 0x00, 0x00, 0x7E, 0x00,
                             0xF7]),
                      bytes([0xF0, 0x7E, 0x7F, 0x09, 0x03, 0xF7])):
            for msg in (0x403C90, 0x504090, 0x7F4391, 0x4007B1, 0x7F7FE0):
                before = len(e.chip.events)
                e.data(msg)
                c.short(msg)
                self.assertEqual(c.writes(), decode(e.chip.events[before:]))
            before = len(e.chip.events)
            e.longdata(sysex)
            c.lib.esfm_gm_sysex(c.g, sysex, len(sysex))
            self.assertEqual(c.writes(), decode(e.chip.events[before:]),
                             sysex.hex())
            self.assertEqual(c.voices(), e.voices())


@unittest.skipUnless(shutil.which("gcc"), "needs gcc")
class SidesTest(unittest.TestCase):
    """the sides option: pan left with bit 5 and right with bit 4, as the
    chip plays them, and nothing else changed"""

    @staticmethod
    def swapped(v):
        side = v & 0x30
        return v ^ 0x30 if side in (0x10, 0x20) else v

    def test_only_the_pan_bits_change(self):
        # the harness keeps one list of writes, so each engine's are taken
        # right after it runs
        rng = random.Random(5)
        ess = Engine(bank(), ALL)
        reset = ess.writes()
        ours = Engine(bank(), ALL | SIDES)
        self.assertEqual(ours.writes(), reset)
        moved = 0
        for i in range(3000):
            msg = random_message(rng)
            ess.short(msg)
            want = ess.writes()
            ours.short(msg)
            got = ours.writes()
            self.assertEqual([r for r, _ in got], [r for r, _ in want], i)
            for (r, a), (_, b) in zip(want, got):
                if a != b:
                    self.assertTrue(r < 0x240 and r & 7 == 6, hex(r))
                    self.assertEqual(b, self.swapped(a), hex(r))
                    moved += 1
            self.assertEqual(ours.voices(), ess.voices())
        self.assertGreater(moved, 100)

    def test_pan_left_is_bit_5(self):
        e = Engine(bank(), ALL | SIDES)
        e.writes()
        for val, side in ((0, 0x20), (127, 0x10)):
            e.short(0xB0 | 10 << 8 | val << 16)
            e.short(0x90 | 60 << 8 | 100 << 16)
            outs = [v & 0x30 for r, v in e.writes()
                    if r < 0x240 and r & 7 == 6 and v & 0x30]
            self.assertTrue(outs)
            self.assertEqual(set(outs), {side}, val)
            e.short(0x80 | 60 << 8)
            e.writes()


if __name__ == "__main__":
    unittest.main()
