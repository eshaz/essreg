# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Test third_party/hdpmi32i/HDPMI32i.EXE: the release's file with the fix
of tools/hdpmifix.py and nothing else, and HDPMI32i's emulation of CLI and
STI (emuprivinstr) from both files, run in unicorn at ring 0 on the frame of
a client's fault. An STI right before an IRETD with interrupts off, as at
the end of Absolute Pinball's interrupt handlers, faults at ring 0 with the
release's file, which stopped the laptop, and returns through the client's
frame with the fix; the other cases come out the same with both.

usage: python3 -m unittest tests.test_hdpmifix
"""

import os
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import hdpmifix  # noqa: E402

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_INTR
    from unicorn.x86_const import (UC_X86_REG_CS, UC_X86_REG_SS,
                                   UC_X86_REG_DS, UC_X86_REG_ES,
                                   UC_X86_REG_ESP, UC_X86_REG_EIP,
                                   UC_X86_REG_EAX, UC_X86_REG_ESI,
                                   UC_X86_REG_EDI, UC_X86_REG_EFLAGS,
                                   UC_X86_REG_GDTR)
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

TOOL = os.path.join(ROOT, "tools", "hdpmifix.py")
EXE = os.path.join(ROOT, "third_party", "hdpmi32i", "HDPMI32i.EXE")
README = os.path.join(ROOT, "third_party", "hdpmi32i", "README.md")
FIXED_SHA256 = ("9c76879685a84649c8bd2e835c015153"
                "0647a2367ef5323e8adac1c97613cb0c")
SIZE = 38646

# emuprivinstr's start (push ds; push esi; push eax; lds esi,[esp+14h];
# mov eax,ds; lsl eax,eax), and its two ends: an instruction it leaves to
# the client's handler (pop eax; pop esi; pop ds; ret), and one it emulated
# (mov [esp+14h],esi; pop eax; pop esi; pop ds; lea esp,[esp+8]; iretd)
ENTRY = bytes.fromhex("1e5650c57424148cd80f03c0")
LEFT = bytes.fromhex("585e1fc3")
DONE = bytes.fromhex("8974241458" "5e1f8d642408cf")

# linear memory: the GDT, the host's code (the file), the host's data and
# stack, the client's code and stack
GDT, CODE, HOST, CLIENT, CSTACK = 0x1000, 0x100000, 0x200000, 0x300000, \
    0x400000
R0_CS, R0_SS, R3_CS, R3_SS = 0x08, 0x10, 0x1B, 0x23
ESP0 = 0x8000               # the host's stack top
ESP3 = 0x8FF0               # the client's: its low word is no selector
INT_COUNT = 0x44A           # the interrupts the host is in (wIntCount)
AT = 0x100                  # the client's instruction
BACK = 0x7777               # where the client's frame goes back to
IF = 0x200


def desc(base, limit, access, flags):
    return struct.pack("<HHBBBB", limit & 0xFFFF, base & 0xFFFF,
                       (base >> 16) & 0xFF, access,
                       flags | ((limit >> 16) & 0xF), (base >> 24) & 0xFF)


def release():
    with open(EXE, "rb") as f:
        return hdpmifix.unfix(f.read())


def fixed():
    with open(EXE, "rb") as f:
        return f.read()


def emulate(exe, code, eflags, count, frame_flags=0x3002):
    """emuprivinstr of exe at ring 0, for the fault of the client's `code`
    at AT with `eflags`, the host in `count` interrupts, and the client's
    frame (BACK, R3_CS, frame_flags) on its stack: ("fault", vector, the
    offset in exe), or ("left" or "done", the fault's frame as EIP, CS,
    EFLAGS, ESP and SS, the registers EAX, ESI, EDI and DS)"""
    entry = exe.find(ENTRY)
    left = exe.find(LEFT, entry) + len(LEFT) - 1
    done = exe.find(DONE, entry) + len(DONE) - 1
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    for base, size in ((GDT, 0x1000), (CODE, (len(exe) + 0xFFF) & ~0xFFF),
                       (HOST, 0x10000), (CLIENT, 0x1000), (CSTACK, 0x10000)):
        uc.mem_map(base, size)
    gdt = b"\0" * 8 + desc(0, 0xFFFFF, 0x9A, 0xC0) \
        + desc(HOST, 0xFFFFF, 0x92, 0xC0) \
        + desc(CLIENT, 0xFFF, 0xFA, 0x40) \
        + desc(CSTACK, 0xFFFF, 0xF2, 0x40)
    uc.mem_write(GDT, gdt)
    uc.reg_write(UC_X86_REG_GDTR, (0, GDT, len(gdt) - 1, 0))
    uc.mem_write(CODE, exe)
    uc.mem_write(CLIENT + AT - 2, b"\x90\x90" + code)
    uc.mem_write(CSTACK + ESP3, struct.pack("<III", BACK, R3_CS,
                                            frame_flags))
    uc.mem_write(HOST + INT_COUNT, struct.pack("<H", count))
    # the call's return (intr0D's), then the fault's frame: the error code,
    # EIP, CS, EFLAGS, ESP and SS
    esp = ESP0 - 28
    uc.mem_write(HOST + esp, struct.pack("<7I", 0xDEADBEEF, 0, AT, R3_CS,
                                         eflags, ESP3, R3_SS))
    uc.reg_write(UC_X86_REG_CS, R0_CS)
    uc.reg_write(UC_X86_REG_SS, R0_SS)
    uc.reg_write(UC_X86_REG_ESP, esp)
    uc.reg_write(UC_X86_REG_DS, R3_SS)
    uc.reg_write(UC_X86_REG_ES, R3_SS)
    regs = ((UC_X86_REG_EAX, 0x11111111), (UC_X86_REG_ESI, 0x22222222),
            (UC_X86_REG_EDI, 0x33333333))
    for reg, v in regs:
        uc.reg_write(reg, v)
    uc.reg_write(UC_X86_REG_EFLAGS, 0x0002)
    out = []

    def fault(uc, vector, _user):
        out.append(("fault", vector, uc.reg_read(UC_X86_REG_EIP) - CODE))
        uc.emu_stop()

    def end(uc, addr, size, how):
        sp = uc.reg_read(UC_X86_REG_ESP) + (8 if how == "left" else 0)
        frame = struct.unpack("<5I", uc.mem_read(HOST + sp, 20))
        out.append((how, frame, tuple(uc.reg_read(r) for r, _ in regs)
                    + (uc.reg_read(UC_X86_REG_DS),)))
        uc.emu_stop()

    uc.hook_add(UC_HOOK_INTR, fault)
    uc.hook_add(UC_HOOK_CODE, end, "left", CODE + left, CODE + left)
    uc.hook_add(UC_HOOK_CODE, end, "done", CODE + done, CODE + done)
    uc.emu_start(CODE + entry, 0, count=500)
    return out[0] if out else ("ran on", uc.reg_read(UC_X86_REG_EIP) - CODE)


class FileTest(unittest.TestCase):
    def test_committed(self):
        """HDPMI32i.EXE is the release's file with the fix and nothing else,
        and its README gives both hashes"""
        data = fixed()
        self.assertEqual(len(data), SIZE)
        self.assertEqual(hdpmifix.state(data)[0], "fixed")
        self.assertEqual(hdpmifix.sha256(data), FIXED_SHA256)
        old = release()
        self.assertEqual(hdpmifix.sha256(old), hdpmifix.RELEASE_SHA256)
        self.assertEqual([i for i in range(SIZE) if old[i] != data[i]],
                         [0x2F70, 0x2F79, 0x2F85, 0x2F89])
        with open(README, encoding="utf-8") as f:
            text = f.read().replace("\r\n", " ").replace("\n", " ")
        text = text.replace("`", "").replace(" ", "")
        for h in (hdpmifix.RELEASE_SHA256, FIXED_SHA256):
            self.assertIn(h, text)

    def test_tool(self):
        """the tool makes the fixed file from the release's, again from the
        fixed one, and tells them apart"""
        with tempfile.TemporaryDirectory() as tmp:
            src, dst = os.path.join(tmp, "IN.EXE"), os.path.join(tmp, "O.EXE")
            other = os.path.join(tmp, "OTHER.EXE")
            with open(src, "wb") as f:
                f.write(release())
            with open(other, "wb") as f:
                f.write(b"MZ" + bytes(1000))

            def run(*args):
                return subprocess.run([sys.executable, TOOL] + list(args),
                                      capture_output=True, text=True)

            self.assertEqual(run("--check", src).returncode, 1)
            self.assertEqual(run("--check", other).returncode, 2)
            r = run(src, dst)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("02F70h, 02F79h, 02F85h, 02F89h", r.stdout)
            with open(dst, "rb") as f:
                self.assertEqual(f.read(), fixed())
            self.assertEqual(run("--check", dst).returncode, 0)
            r = run(dst, src)
            self.assertIn("has the fix already", r.stdout)
            with open(src, "rb") as f:
                self.assertEqual(f.read(), fixed())
            self.assertEqual(run(other, dst).returncode, 2)


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class StiIretTest(unittest.TestCase):
    """emuprivinstr of both files on a client's CLI or STI"""

    def test_handler_end(self):
        """STI, IRETD with interrupts off, at the end of a handler: the
        release's file loads the client's EFLAGS and its ESP's low word for
        the stack and faults at ring 0; the fixed one takes the IRETD's
        frame from the client's stack, with IOPL 0, and sets IF outside an
        interrupt only, as for an STI by itself"""
        bad = emulate(release(), b"\xFB\xCF", 0x0002, 1)
        lds = hdpmifix.state(release())[1] + 5
        self.assertEqual(bad, ("fault", 13, lds))
        regs = (0x11111111, 0x22222222, 0x33333333, R3_SS)
        for count, flags in ((1, 0x0002), (0, 0x0002 | IF)):
            got = emulate(fixed(), b"\xFB\xCF", 0x0002, count)
            self.assertEqual(got, ("done", (BACK, R3_CS, flags, ESP3 + 12,
                                            R3_SS), regs), count)

    def test_alike(self):
        """the cases the fix doesn't reach come out the same with both: an
        STI with interrupts on goes by, an STI by itself sets IF outside an
        interrupt, a CLI clears it there, and an instruction neither
        emulates is left to the client's handler"""
        frame = (AT + 1, R3_CS)
        for code, eflags, count, flags in (
                (b"\xFB\xCF", 0x0202, 1, 0x0202),
                (b"\xFB\x90", 0x0002, 0, 0x0202),
                (b"\xFB\x90", 0x0002, 1, 0x0002),
                (b"\xFA\x90", 0x0202, 0, 0x0002),
                (b"\xFA\x90", 0x0202, 1, 0x0202)):
            want = ("done", frame + (flags, ESP3, R3_SS),
                    (0x11111111, 0x22222222, 0x33333333, R3_SS))
            for exe in (release(), fixed()):
                self.assertEqual(emulate(exe, code, eflags, count), want,
                                 (code, eflags, count))
        for exe in (release(), fixed()):
            got = emulate(exe, b"\x90\x90", 0x0202, 0)
            self.assertEqual(got[:2], ("left", (AT, R3_CS, 0x0202, ESP3,
                                                R3_SS)))


if __name__ == "__main__":
    unittest.main()
