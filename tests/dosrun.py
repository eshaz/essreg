# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Run a DOS program (an MZ .exe) in a CPU emulator in front of a card.

The program runs in real mode with the DOS and BIOS calls an Open Watcom
program makes as Python behind the interrupt hook: its console output, a
few files in memory, memory blocks, the date and time, vectors, Windows'
absence, the BIOS keyboard and the BIOS tick. The keyboard has the keys
given, or asks a person: keys(output), the console's text so far, gives
the key the person has pressed by now, or None.
The PIT's channel 0 counts from the card's clock in the mode the program
sets, the PIC holds the card's interrupt until the program unmasks it and
sends its EOI, and every other port is the card's (sbgame's cards, with
the DMA controllers behind them). An interrupt of the card goes into the
program as the CPU would: at an instruction boundary with IF set, through
the vector the program set.

With a TSRCard the program shares the first megabyte of esssb16.com's
machine, as a game does: the DMA reads its buffers there, its MCB chain
includes the TSR's block, and the trapped ports run the TSR's shims on
their own CPU. With `vxd`, the program runs in a Windows 98 DOS box, as
far as int 2Fh tells, where ES1869.VXD's API answers functions 0001 and
0400 (VXD_API.md) with vxd["adi"] and vxd["info"], AX, BX and CX, or None
for ESS's driver, which fails 0400. With `umb`, (image, paragraphs), a
program is resident in upper memory, as LH loads one: its block joins the
MCB chain while the program links the UMBs (int 21h 5803h).

usage: from dosrun import Dos; Dos(exe_bytes, card).run("/A")
"""

import datetime
import os
import struct
import sys

from unicorn import UC_MODE_16

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from tsremu import Cpu, BufferMemory, EmuError  # noqa: E402

IF = 0x0200
ZF = 0x0040
PSP = 0x2001                # the program's PSP, after the TSR's block
ENV = 0x1F00                # its environment
MCB = 0x2000                # its memory block's MCB
SIZE = 0x17FE               # its block, in paragraphs
ALLOC = 0x4000              # the block int 21h 48h gives
LOL = 0x0090
PIT_HZ = 1193182
PIT_US = 4                  # a PIT access, with the loop around it
KEY_US = 20                 # the BIOS's look at the keyboard
VXD_PARA = 0x9F00           # ES1869.VXD's API entry: int E6h, retf
UMB_PARA = 0xC801           # the program in upper memory, its MCB before
INT_VXD = 0xE6
ADI_DEVNODE = 0x55


class Dos(Cpu):
    """the program, DOS and the BIOS; `files` are the files it wrote"""

    def __init__(self, exe, card, env="BLASTER=A220 I5 D1 H5 P330 T6",
                 keys="", memory=None, vxd=None, umb=None):
        tsr = getattr(card, "m", None)
        if memory is None and tsr is not None and hasattr(tsr, "memory"):
            memory = tsr.memory     # the TSR's machine: one first megabyte
        super().__init__(UC_MODE_16, None, memory)
        self.card = card
        self.person = keys if callable(keys) else None
        self.keys = [] if self.person else list(keys)
        self.output = ""
        self.files, self.handles = {}, {}
        self.pit_mode, self.pit_latch = 3, None
        self.pit_lsb = True
        self.pic_mask = [0xB8, 0xFF]    # timer, keyboard, cascade on
        self.in_service = False
        self.pending = False
        self.exit = None
        self.allocated = []
        self.tsr = tsr
        self.vxd, self.vxd_calls = vxd, []
        self.umb, self.umb_linked, self.umb_calls = umb, False, []
        card.isr = self._raise
        if tsr is None:
            self.card_memory(card)
        self._load(exe, env)

    def card_memory(self, card):
        """the card's DMA reads the program's memory"""
        mem = BufferMemory(self.memory)
        if hasattr(card, "dma"):
            card.dma.memory = mem
            card.memory = mem

    def _load(self, exe, env):
        (_sig, last, pages, nrel, hdr, minalloc, _max, ss, sp, _csum, ip, cs,
         lfarlc) = struct.unpack("<2sHHHHHHHHHHHH", exe[:26])
        size = (pages - 1) * 512 + (last or 512)
        image = bytearray(exe[hdr * 16:size])
        seg = PSP + 0x10
        for i in range(nrel):
            off, rseg = struct.unpack_from("<HH", exe, lfarlc + 4 * i)
            at = rseg * 16 + off
            v = struct.unpack_from("<H", image, at)[0]
            struct.pack_into("<H", image, at, (v + seg) & 0xFFFF)
        self.put(seg * 16, bytes(image))
        bss = seg * 16 + len(image)
        self.put(bss, bytes(min(minalloc * 16, 0x30000)))
        # the PSP, the environment, the memory blocks
        self.put(PSP * 16, b"\xCD\x20" + struct.pack("<H", PSP + SIZE))
        self.put(PSP * 16 + 0x2C, struct.pack("<H", ENV))
        e = env.encode("latin-1") + b"\0" if env else b""
        self.put(ENV * 16, e + b"\0" + struct.pack("<H", 1) +
                 b"C:\\SB16TEST.EXE\0")
        if self.tsr is None:
            self.put(LOL * 16, struct.pack("<H", MCB))
        self.mcb(MCB, b"M", PSP, SIZE)
        self.mcb(MCB + SIZE + 1, b"M", 0, ALLOC - MCB - SIZE - 3)
        self.mcb(ALLOC - 1, b"Z", 0, 0x1000)
        self.w16("ds", PSP)
        self.w16("es", PSP)
        self.w16("ss", seg + ss)
        self.w16("esp", sp)
        self.w16("cs", seg + cs)
        self.w16("eip", ip)
        self.w("eflags", IF | 2)
        self.bios_ticks(0)
        if self.vxd is not None:
            self.put(VXD_PARA * 16, bytes((0xCD, INT_VXD, 0xCB)))
        if self.umb is not None:
            image, paras = self.umb
            self.mcb(UMB_PARA - 1, b"M", UMB_PARA, paras)
            self.put(UMB_PARA * 16, image)
            self.mcb(UMB_PARA + paras, b"Z", 0, 0x100)

    def link_umbs(self, on):
        """the UMBs in the chain: the last block of conventional memory
        'M', followed by the system's block over the adapters' memory up to
        the first UMB's MCB; else 'Z' again"""
        last = ALLOC - 1
        kind = b"M" if on else b"Z"
        self.put(last * 16, kind)
        if on:
            hole = last + 0x1000 + 1
            self.mcb(hole, b"M", 0x0008, UMB_PARA - 1 - hole - 1)
        self.umb_linked = on

    def mcb(self, para, kind, owner, size):
        self.put(para * 16, kind + struct.pack("<HH", owner, size) +
                 b"\0" * 11)

    def tail(self, text):
        t = text.encode("latin-1")
        self.put(PSP * 16 + 0x80, bytes([len(t)]) + t + b"\r")

    # --- time and the card ---------------------------------------------------
    def now_us(self):
        c = self.card
        return c.clock.us if hasattr(c, "clock") else c.chip.clock.us

    def pit_count(self):
        clocks = int(self.now_us() * PIT_HZ / 1e6)
        self.bios_ticks(clocks >> 16)
        if self.pit_mode == 2:
            return (-clocks) & 0xFFFF
        return (-2 * clocks) & 0xFFFF

    def bios_ticks(self, n):
        self.put(0x46C, struct.pack("<I", n & 0xFFFFFFFF))

    def _raise(self, card):
        self.pending = True

    def deliverable(self):
        irq = self.irq
        masked = (self.pic_mask[1] >> (irq - 8) & 1) or \
            (self.pic_mask[0] >> 2 & 1) if irq >= 8 else \
            self.pic_mask[0] >> irq & 1
        return self.pending and not self.in_service and not masked and \
            self.r("eflags") & IF

    # a pending interrupt waits for IF: the run goes on a few instructions
    # at a time until the CPU would take it
    def _after_io(self):
        if self.pending and not self.in_service:
            self.uc.emu_stop()

    def _in(self, uc, port, size, _user):
        if 0x40 <= port <= 0x43:
            self.card.wait(PIT_US)
            if port == 0x40:
                if self.pit_latch is None:
                    self.pit_latch = self.pit_count()
                v = self.pit_latch & 0xFF if self.pit_lsb else \
                    self.pit_latch >> 8
                if not self.pit_lsb:
                    self.pit_latch = None
                self.pit_lsb = not self.pit_lsb
            else:
                v = 0xFF
        elif port in (0x21, 0xA1):
            v = self.pic_mask[port == 0xA1]
        elif port in (0x20, 0xA0):
            v = 0
        else:
            v = self.card.inp(port)
        self._after_io()
        return v

    def _out(self, uc, port, size, value, _user):
        value &= 0xFF
        if 0x40 <= port <= 0x43:
            self.card.wait(PIT_US)
            if port == 0x43 and (value & 0xC0) == 0:
                if (value & 0x30) == 0:         # channel 0's latch
                    self.pit_latch, self.pit_lsb = self.pit_count(), True
                else:                           # its mode
                    self.pit_mode = (value >> 1) & 7
                    self.pit_latch, self.pit_lsb = None, True
        elif port in (0x21, 0xA1):
            self.pic_mask[port == 0xA1] = value
        elif port in (0x20, 0xA0):
            if value == 0x20 and port == 0x20:
                self.in_service = False
        else:
            self.card.out(port, value)
        self._after_io()

    def _inject(self):
        """the card's interrupt: FLAGS, CS and IP pushed, IF and TF clear,
        CS:IP from the vector"""
        self.pending = False
        self.in_service = True
        vect = 0x70 + self.irq - 8 if self.irq >= 8 else 8 + self.irq
        ip, cs = struct.unpack("<HH", self.mem(vect * 4, 4))
        ss, sp = self.r16("ss"), self.r16("esp")
        flags = self.r("eflags") & 0xFFFF
        frame = struct.pack("<HHH", self.r16("eip"), self.r16("cs"), flags)
        sp = (sp - 6) & 0xFFFF
        self.put(ss * 16 + sp, frame)
        self.w16("esp", sp)
        self.w("eflags", self.r("eflags") & ~(IF | 0x0100))
        self.w16("cs", cs)
        self.w16("eip", ip)

    # --- the run -------------------------------------------------------------
    # every run has a count: unicorn adds its counting hook at the first and
    # removes it at a run without one, and blocks translated in between
    # would then run uncounted
    def run(self, tail="", irq=5):
        """the program until it exits; its exit code"""
        self.tail(tail)
        self.irq = irq
        while self.exit is None:
            waiting = self.pending and not self.in_service
            self.uc.emu_start(self.r16("cs") * 16 + self.r16("eip"), 0,
                              count=8 if waiting else 1 << 40)
            if self.exit is None and self.deliverable():
                self._inject()
        return self.exit

    # --- the interrupts --------------------------------------------------------
    def _intr(self, uc, intno, _user):
        if intno == 0x21:
            self._dos()
        elif intno == 0x2F:
            ax = self.r16("eax")
            if ax == 0x1600:
                self.w16("eax", 0x0A04 if self.vxd is not None else 0)
            elif ax == 0x1687:
                self.w16("eax", 0x8687)     # no DPMI host
            elif ax == 0x1684:
                there = self.vxd is not None and self.r16("ebx") == 0x3B07
                self.w16("es", VXD_PARA if there else 0)
                self.w16("edi", 0)
        elif intno == INT_VXD:
            self._vxd_api()
        elif intno == 0x16:
            ah = self.r16("eax") >> 8
            flags = self.r("eflags")
            if self.person and not self.keys:
                k = self.person(self.output)
                if k:
                    self.keys.append(k)
            if ah in (0x01, 0x11):
                self.card.wait(KEY_US)
                self._after_io()
                if self.keys:
                    self.w16("eax", ord(self.keys[0]))
                    self.w("eflags", flags & ~ZF)
                else:
                    self.w("eflags", flags | ZF)
            elif ah in (0x00, 0x10):
                if not self.keys:
                    raise EmuError("the program waits for a key")
                self.w16("eax", ord(self.keys.pop(0)))
        elif intno == 0x1A:
            self.w16("ecx", 0)
            self.w16("edx", 0)
        else:
            raise EmuError("int %02Xh at %04x:%04x" % (
                intno, self.r16("cs"), self.r16("eip")))

    def _vxd_api(self):
        """ES1869.VXD's 0001, the first device's structure into ES:BX, as
        many bytes as its first dword says, and 0400 with the devnode in
        ECX; every call in vxd_calls, (DX, AX, ECX, the bytes asked)"""
        fn, ax, ecx = self.r16("edx"), self.r16("eax"), self.r("ecx")
        buf = self.r16("es") * 16 + self.r16("ebx")
        want = struct.unpack("<I", self.mem(buf, 4))[0] if fn == 1 else None
        self.vxd_calls.append((fn, ax, ecx, want))
        adi, info = self.vxd["adi"], self.vxd["info"]
        if fn == 0x0001 and ax == 0 and want <= 0xE9:
            self.put(buf, adi[:want])
            self.w16("eax", 1)
            self.carry(False)
        elif fn == 0x0400 and info is not None and ecx == struct.unpack_from(
                "<I", adi, ADI_DEVNODE)[0]:
            for reg, v in zip(("eax", "ebx", "ecx"), info):
                self.w16(reg, v)
            self.carry(False)
        else:
            self.carry(True)

    def _str(self, seg, off, n):
        return self.mem(seg * 16 + off, n)

    def _dos(self):
        ax = self.r16("eax")
        ah, al = ax >> 8, ax & 0xFF
        bx, cx, dx = self.r16("ebx"), self.r16("ecx"), self.r16("edx")
        ds = self.r16("ds")
        ok = self.carry
        if ah == 0x30:
            self.w16("eax", 0x0A07)     # DOS 7.10
            self.w16("ebx", 0)
        elif ax == 0x3306:
            self.w16("ebx", 0x0A07)
        elif ah == 0x33:
            self.w8("edx", 0)
        elif ah == 0x4A:
            ok(False)
        elif ah == 0x48:
            self.allocated.append(bx)
            if bx > 0x1000:
                self.w16("eax", 8)
                self.w16("ebx", 0x1000)
                ok(True)
            else:
                self.mcb(ALLOC - 1, b"Z", PSP, 0x1000)
                self.w16("eax", ALLOC)
                ok(False)
        elif ah == 0x49:
            ok(False)
        elif ah == 0x35:
            ip, cs = struct.unpack("<HH", self.mem(al * 4, 4))
            self.w16("es", cs)
            self.w16("ebx", ip)
        elif ah == 0x25:
            self.put(al * 4, struct.pack("<HH", dx, ds))
        elif ah == 0x2A:
            d = datetime.date(2026, 10, 25)
            self.w16("ecx", d.year)
            self.w16("edx", (d.month << 8) | d.day)
            self.w8("eax", d.weekday())
        elif ah == 0x2C:
            self.w16("ecx", 0x0C00)
            self.w16("edx", 0)
        elif ah == 0x62:
            self.w16("ebx", PSP)
        elif ah == 0x52:
            self.w16("es", LOL)
            self.w16("ebx", 2)
        elif ah == 0x44:
            if al == 0x00:
                self.w16("edx", 0x80D3 if bx < 5 else 0x0002)
                ok(False)
            else:
                self.w16("eax", 1)
                ok(True)
        elif ah in (0x3C, 0x3D, 0x6C):
            if ah == 0x6C:
                name = self.cstr(ds * 16 + self.r16("esi"))
                create = bool(dx & 0x10) or bool(dx & 0x02)
            else:
                name = self.cstr(ds * 16 + dx)
                create = ah == 0x3C
            name = name.upper()
            if not create and name not in self.files:
                self.w16("eax", 2)
                ok(True)
                return
            if create:
                self.files[name] = bytearray()
            h = 5 + len(self.handles)
            while h in self.handles:
                h += 1
            self.handles[h] = [name, 0]
            self.w16("eax", h)
            if ah == 0x6C:
                self.w16("ecx", 2 if create else 1)
            ok(False)
        elif ah == 0x3E:
            self.handles.pop(bx, None)
            ok(False)
        elif ah == 0x40:
            data = self._str(ds, dx, cx)
            if bx in (1, 2):
                self.output += data.decode("latin-1")
            else:
                f = self.handles[bx]
                buf = self.files[f[0]]
                buf[f[1]:f[1] + cx] = data
                f[1] += cx
            self.w16("eax", cx)
            ok(False)
        elif ah == 0x3F:
            f = self.handles.get(bx)
            if f is None:
                self.w16("eax", 0)
                ok(False)
                return
            data = self.files[f[0]][f[1]:f[1] + cx]
            self.put(ds * 16 + dx, bytes(data))
            f[1] += len(data)
            self.w16("eax", len(data))
            ok(False)
        elif ah == 0x42:
            f = self.handles[bx]
            pos = (cx << 16) | dx
            base = (0, f[1], len(self.files[f[0]]))[al]
            f[1] = base + pos
            self.w16("eax", f[1] & 0xFFFF)
            self.w16("edx", f[1] >> 16)
            ok(False)
        elif ah == 0x41:
            name = self.cstr(ds * 16 + dx).upper()
            if name in self.files:
                del self.files[name]
                ok(False)
            else:
                self.w16("eax", 2)
                ok(True)
        elif ah == 0x56:
            old = self.cstr(ds * 16 + dx).upper()
            new = self.cstr(self.r16("es") * 16 + self.r16("edi")).upper()
            if old not in self.files or new in self.files:
                self.w16("eax", 5)
                ok(True)
            else:
                self.files[new] = self.files.pop(old)
                ok(False)
        elif ah == 0x68:
            ok(False)
        elif ax == 0x5802:          # the UMB link
            self.w8("eax", int(self.umb_linked))
            ok(False)
        elif ax == 0x5803:
            self.umb_calls.append(bx)
            if self.umb is not None:
                self.link_umbs(bool(bx & 1))
            ok(False)
        elif ax == 0x6601:          # the code page: 437
            self.w16("ebx", 437)
            self.w16("edx", 437)
            ok(False)
        elif ax == 0x6300:          # the DBCS lead bytes: none
            self.put(0x510, b"\0\0")
            self.w16("ds", 0x0050)
            self.w16("esi", 0x0010)
            self.w8("eax", 0)
            ok(False)
        elif ah == 0x02:
            self.output += chr(dx & 0xFF)
        elif ah == 0x09:
            s = b""
            while True:
                c = self.mem(ds * 16 + dx + len(s), 1)
                if c == b"$":
                    break
                s += c
            self.output += s.decode("latin-1")
        elif ah == 0x0B:
            self.w8("eax", 0)
        elif ah == 0x19:
            self.w8("eax", 2)
        elif ah == 0x47:
            self.put(ds * 16 + self.r16("esi"), b"\0")
            ok(False)
        elif ah == 0x4C:
            self.exit = al
            self.uc.emu_stop()
        else:
            raise EmuError("int 21h AX=%04X at %04x:%04x" % (
                ax, self.r16("cs"), self.r16("eip")))
