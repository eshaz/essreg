# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Run ESFM.DRV in a 16-bit CPU emulator against a model of the FM chip.

The driver's segments are loaded at fixed paragraphs with their relocations
applied. Every import (KERNEL, MMSYSTEM) and the ES1869.VXD entry point is
a stub that stops the emulator so Python can answer it. FMChip records
each FM port write with the call it came from, so a test can see keys left
on and address/data pairs split by an interrupt.

A nested modMessage call can be injected like an interrupt, after the N-th
FM port write or the N-th instruction of the running call (held until IF
is set, as on the hardware), or from DriverCallback, the way a client
sends MIDI data from its MOM_DONE callback.

The bank file of the fixed driver has what it needs too: SYSTEM.INI
(emu.ini), files (emu.files, read through DOS3Call), a global heap with
page locks, the driver's resources, and the task that watches the file,
which runs when it's signaled and a Yield or emu.run_task() lets it.
emu.tick() fires the timers and then runs the task, like one second.

    emu = ESFMEmu(open("build/ESFM.DRV", "rb").read())
    emu.open()
    emu.data(0x403C90)                     # note on
    emu.data(0x003C80, injects=[Inject(MODM_DATA, dw1=..., after_writes=5)])
    emu.keyed_voices()                     # voices keyed on in the chip
"""

import os
import struct
import sys

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR,
                     UC_HOOK_INSN, UC_HOOK_CODE, UC_HOOK_MEM_WRITE)
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
TASK_STACK_PARA = 0x6000    # stack of the watcher task
BANK_PARA = 0x7000
STUB_PARA = 0x8000
STACK_PARA = 0x9000
HEAP_PARA = 0xA000          # GlobalAlloc blocks, up to HEAP_END
HEAP_END = 0xF000
APP_TASK = 0x1111           # GetCurrentTask of the program
WATCH_TASK = 0x2222         # and of the task mmTaskCreate starts
TRAMP_OFF = 0x8000          # trampolines in the stub segment
CLIENT_OFF = 0xC000         # scratch data in the stub segment
HEAP_START = 0x0400         # LocalAlloc arena in DGROUP
FM_PORT = 0x388
DEVNODE = 0x00012345
IF_FLAG = 0x200
DOS_REGS = {"AX": UC_X86_REG_AX, "BX": UC_X86_REG_BX, "CX": UC_X86_REG_CX,
            "DX": UC_X86_REG_DX, "SI": UC_X86_REG_SI, "DS": UC_X86_REG_DS}

MODM_OPEN, MODM_CLOSE, MODM_DATA, MODM_LONGDATA, MODM_RESET = 3, 4, 7, 8, 9
MODM_PREPARE = 5
DRV_ENABLE, DRV_DISABLE = 2, 5
DRV_POWER = 0x0F
PWR_SUSPENDREQUEST, PWR_SUSPENDRESUME = 1, 2
MOM_OPEN, MOM_CLOSE, MOM_DONE = 0x3C7, 0x3C8, 0x3C9
MIDIERR_NOTREADY = 0x43
MHDR_DONE, MHDR_PREPARED = 1, 2

# import stubs: (module, ordinal) -> argument bytes the callee pops (Pascal)
STUB_ARGS = {
    ("KERNEL", 5): 4,       # LocalAlloc(flags, size)
    ("KERNEL", 7): 2,       # LocalFree(h)
    ("KERNEL", 15): 6,      # GlobalAlloc(flags, dwBytes)
    ("KERNEL", 17): 2,      # GlobalFree(h)
    ("KERNEL", 20): 2,      # GlobalSize(h)
    ("KERNEL", 29): 0,      # Yield()
    ("KERNEL", 36): 0,      # GetCurrentTask()
    ("KERNEL", 60): 10,     # FindResource(hInst, lpName, lpType)
    ("KERNEL", 61): 4,      # LoadResource(hInst, hRsrc)
    ("KERNEL", 62): 2,      # LockResource(hResData)
    ("KERNEL", 63): 2,      # FreeResource(hResData)
    ("KERNEL", 65): 4,      # SizeofResource(hInst, hRsrc)
    ("KERNEL", 88): 8,      # lstrcpy(dst, src)
    ("KERNEL", 102): 0,     # DOS3Call: INT 21h registers
    ("KERNEL", 111): 2,     # GlobalWire(h)
    ("KERNEL", 112): 2,     # GlobalUnWire(h)
    ("KERNEL", 128): 22,    # GetPrivateProfileString(app, key, def, buf,
                            # size, file)
    ("KERNEL", 191): 2,     # GlobalPageLock(sel)
    ("KERNEL", 192): 2,     # GlobalPageUnlock(sel)
    ("USER", 471): 8,       # lstrcmpi(s1, s2)
    ("MMSYSTEM", 31): 22,   # DriverCallback
    ("MMSYSTEM", 216): 12,  # midiOutMessage(h, msg, dw1, dw2)
    ("MMSYSTEM", 602): 14,  # timeSetEvent(delay, res, lpfn, dwUser, flags)
    ("MMSYSTEM", 603): 2,   # timeKillEvent(id)
    ("MMSYSTEM", 900): 12,  # mmTaskCreate(lpfn, lph, dwInst)
    ("MMSYSTEM", 902): 2,   # mmTaskBlock(h)
    ("MMSYSTEM", 903): 2,   # mmTaskSignal(h)
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
        self.pending = {}       # depth -> address bytes set by that writer
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
        # an address write clobbers what writers at other depths set
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
                 after_insns=None, in_callback=None, user=None,
                 on_lock=False):
        self.msg, self.dw1, self.dw2 = msg, dw1, dw2
        self.after_writes, self.after_insns = after_writes, after_insns
        self.in_callback = in_callback      # MOM_* message to react to
        self.on_lock = on_lock              # when the fixed driver takes
                                            # its lock (fix_lock goes 0 -> 1)
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
        self.bank_res = ne.resource_data(256, 1234)
        bank = bank if bank is not None else self.bank_res
        uc.mem_write(BANK_PARA * 16, bank)
        self.w16(4, 0x12, 0)
        self.w16(4, 0x14, BANK_PARA)
        self.client = None
        self.dev = None
        self._pending_marker = []
        self.calls = []          # imports called, in order
        # global heap: paragraph of each block -> bytes
        self.gblocks = {BANK_PARA: len(bank)}
        self.plocks = {}         # page-lock count of each block
        self.wires = {}
        self.freed_locked = []   # blocks freed while page-locked
        self.freed = set()       # blocks freed and not given out again
        self.check_selectors = False
        self.stale = []          # (cs, ip, register) holding a freed block
        self.gmem_fail = False   # GlobalAlloc fails
        # SYSTEM.INI and the files DOS3Call reads
        self.ini = {}            # (section, key), lower case -> value
        self.ini_reads = []
        self.files = {}          # path in upper case -> bytes
        self.handles = {}        # DOS handle -> [path, position, bytes]
        self.lfn = True          # INT 21h 716Ch works, as on Windows 95;
                                 # False: AX=7100h and the carry set, as on
                                 # Windows 3.1, "nocarry": the carry clear
        self.opens = []          # paths opened, in order
        # the watcher task (mmTaskCreate) and the timers (timeSetEvent)
        self.task = None
        self.tasks_created = 0
        self.mmtask_error = 0    # mmTaskCreate fails with this when nonzero
        self.timer_fail = False
        self.timers = {}         # id -> (callback, dwUser)
        self._next_timer = 1
        self.cur_task = APP_TASK
        self.task_exit = self._stub(("TASK_EXIT", 0))
        self._lock_seen = False

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

    def _lock_write(self, uc, access, address, size, value, _user):
        # stopping here would run the write again after the interrupt, so
        # _count injects at the next instruction
        if not self.depth and value & 0xFFFF:
            self._lock_seen = True

    def _count(self, uc, address, size, _user):
        if self.check_selectors and self.freed:
            # in protected mode, a segment register holding a freed
            # selector faults when it's loaded again, by the code or by an
            # interrupt handler that saves and restores it
            for name, reg in (("DS", UC_X86_REG_DS), ("ES", UC_X86_REG_ES),
                              ("SS", UC_X86_REG_SS)):
                if uc.reg_read(reg) in self.freed:
                    self.stale.append((uc.reg_read(UC_X86_REG_CS),
                                       uc.reg_read(UC_X86_REG_IP), name))
        if self.depth:
            return
        self.icount += 1
        if self._lock_seen:
            for inj in self.injects:
                if not inj.started and inj.on_lock:
                    self.stop = ("inject", inj)
                    uc.emu_stop()
                    return
        for inj in self.injects:
            if not inj.started and inj.after_insns is not None and \
                    self.icount > inj.after_insns:
                self.stop = ("inject", inj)
                uc.emu_stop()
                return

    # --- running ----------------------------------------------------------

    def _ss_lin(self, off):
        return self.uc.reg_read(UC_X86_REG_SS) * 16 + (off & 0xFFFF)

    def _push(self, *words):
        sp = self.uc.reg_read(UC_X86_REG_SP)
        for w in words:
            sp = (sp - 2) & 0xFFFF
            self.uc.mem_write(self._ss_lin(sp), struct.pack("<H", w & 0xFFFF))
        self.uc.reg_write(UC_X86_REG_SP, sp)

    def _pop(self):
        sp = self.uc.reg_read(UC_X86_REG_SP)
        w = struct.unpack("<H", self.uc.mem_read(self._ss_lin(sp), 2))[0]
        self.uc.reg_write(UC_X86_REG_SP, (sp + 2) & 0xFFFF)
        return w

    _REGS = (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
             UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP,
             UC_X86_REG_CS, UC_X86_REG_IP, UC_X86_REG_DS, UC_X86_REG_ES,
             UC_X86_REG_SS, UC_X86_REG_EFLAGS)

    def _save(self):
        return [self.uc.reg_read(r) for r in self._REGS]

    def _restore(self, regs):
        for r, v in zip(self._REGS, regs):
            self.uc.reg_write(r, v)

    def call(self, target, args, injects=()):
        """Far call target (para, off) with the word arguments pushed in
        order and return DX:AX."""
        uc = self.uc
        uc.reg_write(UC_X86_REG_SS, STACK_PARA)
        uc.reg_write(UC_X86_REG_SP, 0xFFF0)
        uc.reg_write(UC_X86_REG_DS, SEG_PARA[4])
        uc.reg_write(UC_X86_REG_ES, SEG_PARA[4])
        uc.reg_write(UC_X86_REG_EFLAGS, 0x0202)
        self._push(*args)
        self._push(STUB_PARA, self.sentinel)
        how, value = self._run(target, injects)
        if how != "return":
            raise EmuError("mmTaskBlock outside the task")
        return value

    def run_task(self, injects=()):
        """Run the watcher task until it blocks or ends, if it can run: it
        is new, or blocked in mmTaskBlock and signaled. True if it ran."""
        t = self.task
        if t is None or t["state"] not in ("new", "blocked"):
            return False
        uc = self.uc
        if t["state"] == "blocked":
            if not t["signals"]:
                return False
            t["signals"] -= 1
            self._restore(t["ctx"])
            target = (t["ctx"][8], t["ctx"][9])
        else:
            # MMTASK.TSK calls the task procedure with its own DS
            uc.reg_write(UC_X86_REG_SS, TASK_STACK_PARA)
            uc.reg_write(UC_X86_REG_SP, 0xFFF0)
            uc.reg_write(UC_X86_REG_DS, STUB_PARA)
            uc.reg_write(UC_X86_REG_ES, STUB_PARA)
            uc.reg_write(UC_X86_REG_EFLAGS, 0x0202)
            self._push(t["inst"] >> 16, t["inst"])
            self._push(STUB_PARA, self.task_exit)
            target = t["proc"]
        t["state"] = "running"
        t["runs"] += 1
        prev, self.cur_task = self.cur_task, WATCH_TASK
        try:
            how, _value = self._run(target, injects)
        finally:
            self.cur_task = prev
        t["state"] = "blocked" if how == "block" else "done"
        return True

    def tick(self, injects=()):
        """One second: the timer callbacks (at interrupt time), then the
        task if they signaled it. True if the task ran."""
        for tid, (cb, user) in list(self.timers.items()):
            self.call(cb, [tid, 0, user >> 16, user, 0, 0, 0, 0])
        return self.run_task(injects)

    def _yield(self):
        """Yield from the program: let the task run, then go on where the
        program was."""
        t = self.task
        if t is None or not (t["state"] == "new" or (
                t["state"] == "blocked" and t["signals"])):
            return
        regs = self._save()
        saved = self.injects, self.icount, self.depth
        self.injects, self.depth = [], 0
        try:
            self.run_task()
        finally:
            self.injects, self.icount, self.depth = saved
            self._restore(regs)

    def _run(self, target, injects):
        """Run from target until the sentinel (("return", DX:AX)), the end
        of the task (("exit", None)) or mmTaskBlock (("block", None))."""
        uc = self.uc
        saved_injects = self.injects
        self.injects = list(injects)
        self.icount = 0
        need_count = self.check_selectors or any(
            i.after_insns is not None or i.on_lock for i in self.injects)
        saved_lock_seen, self._lock_seen = self._lock_seen, False
        added_hook = False
        if need_count and self._code_hook is None:
            self._code_hook = uc.hook_add(UC_HOOK_CODE, self._count)
            uc.ctl_flush_tb()       # code hooks apply to newly translated code
            added_hook = True
        lock_hook = None
        if any(i.on_lock for i in self.injects):
            dgroup = self.rd(SEG_PARA[4], 0, 0x800)
            at = SEG_PARA[4] * 16 + dgroup.find(b"ESFMFIX\0") + 10
            lock_hook = uc.hook_add(UC_HOOK_MEM_WRITE, self._lock_write,
                                    None, at, at + 1)
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
                        # IF is clear, so the interrupt waits: step one
                        # instruction at a time until IF is set again
                        inj.deferred = True
                        inj.started = True
                        waiting = inj
                    continue
                _k, scs, sip = self.stop
                if scs != STUB_PARA:
                    raise EmuError("int 3 at %04x:%04x" % (scs, sip))
                key = self.stubs[sip]
                if key in (("SENTINEL", 0), ("TASK_EXIT", 0)):
                    if waiting is not None:
                        raise EmuError("interrupts still off at the return")
                    if key == ("TASK_EXIT", 0):
                        return "exit", None
                    return "return", (uc.reg_read(UC_X86_REG_DX) << 16) | \
                        uc.reg_read(UC_X86_REG_AX)
                if key == ("MARKER", 0):
                    inj = self._pending_marker.pop()
                    inj.result = uc.reg_read(UC_X86_REG_AX)
                    inj.done = True
                    self.depth -= 1
                    ip = self._pop()
                    cs = self._pop()
                    continue
                nxt = self._handle_stub(key)
                if nxt == "block":
                    if waiting is not None:
                        raise EmuError("interrupts off in mmTaskBlock")
                    return "block", None
                cs, ip = nxt
        finally:
            self.injects = saved_injects
            self._lock_seen = saved_lock_seen
            if lock_hook is not None:
                uc.hook_del(lock_hook)
            if added_hook:
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

    # --- global memory, strings -------------------------------------------

    def galloc(self, size, data=b""):
        """A global block of size bytes at a free paragraph, 0 if none."""
        paras = (max(size, 1) + 15) >> 4
        at = HEAP_PARA
        for p in sorted(self.gblocks):
            if p < HEAP_PARA:
                continue
            if p >= at + paras:
                break
            at = max(at, p + ((self.gblocks[p] + 15) >> 4))
        if at + paras > HEAP_END:
            return 0
        self.gblocks[at] = size
        self.freed.discard(at)
        self.wr(at, 0, bytes(data) + b"\0" * (size - len(data)))
        return at

    def gfree(self, h):
        if h not in self.gblocks:
            raise EmuError("GlobalFree of %04x, not a block" % h)
        if self.plocks.get(h):
            self.freed_locked.append(h)
        size = self.gblocks.pop(h)
        self.freed.add(h)
        self.plocks.pop(h, None)
        self.wires.pop(h, None)
        self.wr(h, 0, b"\xCC" * size)       # catch reads of a freed bank

    def rdstr(self, ptr):
        seg, off = ptr >> 16, ptr & 0xFFFF
        out = bytearray()
        while len(out) < 512:
            c = self.rd(seg, off + len(out), 1)[0]
            if not c:
                break
            out.append(c)
        return out.decode("latin-1")

    def wrstr(self, ptr, text):
        self.wr(ptr >> 16, ptr & 0xFFFF, text.encode("latin-1") + b"\0")

    def _dos(self):
        """INT 21h through DOS3Call: open, seek, read and close files of
        self.files."""
        uc = self.uc
        r = {n: uc.reg_read(DOS_REGS[n]) for n in DOS_REGS}
        ax, flags = r["AX"], uc.reg_read(UC_X86_REG_EFLAGS)
        err = None
        out = {}
        if ax == 0x716C or ax >> 8 == 0x3D:
            if ax == 0x716C and self.lfn is not True:
                out["AX"] = 0x7100
                err = self.lfn is False
            else:
                name = self.rdstr((r["DS"] << 16) | (
                    r["SI"] if ax == 0x716C else r["DX"])).upper()
                self.opens.append(name)
                if name in self.files:
                    h = 5 + len(self.handles)
                    while h in self.handles:
                        h += 1
                    self.handles[h] = [name, 0, bytes(self.files[name])]
                    out["AX"] = h
                else:
                    out["AX"] = 2           # file not found
                    err = True
        elif ax >> 8 in (0x3E, 0x3F, 0x42):
            f = self.handles.get(r["BX"])
            if f is None:
                out["AX"] = 6               # invalid handle
                err = True
            elif ax >> 8 == 0x3E:
                del self.handles[r["BX"]]
            elif ax >> 8 == 0x3F:
                data = f[2][f[1]:f[1] + r["CX"]]
                self.wr(r["DS"], r["DX"], data)
                f[1] += len(data)
                out["AX"] = len(data)
            else:
                pos = (r["CX"] << 16) | r["DX"]
                base = (0, f[1], len(f[2]))[ax & 0xFF]
                f[1] = base + pos
                out["AX"], out["DX"] = f[1] & 0xFFFF, f[1] >> 16
        else:
            raise EmuError("DOS3Call AX=%04x" % ax)
        for n, v in out.items():
            uc.reg_write(DOS_REGS[n], v)
        uc.reg_write(UC_X86_REG_EFLAGS, (flags | 1) if err else (flags & ~1))

    def _handle_stub(self, key):
        uc = self.uc
        # return address of the far call into the stub
        sp = uc.reg_read(UC_X86_REG_SP)
        args_at = sp + 4
        ret_ip, ret_cs = struct.unpack("<HH", uc.mem_read(self._ss_lin(sp),
                                                          4))

        def arg(off, size=2):
            fmt = "<H" if size == 2 else "<I"
            return struct.unpack(fmt, uc.mem_read(
                self._ss_lin(args_at + off), size))[0]

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
        self.calls.append(key)
        if key == ("KERNEL", 102):          # DOS3Call
            self._dos()
            uc.reg_write(UC_X86_REG_SP, sp + 4)
            return ret_cs, ret_ip
        if key == ("KERNEL", 5):            # LocalAlloc(flags, size)
            size, flags = arg(0), arg(2)
            ax = self.heap
            self.heap = (self.heap + size + 15) & ~15
            self.wr(SEG_PARA[4], ax, b"\0" * size)
        elif key == ("KERNEL", 7):
            ax = 0
        elif key == ("KERNEL", 15):         # GlobalAlloc(flags, dwBytes)
            size = arg(0, 4)
            ax = 0 if self.gmem_fail or size > 0xFFF0 else self.galloc(size)
        elif key == ("KERNEL", 17):         # GlobalFree
            self.gfree(arg(0))
        elif key == ("KERNEL", 20):         # GlobalSize
            size = self.gblocks.get(arg(0), 0)
            size = (size + 15) & ~15
            ax, dx = size & 0xFFFF, size >> 16
        elif key == ("KERNEL", 29):         # Yield
            self._yield()
        elif key == ("KERNEL", 36):         # GetCurrentTask
            ax = self.cur_task
        elif key == ("KERNEL", 60):         # FindResource
            rtype, name = arg(0, 4), arg(4, 4)
            ax = 0x0B01 if (rtype, name) == (256, 1234) else 0
        elif key == ("KERNEL", 61):         # LoadResource
            if arg(0) == 0x0B01:
                ax = self.galloc(len(self.bank_res), self.bank_res)
        elif key == ("KERNEL", 62):         # LockResource
            ax, dx = 0, arg(0)
        elif key == ("KERNEL", 63):         # FreeResource
            self.gfree(arg(0))
        elif key == ("KERNEL", 65):         # SizeofResource
            if arg(0) == 0x0B01:
                ax = len(self.bank_res)
        elif key == ("KERNEL", 88):         # lstrcpy(dst, src)
            src, dst = arg(0, 4), arg(4, 4)
            self.wrstr(dst, self.rdstr(src))
            ax, dx = dst & 0xFFFF, dst >> 16
        elif key == ("USER", 471):          # lstrcmpi(s1, s2)
            a, b = self.rdstr(arg(4, 4)).lower(), self.rdstr(arg(0, 4)).lower()
            ax = (a > b) - (a < b)
        elif key == ("KERNEL", 111):        # GlobalWire -> far pointer
            h = arg(0)
            self.wires[h] = self.wires.get(h, 0) + 1
            ax, dx = 0, h
        elif key == ("KERNEL", 112):        # GlobalUnWire
            h = arg(0)
            self.wires[h] = self.wires.get(h, 0) - 1
            ax = 1
        elif key == ("KERNEL", 128):        # GetPrivateProfileString
            fname, size, buf = self.rdstr(arg(0, 4)), arg(4), arg(6, 4)
            default, keyname = self.rdstr(arg(10, 4)), self.rdstr(arg(14, 4))
            app = self.rdstr(arg(18, 4))
            self.ini_reads.append((fname, app, keyname))
            val = self.ini.get((app.lower(), keyname.lower()), default)
            val = val[:size - 1]
            self.wrstr(buf, val)
            ax = len(val)
        elif key == ("KERNEL", 191):        # GlobalPageLock
            h = arg(0)
            self.plocks[h] = self.plocks.get(h, 0) + 1
            ax = self.plocks[h]
        elif key == ("KERNEL", 192):        # GlobalPageUnlock
            h = arg(0)
            self.plocks[h] = max(0, self.plocks.get(h, 0) - 1)
            ax = self.plocks[h]
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
        elif key == ("MMSYSTEM", 602):      # timeSetEvent
            if not self.timer_fail:
                lpfn, user = arg(6, 4), arg(2, 4)
                ax = self._next_timer
                self._next_timer += 1
                self.timers[ax] = ((lpfn >> 16, lpfn & 0xFFFF), user)
        elif key == ("MMSYSTEM", 603):      # timeKillEvent
            self.timers.pop(arg(0), None)
        elif key == ("MMSYSTEM", 900):      # mmTaskCreate
            if self.mmtask_error:
                ax = self.mmtask_error
            else:
                lpfn, lph, inst = arg(8, 4), arg(4, 4), arg(0, 4)
                self.task = {"proc": (lpfn >> 16, lpfn & 0xFFFF),
                             "inst": inst, "state": "new", "signals": 0,
                             "ctx": None, "runs": 0}
                self.tasks_created += 1
                self.wr(lph >> 16, lph & 0xFFFF,
                        struct.pack("<H", WATCH_TASK))
        elif key == ("MMSYSTEM", 902):      # mmTaskBlock
            t = self.task
            if self.cur_task != WATCH_TASK or t is None:
                raise EmuError("mmTaskBlock outside the task")
            if t["signals"]:
                t["signals"] -= 1
            else:
                # the task sleeps here until it's signaled
                uc.reg_write(UC_X86_REG_AX, 0)
                uc.reg_write(UC_X86_REG_SP, sp + 4 + pop)
                t["ctx"] = self._save()
                t["ctx"][8], t["ctx"][9] = ret_cs, ret_ip
                return "block"
        elif key == ("MMSYSTEM", 903):      # mmTaskSignal
            if self.task is not None and arg(0) == WATCH_TASK and \
                    self.task["state"] != "done":
                self.task["signals"] += 1
                ax = 1
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
        """Send DRVM_INIT and DRVM_ENABLE (a devnode arrives), then
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
        """Write a prepared MIDIHDR with data into the stub segment and
        return its far pointer."""
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
        """Call DriverProc(dwDriverID, hDriver, msg, lParam1, lParam2)."""
        seg, off, _f = self.ne.entries[2]
        return self.call((SEG_PARA[seg], off), [
            0, 1, 1, msg, lp1 >> 16, lp1 & 0xFFFF, lp2 >> 16, lp2 & 0xFFFF],
            injects)

    # --- state ------------------------------------------------------------

    def fix_state(self):
        """Counters of the fixed driver (None for the ESS driver), and from
        version 2 the state of the bank file."""
        dgroup = self.rd(SEG_PARA[4], 0, 0x800)
        at = dgroup.find(b"ESFMFIX\0")
        if at < 0:
            return None
        names = ("version", "lock", "head", "tail", "queued", "overflow",
                 "maxdepth", "purged", "qsize")
        st = dict(zip(names, struct.unpack_from("<HHHHIIHHH", dgroup,
                                                at + 8)))
        if st["version"] >= 2:
            names = ("bstate", "bsrc", "blen", "bloads", "bpolls", "bwatch",
                     "bwerr")
            st.update(zip(names, struct.unpack_from("<7H", dgroup, at + 30)))
            path = dgroup[at + 44:at + 44 + 128]
            st["bpath"] = path.split(b"\0")[0].decode("latin-1")
        return st

    def bank(self, size=None):
        """The bank that plays: the block the driver points to (0012h)."""
        sel = self.r16(4, 0x14)
        if size is None:
            size = self.gblocks.get(sel, 0)
        return self.rd(sel, self.r16(4, 0x12), size)

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
