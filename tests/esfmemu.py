# SPDX-License-Identifier: GPL-3.0-or-later
"""Run ESFM.DRV in a 16-bit CPU emulator against a model of the FM chip.

The driver's segments are loaded at fixed paragraphs, its relocations are
applied, and every import (KERNEL, MMSYSTEM) and the ES1869.VXD entry point
is a stub that stops the emulator so Python can answer it.  FM port writes
go to FMChip, which records every write together with the call it came
from, so a test can see keys left on and address/data pairs split by an
interrupt.

Interrupts are simulated: a nested modMessage call can be injected after the
N-th FM port write or the N-th instruction of the running call (only where
IF is set, as the hardware would), or from inside DriverCallback, the way a
client sends MIDI data from its MOM_DONE callback.

    emu = ESFMEmu(open("build/ESFM.DRV", "rb").read())
    emu.open()
    emu.data(0x403C90)                     # note on
    emu.data(0x003C80, inject=Inject(after_writes=5, msg=MODM_DATA, dw1=...))
    emu.keyed_voices()                     # voices keyed on in the chip
"""

import os
import struct
import sys

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR,
                     UC_HOOK_INSN, UC_HOOK_CODE)
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX,
                               UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
                               UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_IP,
                               UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
                               UC_X86_REG_SS, UC_X86_REG_EFLAGS,
                               UC_X86_REG_ECX, UC_X86_INS_IN, UC_X86_INS_OUT)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from retools.ne import NEFile  # noqa: E402

SEG_PARA = {1: 0x1000, 2: 0x2000, 3: 0x3000, 4: 0x5000}
BANK_PARA = 0x7000
STUB_PARA = 0x8000
STACK_PARA = 0x9000
TRAMP_OFF = 0x8000          # trampolines in the stub segment
CLIENT_OFF = 0xC000         # scratch data in the stub segment
HEAP_START = 0x0400         # LocalAlloc arena in DGROUP
FM_PORT = 0x388
DEVNODE = 0x00012345
IF_FLAG = 0x200

MODM_OPEN, MODM_CLOSE, MODM_DATA, MODM_LONGDATA, MODM_RESET = 3, 4, 7, 8, 9
MODM_PREPARE = 5
DRV_POWER = 0x0F
PWR_SUSPENDREQUEST, PWR_SUSPENDRESUME = 1, 2
MOM_OPEN, MOM_CLOSE, MOM_DONE = 0x3C7, 0x3C8, 0x3C9
MIDIERR_NOTREADY = 0x43
MHDR_DONE, MHDR_PREPARED = 1, 2

# import stubs: name -> bytes of arguments (Pascal: popped by the callee)
STUB_ARGS = {
    ("KERNEL", 5): 4,       # LocalAlloc(flags, size)
    ("KERNEL", 7): 2,       # LocalFree(h)
    ("KERNEL", 111): 2,     # GlobalWire(h)
    ("KERNEL", 112): 2,     # GlobalUnWire(h)
    ("KERNEL", 191): 2,     # GlobalPageLock(sel)
    ("KERNEL", 192): 2,     # GlobalPageUnlock(sel)
    ("MMSYSTEM", 31): 22,   # DriverCallback
    ("MMSYSTEM", 216): 12,  # midiOutMessage(h, msg, dw1, dw2)
}


class EmuError(Exception):
    pass


class FMChip:
    """ESFM in native mode: FM_Base+2/+3 select a register, +1 writes it."""

    def __init__(self, base=FM_PORT):
        self.base = base
        self.regs = bytearray(0x800)
        self.addr = 0
        self.events = []        # (depth, port offset, value)
        self.splits = []        # (register written, intended register)
        self.pending = {}       # depth -> [low set, high set] of this writer
        self.writes = 0

    def out(self, port, value, depth):
        off = port - self.base
        self.events.append((depth, off, value))
        self.writes += 1
        if off == 2:
            self.addr = (self.addr & 0xFF00) | value
            self._track(depth, "low", value)
        elif off == 3:
            self.addr = (self.addr & 0x00FF) | (value << 8)
            self._track(depth, "high", value)
        elif off == 1:
            want = self.pending.pop(depth, None)
            if want is not None and want.get("reg") is not None and \
                    want["reg"] != self.addr:
                self.splits.append((self.addr, want["reg"]))
            self.regs[self.addr & 0x7FF] = value
        # writes of other writers in between invalidate what this one set
        for d, w in self.pending.items():
            if d != depth and off in (2, 3):
                w["clobbered"] = True

    def _track(self, depth, part, value):
        w = self.pending.setdefault(depth, {"low": None, "high": None,
                                            "reg": None})
        w[part] = value
        if w["low"] is not None and w["high"] is not None:
            w["reg"] = w["low"] | (w["high"] << 8)

    def inp(self, port):
        return 0x00

    def keyed(self):
        """Voices keyed on (0-17), from the key-on registers."""
        out = [v for v in range(16) if self.regs[0x240 + v] & 1]
        if self.regs[0x250] & 1 or self.regs[0x251] & 1:
            out.append(16)
        if self.regs[0x252] & 1 or self.regs[0x253] & 1:
            out.append(17)
        return out


class Inject:
    """A nested modMessage call, as if made by an interrupt handler."""

    def __init__(self, msg, dw1=0, dw2=0, after_writes=None,
                 after_insns=None, in_callback=None, user=None):
        self.msg, self.dw1, self.dw2 = msg, dw1, dw2
        self.after_writes, self.after_insns = after_writes, after_insns
        self.in_callback = in_callback      # MOM_* message to react to
        self.user = user
        self.result = None
        self.done = False
        self.started = False
        self.deferred = False


class ESFMEmu:
    def __init__(self, drv, bank=None):
        self.ne = ne = NEFile(drv)
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x100000)
        for s in ne.segments:
            uc.mem_write(SEG_PARA[s.index] * 16, ne.segment_data(s.index))
        self.stubs = {}          # stub offset -> (module, ordinal)
        self.stub_at = {}
        self._next_stub = 0x10
        self.sentinel = self._stub(("SENTINEL", 0))
        self.marker = self._stub(("MARKER", 0))
        self.vxd_entry = self._stub(("VXD", 0))
        self._relocate()
        entries = ne.entries
        seg, off, _f = entries[3]
        self.modmsg_addr = (SEG_PARA[seg], off)
        self.chip = FMChip()
        # the local heap follows the static data of DGROUP
        dgroup = ne.segments[ne.autodata - 1]
        self.heap = max(HEAP_START, (dgroup.minalloc + 0x1F) & ~0xF)
        self.callbacks = []      # (msg, dwInstance, dw1, dw2)
        self.vxd_calls = []
        self.depth = 0
        self.injects = []
        self.icount = 0
        self._code_hook = None
        self.returns = {}
        uc.hook_add(UC_HOOK_INTR, self._intr)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        # the driver's DGROUP state after DRV_LOAD and DRV_ENABLE
        bank = bank if bank is not None else ne.resource_data(256, 1234)
        uc.mem_write(BANK_PARA * 16, bank)
        self.w16(4, 0x12, 0)
        self.w16(4, 0x14, BANK_PARA)
        self.client = None
        self.dev = None
        self._pending_marker = []

    # --- memory -----------------------------------------------------------

    def lin(self, para, off):
        return para * 16 + (off & 0xFFFF)

    def r8(self, seg, off):
        return self.uc.mem_read(self.lin(SEG_PARA[seg], off), 1)[0]

    def r16(self, seg, off):
        return struct.unpack("<H", self.uc.mem_read(
            self.lin(SEG_PARA[seg], off), 2))[0]

    def w16(self, seg, off, val):
        self.uc.mem_write(self.lin(SEG_PARA[seg], off),
                          struct.pack("<H", val & 0xFFFF))

    def rd(self, para, off, n):
        return bytes(self.uc.mem_read(self.lin(para, off), n))

    def wr(self, para, off, data):
        self.uc.mem_write(self.lin(para, off), bytes(data))

    # --- loading ----------------------------------------------------------

    def _stub(self, key):
        off = self._next_stub
        self._next_stub += 1
        self.uc.mem_write(STUB_PARA * 16 + off, b"\xCC")      # int 3
        self.stubs[off] = key
        self.stub_at[key] = off
        return off

    def _target(self, rtype, rflags, target):
        a, b = target
        kind = rflags & 3
        if kind == 0:
            seg = a & 0xFF
            if seg == 0xFF:
                seg, off, _f = self.ne.entries[b]
                return SEG_PARA[seg], off
            return SEG_PARA[seg], b
        if kind == 1:
            mod = self.ne.modules[a - 1]
            if (mod, b) == ("KERNEL", 178):     # __WINFLAGS
                return None, 0x0025
            key = (mod, b)
            off = self.stub_at.get(key)
            if off is None:
                off = self._stub(key)
            return STUB_PARA, off
        raise EmuError("unsupported relocation %r" % ((rtype, rflags, target),))

    def _relocate(self):
        for s in self.ne.segments:
            base = SEG_PARA[s.index] * 16
            for rtype, rflags, off, target in s.relocs:
                seg, toff = self._target(rtype, rflags, target)
                pos, sites = off, []
                if rflags & 4:
                    sites = [off]
                else:
                    while pos != 0xFFFF:
                        sites.append(pos)
                        pos = struct.unpack("<H", self.uc.mem_read(
                            base + pos, 2))[0]
                for site in sites:
                    old = struct.unpack("<HH", self.uc.mem_read(
                        base + site, 4))
                    add = old[0] if rflags & 4 else 0
                    if rtype == 2:
                        val = struct.pack("<H", seg)
                    elif rtype == 3:
                        val = struct.pack("<HH", (toff + add) & 0xFFFF, seg)
                    elif rtype == 5:
                        val = struct.pack("<H", (toff + add) & 0xFFFF)
                    else:
                        raise EmuError("relocation type %d" % rtype)
                    self.uc.mem_write(base + site, val)

    # --- hooks ------------------------------------------------------------

    def _intr(self, uc, intno, _user):
        if intno == 3:
            ip = uc.reg_read(UC_X86_REG_IP)
            cs = uc.reg_read(UC_X86_REG_CS)
            self.stop = ("int3", cs, (ip - 1) & 0xFFFF)
            uc.emu_stop()
        elif intno == 0x2F:
            ax, bx = uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_BX)
            if ax == 0x1684 and bx == 0x3B07:
                uc.reg_write(UC_X86_REG_ES, STUB_PARA)
                uc.reg_write(UC_X86_REG_DI, self.vxd_entry)
            else:
                uc.reg_write(UC_X86_REG_ES, 0)
                uc.reg_write(UC_X86_REG_DI, 0)
        else:
            raise EmuError("interrupt %#x" % intno)

    def _in(self, uc, port, size, _user):
        if self.chip.base <= port < self.chip.base + 4:
            return self.chip.inp(port)
        return 0xFF

    def _out(self, uc, port, size, value, _user):
        if self.chip.base <= port < self.chip.base + 4:
            self.chip.out(port, value & 0xFF, self.depth)
        for inj in self.injects:
            if not inj.started and inj.after_writes is not None and \
                    self.depth == 0 and self.chip.writes >= inj.after_writes:
                self.stop = ("inject", inj)
                uc.emu_stop()
                return

    def _count(self, uc, address, size, _user):
        if self.depth:
            return
        self.icount += 1
        for inj in self.injects:
            if not inj.started and inj.after_insns is not None and \
                    self.icount > inj.after_insns:
                self.stop = ("inject", inj)
                uc.emu_stop()
                return

    # --- running ----------------------------------------------------------

    def _push(self, *words):
        sp = self.uc.reg_read(UC_X86_REG_SP)
        for w in words:
            sp = (sp - 2) & 0xFFFF
            self.uc.mem_write(STACK_PARA * 16 + sp, struct.pack("<H", w))
        self.uc.reg_write(UC_X86_REG_SP, sp)

    def _pop(self):
        sp = self.uc.reg_read(UC_X86_REG_SP)
        w = struct.unpack("<H", self.uc.mem_read(STACK_PARA * 16 + sp, 2))[0]
        self.uc.reg_write(UC_X86_REG_SP, (sp + 2) & 0xFFFF)
        return w

    def call(self, target, args, injects=()):
        """Far call target (para, off) with word arguments pushed in order;
        returns DX:AX."""
        uc = self.uc
        self.injects = list(injects)
        self.icount = 0
        need_count = any(i.after_insns is not None for i in self.injects)
        if need_count and self._code_hook is None:
            self._code_hook = uc.hook_add(UC_HOOK_CODE, self._count)
            uc.ctl_flush_tb()       # code hooks apply to newly translated code
        uc.reg_write(UC_X86_REG_SS, STACK_PARA)
        uc.reg_write(UC_X86_REG_SP, 0xFFF0)
        uc.reg_write(UC_X86_REG_DS, SEG_PARA[4])
        uc.reg_write(UC_X86_REG_ES, SEG_PARA[4])
        uc.reg_write(UC_X86_REG_EFLAGS, 0x0202)
        self._push(*args)
        self._push(STUB_PARA, self.sentinel)
        cs, ip = target
        waiting = None          # injection waiting for IF
        try:
            while True:
                self.stop = None
                uc.reg_write(UC_X86_REG_CS, cs)
                uc.reg_write(UC_X86_REG_IP, ip)
                uc.emu_start(self.lin(cs, ip), 0,
                             count=1 if waiting else 5_000_000)
                cs = uc.reg_read(UC_X86_REG_CS)
                ip = uc.reg_read(UC_X86_REG_IP)
                if waiting is not None and (self.stop is None or
                                            self.stop[0] == "inject"):
                    if uc.reg_read(UC_X86_REG_EFLAGS) & IF_FLAG:
                        cs, ip = self._inject(waiting, cs, ip, True)
                        waiting = None
                    continue
                if self.stop is None:
                    raise EmuError("emulation ended at %04x:%04x" % (cs, ip))
                if self.stop[0] == "inject":
                    inj = self.stop[1]
                    if uc.reg_read(UC_X86_REG_EFLAGS) & IF_FLAG:
                        cs, ip = self._inject(inj, cs, ip, True)
                    else:
                        # interrupts are off: the interrupt waits; run one
                        # instruction at a time until IF is set again
                        inj.deferred = True
                        inj.started = True
                        waiting = inj
                    continue
                _k, scs, sip = self.stop
                if scs != STUB_PARA:
                    raise EmuError("int 3 at %04x:%04x" % (scs, sip))
                key = self.stubs[sip]
                if key == ("SENTINEL", 0):
                    if waiting is not None:
                        raise EmuError("interrupts still off at the return")
                    return (uc.reg_read(UC_X86_REG_DX) << 16) | \
                        uc.reg_read(UC_X86_REG_AX)
                if key == ("MARKER", 0):
                    inj = self._pending_marker.pop()
                    inj.result = uc.reg_read(UC_X86_REG_AX)
                    inj.done = True
                    self.depth -= 1
                    ip = self._pop()
                    cs = self._pop()
                    continue
                cs, ip = self._handle_stub(key)
        finally:
            if self._code_hook is not None:
                uc.hook_del(self._code_hook)
                self._code_hook = None
                uc.ctl_flush_tb()

    def _inject(self, inj, cs, ip, interrupt):
        """Push an interrupt frame and enter a trampoline that calls
        modMessage and returns with IRET."""
        inj.started = True
        self._pending_marker.append(inj)
        flags = self.uc.reg_read(UC_X86_REG_EFLAGS)
        self._push(flags & 0xFFFF, cs, ip)
        self.uc.reg_write(UC_X86_REG_EFLAGS, flags & ~IF_FLAG)
        user = inj.user if inj.user is not None else (self.client or 0)
        code = bytearray(b"\x60\x1E\x06")             # pusha, push ds, es
        for w in (0, inj.msg, user >> 16, user & 0xFFFF, inj.dw1 >> 16,
                  inj.dw1 & 0xFFFF, inj.dw2 >> 16, inj.dw2 & 0xFFFF):
            code += b"\x68" + struct.pack("<H", w & 0xFFFF)
        code += b"\x9A" + struct.pack("<HH", self.modmsg_addr[1],
                                      self.modmsg_addr[0])
        code += b"\x9A" + struct.pack("<HH", self.marker, STUB_PARA)
        code += b"\x07\x1F\x61\xCF"                    # pop es, ds; popa; iret
        tramp = TRAMP_OFF + 0x40 * self.depth
        self.uc.mem_write(STUB_PARA * 16 + tramp, bytes(code))
        # the marker stub is reached with a far call: drop its return address
        self.depth += 1
        return STUB_PARA, tramp

    def _handle_stub(self, key):
        uc = self.uc
        # return address of the far call into the stub
        sp = uc.reg_read(UC_X86_REG_SP)
        args_at = sp + 4
        ret_ip, ret_cs = struct.unpack("<HH", uc.mem_read(
            STACK_PARA * 16 + sp, 4))

        def arg(off, size=2):
            fmt = "<H" if size == 2 else "<I"
            return struct.unpack(fmt, uc.mem_read(
                STACK_PARA * 16 + args_at + off, size))[0]

        ax = dx = 0
        pop = 0
        inject_after = None
        if key == ("MARKER", 0):
            raise EmuError("marker")
        if key == ("VXD", 0):
            fn = uc.reg_read(UC_X86_REG_DX)
            self.vxd_calls.append(fn)
            flags = uc.reg_read(UC_X86_REG_EFLAGS) & ~1
            ax = uc.reg_read(UC_X86_REG_AX)
            if fn == 0x0000:
                ax = 0x0404
            elif fn == 0x0101:
                es, bx = uc.reg_read(UC_X86_REG_ES), uc.reg_read(UC_X86_REG_BX)
                buf = bytearray(self.rd(es, bx, 0x1C))
                struct.pack_into("<HH", buf, 4, 0x0001, FM_PORT)
                struct.pack_into("<I", buf, 0x0C, DEVNODE)
                self.wr(es, bx, buf)
            elif fn in (0x0102, 0x0103, 0x0201):
                pass
            elif fn == 0x0200:
                ax, dx = 0x0042, 0x0000
            else:
                flags |= 1
            uc.reg_write(UC_X86_REG_EFLAGS, flags)
            uc.reg_write(UC_X86_REG_AX, ax)
            uc.reg_write(UC_X86_REG_DX, dx)
            uc.reg_write(UC_X86_REG_SP, sp + 4)
            return ret_cs, ret_ip
        if key not in STUB_ARGS:
            raise EmuError("no stub for %s.%d" % key)
        pop = STUB_ARGS[key]
        mod, ordinal = key
        if key == ("KERNEL", 5):            # LocalAlloc(flags, size)
            size, flags = arg(0), arg(2)
            ax = self.heap
            self.heap = (self.heap + size + 15) & ~15
            self.wr(SEG_PARA[4], ax, b"\0" * size)
        elif key == ("KERNEL", 7):
            ax = 0
        elif key == ("KERNEL", 111):        # GlobalWire -> far pointer
            ax, dx = 0, arg(0)
        elif key in (("KERNEL", 112), ("KERNEL", 191)):
            ax = 1
        elif key == ("KERNEL", 192):
            ax = 0
        elif key == ("MMSYSTEM", 31):       # DriverCallback
            dw2, dw1, inst = arg(0, 4), arg(4, 4), arg(8, 4)
            msg = arg(12)
            self.callbacks.append((msg, inst, dw1, dw2))
            ax = 1
            for inj in self.injects:
                if not inj.started and inj.in_callback == msg:
                    inject_after = inj
                    break
        elif key == ("MMSYSTEM", 216):      # midiOutMessage
            # DRVM_ENABLE asks MMDEVLDR (message 804h) for the device ID of
            # the VxD that owns the devnode
            msg, dw1 = arg(8), arg(4, 4)
            if msg == 0x804:
                self.wr(dw1 >> 16, dw1 & 0xFFFF, struct.pack("<H", 0x3B07))
            ax = 0
        uc.reg_write(UC_X86_REG_AX, ax)
        uc.reg_write(UC_X86_REG_DX, dx)
        uc.reg_write(UC_X86_REG_SP, sp + 4 + pop)
        if inject_after is not None:
            # the client's callback sends data before returning
            return self._inject(inject_after, ret_cs, ret_ip, interrupt=False)
        return ret_cs, ret_ip

    # --- driver interface -------------------------------------------------

    def modmessage(self, msg, dw1=0, dw2=0, user=None, injects=()):
        user = user if user is not None else (self.client or 0)
        return self.call(self.modmsg_addr, [0, msg, user >> 16, user & 0xFFFF,
                                           dw1 >> 16, dw1 & 0xFFFF,
                                           dw2 >> 16, dw2 & 0xFFFF],
                         injects) & 0xFFFF

    def open(self, callback=True, injects=()):
        """DRVM_INIT and DRVM_ENABLE (a devnode arrives), then
        MODM_OPEN with a callback function."""
        if self.modmessage(0x64, 0, DEVNODE, user=0) != 0:
            raise EmuError("DRVM_INIT failed")
        r = self.modmessage(0x67, 0, DEVNODE, user=0)
        if r != 0:
            raise EmuError("DRVM_ENABLE failed: %#x" % r)
        self.dev = self.r16(4, 0x3C)
        # MIDIOPENDESC: hMidi, dwCallback, dwInstance, ..., dnDevNode at +0Ch
        desc = struct.pack("<HIIHI", 0x1111, 0x22223333, 0x44445555, 0,
                           DEVNODE)
        self.wr(STUB_PARA, CLIENT_OFF, desc)
        self.wr(STUB_PARA, CLIENT_OFF + 0x40, b"\0" * 4)
        flags = 0x00030000 if callback else 0
        r = self.call(self.modmsg_addr, [
            0, MODM_OPEN, STUB_PARA, CLIENT_OFF + 0x40, STUB_PARA,
            CLIENT_OFF, flags >> 16, flags & 0xFFFF], injects) & 0xFFFF
        if r != 0:
            raise EmuError("MODM_OPEN failed: %#x" % r)
        self.client = struct.unpack("<I", self.rd(STUB_PARA, CLIENT_OFF + 0x40,
                                                   4))[0]
        return r

    def data(self, msg, injects=()):
        return self.modmessage(MODM_DATA, msg, 0, injects=injects)

    def longdata(self, data, injects=(), at=0xD000):
        hdr = at
        buf = at + 0x40
        self.wr(STUB_PARA, buf, data)
        # MIDIHDR: lpData, dwBufferLength, dwBytesRecorded, dwUser, dwFlags
        self.wr(STUB_PARA, hdr, struct.pack("<IIIII", (STUB_PARA << 16) | buf,
                                            len(data), len(data), 0,
                                            MHDR_PREPARED))
        r = self.modmessage(MODM_LONGDATA, (STUB_PARA << 16) | hdr, 0x20,
                            injects=injects)
        flags = struct.unpack("<I", self.rd(STUB_PARA, hdr + 16, 4))[0]
        return r, flags

    def header(self, data, at=0xD800):
        """A prepared MIDIHDR with data in the stub segment; far pointer."""
        buf = at + 0x40
        self.wr(STUB_PARA, buf, data)
        self.wr(STUB_PARA, at, struct.pack("<IIIII", (STUB_PARA << 16) | buf,
                                           len(data), len(data), 0,
                                           MHDR_PREPARED))
        return (STUB_PARA << 16) | at

    def header_flags(self, ptr):
        return struct.unpack("<I", self.rd(ptr >> 16, (ptr & 0xFFFF) + 16,
                                           4))[0]

    def reset(self, injects=()):
        return self.modmessage(MODM_RESET, 0, 0, injects=injects)

    def close(self, injects=()):
        return self.modmessage(MODM_CLOSE, 0, 0, injects=injects)

    def driverproc(self, msg, lp1=0, lp2=0, injects=()):
        """DriverProc(dwDriverID, hDriver, msg, lParam1, lParam2)."""
        seg, off, _f = self.ne.entries[2]
        return self.call((SEG_PARA[seg], off), [
            0, 1, 1, msg, lp1 >> 16, lp1 & 0xFFFF, lp2 >> 16, lp2 & 0xFFFF],
            injects)

    # --- state ------------------------------------------------------------

    def fix_state(self):
        """Counters of the fixed driver (None for the ESS driver)."""
        dgroup = self.rd(SEG_PARA[4], 0, 0x800)
        at = dgroup.find(b"ESFMFIX\0")
        if at < 0:
            return None
        names = ("version", "lock", "head", "tail", "queued", "overflow",
                 "maxdepth", "purged", "qsize")
        return dict(zip(names, struct.unpack_from("<HHHHIIHHH", dgroup,
                                                  at + 8)))

    def voices(self):
        """Driver's voice table: list of (flags, channel, note)."""
        out = []
        for v in range(18):
            base = self.dev + 0x70 + 0x21 * v
            out.append((self.r8(4, base), self.r8(4, base + 5),
                        self.r8(4, base + 6)))
        return out

    def keyed_voices(self):
        return self.chip.keyed()

    def driver_active(self):
        return [v for v, (f, _c, _n) in enumerate(self.voices()) if f & 1]

    def busy(self):
        return self.r16(4, 0x22)
