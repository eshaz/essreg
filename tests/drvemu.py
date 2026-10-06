# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Run ES1869.DRV in a 16-bit CPU emulator against a model of the ES1869.

The driver's segments are loaded at fixed paragraphs with their relocations
applied, and a device structure is set up as DRVM_ENABLE leaves it (the
enable itself asks the registry, CONFIGMG and the VxD for too much). Every
import and the ES1869.VXD entry point is a stub that stops the emulator so
Python can answer it.

The chip model has what the wave devices use:
- the mixer (Audio_Base+4/+5) and the DSP (+6, +Ah, +Ch, +Eh): extended
  mode, the controller registers A0h-BFh written by command and read with
  C0h, and the D1h/D3h state of the Audio 1 DAC
- the 8237 DMA controller of channels 0-3, whose transfers the test moves
  forward (dma()): the bytes the chip takes from the Audio 1 ring are kept,
  so a test can compare what played with what a program wrote
- an Audio 1 interrupt, run through ESS's own handler (isr_template, 3:01B9)
  copied to a block as isr_install does, so it goes through isr_srv_table

    emu = DrvEmu(build_es1869drv.build(True))
    emu.wod(1, WODM_OPEN, ...)
    emu.dma(emu.block); emu.interrupt()
"""

import os
import struct
import sys

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR,
                     UC_HOOK_INSN)
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_DX,
                               UC_X86_REG_ECX,
                               UC_X86_REG_SP, UC_X86_REG_IP, UC_X86_REG_CS,
                               UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
                               UC_X86_REG_EFLAGS, UC_X86_INS_IN,
                               UC_X86_INS_OUT)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from retools.ne import NEFile  # noqa: E402

SEG_PARA = {i: 0x1000 * i for i in range(1, 8)}
DGROUP = SEG_PARA[7]
STUB_PARA = 0x8000
STACK_PARA = 0x9000
A1_BUF_PARA = 0xA000        # the Audio 1 DMA buffer, physical 0A0000h
A2_BUF_PARA = 0xA400        # the Audio 2 DMA buffer
MIXSTATE_PARA = 0xA800      # the mixer state block (+26h)
ISR_PARA = 0xAC00           # the interrupt handler, as isr_install copies it
APP_PARA = 0xB000           # the program's memory: headers, data
APP_END = 0xF000
AUDIO_BASE = 0x220
DEVNODE = 0x00012345
A1_DMA = 1
A2_DMA = 0
BUF_SIZE = 0x4000
DEV_OFF = 0x2000            # the device structure in DGROUP
HEAP_OFF = 0x3000           # LocalAlloc from here
IF_FLAG = 0x200
PAGE_PORTS = {0: 0x87, 1: 0x83, 2: 0x81, 3: 0x82, 5: 0x8B, 6: 0x89, 7: 0x8A}

WODM_GETNUMDEVS, WODM_GETDEVCAPS, WODM_OPEN, WODM_CLOSE = 3, 4, 5, 6
WODM_PREPARE, WODM_UNPREPARE, WODM_WRITE, WODM_PAUSE = 7, 8, 9, 10
WODM_RESTART, WODM_RESET, WODM_GETPOS = 11, 12, 13
WODM_GETVOLUME, WODM_SETVOLUME, WODM_BREAKLOOP = 16, 17, 20
WOM_OPEN, WOM_CLOSE, WOM_DONE = 0x3BB, 0x3BC, 0x3BD
WIDM_GETNUMDEVS, WIDM_GETDEVCAPS, WIDM_OPEN, WIDM_CLOSE = 50, 51, 52, 53
WIDM_ADDBUFFER, WIDM_START, WIDM_STOP, WIDM_RESET = 56, 57, 58, 59
WIM_OPEN, WIM_CLOSE, WIM_DATA = 0x3BE, 0x3BF, 0x3C0
CALLBACK_FUNCTION = 0x00030000
WHDR_DONE, WHDR_PREPARED, WHDR_BEGINLOOP = 1, 2, 4
WHDR_ENDLOOP, WHDR_INQUEUE = 8, 0x10
TIME_MS, TIME_SAMPLES, TIME_BYTES = 1, 2, 4

# import stubs: (module, ordinal) -> argument bytes the callee pops
STUB_ARGS = {
    ("KERNEL", 5): 4,       # LocalAlloc(flags, size)
    ("KERNEL", 7): 2,       # LocalFree(h)
    ("KERNEL", 15): 6,      # GlobalAlloc(flags, size)
    ("KERNEL", 127): 14,    # GetPrivateProfileInt(app, key, def, file)
    ("KERNEL", 353): 10,    # lstrcpyn(dst, src, n)
    ("USER", 176): 10,      # LoadString(hInst, id, buf, n)
    ("USER", 420): 0,       # wsprintf, cdecl
    ("USER", 471): 8,       # lstrcmpi(a, b)
    ("MMSYSTEM", 31): 22,   # DriverCallback
    ("MMSYSTEM", 607): 0,   # timeGetTime
}


class EmuError(Exception):
    pass


def profile_int(value):
    """what Windows' GetPrivateProfileInt makes of a value: its leading
    digits after blanks, 0 without any"""
    digits = ""
    for ch in value.lstrip(" \t"):
        if not ch.isdigit():
            break
        digits += ch
    return int(digits) & 0xFFFF if digits else 0


class Chip:
    """The ES1869's mixer and DSP, and the 8237 of channels 0-3."""

    def __init__(self, base=AUDIO_BASE):
        self.base = base
        self.mixer = bytearray(256)
        self.index = 0
        self.regs = bytearray(256)      # controller registers A0h-BFh
        self.ext = False                # extended mode (C6h)
        self.a1_dac = False             # D1h: the Audio 1 DAC in the mixer
        self.pending = None             # the command waiting for its byte
        self.readq = []
        self.log = []                   # ("cmd", c), ("reg", r, v), ...
        self.ext_errors = []            # register commands without C6h
        self.a1_irq = False
        # 8237: per channel, 0-3 in bytes and 5-7 in words
        self.dma = {ch: {"mask": True, "mode": 0, "addr": 0, "count": 0,
                         "page": 0, "pos": 0} for ch in (0, 1, 2, 3, 5, 6, 7)}
        self.ff = {0: 0, 1: 0}
        self.dma_log = []

    def _dsp_byte(self, v):
        if self.pending is not None:
            cmd = self.pending
            self.pending = None
            if 0xA0 <= cmd <= 0xBF:
                if not self.ext:
                    self.ext_errors.append((cmd, v))
                self.regs[cmd] = v
                self.log.append(("reg", cmd, v))
            elif cmd == 0xC0:
                self.readq.append(self.regs[v])
                self.log.append(("read", v))
            else:
                self.log.append(("data", cmd, v))
            return
        self.log.append(("cmd", v))
        if 0xA0 <= v <= 0xBF or v in (0xC0, 0x40):
            self.pending = v
        elif v == 0xC6:
            self.ext = True
        elif v == 0xC7:
            self.ext = False
        elif v == 0xD1:
            self.a1_dac = True
        elif v == 0xD3:
            self.a1_dac = False

    def out(self, port, v):
        b = self.base
        if port == b + 4:
            self.index = v
        elif port == b + 5:
            self.mixer[self.index] = v
            self.log.append(("mixer", self.index, v))
        elif port == b + 6:
            self.log.append(("reset", v))
            if v & 1:
                self.readq = [0xAA]
                self.ext = False
                self.a1_dac = False
                self.pending = None
                self.mixer[0x7C] = 0
        elif port == b + 0xC:
            self._dsp_byte(v)
        elif port < 0x10 or 0xC0 <= port < 0xE0 or \
                port in PAGE_PORTS.values():
            self._dma_out(port, v)

    def inp(self, port):
        b = self.base
        if port == b + 5:
            return self.mixer[self.index]
        if port == b + 0xA:
            return self.readq.pop(0) if self.readq else 0xFF
        if port == b + 0xC:
            return 0x01 if self.a1_irq else 0x00
        if port == b + 0xE:
            self.a1_irq = False
            return 0x80 if self.readq else 0x00
        return 0xFF

    def _dma_out(self, port, v):
        self.dma_log.append((port, v))
        if port in PAGE_PORTS.values():
            for ch, p in PAGE_PORTS.items():
                if p == port:
                    self.dma[ch]["page"] = v
            return
        # the 16-bit controller: channels 4-7, registers at C0h + 2n
        hi, reg = (1, (port - 0xC0) >> 1) if port >= 0xC0 else (0, port)
        base = 4 if hi else 0
        if reg == 0x0A:
            ch = base + (v & 3)
            if ch in self.dma:
                self.dma[ch]["mask"] = bool(v & 4)
        elif reg == 0x0B:
            ch = base + (v & 3)
            if ch in self.dma:
                self.dma[ch]["mode"] = v
        elif reg == 0x0C:
            self.ff[hi] = 0
        elif reg < 8:
            ch = base + (reg >> 1)
            key = "count" if reg & 1 else "addr"
            if ch not in self.dma:
                return
            if self.ff[hi] == 0:
                self.dma[ch][key] = (self.dma[ch][key] & 0xFF00) | v
            else:
                self.dma[ch][key] = (self.dma[ch][key] & 0x00FF) | (v << 8)
            self.ff[hi] ^= 1
            self.dma[ch]["pos"] = 0

    def dma_count(self, ch):
        """the 8237's current count: bytes left in the pass - 1"""
        d = self.dma[ch]
        return (d["count"] - d["pos"]) & 0xFFFF

    def controller(self):
        return [e for e in self.log if e[0] in ("reg", "cmd")]


class DrvEmu:
    def __init__(self, drv, syms=None):
        self.ne = ne = NEFile(drv)
        self.syms = syms or {}
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x100000)
        for s in ne.segments:
            uc.mem_write(SEG_PARA[s.index] * 16, ne.segment_data(s.index))
        self.stubs = {}
        self.stub_at = {}
        self._next_stub = 0x10
        self.sentinel = self._stub(("SENTINEL", 0))
        self.iret_stop = self._stub(("IRET", 0))
        self.vxd_entry = self._stub(("VXD", 0))
        self._relocate()
        self.chip = Chip()
        self.ini = {}
        self.ppint = []
        self.callbacks = []     # (msg, dwInstance, dw1)
        self.vxd_calls = []     # (DX, BX)
        self.vxd_fail = set()   # functions that answer with the carry set
        self.dsp_taken = []     # ECX of each call to 040D
        self.heap = HEAP_OFF
        self.app = APP_PARA * 16
        self.clock = 0
        self.played = bytearray()      # what the chip took from Audio 1
        self.played2 = bytearray()     # and from Audio 2's buffer
        self.a2_dma = A2_DMA
        uc.hook_add(UC_HOOK_INTR, self._intr)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        self._setup_device()

    # --- memory -----------------------------------------------------------

    def r8(self, para, off):
        return self.uc.mem_read(para * 16 + (off & 0xFFFF), 1)[0]

    def r16(self, para, off):
        return struct.unpack("<H", self.uc.mem_read(
            para * 16 + (off & 0xFFFF), 2))[0]

    def r32(self, para, off):
        return self.r16(para, off) | (self.r16(para, off + 2) << 16)

    def w8(self, para, off, v):
        self.uc.mem_write(para * 16 + (off & 0xFFFF), bytes([v & 0xFF]))

    def w16(self, para, off, v):
        self.uc.mem_write(para * 16 + (off & 0xFFFF),
                          struct.pack("<H", v & 0xFFFF))

    def w32(self, para, off, v):
        self.w16(para, off, v)
        self.w16(para, off + 2, v >> 16)

    def rd(self, lin, n):
        return bytes(self.uc.mem_read(lin, n))

    def wr(self, lin, data):
        self.uc.mem_write(lin, bytes(data))

    def dev8(self, off):
        return self.r8(DGROUP, DEV_OFF + off)

    def dev16(self, off):
        return self.r16(DGROUP, DEV_OFF + off)

    def dev32(self, off):
        return self.r32(DGROUP, DEV_OFF + off)

    def set_dev8(self, off, v):
        self.w8(DGROUP, DEV_OFF + off, v)

    def set_dev16(self, off, v):
        self.w16(DGROUP, DEV_OFF + off, v)

    def alloc(self, data, align=16):
        """program memory: a far pointer (seg:off) to a copy of data"""
        self.app = (self.app + align - 1) & ~(align - 1)
        lin = self.app
        if lin + len(data) > APP_END * 16:
            raise EmuError("out of program memory")
        self.wr(lin, data)
        self.app += max(len(data), 1)
        return ((lin >> 4) << 16) | (lin & 0xF)

    def far_lin(self, ptr):
        return ((ptr >> 16) << 4) + (ptr & 0xFFFF)

    def sym(self, name):
        seg, off = self.syms[name]
        return seg, off

    # --- loading ----------------------------------------------------------

    def _stub(self, key):
        off = self._next_stub
        self._next_stub += 1
        self.uc.mem_write(STUB_PARA * 16 + off, b"\xCC")
        self.stubs[off] = key
        self.stub_at[key] = off
        return off

    def _target(self, rflags, target):
        a, b = target
        if rflags & 3 == 0:
            seg = a & 0xFF
            if seg == 0xFF:
                seg, off, _f = self.ne.entries[b]
            else:
                off = b
            return SEG_PARA[seg], off
        mod = self.ne.modules[a - 1]
        if (mod, b) == ("KERNEL", 114):         # __AHINCR: 64 KB in real mode
            return None, 0x1000
        if (mod, b) == ("KERNEL", 178):         # __WINFLAGS
            return None, 0x0025
        key = (mod, b)
        off = self.stub_at.get(key)
        if off is None:
            off = self._stub(key)
        return STUB_PARA, off

    def _relocate(self):
        for s in self.ne.segments:
            base = SEG_PARA[s.index] * 16
            for rtype, rflags, off, target in s.relocs:
                seg, toff = self._target(rflags, target)
                sites, pos = [], off
                if rflags & 4:
                    sites = [off]
                else:
                    while pos != 0xFFFF:
                        sites.append(pos)
                        pos = struct.unpack("<H", self.uc.mem_read(
                            base + pos, 2))[0]
                for site in sites:
                    old = struct.unpack("<H", self.uc.mem_read(
                        base + site, 2))[0]
                    add = old if rflags & 4 else 0
                    if rtype == 2:
                        val = struct.pack("<H", seg)
                    elif rtype == 3:
                        val = struct.pack("<HH", (toff + add) & 0xFFFF, seg)
                    elif rtype == 5:
                        val = struct.pack("<H", (toff + add) & 0xFFFF)
                    else:
                        raise EmuError("relocation type %d" % rtype)
                    self.uc.mem_write(base + site, val)

    def _setup_device(self):
        """the device structure and DGROUP as DRVM_ENABLE leaves them"""
        d = DEV_OFF
        dg = DGROUP
        self.w16(dg, 0x10, self.vxd_entry)          # the VxD's entry point
        self.w16(dg, 0x12, STUB_PARA)
        self.w16(dg, 0xBD0, 0x1234)                 # hInstance
        self.w16(dg, 0xBD2, d)                      # the device list
        self.w16(dg, d + 0x00, AUDIO_BASE)
        self.w8(dg, d + 0x05, 5)                    # IRQ
        self.w8(dg, d + 0x06, A1_DMA)
        self.w16(dg, d + 0x0E, BUF_SIZE)
        self.w32(dg, d + 0x14, DEVNODE)
        self.w16(dg, d + 0x1C, 1)                   # enabled
        self.w16(dg, d + 0x26, 0)                   # the mixer state block
        self.w16(dg, d + 0x28, MIXSTATE_PARA)
        self.w8(dg, d + 0x2B, 0x80)                 # demand transfers
        # Audio 1's DMA (3:4981-49E3): buffer, 8237 ports, modes
        self.w16(dg, d + 0x3B, BUF_SIZE - 1)
        self.w16(dg, d + 0x41, 0)
        self.w16(dg, d + 0x43, A1_BUF_PARA)
        self.w16(dg, d + 0x45, (A1_BUF_PARA * 16) & 0xFFFF)
        self.w8(dg, d + 0x47, (A1_BUF_PARA * 16) >> 16)
        for off, v in ((0x2F, A1_DMA * 2), (0x30, A1_DMA * 2 + 1),
                       (0x31, 0x0A), (0x32, 0x0B), (0x33, 0x0C),
                       (0x34, PAGE_PORTS[A1_DMA]), (0x35, A1_DMA),
                       (0x36, A1_DMA + 4), (0x37, 0x54 + A1_DMA),
                       (0x38, 0x58 + A1_DMA)):
            self.w8(dg, d + off, v)
        self.w16(dg, d + 0x5B, 0x6000 | 0x65)       # the EOI word
        # Audio 2's DMA (3:4A33-4AE3)
        self.set_a2_dma(A2_DMA)
        self.w16(dg, d + 0xD5, (A2_BUF_PARA * 16) & 0xFFFF)
        self.w8(dg, d + 0xD7, (A2_BUF_PARA * 16) >> 16)
        self.w16(dg, d + 0xD9, BUF_SIZE)
        self.w16(dg, d + 0xDD, A2_BUF_PARA)
        self.w16(dg, d + 0xEF, BUF_SIZE - 1)
        self.w16(dg, d + 0xF5, 0)
        self.w16(dg, d + 0xF7, A2_BUF_PARA)
        self.w16(dg, d + 0xF9, (A2_BUF_PARA * 16) & 0xFFFF)
        self.w8(dg, d + 0xFB, (A2_BUF_PARA * 16) >> 16)
        # the interrupt handler, as isr_install copies it (3:0261)
        code = self.rd(SEG_PARA[3] * 16 + 0x01B9, 0xA8)
        self.wr(ISR_PARA * 16, code)
        self.w16(ISR_PARA, 0x0A, d)
        self.w16(dg, d + 0x53, ISR_PARA)

    def set_a2_dma(self, ch):
        """Audio 2 on DMA channel ch, its 8237 fields as the enable sets
        them (3:4A5A-4AE3)"""
        dg, d = DGROUP, DEV_OFF
        self.a2_dma = ch
        self.w8(dg, d + 0xD4, ch)
        if ch > 3:
            ports = ((ch & 3) * 4 + 0xC0, (ch & 3) * 4 + 0xC2, 0xD4, 0xD6,
                     0xD8)
        else:
            ports = (ch * 2, ch * 2 + 1, 0x0A, 0x0B, 0x0C)
        for off, v in zip((0xE3, 0xE4, 0xE5, 0xE6, 0xE7), ports):
            self.w8(dg, d + off, v)
        for off, v in ((0xE8, PAGE_PORTS[ch]), (0xE9, ch & 3),
                       (0xEA, (ch & 3) + 4), (0xEB, 0x54 + (ch & 3)),
                       (0xEC, 0x58 + (ch & 3))):
            self.w8(dg, d + off, v)

    # --- hooks ------------------------------------------------------------

    def _intr(self, uc, intno, _user):
        if intno != 3:
            raise EmuError("interrupt %#x at %04x:%04x" % (
                intno, uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)))
        ip = uc.reg_read(UC_X86_REG_IP)
        cs = uc.reg_read(UC_X86_REG_CS)
        self.stop = (cs, (ip - 1) & 0xFFFF)
        uc.emu_stop()

    def _in(self, uc, port, size, _user):
        return self.chip.inp(port)

    def _out(self, uc, port, size, value, _user):
        self.chip.out(port, value & 0xFF)

    # --- running ----------------------------------------------------------

    def _push(self, *words):
        sp = self.uc.reg_read(UC_X86_REG_SP)
        for w in words:
            sp = (sp - 2) & 0xFFFF
            self.w16(STACK_PARA, sp, w)
        self.uc.reg_write(UC_X86_REG_SP, sp)

    def _args(self, n):
        sp = self.uc.reg_read(UC_X86_REG_SP)
        return [self.r16(STACK_PARA, sp + 4 + 2 * i) for i in range(n)]

    def call(self, target, args):
        """far call target (para, off) with the words pushed in order: DX:AX"""
        uc = self.uc
        uc.reg_write(UC_X86_REG_SS, STACK_PARA)
        uc.reg_write(UC_X86_REG_SP, 0xFF00)
        uc.reg_write(UC_X86_REG_DS, DGROUP)
        uc.reg_write(UC_X86_REG_ES, DGROUP)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x0202)
        self._push(*args)
        self._push(STUB_PARA, self.sentinel)
        return self._run(target)

    def call_name(self, name, *args):
        seg, off = self.syms[name]
        return self.call((SEG_PARA[seg], off), args)

    def _run(self, target):
        uc = self.uc
        cs, ip = target
        while True:
            self.stop = None
            uc.reg_write(UC_X86_REG_CS, cs)
            uc.reg_write(UC_X86_REG_IP, ip)
            uc.emu_start(cs * 16 + ip, 0, count=5_000_000)
            if self.stop is None:
                raise EmuError("ran on at %04x:%04x" % (
                    uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)))
            scs, sip = self.stop
            if scs != STUB_PARA:
                raise EmuError("int 3 at %04x:%04x" % (scs, sip))
            key = self.stubs[sip]
            if key in (("SENTINEL", 0), ("IRET", 0)):
                return (uc.reg_read(UC_X86_REG_DX) << 16) | \
                    uc.reg_read(UC_X86_REG_AX)
            cs, ip = self._handle(key)

    def _ret(self, pop, ax=None, dx=None):
        """return from a far stub, popping pop bytes of arguments"""
        uc = self.uc
        sp = uc.reg_read(UC_X86_REG_SP)
        ip, cs = self.r16(STACK_PARA, sp), self.r16(STACK_PARA, sp + 2)
        uc.reg_write(UC_X86_REG_SP, (sp + 4 + pop) & 0xFFFF)
        if ax is not None:
            uc.reg_write(UC_X86_REG_AX, ax & 0xFFFF)
        if dx is not None:
            uc.reg_write(UC_X86_REG_DX, dx & 0xFFFF)
        return cs, ip

    def cstr(self, seg, off):
        out = bytearray()
        while True:
            b = self.r8(seg, off + len(out))
            if not b:
                return out.decode("latin-1")
            out.append(b)

    def _handle(self, key):
        if key == ("VXD", 0):
            return self._vxd()
        if key not in STUB_ARGS:
            raise EmuError("import %s.%d called" % key)
        pop = STUB_ARGS[key]
        if key == ("MMSYSTEM", 31):
            w = self._args(11)
            dw2 = w[0] | (w[1] << 16)
            dw1 = w[2] | (w[3] << 16)
            user = w[4] | (w[5] << 16)
            msg = w[6]
            self.callbacks.append((msg, user, dw1, dw2))
            return self._ret(pop, ax=1)
        if key == ("MMSYSTEM", 607):
            self.clock += 50
            return self._ret(pop, ax=self.clock, dx=self.clock >> 16)
        if key == ("KERNEL", 127):
            w = self._args(7)
            file_ = self.cstr(w[1], w[0])
            default = w[2]
            k = self.cstr(w[4], w[3])
            sec = self.cstr(w[6], w[5])
            self.ppint.append((sec, k, default, file_))
            found = {(a.lower(), b.lower()): v for (a, b), v in
                     self.ini.items()}
            v = found.get((sec.lower(), k.lower()))
            return self._ret(pop, ax=default if v is None else profile_int(v))
        if key == ("KERNEL", 5):
            size = self._args(2)[0]
            at = (self.heap + 1) & ~1
            self.heap = at + size
            self.wr(DGROUP * 16 + at, bytes(size))
            return self._ret(pop, ax=at)
        if key == ("KERNEL", 7):
            return self._ret(pop, ax=0)
        if key == ("KERNEL", 15):       # the mixer's state block (5:00E8)
            size = self._args(2)[0]
            self.wr(MIXSTATE_PARA * 16, bytes(size))
            return self._ret(pop, ax=MIXSTATE_PARA)
        if key == ("USER", 176):        # LoadString(hInst, id, lpBuf, n)
            n, boff, bseg, sid = self._args(4)[:4]
            s = self.string(sid).encode("latin-1")[:max(n - 1, 0)]
            self.wr(bseg * 16 + boff, s + b"\0")
            return self._ret(pop, ax=len(s))
        if key == ("USER", 420):        # wsprintf(buf, fmt, ...), cdecl
            w = self._args(8)
            fmt = self.cstr(w[3], w[2])
            out = fmt.replace("%X", "%X" % w[4]).encode("latin-1")
            self.wr(w[1] * 16 + w[0], out + b"\0")
            return self._ret(0, ax=len(out))
        if key == ("USER", 471):        # lstrcmpi(a, b)
            boff, bseg, aoff, aseg = self._args(4)
            a = self.cstr(aseg, aoff).lower()
            b = self.cstr(bseg, boff).lower()
            return self._ret(pop, ax=(a > b) - (a < b))
        if key == ("KERNEL", 353):      # lstrcpyn(dst, src, n)
            n, soff, sseg, doff, dseg = self._args(5)
            s = self.cstr(sseg, soff).encode("latin-1")[:max(n - 1, 0)]
            self.wr(dseg * 16 + doff, s + b"\0")
            return self._ret(pop, ax=doff, dx=dseg)
        raise EmuError("import %s.%d not modelled" % key)

    def _vxd(self):
        uc = self.uc
        dx = uc.reg_read(UC_X86_REG_DX)
        bx = uc.reg_read(UC_X86_REG_BX)
        self.vxd_calls.append((dx, bx))
        flags = uc.reg_read(UC_X86_REG_EFLAGS) & ~1
        ax = 0
        if dx in self.vxd_fail:
            flags |= 1
            ax = 2
        elif dx == 0x0000:
            ax = 0x0404
        elif dx == 0x0004:
            ch = A1_DMA if bx == 0 else self.a2_dma
            ax = self.chip.dma_count(ch)
        elif dx == 0x040D:
            # a DOS program gave up the DSP
            self.dsp_taken.append(uc.reg_read(UC_X86_REG_ECX))
        elif dx not in (0x0002, 0x0003):
            raise EmuError("VxD function %04x" % dx)
        uc.reg_write(UC_X86_REG_EFLAGS, flags)
        return self._ret(0, ax=ax)

    def string(self, sid):
        data = self.ne.resource_data(6, (sid >> 4) + 1)
        pos = 0
        for _i in range(sid & 15):
            pos += 1 + data[pos]
        return data[pos + 1:pos + 1 + data[pos]].decode("latin-1")

    # --- the mixer ----------------------------------------------------------

    def mixer_init(self):
        """the mixer's state block and lines, as the first DRVM_ENABLE sets
        them up (5:00D6), after ESS's configuration and SYSTEM.INI"""
        return self.call((SEG_PARA[5], 0x00D6), (DEV_OFF,))

    def mxd(self, dev_id, msg, user=0, dw1=0, dw2=0):
        """mxdMessage, the export (ordinal 6)"""
        seg, off, _f = self.ne.entries[6]
        return self.call((SEG_PARA[seg], off), (
            dev_id, msg, user >> 16, user & 0xFFFF, dw1 >> 16, dw1 & 0xFFFF,
            dw2 >> 16, dw2 & 0xFFFF))

    def mixer_instance(self):
        """an instance as MXDM_OPEN makes it, in DGROUP: its device at
        +10h"""
        at = (self.heap + 1) & ~1
        self.heap = at + 0x20
        self.wr(DGROUP * 16 + at, bytes(0x20))
        self.w16(DGROUP, at + 0x10, DEV_OFF)
        return at

    # --- the wave devices ---------------------------------------------------

    def wod(self, dev_id, msg, user=0, dw1=0, dw2=0):
        """wodMessage, the export (ordinal 3)"""
        seg, off, _f = self.ne.entries[3]
        return self.call((SEG_PARA[seg], off), (
            dev_id, msg, user >> 16, user & 0xFFFF, dw1 >> 16, dw1 & 0xFFFF,
            dw2 >> 16, dw2 & 0xFFFF))

    def open(self, dev_id=1, rate=22050, channels=2, bits=16, flags=0,
             instance=0x11112222, hwave=0x0BAD):
        """WODM_OPEN with a PCMWAVEFORMAT: (result, dwUser)"""
        align = channels * bits // 8
        fmt = self.alloc(struct.pack("<HHIIHH", 1, channels, rate,
                                     rate * align, align, bits))
        desc = self.alloc(struct.pack("<HIIIHI", hwave, fmt, 0x5000AAAA,
                                      instance, 0, DEVNODE))
        user_ptr = self.alloc(b"\0" * 4)
        r = self.wod(dev_id, WODM_OPEN, user_ptr, desc,
                     flags | CALLBACK_FUNCTION)
        return r, self.r32(user_ptr >> 16, user_ptr & 0xFFFF)

    def wid(self, dev_id, msg, user=0, dw1=0, dw2=0):
        """widMessage, the export (ordinal 4)"""
        seg, off, _f = self.ne.entries[4]
        return self.call((SEG_PARA[seg], off), (
            dev_id, msg, user >> 16, user & 0xFFFF, dw1 >> 16, dw1 & 0xFFFF,
            dw2 >> 16, dw2 & 0xFFFF))

    def open_in(self, dev_id=1, rate=49716, channels=2, bits=16, flags=0,
                instance=0x33334444, hwave=0x0BED):
        """WIDM_OPEN with a PCMWAVEFORMAT: (result, dwUser)"""
        align = channels * bits // 8
        fmt = self.alloc(struct.pack("<HHIIHH", 1, channels, rate,
                                     rate * align, align, bits))
        desc = self.alloc(struct.pack("<HIIIHI", hwave, fmt, 0x5000BBBB,
                                      instance, 0, DEVNODE))
        user_ptr = self.alloc(b"\0" * 4)
        r = self.wid(dev_id, WIDM_OPEN, user_ptr, desc,
                     flags | CALLBACK_FUNCTION)
        return r, self.r32(user_ptr >> 16, user_ptr & 0xFFFF)

    def header(self, data, flags=WHDR_PREPARED, loops=0):
        """a prepared WAVEHDR for data: its far pointer"""
        buf = self.alloc(data)
        return self.alloc(struct.pack("<IIIIIIII", buf, len(data), 0, 0,
                                      flags, loops, 0, 0))

    def hdr_flags(self, hdr):
        return self.r32(hdr >> 16, (hdr & 0xFFFF) + 0x10)

    def write(self, user, hdr):
        return self.wod(0, WODM_WRITE, user, hdr, 0x20)

    def getpos(self, user, wtype=TIME_BYTES):
        mmt = self.alloc(struct.pack("<HI", wtype, 0) + b"\0\0")
        r = self.wod(0, WODM_GETPOS, user, mmt, 8)
        return r, self.r16(mmt >> 16, mmt & 0xFFFF), \
            self.r32(mmt >> 16, (mmt & 0xFFFF) + 2)

    def done(self):
        """the headers given back (WOM_DONE), in order"""
        return [c[2] for c in self.callbacks if c[0] == WOM_DONE]

    # --- the hardware running -----------------------------------------------

    def dma(self, n):
        """the chip takes n bytes from each channel that runs, in step: the
        8237 moves on, and what it read is kept, Audio 1's in played and
        Audio 2's in played2"""
        for ch, out in ((A1_DMA, self.played), (self.a2_dma, self.played2)):
            d = self.chip.dma[ch]
            if d["mask"]:
                continue
            if ch >= 4:     # words: a 17-bit word address, the page's bit 0
                size = (d["count"] + 1) * 2
                base = ((d["page"] & 0xFE) << 16) | (d["addr"] << 1)
            else:
                size = d["count"] + 1
                base = (d["page"] << 16) | d["addr"]
            pos = d["pos"] * 2 if ch >= 4 else d["pos"]
            for _i in range(n):
                out.append(self.rd(base + pos, 1)[0])
                pos += 1
                if pos >= size:
                    pos = 0
            d["pos"] = pos // 2 if ch >= 4 else pos

    def interrupt(self):
        """an Audio 1 interrupt through ESS's handler: DX:AX unused"""
        self.chip.a1_irq = True
        uc = self.uc
        uc.reg_write(UC_X86_REG_SS, STACK_PARA)
        uc.reg_write(UC_X86_REG_SP, 0xFF00)
        uc.reg_write(UC_X86_REG_DS, 0)
        uc.reg_write(UC_X86_REG_ES, 0)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x0002)
        self._push(0x0202, STUB_PARA, self.iret_stop)
        self._run((ISR_PARA, 0))
        if uc.reg_read(UC_X86_REG_SP) != 0xFF00:
            raise EmuError("the handler left the stack at %04x" %
                           uc.reg_read(UC_X86_REG_SP))

    def isr_slot(self, index):
        """isr_srv_table[index] as (segment number, offset)"""
        off = self.r16(DGROUP, 0xAE + 4 * index)
        sel = self.r16(DGROUP, 0xB0 + 4 * index)
        for seg, para in SEG_PARA.items():
            if para == sel:
                return seg, off
        return None, off
