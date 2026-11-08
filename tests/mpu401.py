# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The ES1869's MPU-401 UART as the data sheet gives it (DS p.17-18, 23,
38, 42): the data port at its base and the command and status port at
base+1, with an 8-byte FIFO each way.

Status (base+1): bit 7 is 0 while a byte waits to be read, a MIDI byte or
an acknowledge (FEh); bit 6 is 0 while the output FIFO takes another byte.
FFh resets the UART to Smart mode, dropping what waits to go out, and
answers FEh only if it was in Smart mode already; 3Fh goes to UART mode
and answers FEh from Smart mode, and is ignored in UART mode (DS p.42). In
UART mode a byte written to the data port leaves at MIDI's 31,250 baud,
320 us a byte; with `loop`, a cable from MIDI out to MIDI in, it comes back
into the input FIFO as it ends. `feed()` brings bytes in from another
interface. A full input FIFO loses the byte that comes (`overruns`).

The interrupt request is the input's read-data-available flag: it drops
when the program reads a byte and rises again a moment later while bytes
remain (DS p.17), so each byte gives a rising edge. The chip gates it
(esschip): mixer 64h bit 6 into Config_Base+6 bit 3, and card register 28h
with Config_Base+7 bit 3 onto the interrupt pin."""

BYTE_US = 320       # a MIDI byte: 10 bits at 31,250 baud
ACK_US = 4          # an acknowledge's delay (not measured)
FIFO = 8
GAP_US = 1          # the request's low time after a read, bytes remaining
ACK = 0xFE


class MPU401:
    def __init__(self, loop=False):
        self.loop = loop
        self.uart = False
        self.rx = []                # the input FIFO
        self.tx = []                # bytes waiting to go out
        self.wire = None            # (byte, the us it ends) on MIDI out
        self.due = []               # (us, byte): acknowledges and input
        self.sent = []              # (us, byte): what left by MIDI out
        self.got = []               # (us, byte): what the program read
        self.ignored = []           # (us, port offset, value) not taken
        self.overruns = 0
        self.tx_lost = 0            # written with no room, or reset away
        self.low_until = 0
        self.now = 0
        self.last = 0xFF

    # --- time ---------------------------------------------------------------
    def step(self, now):
        """everything due by `now`, in order"""
        self.now = now
        while True:
            t_wire = self.wire[1] if self.wire is not None else None
            t_due = self.due[0][0] if self.due else None
            if t_wire is not None and t_wire <= now and \
                    (t_due is None or t_wire <= t_due):
                b, t = self.wire
                self.sent.append((t, b))
                self.wire = None
                if self.loop:
                    self._arrive(t, b)
                self._next(t)
            elif t_due is not None and t_due <= now:
                t, b = self.due.pop(0)
                self._arrive(t, b)
            else:
                break

    def _arrive(self, t, b):
        if len(self.rx) >= FIFO:
            self.overruns += 1
        else:
            self.rx.append(b)

    def _next(self, t):
        if self.wire is None and self.tx:
            self.wire = (self.tx.pop(0), t + BYTE_US)

    def feed(self, data, at):
        """bytes from another interface's MIDI out, the first arriving at
        `at` us and the others 320 us apart"""
        for i, b in enumerate(data):
            self.due.append((at + (i + 1) * BYTE_US, b & 0xFF))
        self.due.sort(key=lambda d: d[0])

    def request(self, now):
        return bool(self.rx) and now >= self.low_until

    # --- ports --------------------------------------------------------------
    def inb(self, off, now):
        self.step(now)
        if off == 1:
            return (0x80 if not self.rx else 0) | \
                (0x40 if len(self.tx) >= FIFO else 0) | 0x3F
        if self.rx:
            self.last = self.rx.pop(0)
            self.got.append((now, self.last))
            self.low_until = now + GAP_US
        return self.last

    def outb(self, off, v, now):
        self.step(now)
        v &= 0xFF
        if off == 1:
            if v == 0xFF:
                ack = not self.uart
                self.uart = False
                self.tx_lost += len(self.tx)
                self.tx = []
                if ack:
                    self._ack(now)
            elif v == 0x3F and not self.uart:
                self.uart = True
                self._ack(now)
            else:
                self.ignored.append((now, off, v))
            return
        if not self.uart:
            self.ignored.append((now, off, v))
        elif len(self.tx) >= FIFO:
            self.tx_lost += 1
        else:
            self.tx.append(v)
            self._next(now)

    def _ack(self, now):
        self.due.append((now + ACK_US, ACK))
        self.due.sort(key=lambda d: d[0])
