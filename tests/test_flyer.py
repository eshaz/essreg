#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""MOTU's PC-MIDI Flyer protocol, on the host, and its kernel patch.

linux/flyer/proto.h is built for this machine and called through ctypes
against a model of the unit as the Armada's showed it: the control lines that
choose a port, a queue for both outputs that keeps seven bytes waiting behind
those being sent, with room once none waits, a ring of sixteen that holds
fifteen on each input, the bytes read two bits at a time, the supply from the
data lines, and the pulse on status bit 6.
Each port access takes a microsecond of the model's time, and the unit needs
the rests that MOTU's driver leaves after a strobe and between pairs.

The driver's work, as snd-flyer's timer and interrupt run it, is the same
helpers in a loop (DRIVE below), which plays bytes out of both outputs at full
speed into cables back to their inputs.

Each negative control breaks one rule, in a copy of proto.h or in the loop,
and the model has to catch it: the data lines left at the byte, eight bytes on
a sight of room, the pairs read high first, rests too short, a poll too slow.

usage: python3 -m unittest tests.test_flyer   (needs gcc)
"""

import contextlib
import ctypes
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRV = os.path.join(ROOT, "linux", "flyer")
sys.path.insert(0, os.path.join(ROOT, "tools"))

DATA, STATUS, CONTROL = 0, 1, 2
ACCESS_US = 1.0  # a port access
BYTE_US = 320.0  # a MIDI byte
BEAT_US = 16384.0  # the pulse whatever is happening
BEAT_HIGH_US = 240.0  # how long the line is up for it
WAIT_US = 256.0  # the pulse while a byte waits
DROP_US = 16.0  # how long the line drops then
STARVE_US = 1e6  # the data lines not FFh this long, and the unit stops
FEED_US = 50e3  # FFh this long, and it runs again
NOTHING = 0x7F  # the status of a port with nothing on it

# the control lines' low four bits, as the unit sees them
IDLE = 0x04
WRITE = {0x05: 0, 0x0F: 0, 0x00: 1}  # 15h, and 1Fh, the question: A; 10h: B
READ = {0x06: 0, 0x0C: 1}  # 16h A, 1Ch B
COMMAND = 0x01  # 11h
BOTH_READ = 0x0E  # 1Eh
REST = (6, 3)  # accesses that MOTU's driver rests on port A and on port B
MODEL_ID = 2


def driver_us(name):
    """one of flyer.c's timer periods"""
    with open(os.path.join(DRV, "flyer.c")) as f:
        m = re.search(r"#define %s (\d+)" % name, f.read())
    return float(m.group(1))


OUT_US = driver_us("FL_OUT_US")
IN_US = driver_us("FL_IN_US")

SHIM = r"""
#define fl_out(io, reg, v) hook_out(reg, v)
#define fl_in(io, reg) ((unsigned char)hook_in(reg))
#define fl_us(n) hook_wait(n)
#define fl_ms(n) hook_wait((n) * 1000.0)
static void (*hook_out)(int, int);
static int (*hook_in)(int);
static void (*hook_wait)(double);
static double (*hook_now)(void);
static int (*hook_sleep)(double, int);
#include "proto.h"
static struct fl_io io = { 0x378, FL_C_IRQ };

void t_hooks(void *o, void *i, void *w, void *n, void *s)
{
    hook_out = o; hook_in = i; hook_wait = w; hook_now = n; hook_sleep = s;
}
void t_irq(int on) { io.irq = on ? FL_C_IRQ : 0; }
int t_room(void) { return fl_room(&io); }
unsigned int t_credit(unsigned int left) { return fl_credit(&io, left); }
int t_waiting(int in) { return fl_waiting(&io, in); }
int t_get(int in) { return fl_get(&io, in); }
void t_put(int out, int b) { fl_put(&io, out, b); }
void t_command(int c) { fl_command(&io, c); }
int t_id(void) { return fl_id(&io); }
int t_alive(void) { return fl_alive(&io); }
void t_start(void) { fl_start(&io); }

struct t_out { const unsigned char *src; int n, sent; };
struct t_in { unsigned char *dst; int cap, got; };

/* DRIVE: flyer.c's work. Its timer runs every out_us while an output has
 * bytes, and when polling (irq 0) every in_us while an input is open; with
 * irq, the port's interrupt starts it at once. Each run reads both inputs, a
 * ring at most each, then feeds the outputs in turn on the room flag's
 * credit. Until every byte is out and nothing has come in for quiet_us. */
void t_drive(struct t_out *o, struct t_in *in, int irq, double out_us,
         double in_us, double quiet_us, double until)
{
    unsigned int credit = 0, turn = 0, want, i, port;
    double next = hook_now(), last = next;
    int n;

    io.irq = irq ? FL_C_IRQ : 0;
    fl_ctl(&io, FL_C_IDLE);
    for (;;) {
        for (i = 0; i < 2; i++)
            for (n = 0; n < FL_RING && fl_waiting(&io, i); n++) {
                unsigned char b = fl_get(&io, i);

                if (in[i].got < in[i].cap)
                    in[i].dst[in[i].got] = b;
                in[i].got++;
                last = hook_now();
            }
        for (;;) {
            want = (o[0].sent < o[0].n) | (o[1].sent < o[1].n) << 1;
            if (!want)
                break;
            credit = fl_credit(&io, credit);
            if (!credit)
                break;
            port = want & (1u << turn) ? turn : !turn;
            fl_put(&io, port, o[port].src[o[port].sent++]);
            credit--;
            turn = !port;
            last = hook_now();
        }
        want = (o[0].sent < o[0].n) | (o[1].sent < o[1].n) << 1;
        if ((!want && hook_now() - last > quiet_us) ||
            hook_now() > until)
            break;
        if (want || !irq) {
            double us = want ? out_us : in_us;

            do
                next += us;
            while (next <= hook_now());
            hook_sleep(next, 0);
        } else {
            hook_sleep(hook_now() + quiet_us, 1);
            next = hook_now();
        }
    }
    io.irq = FL_C_IRQ;
}
"""

# rules broken one at a time, in a copy of proto.h
BROKEN = {
    "byte left on the data lines": (
        "\tfl_rest(io, out, 1);\n\tfl_out(io, FL_DATA, 0xFF);\n",
        "\tfl_rest(io, out, 1);\n"),
    "eight on a sight of room": (
        "#define FL_BURST 4 ", "#define FL_BURST 8 "),
    "pairs read high first": (
        "& FL_S_PAIR) >> 4) << n;", "& FL_S_PAIR) >> 4) << (6 - n);"),
    "rests too short": (
        "for (n *= port ? 3 : 6; n; n--)", "for (n *= 1; n; n--)"),
}


class Unit:
    """the Flyer as the Armada's showed it, timed by its port accesses"""

    def __init__(self, there=True, fed=True):
        self.there = there
        self.t = 0.0
        self.data = 0xFF if fed else 0x00
        self.ctl = IDLE
        self.irq_on = False
        self.low_at = None if fed else 0.0
        self.high_at = 0.0 if fed else None
        self.dead = not fed
        self.queue = []  # [port, byte, started], oldest first
        self.busy = [None, None]  # (end, entry) on each output
        self.rings = [[], []]
        self.shown = [0, 0]  # pairs of the first byte shown
        self.off_at = [-1e9, -1e9]  # when each read line went off
        self.latches = []  # (time, port): the unit reads the data lines then
        self.arrivals = []  # (time, input, byte) from outside
        self.cable = (0, 1)  # output -> input
        self.out = [[], []]  # bytes out of each output
        self.lost = 0  # bytes the output queue dropped
        self.overruns = 0  # rings that wrapped
        self.commands = []
        self.longest_low = 0.0
        self.edges = 0

    # time

    def _supply(self, t):
        if self.low_at is not None:
            self.longest_low = max(self.longest_low, t - self.low_at)
            if not self.dead and t - self.low_at >= STARVE_US:
                self.dead = True
                self.queue, self.busy = [], [None, None]
                self.rings, self.shown = [[], []], [0, 0]
        elif self.dead and t - self.high_at >= FEED_US:
            self.dead = False

    def _start(self, port, t):
        if self.busy[port] or self.dead:
            return
        for e in self.queue:
            if e[0] == port and not e[2]:
                e[2] = True
                self.busy[port] = (t + BYTE_US, e)
                return

    def _arrive(self, i, b):
        if self.dead:
            return
        ring = self.rings[i]
        if len(ring) == 15:
            ring.clear()
            self.shown[i] = 0
            self.overruns += 1
        else:
            ring.append(b)

    def _next(self):
        best = None
        for p in (0, 1):
            b = self.busy[p]
            if b and (best is None or b[0] < best[0]):
                best = (b[0], 0, p)
        if self.latches and (best is None or self.latches[0][0] < best[0]):
            best = (self.latches[0][0], 1, self.latches[0][1])
        if self.arrivals and (best is None or self.arrivals[0][0] < best[0]):
            best = (self.arrivals[0][0], 2, 0)
        return best

    def advance(self, to):
        while True:
            ev = self._next()
            if ev is None or ev[0] > to:
                break
            when, kind, p = ev
            self._supply(when)
            if kind == 0:
                end, e = self.busy[p]
                self.busy[p] = None
                for k, x in enumerate(self.queue):
                    if x is e:
                        del self.queue[k]
                        self.out[p].append(e[1])
                        self._arrive(self.cable[p], e[1])
                        break
                self._start(p, end)
            elif kind == 1:
                self.latches.pop(0)
                if p < 2:
                    self._enqueue(p, self.data, when)
                elif not self.dead:
                    self.commands.append(self.data)
                    if self.data == 0x07:
                        self.rings, self.shown = [[], []], [0, 0]
            else:
                _, i, b = self.arrivals.pop(0)
                self._arrive(i, b)
        self.t = max(self.t, to)
        self._supply(self.t)

    def _enqueue(self, port, b, t):
        if self.dead:
            return
        waiting = [e for e in self.queue if not e[2]]
        if len(waiting) == 7:
            # an eighth waiting makes the queue look empty: those seven go,
            # and so does the eighth
            self.lost += 8
            self.queue = [e for e in self.queue if e[2]]
            return
        self.queue.append([port, b, False])
        self._start(port, t)

    def feed(self, i, data, start=None):
        """bytes into input i from outside, at MIDI's speed"""
        t = self.t if start is None else start
        for k, b in enumerate(data):
            self.arrivals.append((t + (k + 1) * BYTE_US, i, b))
        self.arrivals.sort()

    # the lines

    def line(self, t):
        """status bit 6: up for 240 us every 16.4 ms, and while a byte
        waits up but for a drop every 256 us"""
        if self.rings[0] or self.rings[1]:
            return int(t % WAIT_US >= DROP_US)
        return int(t % BEAT_US < BEAT_HIGH_US)

    def next_edge(self, after):
        """the next rise of the line after a time, if a byte waits now"""
        beat = (after // BEAT_US + 1) * BEAT_US
        if not (self.rings[0] or self.rings[1]):
            return beat
        wait = (after // WAIT_US + 1) * WAIT_US + DROP_US
        if after % WAIT_US < DROP_US:
            wait = after - after % WAIT_US + DROP_US
        return min(beat, wait)

    def out_(self, reg, v):
        self.advance(self.t + ACCESS_US)
        if not self.there:
            return
        if reg == DATA:
            v &= 0xFF
            if v != 0xFF and self.low_at is None:
                self.low_at, self.high_at = self.t, None
            elif v == 0xFF and self.low_at is not None:
                self.low_at, self.high_at = None, self.t
            self.data = v
        elif reg == CONTROL:
            self.irq_on = bool(v & 0x10)
            self._control(v & 0x0F)

    def _control(self, new):
        was = self.ctl
        if new == was:
            return
        self.ctl = new
        if was in READ:
            i = READ[was]
            self.off_at[i] = self.t
            if self.shown[i] == 4:
                self.rings[i].pop(0)
                self.shown[i] = 0
        if new in WRITE or new == COMMAND:
            # the unit reads the data lines a rest after the strobe starts
            p = WRITE.get(new, 2)
            self.latches.append((self.t + REST[p % 2] - 0.5, p))
        if new in READ:
            i = READ[new]
            # a read line back on too soon is one long read, no new pair
            if self.rings[i] and (self.shown[i] == 0 or
                                  self.t - self.off_at[i] >= REST[i]):
                self.shown[i] += 1

    def in_(self, reg):
        self.advance(self.t + ACCESS_US)
        if not self.there:
            return NOTHING
        if reg != STATUS:
            return self.ctl | 0xC0 if reg == CONTROL else self.data
        if self.dead:
            return 0xFF
        s = 0 if any(not e[2] for e in self.queue) else 0x80
        s |= self.line(self.t) << 6
        if self.ctl in READ:
            i = READ[self.ctl]
            if self.rings[i]:
                pair = self.rings[i][0] >> 2 * (self.shown[i] - 1) & 3
                s |= 0x08 | pair << 4
        elif self.ctl == BOTH_READ:
            if not (self.rings[0] or self.rings[1]):
                s |= MODEL_ID << 4
        elif self.rings[0] or self.rings[1]:
            s |= 0x08
        return s

    def wait(self, us):
        self.advance(self.t + us)

    def sleep(self, until, wake):
        """until a time, or with wake the port's next interrupt"""
        while wake and self.irq_on:
            edge = self.next_edge(self.t)
            ev = self._next()
            if ev is not None and ev[0] < min(edge, until):
                self.advance(ev[0])  # a byte in or out first
                continue
            if edge > until:
                break
            self.advance(edge)
            if self.line(edge) and not self.line(edge - 1):
                self.edges += 1
                return 1
        if until > self.t:
            self.advance(until)
        return 0


class TOut(ctypes.Structure):
    _fields_ = [("src", ctypes.c_char_p), ("n", ctypes.c_int),
                ("sent", ctypes.c_int)]


class TIn(ctypes.Structure):
    _fields_ = [("dst", ctypes.POINTER(ctypes.c_ubyte)), ("cap", ctypes.c_int),
                ("got", ctypes.c_int)]


OUT = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_int)
IN = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int)
WAIT = ctypes.CFUNCTYPE(None, ctypes.c_double)
NOW = ctypes.CFUNCTYPE(ctypes.c_double)
SLEEP = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_double, ctypes.c_int)


def build(tmp, name, broken=None):
    """the shim on proto.h, or on a copy with one rule broken"""
    inc = DRV
    if broken:
        old, new = BROKEN[broken]
        with open(os.path.join(DRV, "proto.h")) as f:
            text = f.read()
        if text.count(old) != 1:
            raise AssertionError("proto.h changed: %r" % old)
        inc = os.path.join(tmp, name)
        os.makedirs(inc)
        with open(os.path.join(inc, "proto.h"), "w") as f:
            f.write(text.replace(old, new))
    src = os.path.join(tmp, name + ".c")
    with open(src, "w") as f:
        f.write(SHIM)
    lib = os.path.join(tmp, name + ".so")
    subprocess.check_call(["gcc", "-shared", "-fPIC", "-O1", "-Wall",
                           "-Werror", "-I", inc, "-o", lib, src])
    return ctypes.CDLL(lib)


def sysex(n, mul, add):
    return bytes([0xF0] + [(i * mul + add) % 128 for i in range(n - 2)] +
                 [0xF7])


class Bench:
    """a unit on a port, and proto.h's functions on it"""

    def __init__(self, lib, unit):
        self.lib, self.unit = lib, unit
        self.hooks = (OUT(unit.out_), IN(unit.in_), WAIT(unit.wait),
                      NOW(lambda: unit.t), SLEEP(unit.sleep))
        lib.t_hooks(*self.hooks)

    def drive(self, a=b"", b=b"", irq=True, out_us=OUT_US, in_us=IN_US,
              quiet_us=20000.0, cap=8192, limit_us=3e6):
        outs = (TOut * 2)(TOut(a, len(a), 0), TOut(b, len(b), 0))
        bufs = [(ctypes.c_ubyte * cap)() for _ in range(2)]
        ptr = ctypes.POINTER(ctypes.c_ubyte)
        ins = (TIn * 2)(*(TIn(ctypes.cast(buf, ptr), cap, 0) for buf in bufs))
        self.lib.t_drive(outs, ins, int(irq), ctypes.c_double(out_us),
                         ctypes.c_double(in_us), ctypes.c_double(quiet_us),
                         ctypes.c_double(self.unit.t + limit_us))
        return [bytes(bufs[i][:min(ins[i].got, cap)]) for i in range(2)]

    def get_all(self, i, most=300):
        got = []
        while len(got) < most and self.lib.t_waiting(i):
            got.append(self.lib.t_get(i))
        return got


class Base(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.lib = build(cls.tmp, "shim")
        cls.broken = {}

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def bench(self, broken=None, **kw):
        lib = self.lib
        if broken:
            if broken not in self.broken:
                self.broken[broken] = build(self.tmp, "b%d" % len(self.broken),
                                            broken)
            lib = self.broken[broken]
        return Bench(lib, Unit(**kw))


class ProtoTest(Base):
    def test_every_byte_out_of_each_output(self):
        for port in (0, 1):
            b = self.bench()
            for v in range(256):
                while not b.lib.t_room():
                    b.unit.wait(50)
                b.lib.t_put(port, v)
            b.unit.wait(10 * BYTE_US)
            self.assertEqual(b.unit.out[port], list(range(256)))
            self.assertEqual(b.unit.out[1 - port], [])
            self.assertLess(b.unit.longest_low, 10)

    def test_every_byte_in_from_each_input(self):
        for port in (0, 1):
            b = self.bench()
            got = []
            for first in range(0, 256, 15):
                b.unit.rings[port] = list(range(first, min(first + 15, 256)))
                self.assertFalse(b.lib.t_waiting(1 - port))
                got += b.get_all(port)
            self.assertEqual(got, list(range(256)))

    def test_room_and_credit(self):
        b = self.bench()
        self.assertEqual(b.lib.t_credit(0), 4)
        self.assertEqual(b.lib.t_credit(3), 3)
        # four into an empty unit: room again some 970 us later, once the
        # last of them is being sent, as on the Armada
        for _ in range(4):
            b.lib.t_put(0, 0x10)
        self.assertFalse(b.lib.t_room())
        self.assertEqual(b.lib.t_credit(0), 0)
        b.unit.wait(900)
        self.assertFalse(b.lib.t_room())
        b.unit.wait(100)
        self.assertTrue(b.lib.t_room())

    def test_bytes_in_a_row(self):
        # into an idle unit: the first is sent at once and seven wait, as on
        # the Armada, where 8 in a row came back and 9 gave only the first
        for n, back in ((8, 8), (9, 1), (10, 2), (16, 8)):
            b = self.bench()
            for v in range(n):
                b.lib.t_put(0, 0x20 + v)
            b.unit.wait(10 * BYTE_US)
            self.assertEqual(len(b.unit.out[0]), back, n)
            self.assertEqual(b.unit.out[0][0], 0x20)

    def test_the_open_command_clears_the_rings(self):
        b = self.bench()
        b.unit.rings = [[1, 2, 3], [4]]
        b.lib.t_start()
        self.assertEqual(b.unit.commands, [0x07])
        self.assertEqual(b.unit.rings, [[], []])
        self.assertEqual(b.unit.data, 0xFF)

    def test_the_question(self):
        b = self.bench()
        self.assertEqual(b.lib.t_id(), MODEL_ID)
        b.unit.wait(2 * BYTE_US)
        self.assertEqual(b.unit.out[0], [0x11])
        b.unit.rings[1] = [0x40]
        self.assertEqual(b.lib.t_id(), 0)

    def test_alive(self):
        b = self.bench()
        self.assertTrue(b.lib.t_alive())
        nothing = self.bench(there=False)
        self.assertFalse(nothing.lib.t_alive())

    def test_a_unit_fed_comes_up(self):
        b = self.bench(fed=False)
        self.assertEqual(b.unit.in_(STATUS), 0xFF)
        b.lib.t_start()
        self.assertTrue(b.lib.t_alive())
        self.assertEqual(b.drive(a=sysex(64, 3, 1)), [sysex(64, 3, 1), b""])


class DriveTest(Base):
    A = sysex(1024, 7, 3)
    B = sysex(1024, 11, 5)

    def check(self, got, a, b, unit):
        self.assertEqual(got[0], a)
        self.assertEqual(got[1], b)
        self.assertEqual((unit.lost, unit.overruns), (0, 0))
        self.assertLess(unit.longest_low, 10)

    def test_each_port_and_both_by_interrupt(self):
        for a, b in ((self.A, b""), (b"", self.B), (self.A, self.B)):
            bench = self.bench()
            got = bench.drive(a=a, b=b, irq=True)
            self.check(got, a, b, bench.unit)

    def test_each_port_and_both_polled(self):
        for a, b in ((self.A, b""), (b"", self.B), (self.A, self.B)):
            bench = self.bench()
            got = bench.drive(a=a, b=b, irq=False)
            self.check(got, a, b, bench.unit)

    def test_full_speed(self):
        # both cables at MIDI's speed, give or take a few bytes; at a period
        # of 400 us an output has run dry before the timer looks again
        for us, fast in ((OUT_US, True), (400.0, False)):
            bench = self.bench()
            t0 = bench.unit.t
            bench.drive(a=self.A, b=self.B, out_us=us, quiet_us=0)
            took = bench.unit.t - t0
            self.assertEqual(took < (len(self.A) + 8) * BYTE_US, fast, us)

    def test_input_alone_by_interrupt(self):
        bench = self.bench()
        data = sysex(300, 5, 2)
        bench.unit.feed(1, data)
        got = bench.drive(irq=True, quiet_us=50000)
        self.assertEqual(got, [b"", data])
        self.assertGreater(bench.unit.edges, 0)
        self.assertEqual(bench.unit.overruns, 0)


class NegativeTest(Base):
    """each broken rule loses or garbles bytes on the model"""

    def test_byte_left_on_the_data_lines(self):
        # a unit without its supply reads FFh: no room, no bytes, nothing out
        data = sysex(64, 3, 1)
        for broken, alive in ((None, True),
                              ("byte left on the data lines", False)):
            b = self.bench(broken)
            b.lib.t_put(0, 0x00)
            b.unit.wait(1.1e6)
            self.assertEqual(not b.unit.dead, alive, broken)
            got = b.drive(a=data, limit_us=0.5e6)
            self.assertEqual(got, [b"\x00" + data if alive else b"", b""],
                             broken)

    def test_eight_on_a_sight_of_room(self):
        b = self.bench("eight on a sight of room")
        a, bb = sysex(256, 7, 3), sysex(256, 11, 5)
        got = b.drive(a=a, b=bb)
        self.assertGreater(b.unit.lost, 0)
        self.assertNotEqual(got, [a, bb])

    def test_pairs_read_high_first(self):
        b = self.bench("pairs read high first")
        b.unit.rings[0] = [0x12, 0x34]
        self.assertEqual(b.get_all(0), [0x84, 0x1C])

    def test_rests_too_short(self):
        b = self.bench("rests too short")
        for v in (0x12, 0x34):
            b.lib.t_put(0, v)
        b.unit.wait(4 * BYTE_US)
        self.assertNotEqual(b.unit.out[0], [0x12, 0x34])
        b.unit.rings[1] = [0x12, 0x34]
        self.assertNotEqual(b.get_all(1), [0x12, 0x34])

    def test_a_poll_too_slow(self):
        # an input at full speed fills its ring in 4.8 ms
        data = sysex(300, 5, 2)
        for us, exact in ((IN_US, True), (6000.0, False)):
            b = self.bench()
            b.unit.feed(1, data)
            got = b.drive(irq=False, in_us=us)
            self.assertEqual(got == [b"", data], exact, us)
            self.assertEqual(b.unit.overruns == 0, exact, us)


class PatchTest(unittest.TestCase):
    KCONFIG = (
        "config SND_MTPAV\n"
        "\ttristate \"MOTU MidiTimePiece AV multiport MIDI\"\n"
        "\tdepends on HAS_IOPORT\n"
        "\tselect SND_RAWMIDI\n"
        "\thelp\n"
        "\t  To use a MOTU MidiTimePiece AV multiport MIDI adapter\n"
        "\t  connected to the parallel port, say Y here and make sure that\n"
        "\t  the standard parallel port driver isn't used for the port.\n"
        "\n"
        "\t  To compile this driver as a module, choose M here: the module\n"
        "\t  will be called snd-mtpav.\n"
        "\n"
        "config SND_MTS64\n"
        "\ttristate \"ESI Miditerminal 4140 driver\"\n"
        "\tdepends on PARPORT\n"
        "\tselect SND_RAWMIDI\n"
        "\thelp\n"
        "\t  The ESI Miditerminal 4140 is a 4 In 4 Out MIDI Interface with\n"
        "\t  additional SMPTE Timecode capabilities for the parallel port.\n"
        "\n"
        "\t  Say 'Y' to include support for this device.\n"
        "\n"
        "\t  To compile this driver as a module, chose 'M' here: the module\n"
        "\t  will be called snd-mts64.\n"
        "\n"
        "config SND_PORTMAN2X4\n"
        "\ttristate \"Portman 2x4 driver\"\n"
        "\tdepends on PARPORT\n"
        "\tselect SND_RAWMIDI\n"
        "\thelp\n"
        "\t  Say Y here to include support for Midiman Portman 2x4 parallel\n"
        "\t  port MIDI device.\n"
        "\n"
        "\t  To compile this driver as a module, choose M here: the module\n"
        "\t  will be called snd-portman2x4.\n"
        "\n"
        "config SND_AC97_POWER_SAVE\n"
        "\tbool \"AC97 Power-Saving Mode\"\n")
    MAKE = (
        "snd-mtpav-y := mtpav.o\n"
        "snd-mts64-y := mts64.o\n"
        "\n"
        "# Toplevel Module Dependency\n"
        "obj-$(CONFIG_SND_SERIAL_U16550) += snd-serial-u16550.o\n"
        "obj-$(CONFIG_SND_SERIAL_GENERIC) += snd-serial-generic.o\n"
        "obj-$(CONFIG_SND_MTPAV) += snd-mtpav.o\n"
        "obj-$(CONFIG_SND_MTS64) += snd-mts64.o\n"
        "obj-$(CONFIG_SND_PORTMAN2X4) += snd-portman2x4.o\n"
        "\n"
        "obj-$(CONFIG_SND) += opl3/ opl4/ mpu401/ vx/ pcsp/\n")

    def tree(self, tmp, kconfig, make):
        os.makedirs(os.path.join(tmp, "sound", "drivers"))
        for name, text in (("Kconfig", kconfig), ("Makefile", make)):
            with open(os.path.join(tmp, "sound", "drivers", name), "w") as f:
                f.write(text)

    def make_patch(self, tool, kconfig, make, tmp):
        tree = os.path.join(tmp, "src")
        self.tree(tree, kconfig, make)
        out = os.path.join(tmp, tool + ".patch")
        with contextlib.redirect_stdout(io.StringIO()):
            __import__(tool).main(["--ksrc", tree, "-o", out])
        return out

    def test_the_patch_adds_the_driver_and_hooks_it_in(self):
        with tempfile.TemporaryDirectory() as tmp:
            with open(self.make_patch("flyerpatch", self.KCONFIG, self.MAKE,
                                      tmp)) as f:
                text = f.read()
        self.assertIn("+++ b/sound/drivers/flyer/flyer.c", text)
        self.assertIn("+++ b/sound/drivers/flyer/proto.h", text)
        self.assertIn('+source "sound/drivers/flyer/Kconfig"', text)
        self.assertIn("+obj-$(CONFIG_SND_FLYER) += flyer/", text)

    def test_the_patch_refuses_a_tree_that_has_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit):
                self.make_patch("flyerpatch", self.KCONFIG + "# flyer\n",
                                self.MAKE, tmp)

    @unittest.skipUnless(shutil.which("patch"), "needs patch")
    def test_it_and_the_midiengines_apply_in_either_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            patches = [self.make_patch(t, self.KCONFIG, self.MAKE,
                                       os.path.join(tmp, t))
                       for t in ("flyerpatch", "mqenginepatch")]
            for order in (patches, patches[::-1]):
                tree = os.path.join(tmp, "tree%d" % (order is patches))
                self.tree(tree, self.KCONFIG, self.MAKE)
                for p in order:
                    r = subprocess.run(["patch", "-p1", "--fuzz=0", "-s",
                                        "-d", tree, "-i", p],
                                       capture_output=True, text=True)
                    self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                with open(os.path.join(tree, "sound/drivers/Makefile")) as f:
                    make = f.read()
                self.assertIn("obj-$(CONFIG_SND_FLYER) += flyer/", make)
                self.assertIn("obj-$(CONFIG_SND_MQENGINE) += mqengine/", make)


if __name__ == "__main__":
    unittest.main()
