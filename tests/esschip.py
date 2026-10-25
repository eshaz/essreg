# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The ES1869's audio, the chip behind the Sound Blaster 16 fronts, for
their tests.

ES1869Audio goes over vxdemu's FakeES1869 and tsremu's Chip. It has the
DSP's commands as the data sheet lists them (DS p.72-74) and these engines:
* Audio 1 in Extended mode: the controller registers A1h-B9h, a 256-byte
  FIFO that the DMA fills ahead of the DAC, and the byte counter of
  A4h/A5h with its interrupt (DS p.48-50, p.68-70);
* Audio 1 in the firmware's Compatibility mode: 14h, 1Ch, 48h, 90h, 91h,
  the ADC commands, ADPCM, silence, the direct DAC and MIDI;
* Audio 2, through mixer registers 70h-7Ch (DS p.51-52, p.64-65).
Around them it models the interrupt bits of Audio_Base+Ch and
Config_Base+6, the acknowledge at +Eh, the resets of +6, and time. A DMA
engine moves a byte a DMA cycle on its 8-bit channel (dma8237).

What the data sheet leaves undefined is not guessed at: it goes into
`violations`. That covers a Compatibility transfer or pause with the
Extended commands on, a register command without C6h, a read of the FIFO
port, and commands the chip doesn't have. The fronts' tests expect none.

Time passes in advance(us), which stops at a rising edge of the game's
interrupt, and in each port access, which takes a microsecond; the
engines catch up before every access.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from sb16 import ADPCM, adpcm_codes, decode, encode  # noqa: E402

FIFO1 = 256                 # Audio 1's FIFO in Extended mode
FIFO_FW = 64                # the firmware's buffer in Compatibility mode
FIFO2 = 64                  # Audio 2's FIFO: 32 words
RESET_US = 25               # AAh this long after the reset's end (assumed)
DMA_PER_US = 2              # bytes a DMA engine moves in a microsecond

# Audio_Base+Ch's interrupt bits
I_OVF, I_HE, I_PROC = 0x01, 0x02, 0x04

# the commands of Compatibility mode that start the firmware's transfers,
# with the operands each takes
FW_XFER = {0x14: 2, 0x15: 2, 0x1C: 0, 0x1D: 0, 0x24: 2, 0x25: 2, 0x2C: 0,
           0x2D: 0, 0x74: 2, 0x75: 2, 0x76: 2, 0x77: 2, 0x7A: 2, 0x7B: 2,
           0x80: 2, 0x90: 0, 0x91: 0, 0x98: 0, 0x99: 0,
           **{c: 2 for c in (0x64, 0x65, 0x66, 0x67, 0x6A, 0x6B, 0x6E,
                             0x6F)}}
# the other commands of the table, with their operands
FW_OTHER = {0x10: 1, 0x11: 2, 0x20: 0, 0x21: 0, 0x30: 0, 0x31: 0, 0x34: 0,
            0x35: 0, 0x38: 1, 0x40: 1, 0x41: 1, 0x42: 1, 0x48: 2, 0xC1: 0,
            0xC6: 0, 0xC7: 0, 0xCE: 0, 0xCF: 1, 0xD0: 0, 0xD1: 0, 0xD3: 0,
            0xD4: 0, 0xD8: 0, 0xDC: 0, 0xDD: 1, 0xE1: 0, 0xE7: 0, 0xF2: 0,
            0xFD: 0,
            # the ES488's, which the ES1868 keeps (ES1868 DS)
            0xD5: 0, 0xD6: 0, 0xD7: 1}
# the ones that are safe with the Extended commands on (DS p.44): the DAC
# input, its status, the versions, the interrupt test, the mode itself
EXT_SAFE = {0xC6, 0xC7, 0xC0, 0xD1, 0xD3, 0xD8, 0xE1, 0xE7, 0xF2}


def a1_hz(a1, new=False):
    """Audio 1's rate for A1h; `new` is mixer 71h bit 5, A1h as 70h"""
    if new:
        return a2_hz(a1)
    if a1 & 0x80:
        return 795500 / (256 - a1)
    return 397700 / (128 - a1)


def a2_hz(r70):
    """Audio 2's rate for mixer 70h"""
    clock = 768000 if r70 & 0x80 else 793800
    return clock / (128 - (r70 & 0x7F))


def irq_bits(irq):
    """B1h's read-only low nibble for an IRQ"""
    return {5: 0x05, 7: 0x0A, 10: 0x0F}.get(irq, 0x00)


def drq_bits(ch):
    """B2h's read-only low nibble for a DMA channel"""
    return {0: 0x05, 1: 0x0A, 3: 0x0F}.get(ch, 0x00)


class Stream:
    """a DMA engine and its converter"""

    def __init__(self, size):
        self.size = size
        self.fifo = bytearray()
        self.debt = 0.0
        self.counter = 0
        self.halted = False


class FwTransfer:
    """the firmware's transfer: PCM, ADPCM or silence"""

    def __init__(self, coding, rec, auto, length, ref=False, bits=8,
                 stereo=False):
        self.coding = coding        # "pcm", "silence", or ADPCM bits 2-4
        self.rec, self.auto = rec, auto
        self.length = length        # bytes (or samples of silence) a block
        self.left = length          # still to move in this block
        self.ref = ref
        self.bits, self.stereo = bits, stereo
        self.paused = False
        self.done = False           # the DMA side ended (single-cycle)
        self.codes = []
        self.reference, self.step = 0x80, 0
        self.right = None           # a stereo frame's first byte


class ES1869Audio:
    """the audio engines; call audio_init() from the subclass's __init__"""

    def audio_init(self, irq=5, dma1=1, dma2=None, irq2=None, dmac=None):
        if dmac is not None:
            self.dmac = dmac
        self.pnp[(1, 0x70)] = irq
        self.pnp[(1, 0x74)] = dma1
        self.pnp[(1, 0x72)] = irq if irq2 is None else irq2
        self.pnp[(1, 0x75)] = 4 if dma2 is None else dma2
        self.game_irq = irq
        self.ctrl.update({0xA1: 0x4E, 0xA2: 0xF4, 0xA4: 0x00, 0xA5: 0xF8,
                          0xB1: 0x80 | irq_bits(irq),
                          0xB2: 0x80 | drq_bits(dma1)})
        self.a1 = Stream(FIFO1)
        self.fw = None              # the firmware's transfer
        self.fw_buf = Stream(FIFO_FW)
        self.a2 = Stream(FIFO2)
        self.a1_flags = 0           # I_* of Audio_Base+Ch
        self.a1_load = False        # B7h bit 0: the holding register next
        self.dac_on = False         # D1h: Audio 1 into the mixer
        self.fw_rate = 8000.0       # the firmware's timer (40h/41h)
        self.fw_filter = 0
        self.block8 = 0x07FF        # 48h
        self.midi = None            # None, "read" or "uart"
        self.midi_out = []
        self.midi_in = []           # bytes the MIDI input gets
        self.played = []            # Audio 1's DAC: frames heard
        self.muted = []             # Audio 1 frames with D1h off
        self.played2 = []           # Audio 2's DAC
        self.loads = []             # holding-register loads (B7h bit 0)
        self.recorded = []
        self.source = None          # callable(n) -> n frames for the ADC
        self.violations = []        # (us, what): undefined uses
        self.irq_edges = []         # us of each rising edge of game_irq
        self.irq_line = False
        self.ready_at = None        # the reset's AAh, when it is due
        self.reset_high = False
        self.cmd_cur = None         # the command whose operands come
        self.cmd_need = 0
        self.cmd_ops = []
        self.resets = 0             # software resets of the chip
        self.a_us = self.clock.us   # the engines' time

    # --- what the tests see ------------------------------------------------
    def violate(self, what):
        self.violations.append((self.clock.us, what))

    def a1_channel(self):
        return self.pnp.get((1, 0x74), 1)

    def a2_channel(self):
        return self.pnp.get((1, 0x75), 4)

    def a1_rate(self):
        return a1_hz(self.ctrl[0xA1], bool(self.mixer[0x71] & 0x20))

    def a2_rate(self):
        if self.mixer[0x71] & 0x02:
            return a2_hz(self.mixer[0x70])
        return self.a1_rate()

    # --- time ----------------------------------------------------------------
    def catch_up(self):
        while self.a_us < self.clock.us:
            self.a_us += 1
            self._step()

    def advance(self, us):
        """up to us of time pass, a microsecond at a time; it stops at a
        rising edge of the game's interrupt; the time that passed"""
        self.catch_up()
        edges = len(self.irq_edges)
        done = 0
        while done < us:
            self.clock.advance(1)
            self.a_us = self.clock.us
            self._step()
            done += 1
            if len(self.irq_edges) != edges:
                break
        return done

    def _step(self):
        if self.ready_at is not None and self.clock.us >= self.ready_at:
            self.ready_at = None
            self.out.append(0xAA)
        if not self.reset_high:
            self._a1_step()
            self._fw_step()
        self._a2_step()
        self._line()

    # --- the interrupt line --------------------------------------------------
    def a1_request(self):
        f = self.a1_flags
        return bool(f & I_PROC or (f & I_OVF and self.ctrl[0xB1] & 0x40)
                    or (f & I_HE and self.ctrl[0xB1] & 0x20))

    def a2_request(self):
        r = self.mixer[0x7A]
        return bool(r & 0x80 and r & 0x40)

    def _line(self):
        line = (self.a1_request() and self.pnp.get((1, 0x70)) ==
                self.game_irq) or \
            (self.a2_request() and self.pnp.get((1, 0x72)) == self.game_irq)
        if line and not self.irq_line:
            self.irq_edges.append(self.clock.us)
        self.irq_line = line

    # --- Audio 1 in Extended mode --------------------------------------------
    def _a1_format(self):
        b7 = self.ctrl[0xB7]
        return (16 if b7 & 0x04 else 8), bool(b7 & 0x20), bool(b7 & 0x08)

    def _a1_count(self):
        """a byte moved: the counter, its overflow and interrupt; True when
        the DMA halts (normal mode)"""
        s = self.a1
        s.counter = (s.counter + 1) & 0xFFFF
        if s.counter:
            return False
        self.a1_flags |= I_OVF
        s.counter = self.ctrl[0xA4] | (self.ctrl[0xA5] << 8)
        if self.ctrl[0xB8] & 0x04:
            return False
        s.halted = True
        return True

    def _a1_step(self):
        s, b8 = self.a1, self.ctrl[0xB8]
        adc = bool(b8 & 0x08)
        if b8 & 1 and not s.halted and self.ctrl[0xB2] & 0x40:
            ch = self.a1_channel()
            for _ in range(DMA_PER_US):
                if adc:
                    if not s.fifo or not self.dmac.write(ch, s.fifo[:1]):
                        break
                    del s.fifo[0]
                else:
                    if len(s.fifo) >= s.size:
                        break
                    data = self.dmac.read(ch, 1)
                    if not data:
                        break
                    s.fifo += data
                if self._a1_count():
                    break
        s.debt += self.a1_rate() / 1e6
        while s.debt >= 1:
            s.debt -= 1
            self._a1_sample(adc)

    def _a1_sample(self, adc):
        s = self.a1
        bits, signed, stereo = self._a1_format()
        if self.a1_load:
            self.a1_load = False
            self.ctrl[0xB7] &= ~0x01
            hold = self.ctrl[0xB5] | (self.ctrl[0xB6] << 8)
            data = bytes((hold & 0xFF, hold >> 8)) if bits == 16 else \
                bytes((hold >> 8,))
            self.loads.append(decode(data, bits, signed, False))
        if not self.ctrl[0xB7] & 0x80:
            return                  # the FIFO isn't on the converter
        size = (bits // 8) * (2 if stereo else 1)
        if adc:
            frame = self.source(1)[0] if self.source else \
                (0,) * (2 if stereo else 1)
            data = encode([frame], bits, signed, stereo)
            if len(s.fifo) + len(data) <= s.size:
                s.fifo += data
                self.recorded.append(frame)
            return
        if len(s.fifo) < size:
            return                  # an underrun: the DAC holds
        data = bytes(s.fifo[:size])
        del s.fifo[:size]
        self._heard(decode(data, bits, signed, stereo))

    def _heard(self, frame):
        (self.played if self.dac_on else self.muted).append(frame)

    def _b8_write(self, old, new):
        if new & 1 and not old & 1:
            self.a1.counter = self.ctrl[0xA4] | (self.ctrl[0xA5] << 8)
            self.a1.halted = False
            if self.fw is not None:
                self.violate("Extended DMA while the firmware transfers")
            if bool(new & 0x08) != bool(new & 0x02):
                self.violate("B8h: the converter and the DMA disagree")
            bits, signed, stereo = self._a1_format()
            a8 = self.ctrl[0xA8] & 3
            if a8 != (1 if stereo else 2):
                self.violate("A8h and B7h disagree on stereo")
            if bool(self.ctrl[0xB7] & 0x40) == stereo:
                self.violate("B7h bit 6 is not the opposite of bit 3")

    # --- the firmware's transfers (Compatibility mode) -----------------------
    def _fw_start(self, c, ops):
        if self.ctrl[0xB8] & 1:
            self.violate("firmware transfer %02Xh during Extended DMA" % c)
        length = (ops[0] | (ops[1] << 8)) + 1 if ops else self.block8 + 1
        hi, lo = c >> 4, c & 0x0F
        stereo = bool(self.mixer[0x0E] & 0x02)
        if c == 0x80:
            t = FwTransfer("silence", False, False, length)
        elif hi in (0x1, 0x2):          # 14h 15h 1Ch 1Dh, 24h 25h 2Ch 2Dh
            t = FwTransfer("pcm", hi == 0x2, lo >= 0x0C, length,
                           bits=16 if lo & 1 else 8, stereo=stereo)
        elif hi == 0x9:                 # 90h 91h, 98h 99h
            t = FwTransfer("pcm", lo >= 8, not lo & 1, length,
                           stereo=stereo)
        elif hi == 0x7:
            bits = {0x74: 4, 0x75: 4, 0x76: 3, 0x77: 3, 0x7A: 2, 0x7B: 2}[c]
            t = FwTransfer(bits, False, False, length, ref=bool(lo & 1))
        else:
            self.violate("ESPCM transfer %02Xh" % c)
            return
        self.fw = t
        self.fw_buf.fifo = bytearray()
        self.fw_buf.debt = 0.0

    def _fw_step(self):
        t = self.fw
        if t is None:
            return
        buf = self.fw_buf
        ch = self.a1_channel()
        if t.coding != "silence" and not t.paused and not t.done:
            for _ in range(DMA_PER_US):
                if t.rec:
                    if not buf.fifo or not self.dmac.write(ch, buf.fifo[:1]):
                        break
                    del buf.fifo[0]
                else:
                    if len(buf.fifo) >= buf.size:
                        break
                    data = self.dmac.read(ch, 1)
                    if not data:
                        break
                    buf.fifo += data
                t.left -= 1
                if t.left == 0:
                    self._fw_block_end(t)
                    if t.done:
                        break
        rate = self.fw_rate
        buf.debt += rate / 1e6
        while buf.debt >= 1 and self.fw is t:
            buf.debt -= 1
            self._fw_sample(t)

    def _fw_block_end(self, t):
        self.a1_flags |= I_PROC
        if t.auto:
            t.length = self.block8 + 1
            t.left = t.length
        else:
            t.done = True

    def _fw_sample(self, t):
        buf = self.fw_buf
        if t.coding == "silence":
            if t.paused:
                return
            self._heard((0,))
            t.left -= 1
            if t.left == 0:
                self.a1_flags |= I_PROC
                self.fw = None
            return
        if t.rec:
            frame = self.source(1)[0] if self.source else (0,)
            buf.fifo += encode([frame], t.bits, False, False)
            self.recorded.append(frame)
            return
        if t.coding != "pcm":
            self._fw_adpcm(t)
        elif t.bits == 16:
            if len(buf.fifo) >= 2:
                self._heard(decode(bytes(buf.fifo[:2]), 16, False, False))
                del buf.fifo[:2]
        elif buf.fifo:
            b = buf.fifo.pop(0)
            if not t.stereo:
                self._heard(decode(bytes((b,)), 8, False, False))
            elif t.right is None:
                t.right = b         # the right channel comes first
            else:
                self._heard(decode(bytes((b, t.right)), 8, False, True))
                t.right = None
        if t.done and not buf.fifo:
            self.fw = None

    def _fw_adpcm(self, t):
        buf = self.fw_buf
        if not t.codes:
            if not buf.fifo:
                if t.done:
                    self.fw = None
                return
            b = buf.fifo.pop(0)
            if t.ref:
                t.ref = False
                t.reference, t.step = b, 0
                self._heard(((t.reference - 128) << 8,))
                return
            t.codes = adpcm_codes(b, t.coding)
        scale, adjust, top = ADPCM[t.coding]
        s = max(0, min(top, t.codes.pop(0) + t.step))
        t.reference = max(0, min(255, t.reference + scale[s]))
        t.step = (t.step + adjust[s]) & 0xFF
        self._heard(((t.reference - 128) << 8,))

    # --- Audio 2 -------------------------------------------------------------
    def _a2_count(self):
        s = self.a2
        s.counter = (s.counter + 1) & 0xFFFF
        if s.counter:
            return False
        self.mixer[0x7A] |= 0x80
        s.counter = self.mixer[0x74] | (self.mixer[0x76] << 8)
        if self.mixer[0x78] & 0x10:
            return False
        self.mixer[0x78] &= ~0x02
        return True

    def _a2_step(self):
        s, r78 = self.a2, self.mixer[0x78]
        if r78 & 0x02:
            ch = self.a2_channel()
            for _ in range(DMA_PER_US):
                if len(s.fifo) >= s.size or ch > 3:
                    break
                data = self.dmac.read(ch, 1)
                if not data:
                    break
                s.fifo += data
                if self._a2_count():
                    break
        s.debt += self.a2_rate() / 1e6
        while s.debt >= 1:
            s.debt -= 1
            if not self.mixer[0x78] & 0x01:
                continue
            r7a = self.mixer[0x7A]
            bits, signed = (16 if r7a & 1 else 8), bool(r7a & 4)
            stereo = bool(r7a & 2)
            size = (bits // 8) * (2 if stereo else 1)
            if len(s.fifo) >= size:
                data = bytes(s.fifo[:size])
                del s.fifo[:size]
                self.played2.append(decode(data, bits, signed, stereo))

    def _mixer_written(self, reg, old, v):
        if reg == 0x78 and v & 0x02 and not old & 0x02:
            self.a2.counter = self.mixer[0x74] | (self.mixer[0x76] << 8)
        elif reg == 0x7A:
            # the latch: a 0 clears it, a 1 sets it
            self.mixer[0x7A] = v
        elif reg == 0x00:
            for r in range(0x70, 0x7D):
                self.mixer[r] = 0
            self.mixer[0x0E] = 0
        self._line()

    # --- ports ---------------------------------------------------------------
    def inb(self, port):
        self.catch_up()
        return super().inb(port)

    def _inb(self, port):
        off = port - self.base
        if off == 0x0C:
            busy = self.busy or self.reset_high or self.ready_at is not None
            v = (0x80 if busy else 0) | (0x40 if self.out else 0)
            if self.ext_mode or self.ctrl[0xB8] & 1:
                n = len(self.a1.fifo)
                v |= (0x20 if n >= FIFO1 else 0) | (0x10 if not n else 0) | \
                    (0x08 if n <= FIFO1 // 2 else 0)
            return v | self.a1_flags
        if off == 0x0E:
            self.a1_flags = 0       # any read acknowledges Audio 1
            self._line()
            return 0x80 if self.out else 0x00
        if off == 0x0A:
            return self.out.pop(0) if self.out else 0xFF
        if off == 0x0F:
            self.violate("a read of the FIFO port")
            return self.a1.fifo.pop(0) if self.a1.fifo else 0xFF
        if self.cfg and port == self.cfg + 6:
            return (0x01 if self.a1_request() else 0) | \
                (0x02 if self.a2_request() else 0)
        return super()._inb(port)

    def outb(self, port, v):
        self.catch_up()
        off = port - self.base
        v &= 0xFF
        if off == 6:
            self.clock.advance(1)
            self.log.append(("out", port, v))
            self._reset_port(v)
            return
        if off == 0x0F:
            self.clock.advance(1)
            self.log.append(("out", port, v))
            self.violate("a write to the FIFO port")
            return
        if off == 5:
            reg = self.index
            old = self.mixer[reg]
            super().outb(port, v)
            self._mixer_written(reg, old, v)
            return
        super().outb(port, v)

    def _reset_port(self, v):
        if v & 0x02:
            self.a1.fifo = bytearray()
        if v & 0x01:
            if not self.reset_high:
                self.reset_high = True
                self._halt()
        elif self.reset_high:
            self.reset_high = False
            self.ready_at = self.clock.us + RESET_US
        self.ports[6] = v

    def _halt(self):
        """the software reset (DS p.43): Extended mode off, DMA stopped,
        interrupts cleared, the DAC off the mixer, the counters at 2048
        bytes, the timer at 8 kHz, 7Ah cleared"""
        self.ext_mode = False
        self.cmd_cur, self.cmd_need, self.cmd_ops = None, 0, []
        self.pending, self.operands = [], []
        self.out = []
        self.resets += 1
        self.fw = None
        self.fw_buf.fifo = bytearray()
        self.a1.fifo = bytearray()
        self.a1.halted = False
        self.ctrl[0xB8] = 0
        self.ctrl.update({0xA1: 0x4E, 0xA2: 0xF4, 0xA4: 0x00, 0xA5: 0xF8})
        self.a1_flags = 0
        self.a1_load = False
        self.dac_on = False
        self.fw_rate = 8000.0
        self.midi = None
        self.mixer[0x7A] = 0
        self._line()

    # --- the command port ----------------------------------------------------
    def _dsp(self, v):
        if self.reset_high:
            return
        if self.midi == "uart":
            self.midi_out.append(v)
            return
        if self.midi == "read":
            self.midi = None
            return
        if self.cmd_cur is not None:
            self.cmd_ops.append(v)
            if len(self.cmd_ops) < self.cmd_need:
                return
            c, ops = self.cmd_cur, self.cmd_ops
            self.cmd_cur, self.cmd_ops = None, []
            self.commands.append((c,) + tuple(ops))
            self._command(c, ops)
            return
        if self.ext_mode and (0xA0 <= v <= 0xBF or v == 0xC0):
            need = 1
        elif 0xA0 <= v <= 0xBF or v == 0xC0:
            self.violate("register command %02Xh without C6h" % v)
            self.commands.append((v,))
            return
        elif v in FW_XFER:
            need = FW_XFER[v]
        elif v in FW_OTHER:
            need = FW_OTHER[v]
        else:
            self.violate("command %02Xh, which the chip doesn't have" % v)
            self.commands.append((v,))
            return
        if need:
            self.cmd_cur, self.cmd_need, self.cmd_ops = v, need, []
            return
        self.commands.append((v,))
        self._command(v, [])

    def _command(self, c, ops):
        if 0xA0 <= c <= 0xBF:
            old = self.ctrl[c]
            if c in (0xB1, 0xB2):
                low = irq_bits(self.pnp.get((1, 0x70))) if c == 0xB1 else \
                    drq_bits(self.a1_channel())
                self.ctrl[c] = (ops[0] & 0xF0) | low
            else:
                self.ctrl[c] = ops[0]
            if c == 0xB8:
                self._b8_write(old, ops[0])
            elif c == 0xB7 and ops[0] & 1:
                self.a1_load = True
            if self.fw is not None:
                self.violate("register %02Xh during a firmware transfer" % c)
            return
        if c == 0xC0:
            if 0xA0 <= ops[0] <= 0xBF:
                self.out.append(self.ctrl[ops[0]])
            return
        if self.ext_mode and c not in EXT_SAFE:
            self.violate("Compatibility command %02Xh in Extended mode" % c)
        if c in FW_XFER:
            self._fw_start(c, ops)
        elif c == 0xC6:
            self.ext_mode = True
        elif c == 0xC7:
            self.ext_mode = False
        elif c == 0xE7:
            self.out += [0x68, 0x89]
        elif c == 0xE1:
            self.out += [0x03, 0x01]
        elif c == 0xD1:
            self.dac_on = True
        elif c == 0xD3:
            self.dac_on = False
        elif c == 0xD8:
            self.out.append(0xFF if self.dac_on else 0x00)
        elif c in (0xD0, 0xD4):
            if self.fw is not None:
                self.fw.paused = c == 0xD0
        elif c == 0xD5:
            self.out.append(0x01)
        elif c == 0xD6:
            self.out.append(0x00)
        elif c == 0xF2:
            self.a1_flags |= I_PROC
            self._line()
        elif c == 0x40:
            self.fw_rate = 1000000 / (256 - ops[0])
        elif c == 0x41:
            self.fw_rate = 1500000 / (256 - ops[0])
        elif c == 0x42:
            self.fw_filter = ops[0]
        elif c == 0x48:
            self.block8 = ops[0] | (ops[1] << 8)
        elif c == 0x10:
            self._heard(decode(bytes((ops[0],)), 8, False, False))
        elif c == 0x11:
            self._heard(decode(bytes(ops[:2]), 16, False, False))
        elif c in (0x20, 0x21):
            frame = self.source(1)[0] if self.source else (0,)
            self.recorded.append(frame)
            w = (frame[0] + 0x8000) & 0xFFFF
            self.out += [w >> 8] if c == 0x20 else [w & 0xFF, w >> 8]
        elif c == 0x38:
            self.midi_out.append(ops[0])
        elif c in (0x34, 0x35):
            self.midi = "uart"
        elif c in (0x30, 0x31):
            self.midi = "read"
            self.out += self.midi_in
            self.midi_in = []
        elif c in (0xCE, 0xDC):
            self.out.append(0x00)
