# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Run essctl.exe and ess3d.exe as 16-bit Windows programs under Wine.

Opt-in (slow, and needs 32-bit Wine, Xvfb and Open Watcom):

  ESSREG_WINE=1 OW2=/path/to/open-watcom python3 -m unittest tests.test_wine

- profiles: /save and /load in batch mode against the simulated chip
- /i2s=off and /i2s=on: ESSWaveTableChip in the ES1869's software key,
  and mixer 7Fh bit 0 cleared
- ESFM: tests/host/drvhold.c loads and enables the real driver/ESFM.DRV,
  then starts "essctl /load" with a profile naming a bank larger than the
  driver's own, and dumps the bank the driver holds before and after
- ESFM voices: "essctl /dump" with the ESS driver, then the fixed build,
  shows the driver's 18 voices and, for the fixed build, its counters
- ESFM bank file: the fixed build loads the bank named in SYSTEM.INI
  [ESFM.DRV] Bank= when a program opens the device, and "essctl /load" of
  a profile with a bank names it there
- ess3d: commands against the simulated chip, bad input and no card, read
  from its /log= file, since Wine's winevdm always exits with 0 and drops
  the program's exit code; without /q, no card shows in the display, which
  closes by itself
- ess3d's display: a second ess3d hands its setting to the display of the
  first and exits, and the first goes after the second one's time
- ess3d's tray icon: a second "ess3d tray" opens the panel of the first,
  and "ess3d exit" closes it
- esfmrec: /sim records the test tone for /t= seconds into a WAV file at
  the music DAC's rate, and /raw writes the samples alone
- esfmrec ended by force: the header it saved every 5 s holds the
  samples, and the next start repairs it and removes the marker
- esfmrec on a full disk (a file size limit): it stops, and the WAV file
  holds what was written; /split= goes on in NAME_2.WAV without a gap

Win16 wants 8.3 path names, so the work directory is reached through a
short symbolic link in /tmp.
"""

import os
import resource
import shutil
import signal
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
        shutil.copy(os.path.join(ROOT, "out", "ow2", "ess3d.exe"), work)
        shutil.copy(os.path.join(ROOT, "out", "ow2", "esfmrec.exe"), work)
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
        # a server of its own, so it never inherits a test's file size limit
        os.makedirs(cls.env["WINEPREFIX"])
        subprocess.run(["wineserver", "-p"], env=cls.env, timeout=60)
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

    def reg(self, *args):
        return subprocess.run(["wine", "reg"] + list(args), env=self.env,
                              cwd=self.link, timeout=120,
                              capture_output=True).stdout.decode("latin-1")

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
        self.assertIn("56 settings applied", log)

    def test_i2s_off(self):
        key = r"HKLM\System\CurrentControlSet\Services\Class\Media\0003"
        # without the ES1869's key nothing changes, and the log says why
        self.wine("essctl.exe", "/sim", "/i2s=off", "/q")
        log = read(self.path("ESSCTL.LOG")).decode()
        self.assertIn("/i2s=off: the ES1869's driver settings aren't in the "
                      "registry", log)
        self.reg("add", key, "/v", "Driver", "/d", "es1869.vxd", "/f")
        self.reg("add", key + r"\Config", "/v", "Disable Warning", "/t",
                 "REG_BINARY", "/d", "ff", "/f")
        # I2S has the music DAC, then /i2s=off takes it back for FM
        with open(self.path("i2s.ini"), "w", newline="\r\n") as f:
            f.write("[Fields]\nfx.i2s.enable=on\n")
        self.wine("essctl.exe", "/sim", "/load", "i2s.ini", "/save",
                  "i2s1.ini", "/q")
        self.assertIn("fx.i2s.enable=on", read(self.path("i2s1.ini")).decode())
        self.wine("essctl.exe", "/sim", "/load", "i2s.ini", "/i2s=off",
                  "/save", "i2s2.ini", "/q")
        self.assertIn("fx.i2s.enable=off",
                      read(self.path("i2s2.ini")).decode())
        values = self.reg("query", key + r"\Config")
        self.assertIn("ESSWaveTableChip    REG_BINARY    01000000", values)
        self.wine("essctl.exe", "/sim", "/i2s=on", "/q")
        values = self.reg("query", key + r"\Config")
        self.assertIn("ESSWaveTableChip    REG_BINARY    00000000", values)
        log = read(self.path("ESSCTL.LOG")).decode()
        self.assertIn("/i2s=off: FM has the music DAC now", log)
        self.assertIn("/i2s=on: ESS's driver gives the music DAC to I2S", log)

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

    def ess3d(self, *args, log="E3.LOG"):
        """Run ess3d.exe, returns the lines it added to its log."""
        path = self.path(log)
        old = len(read(path).splitlines()) if os.path.exists(path) else 0
        self.wine("ess3d.exe", *args)
        lines = read(path).decode().splitlines() if os.path.exists(path) \
            else []
        # without the time stamps
        return [line.split(" ", 2)[2] for line in lines[old:]]

    def test_ess3d_commands(self):
        def run(*args):
            return self.ess3d("/sim", "/q", "/log=E3.LOG", *args)

        self.assertEqual(run("on", "level", "40"), ["3-D on, level 40 of 63"])
        # in order, each run starts from the simulated chip's reset values
        self.assertEqual(run("ON", "level", "40", "level", "+30", "down", "8",
                             "toggle"), ["3-D off, level 55 of 63"])
        self.assertEqual(run("toggle", "level", "50%", "up"),
                         ["3-D on, level 36 of 63"])
        self.assertEqual(run("on", "hold", "down", "2"),
                         ["3-D on, held in reset, level 0 of 63"])
        self.assertEqual(run("hold", "on", "reset", "show"),
                         ["3-D on, level 0 of 63"])
        self.assertEqual(run("on", "bogus"), ["unknown command: bogus"])
        self.assertEqual(run("level", "64"),
                         ["level 64: the level is 0 to 63, or 0% to 100%"])
        # Wine has no ES1869, and /q without /log= writes ESS3D.LOG
        self.assertEqual(self.ess3d("/q", "toggle", log="ESS3D.LOG"),
                         ["No ES1869 answered at 220h: use /base=, or /sim "
                          "to try ess3d without the card"])
        # without /q the problem shows in the display, which goes away by
        # itself: a message box would wait for a click
        t = time.time()
        self.assertEqual(self.ess3d("/log=E3.LOG", "toggle"),
                         ["No ES1869 answered at 220h: use /base=, or /sim "
                          "to try ess3d without the card"])
        self.assertLess(time.time() - t, 30)

    def test_ess3d_display(self):
        def start(*args):
            return subprocess.Popen(["wine", "ess3d.exe", "/sim",
                                     "/log=E3D.LOG"] + list(args),
                                    env=self.env, cwd=self.link,
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)

        first = start("/t=30000", "on")
        # it writes the log, then shows the display
        deadline = time.time() + 60
        while not os.path.exists(self.path("E3D.LOG")):
            self.assertLess(time.time(), deadline)
            time.sleep(0.1)
        time.sleep(1)
        t = time.time()
        start("/t=5000", "level", "10").wait(timeout=60)
        second = time.time() - t
        first.wait(timeout=60)
        first_after = time.time() - t
        # the second one showed no display of its own, and the first one's
        # went 5 s after the second one's setting instead of after 30 s
        self.assertLess(second, 5)
        self.assertGreater(first_after, 4.5)
        self.assertLess(first_after, 15)
        self.assertEqual(len(read(self.path("E3D.LOG")).splitlines()), 2)

    def test_ess3d_tray(self):
        def logged(text):
            deadline = time.time() + 60
            while True:
                path = self.path("E3T.LOG")
                if os.path.exists(path) and text in read(path).decode():
                    return
                self.assertLess(time.time(), deadline, text)
                time.sleep(0.1)

        # no tray icon: exit does nothing
        self.assertEqual(self.ess3d("/sim", "/q", "/log=E3T.LOG", "exit",
                                    log="E3T.LOG"), [])
        tray = subprocess.Popen(["wine", "ess3d.exe", "/sim", "/log=E3T.LOG",
                                 "tray"], env=self.env, cwd=self.link,
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        logged("tray: started")
        # a second "tray" opens the first one's panel and exits
        self.wine("ess3d.exe", "/sim", "/log=E3T.LOG", "tray")
        logged("tray: panel")
        # a command, then the icon closes
        self.wine("ess3d.exe", "/sim", "/q", "/log=E3T.LOG", "on", "exit")
        tray.wait(timeout=60)
        lines = read(self.path("E3T.LOG")).decode().splitlines()
        self.assertEqual([line.split(" ", 2)[2] for line in lines],
                         ["tray: started", "tray: panel",
                          "3-D on, level 0 of 63", "tray: closed"])

    def test_esfmrec(self):
        self.wine("esfmrec.exe", "/sim", "/t=2", "/q", "FM.WAV")
        data = read(self.path("FM.WAV"))
        # 2 s at 49,716 Hz, 16-bit stereo, after a 44-byte header
        self.assertEqual(data[:4], b"RIFF")
        self.assertEqual(struct.unpack("<IHHIIHH", data[16:36]),
                         (16, 1, 2, 49716, 49716 * 4, 4, 16))
        self.assertEqual(struct.unpack("<I", data[40:44])[0], 2 * 49716 * 4)
        self.assertEqual(len(data), 44 + 2 * 49716 * 4)
        pcm = struct.unpack("<%dh" % ((len(data) - 44) // 2), data[44:])
        left, right = pcm[0::2], pcm[1::2]
        rising = lambda s: sum(a < 0 <= b for a, b in zip(s, s[1:]))  # noqa
        self.assertLessEqual(abs(rising(left) - 2000), 1)    # 1 kHz
        self.assertLessEqual(abs(rising(right) - 1000), 1)   # 500 Hz
        self.assertEqual(max(map(abs, left)), 16383)          # -6 dB
        log = read(self.path("ESFMREC.LOG")).decode()
        self.assertIn("FM.WAV: 2.0 s, 397728 bytes at 49716 Hz", log)
        # /raw: the same samples without the header
        self.wine("esfmrec.exe", "/sim", "/t=1", "/raw", "/q", "FM.PCM")
        raw = read(self.path("FM.PCM"))
        self.assertEqual(len(raw), 49716 * 4)
        self.assertEqual(raw[:4000], data[44:4044])

    def wav_lengths(self, name):
        data = read(self.path(name))
        return len(data), struct.unpack("<I", data[4:8])[0] + 8, \
            struct.unpack("<I", data[40:44])[0]

    def test_esfmrec_ended_by_force(self):
        rec = subprocess.Popen(["wine", "esfmrec.exe", "/sim", "/q",
                                "/log=KILL.LOG", "KILL.WAV"], env=self.env,
                               cwd=self.link, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
        deadline = time.time() + 60
        while not os.path.exists(self.path("KILL.LOG")):
            self.assertLess(time.time(), deadline)
            time.sleep(0.1)
        # past the first save at 5 s, then End Task
        time.sleep(7)
        self.wine("taskkill", "/f", "/im", "winevdm.exe")
        rec.wait(timeout=60)
        self.assertTrue(os.path.exists(self.path("ESFMSIM.RST")))
        size, riff, data = self.wav_lengths("KILL.WAV")
        self.assertGreaterEqual(data, 5 * 49716 * 4)
        self.assertLess(data, size - 44)
        # the next start repairs the header and removes the marker
        self.wine("esfmrec.exe", "/sim", "/t=1", "/q", "/log=KILL.LOG",
                  "NEXT.WAV")
        size, riff, data = self.wav_lengths("KILL.WAV")
        self.assertEqual(riff, size)
        self.assertEqual(data, (size - 44) // 4 * 4)
        self.assertFalse(os.path.exists(self.path("ESFMSIM.RST")))
        log = read(self.path("KILL.LOG")).decode()
        self.assertIn("ended by force", log)
        self.assertIn("KILL.WAV is repaired, 0:00:0", log)
        self.assertIn("NEXT.WAV: 1.0 s", log)

    def test_esfmrec_disk_full(self):
        def limit():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (600 * 1024,) * 2)

        subprocess.run(["wine", "esfmrec.exe", "/sim", "/t=10", "/q",
                        "/log=FULL.LOG", "FULL.WAV"], env=self.env,
                       cwd=self.link, timeout=120, preexec_fn=limit,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        size, riff, data = self.wav_lengths("FULL.WAV")
        self.assertLessEqual(size, 600 * 1024)
        self.assertEqual(riff, size)
        self.assertEqual(data, (size - 44) // 4 * 4)
        log = read(self.path("FULL.LOG")).decode()
        self.assertIn("FULL.WAV failed: is the disk full?", log)
        self.assertFalse(os.path.exists(self.path("ESFMSIM.RST")))

    def test_esfmrec_split(self):
        self.wine("esfmrec.exe", "/sim", "/t=3", "/split=1", "/q", "SP.WAV")
        pcm = b""
        for name in ("SP.WAV", "SP_2.WAV", "SP_3.WAV"):
            data = read(self.path(name))
            self.assertEqual(len(data), 44 + 49716 * 4)
            pcm += data[44:]
        self.assertFalse(os.path.exists(self.path("SP_4.WAV")))
        # the tone runs on across the files
        left = struct.unpack("<%dh" % (len(pcm) // 2), pcm)[0::2]
        rising = sum(a < 0 <= b for a, b in zip(left, left[1:]))
        self.assertLessEqual(abs(rising - 3000), 1)


if __name__ == "__main__":
    unittest.main()
