# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Minimal reader for 32-bit little-endian ELF relocatable objects (NASM -f elf32)."""

import struct

SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_REL, SHT_NOBITS = 1, 2, 3, 9, 8
R_386_32, R_386_PC32 = 1, 2
SHN_UNDEF, SHN_ABS = 0, 0xFFF1


class Section:
    def __init__(self, index, name, stype, flags, offset, size, link, info):
        self.index, self.name, self.type, self.flags = index, name, stype, flags
        self.offset, self.size, self.link, self.info = offset, size, link, info
        self.data = b""
        self.relocs = []  # (offset, type, symbol index)


class Symbol:
    def __init__(self, name, value, size, info, shndx):
        self.name, self.value, self.size = name, value, size
        self.bind, self.type, self.shndx = info >> 4, info & 0xF, shndx


class ElfObject:
    def __init__(self, data):
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
            raise ValueError("not a 32-bit little-endian ELF file")
        (etype, machine, _v, _entry, _phoff, shoff, _flags, _ehsize, _phes,
         _phnum, shentsize, shnum, shstrndx) = struct.unpack_from(
             "<HHIIIIIHHHHHH", data, 16)
        if etype != 1 or machine != 3:
            raise ValueError("expected an i386 relocatable object")
        raw = []
        for i in range(shnum):
            raw.append(struct.unpack_from("<10I", data, shoff + i * shentsize))
        strtab = raw[shstrndx]
        names = data[strtab[4]:strtab[4] + strtab[5]]

        def cstr(buf, off):
            return buf[off:buf.index(b"\0", off)].decode("latin-1")

        self.sections = []
        for i, (nm, st, fl, _addr, off, size, link, info, _al, _es) in \
                enumerate(raw):
            s = Section(i, cstr(names, nm), st, fl, off, size, link, info)
            if st != SHT_NOBITS:
                s.data = data[off:off + size]
            self.sections.append(s)

        self.symbols = []
        for s in self.sections:
            if s.type == SHT_SYMTAB:
                strs = self.sections[s.link].data
                for k in range(s.size // 16):
                    nm, value, size, info, _other, shndx = struct.unpack_from(
                        "<IIIBBH", s.data, k * 16)
                    self.symbols.append(
                        Symbol(cstr(strs, nm), value, size, info, shndx))
        for s in self.sections:
            if s.type == SHT_REL:
                target = self.sections[s.info]
                for k in range(s.size // 8):
                    off, info = struct.unpack_from("<II", s.data, k * 8)
                    target.relocs.append((off, info & 0xFF, info >> 8))

    def section(self, name):
        for s in self.sections:
            if s.name == name:
                return s
        return None

    def symbol_map(self):
        """name -> (section index, value) for defined symbols."""
        out = {}
        for sym in self.symbols:
            if sym.name and sym.shndx not in (SHN_UNDEF,) and sym.type != 4:
                out[sym.name] = (sym.shndx, sym.value)
        return out


def read_elf(path):
    with open(path, "rb") as f:
        return ElfObject(f.read())
