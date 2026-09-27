# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Run essctl.exe as a 16-bit Windows program under Wine.

Opt-in (slow, and needs 32-bit Wine, Xvfb and Open Watcom):

  ESSREG_WINE=1 OW2=/path/to/open-watcom python3 -m unittest tests.test_wine

- profiles: /save and /load in batch mode against the simulated chip
- ESFM: tests/host/drvhold.c loads and enables the real driver/ESFM.DRV,
  then starts "essctl /load" with a profile naming a bank larger than the
  driver's own, and dumps the bank the driver holds before and after
- ESFM voices: "essctl /dump" with the ESS driver, then the fixed build,
  shows the driver's 18 voices and, for the fixed build, its counters
- ESFM bank file: the fixed build loads the bank named in SYSTEM.INI
  [ESFM.DRV] Bank= when a program opens the device, and "essctl /load" of
  a profile with a bank names it there

Win16 wants 8.3 path names, so the work directory is reached through a
short symbolic link in /tmp.
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OW2 = os.environ.get("OW2", "")


def available():
    return (os.environ.get("ESSREG_WINE") == "1" and shutil.which("wine")
            and shutil.which("Xvfb") and
            os.path.exists(os.path.join(OW2, "binl64", "wcc")))


def read(path):
    with open(path, "rb") as f:
        return f.read()


@unittest.skipUnless(available(), "set ESSREG_WINE=1 and OW2 (needs wine, "
                     "Xvfb and Open Watcom)")
class WineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        work = cls.tmp.name
        cls.link = tempfile.mktemp(prefix="es", dir="/tmp")[:12]
        os.symlink(work, cls.link)
        cls.win = "Z:" + cls.link.replace("/", "\\")
        subprocess.run([os.path.join(ROOT, "tools", "ow2build.sh"), OW2],
                       check=True, capture_output=True)
        shutil.copy(os.path.join(ROOT, "out", "ow2", "essctl.exe"), work)
        shutil.copy(os.path.join(ROOT, "driver", "ESFM.DRV"), work)
        env = dict(os.environ, WATCOM=OW2, INCLUDE=os.path.join(OW2, "h"),
                   PATH=os.path.join(OW2, "binl64") + ":" + os.environ["PATH"])
        subprocess.run(["wcc", os.path.join(ROOT, "tests", "host", "drvhold.c"),
                        "-i=%s;%s" % (os.path.join(OW2, "h"),
                                      os.path.join(OW2, "h", "win")),
                        "-bt=windows", "-ml", "-zW", "-zc", "-3", "-zq",
                        "-fo=drvhold.obj"], cwd=work, env=env, check=True)
        subprocess.run(["wlink", "name", "drvhold.exe", "system", "windows",
                        "option", "quiet", "file", "drvhold.obj", "library",
                        "mmsystem.lib"], cwd=work, env=env, check=True)
        cls.env = dict(os.environ, WINEARCH="win32", WINEDEBUG="-all",
                       WINEPREFIX=os.path.join(work, "prefix"),
                       DISPLAY=":93")
        cls.xvfb = subprocess.Popen(["Xvfb", ":93", "-screen", "0",
                                     "800x600x24", "-nolisten", "tcp"],
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
        time.sleep(1.5)
        subprocess.run(["wineboot", "-i"], env=cls.env, timeout=300,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    @classmethod
    def tearDownClass(cls):
        subprocess.run(["wineserver", "-k"], env=cls.env)
        cls.xvfb.kill()
        os.unlink(cls.link)
        cls.tmp.cleanup()

    def wine(self, *args):
        return subprocess.run(["wine"] + list(args), env=self.env,
                              cwd=self.link, timeout=120,
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)

    def path(self, name):
        return os.path.join(self.link, name)

    def test_profile_round_trip(self):
        self.wine("essctl.exe", "/sim", "/save", "a.ini", "/q")
        text = read(self.path("a.ini")).decode()
        self.assertIn("fx.3d.level=0", text)
        text = text.replace("fx.3d.level=0", "fx.3d.level=40")
        text = text.replace("rec.source=Microphone", "rec.source=Line")
        with open(self.path("a.ini"), "w", newline="\r\n") as f:
            f.write(text)
        self.wine("essctl.exe", "/sim", "/load", "a.ini", "/save", "b.ini",
                  "/q")
        back = read(self.path("b.ini")).decode()
        self.assertIn("fx.3d.level=40", back)
        self.assertIn("rec.source=Line", back)
        log = read(self.path("ESSCTL.LOG")).decode()
        self.assertIn("50 settings applied", log)

    def test_esfm_live_load(self):
        bank = bytearray(read(os.path.join(ROOT, "esfm_patch_banks",
                                           "bnk_com_better_square_wave.bin")))
        size = len(bank)
        voice = bytearray(bank[0x200:0x200 + 36])
        voice[0] &= ~6
        for i in range(40):
            struct.pack_into("<H", bank, 2 * (216 + i), size + 36 * i)
            bank += voice
        with open(self.path("BIG.BIN"), "wb") as f:
            f.write(bank)
        with open(self.path("PROF.INI"), "w", newline="\r\n") as f:
            f.write("[ESSCTL]\nFormat=1\n[ESFM]\nBank=%s\\BIG.BIN\n" % self.win)
        self.wine("drvhold.exe", "ESFM.DRV", self.win,
                  "%s\\essctl.exe /sim /load %s\\PROF.INI /q" %
                  (self.win, self.win))
        before = read(self.path("BEFORE.BIN"))
        after = read(self.path("AFTER.BIN"))
        original = read(os.path.join(ROOT, "esfm_patch_banks",
                                     "bnk_com.bin"))
        self.assertEqual(before[:len(original)], original)
        self.assertEqual(after[:len(bank)], bytes(bank))
        self.assertIn("ESFM bank loaded", read(self.path("ESSCTL.LOG"))
                      .decode())

    def esfm_dump(self, driver):
        shutil.copy(driver, self.path("DRV.DRV"))
        self.wine("drvhold.exe", "DRV.DRV", self.win,
                  "%s\\essctl.exe /sim /dump %s\\ESFM.TXT /q" %
                  (self.win, self.win))
        return read(self.path("ESFM.TXT")).decode("latin-1")

    def system_ini(self):
        return os.path.join(self.env["WINEPREFIX"], "drive_c", "windows",
                            "system.ini")

    def fixed_driver(self):
        fixed = os.path.join(self.link, "FIXED.DRV")
        if not os.path.exists(fixed):
            sys.path.insert(0, os.path.join(ROOT, "tools"))
            import build_esfm
            build_esfm.build(True, fixed, workdir=self.link)
        return fixed

    def test_esfm_bank_file(self):
        bank = bytearray(read(os.path.join(ROOT, "esfm_patch_banks",
                                           "bnk_com.bin")))
        off = struct.unpack_from("<H", bank, 0)[0]
        bank[off + 4] = 0x5A
        with open(self.path("MARK.BIN"), "wb") as f:
            f.write(bank)
        ini = self.system_ini()
        saved = read(ini)
        try:
            with open(ini, "ab") as f:
                f.write(b"\r\n[ESFM.DRV]\r\nBank=%s\\MARK.BIN\r\n" %
                        self.win.encode())
            text = self.esfm_dump(self.fixed_driver())
            # the driver read SYSTEM.INI, the file's date and the file
            # through KERNEL when drvhold opened the device
            self.assertEqual(read(self.path("BEFORE.BIN"))[:len(bank)],
                             bytes(bank))
            self.assertIn("Bank file: %s\\MARK.BIN, %d bytes" %
                          (self.win, len(bank)), text)
        finally:
            with open(ini, "wb") as f:
                f.write(saved)

    def test_esfm_profile_names_bank_file(self):
        bank = read(os.path.join(ROOT, "esfm_patch_banks",
                                 "bnk_com_better_square_wave.bin"))
        with open(self.path("SQUARE.BIN"), "wb") as f:
            f.write(bank)
        with open(self.path("PROF2.INI"), "w", newline="\r\n") as f:
            f.write("[ESSCTL]\nFormat=1\n[ESFM]\nBank=%s\\SQUARE.BIN\n" %
                    self.win)
        ini = self.system_ini()
        saved = read(ini)
        try:
            shutil.copy(self.fixed_driver(), self.path("DRV.DRV"))
            self.wine("drvhold.exe", "DRV.DRV", self.win,
                      "%s\\essctl.exe /sim /load %s\\PROF2.INI /q" %
                      (self.win, self.win))
            self.assertEqual(read(self.path("AFTER.BIN"))[:len(bank)], bank)
            text = read(ini).decode("latin-1").replace("\r\n", "\n")
            self.assertIn("[ESFM.DRV]\nBank=%s\\SQUARE.BIN" % self.win,
                          text)
            self.assertIn("ESFM.DRV plays it from the file now",
                          read(self.path("ESSCTL.LOG")).decode("latin-1"))
        finally:
            with open(ini, "wb") as f:
                f.write(saved)

    def test_esfm_voices_in_dump(self):
        text = self.esfm_dump(os.path.join(ROOT, "driver", "ESFM.DRV"))
        self.assertIn("ESFM.DRV", text)
        self.assertIn("The MIDI device is closed", text)
        self.assertIn("ESS driver: drops messages", text)
        self.assertEqual(text.count("  free"), 18)
        text = self.esfm_dump(self.fixed_driver())
        self.assertIn("Fixed driver: 0 messages queued", text)
        self.assertIn("Bank file: none, the driver's own bank plays", text)
        self.assertEqual(text.count("  free"), 18)


if __name__ == "__main__":
    unittest.main()
