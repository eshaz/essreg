# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Run esssb16.com in a CPU emulator against a model of the ES1869.

DOS, the memory manager and the DPMI host are Python behind the interrupt
hook: int 21h (the program's output, the device opens and IOCTLs of QEMM
and EMM386, the memory blocks with upper memory's, the UMB link and the
allocation strategy, the current PSP, and the exits), int 2Fh (Windows,
QPIEMU's entry, EMM386's port traps, the DPMI host), and the QPI entry,
the callback before the program's and the DPMI entry, which are stubs that
interrupt. Every IN and OUT goes to vxdemu's FakeES1869, which has the
mixer, the DSP and the PnP registers.

Machine runs the installer the way COMMAND.COM would, then calls the
resident shims where the resident part stays (`seg`), the way the memory
manager does: qpi_io() is a QPI callback in V86 mode with a far frame,
emm_io() EMM386's handler as EmmCpu calls it. PMMachine runs the
protected-mode parts at ring 3: HDPMI32i's handler (hdpmi_io()), and the
installer's code from the DPMI switch on (install()), with the descriptor
functions of DPMI. The three share the memory.

The models keep what made the real thing fail: DOS reuses the memory past
what the TSR keeps, and all of the program's block when the resident part
moved to a block of its own (resident() fills it with INT 3), EMM386
searches the program's dispatch table where the program left it at every
trapped access and calls the handler at ring 0 through selectors as long
as DI said, with the direction flag set, and HDPMI takes the client's
real-mode stack at the switch and returns to it on a 16-bit code selector.

Needs the unicorn package.
"""

import ctypes
import os
import struct
import sys

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_MODE_32, UC_HOOK_INTR,
                     UC_HOOK_INSN, UC_HOOK_CODE, UC_HOOK_MEM_READ,
                     UC_HOOK_MEM_WRITE, UC_PROT_ALL)
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_EAX,
                               UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                               UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP,
                               UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_CS,
                               UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
                               UC_X86_REG_EFLAGS, UC_X86_REG_GDTR)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from vxdemu import FakeES1869  # noqa: E402
from dma8237 import DMA8237  # noqa: E402


PAGE_PORTS = {0: 0x87, 1: 0x83, 2: 0x81, 3: 0x82, 5: 0x8B, 6: 0x89, 7: 0x8A}


class BufferMemory:
    """the emulator's first megabyte as the DMA's memory"""

    def __init__(self, buf):
        self.buf = buf

    def read(self, addr, n):
        return bytes(self.buf[addr:addr + n])

    def write(self, addr, data):
        self.buf[addr:addr + len(data)] = bytes(data)


class _Channels:
    """chip.dma[ch]: a channel as the tests compare it"""

    def __init__(self, dmac):
        self.dmac = dmac

    def __getitem__(self, ch):
        return self.dmac.ch[ch].view()


class Chip(FakeES1869):
    """FakeES1869 with Audio 1's interrupt bit (Audio_Base+Ch bit 0, cleared
    by a read of +Eh), the chip's own commands with an operand, E0h's
    answer, the PnP registers the installer reads, and the two 8237 DMA
    controllers with their page registers (dma8237): a game programs its
    channel and the program reads it back and programs the card's"""

    SB_OPERANDS = {**FakeES1869.SB_OPERANDS, 0x41: 1, 0x42: 1, 0xCF: 1}

    def __init__(self, base=0x220, cfg=0x250, irq=5, dma8=1):
        super().__init__(base, cfg)
        self.a1_irq = False
        self.pnp.update({(1, 0x70): irq, (1, 0x72): 0, (1, 0x74): dma8,
                         (1, 0x75): 4})
        self.dmac = DMA8237()
        self.dma = _Channels(self.dmac)
        self.dma_log = []

    @property
    def ff(self):
        """the controllers' flip-flops"""
        return self.dmac.ff

    @ff.setter
    def ff(self, v):
        self.dmac.ff = list(v)

    def _dsp(self, v):
        n = len(self.commands)
        super()._dsp(v)
        if len(self.commands) > n and self.commands[-1][0] == 0xE0:
            self.out.append(~self.commands[-1][1] & 0xFF)   # E0h: the inverse

    def _inb(self, port):
        if self.dmac.handles(port):
            return self.dmac.inb(port)
        v = super()._inb(port)
        if port == self.base + 0x0C and self.a1_irq:
            v |= 0x01
        elif port == self.base + 0x0E:
            self.a1_irq = False
        return v

    def outb(self, port, v):
        if not self.dmac.handles(port):
            super().outb(port, v)
            return
        self.log.append(("out", port, v))
        self.dma_log.append((port, v))
        self.dmac.outb(port, v)


class DeadChip(Chip):
    """nothing at the ports"""

    def _inb(self, port):
        return 0xFF

    def outb(self, port, v):
        self.log.append(("out", port, v))


PSP_PARA = 0x1000           # the program, loaded as COMMAND.COM loads it
ENV_PARA = 0x0800           # its environment
LOL_PARA = 0x0090           # DOS's list of lists
MCB_PARA = 0x0FFF           # the first memory control block
COPY_PARA = 0x3000          # a copy that is resident already (loaded=True)
SYS_MCB = 0x4000            # the system's block up to upper memory, linked
GAME_SS, GAME_SP = 0x5000, 0x1000       # the stack a trapped access comes with
STUB_PARA = 0x6000          # the stubs that interrupt
HOST_PARA = 0x7000          # a block of conventional memory, from int 21h 48h
# upper memory: each free block's MCB and paragraphs, where dosrun links
# it too; UMBS one 32 KB block, UMBS2 another below it, at B000h
UMBS = ((0xC800, 0x07FF),)
UMBS2 = ((0xB000, 0x07FF), (0xC800, 0x07FF))
DPMI_PARA = 0x6100          # the DPMI host's resident segment: its logo
QPI_OFF, OLD_OFF, SENTINEL_OFF, DPMI_OFF, MUX_OFF = 0x00, 0x10, 0x20, 0x30, 0x40
HDPMI_LOGO = b"HDPMI\x03\x14\x01"     # HDPMI 3.20, 32-bit
POISON = 0xCC               # INT 3: memory DOS or the host took back
INT_QPI, INT_OLD, INT_SENTINEL, INT_DPMI, INT_VENDOR = 0xE0, 0xE1, 0xE2, 0xE4, 0xE5
INT_MUX = 0xE6              # int 2Fh's handler before the program's: the stub
CF = 0x0001
IF = 0x0200

REGS = {"eax": UC_X86_REG_EAX, "ebx": UC_X86_REG_EBX, "ecx": UC_X86_REG_ECX,
        "edx": UC_X86_REG_EDX, "esi": UC_X86_REG_ESI, "edi": UC_X86_REG_EDI,
        "ebp": UC_X86_REG_EBP, "esp": UC_X86_REG_ESP, "eip": UC_X86_REG_EIP,
        "cs": UC_X86_REG_CS, "ds": UC_X86_REG_DS, "es": UC_X86_REG_ES,
        "ss": UC_X86_REG_SS, "eflags": UC_X86_REG_EFLAGS}


def trap_eax(value=None):
    """EAX at a trapped access: AL the byte written (A5h for a read), and
    the rest whatever the game had there"""
    return 0x5A5AA500 | (value if value is not None else 0xA5)


class EmuError(Exception):
    pass


class Cpu:
    """register access by name, and the port hooks; the first megabyte is
    `memory`, shared with the other CPUs of the same machine"""

    def __init__(self, mode, chip, memory=None):
        self.uc = Uc(UC_ARCH_X86, mode)
        self.chip = chip
        self.ports = []                 # every IN and OUT the code did itself
        self.stopped = None
        self.memory = memory or ctypes.create_string_buffer(0x100000)
        self.uc.mem_map_ptr(0, 0x100000, UC_PROT_ALL,
                            ctypes.addressof(self.memory))
        self.uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        self.uc.hook_add(UC_HOOK_INTR, self._intr)

    def _in(self, uc, port, size, _user):
        self._direct(port)
        v = self.chip.inb(port)
        self.ports.append(("in", port, v))
        return v

    def _out(self, uc, port, size, value, _user):
        self._direct(port)
        self.ports.append(("out", port, value & 0xFF))
        self.chip.outb(port, value & 0xFF)

    def _direct(self, port):
        """a port the code reaches itself: inside the QPI callback, a port
        QPI traps would call the callback again"""
        if getattr(self, "in_callback", False) and port in self.qpi_trapped:
            raise EmuError("the QPI callback reached port %03Xh, which QPI "
                           "traps, directly" % port)

    def _intr(self, uc, intno, _user):
        raise NotImplementedError

    def r(self, name):
        return self.uc.reg_read(REGS[name])

    def w(self, name, v):
        self.uc.reg_write(REGS[name], v)

    def r16(self, name):
        return self.r(name) & 0xFFFF

    def r8(self, name):
        return self.r(name) & 0xFF

    def w16(self, name, v):
        self.w(name, (self.r(name) & 0xFFFF0000) | (v & 0xFFFF))

    def w8(self, name, v):
        self.w(name, (self.r(name) & 0xFFFFFF00) | (v & 0xFF))

    def carry(self, on):
        f = self.r("eflags")
        self.w("eflags", (f | CF) if on else (f & ~CF))

    def stop(self, why):
        self.stopped = why
        self.uc.emu_stop()

    def mem(self, addr, n):
        return bytes(self.uc.mem_read(addr, n))

    def put(self, addr, data):
        self.uc.mem_write(addr, bytes(data))

    def cstr(self, addr):
        s = b""
        while True:
            c = self.mem(addr + len(s), 1)
            if c == b"\0":
                return s.decode("latin-1")
            s += c

    def run(self, start, count=50_000_000):
        self.stopped = None
        self.uc.emu_start(start, 0, count=count)
        if self.stopped is None:
            raise EmuError("ran on at %04x:%08x" % (self.r16("cs"),
                                                    self.r("eip")))
        return self.stopped


class Machine(Cpu):
    """DOS, a memory manager and the chip around the real-mode program;
    `umbs` the free blocks of upper memory (UMBS), `load_seg` the PSP that
    DOS gives the program (a UMB's for LH), `memory` another machine's, for
    the program loaded again where it is resident"""

    def __init__(self, image, syms, chip=None,
                 env="BLASTER=A220 I5 D1 H5 P330 T6", umbs=(),
                 load_seg=PSP_PARA, memory=None):
        super().__init__(UC_MODE_16, chip or Chip(), memory)
        if hasattr(self.chip, "dmac"):  # the DMA reaches the first megabyte
            self.chip.dmac.memory = BufferMemory(self.memory)
        self.image, self.syms = image, syms
        self.output = ""                # what the program printed
        self.exit = None                # ("tsr", paragraphs) or ("exit", code)
        self.qemm = False               # the QEMM386$ device answers
        self.qpiemu = False             # int 2Fh 1684h/4354h answers
        self.qpi_version = 0x0703
        self.qpi_fail = set()           # QPI functions that fail
        self.qpi_fail_after = {}        # function: the calls that succeed
        self.qpi_calls = []             # (function, dx, es:di)
        self.qpi_counts = {}            # function: its calls
        self.qpi_trapped = set()        # ports trapped through 1A09h
        self.qpi_callback = (0, 0)      # the I/O callback: seg, off
        self.qpi_accesses = []          # 1A00h/1A01h: ("in", port, v) ...
        self.chained = []               # calls to the callback before ours
        self.emm386 = None              # (major, minor) of EMMXXXX0
        self.noems = False              # EMM386 with NOEMS: EMMQXXX0
        self.emm_fail = set()           # the 4A15h calls that fail (0, 1, ...)
        self.emm_low_only = False       # EMM386 refuses tables in upper memory
        self.emm_traps = []             # 4A15h: dicts
        self.emm_out = 4                # EMM386's ECX for an out: 4, or 8
        self.dpmi = False               # int 2Fh 1687h answers
        self.dpmi_logo = HDPMI_LOGO     # at the start of the host's segment
        self.dpmi_paras = 0x20          # the host's data: its real-mode stack
        self.dpmi_fails = False         # the switch fails: CF, real mode
        self.dpmi_call = None           # (ax, es, cs, ip, sp) at the entry
        self.kept = None                # the paragraphs the TSR kept
        self.sharers = [self]           # the CPUs on this memory
        self.windows = 0                # int 2Fh 1600h's AL
        self.umb_link = 0
        self.umb_links = []
        self.umbs = tuple(umbs)
        self.load_seg = load_seg
        self.seg = load_seg             # where the resident part is
        self.psp = load_seg             # DOS's current PSP (int 21h 50h)
        self.psps = []                  # int 21h 50h's
        self.tsr_psp = None             # the current PSP at int 21h 31h
        self.strategy = 0               # int 21h 5801h's
        self.strategies = []
        self.freed, self.resized, self.allocated = [], [], []
        self.alloc_fail = False
        self.mux_chained = []           # int 2Fh's AX at the handler before
        self.own_traps = False          # its accesses through EMM386's traps
        self.path = "qpi"               # how in() and out() reach the front
        self.load(env)

    # --- the program and its surroundings -----------------------------------
    def load(self, env):
        psp = self.load_seg * 16
        self.put(psp, b"\xCD\x20" + struct.pack("<H", 0xA000))
        self.put(psp + 0x16, struct.pack("<H", 0x0700))     # COMMAND.COM's
        self.put(psp + 0x2C, struct.pack("<H", ENV_PARA))
        self.put(psp + 0x34, struct.pack("<HH", 0x18, self.load_seg))
        self.put(psp + 0x100, self.image)
        e = env.encode("latin-1") + b"\0" if env else b""
        self.put(ENV_PARA * 16, e + b"\0" + struct.pack("<H", 1) +
                 b"C:\\ESSSB16.COM\0")
        # the memory blocks: the program's (64 KB), a free one, the last;
        # loaded high, the program has upper memory's first block
        self.put(LOL_PARA * 16, struct.pack("<H", MCB_PARA))
        high = self.load_seg >= 0xA000
        self.mcb(MCB_PARA, b"M", 0 if high else PSP_PARA, 0x1000)
        self.mcb(MCB_PARA + 0x1001, b"M", 0, 0xFFE)
        self.mcb(COPY_PARA - 1, b"Z", 0, 0x1000)
        for i, (para, size) in enumerate(self.umbs):
            last = i == len(self.umbs) - 1
            self.mcb(para, b"Z" if last else b"M", 0, size)
            if not last:                # the adapters' memory between
                hole = para + size + 1
                self.mcb(hole, b"M", 8, self.umbs[i + 1][0] - hole - 1, b"SC")
        if high:
            self.put((self.load_seg - 1) * 16 + 1,
                     struct.pack("<H", self.load_seg))
        # the stubs: the QPI entry, the callback before ours, the sentinel
        # the shims return to, int 2Fh's handler before ours, the DPMI entry
        s = STUB_PARA * 16
        self.put(s + QPI_OFF, bytes([0xCD, INT_QPI, 0xCB]))
        self.put(s + OLD_OFF, bytes([0xCD, INT_OLD, 0xCB]))
        self.put(s + SENTINEL_OFF, bytes([0xCD, INT_SENTINEL, 0xF4]))
        self.put(s + MUX_OFF, bytes([0xCD, INT_MUX, 0xCF]))
        self.put(0x2F * 4, struct.pack("<HH", MUX_OFF, STUB_PARA))
        self.put(DPMI_PARA * 16 + DPMI_OFF, bytes([0xCD, INT_DPMI, 0xCB]))

    def mcb(self, para, kind, owner, size, name=b""):
        self.put(para * 16, kind + struct.pack("<HH", owner, size) + b"\0" * 3
                 + name.ljust(8, b"\0"))

    def block(self, seg):
        """the memory block at seg: (kind, owner, paragraphs, name)"""
        kind, owner, size = struct.unpack("<cHH", self.mem((seg - 1) * 16, 5))
        name = self.mem((seg - 1) * 16 + 8, 8)
        return kind, owner, size, name

    def chain(self):
        """the memory blocks as DOS walks them, from the first: (seg, kind,
        owner, paragraphs)"""
        para, blocks = MCB_PARA, []
        while len(blocks) < 64:
            kind, owner, size, _name = self.block(para + 1)
            if kind not in (b"M", b"Z"):
                raise EmuError("no memory block at %04Xh" % para)
            blocks.append((para + 1, kind, owner, size))
            if kind == b"Z":
                return blocks
            para += size + 1
        raise EmuError("the memory blocks go round")

    def link_umbs(self, on):
        """upper memory joins the chain (int 21h 5803h): the last block of
        conventional memory 'M', then the system's block over the rest up
        to the first UMB's MCB; else 'Z' again"""
        self.put((COPY_PARA - 1) * 16, b"M" if on and self.umbs else b"Z")
        if on and self.umbs:
            self.mcb(SYS_MCB, b"M", 8, self.umbs[0][0] - SYS_MCB - 1, b"SC")

    def tail(self, text):
        t = text.encode("latin-1")
        self.put(self.load_seg * 16 + 0x80, bytes([len(t)]) + t + b"\r")

    def copy_resident(self):
        """a copy of the program at COPY_PARA, owned by its own PSP"""
        self.mcb(COPY_PARA - 1, b"Z", COPY_PARA, 0x1000)
        self.put(COPY_PARA * 16 + 0x100, self.image)

    def _in(self, uc, port, size, _user):
        if self.own_traps:
            al, cf = self.emm_io(port)
            if not cf:
                return al
        return super()._in(uc, port, size, _user)

    def _out(self, uc, port, size, value, _user):
        if self.own_traps:
            _al, cf = self.emm_io(port, value & 0xFF)
            if not cf:
                return
        super()._out(uc, port, size, value, _user)

    def flush(self):
        """the code the CPUs translated, dropped after memory changed"""
        for cpu in self.sharers:
            cpu.uc.ctl_flush_tb()

    def resident(self, paras):
        """the TSR in: DOS shrinks the current PSP's block to what it keeps
        and reuses the rest, and all of the program's block if that isn't
        the one (the resident part moved)"""
        self.kept = paras
        seg = self.tsr_psp if self.tsr_psp is not None else self.load_seg
        self.seg = seg
        poison = bytes([POISON])
        kind, owner, size, _name = self.block(seg)
        if seg == PSP_PARA:             # the program's 64 KB, as COMMAND.COM
            size = 0x1000               # loads it
        elif kind not in (b"M", b"Z"):
            raise EmuError("the TSR's PSP %04Xh has no memory block" % seg)
        if paras > size:
            raise EmuError("the TSR keeps %Xh paragraphs of a block of %Xh"
                           % (paras, size))
        if paras < size and seg != PSP_PARA:
            self.mcb(seg - 1, b"M", owner, paras,
                     self.mem((seg - 1) * 16 + 8, 8))
            self.mcb(seg + paras, kind, 0, size - paras - 1)
            self.put((seg + paras + 1) * 16, poison * ((size - paras - 1) * 16))
        elif paras < size:
            self.put((seg + paras) * 16, poison * ((size - paras) * 16))
        if seg != self.load_seg:
            self.put(self.load_seg * 16, poison * 0x10000)
        self.flush()

    def sym(self, name):
        return self.syms[name]

    def addr(self, name):
        return self.seg * 16 + self.syms[name]

    def data8(self, name):
        return self.mem(self.addr(name), 1)[0]

    def data16(self, name):
        return struct.unpack("<H", self.mem(self.addr(name), 2))[0]

    def set8(self, name, v):
        self.put(self.addr(name), bytes([v & 0xFF]))

    def set16(self, name, v):
        self.put(self.addr(name), struct.pack("<H", v))

    def flags(self):
        return self.data8("flags")

    # --- the interrupts ------------------------------------------------------
    def _intr(self, uc, intno, _user):
        if intno == 0x21:
            self._dos()
        elif intno == 0x2F:
            self._mux()
        elif intno == INT_QPI:
            self._qpi()
        elif intno == INT_OLD:
            self.chained.append((self.r16("edx"), self.r8("ecx"),
                                 self.r8("eax")))
            self.w8("eax", 0x5A)
        elif intno == INT_SENTINEL:
            self.stop("sentinel")
        elif intno == INT_MUX:
            self.mux_chained.append(self.r16("eax"))
        elif intno == INT_DPMI:
            ss, sp = self.r16("ss"), self.r16("esp")
            ip, cs = struct.unpack("<HH", self.mem(ss * 16 + sp, 4))
            es = self.r16("es")
            self.dpmi_call = (self.r16("eax"), es, cs, ip, sp + 4)
            # the host takes its data, the client's real-mode stack
            self.put(es * 16, bytes([POISON]) * (self.dpmi_paras * 16))
            self.flush()
            if self.dpmi_fails:
                self.w16("eax", 0x8021)
                self.carry(True)
                return
            self.stop("dpmi")
        else:
            raise EmuError("int %02Xh at %04x:%04x" % (
                intno, self.r16("cs"), self.r16("eip")))

    def _dos(self):
        ax, ah = self.r16("eax"), self.r8("eax") >> 8 & 0xFF
        ah = (ax >> 8) & 0xFF
        ds, dx = self.r16("ds"), self.r16("edx")
        if ah == 0x09:
            s = b""
            while True:
                c = self.mem(ds * 16 + dx + len(s), 1)
                if c == b"$":
                    break
                s += c
            self.output += s.decode("latin-1")
        elif ah == 0x02:
            self.output += chr(dx & 0xFF)
        elif ah == 0x3D:
            name = self.cstr(ds * 16 + dx)
            if name == "QEMM386$" and self.qemm:
                self.w16("eax", 5)
                self.carry(False)
            elif name == ("EMMQXXX0" if self.noems else "EMMXXXX0") \
                    and self.emm386:
                self.w16("eax", 6)
                self.carry(False)
            else:
                self.w16("eax", 2)
                self.carry(True)
        elif ax == 0x4402:
            bx, cx = self.r16("ebx"), self.r16("ecx")
            if bx == 5:
                data = struct.pack("<HH", QPI_OFF, STUB_PARA)
            elif self.mem(ds * 16 + dx, 1) == b"\x02":
                data = bytes(self.emm386)   # subfunction 2: the version
            else:
                data = b"\0\0\0\0\0\0"   # subfunction 0: the entry point
            self.put(ds * 16 + dx, data[:cx])
            self.w16("eax", len(data[:cx]))
            self.carry(False)
        elif ah == 0x3E:
            self.carry(False)
        elif ah == 0x52:
            self.w16("es", LOL_PARA)
            self.w16("ebx", 2)
        elif ax == 0x5800:
            self.w16("eax", self.strategy)
            self.carry(False)
        elif ax == 0x5801:
            bx = self.r16("ebx")
            self.strategies.append(bx)
            if bx & 0xFF3C or bx & 3 == 3:
                self.w16("eax", 1)
                self.carry(True)
            else:
                self.strategy = bx
                self.carry(False)
        elif ax == 0x5802:
            self.w8("eax", self.umb_link)
            self.carry(False)
        elif ax == 0x5803:
            bx = self.r16("ebx")
            self.umb_links.append(bx)
            if bx & 1 and not self.umbs:        # no upper memory (DOS=UMB)
                self.w16("eax", 1)
                self.carry(True)
            else:
                self.umb_link = bx
                self.link_umbs(bool(bx & 1))
                self.carry(False)
        elif ah == 0x49:
            self.free(self.r16("es"))
            self.carry(False)
        elif ah == 0x4A:
            self.resized.append((self.r16("es"), self.r16("ebx")))
            self.carry(False)
        elif ah == 0x48:
            self.alloc(self.r16("ebx"))
        elif ah == 0x50:
            self.psp = self.r16("ebx")
            self.psps.append(self.psp)
        elif ah == 0x35:
            ip, cs = struct.unpack("<HH", self.mem((ax & 0xFF) * 4, 4))
            self.w16("es", cs)
            self.w16("ebx", ip)
        elif ah == 0x25:
            self.put((ax & 0xFF) * 4, struct.pack("<HH", dx, ds))
        elif ah in (0x51, 0x62):
            self.w16("ebx", self.psp)
        elif ah == 0x31:
            self.exit = ("tsr", dx)
            self.tsr_psp = self.psp
            self.stop("tsr")
        elif ah == 0x4C:
            self.exit = ("exit", ax & 0xFF)
            self.stop("exit")
        else:
            raise EmuError("int 21h AX=%04X" % ax)

    def alloc(self, paras):
        """int 21h 48h: with the UMBs linked and a strategy of high memory,
        a block of upper memory, the first, best or last that fits, else
        (high first, or the strategy low) conventional memory; owned by the
        current PSP"""
        self.allocated.append((paras, self.strategy, self.umb_link & 1))
        seg = None
        high = self.umbs and self.umb_link & 1 and self.strategy & 0xC0
        if high:
            seg = self._umb_alloc(paras, self.strategy & 3)
        if seg is None and not (high and self.strategy & 0x40) \
                and not self.alloc_fail:
            seg = HOST_PARA
        if seg is None:
            self.w16("eax", 8)
            self.w16("ebx", 0)
            self.carry(True)
            return
        self.w16("eax", seg)
        self.carry(False)

    def _umb_alloc(self, paras, fit):
        free = []
        para = self.umbs[0][0]
        while True:
            kind, owner, size, _name = self.block(para + 1)
            if owner == 0 and size >= paras:
                free.append((para, kind, size))
            if kind == b"Z":
                break
            para += size + 1
        if not free:
            return None
        if fit == 1:
            para, kind, size = min(free, key=lambda b: b[2])
        else:
            para, kind, size = free[-1 if fit == 2 else 0]
        if size == paras:
            self.mcb(para, kind, self.psp, paras)
            return para + 1
        if fit == 2:                    # the top of the block
            self.mcb(para, b"M", 0, size - paras - 1)
            at = para + size - paras
            self.mcb(at, kind, self.psp, paras)
            return at + 1
        self.mcb(para, b"M", self.psp, paras)
        self.mcb(para + paras + 1, kind, 0, size - paras - 1)
        return para + 1

    def free(self, seg):
        """int 21h 49h: the block's owner 0"""
        self.freed.append(seg)
        if self.mem((seg - 1) * 16, 1) in (b"M", b"Z"):
            self.put((seg - 1) * 16 + 1, b"\0\0")

    def _mux(self):
        ax = self.r16("eax")
        if ax == 0x1600:
            self.w8("eax", self.windows)
        elif ax == 0x1684 and self.r16("ebx") == 0x4354:
            if self.qpiemu:
                self.w8("eax", 0)
                self.w16("es", STUB_PARA)
                self.w16("edi", QPI_OFF)
        elif ax == 0x4A15 and self.r16("ebx") == 0:
            if not self.emm386:
                return
            edx = self.r("edx")
            cx, ds, si = self.r16("ecx"), self.r16("ds"), self.r16("esi")
            size = self.r16("edi")
            table = [struct.unpack("<HH", self.mem(ds * 16 + si + 4 * i, 4))
                     for i in range(cx)]
            call = {"first": edx & 0xFFFF, "last": edx >> 16, "count": cx,
                    "table": table, "size": size, "ds": ds, "si": si,
                    "handle": len(self.emm_traps) + 1}
            self.emm_traps.append(call)
            if len(self.emm_traps) - 1 in self.emm_fail or \
                    (self.emm_low_only and ds >= 0xA000):
                call["failed"] = True
                return
            # EMM386 reaches the table and the handlers at ring 0 through
            # selectors of DS as long as DI: beyond, they fault
            if si + 4 * cx > size:
                raise EmuError("the dispatch table (%04Xh-%04Xh) is beyond "
                               "the %04Xh bytes EMM386 maps" %
                               (si, si + 4 * cx - 1, size))
            for port, handler in table:
                if handler >= size:
                    raise EmuError("the handler of port %03Xh (%04Xh) is "
                                   "beyond the %04Xh bytes EMM386 maps" %
                                   (port, handler, size))
            self.w16("eax", call["handle"])
            self.carry(False)
        elif ax == 0x1687:
            if self.dpmi:
                self.w16("eax", 0)
                self.w16("ebx", 1)
                self.w8("ecx", 4)
                self.w16("edx", 0x005A)
                self.w16("esi", self.dpmi_paras)
                self.w16("es", DPMI_PARA)
                self.w16("edi", DPMI_OFF)
        else:
            raise EmuError("int 2Fh AX=%04X" % ax)

    def _qpi(self):
        ax, dx = self.r16("eax"), self.r16("edx")
        fn = 0x0300 if ax >> 8 == 3 else ax
        self.qpi_calls.append((fn, dx, (self.r16("es"), self.r16("edi"))))
        calls = self.qpi_counts[fn] = self.qpi_counts.get(fn, 0) + 1
        if fn in self.qpi_fail or calls > self.qpi_fail_after.get(fn, calls):
            self.carry(True)
            return
        self.carry(False)
        if fn == 0x0300:
            self.w16("eax", self.qpi_version)
            self.w16("ebx", self.qpi_version)
        elif fn == 0x1A06:
            self.w16("es", self.qpi_callback[0])
            self.w16("edi", self.qpi_callback[1])
        elif fn == 0x1A07:
            self.qpi_callback = (self.r16("es"), self.r16("edi"))
        elif fn == 0x1A09:
            self.qpi_trapped.add(dx)
        elif fn == 0x1A0A:
            self.qpi_trapped.discard(dx)
        elif fn == 0x1A00:
            v = self.chip.inb(dx)
            self.qpi_accesses.append(("in", dx, v))
            self.w8("ebx", v)
        elif fn == 0x1A01:
            v = self.r8("ebx")
            self.qpi_accesses.append(("out", dx, v))
            self.chip.outb(dx, v)
        else:
            raise EmuError("QPI function %04X" % fn)

    # --- the installer -------------------------------------------------------
    def install(self, tail="", qemm=False, qpiemu=False, emm386=None,
                noems=False, dpmi=False, windows=0, loaded=False,
                old_callback=True):
        self.qemm, self.qpiemu, self.emm386 = qemm, qpiemu, emm386
        self.noems = noems
        self.dpmi, self.windows = dpmi, windows
        self.qpi_callback = (STUB_PARA, OLD_OFF) if old_callback else (0, 0)
        self.put(DPMI_PARA * 16, self.dpmi_logo)
        if loaded:
            self.copy_resident()
        self.tail(tail)
        for name in ("cs", "ds", "es", "ss"):
            self.w16(name, self.load_seg)
        if self.load_seg == PSP_PARA:   # 64 KB, else the UMB that LH gave
            self.w("esp", 0xFFFE)
        else:
            self.w("esp", self.block(self.load_seg)[2] * 16 - 2)
        self.w("eflags", IF | 2)
        self.w16("eip", 0x100)
        why = self.run(self.load_seg * 16 + 0x100)
        if self.exit and self.exit[0] == "tsr":
            self.resident(self.exit[1])
        return why

    # --- the shims -----------------------------------------------------------
    def _frame(self):
        """the far frame of a call from the memory manager: the sentinel"""
        self.w16("ss", GAME_SS)
        sp = GAME_SP - 4
        self.put(GAME_SS * 16 + sp, struct.pack("<HH", SENTINEL_OFF, STUB_PARA))
        self.w("esp", sp)

    def _shim(self, name, port, value, cl, ds):
        self._frame()
        self.w("eax", trap_eax(value))
        self.w("ebx", 0x11112222)
        self.w("ecx", 0x3333_0000 | (IF | cl if value is not None else IF))
        self.w("edx", 0x44440000 | port)
        self.w("esi", 0x55556666)
        self.w("edi", 0x77778888)
        self.w("ebp", 0x9999AAAA)
        self.w16("ds", ds)
        self.w16("es", 0xBBBB)
        self.w("eflags", IF | 2)
        self.w16("cs", self.seg)
        self.w16("eip", self.sym(name))
        self.run(self.addr(name))
        if (self.r16("ss"), self.r16("esp")) != (GAME_SS, GAME_SP):
            raise EmuError("%s left the stack at %04x:%04x" % (
                name, self.r16("ss"), self.r16("esp")))
        for reg, v in (("ebx", 0x11112222), ("esi", 0x55556666),
                       ("edi", 0x77778888), ("ebp", 0x9999AAAA)):
            if self.r(reg) != v:
                raise EmuError("%s changed %s" % (name, reg))
        if self.r16("ds") != ds or self.r16("es") != 0xBBBB:
            raise EmuError("%s changed DS or ES" % name)
        if self.r("edx") != 0x44440000 | port:
            raise EmuError("%s changed DX" % name)
        return self.r8("eax"), bool(self.r("eflags") & CF)

    def mux(self, ax):
        """int 2Fh as a program calls it, through the vector table, such as
        Windows' broadcasts at its start (1605h) and end (1606h)"""
        self._frame()
        ip, cs = struct.unpack("<HH", self.mem(0x2F * 4, 4))
        sp = self.r16("esp") - 6        # the interrupt's frame: IP, CS, flags
        frame = struct.pack("<HHH", SENTINEL_OFF, STUB_PARA, IF | 2)
        self.put(GAME_SS * 16 + sp, frame)
        self.w("esp", sp)
        self.w("eax", ax)
        self.w("eflags", 2)
        self.w16("cs", cs)
        self.w16("eip", ip)
        self.run(cs * 16 + ip)
        if (self.r16("ss"), self.r16("esp")) != (GAME_SS, GAME_SP - 4):
            raise EmuError("int 2Fh left the stack at %04x:%04x" % (
                self.r16("ss"), self.r16("esp")))
        return self.r16("eax")

    def qpi_io(self, port, value=None):
        """a QPI callback: AL read, or the byte written; QPIEMU's CL"""
        self.in_callback = True
        try:
            al, _cf = self._shim("qpi_shim", port, value, 4, 0xCCCC)
        finally:
            self.in_callback = False
        if value is not None and al != value:
            raise EmuError("the QPI callback changed AL on an out")
        return al

    def emm_io(self, port, value=None):
        """EMM386 on a trapped access: the dispatch table searched where
        the program left it, its handler called at ring 0; (AL, CF), CF set
        when EMM386 is to do the access itself, as for a port it traps but
        the table doesn't have; (None, True) for a port it doesn't trap"""
        for trap in self.emm_traps:
            if not trap.get("failed") and \
                    trap["first"] <= port <= trap["last"]:
                break
        else:
            return None, True
        size, base = trap["size"], trap["ds"] * 16
        for i in range(trap["count"]):
            entry = trap["si"] + 4 * i
            if entry + 4 > size:
                raise EmuError("EMM386 faults: the dispatch table's entry "
                               "at %04Xh is beyond the %04Xh bytes it maps"
                               % (entry, size))
            p, handler = struct.unpack("<HH", self.mem(base + entry, 4))
            if p == port:
                break
        else:
            return None, True
        if handler >= size:
            raise EmuError("EMM386 faults: the handler of port %03Xh "
                           "(%04Xh) is beyond the %04Xh bytes it maps"
                           % (port, handler, size))
        if "cpu" not in trap:
            trap["cpu"] = EmmCpu(self, trap)
        return trap["cpu"].call(handler, entry, port, value, self.emm_out)

    def out(self, port, value):
        if self.path == "qpi":
            self.qpi_io(port, value)
        else:
            al, cf = self.emm_io(port, value)
            if cf:
                self.chip.outb(port, value)
                self.chained.append((port, 4, value))

    def inp(self, port):
        if self.path == "qpi":
            return self.qpi_io(port)
        al, cf = self.emm_io(port)
        if cf:
            v = self.chip.inb(port)
            self.chained.append((port, 0, v))
            return v
        return al

    # --- the Sound Blaster, as a game drives it -------------------------------
    def base(self):
        return self.data16("base")

    def dsp(self, *bytes_):
        for b in bytes_:
            self.out(self.base() + 0x0C, b)

    def dsp_read(self):
        return self.inp(self.base() + 0x0A)

    def status(self):
        return self.inp(self.base() + 0x0E)

    def mix_set(self, reg, v):
        self.out(self.base() + 4, reg)
        self.out(self.base() + 5, v)

    def mix_get(self, reg):
        self.out(self.base() + 4, reg)
        return self.inp(self.base() + 5)

    def reset(self):
        self.out(self.base() + 6, 1)
        for _ in range(3):
            self.inp(self.base() + 6)
        self.out(self.base() + 6, 0)


def desc(base, limit, access, flags):
    return struct.pack("<HHBBBB", limit & 0xFFFF, base & 0xFFFF,
                       (base >> 16) & 0xFF, access,
                       flags | ((limit >> 16) & 0xF), (base >> 24) & 0xFF)


GDT_BASE = 0x200000
STUB32 = 0x300000           # the host's 32-bit stubs
STACK32 = 0x400000          # the game's 32-bit stack
SEL_CODE0, SEL_DATA0 = 0x08, 0x10       # ring 0, flat: the trampoline
SEL_CODE32, SEL_DATA32 = 0x1B, 0x23     # ring 3, flat: the host, the stack
# ring 3, the program's segment as HDPMI gives it: 16-bit code, data with
# B set (a 32-bit client's DS and SS), the PSP; and what int 31h 0000h
# allocates
SEL_CODE16, SEL_DATA, SEL_PSP, SEL_NEW = 0x2B, 0x33, 0x3B, 0x43
VENDOR_OFF, SENTINEL32_OFF, TRAMP_OFF = 0x00, 0x10, 0x100

# EMM386's side: its flat ring 0 code and stack, the handler's CS, DS and
# ES as long as DI said, and its caller, below 64 KB in its own segment so
# that a 16-bit far call can come back to it
EMM_GDT, EMM_STUB, EMM_STACK = 0x500000, 0x600000, 0x700000
E_CODE0, E_DATA0, E_CODE16, E_DATA16, E_ES16, E_STUB = \
    0x08, 0x10, 0x18, 0x20, 0x28, 0x30


class EmmCpu(Cpu):
    """EMM386's call of a handler, at ring 0: a 16-bit far call from its own
    code, CS and DS (and ES) selectors of the program's segment as long as
    the DI of its 4A15h call, its own 32-bit stack, interrupts off and the
    direction flag set (nothing clears it). The CPU doesn't check the
    selectors' limits, so hooks do"""

    def __init__(self, machine, trap):
        super().__init__(UC_MODE_32, machine.chip, machine.memory)
        machine.sharers.append(self)
        base, size = trap["ds"] * 16, trap["size"]
        self.base, self.size = base, size
        gdt = b"\0" * 8 + desc(0, 0xFFFFF, 0x9A, 0xC0) \
            + desc(0, 0xFFFFF, 0x92, 0xC0) \
            + desc(base, size - 1, 0x9A, 0x00) \
            + desc(base, size - 1, 0x92, 0x00) \
            + desc(base, size - 1, 0x92, 0x00) \
            + desc(EMM_STUB, 0xFFFF, 0x9A, 0x40)
        self.uc.mem_map(EMM_GDT, 0x1000)
        self.uc.mem_map(EMM_STUB, 0x1000)
        self.uc.mem_map(EMM_STACK, 0x10000)
        self.put(EMM_GDT, gdt)
        self.uc.reg_write(UC_X86_REG_GDTR, (0, EMM_GDT, len(gdt) - 1, 0))
        end = base + 0xFFFF
        self.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self._beyond,
                         None, base + size, end)
        self.uc.hook_add(UC_HOOK_CODE, self._beyond_code, None,
                         base + size, end)

    def _beyond(self, uc, access, addr, size, value, _user):
        raise EmuError("EMM386's handler reached %04Xh, beyond the %04Xh "
                       "bytes of its selectors" % (addr - self.base,
                                                   self.size))

    def _beyond_code(self, uc, addr, size, _user):
        raise EmuError("EMM386's handler ran at %04Xh, beyond the %04Xh "
                       "bytes of its code selector" % (addr - self.base,
                                                       self.size))

    def _intr(self, uc, intno, _user):
        if intno == INT_SENTINEL:
            self.stop("sentinel")
            return
        raise EmuError("EMM386's handler: interrupt or fault %02Xh at "
                       "%04x:%08x" % (intno, self.r16("cs"), self.r("eip")))

    def call(self, handler, entry, port, value, out):
        """the handler at `handler` for the table's entry at `entry`: (AL,
        CF); the registers EMM386 doesn't give back must come back"""
        stub = b"\x66\x9A" + struct.pack("<HH", handler, E_CODE16) \
            + bytes([0xCD, INT_SENTINEL, 0xF4])
        self.put(EMM_STUB, stub)
        self.uc.ctl_remove_cache(EMM_STUB, EMM_STUB + len(stub))
        esp = EMM_STACK + 0xF000
        regs = {"ebx": 0x11112222, "ecx": out if value is not None else 0,
                "edx": port, "esi": 0x00000001, "edi": entry,
                "ebp": 0x9999AAAA}
        self.w16("ss", E_DATA0)
        self.w("esp", esp)
        self.w16("ds", E_DATA16)
        self.w16("es", E_ES16)
        self.w("eax", trap_eax(value))
        for reg, v in regs.items():
            self.w(reg, v)
        self.w("eflags", 0x0402)                # DF set, IF clear
        self.w16("cs", E_STUB)
        self.ports = []
        self.run(0)
        if (self.r16("ss"), self.r("esp")) != (E_DATA0, esp):
            raise EmuError("the handler left EMM386's stack at %04x:%08x"
                           % (self.r16("ss"), self.r("esp")))
        if (self.r16("ds"), self.r16("es")) != (E_DATA16, E_ES16):
            raise EmuError("the handler changed DS or ES")
        for reg, v in regs.items():
            if self.r(reg) != v:
                raise EmuError("the handler changed %s" % reg)
        return self.r8("eax"), bool(self.r("eflags") & CF)


class PMMachine(Cpu):
    """the program's protected-mode code at ring 3, under a DPMI host and
    HDPMI32i that are Python, on the machine's memory"""

    def __init__(self, machine):
        super().__init__(UC_MODE_32, machine.chip, machine.memory)
        machine.sharers.append(self)
        self.machine = machine
        self.syms = machine.syms
        self.output = ""
        self.vendor_calls = []          # (function, regs)
        self.vendor_io = []             # function 8: ("in", port, v) ...
        self.trap_fail = set()          # the function 6 calls that fail
        self.handler = None             # (CS, EIP, DS) of function 6
        self.hdpmi = True               # int 2Fh 168Ah answers
        self.dpmi_calls = []            # int 31h: (function, regs)
        self.dpmi_fail = set()          # int 31h functions that fail
        self.rmcs = None                # int 31h 0300h's structure
        self.gp = []                    # cli and sti at ring 3
        self.segs = {}                  # int 31h 0002h's selectors
        # the program's segment as the switch gives it, and its PSP's: the
        # current PSP at the switch
        base, psp = machine.load_seg * 16, machine.psp * 16
        gdt = b"\0" * 8 + desc(0, 0xFFFFF, 0x9A, 0xC0) \
            + desc(0, 0xFFFFF, 0x92, 0xC0) \
            + desc(0, 0xFFFFF, 0xFA, 0xC0) + desc(0, 0xFFFFF, 0xF2, 0xC0) \
            + desc(base, 0xFFFF, 0xFA, 0x00) + desc(base, 0xFFFF, 0xF2, 0x40) \
            + desc(psp, 0xFF, 0xF2, 0x00)
        self.next_sel = SEL_NEW         # what int 31h 0000h gives next
        self.uc.mem_map(GDT_BASE, 0x1000)
        self.uc.mem_map(STUB32, 0x1000)
        self.uc.mem_map(STACK32, 0x10000)
        self.put(GDT_BASE, gdt)
        self.uc.reg_write(UC_X86_REG_GDTR, (0, GDT_BASE, 0xFFF, 0))
        self.put(STUB32 + VENDOR_OFF, bytes([0xCD, INT_VENDOR, 0xCB]))
        self.put(STUB32 + SENTINEL32_OFF, bytes([0xCD, INT_SENTINEL, 0xF4]))
        self.ring0 = self.uc.context_save()     # the CPU at ring 0

    def sym(self, name):
        return self.syms[name]

    def addr(self, name):
        """the resident part's"""
        return self.machine.seg * 16 + self.syms[name]

    def descriptor(self, sel):
        """(base, limit, access, flags) of a selector"""
        d = self.mem(GDT_BASE + (sel & ~7), 8)
        limit = d[0] | d[1] << 8 | (d[6] & 0x0F) << 16
        base = d[2] | d[3] << 8 | d[4] << 16 | d[7] << 24
        return base, limit, d[5], d[6] & 0xF0

    def set_descriptor(self, sel, base=None, limit=None, access=None,
                       flags=None):
        old = self.descriptor(sel)
        new = [old[0] if base is None else base,
               old[1] if limit is None else limit,
               old[2] if access is None else access,
               old[3] if flags is None else flags]
        self.put(GDT_BASE + (sel & ~7), desc(*new))

    def data8(self, name):
        return self.mem(self.addr(name), 1)[0]

    def data16(self, name):
        return struct.unpack("<H", self.mem(self.addr(name), 2))[0]

    def set8(self, name, v):
        self.put(self.addr(name), bytes([v & 0xFF]))

    def set16(self, name, v):
        self.put(self.addr(name), struct.pack("<H", v))

    def set32(self, name, v):
        self.put(self.addr(name), struct.pack("<I", v))

    def regs(self):
        return {k: self.r(k) for k in REGS}

    def _intr(self, uc, intno, _user):
        if intno == 13:
            eip = self.r("eip")
            cs = self.r16("cs")
            base = self.descriptor(cs)[0]
            op = self.mem(base + eip, 1)[0]
            if op not in (0xFA, 0xFB):
                raise EmuError("privileged instruction %02X at %04x:%08x"
                               % (op, cs, eip))
            self.gp.append(op)
            self.w("eip", eip + 1)
        elif intno == INT_SENTINEL:
            self.stop("sentinel")
        elif intno == INT_VENDOR:
            self._vendor()
        elif intno == 0x31:
            self._dpmi()
        elif intno == 0x2F:
            self._mux()
        elif intno == 0x21:
            self._dos()
        else:
            raise EmuError("int %02Xh at %04x:%08x" % (
                intno, self.r16("cs"), self.r("eip")))

    def _vendor(self):
        ax = self.r16("eax")
        self.vendor_calls.append((ax, self.regs()))
        if ax == 8:
            port, out = self.r16("edx"), (self.r("ecx") >> 8) & 0xFF
            if out:
                v = self.r8("ecx")
                self.vendor_io.append(("out", port, v))
                self.chip.outb(port, v)
            else:
                v = self.chip.inb(port)
                self.vendor_io.append(("in", port, v))
                self.w8("eax", v)
            self.carry(False)
        elif ax == 6:
            n = len([c for c in self.vendor_calls if c[0] == 6]) - 1
            if n in self.trap_fail:
                self.carry(True)
                return
            self.handler = (self.r16("ecx"), self.r("edx"), self.r16("ebx"))
            self.w("eax", 0x1000 + 0x10 * n)
            self.carry(False)
        elif ax == 7:
            self.carry(False)
        else:
            self.carry(True)

    def _new_sel(self):
        sel = self.next_sel
        if sel >= 0x1000:
            raise EmuError("no descriptors left")
        self.next_sel += 8
        self.put(GDT_BASE + (sel & ~7), bytes(8))
        return sel

    def _dpmi(self):
        """the descriptor functions (0000h, 0002h, 0007h-0009h), which check
        what a strict host checks, and 0300h's int 21h: a block freed, or the
        TSR, from which it doesn't come back"""
        ax = self.r16("eax")
        self.dpmi_calls.append((ax, self.regs()))
        self.carry(False)
        bx = self.r16("ebx")
        cxdx = (self.r16("ecx") << 16) | self.r16("edx")
        if ax in self.dpmi_fail:
            self.w16("eax", 0x8011)
            self.carry(True)
        elif ax == 0x0000:
            if self.r16("ecx") != 1:
                raise EmuError("int 31h 0000h for %d descriptors"
                               % self.r16("ecx"))
            self.w16("eax", self._new_sel())
        elif ax == 0x0002:
            if bx not in self.segs:
                self.segs[bx] = self._new_sel()
                self.set_descriptor(self.segs[bx], bx * 16, 0xFFFF, 0xF2, 0)
            self.w16("eax", self.segs[bx])
        elif ax in (0x0007, 0x0008, 0x0009):
            if not SEL_NEW <= bx < self.next_sel or bx & 7 != 3 \
                    or bx in self.segs.values():
                self.w16("eax", 0x8022)         # not the client's to change
                self.carry(True)
            elif ax == 0x0007:
                self.set_descriptor(bx, base=cxdx)
            elif ax == 0x0008:
                if cxdx > 0xFFFFF:
                    raise EmuError("int 31h 0008h: a limit over 1 MB")
                self.set_descriptor(bx, limit=cxdx)
            else:
                cl, ch = self.r8("ecx"), (self.r16("ecx") >> 8) & 0xFF
                if cl & 0xF0 != 0xF0 or ch & 0x20:
                    self.w16("eax", 0x8021)     # present, ring 3, code or
                    self.carry(True)            # data, and nothing reserved
                else:
                    self.set_descriptor(bx, access=cl, flags=ch & 0xD0)
        elif ax == 0x0300:
            es, edi = self.r16("es"), self.r("edi")
            if es != SEL_DATA or edi >> 16 or bx != 0x21:
                raise EmuError("int 31h 0300h with ES:EDI %04x:%08x, BX %04X"
                               % (es, edi, bx))
            at = self.descriptor(es)[0] + edi
            rmcs = self.mem(at, 0x32)
            fn = rmcs[0x1D]
            if rmcs[0x2E:0x32] != b"\0\0\0\0":
                raise EmuError("int 31h 0300h with a real-mode stack")
            if fn == 0x49:
                self.machine.free(int.from_bytes(rmcs[0x22:0x24], "little"))
                self.put(at + 0x20, b"\x02\x00")        # flags: CF clear
            elif fn == 0x31:
                self.rmcs = rmcs
                self.stop("rmcs")
            else:
                raise EmuError("int 31h 0300h: int 21h AH=%02Xh" % fn)
        else:
            raise EmuError("int 31h AX=%04X" % ax)

    def _mux(self):
        ax = self.r16("eax")
        if ax == 0x168A:
            name = self.cstr(self.descriptor(self.r16("ds"))[0]
                             + self.r("esi"))
            if name == "HDPMI" and self.hdpmi:
                self.w8("eax", 0)
                self.w16("es", SEL_CODE32)
                self.w("edi", STUB32 + VENDOR_OFF)
        else:
            raise EmuError("int 2Fh AX=%04X" % ax)

    def _dos(self):
        ax = self.r16("eax")
        if ax >> 8 == 0x09:
            if self.r16("ds") != SEL_DATA or self.r("edx") >> 16:
                raise EmuError("int 21h 09h with DS:EDX %04x:%08x"
                               % (self.r16("ds"), self.r("edx")))
            addr = self.descriptor(SEL_DATA)[0] + self.r("edx")
            s = b""
            while self.mem(addr + len(s), 1) != b"$":
                s += self.mem(addr + len(s), 1)
            self.output += s.decode("latin-1")
        else:
            raise EmuError("int 21h AX=%04X" % ax)

    def _ring3(self, ss, esp, cs, eip):
        """to ring 3 through a far return from ring 0"""
        code = b""
        for v in (ss, esp, cs, eip):
            code += b"\x68" + struct.pack("<I", v)
        code += b"\xCB"
        self.put(STUB32 + TRAMP_OFF, code)
        self.w16("cs", SEL_CODE0)
        self.w16("ss", SEL_DATA0)
        self.w("esp", STACK32 + 0xFF00)
        self.w("eflags", 0x3002)                # IOPL 3: cli and sti run
        return self.run(STUB32 + TRAMP_OFF)

    def fake_install(self):
        """what the installer leaves under HDPMI32i, for a program that was
        installed without it: the selectors, the vendor entry, the trap"""
        self.set16("pm_data", SEL_DATA)
        self.set32("vendor", STUB32 + VENDOR_OFF)
        self.set16("vendor_sel", SEL_CODE32)
        self.handler = (SEL_CODE16, self.sym("hdpmi_shim"), SEL_DATA)

    def hdpmi_io(self, port, value=None):
        """HDPMI32i's call of the handler function 6 registered: AL read, or
        the byte written"""
        cs, eip, ds = self.handler
        self.uc.context_restore(self.ring0)     # back from ring 3
        top = STACK32 + 0x8000
        self.put(top - 8, struct.pack("<II", STUB32 + SENTINEL32_OFF, SEL_CODE32))
        self.w("eax", trap_eax(value))
        self.w("ebx", 0x11112222)
        self.w("ecx", 1 if value is not None else 0)
        self.w("edx", port)
        self.w("esi", 0x55556666)
        self.w("edi", 0x77778888)
        self.w("ebp", 0x9999AAAA)
        self.w16("ds", ds)
        self.w16("es", ds)
        self.ports = []
        self._ring3(SEL_DATA32, top - 8, cs, eip)
        if (self.r16("ss"), self.r("esp")) != (SEL_DATA32, top):
            raise EmuError("the handler left the stack at %04x:%08x" % (
                self.r16("ss"), self.r("esp")))
        for reg, v in (("ebx", 0x11112222), ("esi", 0x55556666),
                       ("ebp", 0x9999AAAA), ("edx", port),
                       ("ecx", 1 if value is not None else 0)):
            if self.r(reg) != v:
                raise EmuError("the handler changed %s" % reg)
        if self.ports:
            raise EmuError("the handler touched the ports itself: %r"
                           % self.ports)
        if value is not None and self.r("eax") != trap_eax(value):
            raise EmuError("the handler changed EAX on an out")
        return self.r8("eax")

    def install(self, machine):
        """the installer from the DPMI switch on, as HDPMI returns to a
        32-bit client: CS a 16-bit code selector of the program's segment,
        DS and SS its data selector with B set, ES the PSP's. What it
        leaves for DOS, the real-mode call's structure; DOS keeps what it
        asks for"""
        ax, es, cs, ip, sp = machine.dpmi_call
        if ax != 1 or cs != machine.load_seg:
            raise EmuError("the DPMI entry was called with AX=%d from %04x"
                           % (ax, cs))
        self.uc.context_restore(self.ring0)
        self.w16("ds", SEL_DATA)
        self.w16("es", SEL_PSP)
        self._ring3(SEL_DATA, sp, SEL_CODE16, ip)
        if self.rmcs is not None:
            machine.tsr_psp = machine.psp
            machine.resident(int.from_bytes(self.rmcs[0x14:0x16], "little"))
        return self.rmcs
