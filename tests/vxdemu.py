# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Run ES1869.VXD code in a CPU emulator against a simulated chip.

The LE objects are loaded at fixed addresses with their fixups applied.
INT 20h dynamic links are emulated for the VMM, VPICD, VDMAD and SHELL
services the tested code uses. Every IN/OUT goes to FakeES1869, a small
model of the ES1869 ports: mixer index/data, the DSP command channel, the
FM synthesizer (FakeFM), the power register and the configuration device
with its PnP index and logical device registers.

Machine builds a whole setup: one device instance (ADI) with the extension's
fields, the system VM and two DOS VMs with their per-VM nodes, and ports
trapped per VM as the VMM would. A VM's port access goes through the
driver's trap handler while the port is trapped for that VM, and straight to
the chip otherwise.

Needs the unicorn package.
"""

import struct

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INSN,
                     UC_HOOK_INTR)
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT,
                               UC_X86_INS_CPUID, UC_X86_REG_EAX,
                               UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                               UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP,
                               UC_X86_REG_ESP, UC_X86_REG_EIP,
                               UC_X86_REG_EFLAGS, UC_X86_REG_GDTR,
                               UC_X86_REG_LDTR)

CLIENT_OFF = {"EDI": 0x00, "ESI": 0x04, "EBP": 0x08, "EBX": 0x10,
              "EDX": 0x14, "ECX": 0x18, "EAX": 0x1C, "EFlags": 0x2C,
              "ES": 0x38, "DS": 0x3C}

OBJ_BASE = 0x10000000
CLIENT = 0x20000000
ADI = 0x21000000
LISTS = 0x22000000
BUFFER = 0x23000000
STUB = 0x24000000
PROFILE = 0x24000800       # Get_Profile_String's answers
GDT = 0x25000000           # and the LDT at GDT + 10000h
LDT = 0x25010000
HEAP = 0x26000000
V86 = 0x27000000           # the first megabyte of the calling DOS VM
STACK = 0x30000000
VM_CB = 0x0C000000

VM_SYS = 0x0C001000        # handle of the calling VM (System VM)
VM_DOS = 0x0C002000        # another VM
VM_DOS2 = 0x0C003000
VMS = (VM_SYS, VM_DOS, VM_DOS2)
DEVNODE = 0x00C0FFEE
CB_OFFSET = 0x100          # the driver's per-VM control block area
TSC_MHZ = 100              # the emulated time stamp counter's rate
INT_RDTSC = 0x99           # rdtsc is patched to this interrupt

SVC_GET_CUR_VM = 0x00010001
SVC_GET_SYS_VM = 0x00010003
SVC_TEST_SYS_VM = 0x00010004
SVC_MAP_FLAT = 0x0001001C
SVC_GET_NEXT_VM = 0x0001003B
SVC_GET_SYSTEM_TIME = 0x0001003F
SVC_HEAP_ALLOCATE = 0x0001004F
SVC_HEAP_FREE = 0x00010051
SVC_SIMULATE_IO = 0x00010094
SVC_ENABLE_TRAP = 0x00010098
SVC_DISABLE_TRAP = 0x0001009A
SVC_LIST_DESTROY = 0x0001009C
SVC_LIST_REMOVE = 0x000100A1
SVC_LIST_DEALLOCATE = 0x000100A2
SVC_LIST_GET_FIRST = 0x000100A3
SVC_LIST_GET_NEXT = 0x000100A4
SVC_GET_PROFILE_STRING = 0x000100B3
SVC_GET_SYSTEM_INIT_STATE = 0x00010111
SVC_SHELL_MESSAGE = 0x00170004
SVC_VDMAD_GET_PHYS_COUNT = 0x0004001C
NOOP_SERVICES = {
    0x00030002, 0x00030003, 0x00030004, 0x00030008, 0x00030009,  # VPICD
    0x00040013, 0x00040014,                                      # VDMAD
    0x0017000E,                         # SHELL_CallAtAppyTime
}
SVC_VPICD_STATUS = 0x00030005


def obj_base(n):
    return OBJ_BASE + n * 0x100000


class Clock:
    """Microseconds of emulated time; every chip access takes one."""

    def __init__(self):
        self.us = 1000000

    def advance(self, us):
        self.us += us


class FakeFM:
    """The ESFM as ESFMu models it: emulation mode (OPL3 registers in two
    banks), native mode (11-bit addresses, readback) and the two timers."""

    def __init__(self, clock):
        self.clock = clock
        self.reset()

    def reset(self):
        self.native = False
        self.latch = 0
        self.emu = [[0] * 256, [0] * 256]
        self.nat = [0] * 0x800
        self.t = [0, 0]
        self.ctl = 0
        self.status = 0
        self.start = [0, 0]
        self.writes = []            # (mode, register, value)

    def _timers(self):
        for i, (bit, unit, mask, flag) in enumerate(
                ((1, 80, 0x40, 0x40), (2, 320, 0x20, 0x20))):
            if self.ctl & bit and not self.ctl & mask:
                period = (256 - self.t[i]) * unit
                if self.clock.us - self.start[i] >= period:
                    self.status |= 0x80 | flag

    def _ctl(self, v):
        if v & 0x80:
            self.status = 0
            return
        for i, bit in enumerate((1, 2)):
            if v & bit and not self.ctl & bit:
                self.start[i] = self.clock.us
        self.ctl = v & 0x63

    def _emu(self, reg, v):
        self.writes.append(("emu", reg, v))
        bank, r = reg >> 8, reg & 0xFF
        if r in (2, 3):
            self.t[r - 2] = v
        elif reg == 4:
            self._ctl(v)
            return
        self.emu[bank][r] = v
        if reg == 0x105 and v & 0x80:
            self.native = True

    def _nat(self, reg, v):
        self.writes.append(("nat", reg, v))
        if reg in (0x402, 0x403):
            self.t[reg - 0x402] = v
        elif reg == 0x404:
            self._ctl(v)
        else:
            self.nat[reg] = v

    def write(self, off, v):
        if self.native:
            if off == 0:
                self.native = False
                self.latch = v
            elif off == 1:
                self._nat(self.latch & 0x7FF, v)
            elif off == 2:
                self.latch = (self.latch & 0xFF00) | v
            else:
                self.latch = (self.latch & 0xFF) | (v << 8)
        elif off == 0:
            self.latch = v
        elif off == 2:
            self.latch = v | 0x100
        else:
            self._emu(self.latch & 0x1FF, v)

    def read(self, off):
        if off == 0:
            self._timers()
            return self.status
        if off == 1:
            if not self.native:
                return 0
            reg = self.latch & 0x7FF
            v = self.nat[reg]
            if reg < 0x240 and reg & 7 == 0:
                v = (v & ~0x10) | ((v >> 2) & 0x10)
            elif 0x240 <= reg < 0x254:
                v &= 3
            return v
        return 0xFF

    def key_on(self, bank, channel):
        return bool(self.emu[bank][0xB0 + channel] & 0x20)


class FakeES1869:
    """Port-level model of the parts of the ES1869 the driver touches."""

    def __init__(self, base=0x220, cfg=0x250, alias=0x388, clock=None):
        self.base, self.cfg, self.alias = base, cfg, alias
        self.clock = clock or Clock()
        self.fm = FakeFM(self.clock)
        self.mixer = [0] * 256
        self.index = 0
        self.id_seq = 0
        self.id_cfg = cfg          # what the identification sequence says
        self.ctrl = {r: 0 for r in range(0xA0, 0xC0)}
        self.ext_mode = False
        self.pending = []          # DSP command bytes waiting for operands
        self.out = []              # DSP read-data buffer
        self.busy = False          # Audio_Base+Ch bit 7 stuck
        self.ports = [0] * 16      # plain audio ports (6, 7, ...)
        self.cfg_index = 0
        self.ldn = 0
        self.pnp = {}              # (ldn or None for card level, reg) -> value
        self.cfg_ports = [0] * 8
        self.fm_resets = 0
        self.log = []

    def _fm_off(self, port):
        off = port - self.base
        if 0 <= off < 4:
            return off
        if off in (8, 9):
            return off - 8
        if self.alias <= port < self.alias + 4:
            return port - self.alias
        return None

    # -- port reads --
    def inb(self, port):
        self.clock.advance(1)
        v = self._inb(port)
        self.log.append(("in", port, v))
        return v

    def _inb(self, port):
        fm = self._fm_off(port)
        if fm is not None:
            return self.fm.read(fm)
        off = port - self.base
        if off == 4:
            return self.index
        if off == 5:
            if self.index == 0x40:
                seq = [0x18, 0x69, self.id_cfg >> 8, self.id_cfg & 0xFF]
                v = seq[self.id_seq % 4]
                self.id_seq += 1
                return v
            return self.mixer[self.index]
        if off == 0x0C:
            return (0x80 if self.busy else 0) | (0x40 if self.out else 0)
        if off == 0x0A:
            return self.out.pop(0) if self.out else 0xFF
        if off == 0x0E:
            return 0x80 if self.out else 0x00
        if 0 <= off < 16:
            return self.ports[off]
        if port == self.cfg:
            return self.cfg_index
        if port == self.cfg + 1:
            if self.cfg_index == 0x07:
                return self.ldn
            key = (None if self.cfg_index < 0x30 else self.ldn, self.cfg_index)
            return self.pnp.get(key, 0)
        if self.cfg < port < self.cfg + 8:
            return self.cfg_ports[port - self.cfg]
        return 0xFF

    # -- port writes --
    def outb(self, port, v):
        self.clock.advance(1)
        self.log.append(("out", port, v))
        fm = self._fm_off(port)
        if fm is not None:
            self.fm.write(fm, v)
            return
        off = port - self.base
        if off == 4:
            self.index = v
            self.id_seq = 0
        elif off == 5:
            self.mixer[self.index] = v
        elif off == 6:
            if self.ports[6] & 1 and not v & 1:     # reset released
                self.ext_mode = False
                self.pending = []
                self.out = [0xAA]
            self.ports[6] = v
        elif off == 7:
            if v & 0x20:
                self.fm.reset()                     # FM held in reset
                self.fm_resets += 1
            self.ports[7] = v
        elif off == 0x0C:
            self._dsp(v)
        elif 0 <= off < 16:
            self.ports[off] = v
        elif port == self.cfg:
            self.cfg_index = v
        elif port == self.cfg + 1:
            if self.cfg_index == 0x07:
                self.ldn = v
            else:
                key = (None if self.cfg_index < 0x30 else self.ldn,
                       self.cfg_index)
                self.pnp[key] = v
        elif self.cfg < port < self.cfg + 8:
            self.cfg_ports[port - self.cfg] = v

    def _dsp(self, v):
        if self.pending:
            cmd = self.pending.pop(0)
            if cmd == 0xC0:
                if self.ext_mode and 0xA0 <= v <= 0xBF:
                    self.out.append(self.ctrl[v])
            elif 0xA0 <= cmd <= 0xBF and self.ext_mode:
                self.ctrl[cmd] = v
            return
        if v == 0xC6:
            self.ext_mode = True
        elif v == 0xC7:
            self.ext_mode = False
        elif v == 0xC0 or 0xA0 <= v <= 0xBF:
            self.pending.append(v)

    def dsp_writes(self):
        return [v for op, p, v in self.log
                if op == "out" and p == self.base + 0x0C]


def desc(base, limit, access):
    """a GDT descriptor: access 0F2h = present, DPL 3, writable data"""
    return struct.pack("<HHBBBB", limit & 0xFFFF, base & 0xFFFF,
                       (base >> 16) & 0xFF, access,
                       0x40 | ((limit >> 16) & 0xF), base >> 24)


class VxDEmu:
    def __init__(self, le, hw, syms=None):
        self.le, self.hw = le, hw
        self.syms = syms or {}
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        for o in le.objects:
            size = (len(o.data) + 0xFFF) & ~0xFFF
            uc.mem_map(obj_base(o.index), max(size, 0x1000))
            uc.mem_write(obj_base(o.index), bytes(o.data))
        for f in le.fixups:
            src = obj_base(f.obj) + f.off
            tgt = obj_base(f.tobj) + f.toff
            val = tgt if f.type == 7 else tgt - (src + 4)
            uc.mem_write(src, struct.pack("<I", val & 0xFFFFFFFF))
        for addr in (CLIENT, ADI, LISTS, BUFFER, STUB):
            uc.mem_map(addr, 0x1000)
        uc.mem_map(GDT, 0x20000)
        uc.mem_map(HEAP, 0x100000)
        uc.mem_map(V86, 0x120000)
        uc.mem_map(VM_CB, 0x10000)
        uc.mem_map(STACK - 0x10000, 0x10000)
        uc.mem_write(STUB, b"\xf4")
        self.heap_next = HEAP
        self.heap = {}             # address -> size of live blocks
        self.lists = {}
        self.trap_off = set()      # (vm, port) with trapping disabled
        self.messages = []
        self.dma_count = 0x2B10         # VDMAD_Get_Phys_Count's next answer
        self.has_tsc = True
        # SYSTEM.INI: {(section, key): value}, as VMM finds it while
        # Windows starts (VMM_GetSystemInitState below 40000000h)
        self.ini = {}
        self.init_state = 0x20000000
        self.use32 = set()         # VMs whose protected mode code is 32-bit
        self.selectors = {}
        self.add_selector(0x1234, BUFFER, 0xFFF)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INSN, self._cpuid, None, 1, 0, UC_X86_INS_CPUID)
        uc.hook_add(UC_HOOK_INTR, self._intr)
        self.services = []
        rdtsc = self.syms.get("ESSREG_Rdtsc")
        if rdtsc is not None:
            uc.mem_write(rdtsc, bytes([0xCD, INT_RDTSC]))

    @property
    def clock(self):
        return self.hw.clock

    def add_selector(self, sel, base, limit, access=0xF2):
        """a protected mode selector the client may pass, in the LDT
        (like a Win16 program's) or the GDT"""
        self.selectors[sel] = (base, limit)
        table = LDT if sel & 4 else GDT
        self.uc.mem_write(table + (sel & ~7), desc(base, limit, access))
        self.uc.reg_write(UC_X86_REG_GDTR, (0, GDT, 0xFFFF, 0))
        self.uc.reg_write(UC_X86_REG_LDTR, (0x28, LDT, 0xFFFF, 0x82))

    # list handles: address of a fake list -> list of node addresses
    def set_list(self, handle, nodes):
        self.lists[handle] = nodes

    def write32(self, addr, v):
        self.uc.mem_write(addr, struct.pack("<I", v & 0xFFFFFFFF))

    def read32(self, addr):
        return struct.unpack("<I", self.uc.mem_read(addr, 4))[0]

    def read8(self, addr):
        return self.uc.mem_read(addr, 1)[0]

    def _in(self, uc, port, size, _user):
        return self.hw.inb(port)

    def _out(self, uc, port, size, value, _user):
        self.hw.outb(port, value & 0xFF)

    def _cpuid(self, uc, _user):
        uc.reg_write(UC_X86_REG_EAX, 0x543)
        uc.reg_write(UC_X86_REG_EBX, 0)
        uc.reg_write(UC_X86_REG_ECX, 0)
        uc.reg_write(UC_X86_REG_EDX, 0x10 if self.has_tsc else 0)
        return 1

    def _set_zf(self, zf):
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        fl = (fl | 0x40) if zf else (fl & ~0x40)
        self.uc.reg_write(UC_X86_REG_EFLAGS, fl)

    def _set_cf(self, cf):
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        self.uc.reg_write(UC_X86_REG_EFLAGS, (fl | 1) if cf else (fl & ~1))

    def cstr(self, addr):
        out = bytearray()
        while self.read8(addr + len(out)):
            out.append(self.read8(addr + len(out)))
        return out.decode("latin-1")

    def _profile_string(self, uc):
        """Get_Profile_String: ESI = section (0: [386Enh]), EDI = key;
        CF clear and EDX = the value if found. Like VMM, only while
        Windows starts: afterwards the service is gone"""
        if self.init_state >= 0x40000000:
            raise RuntimeError("Get_Profile_String after initialization")
        esi = uc.reg_read(UC_X86_REG_ESI)
        section = self.cstr(esi) if esi else "386Enh"
        key = self.cstr(uc.reg_read(UC_X86_REG_EDI))
        found = {(s.lower(), k.lower()): v for (s, k), v in self.ini.items()}
        value = found.get((section.lower(), key.lower()))
        if value is None:
            self._set_cf(True)
            return
        uc.mem_write(PROFILE, value.encode("latin-1") + b"\0")
        uc.reg_write(UC_X86_REG_EDX, PROFILE)
        self._set_cf(False)

    def _arg(self, n):
        return self.read32(self.uc.reg_read(UC_X86_REG_ESP) + 4 * n)

    def _intr(self, uc, intno, _user):
        if intno == INT_RDTSC:
            tsc = self.clock.us * TSC_MHZ
            uc.reg_write(UC_X86_REG_EAX, tsc & 0xFFFFFFFF)
            uc.reg_write(UC_X86_REG_EDX, tsc >> 32)
            return
        if intno != 0x20:
            raise RuntimeError("unexpected interrupt %#x" % intno)
        eip = uc.reg_read(UC_X86_REG_EIP)
        svc = self.read32(eip) & ~0x8000
        jump = self.read32(eip) & 0x8000
        self.services.append(svc)
        ebx = uc.reg_read(UC_X86_REG_EBX)
        if svc == SVC_LIST_GET_FIRST:
            nodes = self.lists.get(uc.reg_read(UC_X86_REG_ESI), [])
            uc.reg_write(UC_X86_REG_EAX, nodes[0] if nodes else 0)
            self._set_zf(not nodes)
        elif svc == SVC_LIST_GET_NEXT:
            nodes = self.lists.get(uc.reg_read(UC_X86_REG_ESI), [])
            cur = uc.reg_read(UC_X86_REG_EAX)
            nxt = nodes[nodes.index(cur) + 1] \
                if cur in nodes and nodes.index(cur) + 1 < len(nodes) else 0
            uc.reg_write(UC_X86_REG_EAX, nxt)
            self._set_zf(nxt == 0)
        elif svc == SVC_LIST_REMOVE:
            nodes = self.lists.get(uc.reg_read(UC_X86_REG_ESI), [])
            node = uc.reg_read(UC_X86_REG_EAX)
            if node in nodes:
                nodes.remove(node)
        elif svc == SVC_LIST_DEALLOCATE:
            pass
        elif svc == SVC_LIST_DESTROY:
            self.lists.pop(uc.reg_read(UC_X86_REG_ESI), None)
        elif svc == SVC_MAP_FLAT:
            uc.reg_write(UC_X86_REG_EAX, self._map_flat(
                uc.reg_read(UC_X86_REG_EAX) & 0xFFFF))
        elif svc == SVC_TEST_SYS_VM:
            self._set_zf(ebx == VM_SYS)
        elif svc == SVC_GET_SYS_VM:
            uc.reg_write(UC_X86_REG_EBX, VM_SYS)
        elif svc == SVC_GET_CUR_VM:
            uc.reg_write(UC_X86_REG_EBX, self.current_vm)
        elif svc == SVC_GET_NEXT_VM:
            uc.reg_write(UC_X86_REG_EBX, VMS[(VMS.index(ebx) + 1) % len(VMS)])
        elif svc == SVC_GET_SYSTEM_TIME:
            uc.reg_write(UC_X86_REG_EAX, self.clock.us // 1000)
        elif svc == SVC_GET_SYSTEM_INIT_STATE:
            uc.reg_write(UC_X86_REG_EAX, self.init_state)
            uc.reg_write(UC_X86_REG_ECX, 0)
        elif svc == SVC_GET_PROFILE_STRING:
            self._profile_string(uc)
        elif svc == SVC_HEAP_ALLOCATE:
            size, flags = self._arg(0), self._arg(1)
            addr = self.heap_next
            self.heap_next += (size + 15) & ~15
            self.uc.mem_write(addr, bytes(size) if flags & 1 else
                              b"\xa5" * size)
            self.heap[addr] = size
            uc.reg_write(UC_X86_REG_EAX, addr)
        elif svc == SVC_HEAP_FREE:
            addr = self._arg(0)
            if addr not in self.heap:
                raise RuntimeError("_HeapFree of %08X" % addr)
            del self.heap[addr]
            uc.reg_write(UC_X86_REG_EAX, 1)
        elif svc == SVC_ENABLE_TRAP:
            self.trap_off.discard((ebx, uc.reg_read(UC_X86_REG_EDX) & 0xFFFF))
        elif svc == SVC_DISABLE_TRAP:
            self.trap_off.add((ebx, uc.reg_read(UC_X86_REG_EDX) & 0xFFFF))
        elif svc == SVC_SHELL_MESSAGE:
            self.messages.append(ebx)
        elif svc == SVC_VPICD_STATUS:
            uc.reg_write(UC_X86_REG_ECX, 0)
        elif svc == SVC_VDMAD_GET_PHYS_COUNT:
            # a transfer that plays: the count goes down on every look
            self.dma_count = (self.dma_count - 0x123) & 0xFFFF
            uc.reg_write(UC_X86_REG_ECX, self.dma_count)
        elif svc in NOOP_SERVICES:
            pass
        else:
            raise RuntimeError("VxD service %08X not emulated" % svc)
        if jump:
            # VxDJmp: the service returns to our caller
            esp = uc.reg_read(UC_X86_REG_ESP)
            uc.reg_write(UC_X86_REG_EIP, self.read32(esp))
            uc.reg_write(UC_X86_REG_ESP, esp + 4)
        else:
            uc.reg_write(UC_X86_REG_EIP, eip + 4)

    def _map_flat(self, ax):
        def client16(off):
            return struct.unpack("<H", self.uc.mem_read(CLIENT + off, 2))[0]
        seg = client16(ax >> 8)
        if ax & 0xFF == 0xFF:
            off = 0
        elif self.client_vm in self.use32:
            off = self.read32(CLIENT + (ax & 0xFF))
        else:
            off = client16(ax & 0xFF)
        if self.read32(CLIENT + CLIENT_OFF["EFlags"]) & 0x20000:
            return (V86 + seg * 16 + (off & 0xFFFF)) & 0xFFFFFFFF
        if seg not in self.selectors:
            return 0xFFFFFFFF
        return (self.selectors[seg][0] + off) & 0xFFFFFFFF

    client_vm = VM_SYS
    current_vm = VM_SYS

    def run(self, addr, regs, count=5000000):
        """call addr with the given registers until it returns"""
        sp = STACK - 0x100
        self.write32(sp, STUB)
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.uc.reg_write(UC_X86_REG_EFLAGS, 0x202)
        names = {"EAX": UC_X86_REG_EAX, "EBX": UC_X86_REG_EBX,
                 "ECX": UC_X86_REG_ECX, "EDX": UC_X86_REG_EDX,
                 "ESI": UC_X86_REG_ESI, "EDI": UC_X86_REG_EDI,
                 "EBP": UC_X86_REG_EBP}
        for name, value in regs.items():
            self.uc.reg_write(names[name], value & 0xFFFFFFFF)
        self.uc.emu_start(addr, STUB, count=count)
        if self.uc.reg_read(UC_X86_REG_EIP) != STUB:
            raise RuntimeError("didn't return (EIP %08X)"
                               % self.uc.reg_read(UC_X86_REG_EIP))
        out = {name: self.uc.reg_read(r) for name, r in names.items()}
        out["EFlags"] = self.uc.reg_read(UC_X86_REG_EFLAGS)
        return out

    def call(self, addr, client, vm=VM_SYS):
        """Call a V86/PM API procedure, client is a dict of Client_* values."""
        blob = bytearray(0x48)
        for name, value in client.items():
            size = 2 if name in ("ES", "DS") else 4
            struct.pack_into("<I" if size == 4 else "<H", blob,
                             CLIENT_OFF[name], value)
        self.uc.mem_write(CLIENT, bytes(blob))
        self.client_vm = self.current_vm = vm
        self.run(addr, {"EBP": CLIENT, "EBX": vm})
        out = {}
        raw = self.uc.mem_read(CLIENT, 0x48)
        for name, off in CLIENT_OFF.items():
            out[name] = struct.unpack_from("<I", raw, off)[0]
        return out


def load_syms(elf, le):
    """symbol name -> emulated address (or value, for constants)"""
    secobj = {}
    order = [o.name_str for o in le.objects]
    for s in elf.sections:
        if s.name in order:
            secobj[s.index] = order.index(s.name) + 1
    syms = {}
    for sym in elf.symbols:
        if not sym.name:
            continue
        if sym.shndx in secobj:
            syms[sym.name] = obj_base(secobj[sym.shndx]) + sym.value
        elif sym.shndx == 0xFFF1:
            syms[sym.name] = sym.value
    return syms


# ADI fields (src/vxd/adi.inc)
ADI_FLAGS, ADI_AUDIO, ADI_FM, ADI_ALIAS, ADI_MPU = 0x04, 0x06, 0x08, 0x0A, 0x0C
ADI_DSP_OWNER, ADI_DSP_LAST = 0x35, 0x39
ADI_FM_OWNER, ADI_FM_LAST = 0x3D, 0x41
ADI_MPU_OWNER = 0x45
ADI_DEVNODE = 0x55


class Machine:
    """One ES1869 (ADI at ADI), the system VM and two DOS VMs."""

    def __init__(self, le, elf, ext=True):
        self.syms = load_syms(elf, le)
        self.ext = ext
        self.hw = FakeES1869()
        self.emu = VxDEmu(le, self.hw, self.syms)
        e, s = self.emu, self.syms
        e.write32(s["ADI_List"], 0x5000)
        e.set_list(0x5000, [LISTS + 0x100])
        e.write32(LISTS + 0x100, ADI)
        e.write32(s["AUDDRV_Globals"], CB_OFFSET)
        e.write32(s["D1_02B6"], STUB)          # DOSMGR's service, chained to
        adi = bytearray(0x200)
        struct.pack_into("<HHHhH", adi, ADI_FLAGS, 0x0000, 0x220, 0x220,
                         0x388, 0x330)
        struct.pack_into("<I", adi, 0x2D, 0x7000)   # VPICD handles
        struct.pack_into("<I", adi, 0x31, 0x7001)
        struct.pack_into("<I", adi, ADI_DEVNODE, DEVNODE)
        e.uc.mem_write(ADI, bytes(adi))
        # one node per VM: [0] message bits, [4] ADI, [30h] virtual FM chip
        self.nodes = {}
        for i, vm in enumerate(VMS):
            node = LISTS + 0x200 + 0x40 * i
            e.uc.mem_write(node, bytes(0x40))
            e.write32(node + 4, ADI)
            e.write32(vm + CB_OFFSET, 0x6000 + i)
            e.set_list(0x6000 + i, [node])
            self.nodes[vm] = node
        # the trap handlers, as ADI_Create installs them
        self.handlers = {}
        table = s["D6_02D2"]
        for i in range(16):
            self.handlers[0x220 + i] = e.read32(table + 2 + 6 * i + 2)
        table = s["D6_0334"]
        for i in range(4):
            self.handlers[0x388 + i] = e.read32(table + 2 + 6 * i + 2)
        table = s["D6_034E"]
        for i in range(2):
            self.handlers[0x330 + i] = e.read32(table + 2 + 6 * i + 2)
        self.hw.mixer[0x7F] = 0x01        # ES1869.DRV: I2S has the music DAC
        self.hw.mixer[0x36] = 0x00        # and IIS is muted
        self.hw.pnp[(1, 0x60)] = 0x02     # logical device 1 at 220h
        self.hw.pnp[(1, 0x61)] = 0x20

    # -- the ADI --
    def adi32(self, off):
        return self.emu.read32(ADI + off)

    def adi8(self, off):
        return self.emu.read8(ADI + off)

    def set_owner(self, off, vm):
        self.emu.write32(ADI + off, vm)

    def vfm(self, vm):
        return self.emu.read32(self.nodes[vm] + self.syms.get("NODE_VFM",
                                                              0x30))

    # -- what the VMs do --
    def io(self, vm, port, value=None):
        """a byte IN (value None) or OUT by a VM"""
        self.hw.clock.advance(1)
        if (vm, port) in self.emu.trap_off:
            if value is None:
                return self.hw.inb(port)
            self.hw.outb(port, value)
            return None
        self.emu.current_vm = vm
        out = self.emu.run(self.handlers[port], {
            "EBX": vm, "ESI": ADI, "EDX": port,
            "ECX": 0 if value is None else 4,
            "EAX": 0xFFFFFFFF if value is None else value,
            "EBP": CLIENT})
        return None if value is not None else out["EAX"] & 0xFF

    def inb(self, vm, port):
        return self.io(vm, port)

    def outb(self, vm, port, value):
        self.io(vm, port, value)

    def opl(self, vm, reg, value, base=0x388):
        """an OPL register write the AdLib way: address, 6 status reads,
        data, 35 status reads"""
        self.outb(vm, base + (2 if reg & 0x100 else 0), reg & 0xFF)
        for _ in range(6):
            self.inb(vm, base)
        self.outb(vm, base + 1, value)
        for _ in range(35):
            self.inb(vm, base)

    def api(self, vm, fn, **regs):
        client = {"EDX": fn, "ECX": DEVNODE}
        client.update(regs)
        out = self.emu.call(self.syms["AUDDRV_API_Proc"], client, vm)
        return out, bool(out["EFlags"] & 1)

    def program_end(self, vm):
        """DOSMGR_End_V86_App: a program in the VM ended"""
        self.emu.current_vm = vm
        self.emu.run(self.syms["L1_04B8"], {"EBX": vm})

    def vm_close(self, vm):
        """VM_Not_Executeable"""
        self.emu.current_vm = vm
        self.emu.run(self.syms["AUDDRV_Control"], {"EAX": 0x0B, "EBX": vm})

    def start(self, ini, state=0x20000000):
        """Sys_Dynamic_Device_Init with SYSTEM.INI as in ini, {(section,
        key): value}; ESS's own init is left out (this setup is made by
        hand), so it only returns"""
        self.emu.ini = dict(ini)
        self.emu.init_state = state
        self.emu.uc.mem_write(self.syms["AUDDRV_Dynamic_Init"], b"\xF8\xC3")
        out = self.emu.run(self.syms["AUDDRV_Control"],
                           {"EAX": 0x1B, "EBX": VM_SYS})
        return not out["EFlags"] & 1

    def opts(self):
        """the settings the extension runs with (ESSREG_Opts)"""
        return struct.unpack("<H", self.emu.uc.mem_read(
            self.syms["ESSREG_Opts"], 2))[0]
