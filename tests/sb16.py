# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""A Creative Sound Blaster 16 at the port level, the reference for the
tests of the Sound Blaster 16 fronts.

The model is the card a game finds: the DSP (CT1741, version 4.05 unless
told) at Audio_Base+6, +Ah, +Ch, +Eh and +Fh, the CT1745 mixer at +4 and
+5, its two DMA engines on a dma8237 controller, and the interrupt line.
It follows the DSP's own firmware as the S95Sedan/CT1741_DSP disassembly of
the 4.04-4.16 ROMs shows it, and Creative's Hardware Programming Guide for
the mixer; docs/SB16.md lists what it does and the sources.

Time passes in advance(us): a running transfer moves one sample a sample
period, a block's end raises the engine's interrupt, and what the DAC
plays is kept in `played` as frames of signed 16-bit samples. The ADPCM
decoders are DOSBox-X's (GPL-2.0 or later).
"""

COPYRIGHT = b"COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.\0"
RESET_US = 50               # AAh comes this long after the reset's end
BUSY_US = 100               # D0h and D5h keep the busy bit this long
ROM_ID = {(4, 4): (0x75, 0x92), (4, 5): (0x7D, 0x1B)}

# the mode register (X-bus 04h)
M_ROUTE16 = 0x01            # DAC from the 16-bit engine, ADC to the 8-bit
M_TICK = 0x02               # a tick each sample: silence, ADPCM
M_COUNT = 0x04              # interrupt at the DMA count's end
M_SIGNED8, M_SIGNED16 = 0x10, 0x20
M_MONO8, M_MONO16 = 0x40, 0x80


def v_hz(v):
    """the rate generator's frequency for the value v"""
    return v * 44100 / 248


def tc_to_v(tc, version=(4, 5)):
    """40h: the rate generator's value for a time constant"""
    t = min(tc, 0xEB)
    if t == 0x68:
        return 0x25
    if t == 0x6C:
        return 0x26
    if version < (4, 13):
        if t == 0xC8:
            return 0xC8             # the ROM table's error: 35.6 kHz
    elif t == 0x83:
        return 0x2D
    return min(0xFF, 248000000 // ((256 - t) * 44100))


def rate_to_v(rate, version=(4, 5)):
    """41h/42h: the value for a rate in Hz, with the carry that DSPs before
    4.13 lose (23000 Hz becomes 20094 Hz)"""
    hi, lo = rate >> 8, rate & 0xFF
    if hi >= 0xB1:
        return 0xFF
    if hi < 0x13:
        return 0x1C
    s = (23 * hi & 0xFF) + ((23 * lo) >> 8)
    carry = 0 if version < (4, 13) else s >> 8
    x = (((23 * hi) >> 8) + carry) << 8 | (s & 0xFF)
    return (x >> 4) + ((x >> 3) & 1)


# Creative ADPCM, as DOSBox-X decodes it
ADPCM4_SCALE = (0, 1, 2, 3, 4, 5, 6, 7, 0, -1, -2, -3, -4, -5, -6, -7,
                1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15,
                2, 6, 10, 14, 18, 22, 26, 30, -2, -6, -10, -14, -18, -22,
                -26, -30, 4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28,
                -36, -44, -52, -60)
ADPCM4_ADJUST = (0, 0, 0, 0, 0, 16, 16, 16) * 2 + \
    (240, 0, 0, 0, 0, 16, 16, 16) * 4 + (240, 0, 0, 0, 0, 0, 0, 0) * 2
ADPCM3_SCALE = (0, 1, 2, 3, 0, -1, -2, -3, 1, 3, 5, 7, -1, -3, -5, -7,
                2, 6, 10, 14, -2, -6, -10, -14, 4, 12, 20, 28, -4, -12, -20,
                -28, 5, 15, 25, 35, -5, -15, -25, -35)
ADPCM3_ADJUST = (0, 0, 0, 8, 0, 0, 0, 8) + (248, 0, 0, 8) * 6 + \
    (248, 0, 0, 0) * 2
ADPCM2_SCALE = (0, 1, 0, -1, 1, 3, -1, -3, 2, 6, -2, -6, 4, 12, -4, -12,
                8, 24, -8, -24, 16, 48, -16, -48)
ADPCM2_ADJUST = (0, 4, 0, 4) + (252, 4) * 8 + (252, 0, 252, 0)
ADPCM = {4: (ADPCM4_SCALE, ADPCM4_ADJUST, 63),
         3: (ADPCM3_SCALE, ADPCM3_ADJUST, 39),
         2: (ADPCM2_SCALE, ADPCM2_ADJUST, 23)}


def adpcm_codes(byte, bits):
    """the codes of one ADPCM byte, first played first"""
    if bits == 4:
        return [byte >> 4, byte & 0x0F]
    if bits == 3:
        return [(byte >> 5) & 7, (byte >> 2) & 7, (byte & 3) << 1]
    return [(byte >> 6) & 3, (byte >> 4) & 3, (byte >> 2) & 3, byte & 3]


class Clock:
    """microseconds of emulated time"""

    def __init__(self, us=0):
        self.us = us

    def advance(self, us):
        self.us += us


class Engine:
    """one of the DSP's two DMA engines"""

    def __init__(self, kind):
        self.kind = kind            # 8 or 16: the interrupt it raises
        self.clear()

    def clear(self):
        self.running = False
        self.auto = False           # the auto-initialize flag
        self.switch = False         # a single-cycle length waits
        self.switch_len = 0
        self.latch = 0              # a block's length less one
        self.count = 0              # units left in this block
        self.gate = False           # its DMA requests on
        self.stalled = False
        self.expired = False        # the count ran out unserviced (EX1 off)
        self.rec = False
        self.coding = "pcm"         # "pcm", "silence", or ADPCM bits 2-4
        self.ref = False            # an ADPCM block starts with a reference
        self.codes = []             # ADPCM codes still to play
        self.reference = 0x80
        self.step = 0
        self.part = b""             # the bytes of a frame not complete
        self.debt = 0.0             # samples owed by elapsed time
        self.end_auto_at_end = False    # 14h's flag clears at the block end


class SB16:
    """the DSP, its two DMA engines and the CT1745 mixer"""

    def __init__(self, dma, base=0x220, irq=5, dma8=1, dma16=5, clock=None,
                 version=(4, 5), mixer_80_high=0xF0, mixer_unknown=0x00):
        self.dma = dma
        self.base, self.irq, self.dma8 = base, irq, dma8
        self.dma16 = dma16          # 0: 16-bit transfers on the 8-bit one
        self.clock = clock or Clock()
        self.version = version
        self.mixer_80_high = mixer_80_high
        self.mixer_unknown = mixer_unknown
        self.played = []            # the DAC's frames
        self.unheard = []           # an engine's output not routed to it
        self.recorded = []          # the ADC's frames, from `source`
        self.source = None          # callable(n) -> n frames to record
        self.midi_out = []
        self.irq_edges = []         # (us, kinds pending) at each 0 -> 1
        self.raised = []            # (us, 8 or 16) for every interrupt
        self.e8, self.e16 = Engine(8), Engine(16)
        self.pending = {8: False, 16: False}
        self.irq_line = False
        self.test = 0               # E4h's register survives a reset
        self.csp_id = (0, 0)
        self.last_rate = (0, 0)     # RAM 13h/14h
        self.len16 = 0              # RAM 0Ch/0Dh
        self.mixer_index = 0
        self.mixer_reset()
        self.reset_high = False
        self.warm = False
        self.latch = 0
        self.cold_boot()
        self.out_full = True        # a powered-up DSP has said AAh
        self.latch = 0xAA

    # --- the firmware's state ------------------------------------------------
    def cold_boot(self):
        self.boot(warm=False)

    def boot(self, warm):
        """the firmware's start after a reset"""
        for e in (self.e8, self.e16):
            e.clear()
        self.lock = 0
        self.ex1 = False
        self.hung = False
        self.midi = None            # None, "read" or "uart"
        self.e2_waiting = None      # a byte E2h waits to write
        self.sine = None            # F0h's place in its table
        self.timer = False
        self.busy_until = None
        if not warm:
            self.mode = 0x60
            self.v = 0x38           # 9958 Hz
            self.hw_v = 0xF8
            self.block8 = 0x07FF
            self.e2 = [0xAA, 0x96]
            self.speaker = False
        self.warm = False
        self.cmd = None
        self.ops = []
        self.need = 0
        self.upload = None          # 01h's bytes still to come
        self.outq = []              # bytes the firmware still has to write
        self.in_byte = None         # the input latch, while blocked
        self.prev_cmd = 0
        self.cur_cmd = 0
        self.ram = bytearray(256)
        self._line()

    # --- the output latch ----------------------------------------------------
    def _put(self, *data):
        """bytes for 2xA: the firmware writes each once the last is read"""
        for b in data:
            if not self.out_full:
                self.latch, self.out_full = b & 0xFF, True
            else:
                self.outq.append(b & 0xFF)

    def _blocked(self):
        """the firmware can't take a command: it writes a reply, waits for
        a DMA cycle, or hangs"""
        return bool(self.outq) or self.hung or self.e2_waiting is not None \
            or self.ready_at is not None

    # --- ports ---------------------------------------------------------------
    ready_at = None             # the reset's AAh, when it is due

    def handles(self, port):
        return self.base + 4 <= port <= self.base + 0x0F

    def inb(self, port):
        self._settle()
        off = port - self.base
        if off == 4:
            return self.mixer_index
        if off == 5:
            return self.mixer_read(self.mixer_index)
        if off == 0x0A:
            v = self.latch
            if self.out_full:
                self.out_full = False
                if self.outq:
                    self.latch, self.out_full = self.outq.pop(0), True
                if not self.outq:
                    self._resume()
            return v
        if off == 0x0C:
            return 0xFF if self._busy() else 0x7F
        if off == 0x0E:
            self._ack(8)
            return 0xFF if self.out_full else 0x7F
        if off == 0x0F:
            self._ack(16)
            return 0xFF
        return 0xFF

    def _busy(self):
        if self.reset_high or self.ready_at is not None:
            return True
        if self.in_byte is not None:
            return True             # the input latch is full
        if self.busy_until is not None:
            if self.clock.us < self.busy_until:
                return True
            self.busy_until = None
        if self._blocked():
            return False            # inside a handler: low
        return self.clock.us % 8 < 3    # the idle loop's busy pin

    def outb(self, port, v):
        self._settle()
        off = port - self.base
        v &= 0xFF
        if off == 4:
            self.mixer_index = v
        elif off == 5:
            self.mixer_write(self.mixer_index, v)
        elif off == 6:
            if v & 1:
                if not self.reset_high:
                    self.reset_high = True
                    self._halt()
            elif self.reset_high:
                self.reset_high = False
                self.boot(warm=self.warm)
                self.out_full = False
                self.ready_at = self.clock.us + RESET_US
        elif off == 0x0C:
            self._write(v)

    def _halt(self):
        """the 8052 held in reset: its DMA gates off, the latches clear"""
        for e in (self.e8, self.e16):
            e.gate = False
            e.running = False
        self.out_full = False
        self.outq = []
        self.in_byte = None
        self.pending = {8: False, 16: False}
        self._line()

    def _settle(self):
        if self.ready_at is not None and self.clock.us >= self.ready_at:
            self.ready_at = None
            self.latch, self.out_full = 0xAA, True
            self._resume()

    def _write(self, v):
        """a byte into the input latch"""
        self.sine = None
        for e in (self.e8, self.e16):
            if e.stalled:               # a host byte frees a stalled engine
                self._unstall(e)
        if self.reset_high:
            return
        if self._blocked():
            self.in_byte = v            # waits; a second byte overwrites
            return
        self._byte(v)

    def _resume(self):
        """the firmware took its last reply: the waiting byte is next"""
        if self.in_byte is not None and not self._blocked():
            v, self.in_byte = self.in_byte, None
            self._byte(v)

    # --- the command loop ----------------------------------------------------
    def operands(self, c):
        """the bytes the firmware reads after command c"""
        hi, lo = c >> 4, c & 0x0F
        if hi == 0x0:
            return {2: 1, 4: 1, 5: 2, 0x0B: 1, 0x0C: 1, 0x0E: 2,
                    0x0F: 1}.get(lo, 0)
        if hi == 0x1:
            return 1 if lo < 4 else 2 if lo < 8 else 0
        if hi == 0x2:
            return 2 if 4 <= lo < 8 else 0
        if hi == 0x3:
            return 1 if lo >= 8 else 0
        if hi == 0x4:
            if lo == 3 and self.version >= (4, 11):
                return 2
            if lo == 0x0F:
                return 0 if self.timer else 2
            return {0: 1, 1: 2, 2: 2, 8: 2, 0x0C: 2, 0x0D: 1,
                    0x0E: 2}.get(lo, 0)
        if hi == 0x7:
            return 2 if 4 <= lo < 8 else 0
        if hi == 0x8:
            return 2
        if hi in (0xB, 0xC):
            return 3
        if hi == 0xD:
            return 1 if lo == 0x0C and self.version >= (4, 11) else 0
        if hi == 0xE:
            return 1 if lo in (0, 2, 4) else 0
        if hi == 0xF:
            return {9: 1, 0x0A: 2}.get(lo, 0)
        return 0

    def _byte(self, v):
        if self.hung:
            return
        if self.midi == "uart":
            self.midi_out.append(v)
            return
        if self.midi == "read":
            self.midi = None        # any byte leaves the MIDI-in mode
            return
        if self.upload is not None:
            self._upload(v)
            return
        if self.cmd is not None:
            self.ops.append(v)
            if len(self.ops) < self.need:
                return
            cmd, ops = self.cmd, self.ops
            self.cmd, self.ops = None, []
            self.command(cmd, ops)
            return
        self.prev_cmd, self.cur_cmd = self.cur_cmd, v
        if v == 0x01 and self.lock == 0:
            self._put(0x00)
            self.upload = {"len": None, "got": []}
            return
        n = self.operands(v)
        if n:
            self.cmd, self.ops, self.need = v, [], n
            return
        self.command(v, [])

    def _upload(self, v):
        """01h: a CSP program: its length, the bytes, a checksum and an ID"""
        up = self.upload
        up["got"].append(v)
        if up["len"] is None:
            if len(up["got"]) == 2:
                up["len"] = up["got"][0] | (up["got"][1] << 8)
                up["got"] = []
            return
        if len(up["got"]) < max(up["len"] - 3, 0) + 4:
            return
        body = up["got"][:-4]
        csum = up["got"][-4] | (up["got"][-3] << 8)
        self.upload = None
        if (sum(body) & 0xFFFF) != csum:
            self._put(0xFF)
        else:
            self._put(0xFF)         # no CSP: its register reads FFh
            self.csp_id = (up["got"][-2], up["got"][-1])

    def command(self, c, ops):
        """one command with its operands, as the 4.05 firmware runs it"""
        hi, lo = c >> 4, c & 0x0F
        if hi == 0x0:
            self._csp(lo, ops)
        elif hi == 0x1:
            if lo < 4:
                self._direct_dac(ops[0])
            elif lo < 8:
                self._old(8, False, False, ops, 2 if lo & 2 else 8,
                          bool(lo & 1))
            else:
                self._old(8, False, True, None, 2 if lo & 2 else 8,
                          bool(lo & 1))
        elif hi == 0x2:
            if lo < 4:
                self.mode = 0x61
                self._put(self._adc_byte())
            else:
                self._old(8, True, lo >= 8, ops if lo < 8 else None, 8,
                          False)
        elif hi == 0x3:
            if lo < 4:
                self.midi = "read"
            elif lo < 8:
                self.midi = "uart"
                self.warm = True
            else:
                self.midi_out.append(ops[0])
        elif hi == 0x4:
            self._setup(lo, ops)
        elif hi == 0x7:
            bits = 3 if lo & 2 else 4
            if 4 <= lo < 8:
                self._old(8, False, False, ops, bits, bool(lo & 1))
            else:
                self._old(8, False, True, None, bits, bool(lo & 1))
        elif hi == 0x8:
            self._silence(ops[0] | (ops[1] << 8))
        elif hi == 0x9:
            self.warm = True
            self._old(8, bool(lo & 8), not lo & 1, None, 8, False,
                      always_full=True)
        elif hi == 0xB:
            self._generic(self.e16, c, ops)
        elif hi == 0xC:
            self._generic(self.e8, c, ops)
        elif hi == 0xD:
            self._misc(lo)
        elif hi == 0xE:
            self._ident(lo, ops)
        elif hi == 0xF:
            self._aux(lo, ops)

    # --- group 0: the CSP, which this card doesn't have ----------------------
    def _csp(self, lo, ops):
        if lo in (0x03, 0x08):        # 02h waits for the CSP or the next
            self._put(0xFF)         # byte, which is the next command
        elif lo == 0x06:
            self.lock += 1
        elif lo == 0x07:
            self.lock = max(0, self.lock - 1)
        elif lo == 0x09:
            self._put(*self.csp_id)
        elif lo == 0x0A:
            self._put(self.lock & 0xFF, 0xFF)
        elif lo in (0x0B, 0x0C):
            self.hung = True        # waits for the CSP until a reset
        elif lo == 0x0F:
            self._put(self._xbus(ops[0]))

    def _xbus(self, reg):
        if reg == 0x04:
            return self.mode
        if reg == 0x09:
            return self.hw_v
        return 0xFF if 0x80 <= reg <= 0x83 else 0x00

    # --- group 4 -------------------------------------------------------------
    def _setup(self, lo, ops):
        if lo == 0x0:
            self.v = tc_to_v(ops[0], self.version)
        elif lo in (0x1, 0x2):
            self.last_rate = (ops[1], ops[0])
            self.v = rate_to_v((ops[0] << 8) | ops[1], self.version)
        elif lo == 0x3 and self.version >= (4, 11):
            self.v = self.hw_v = rate_to_v((ops[0] << 8) | ops[1],
                                           self.version)
        elif lo == 0x8:
            self.block8 = ops[0] | (ops[1] << 8)
        elif lo == 0xD:
            self._put(self.ram[0x1B + (ops[0] & 3)])
        elif lo == 0xC:
            self.ram[0x1B + (ops[0] & 3)] = ops[1]

    # --- transfers -----------------------------------------------------------
    def _route(self, e, rec):
        """mode bit 0: the DAC hears the 16-bit engine's output, or the ADC
        feeds the 8-bit engine"""
        return 1 if (e is self.e16) != rec else 0

    def _generic(self, e, c, ops):
        """Bxh/Cxh: bit 3 records, bit 2 auto-initializes, bit 1 the FIFO,
        bit 0 ignored; the mode's bit 4 signed, bit 5 stereo"""
        rec, auto = bool(c & 8), bool(c & 4)
        length = ops[1] | (ops[2] << 8)
        self.hw_v = self.v
        if not auto and e.auto:     # queued for one last block
            e.auto, e.switch, e.switch_len = False, True, length
            e.gate = self.ex1 = True
            return
        route = self._route(e, rec)
        mode = ops[0]
        if e is self.e8:
            self.mode = (self.mode & 0xA0) | (mode & 0x10) | \
                (0 if mode & 0x20 else M_MONO8) | M_COUNT | route
            self.block8 = length
        else:
            self.mode = (self.mode & 0x50) | ((mode & 0x10) << 1) | \
                (0 if mode & 0x20 else M_MONO16) | M_COUNT | route
            self.len16 = length
        self._full_start(e, rec, auto, length, "pcm", False)

    def _old(self, kind, rec, auto, ops, bits, ref, always_full=False):
        """the 8-bit engine's older commands: 1xh, 2xh, 7xh, 9xh; length
        from the operands, or the block size of 48h and the last Cxh"""
        e = self.e8
        length = (ops[0] | (ops[1] << 8)) if ops else self.block8
        self.hw_v = self.v
        if not always_full and not auto and ops is not None:
            queue = e.auto if not rec else e.running
            if queue:
                e.switch, e.switch_len = True, length
                if bits == 8 and not rec:
                    e.end_auto_at_end = True    # 14h: the flag clears later
                else:
                    e.auto = False
                e.gate = self.ex1 = True
                return
        self.mode = (self.mode & 0xF0) | M_COUNT | (1 if rec else 0)
        coding = "pcm" if bits == 8 else bits
        if coding != "pcm":
            self.mode |= M_TICK
        self._full_start(e, rec, auto, length, coding, ref)

    def _full_start(self, e, rec, auto, length, coding, ref):
        e.clear()
        e.rec, e.auto = rec, auto
        e.latch = length
        e.count = length + 1
        e.coding, e.ref = coding, ref
        e.gate = e.running = True
        self.ex1 = True

    def _silence(self, length):
        e = self.e8
        self.mode = (self.mode & 0xF0) | M_TICK
        e.clear()
        e.coding = "silence"
        e.latch, e.count = length, length + 1
        e.gate = e.running = True
        self.hw_v = self.v
        self.ex1 = True

    def _direct_dac(self, sample):
        self.mode = 0x60
        self.played.append(((sample - 128) << 8,))

    def _adc_byte(self):
        frame = self.source(1)[0] if self.source else (0,)
        self.recorded.append(frame)
        return ((frame[0] >> 8) + 128) & 0xFF

    # --- group D -------------------------------------------------------------
    def _misc(self, lo):
        if lo == 0x0:
            self._pause(self.e8)
        elif lo == 0x1:
            self.speaker = True
        elif lo == 0x3:
            self.speaker = False
        elif lo == 0x4:
            self._continue(self.e8, self.block8)
        elif lo == 0x5:
            self._pause(self.e16)
        elif lo == 0x6:
            self._continue(self.e16, self.len16)
        elif lo == 0x8:
            self._put(0xFF if self.speaker else 0x00)
        elif lo == 0x9:
            self.e16.auto = False
        elif lo == 0xA:
            self.e8.auto = False

    def _pause(self, e):
        e.gate = False
        self.ex1 = False
        self.busy_until = self.clock.us + BUSY_US
        if self.v < 0xF8:
            self.hw_v = 0xF8        # 4.05: the shared rate goes to 44.1 kHz

    def _continue(self, e, latch):
        if not e.running:
            return                  # nothing paused: left alone
        self.hw_v = self.v
        e.gate = True
        e.latch = latch
        self.ex1 = True
        if e.expired:
            e.expired = False
            self._block_end(e)
        for other in (self.e8, self.e16):
            if other is not e and other.expired:
                other.expired = False
                self._block_end(other)

    # --- group E -------------------------------------------------------------
    def _ident(self, lo, ops):
        if lo == 0x0:
            self._put(~ops[0] & 0xFF)
        elif lo == 0x1:
            self._put(*self.version)
        elif lo == 0x2:
            acc, key = self.e2
            acc = (acc + (ops[0] ^ key)) & 0xFF
            key = ((key >> 2) | (key << 6)) & 0xFF
            self.e2 = [acc, key]
            self.mode = (self.mode & 0xF0) | 0x03
            self.e2_waiting = acc
            self._e2_try()
        elif lo == 0x3:
            self._put(*COPYRIGHT)
        elif lo == 0x4:
            self.test = ops[0]
        elif lo == 0x8:
            self._put(self.test)

    def _e2_try(self):
        if self.e2_waiting is None or not self.dma.ready(self.dma8):
            return
        self.dma.write(self.dma8, bytes((self.e2_waiting,)))
        self.e2_waiting = None
        self._resume()

    # --- group F -------------------------------------------------------------
    def _aux(self, lo, ops):
        if lo == 0x0:
            self.v = self.hw_v = 0x5A
            self.mode = 0x60
            self.sine, self.sine_debt = 0, 0.0
        elif lo == 0x2:
            self._raise(8)
        elif lo == 0x3:
            self._raise(16)
        elif lo == 0x4:
            self._put(*ROM_ID.get(self.version, (0xA4, 0x6F)))
        elif lo == 0x8:
            self.latch, self.out_full = 0x00, True     # at once, over all
        elif lo == 0x9:
            self._put(self.ram_read(ops[0]))
        elif lo == 0xA:
            self.ram_write(ops[0], ops[1])
        elif lo == 0xB:
            self._put(self.ram_read(0x23))
        elif lo == 0xC:
            self._put(self.ram_read(0x24))
        elif lo == 0xD:
            self._put(self.ram_read(0x30))

    def ram_read(self, a):
        """F9h: the firmware's RAM, its state where it keeps it"""
        if a in (0x0E, 0x0F):
            return (self.block8 >> (8 * (a - 0x0E))) & 0xFF
        if a in (0x0C, 0x0D):
            return (self.len16 >> (8 * (a - 0x0C))) & 0xFF
        if a in (0x13, 0x14):
            return self.last_rate[a - 0x13]
        if a == 0x20:
            return self.cur_cmd
        if a == 0x30:
            return self.prev_cmd
        if a == 0x23:
            v = 0
            for e, shift in ((self.e8, 0), (self.e16, 2)):
                if e.running and e.coding == "pcm":
                    v |= (2 if e.rec else 1) << shift
            v |= 0x10 if self.speaker else 0
            v |= 0x20 if self.midi else 0
            return v | (0x80 if self.v < 0xF8 else 0)
        if a == 0x24:
            return (0x02 if self.e8.switch else 0) | \
                (0x04 if self.e8.auto else 0) | \
                (0x08 if self.e16.switch else 0) | \
                (0x10 if self.e16.auto else 0)
        if a in (0x25, 0x26):
            return self.e2[a - 0x25]
        if a == 0x2A:
            return self.test
        if a == 0x2B:
            return self.lock & 0xFF
        if a == 0x37:
            return self.v
        if a == 0x81:
            return 0x1D if self.version == (4, 5) else 0x19
        if a == 0x82:
            return 0x0A
        return self.ram[a]

    def ram_write(self, a, v):
        if a == 0x37:
            self.v = v
        elif a in (0x0E, 0x0F):
            shift = 8 * (a - 0x0E)
            self.block8 = (self.block8 & ~(0xFF << shift)) | (v << shift)
        elif a == 0x2A:
            self.test = v
        elif a in (0x25, 0x26):
            self.e2[a - 0x25] = v
        else:
            self.ram[a] = v

    # --- interrupts ----------------------------------------------------------
    def _raise(self, kind):
        self.raised.append((self.clock.us, kind))
        self.pending[kind] = True
        self._line()

    def _ack(self, kind):
        if self.pending[kind]:
            self.pending[kind] = False
            self._line()
            e = self.e8 if kind == 8 else self.e16
            if e.stalled:
                self._unstall(e)

    def _line(self):
        line = self.pending[8] or self.pending[16]
        if line and not self.irq_line:
            self.irq_edges.append((self.clock.us, dict(self.pending)))
        self.irq_line = line

    # --- time ----------------------------------------------------------------
    def advance(self, us):
        """up to us of time pass, in steps of a microsecond: the engines
        move their samples; it stops early at a rising edge of the
        interrupt line, for the game's handler; the time that passed"""
        edges = len(self.irq_edges)
        done = 0
        while done < us:
            self.clock.advance(1)
            done += 1
            self._settle()
            self._e2_try()
            self._sine(1)
            for e in (self.e8, self.e16):
                self._run(e, 1)
            if len(self.irq_edges) != edges:
                break
        return done

    SINE = (0x7F, 0x26, 0x01, 0x26, 0x80, 0xD9, 0xFF, 0xD9)

    def _sine(self, us):
        if self.sine is None:
            return
        self.sine_debt += v_hz(self.hw_v) * us / 1e6
        while self.sine_debt >= 1:
            self.sine_debt -= 1
            self.played.append(((self.SINE[self.sine] - 128) << 8,))
            self.sine = (self.sine + 1) & 7

    def _format(self, e):
        """(bits, signed, stereo) of the engine's samples"""
        if e is self.e8:
            return 8, bool(self.mode & M_SIGNED8), not self.mode & M_MONO8
        return 16, bool(self.mode & M_SIGNED16), not self.mode & M_MONO16

    def _channel(self, e):
        if e is self.e16 and self.dma16:
            return self.dma16
        return self.dma8

    def _run(self, e, us):
        if not e.running or not e.gate or e.stalled or e.expired:
            return
        e.debt += v_hz(self.hw_v) * us / 1e6
        while e.debt >= 1 and e.running and e.gate and not e.stalled \
                and not e.expired:
            if not self._sample(e):
                e.debt = 0          # no DMA: the engine waits for it
                return
            e.debt -= 1
            if e.count == 0:
                if self.ex1:
                    self._block_end(e)
                else:
                    e.expired = True

    def _sample(self, e):
        """one sample period of the engine; False when no DMA cycle came"""
        if e.coding == "silence":
            e.count -= 1
            self._out(e, (0,))
            return True
        if e.coding != "pcm":
            return self._adpcm(e)
        bits, signed, stereo = self._format(e)
        channels = 2 if stereo else 1
        size = (bits // 8) * channels
        ch = self._channel(e)
        unit = 2 if ch >= 4 else 1
        units = size // unit        # DMA cycles a frame
        if e.rec:
            frame = self.source(1)[0] if self.source else (0,) * channels
            data = encode([frame], bits, signed, stereo)
            took = self.dma.write(ch, data)
            if took < len(data):
                return False
            self.recorded.append(frame)
        else:
            data = self.dma.read(ch, units)
            if len(data) < size:
                return False
            self._out(e, decode(data, bits, signed, stereo))
        e.count -= channels         # samples, both channels counted
        if e.count < 0:
            e.count = 0
        return True

    def _adpcm(self, e):
        if not e.codes:
            data = self.dma.read(self.dma8, 1)
            if not data:
                return False
            e.count -= 1
            if e.ref:
                e.ref = False
                e.reference, e.step = data[0], 0
                self._out(e, ((e.reference - 128) << 8,))
                return True
            e.codes = adpcm_codes(data[0], e.coding)
        scale, adjust, top = ADPCM[e.coding]
        s = max(0, min(top, e.codes.pop(0) + e.step))
        e.reference = max(0, min(255, e.reference + scale[s]))
        e.step = (e.step + adjust[s]) & 0xFF
        self._out(e, ((e.reference - 128) << 8,))
        return True

    def _out(self, e, frame):
        heard = (self.mode & M_ROUTE16) == (1 if e is self.e16 else 0)
        (self.played if heard else self.unheard).append(frame)

    def _block_end(self, e):
        if e.switch:                # (a) the queued single-cycle block
            e.auto = e.switch = e.end_auto_at_end = False
            e.latch = e.switch_len
            self._raise(e.kind)
            e.count = e.latch + 1
            e.codes = []
        elif e.auto:                # (b) auto-initialize, unless the last
            if self.pending[e.kind] and self.in_byte is None:
                e.stalled = True    # interrupt is still pending
                return
            self._raise(e.kind)
            e.count = e.latch + 1
        else:                       # (c) the end of a single-cycle block
            e.running = e.gate = False
            self._raise(e.kind)
            if self.v < 0xF8:
                self.hw_v = 0xF8
            if e is self.e8:
                self.warm = False

    def _unstall(self, e):
        e.stalled = False
        self._raise(e.kind)
        e.count = e.latch + 1

    # --- the CT1745 mixer ----------------------------------------------------
    DEFAULTS = {0x30: 0xC0, 0x31: 0xC0, 0x32: 0xC0, 0x33: 0xC0, 0x34: 0xC0,
                0x35: 0xC0, 0x36: 0x00, 0x37: 0x00, 0x38: 0x00, 0x39: 0x00,
                0x3A: 0x00, 0x3B: 0x00, 0x3C: 0x1F, 0x3D: 0x15, 0x3E: 0x0B,
                0x3F: 0x00, 0x40: 0x00, 0x41: 0x00, 0x42: 0x00, 0x43: 0x00,
                0x44: 0x80, 0x45: 0x80, 0x46: 0x80, 0x47: 0x80}
    MASKS = {**{r: 0xF8 for r in range(0x30, 0x3B)}, 0x3B: 0xC0,
             0x3C: 0x1F, 0x3D: 0x7F, 0x3E: 0x7F, 0x3F: 0xC0, 0x40: 0xC0,
             0x41: 0xC0, 0x42: 0xC0, 0x43: 0x01,
             **{r: 0xF0 for r in range(0x44, 0x48)}}
    PAIRS = {0x04: 0x32, 0x22: 0x30, 0x26: 0x34, 0x28: 0x36, 0x2E: 0x38}

    def mixer_reset(self):
        self.mix = dict(self.DEFAULTS)

    def mixer_read(self, reg):
        if reg in self.mix:
            return self.mix[reg]
        if reg in self.PAIRS:
            left = self.PAIRS[reg]
            return (self.mix[left] & 0xF0) | (self.mix[left + 1] >> 4)
        if reg == 0x0A:
            return (self.mix[0x3A] >> 5) & 7
        if reg == 0x80:
            bit = {2: 1, 9: 1, 5: 2, 7: 4, 10: 8}.get(self.irq, 0)
            return self.mixer_80_high | bit
        if reg == 0x81:
            v = 1 << self.dma8
            return v | (1 << self.dma16) if self.dma16 else v
        if reg == 0x82:
            return 0x20 | (1 if self.pending[8] else 0) | \
                (2 if self.pending[16] else 0)
        return self.mixer_unknown

    def mixer_write(self, reg, v):
        if reg == 0x00:
            self.mixer_reset()
        elif reg in self.mix:
            self.mix[reg] = v & self.MASKS[reg]
        elif reg in self.PAIRS:
            left = self.PAIRS[reg]
            self.mix[left] = (v & 0xF0) | 0x08
            self.mix[left + 1] = ((v << 4) & 0xF0) | 0x08
        elif reg == 0x0A:
            self.mix[0x3A] = ((v & 7) << 5) | 0x18
        elif reg == 0x81:
            for ch in (5, 6, 7):
                if v >> ch & 1:
                    self.dma16 = ch
                    break
            else:
                self.dma16 = 0


def decode(data, bits, signed, stereo):
    """a frame's bytes as signed 16-bit samples"""
    out = []
    step = bits // 8
    for i in range(0, len(data), step):
        if bits == 8:
            s = data[i] - (0 if signed else 128)
            if signed and s >= 128:
                s -= 256
            out.append(s << 8)
        else:
            s = data[i] | (data[i + 1] << 8)
            if signed:
                s = s - 0x10000 if s >= 0x8000 else s
            else:
                s -= 0x8000
            out.append(s)
    return tuple(out)


def encode(frames, bits, signed, stereo):
    """signed 16-bit frames as a transfer's bytes"""
    out = bytearray()
    for f in frames:
        for s in (f if stereo else f[:1]):
            if bits == 8:
                b = (s >> 8) & 0xFF
                out.append(b if signed else (b + 128) & 0xFF)
            else:
                w = (s if signed else s + 0x8000) & 0xFFFF
                out += bytes((w & 0xFF, w >> 8))
    return bytes(out)
