# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Run esssb16.com in a CPU emulator against a model of the ES1869.

DOS, the memory manager and the DPMI host are Python behind the interrupt
hook: int 21h (the program's output, the device opens and IOCTLs of QEMM
and EMM386, the memory block list, the UMB link, memory, and the exits),
int 2Fh (Windows, QPIEMU's entry, EMM386's port traps, the DPMI host), and
the QPI entry, the callback before the program's and the DPMI entry, which
are stubs that interrupt. Every IN and OUT goes to vxdemu's FakeES1869,
which has the mixer, the DSP and the PnP registers.

Machine runs the installer the way COMMAND.COM would, then calls the
resident shims the way the memory manager does: qpi_io() is a QPI callback
in V86 mode with a far frame, emm_io() EMM386's handler. PMMachine runs the
32-bit protected-mode parts at ring 3: HDPMI32i's handler (hdpmi_io()),
and the installer's code from the DPMI switch on (install()).

Needs the unicorn package.
"""

import os
import struct
import sys

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_MODE_32, UC_HOOK_INTR,
                     UC_HOOK_INSN)
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_EAX,
                               UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                               UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP,
                               UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_CS,
                               UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
                               UC_X86_REG_EFLAGS, UC_X86_REG_GDTR)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from vxdemu import FakeES1869  # noqa: E402


class Chip(FakeES1869):
    """FakeES1869 with Audio 1's interrupt bit (Audio_Base+Ch bit 0, cleared
    by a read of +Eh), the chip's own commands with an operand, E0h's
    answer, and the PnP registers the installer reads"""

    SB_OPERANDS = {**FakeES1869.SB_OPERANDS, 0x41: 1, 0x42: 1, 0xCF: 1}

    def __init__(self, base=0x220, cfg=0x250, irq=5, dma8=1):
        super().__init__(base, cfg)
        self.a1_irq = False
        self.pnp.update({(1, 0x70): irq, (1, 0x72): 0, (1, 0x74): dma8,
                         (1, 0x75): 4})

    def _dsp(self, v):
        n = len(self.commands)
        super()._dsp(v)
        if len(self.commands) > n and self.commands[-1][0] == 0xE0:
            self.out.append(~self.commands[-1][1] & 0xFF)   # E0h: the inverse

    def _inb(self, port):
        v = super()._inb(port)
        if port == self.base + 0x0C and self.a1_irq:
            v |= 0x01
        elif port == self.base + 0x0E:
            self.a1_irq = False
        return v


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
GAME_SS, GAME_SP = 0x5000, 0x1000       # the stack a trapped access comes with
STUB_PARA = 0x6000          # the stubs that interrupt
HOST_PARA = 0x7000          # the DPMI host's data block (int 21h 48h)
QPI_OFF, OLD_OFF, SENTINEL_OFF, DPMI_OFF = 0x00, 0x10, 0x20, 0x30
INT_QPI, INT_OLD, INT_SENTINEL, INT_DPMI, INT_VENDOR = 0xE0, 0xE1, 0xE2, 0xE4, 0xE5
CF = 0x0001
IF = 0x0200

REGS = {"eax": UC_X86_REG_EAX, "ebx": UC_X86_REG_EBX, "ecx": UC_X86_REG_ECX,
        "edx": UC_X86_REG_EDX, "esi": UC_X86_REG_ESI, "edi": UC_X86_REG_EDI,
        "ebp": UC_X86_REG_EBP, "esp": UC_X86_REG_ESP, "eip": UC_X86_REG_EIP,
        "cs": UC_X86_REG_CS, "ds": UC_X86_REG_DS, "es": UC_X86_REG_ES,
        "ss": UC_X86_REG_SS, "eflags": UC_X86_REG_EFLAGS}


class EmuError(Exception):
    pass


class Cpu:
    """register access by name, and the port hooks"""

    def __init__(self, mode, chip):
        self.uc = Uc(UC_ARCH_X86, mode)
        self.chip = chip
        self.ports = []                 # every IN and OUT the code did itself
        self.stopped = None
        self.uc.mem_map(0, 0x100000)
        self.uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        self.uc.hook_add(UC_HOOK_INTR, self._intr)

    def _in(self, uc, port, size, _user):
        v = self.chip.inb(port)
        self.ports.append(("in", port, v))
        return v

    def _out(self, uc, port, size, value, _user):
        self.ports.append(("out", port, value & 0xFF))
        self.chip.outb(port, value & 0xFF)

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
    """DOS, a memory manager and the chip around the real-mode program"""

    def __init__(self, image, syms, chip=None, env="BLASTER=A220 I5 D1 H5 P330 T6"):
        super().__init__(UC_MODE_16, chip or Chip())
        self.image, self.syms = image, syms
        self.output = ""                # what the program printed
        self.exit = None                # ("tsr", paragraphs) or ("exit", code)
        self.qemm = False               # the QEMM386$ device answers
        self.qpiemu = False             # int 2Fh 1684h/4354h answers
        self.qpi_version = 0x0703
        self.qpi_fail = set()           # QPI functions that fail
        self.qpi_calls = []             # (function, dx, es:di)
        self.qpi_trapped = set()        # ports trapped through 1A09h
        self.qpi_callback = (0, 0)      # the I/O callback: seg, off
        self.qpi_accesses = []          # 1A00h/1A01h: ("in", port, v) ...
        self.chained = []               # calls to the callback before ours
        self.emm386 = None              # (major, minor) of EMMXXXX0
        self.emm_fail = set()           # the 4A15h calls that fail (0, 1, ...)
        self.emm_traps = []             # 4A15h: dicts
        self.dpmi = False               # int 2Fh 1687h answers
        self.dpmi_paras = 0x20          # the host's data block
        self.dpmi_call = None           # (ax, es, cs, ip, sp) at the entry
        self.windows = 0                # int 2Fh 1600h's AL
        self.umb_link = 0
        self.umb_links = []
        self.freed, self.resized, self.allocated = [], [], []
        self.alloc_fail = False
        self.path = "qpi"               # how in() and out() reach the front
        self.load(env)

    # --- the program and its surroundings -----------------------------------
    def load(self, env):
        psp = PSP_PARA * 16
        self.put(psp, b"\xCD\x20" + struct.pack("<H", 0xA000))
        self.put(psp + 0x2C, struct.pack("<H", ENV_PARA))
        self.put(psp + 0x100, self.image)
        e = env.encode("latin-1") + b"\0" if env else b""
        self.put(ENV_PARA * 16, e + b"\0" + struct.pack("<H", 1) +
                 b"C:\\ESSSB16.COM\0")
        # the memory blocks: the program's (64 KB), a free one, the last
        self.put(LOL_PARA * 16, struct.pack("<H", MCB_PARA))
        self.mcb(MCB_PARA, b"M", PSP_PARA, 0x1000)
        self.mcb(MCB_PARA + 0x1001, b"M", 0, 0xFFE)
        self.mcb(COPY_PARA - 1, b"Z", 0, 0x1000)
        # the stubs: the QPI entry, the callback before ours, the sentinel
        # the shims return to, the DPMI entry
        s = STUB_PARA * 16
        self.put(s + QPI_OFF, bytes([0xCD, INT_QPI, 0xCB]))
        self.put(s + OLD_OFF, bytes([0xCD, INT_OLD, 0xCB]))
        self.put(s + SENTINEL_OFF, bytes([0xCD, INT_SENTINEL, 0xF4]))
        self.put(s + DPMI_OFF, bytes([0xCD, INT_DPMI, 0xCB]))

    def mcb(self, para, kind, owner, size):
        self.put(para * 16, kind + struct.pack("<HH", owner, size) + b"\0" * 11)

    def tail(self, text):
        t = text.encode("latin-1")
        self.put(PSP_PARA * 16 + 0x80, bytes([len(t)]) + t + b"\r")

    def copy_resident(self):
        """a copy of the program at COPY_PARA, owned by its own PSP"""
        self.mcb(COPY_PARA - 1, b"Z", COPY_PARA, 0x1000)
        self.put(COPY_PARA * 16 + 0x100, self.image)

    def sym(self, name):
        return self.syms[name]

    def addr(self, name):
        return PSP_PARA * 16 + self.syms[name]

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
        elif intno == INT_DPMI:
            ss, sp = self.r16("ss"), self.r16("esp")
            ip, cs = struct.unpack("<HH", self.mem(ss * 16 + sp, 4))
            self.dpmi_call = (self.r16("eax"), self.r16("es"), cs, ip, sp + 4)
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
            elif name == "EMMXXXX0" and self.emm386:
                self.w16("eax", 6)
                self.carry(False)
            else:
                self.w16("eax", 2)
                self.carry(True)
        elif ax == 0x4402:
            bx, cx = self.r16("ebx"), self.r16("ecx")
            if bx == 5:
                data = struct.pack("<HH", QPI_OFF, STUB_PARA)
            else:
                data = bytes(self.emm386)
            self.put(ds * 16 + dx, data[:cx])
            self.w16("eax", len(data[:cx]))
            self.carry(False)
        elif ah == 0x3E:
            self.carry(False)
        elif ah == 0x52:
            self.w16("es", LOL_PARA)
            self.w16("ebx", 2)
        elif ax == 0x5802:
            self.w8("eax", self.umb_link)
            self.carry(False)
        elif ax == 0x5803:
            self.umb_link = self.r16("ebx")
            self.umb_links.append(self.umb_link)
            self.carry(False)
        elif ah == 0x49:
            self.freed.append(self.r16("es"))
            self.carry(False)
        elif ah == 0x4A:
            self.resized.append((self.r16("es"), self.r16("ebx")))
            self.carry(False)
        elif ah == 0x48:
            self.allocated.append(self.r16("ebx"))
            if self.alloc_fail:
                self.w16("eax", 8)
                self.carry(True)
            else:
                self.w16("eax", HOST_PARA)
                self.carry(False)
        elif ah == 0x31:
            self.exit = ("tsr", dx)
            self.stop("tsr")
        elif ah == 0x4C:
            self.exit = ("exit", ax & 0xFF)
            self.stop("exit")
        else:
            raise EmuError("int 21h AX=%04X" % ax)

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
            table = [struct.unpack("<HH", self.mem(ds * 16 + si + 4 * i, 4))
                     for i in range(cx)]
            call = {"first": edx & 0xFFFF, "last": edx >> 16, "count": cx,
                    "table": table, "size": self.r16("edi")}
            self.emm_traps.append(call)
            if len(self.emm_traps) - 1 in self.emm_fail:
                call["failed"] = True
                return
            self.carry(False)
        elif ax == 0x1687:
            if self.dpmi:
                self.w16("eax", 0)
                self.w16("ebx", 1)
                self.w8("ecx", 4)
                self.w16("edx", 0x005A)
                self.w16("esi", self.dpmi_paras)
                self.w16("es", STUB_PARA)
                self.w16("edi", DPMI_OFF)
        else:
            raise EmuError("int 2Fh AX=%04X" % ax)

    def _qpi(self):
        ax, dx = self.r16("eax"), self.r16("edx")
        fn = 0x0300 if ax >> 8 == 3 else ax
        self.qpi_calls.append((fn, dx, (self.r16("es"), self.r16("edi"))))
        if fn in self.qpi_fail:
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
                dpmi=False, windows=0, loaded=False, old_callback=True):
        self.qemm, self.qpiemu, self.emm386 = qemm, qpiemu, emm386
        self.dpmi, self.windows = dpmi, windows
        self.qpi_callback = (STUB_PARA, OLD_OFF) if old_callback else (0, 0)
        if loaded:
            self.copy_resident()
        self.tail(tail)
        for name in ("cs", "ds", "es", "ss"):
            self.w16(name, PSP_PARA)
        self.w("esp", 0xFFFE)
        self.w("eflags", IF | 2)
        self.w16("eip", 0x100)
        return self.run(PSP_PARA * 16 + 0x100)

    # --- the shims -----------------------------------------------------------
    def _frame(self):
        """the far frame of a call from the memory manager: the sentinel"""
        self.w16("ss", GAME_SS)
        sp = GAME_SP - 4
        self.put(GAME_SS * 16 + sp, struct.pack("<HH", SENTINEL_OFF, STUB_PARA))
        self.w("esp", sp)

    def _shim(self, name, port, value, cl, ds):
        self._frame()
        self.w("eax", 0x5A5A0000 | (value if value is not None else 0xA5A5))
        self.w("ebx", 0x11112222)
        self.w("ecx", 0x3333_0000 | (IF | cl if value is not None else IF))
        self.w("edx", 0x44440000 | port)
        self.w("esi", 0x55556666)
        self.w("edi", 0x77778888)
        self.w("ebp", 0x9999AAAA)
        self.w16("ds", ds)
        self.w16("es", 0xBBBB)
        self.w("eflags", IF | 2)
        self.w16("cs", PSP_PARA)
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

    def qpi_io(self, port, value=None):
        """a QPI callback: AL read, or the byte written; QPIEMU's CL"""
        al, _cf = self._shim("qpi_shim", port, value, 4, 0xCCCC)
        if value is not None and al != value:
            raise EmuError("the QPI callback changed AL on an out")
        return al

    def emm_io(self, port, value=None):
        """EMM386's handler: (AL, CF), CF set when EMM386 is to do it"""
        return self._shim("emm_shim", port, value, 4, PSP_PARA)

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
SEL_CODE16, SEL_DATA = 0x2B, 0x33       # ring 3, the program's segment
SEL_CLIENT, SEL_STACK = 0x3B, 0x43      # ring 3, its 32-bit code and stack
SEL_NEW = 0x4B                          # what int 31h 0000h allocates
VENDOR_OFF, SENTINEL32_OFF, TRAMP_OFF = 0x00, 0x10, 0x100


class PMMachine(Cpu):
    """the program's protected-mode code at ring 3, under a DPMI host and
    HDPMI32i that are Python"""

    def __init__(self, machine):
        super().__init__(UC_MODE_32, machine.chip)
        self.syms = machine.syms
        self.put(0, machine.mem(0, 0x100000))
        self.output = ""
        self.vendor_calls = []          # (function, regs)
        self.vendor_io = []             # function 8: ("in", port, v) ...
        self.hdpmi = True               # int 2Fh 168Ah answers
        self.dpmi_calls = []            # int 31h: (function, regs)
        self.rmcs = None                # int 31h 0300h's structure
        self.gp = []                    # cli and sti at ring 3
        base = PSP_PARA * 16
        gdt = b"\0" * 8 + desc(0, 0xFFFFF, 0x9A, 0xC0) \
            + desc(0, 0xFFFFF, 0x92, 0xC0) \
            + desc(0, 0xFFFFF, 0xFA, 0xC0) + desc(0, 0xFFFFF, 0xF2, 0xC0) \
            + desc(base, 0xFFFF, 0xFA, 0x00) + desc(base, 0xFFFF, 0xF2, 0x00) \
            + desc(base, 0xFFFF, 0xFA, 0x40) + desc(base, 0xFFFF, 0xF2, 0x40) \
            + desc(0, 0, 0, 0)
        self.uc.mem_map(GDT_BASE, 0x1000)
        self.uc.mem_map(STUB32, 0x1000)
        self.uc.mem_map(STACK32, 0x10000)
        self.put(GDT_BASE, gdt)
        self.uc.reg_write(UC_X86_REG_GDTR, (0, GDT_BASE, len(gdt) - 1, 0))
        self.put(STUB32 + VENDOR_OFF, bytes([0xCD, INT_VENDOR, 0xCB]))
        self.put(STUB32 + SENTINEL32_OFF, bytes([0xCD, INT_SENTINEL, 0xF4]))
        self.ring0 = self.uc.context_save()     # the CPU at ring 0

    def sym(self, name):
        return self.syms[name]

    def addr(self, name):
        return PSP_PARA * 16 + self.syms[name]

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
            base = PSP_PARA * 16 if cs in (SEL_CODE16, SEL_CLIENT) else 0
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
            self.w("eax", 0x1000 + 0x10 * len(self.vendor_calls))
            self.carry(False)
        else:
            self.carry(True)

    def _dpmi(self):
        ax = self.r16("eax")
        self.dpmi_calls.append((ax, self.regs()))
        self.carry(False)
        if ax == 0x0000:
            self.w16("eax", SEL_NEW)
        elif ax in (0x0007, 0x0008, 0x0009):
            pass
        elif ax == 0x0300:
            es, edi = self.r16("es"), self.r("edi")
            base = PSP_PARA * 16 if es == SEL_DATA else 0
            self.rmcs = self.mem(base + edi, 0x32)
            self.stop("rmcs")
        else:
            raise EmuError("int 31h AX=%04X" % ax)

    def _mux(self):
        ax = self.r16("eax")
        if ax == 0x168A:
            name = self.cstr(PSP_PARA * 16 + self.r("esi"))
            if name == "HDPMI" and self.hdpmi:
                self.w8("eax", 0)
                self.w16("es", SEL_CODE32)
                self.w("edi", STUB32 + VENDOR_OFF)
        else:
            raise EmuError("int 2Fh AX=%04X" % ax)

    def _dos(self):
        ax = self.r16("eax")
        if ax >> 8 == 0x09:
            addr = PSP_PARA * 16 + self.r("edx")
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

    def hdpmi_io(self, port, value=None):
        """HDPMI32i's call of the handler: AL read, or the byte written"""
        self.uc.context_restore(self.ring0)     # back from ring 3
        self.set16("pm_data", SEL_DATA)
        self.set32("vendor", STUB32 + VENDOR_OFF)
        self.set16("vendor_sel", SEL_CODE32)
        top = STACK32 + 0x8000
        self.put(top - 8, struct.pack("<II", STUB32 + SENTINEL32_OFF, SEL_CODE32))
        self.w("eax", 0x5A5A0000 | (value if value is not None else 0xA5A5))
        self.w("ebx", 0x11112222)
        self.w("ecx", 1 if value is not None else 0)
        self.w("edx", port)
        self.w("esi", 0x55556666)
        self.w("edi", 0x77778888)
        self.w("ebp", 0x9999AAAA)
        self.w16("ds", SEL_DATA)
        self.w16("es", SEL_DATA)
        self.ports = []
        self._ring3(SEL_DATA32, top - 8, SEL_CODE16, self.sym("hdpmi_shim"))
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
        if value is not None and self.r("eax") != 0x5A5A0000 | value:
            raise EmuError("the handler changed EAX on an out")
        return self.r8("eax")

    def install(self, machine):
        """the installer from the DPMI switch on: what it leaves for DOS"""
        ax, es, cs, ip, sp = machine.dpmi_call
        if ax != 1 or cs != PSP_PARA:
            raise EmuError("the DPMI entry was called with AX=%d from %04x"
                           % (ax, cs))
        self.uc.context_restore(self.ring0)
        self.w16("ds", SEL_DATA)
        self.w16("es", SEL_DATA)
        self._ring3(SEL_STACK, sp, SEL_CLIENT, ip)
        return self.rmcs
