# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Run ES1869.VXD API code in a CPU emulator against a simulated chip.

The LE objects are loaded at fixed addresses with their fixups applied.
INT 20h dynamic links are emulated for the few VMM services the tested
code uses. Every IN/OUT goes to FakeES1869, a small model of the ES1869
ports: mixer index/data, the DSP command channel and the configuration
device with its PnP index and logical device registers.

Needs the unicorn package.
"""

import struct

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INSN,
                     UC_HOOK_INTR)
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_EAX,
                               UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                               UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP,
                               UC_X86_REG_ESP, UC_X86_REG_EIP,
                               UC_X86_REG_EFLAGS)

CLIENT_OFF = {"EDI": 0x00, "ESI": 0x04, "EBP": 0x08, "EBX": 0x10,
              "EDX": 0x14, "ECX": 0x18, "EAX": 0x1C, "EFlags": 0x2C,
              "ES": 0x38, "DS": 0x3C}

OBJ_BASE = 0x10000000
CLIENT = 0x20000000
ADI = 0x21000000
LISTS = 0x22000000
BUFFER = 0x23000000
STUB = 0x24000000
STACK = 0x30000000

VM_SYS = 0x0C001000        # handle of the calling VM (System VM)
VM_DOS = 0x0C002000        # another VM
DEVNODE = 0x00C0FFEE

SVC_MAP_FLAT = 0x0001001C
SVC_LIST_GET_FIRST = 0x000100A3
SVC_LIST_GET_NEXT = 0x000100A4


def obj_base(n):
    return OBJ_BASE + n * 0x100000


class FakeES1869:
    """Port-level model of the parts of the ES1869 the API touches."""

    def __init__(self, base=0x220, cfg=0x250):
        self.base, self.cfg = base, cfg
        self.mixer = [0] * 256
        self.index = 0
        self.id_seq = 0
        self.ctrl = {r: 0 for r in range(0xA0, 0xC0)}
        self.ext_mode = False
        self.pending = []          # DSP command bytes waiting for operands
        self.out = []              # DSP read-data buffer
        self.ports = [0] * 16      # plain audio ports (6, 7, ...)
        self.cfg_index = 0
        self.ldn = 0
        self.pnp = {}              # (ldn or None for card level, reg) -> value
        self.cfg_ports = [0] * 8
        self.log = []

    # -- port reads --
    def inb(self, port):
        v = self._inb(port)
        self.log.append(("in", port, v))
        return v

    def _inb(self, port):
        off = port - self.base
        if off == 4:
            return self.index
        if off == 5:
            if self.index == 0x40:
                seq = [0x18, 0x69, self.cfg >> 8, self.cfg & 0xFF]
                v = seq[self.id_seq % 4]
                self.id_seq += 1
                return v
            return self.mixer[self.index]
        if off == 0x0C:
            return 0x40 if self.out else 0x00   # bit 6: read data ready
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
        self.log.append(("out", port, v))
        off = port - self.base
        if off == 4:
            self.index = v
            self.id_seq = 0
        elif off == 5:
            self.mixer[self.index] = v
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
        elif v == 0xC0 or 0xA0 <= v <= 0xBF:
            self.pending.append(v)


class VxDEmu:
    def __init__(self, le, hw):
        self.le, self.hw = le, hw
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
        uc.mem_map(STACK - 0x10000, 0x10000)
        uc.mem_write(STUB, b"\xf4")
        self.lists = {}
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INTR, self._intr)
        self.services = []

    # list handles: address of a fake list -> list of node addresses
    def set_list(self, handle, nodes):
        self.lists[handle] = nodes

    def write32(self, addr, v):
        self.uc.mem_write(addr, struct.pack("<I", v & 0xFFFFFFFF))

    def read32(self, addr):
        return struct.unpack("<I", self.uc.mem_read(addr, 4))[0]

    def _in(self, uc, port, size, _user):
        return self.hw.inb(port)

    def _out(self, uc, port, size, value, _user):
        self.hw.outb(port, value & 0xFF)

    def _set_zf(self, zf):
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        fl = (fl | 0x40) if zf else (fl & ~0x40)
        self.uc.reg_write(UC_X86_REG_EFLAGS, fl)

    def _intr(self, uc, intno, _user):
        if intno != 0x20:
            raise RuntimeError("unexpected interrupt %#x" % intno)
        eip = uc.reg_read(UC_X86_REG_EIP)
        svc = self.read32(eip) & ~0x8000
        self.services.append(svc)
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
        elif svc == SVC_MAP_FLAT:
            ax = uc.reg_read(UC_X86_REG_EAX) & 0xFFFF
            seg = struct.unpack("<H", uc.mem_read(CLIENT + (ax >> 8), 2))[0]
            off = struct.unpack("<H", uc.mem_read(CLIENT + (ax & 0xFF), 2))[0]
            flat = BUFFER + off if seg == 0x1234 else 0xFFFFFFFF
            uc.reg_write(UC_X86_REG_EAX, flat)
        else:
            raise RuntimeError("VxD service %08X not emulated" % svc)
        uc.reg_write(UC_X86_REG_EIP, eip + 4)

    def call(self, addr, client, vm=VM_SYS):
        """Call a V86/PM API procedure, client is a dict of Client_* values."""
        blob = bytearray(0x48)
        for name, value in client.items():
            size = 2 if name in ("ES", "DS") else 4
            struct.pack_into("<I" if size == 4 else "<H", blob,
                             CLIENT_OFF[name], value)
        self.uc.mem_write(CLIENT, bytes(blob))
        sp = STACK - 0x100
        self.write32(sp, STUB)
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.uc.reg_write(UC_X86_REG_EBP, CLIENT)
        self.uc.reg_write(UC_X86_REG_EBX, vm)
        self.uc.reg_write(UC_X86_REG_EFLAGS, 0x202)
        self.uc.emu_start(addr, STUB, count=200000)
        out = {}
        raw = self.uc.mem_read(CLIENT, 0x48)
        for name, off in CLIENT_OFF.items():
            out[name] = struct.unpack_from("<I", raw, off)[0]
        return out
