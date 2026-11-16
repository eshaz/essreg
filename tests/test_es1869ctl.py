# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0 or later
"""es1869ctl, the panel of snd-es1869, on files that stand in for the
machine's /sys and /proc: every setting on one page, the pages as the
generator's groups, what the status page says, and what it writes.

The screen itself is ncurses', so these go through --dump and --set, which
build the same lines and take the same path to a setting.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "linux", "tools", "es1869ctl")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import es1869set  # noqa: E402

PROC = """ES1869 at 0x220, Config_Base 0x250, FM 0x388, MPU-401 0x330
IRQ 5, Audio 1 on DMA 1, Audio 2 on DMA 5 (bytes spread over words)
DSP 68 8B, mixer 18 69
Audio 1: none; Audio 2: Audio 2; power: up

interrupts: Audio 1 0, Audio 2 975, buttons 14, MPU-401 0, none 0
DSP busy 0, out of step 0; position rereads 2250; settles 0
stuck sources 0, taken off the line 00
Audio 2 ring: late refills 0, largest 8192 bytes, longest 51544 cycles
"""

HW_PARAMS = """access: RW_INTERLEAVED
format: S16_LE
subformat: STD
channels: 2
rate: %d (%d/1)
period_size: 4096
buffer_size: 16384
"""


class Es1869ctlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.addClassCleanup(shutil.rmtree, cls.tmp)
        cls.exe = os.path.join(cls.tmp, "es1869ctl")
        libs = subprocess.run("pkg-config --libs ncurses", shell=True,
                              capture_output=True, text=True).stdout.split()
        res = subprocess.run(
            ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-O1", "-o",
             cls.exe, os.path.join(TOOL, "es1869ctl.c"),
             os.path.join(ROOT, "linux", "test", "settab.c"), "-lasound"] +
            (libs or ["-lncurses"]), capture_output=True, text=True)
        if res.returncode:
            if "No such file" in res.stderr:
                raise unittest.SkipTest("needs the ncurses and ALSA headers")
            raise AssertionError(res.stderr)
        cls.rows = es1869set.parse()

    def setUp(self):
        self.root = tempfile.mkdtemp(dir=self.tmp)
        self.params = os.path.join(self.root, "sys/module/snd_es1869",
                                   "parameters")
        os.makedirs(self.params)
        self.proc = os.path.join(self.root, "proc/asound/ES1869")
        os.makedirs(self.proc)
        self.write(os.path.join(self.proc, "es1869"), PROC)
        for r in self.rows:
            self.param(r["id"], self.default(r))

    @staticmethod
    def write(path, text):
        with open(path, "w", encoding="ascii") as f:
            f.write(text)

    @staticmethod
    def default(r):
        if r["kind"] == "S_STR":
            return r["items"]
        v = str(r["def"])
        if r["kind"] in ("S_STEREO", "S_MASTER", "S_MASTERSW", "S_SMAG"):
            return "%s,%s" % (v, v)
        return v

    def param(self, name, text):
        self.write(os.path.join(self.params, name), text + "\n")

    def read_param(self, name):
        with open(os.path.join(self.params, name), encoding="ascii") as f:
            return f.read().strip()

    def stream(self, directory, rate):
        d = os.path.join(self.proc, directory)
        os.makedirs(d, exist_ok=True)
        self.write(os.path.join(d, "hw_params"), HW_PARAMS % (rate, rate))
        self.write(os.path.join(d, "status"), "state: RUNNING\n")

    def run_tool(self, *args):
        return subprocess.run([self.exe, "--root", self.root] + list(args),
                              capture_output=True, text=True)

    def dump(self):
        res = self.run_tool("--dump")
        self.assertEqual(res.returncode, 0, res.stderr)
        return res.stdout

    def page(self, title):
        return self.dump().split("== %s\n" % title)[1].split("\n== ")[0]

    def test_pages_are_the_generators_groups(self):
        with open(os.path.join(TOOL, "es1869ctl.c"), encoding="ascii") as f:
            source = f.read()
        pages = re.findall(r'\{ "([^"]+)", "(\w+)" \},\n', source.split(
            "pages[] = {")[1].split("};")[0])
        self.assertEqual(tuple(pages), es1869set.GROUPS)

    def test_every_setting_is_on_a_page(self):
        pages = [p for p in self.dump().split("== ") if p]
        rows = sum(len(p.strip().split("\n")) - 1 for p in pages[1:])
        self.assertEqual(rows, len(self.rows))

    def test_defaults_carry_no_mark(self):
        text = self.dump().split("== Output")[1]
        self.assertNotIn(" *", text)

    def test_a_changed_value_is_marked(self):
        self.param("capture_volume", "11,11")
        line = [ln for ln in self.page("Record").split("\n")
                if "ADC Level" in ln][0]
        self.assertTrue(line.endswith(" *"), line)
        self.assertIn("11,11", line)

    def test_set_goes_to_the_parameter(self):
        for arg, name, want in (("capture_source=Line", "capture_source", "6"),
                                ("capture_source=2", "capture_source", "2"),
                                ("capture_volume=3,5", "capture_volume", "3,5"),
                                ("capture_volume=7", "capture_volume", "7,7"),
                                ("mic_boost=off", "mic_boost", "0"),
                                ("adc_offset=-16", "adc_offset", "-16,-16"),
                                ("esfm_bank=x.bin", "esfm_bank", "x.bin")):
            res = self.run_tool("--set", arg)
            self.assertEqual(res.returncode, 0, arg + res.stderr)
            self.assertEqual(self.read_param(name), want, arg)

    def test_bad_values_change_nothing(self):
        for arg in ("capture_volume=16", "capture_source=Nothing",
                    "mic_boost=maybe", "master_volume=64", "no_such=1",
                    "capture_volume"):
            res = self.run_tool("--set", arg)
            self.assertEqual(res.returncode, 1, arg)
        self.assertEqual(self.read_param("capture_volume"), "4,4")
        self.assertEqual(self.read_param("capture_source"), "5")

    def test_type_f_stays_out_of_reach(self):
        res = self.run_tool("--set", "piix4_type_f=0")
        self.assertEqual(res.returncode, 1)
        self.assertEqual(self.read_param("piix4_type_f"), "1")

    def test_a_setting_the_driver_lacks(self):
        os.remove(os.path.join(self.params, "beep_follow"))
        self.assertIn("not in this driver", self.page("Output"))

    def test_status_routes_the_adc(self):
        self.param("capture_source", "0")
        self.param("capture_volume", "3,3")
        text = self.page("Status")
        self.assertIn("ADC input   Mic: the microphone", text)
        self.assertIn("6 dB on the mic's path", text)

    def test_status_names_what_feeds_the_record_mixer(self):
        self.assertIn("nothing: every level is 0", self.page("Status"))
        self.param("line_capture", "5,5")
        self.param("fm_capture", "3,3")
        text = self.page("Status")
        self.assertIn("Into it", text)
        self.assertIn("Line, FM", text)

    def test_status_says_when_recordings_carry_dc(self):
        self.assertNotIn("carry", self.page("Status"))
        self.param("adc_offset", "-16,-16")
        self.assertIn("recordings carry -1024,-1024 of DC",
                      self.page("Status"))

    def test_streams_and_their_real_rates(self):
        self.stream("pcm0p/sub0", 48000)
        self.stream("pcm0c/sub0", 44100)
        text = self.page("Status")
        self.assertIn("48000 Hz, really 48012.10 Hz", text)
        self.assertRegex(text, r"Recording +RUNNING S16_LE, 2 ch, 44100 Hz,")
        self.assertRegex(text, r"Audio 1 +closed")

    def test_the_laid_out_lines_fit_a_console(self):
        for page in ("Output", "Record", "DACs", "Rate", "FM"):
            for line in self.page(page).split("\n"):
                self.assertLessEqual(len(line), 78, line)


if __name__ == "__main__":
    unittest.main()
