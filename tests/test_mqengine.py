#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The MIDIEngine 2Port/SE's protocol, on the host, and its kernel patch.

linux/mqengine/proto.h is built for this machine and called through ctypes
against a model of the unit as the Armada's showed it. The model takes a byte
as STROBE starts and its output from D5 as STROBE ends, which is what kept bit
5 of a byte from going out on A with the Note/1's sequence.

usage: python3 -m unittest tests.test_mqengine   (needs gcc)
"""

import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRV = os.path.join(ROOT, "linux", "mqengine")
sys.path.insert(0, os.path.join(ROOT, "tools"))

DATA, STATUS, CONTROL = 0, 1, 2
STROBE, NEXT = 1, 2

SHIM = r"""
#define mq_out(io, reg, v) hook_out(reg, v)
#define mq_in(io, reg) ((unsigned char)hook_in(reg))
#define mq_us(n) ((void)(n))
#define mq_ms(n) ((void)(n))
static void (*hook_out)(int, int);
static int (*hook_in)(int);
#include "proto.h"
static struct mq_io io = { .control = MQ_C_IDLE };
void t_hooks(void *o, void *i) { hook_out = o; hook_in = i; }
int t_room(void) { return mq_room(&io); }
void t_put(int out, int b) { mq_put(&io, out, b); }
int t_get(unsigned char *b, unsigned int *in) { return mq_get(&io, b, in); }
int t_reset(unsigned char *found) { return mq_reset(&io, found); }
"""


class Unit:
    """the 2Port/SE: the data lines' low bits choose what status shows"""

    def __init__(self, there=True):
        self.there = there
        self.lines = 0
        self.control = 0
        self.taken = 0
        self.full = False
        self.sent = []
        self.waiting = []

    def out(self, reg, v):
        if reg == DATA:
            self.lines = v
        elif reg == CONTROL:
            was, self.control = self.control, v
            if v & STROBE and not was & STROBE:
                self.taken = self.lines
            if was & STROBE and not v & STROBE:
                self.sent.append((self.lines >> 5 & 1, self.taken))
            if was & NEXT and not v & NEXT and self.waiting:
                self.waiting.pop(0)

    def status(self):
        if not self.there:
            return 0xFF
        if self.control & NEXT:
            if self.waiting:
                return 0 if self.waiting[0][0] else 0x80
            return 0
        sel = self.lines & 3
        if not self.waiting:
            hi, lo = 1, 1
            bit7 = {3: 0, 2: 1, 1: self.full, 0: 1}[sel]
            return bit7 << 7 | hi << 5 | lo << 3
        b = self.waiting[0][1]
        hi = b >> {3: 7, 2: 6, 1: 3, 0: 2}[sel] & 1
        lo = b >> {3: 5, 2: 4, 1: 1, 0: 0}[sel] & 1
        bit7 = {3: 0, 2: 0, 1: self.full, 0: 1}[sel]
        return bit7 << 7 | 0x40 | hi << 5 | lo << 3


class ProtoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        src = os.path.join(cls.tmp, "shim.c")
        with open(src, "w") as f:
            f.write(SHIM)
        lib = os.path.join(cls.tmp, "shim.so")
        subprocess.check_call(["gcc", "-shared", "-fPIC", "-O1", "-Wall",
                               "-Werror", "-I", DRV, "-o", lib, src])
        cls.lib = ctypes.CDLL(lib)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def setUp(self):
        self.unit = Unit()
        out = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_int)
        into = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int)
        self.hooks = (out(self.unit.out),
                      into(lambda reg: self.unit.status()))
        self.lib.t_hooks(*self.hooks)
        self.unit.control = 0x0C

    def test_every_byte_out_of_each_output(self):
        for port in (0, 1):
            for b in range(256):
                self.lib.t_put(port, b)
        want = [(p, b) for p in (0, 1) for b in range(256)]
        self.assertEqual(self.unit.sent, want)

    def test_every_byte_in_from_each_input(self):
        want = [(p, b) for p in (0, 1) for b in range(256)]
        self.unit.waiting = list(want)
        got, b, port = [], ctypes.c_ubyte(), ctypes.c_uint()
        while self.lib.t_get(ctypes.byref(b), ctypes.byref(port)):
            got.append((port.value, b.value))
        self.assertEqual(got, want)

    def test_room(self):
        self.assertTrue(self.lib.t_room())
        self.unit.full = True
        self.assertFalse(self.lib.t_room())

    def test_nothing_waits(self):
        b, port = ctypes.c_ubyte(), ctypes.c_uint()
        self.assertFalse(self.lib.t_get(ctypes.byref(b), ctypes.byref(port)))

    def test_reset_knows_the_unit(self):
        found = (ctypes.c_ubyte * 3)()
        self.assertEqual(self.lib.t_reset(found), 0)
        self.assertEqual(self.unit.control, 0x0C)
        self.assertEqual(self.unit.lines, 0xFF)

    def test_reset_refuses_a_port_with_nothing_on_it(self):
        self.unit.there = False
        found = (ctypes.c_ubyte * 3)()
        self.assertNotEqual(self.lib.t_reset(found), 0)
        self.assertEqual(list(found), [0xFF, 0xFF, 0xFF])


class PatchTest(unittest.TestCase):
    KCONFIG = 'config SND_MTPAV\n\thelp\n\t  will be called snd-mtpav.\n'
    MAKE = 'obj-$(CONFIG_SND_MTPAV) += snd-mtpav.o\n'

    def build(self, kconfig, make):
        from mqenginepatch import main
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, "sound", "drivers"))
            for name, text in (("Kconfig", kconfig), ("Makefile", make)):
                with open(os.path.join(tmp, "sound", "drivers", name),
                          "w") as f:
                    f.write(text)
            out = os.path.join(tmp, "out.patch")
            main(["--ksrc", tmp, "-o", out])
            with open(out) as f:
                return f.read()

    def test_the_patch_adds_the_driver_and_hooks_it_in(self):
        text = self.build(self.KCONFIG, self.MAKE)
        self.assertIn("+++ b/sound/drivers/mqengine/mqengine.c", text)
        self.assertIn("+++ b/sound/drivers/mqengine/proto.h", text)
        self.assertIn('+source "sound/drivers/mqengine/Kconfig"', text)
        self.assertIn("+obj-$(CONFIG_SND_MQENGINE) += mqengine/", text)

    def test_the_patch_refuses_a_tree_that_has_it(self):
        with self.assertRaises(SystemExit):
            self.build(self.KCONFIG + "# mqengine\n", self.MAKE)


if __name__ == "__main__":
    unittest.main()
