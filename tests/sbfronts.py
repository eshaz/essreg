# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The fronts as sbgame cards: esssb16.com and ES1869.VXD's front, each in
the CPU emulator in front of the ES1869 model (esschip).

TSRCard installs the TSR under QPIEMU, EMM386 or HDPMI32i, and sends the
game's accesses of the ports the memory manager traps through the front;
the others reach the chip and the DMA controllers directly, as on the
machine, but under EMM386, whose own traps of the DMA controllers keep
what the game programs until it unmasks the channel (emm386dma), while
the TSR's accesses at ring 0 reach the 8237 itself. Time passes on the
chip, which stops at a rising edge of the game's interrupt so that the
game's handler runs then. What the game hears is what both DACs played;
where Audio 2 has an 8-bit channel, the 16-bit transfers play there and
the 8-bit ones on Audio 1.

VxDCard puts the game in a DOS box of Windows with the extended
ES1869.VXD (vxdemu). Its accesses of the chip's ports go to the VxD's trap
handlers, as ESS's code installs them, until the VxD untraps a port for
it, and its accesses of the DMA controllers go to VDMAD, modelled here: the
VM's virtual controllers, the callbacks of the channels' owners, and the
physical controllers, the chip's, as VDMAD_Default_Handler programs them.
The chip's interrupt runs ESS's hardware interrupt procedure, which
requests the VM's virtual interrupt; then the game's handler runs, and its
EOI goes to the VxD's EOI procedure.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import build_esssb16  # noqa: E402
from dma8237 import DMA8237, PORT_PAGE  # noqa: E402
from emm386dma import EMM386DMA  # noqa: E402
from esschip import ES1869Audio  # noqa: E402
from sbgame import Card, program_dma, write  # noqa: E402
from tsremu import Chip, Machine, PMMachine  # noqa: E402

_BUILD = {}

# the front's flags: esssb16.asm's F_*, essext.asm's S16_*
F_ESS, F_EXT, F_AUTO, F_RUN, F_16 = 0x01, 0x02, 0x04, 0x08, 0x10
F_LAST, F_PAUSE = 0x20, 0x40

# the levels a DOS mixer program left: master 3Ah, muted on the right,
# voice ABh, FM CDh, CD 12h, line 34h, mic 50h, PC speaker 6, the record
# source line; and the chip's own registers a mixer reset would clear
DOS_MIXER = {0x60: 0x3A, 0x62: 0x7A, 0x14: 0xAB, 0x36: 0xCD, 0x38: 0x12,
             0x3E: 0x34, 0x1A: 0x55, 0x3C: 0x06, 0x1C: 0x16, 0x50: 0x0C,
             0x52: 0x2A, 0x71: 0x0E, 0x7C: 0x99, 0x0E: 0x22, 0x00: 0x77}

# A1h and A2h for the rate generator's value v (v * 44100 / 248 Hz), as
# ES1869.DRV picks them for a rate: the 397.7 kHz clock up to 22050 Hz, the
# 795.5 kHz one above, and the filter's divider from ESS's tables for the
# fast clock's divisors 18 to 35, else 7.16 MHz over 35.9 times the rate
FILTER_STEREO = (3, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9)
FILTER_MONO = (3, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10)


def half_up(x):
    return int(x + 0.5)


def clocks(v, stereo):
    rate = v * 44100 / 248
    if v <= 124:
        d = half_up(397700 / rate)
        a1 = 128 - d
    else:
        d = half_up(795500 / rate)
        a1 = 256 - d
    if v > 124 and 18 <= d <= 35:
        n = (FILTER_STEREO if stereo else FILTER_MONO)[d - 18]
    else:
        n = half_up(1122.5 / v)
    return a1, (256 - n) & 0xFF


def written(chip):
    """the chip's registers A0h-BFh as the last commands wrote them"""
    return {c[0]: c[1] for c in chip.commands
            if 0xA0 <= c[0] <= 0xBF and len(c) == 2}


def play16(c, cmd=0xB6, mode=0x30, samples=2048, rate=44100, ch=5,
           addr=0x24680, auto=True):
    """a game's 16-bit transfer: the channel, the rate, the command"""
    program_dma(c, ch, addr, samples * 2 * (2 if mode & 0x20 else 1),
                auto=auto)
    write(c, 0x41, rate >> 8, rate & 0xFF)
    write(c, cmd, mode, (samples - 1) & 0xFF, (samples - 1) >> 8)


def tsr_build():
    if "image" not in _BUILD:
        _BUILD["image"], _BUILD["syms"] = build_esssb16.build()
    return _BUILD["image"], _BUILD["syms"]


def vxd_build():
    """the extended ES1869.VXD, built once: (LEFile, its ELF object)"""
    if "vxd" not in _BUILD:
        import build_vxd
        from retools import elf32
        from retools.le import LEFile
        tmp = _BUILD["vxd_tmp"] = tempfile.TemporaryDirectory()
        data, _o, _f = build_vxd.build(True, os.path.join(tmp.name, "ext.vxd"),
                                       tmp.name)
        elf = elf32.read_elf(os.path.join(tmp.name, "es1869-ext.o"))
        _BUILD["vxd"] = (LEFile(data), elf)
    return _BUILD["vxd"]


class TSRChip(ES1869Audio, Chip):
    """the chip model with the TSR emulator's DMA controllers"""

    def __init__(self, irq=5, dma8=1, dma2=None):
        Chip.__init__(self, irq=irq, dma8=dma8)
        self.audio_init(irq=irq, dma1=dma8, dma2=dma2)

    def outb(self, port, v):
        if self.dmac.handles(port):     # an access takes its microsecond
            self.catch_up()
            self.clock.advance(1)
        super().outb(port, v)


class _GameDMA:
    """the DMA controllers as EMM386 programs them for the game: through
    the chip, so that each access takes its time"""

    def __init__(self, chip):
        self.chip = chip

    def handles(self, port):
        return self.chip.dmac.handles(port)

    @property
    def ff(self):
        return self.chip.dmac.ff

    def outb(self, port, v):
        self.chip.outb(port, v)

    def inb(self, port):
        return self.chip.inb(port)


class TSRCard(Card):
    """the game on esssb16.com: path "qpi" (QPIEMU), "emm" (EMM386 at ring
    0) or "pm" (a protected-mode game under HDPMI32i); `old_callback`: a
    QPI callback was there before the program's; the chip's mixer as
    `mixer` has it before the program is installed; `emm386` the keywords
    of EMM386's DMA translation (emm386dma)"""

    def __init__(self, path="qpi", env="BLASTER=A220 I5 D1 H5 P330 T6",
                 tail="", irq=5, dma8=1, dma2=None, old_callback=True,
                 mixer=None, emm386=None):
        super().__init__()
        self.chip = TSRChip(irq, dma8, dma2)
        for reg, v in (mixer or {}).items():
            self.chip.mixer[reg] = v
        image, syms = tsr_build()
        self.m = Machine(image, syms, self.chip, env)
        self.path = path
        self.pm = None
        self.emm = None
        if path == "emm":
            self.m.install(tail=tail, emm386=(4, 95), noems=True)
            self.m.path = "emm"
            self.emm = EMM386DMA(_GameDMA(self.chip), **(emm386 or {}))
        elif path == "pm":
            self.m.install(tail=tail, qpiemu=True, dpmi=True,
                           old_callback=old_callback)
            self.pm = PMMachine(self.m)
            self.pm.install(self.m)
            self.m.exit = ("tsr", self.m.kept)
        else:
            self.m.install(tail=tail, qpiemu=True, old_callback=old_callback)
        if not self.m.exit or self.m.exit[0] != "tsr":
            raise AssertionError("not resident: %r\n%s"
                                 % (self.m.exit, self.m.output))
        self.pm_ports = set()
        if self.pm is not None:         # the ranges of HDPMI32i's function 6
            for fn, r in self.pm.vendor_calls:
                if fn == 6:
                    first = r["esi"] & 0xFFFF
                    self.pm_ports.update(range(first, first + r["edi"]))

    def flags(self):
        """the front's F_* flags"""
        return self.m.flags()

    def trapped(self, port):
        if self.path == "pm":
            return port in self.pm_ports
        if self.path == "qpi":
            return port in self.m.qpi_trapped
        return any(not t.get("failed") and t["first"] <= port <= t["last"]
                   for t in self.m.emm_traps)

    def port_in(self, port):
        if not self.trapped(port):
            if self.emm is not None and self.emm.handles(port):
                return self.emm.inb(port)
            return self.chip.inb(port)
        if self.pm is not None:
            return self.pm.hdpmi_io(port)
        return self.m.inp(port)

    def port_out(self, port, v):
        if not self.trapped(port):
            if self.emm is not None and self.emm.handles(port):
                self.emm.outb(port, v)
            else:
                self.chip.outb(port, v)
        elif self.pm is not None:
            self.pm.hdpmi_io(port, v)
        else:
            self.m.out(port, v)

    def tick(self, us):
        return self.chip.advance(us), len(self.chip.irq_edges)

    def record_from(self, source):
        self.chip.source = source

    @property
    def audio2(self):
        """the TSR plays 16-bit sound on Audio 2"""
        return self.m.data8("dma2") < 4

    @property
    def played(self):
        return self.chip.heard

    def dac(self, kind):
        return self.chip.played2 if kind == 16 and self.audio2 else \
            self.chip.played

    @property
    def midi_out(self):
        return self.chip.midi_out

    def poke(self, addr, data):
        self.m.put(addr, data)

    def peek(self, addr, n):
        return self.m.mem(addr, n)


class VGA:
    """a VGA in a text mode, as essprobe looks at it: the DAC's 256 entries
    of 6 bits (3C7h, 3C8h, 3C9h, three reads or writes an entry), and
    3DAh bit 3, the vertical retrace, 400 us in every 70 Hz frame"""

    PERIOD, RETRACE = 14286, 400
    PORTS = (0x3C7, 0x3C8, 0x3C9, 0x3DA)

    def __init__(self, clock):
        self.clock = clock
        self.dac = [[(i * 3 + c) & 0x3F for c in range(3)] for i in range(256)]
        self.read_at = self.write_at = 0        # entry * 3 + component

    def inb(self, port):
        if port == 0x3DA:
            return 0x08 if self.clock.us % self.PERIOD < self.RETRACE else 0
        if port == 0x3C9:
            i, c = divmod(self.read_at, 3)
            self.read_at = (self.read_at + 1) % 768
            return self.dac[i][c]
        return 0xFF

    def outb(self, port, v):
        if port == 0x3C7:
            self.read_at = 3 * v
        elif port == 0x3C8:
            self.write_at = 3 * v
        elif port == 0x3C9:
            i, c = divmod(self.write_at, 3)
            self.dac[i][c] = v & 0x3F
            self.write_at = (self.write_at + 1) % 768


class ChipCard(Card):
    """the chip itself, without a front, for a program that drives it as
    essprobe does: the chip model with the DMA controllers and their
    memory; `pins` the channels the card's four DMA pins are wired to
    (card registers 23h and 24h, 15 for none); `emm386` puts EMM386's DMA
    translation (emm386dma, its keywords) between the program and the DMA
    controllers, which phys_in() and phys_out() reach past it"""

    def __init__(self, irq=5, dma8=1, dma2=5, pins=(1, 5, 15, 15),
                 emm386=None):
        super().__init__()
        self.chip = TSRChip(irq, dma8, dma2)
        self.chip.pnp[(None, 0x23)] = pins[0] | pins[1] << 4
        self.chip.pnp[(None, 0x24)] = pins[2] | pins[3] << 4
        self.dma = self.chip.dmac       # dosrun gives it the program's memory
        self.memory = self.dma.memory
        self.vga = VGA(self.chip.clock)
        self.emm = None if emm386 is None else \
            EMM386DMA(self.chip.dmac, **emm386)

    def port_in(self, port):
        if port in VGA.PORTS:
            return self.vga.inb(port)
        if self.emm is not None and self.emm.handles(port):
            self.chip.catch_up()
            self.chip.clock.advance(1)
            return self.emm.inb(port)
        return self.chip.inb(port)

    def port_out(self, port, v):
        if port in VGA.PORTS:
            self.vga.outb(port, v)
        elif self.emm is not None and self.emm.handles(port):
            self.chip.catch_up()
            self.chip.clock.advance(1)
            self.emm.outb(port, v)
        else:
            self.chip.outb(port, v)

    def phys_in(self, port):
        """a port as ring 0 reaches it, past EMM386"""
        v = self.chip.inb(port)
        self._time(1)
        return v

    def phys_out(self, port, v):
        self.chip.outb(port, v)
        self._time(1)

    def tick(self, us):
        return self.chip.advance(us), len(self.chip.irq_edges)

    def record_from(self, source):
        self.chip.source = source

    @property
    def audio2(self):
        return True

    @property
    def played(self):
        return self.chip.heard

    def dac(self, kind):
        return self.chip.played2 if kind == 16 else self.chip.played

    def poke(self, addr, data):
        self.memory.write(addr, bytes(data))

    def peek(self, addr, n):
        return self.memory.read(addr, n)


# --- ES1869.VXD in a DOS box ---------------------------------------------------

class VDMAD:
    """VDMAD for one DOS VM: its virtual DMA controllers (`virt`), which its
    port accesses program, and the chip's physical ones (`phys`). A change
    of a channel's state goes to the channel's owner, with EDX = 0 when the
    VM programmed it or unmasked it, or the port's address with bit 0 when
    it masked it; a read of the count goes to the owner first (EDX = 1) when
    the owner asked for it (VDMAD_Set_Channel_Callbacks bit 0). A channel
    without an owner gets VDMAD_Default_Handler, which programs the physical
    channel from the virtual one, and reads of a channel programmed that
    way see the physical channel. A linear address is the physical one. What
    a VxD asks that VDMAD wouldn't do goes into `errors`"""

    def __init__(self, card):
        self.card = card
        self.m = card.m
        self.phys = card.chip.dmac
        self.virt = DMA8237(self.phys.memory)
        self.on_phys = set()        # channels the default handler programmed
        self.errors = []

    # --- the owners ---------------------------------------------------------
    def channel(self, handle):
        import vxdemu
        e = self.m.emu
        if handle == vxdemu.DMA1:
            return e.read8(vxdemu.ADI + vxdemu.ADI_DMA1_CHANNEL)
        if handle == vxdemu.DMA2:
            return e.read8(vxdemu.ADI + vxdemu.ADI_DMA2_CHANNEL)
        if 0x7200 <= handle < 0x7208:
            return handle - 0x7200
        raise RuntimeError("DMA handle %08X" % handle)

    def owner(self, n):
        """(handle, callback) of channel n's owner, None if it has none:
        ESS's handlers of Audio 1's and Audio 2's channels, as ADI_Create
        virtualizes them, or a channel the VxD virtualized later"""
        import vxdemu
        e, s = self.m.emu, self.m.syms
        if n == e.read8(vxdemu.ADI + vxdemu.ADI_DMA1_CHANNEL):
            return vxdemu.DMA1, s["L1_0518"]
        dma2 = e.read8(vxdemu.ADI + vxdemu.ADI_DMA2_CHANNEL)
        if dma2 != 0xFF and n == dma2:
            return vxdemu.DMA2, s["L1_0870"]
        return e.dma_virt.get(n)

    def notify(self, n, edx):
        own = self.owner(n)
        if own is None:
            self.default(n)
            return
        import vxdemu
        e = self.m.emu
        e.current_vm = vxdemu.VM_DOS
        e.run(own[1], {"EAX": own[0], "EBX": vxdemu.VM_DOS, "EDX": edx,
                       "ECX": 0})

    def default(self, n):
        """VDMAD_Default_Handler: the physical channel as the virtual one"""
        self.card.chip.catch_up()
        v, p = self.virt.ch[n], self.phys.ch[n]
        if v.masked:
            p.masked = True
            return
        p.mode, p.page = v.mode, v.page
        p.base_addr, p.base_count = v.addr, v.count
        p.addr, p.count = v.addr, v.count
        p.masked = False
        self.on_phys.add(n)

    def _sync(self, n):
        """a channel the physical controller runs as the VM programmed it:
        its position from there"""
        if n in self.on_phys:
            self.card.chip.catch_up()
            v, p = self.virt.ch[n], self.phys.ch[n]
            v.addr, v.count = p.addr, p.count
            v.tc = v.tc or p.tc
            p.tc = False

    # --- the VM's ports -----------------------------------------------------
    def outb(self, port, v):
        v &= 0xFF
        self.virt.outb(port, v)
        changed = []                    # (channel, EDX)
        if port in PORT_PAGE:
            n = PORT_PAGE[port]
            self.on_phys.discard(n)     # the VM's own state from now on
            changed = [(n, 0)] if not self.virt.ch[n].masked else []
        else:
            hi, reg = DMA8237.register(port)
            first = 4 * hi
            if reg < 8:
                n = first + (reg >> 1)
                self.on_phys.discard(n)
                if not self.virt.ch[n].masked:
                    changed = [(n, 0)]
            elif reg in (10, 11):
                n = first + (v & 3)
                c = self.virt.ch[n]
                if reg == 10:
                    changed = [(n, port | 1 if c.masked else 0)]
                else:
                    self.on_phys.discard(n)
                    changed = [] if c.masked else [(n, 0)]
            elif reg in (13, 14, 15):
                changed = [(n, port | 1 if self.virt.ch[n].masked else 0)
                           for n in range(first, first + 4)]
        for n, edx in changed:
            if n != 4:                  # the cascade: not the VM's
                self.notify(n, edx)

    def inb(self, port):
        at = DMA8237.register(port)
        if at is not None and at[1] < 8:
            n = 4 * at[0] + (at[1] >> 1)
            if n in self.on_phys:
                self._sync(n)
            elif at[1] & 1:
                own = self.owner(n)
                if own is not None and \
                        self.m.emu.dma_callbacks.get(own[0], 0) & 1:
                    self.notify(n, 1)
        elif at is not None and at[1] == 8:
            for n in range(4 * at[0], 4 * at[0] + 4):
                self._sync(n)
        return self.virt.inb(port)

    # --- its services -------------------------------------------------------
    def service(self, svc, emu):
        """a VDMAD service the VxD called; False if it isn't modelled here"""
        import vxdemu
        from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX,
                                       UC_X86_REG_EDX, UC_X86_REG_ESI)
        uc = emu.uc
        if svc == vxdemu.SVC_VDMAD_VIRTUALIZE:  # vxdemu keeps the owner;
            self.on_phys.discard(uc.reg_read(UC_X86_REG_EAX))  # the physical
            return False                # channel is the owner's now
        if svc not in (vxdemu.SVC_VDMAD_GET_VIRT_STATE,
                       vxdemu.SVC_VDMAD_SET_VIRT_STATE,
                       vxdemu.SVC_VDMAD_DEFAULT_HANDLER,
                       vxdemu.SVC_VDMAD_MASK, vxdemu.SVC_VDMAD_UNMASK,
                       vxdemu.SVC_VDMAD_GET_PHYS_COUNT,
                       vxdemu.SVC_VDMAD_GET_VIRT_COUNT,
                       vxdemu.SVC_VDMAD_SET_VIRT_COUNT):
            return False
        n = self.channel(uc.reg_read(UC_X86_REG_EAX))
        c, p = self.virt.ch[n], self.phys.ch[n]
        unit = 2 if c.word else 1
        if svc == vxdemu.SVC_VDMAD_GET_VIRT_STATE:
            self._sync(n)
            uc.reg_write(UC_X86_REG_ESI, c.phys())
            uc.reg_write(UC_X86_REG_ECX, ((c.count + 1) & 0x1FFFF) * unit)
            uc.reg_write(UC_X86_REG_EDX, (c.mode & 0xFC) | int(c.masked) |
                         ((vxdemu.DMA_16BIT if c.word else 0) << 8))
        elif svc == vxdemu.SVC_VDMAD_SET_VIRT_STATE:
            esi = uc.reg_read(UC_X86_REG_ESI)
            size = uc.reg_read(UC_X86_REG_ECX)
            edx = uc.reg_read(UC_X86_REG_EDX)
            if bool(edx & (vxdemu.DMA_16BIT << 8)) != c.word:
                self.errors.append("channel %d's state as %s" % (
                    n, "words" if edx & 0x4000 else "bytes"))
            page = 17 if c.word else 16     # the buffer in one page
            if not 0 < size <= 0x10000 * unit or size % unit or \
                    esi % unit or esi >> page != (esi + size - 1) >> page:
                self.errors.append("channel %d: %d bytes at %X" % (
                    n, size, esi))
            if c.word:
                c.base_addr = c.addr = (esi >> 1) & 0xFFFF
                c.page = (esi >> 16) & 0xFE
            else:
                c.base_addr = c.addr = esi & 0xFFFF
                c.page = (esi >> 16) & 0xFF
            c.base_count = c.count = (size // unit - 1) & 0xFFFF
            c.mode = (edx & 0xFC) | (n & 3)
            c.masked = bool(edx & 1)
            self.on_phys.discard(n)
        elif svc == vxdemu.SVC_VDMAD_DEFAULT_HANDLER:
            self.default(n)
        elif svc == vxdemu.SVC_VDMAD_MASK:
            self.card.chip.catch_up()
            p.masked = True
        elif svc == vxdemu.SVC_VDMAD_UNMASK:
            self.card.chip.catch_up()
            p.masked = False
        elif svc == vxdemu.SVC_VDMAD_GET_PHYS_COUNT:
            self.card.chip.catch_up()
            uc.reg_write(UC_X86_REG_ECX, p.count)
        elif svc == vxdemu.SVC_VDMAD_GET_VIRT_COUNT:
            self._sync(n)
            uc.reg_write(UC_X86_REG_ECX, ((c.count + 1) & 0x1FFFF) * unit)
        else:                           # SET_VIRT_COUNT, in bytes
            size = uc.reg_read(UC_X86_REG_ECX)
            if size % unit:
                self.errors.append("channel %d's count: %d bytes" % (n, size))
            c.count = (size // unit - 1) & 0xFFFF
        return True


class VxDCard(Card):
    """the game in a DOS box on ES1869.VXD's front: the card at IRQ irq,
    Audio 1 on channel dma8 and Audio 2 on dma2 (None: none, which ESS's
    ADI has as FFh), ESS's demand transfers on unless Single Mode DMA,
    SYSTEM.INI as `ini` has it, the chip's mixer as `mixer` has it when the
    game starts"""

    def __init__(self, irq=5, dma8=1, dma2=None, demand=True, ini=None,
                 mixer=None):
        import vxdemu
        super().__init__()
        self._game_isr = None
        le, elf = vxd_build()
        self.chip = TSRChip(irq, dma8, dma2)
        m = self.m = vxdemu.Machine(le, elf, True, hw=self.chip)
        for reg, v in (mixer or {}).items():
            self.chip.mixer[reg] = v
        e = m.emu
        m.start(ini or {})
        e.uc.mem_write(vxdemu.ADI + vxdemu.ADI_IRQ, bytes((irq, dma8)))
        e.uc.mem_write(vxdemu.ADI + vxdemu.ADI_DMA2_CHANNEL,
                       bytes((0xFF if dma2 is None else dma2,)))
        e.write32(vxdemu.ADI + vxdemu.ADI_DMA1, vxdemu.DMA1)
        e.write32(vxdemu.ADI + vxdemu.ADI_DMA2, vxdemu.DMA2)
        if demand:
            e.uc.mem_write(vxdemu.ADI + 0x13, b"\x01")  # ESS's flag 10000h
        self.vdmad = e.vdmad = VDMAD(self)
        desc = m.syms["D6_035C"]        # the IRQ's VPICD descriptor
        self.hw_int = e.read32(desc + 4)
        self.irq_handle = e.read32(vxdemu.ADI + 0x31)
        self.interrupts = 0             # the VM's interrupts requested

    @property
    def errors(self):
        return self.vdmad.errors

    def flags(self):
        """the front's S16_* flags"""
        return self.m.adi8(self.m.syms["EX_S16_Flags"])

    def _io(self, port, value=None):
        """the VM's access of a port VDMAD doesn't have: the VxD's trap
        handler, unless the VxD untrapped the port for the VM"""
        import vxdemu
        m, e = self.m, self.m.emu
        m.run_events(vxdemu.VM_DOS)
        if (vxdemu.VM_DOS, port) in e.trap_off or port not in m.handlers:
            if value is None:
                return self.chip.inb(port)
            self.chip.outb(port, value)
            return None
        e.current_vm = vxdemu.VM_DOS
        out = e.run(m.handlers[port], {
            "EBX": vxdemu.VM_DOS, "ESI": vxdemu.ADI, "EDX": port,
            "ECX": 0 if value is None else 4,
            "EAX": 0xFFFFFFFF if value is None else value,
            "EBP": vxdemu.CLIENT})
        return None if value is not None else out["EAX"] & 0xFF

    def port_in(self, port):
        if self.chip.dmac.handles(port):
            return self.vdmad.inb(port)
        return self._io(port)

    def port_out(self, port, v):
        if self.chip.dmac.handles(port):
            self.vdmad.outb(port, v)
        else:
            self._io(port, v)

    def tick(self, us):
        done = self.chip.advance(us)
        self.m.advance_to(self.chip.clock.us)   # the VxD's time-outs
        return done, len(self.chip.irq_edges)

    def record_from(self, source):
        self.chip.source = source

    # the game's handler runs when ESS's hardware interrupt procedure has
    # requested the VM's interrupt, and its EOI reaches the VxD
    @property
    def isr(self):
        return None if self._game_isr is None else self._interrupt

    @isr.setter
    def isr(self, fn):
        self._game_isr = fn

    def _interrupt(self, card):
        import vxdemu
        e = self.m.emu
        n = len(e.vpicd)
        e.current_vm = vxdemu.VM_DOS
        e.run(self.hw_int, {"EAX": self.irq_handle, "EBX": vxdemu.VM_DOS,
                            "EDX": vxdemu.ADI})
        if ("set", self.irq_handle, vxdemu.VM_DOS) not in e.vpicd[n:]:
            return
        self.interrupts += 1
        self._game_isr(card)
        self.m.eoi(vxdemu.VM_DOS)

    @property
    def audio2(self):
        """the front plays 16-bit sound on Audio 2"""
        return self.m.adi8(self.m.syms["EX_S16_A2"]) < 4

    @property
    def played(self):
        return self.chip.heard

    def dac(self, kind):
        return self.chip.played2 if kind == 16 and self.audio2 else \
            self.chip.played

    @property
    def midi_out(self):
        return self.chip.midi_out

    def poke(self, addr, data):
        self.chip.dmac.memory.write(addr, data)

    def peek(self, addr, n):
        return self.chip.dmac.memory.read(addr, n)
