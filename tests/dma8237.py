# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The PC/AT's two 8237 DMA controllers and their page registers.

Channels 0-3 move bytes; channels 5-7 move words, with a word address and a
word count, and the page register's bit 0 left out. A program programs a
channel through the ports; a card moves data with read() (memory to the
card) and write() (the card to memory), one byte or word per DMA cycle. At
terminal count the channel's status bit is set, and the channel either
reloads its base address and count (auto-initialize) or masks itself. A
cycle whose mode doesn't move data that way (verify, the other direction,
or the undefined 0Ch) goes into `bad`: a card that reads gets FFh, the
floating bus, and where the channel writes into memory, the memory gets it
too; a card that writes to a channel that reads memory loses its data.
"""

PAGE_PORTS = {0: 0x87, 1: 0x83, 2: 0x81, 3: 0x82, 4: 0x8F, 5: 0x8B,
              6: 0x89, 7: 0x8A}
PORT_PAGE = {p: ch for ch, p in PAGE_PORTS.items()}

# the mode register
MODE_VERIFY, MODE_WRITE, MODE_READ = 0x00, 0x04, 0x08
MODE_AUTO, MODE_DOWN = 0x10, 0x20
MODE_DEMAND, MODE_SINGLE, MODE_BLOCK, MODE_CASCADE = 0x00, 0x40, 0x80, 0xC0


class Channel:
    __slots__ = ("n", "word", "base_addr", "base_count", "addr", "count",
                 "mode", "masked", "page", "tc")

    def __init__(self, n):
        self.n = n
        self.word = n >= 4
        self.base_addr = self.base_count = 0
        self.addr = self.count = 0
        self.mode = 0
        self.masked = True
        self.page = 0
        self.tc = False             # status bit: terminal count since read

    def phys(self):
        """the physical byte address of the next cycle"""
        if self.word:
            return ((self.page & 0xFE) << 16) | (self.addr << 1)
        return (self.page << 16) | self.addr

    def view(self):
        """what the old models kept: the base values as written"""
        return {"mask": self.masked, "mode": self.mode,
                "addr": self.base_addr, "count": self.base_count,
                "page": self.page}


class Memory:
    """a bytearray as the DMA's memory"""

    def __init__(self, size=0x1000000):
        self.data = bytearray(size)

    def read(self, addr, n):
        return bytes(self.data[addr:addr + n])

    def write(self, addr, data):
        self.data[addr:addr + len(data)] = data


class DMA8237:
    """both controllers; memory has read(addr, n) and write(addr, data)"""

    def __init__(self, memory=None):
        self.memory = memory if memory is not None else Memory()
        self.ch = [Channel(i) for i in range(8)]
        # channel 4 cascades the first controller, unmasked, as the BIOS
        # leaves it (env.dma: DEh reads 0Eh)
        self.ch[4].mode, self.ch[4].masked = MODE_CASCADE, False
        self.ff = [0, 0]                # the byte pointer flip-flops
        self.command = [0, 0]
        self.temp = [0, 0]
        self.log = []                   # (port, value) of every write
        self.reads = []                 # (port, value) of every read
        self.bad = []                   # (channel, mode): no data moved
        self.trace = None               # sbops: each channel's programming
        self.served = {}                # as its cycles first move data

    @staticmethod
    def register(port):
        """(controller, register 0-15) of a controller port, or None"""
        if port < 0x10:
            return 0, port
        if 0xC0 <= port <= 0xDF and not port & 1:
            return 1, (port - 0xC0) >> 1
        return None

    def handles(self, port):
        return self.register(port) is not None or port in PORT_PAGE

    def outb(self, port, v):
        v &= 0xFF
        self.log.append((port, v))
        if port in PORT_PAGE:
            self.ch[PORT_PAGE[port]].page = v
            return
        at = self.register(port)
        if at is None:
            return
        hi, reg = at
        first = 4 * hi
        if reg < 8:
            c = self.ch[first + (reg >> 1)]
            shift = 8 if self.ff[hi] else 0
            self.ff[hi] ^= 1
            if reg & 1:
                c.base_count = (c.base_count & ~(0xFF << shift)) | \
                    (v << shift)
                c.count = c.base_count
            else:
                c.base_addr = (c.base_addr & ~(0xFF << shift)) | (v << shift)
                c.addr = c.base_addr
        elif reg == 8:
            self.command[hi] = v
        elif reg == 9:
            pass                        # software request: not modelled
        elif reg == 10:
            self.ch[first + (v & 3)].masked = bool(v & 4)
        elif reg == 11:
            self.ch[first + (v & 3)].mode = v
        elif reg == 12:
            self.ff[hi] = 0
        elif reg == 13:                 # master clear
            self.ff[hi] = 0
            self.command[hi] = 0
            self.temp[hi] = 0
            for c in self.ch[first:first + 4]:
                c.masked = True
                c.tc = False
        elif reg == 14:                 # clear mask register
            for c in self.ch[first:first + 4]:
                c.masked = False
        elif reg == 15:                 # write all mask bits
            for i, c in enumerate(self.ch[first:first + 4]):
                c.masked = bool(v >> i & 1)

    def inb(self, port):
        if port in PORT_PAGE:
            v = self.ch[PORT_PAGE[port]].page
        else:
            at = self.register(port)
            if at is None:
                return 0xFF
            hi, reg = at
            first = 4 * hi
            if reg < 8:
                c = self.ch[first + (reg >> 1)]
                shift = 8 if self.ff[hi] else 0
                self.ff[hi] ^= 1
                v = ((c.count if reg & 1 else c.addr) >> shift) & 0xFF
            elif reg == 8:              # status: TC bits, cleared by the read
                v = 0
                for i, c in enumerate(self.ch[first:first + 4]):
                    if c.tc:
                        v |= 1 << i
                        c.tc = False
            elif reg == 13:
                v = self.temp[hi]
            elif reg == 15:             # the mask bits, as the PIIX4 gives
                v = 0x00                # them (env.dma)
                for i, c in enumerate(self.ch[first:first + 4]):
                    if c.masked:
                        v |= 1 << i
            else:
                v = 0xFF
        self.reads.append((port, v))
        return v

    # --- the card's side ----------------------------------------------------
    def ready(self, n):
        """True while channel n would answer a DMA request"""
        c = self.ch[n]
        return not c.masked and (c.mode & 0xC0) != MODE_CASCADE

    def _serve(self, c):
        key = (c.mode, c.base_addr, c.base_count, c.page)
        if self.served.get(c.n) != key:
            self.served[c.n] = key
            self.trace.append(("DMA", c.n) + key)

    def _step(self, c):
        """one cycle done: the address and count move on, and terminal count
        reloads or masks the channel; True at terminal count"""
        if c.mode & MODE_DOWN:
            c.addr = (c.addr - 1) & 0xFFFF
        else:
            c.addr = (c.addr + 1) & 0xFFFF
        c.count = (c.count - 1) & 0xFFFF
        if c.count != 0xFFFF:
            return False
        c.tc = True
        if c.mode & MODE_AUTO:
            c.addr, c.count = c.base_addr, c.base_count
        else:
            c.masked = True
        return True

    def read(self, n, units):
        """up to `units` cycles of memory to the card on channel n: the bytes
        (two a cycle on a word channel), fewer if the channel masks itself
        at terminal count"""
        c = self.ch[n]
        size = 2 if c.word else 1
        out = bytearray()
        for _ in range(units):
            if not self.ready(n):
                break
            if self.trace is not None:
                self._serve(c)
            if c.mode & 0x0C == MODE_READ:
                out += self.memory.read(c.phys(), size)
            else:
                self.bad.append((n, c.mode))
                out += b"\xFF" * size
                if c.mode & 0x0C == MODE_WRITE:
                    self.memory.write(c.phys(), b"\xFF" * size)
            self._step(c)
        return bytes(out)

    def write(self, n, data):
        """the card's data to memory on channel n, a byte or word a cycle;
        the number of bytes taken"""
        c = self.ch[n]
        size = 2 if c.word else 1
        done = 0
        while done + size <= len(data):
            if not self.ready(n):
                break
            if self.trace is not None:
                self._serve(c)
            if c.mode & 0x0C == MODE_WRITE:
                self.memory.write(c.phys(), data[done:done + size])
            else:
                self.bad.append((n, c.mode))
            done += size
            self._step(c)
        return done

    def left(self, n):
        """cycles to terminal count, as the current count says"""
        return (self.ch[n].count + 1) & 0x1FFFF
