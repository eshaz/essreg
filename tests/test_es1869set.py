# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0 or later
"""The Linux driver's generated files and its settings table.

linux/es1869/regs.h comes from src/esscat.tbl (tools/linuxregs.py), and the
Kconfig, oot_config.h and docs/LINUX.md's settings list from
linux/es1869/settings.def (tools/es1869set.py): each must be current. The
table itself must hold what the driver relies on: valid C names that don't
clash with the resource parameters, defaults inside their ranges, enum
texts for every value of an enum, the stock driver's control names and
types where it had them, and a Kconfig symbol for every row.

usage: python3 -m unittest tests.test_es1869set
"""

import os
import re
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import es1869set  # noqa: E402

RESOURCE_PARAMS = {"index", "id", "enable", "isapnp", "port", "fm_port",
                   "mpu_port", "irq", "dma1", "dma2"}
# snd-es18xx's controls on the ES1869, as celeronix's asound.state saved
# them: name, index, kind, count, range
STOCK = [
    ("Master Playback Volume", 0, "S_MASTER", 2, (0, 63)),
    ("Master Playback Switch", 0, "S_MASTERSW", 2, (0, 1)),
    ("Line Playback Volume", 0, "S_STEREO", 2, (0, 15)),
    ("CD Playback Volume", 0, "S_STEREO", 2, (0, 15)),
    ("FM Playback Volume", 0, "S_STEREO", 2, (0, 15)),
    ("Mic Playback Volume", 0, "S_STEREO", 2, (0, 15)),
    ("Aux Playback Volume", 0, "S_STEREO", 2, (0, 15)),
    ("Record Monitor", 0, "S_BOOL", 1, (0, 1)),
    ("Capture Volume", 0, "S_STEREO", 2, (0, 15)),
    ("Capture Source", 0, "S_ENUM", 1, (0, 7)),
    ("PCM Playback Volume", 0, "S_STEREO", 2, (0, 15)),
    ("PCM Playback Volume", 1, "S_STEREO", 2, (0, 15)),
    ("PCM Capture Volume", 0, "S_STEREO", 2, (0, 15)),
    ("Mic Capture Volume", 0, "S_STEREO", 2, (0, 15)),
    ("Line Capture Volume", 0, "S_STEREO", 2, (0, 15)),
    ("FM Capture Volume", 0, "S_STEREO", 2, (0, 15)),
    ("CD Capture Volume", 0, "S_STEREO", 2, (0, 15)),
    ("Aux Capture Volume", 0, "S_STEREO", 2, (0, 15)),
    ("Mic Boost (+26dB)", 0, "S_BOOL", 1, (0, 1)),
    ("3D Control - Level", 0, "S_INT", 1, (0, 63)),
    ("3D Control - Switch", 0, "S_BOOL", 1, (0, 1)),
    ("Hardware Master Volume Split", 0, "S_BOOL", 1, (0, 1)),
    ("Beep Playback Volume", 0, "S_INT", 1, (0, 7)),
    ("Capture Switch", 0, "S_BOOL", 1, (0, 1)),
    ("Video Playback Switch", 0, "S_BOOL", 1, (0, 1)),
    ("Mono Playback Volume", 0, "S_STEREO", 2, (0, 15)),
    ("Mono Capture Volume", 0, "S_STEREO", 2, (0, 15)),
    ("GPO0 Switch", 0, "S_BOOL", 1, (0, 1)),
    ("GPO1 Switch", 0, "S_BOOL", 1, (0, 1)),
]
STOCK_SOURCES = ["Mic", "Mic Master", "CD", "AOUT", "Mic1", "Mix", "Line",
                 "Master"]


def tool(name, *args):
    return subprocess.run([sys.executable, os.path.join(ROOT, "tools", name)]
                          + list(args), capture_output=True, text=True)


class GeneratedTest(unittest.TestCase):
    def test_regs_h_is_current(self):
        r = tool("linuxregs.py", "--check")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_kconfig_and_oot_config_are_current(self):
        r = tool("es1869set.py", "--check")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


class TableTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = es1869set.parse()
        cls.by_ctl = {(r["control"], r["index"]): r for r in cls.rows
                      if r["control"]}

    def test_names(self):
        for r in self.rows:
            self.assertRegex(r["id"], r"^[a-z][a-z0-9_]*$")
            self.assertNotIn(r["id"], RESOURCE_PARAMS)
            self.assertLessEqual(len(r["id"]), 40)

    def test_defaults_in_range(self):
        for r in self.rows:
            if r["kind"] == "S_STR":
                self.assertTrue(r["items"], r["id"])
                continue
            self.assertLessEqual(r["min"], r["def"], r["id"])
            self.assertLessEqual(r["def"], r["max"], r["id"])

    def test_enums_name_every_value(self):
        for r in self.rows:
            if r["kind"] == "S_ENUM":
                items = r["items"].split("|")
                self.assertEqual(len(items), r["max"] + 1, r["id"])
                self.assertEqual(r["min"], 0, r["id"])

    def test_fields_fit_their_register(self):
        for r in self.rows:
            if r["bank"] == "B_NONE" or r["kind"] in ("S_STEREO",
                                                      "S_MASTER"):
                continue
            self.assertLessEqual(r["shift"] + r["width"], 8, r["id"])
            if r["kind"] in ("S_INT", "S_ENUM"):
                self.assertLessEqual(r["max"], (1 << r["width"]) - 1,
                                     r["id"])

    def test_stock_controls_kept(self):
        """alsactl restores the saved state by name and index, and an enum
        by its item's text"""
        for name, index, kind, count, rng in STOCK:
            r = self.by_ctl.get((name, index))
            self.assertIsNotNone(r, name)
            self.assertEqual(r["kind"], kind, name)
            self.assertEqual((r["min"], r["max"]), rng, name)
        self.assertEqual(self.by_ctl[("Capture Source", 0)]["items"]
                         .split("|"), STOCK_SOURCES)

    def test_controls_unique(self):
        keys = [(r["control"], r["index"]) for r in self.rows
                if r["control"]]
        self.assertEqual(len(keys), len(set(keys)))

    def test_control_names_fit(self):
        """ALSA keeps 43 characters of a control's name and 63 of an enum
        item's, and cuts the rest when the control is added, so that
        alsactl and amixer would find it only by the shorter name"""
        for r in self.rows:
            if r["control"]:
                self.assertLessEqual(len(r["control"]), 43, r["control"])
            if r["kind"] == "S_ENUM":
                for item in r["items"].split("|"):
                    self.assertLessEqual(len(item), 63, (r["id"], item))

    def test_alsamixer_shows_the_mixer(self):
        """alsamixer shows the mixer interface: snd-es18xx's controls, which
        saved states hold there, and Audio 2's output rate, rate correction
        and treble correction; every other control is on the card
        interface"""
        shown = {(r["control"], r["index"]) for r in self.rows
                 if r["control"] and "F_MIXER" in r["flags"]}
        want = {(name, index) for name, index, _k, _c, _r in STOCK} | {
            ("Audio 2 Output Rate", 0), ("Audio 2 Rate Correction", 0),
            ("Audio 2 Treble Correction Playback Switch", 0)}
        self.assertEqual(shown, want)

    def test_corrections_off_by_default(self):
        """Audio 1's correction and linearity stage, the zero step, the
        treble correction and Audio 2's output rate above 1x stay off unless
        asked for"""
        for sid in ("a1c_filter", "a1c_linear", "a2_zstep", "a2_treble",
                    "a2_oversample"):
            r = next(r for r in self.rows if r["id"] == sid)
            self.assertEqual(r["def"], 0, sid)

    def test_dac_stages_off_by_default(self):
        """1 is ESS's setting of a stage, the default 0 turns it off, as
        DRIVER_CONFIG.md has it for the rebuilt Windows drivers"""
        for sid in ("audio2_oversampling", "audio2_filter", "audio1_filter"):
            r = next(r for r in self.rows if r["id"] == sid)
            self.assertEqual(r["def"], 0, sid)
            self.assertIn("F_CTL", r["flags"], sid)

    def test_kconfig_symbols(self):
        with open(es1869set.KCONFIG, encoding="ascii") as f:
            kconfig = f.read()
        for r in self.rows:
            sym = "config SND_ES1869_DEFAULT_" + r["ID"]
            self.assertIn(sym + "\n", kconfig)
            block = kconfig.split(sym + "\n", 1)[1].split("\nconfig ", 1)[0]
            if r["kind"] == "S_STR":
                self.assertIn('default "%s"' % r["items"], block)
            else:
                self.assertIn("default %d\n" % r["def"], block)
                self.assertIn("range %d %d\n" % (r["min"], r["max"]), block)

    def test_settings_def_has_no_lowercase_kconfig_ids(self):
        for r in self.rows:
            self.assertTrue(re.fullmatch(r"[A-Z0-9_]+", r["ID"]), r["ID"])


if __name__ == "__main__":
    unittest.main()
