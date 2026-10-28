# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""build/ESSCHECK.BAT, the checks of a machine in one run, read the way
MS-DOS 7's COMMAND.COM reads it: CR LF lines it can take, no command of
Windows NT's, every label it jumps to there once in its first eight
characters and none the start of another, the programs it runs in build/
and the options those take, the essprobe cases named, prompts and lines
that COMMAND.COM and CHOICE pass on as they are written, and its way
through each case on a model of COMMAND.COM.

usage: python3 -m unittest tests.test_esscheck
"""

import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAT = os.path.join(ROOT, "build", "ESSCHECK.BAT")
NT_ONLY = (r"\bSETLOCAL\b", r"\bENDLOCAL\b", r"\bPUSHD\b", r"\bPOPD\b",
           r"\bCALL\s+:", r"%~", r"%CD%", r"\bIF\s+/I\b", r"\bIF\s+DEFINED\b",
           r"\bFOR\s+/[FDRL]\b", r"\bEXIT\s+/B\b", r"\bGOTO\s+:EOF\b",
           r"&&", r"\|\|", r"\(\s*$", r"^\s*\)", r"!\w+!")
# what each program takes, as its own parser does
OPTIONS = {"ESSPROBE": re.compile(r"^/(R|S|A|N|T|C:[\w.,]+|O:\S+)$"),
           "SB16TEST": re.compile(r"^/(A|L|Q|R|T|H:[0-7]|O:\S+)$"),
           "ESSSB16": re.compile(r"^(/H[0567]|%H%)$")}


def lines(bat=BAT):
    with open(bat, "rb") as f:
        data = f.read()
    return data


def commands(bat=BAT):
    """the lines that aren't remarks, without CR LF"""
    text = lines(bat).decode("ascii")
    return [s for s in text.split("\r\n")
            if s.strip() and not s.upper().startswith("REM")]


def case_names():
    with open(os.path.join(ROOT, "src", "essp.c"), encoding="latin-1") as f:
        return set(re.findall(r'^\s*\{\s*"([a-z0-9]+\.\w+)"', f.read(),
                              re.M))


class BatchChecks:
    """what each batch file has to be: `bat`, running `programs`"""

    bat = BAT
    programs = set()

    def test_lines(self):
        """CR LF throughout, ASCII, within the 127 characters of a line"""
        data = lines(self.bat)
        self.assertTrue(data.endswith(b"\r\n"))
        self.assertNotIn(b"\n", data.replace(b"\r\n", b""))
        data.decode("ascii")
        for line in data.split(b"\r\n"):
            self.assertLessEqual(len(line), 127, line)

    def test_dos_only(self):
        for line in commands(self.bat):
            for pattern in NT_ONLY:
                self.assertIsNone(re.search(pattern, line, re.I),
                                  "%s: %s" % (pattern, line))

    def test_labels(self):
        """every jump has its label, labels differ in their first eight
        characters and none starts another, and GOTO %RET% returns to
        labels only"""
        cmds = commands(self.bat)
        labels = [c[1:].split()[0].upper() for c in cmds if c.startswith(":")]
        short = [x[:8] for x in labels]
        self.assertEqual(len(short), len(set(short)), labels)
        for a in short:
            for b in short:
                self.assertFalse(a != b and b.startswith(a), (a, b))
        targets = set()
        for c in cmds:
            m = re.search(r"\bGOTO\s+(\S+)", c, re.I)
            if m:
                targets.add(m.group(1).upper())
            m = re.match(r"SET RET=(\w+)$", c, re.I)
            if m:
                targets.add(m.group(1).upper())
        self.assertIn("%RET%", targets)
        targets.discard("%RET%")
        self.assertLessEqual(targets, set(labels))

    def test_programs(self):
        """the programs it runs from its folder are in build/ (HDPMI32i
        comes from third_party/), with options they take"""
        seen = set()
        for c in commands(self.bat):
            run = re.sub(r"^IF\s+(NOT\s+)?EXIST\s+\S+\s+", "", c, flags=re.I)
            run = re.sub(r"^LH\s+", "", run, flags=re.I)
            m = re.match(r"%ESSDIR%\\(\w+)(\.\w+)?((?:\s+[^\s>]+)*)", run)
            if m:
                name, args = m.group(1).upper(), m.group(3).split()
                seen.add(name)
                if name == "HDPMI32I":
                    self.assertTrue(os.path.exists(os.path.join(
                        ROOT, "third_party", "hdpmi32i", "HDPMI32i.EXE")))
                    continue
                ext = {"ESSSB16": ".com"}.get(name, ".exe")
                self.assertTrue(os.path.exists(os.path.join(
                    ROOT, "build", name.lower() + ext)), name)
                for a in args:
                    self.assertRegex(a, OPTIONS[name], c)
        self.assertEqual(seen, self.programs)

    def test_prompts(self):
        """CHOICE gets text without a slash, comma, semicolon or equals
        sign, which it would take apart, and ECHO lines redirect at their
        end only"""
        for c in commands(self.bat):
            if c.upper().startswith("CHOICE"):
                text = re.sub(r"^CHOICE((\s+/\w(:\w+)?)+)", "", c, flags=re.I)
                self.assertIsNone(re.search(r"[/,;=]", text), c)
            if c.upper().startswith("ECHO") or " ECHO " in c.upper():
                body = re.sub(r"\s>>?\s*%[A-Z]+%\\?[\w.]*$", "", c)
                self.assertIsNone(re.search(r"[<>|]", body), c)


class BatchTest(BatchChecks, unittest.TestCase):
    bat = BAT
    programs = {"ESSSB16", "ESSPROBE", "SB16TEST", "HDPMI32I"}

    def test_probe_cases(self):
        """its /C: names select the cases they mean: the stream group, the
        pace's three among them"""
        names = case_names()
        picked = {}
        for c in commands():
            m = re.search(r"\\ESSPROBE .*/C:([\w.,]+)", c)
            if m:
                for prefix in m.group(1).split(","):
                    picked[prefix] = sorted(n for n in names
                                            if n.startswith(prefix))
        self.assertEqual(list(picked), ["stream"])
        self.assertEqual(len(picked["stream"]), 8)
        self.assertTrue({"stream.mirror", "stream.pace", "stream.pacec",
                         "stream.pacew"} <= set(picked["stream"]))

    def test_probe_without_window(self):
        """essprobe runs without its window (/N), so that EMM386 keeps no
        trap of its for esssb16 to meet"""
        runs = [c for c in commands() if "\\ESSPROBE " in c]
        self.assertTrue(runs)
        for c in runs:
            self.assertIn(" /N ", c)


class Stopped(Exception):
    """a program that stopped the machine"""


class Command:
    """MS-DOS 7's COMMAND.COM on a batch file, as far as ESSCHECK.BAT
    goes: variables and arguments, IF with strings, EXIST,
    ERRORLEVEL and NOT, GOTO to the first label of its first eight
    characters, SET, CD, C:, CALL, FIND in the log, and the programs, whose
    exit codes and CHOICE's keys come from the scenario; a program whose
    code is "stop" stops the machine. `ran` is what ran, `log` what went
    into the log (`log` given: the log of a run before, there already)"""

    def __init__(self, args, exists=(), codes=None, keys="", bat=BAT,
                 log=()):
        self.env = {}
        self.args = list(args)
        self.exists = {p.upper() for p in exists}
        self.codes = codes or {}
        self.keys = list(keys)
        self.bat = bat
        self.level = 0
        self.cwd = "C:\\"
        self.ran, self.log, self.out = [], list(log), []
        self.stopped = False

    def expand(self, line):
        def var(m):
            name = m.group(1)
            if name.isdigit():
                i = int(name) - 1
                return self.args[i] if 0 <= i < len(self.args) else ""
            return self.env.get(name.upper(), "")
        return re.sub(r"%(\w+)%|%(\d)", lambda m: var(
            re.match(r"(\w+)", m.group(1) or m.group(2))), line)

    def run(self):
        text = [s for s in lines(self.bat).decode("ascii").split("\r\n")]
        labels = {}
        for i, s in enumerate(text):
            if s.startswith(":"):
                labels.setdefault(s[1:].split()[0][:8].upper(), i)
        pc, steps = 0, 0
        while pc < len(text):
            steps += 1
            if steps > 2000:
                raise AssertionError("the batch goes round")
            line, pc = text[pc], pc + 1
            if not line.strip() or line.startswith(":") or \
                    line.upper().startswith("REM") or line == "@ECHO OFF":
                continue
            try:
                target = self.do(self.expand(line))
            except Stopped:
                self.stopped = True
                break
            if target is not None:
                key = target[:8].upper()
                if key not in labels:
                    raise AssertionError("label not found: " + target)
                pc = labels[key] + 1
        return self

    def do(self, line):
        """a command; the label to go to, if it goes"""
        m = re.match(r"IF\s+(NOT\s+)?(.*)", line, re.I)
        if m:
            neg, rest = bool(m.group(1)), m.group(2)
            c = re.match(r'"([^"]*)"=="([^"]*)"\s+(.*)', rest)
            if c:
                cond, cmd = c.group(1) == c.group(2), c.group(3)
            else:
                c = re.match(r"(EXIST|ERRORLEVEL)\s+(\S+)\s+(.*)", rest,
                             re.I)
                if c.group(1).upper() == "EXIST":
                    path = c.group(2).upper()
                    if not re.match(r"[A-Z]:\\", path):
                        path = self.cwd.rstrip("\\") + "\\" + path
                    if path == self.env.get("L", "").upper():
                        cond = bool(self.log)
                    else:
                        cond = path in self.exists
                else:
                    cond = self.level >= int(c.group(2))
                cmd = c.group(3)
            return self.do(cmd) if cond != neg else None
        m = re.match(r"GOTO\s+(\S+)", line, re.I)
        if m:
            return m.group(1)
        redirect = re.search(r"\s(>>?)\s*(\S+)$", line)
        if redirect:
            line = line[:redirect.start()]
        if line.upper().startswith("ECHO"):
            said = line[4:].lstrip(" .")
            if redirect and redirect.group(2) == self.env.get("L"):
                self.log.append(said)
            else:
                self.out.append(said)
            return None
        m = re.match(r"SET\s+(\w+)=(.*)", line, re.I)
        if m:
            if m.group(2):
                self.env[m.group(1).upper()] = m.group(2)
            else:
                self.env.pop(m.group(1).upper(), None)
            return None
        word = line.split()[0].upper()
        if word == "C:":
            return None
        if word == "CD":
            self.cwd = line.split()[1].upper()
            return None
        if word in ("SET", "VER", "TYPE", "MD", "PAUSE"):
            return None
        if "|" in line:                 # MEM /C | FIND: FIND's answer
            self.ran.append("PIPE")
            self.level = self.codes.get("PIPE", 1)
            return None
        if word == "FIND":              # in a file: 0 found, 1 not
            m = re.match(r'FIND\s+"([^"]*)"\s+(\S+)', line, re.I)
            if m and m.group(2).upper() == self.env.get("L", "").upper():
                self.level = 0 if any(m.group(1) in t for t in self.log) \
                    else 1
                return None
            self.ran.append("FIND")
            self.level = self.codes.get("FIND", 1)
            return None
        if word == "LH":
            line = line[2:].strip()
        if word == "CALL":
            line = line[4:].strip()
        name = line.split()[0].upper()
        name = name.split("\\")[-1].split(".")[0]
        if word == "MEM":
            self.level = 0
            return None
        args = line.split()[1:]
        if name == "CHOICE":
            keys = re.search(r"/C:(\w+)", line).group(1).upper()
            key = self.keys.pop(0)
            self.ran.append("CHOICE " + key)
            self.level = keys.index(key) + 1
            return None
        self.ran.append(" ".join([name] + [a for a in args
                                           if not a.startswith("/O:")]))
        code = self.codes.get(name, 0)
        if isinstance(code, list):
            code = code.pop(0)
        if code == "stop":
            raise Stopped(name)
        self.level = code
        return None


ESS = ("C:\\ESS\\ESSSB16.COM", "C:\\ESS\\HDPMI32I.EXE",
       "C:\\ESS\\RESULTS\\NUL", "C:\\CONFIG.SYS", "C:\\AUTOEXEC.BAT")
GAMES = ("C:\\21STCENT\\PWORLD\\SETSND.EXE", "C:\\DREAMS\\PD.EXE",
         "C:\\DREAMS\\SOUND.CFG", "C:\\21STCENT\\ABSOLUTE\\ABS.EXE")


class FlowTest(unittest.TestCase):
    """the batch's way through each case, on a model of COMMAND.COM"""

    def test_everything(self):
        """the probe recorded, both loads, sb16test with BLASTER's DMA and
        on 5, then the games on 5: Pinball World's SETSND and the game,
        Pinball Dreams, Absolute Pinball's SETUP and the game; the answers
        logged"""
        c = Command([], ESS + GAMES, {"ESSSB16": [0] + [5] * 7},
                    "RYYYNY").run()
        self.assertEqual(c.ran, [
            "CHOICE R", "ESSPROBE /R /N /C:stream", "HDPMI32I -r -x",
            "ESSSB16", "SB16TEST /A /Q", "ESSSB16 /H5",
            "SB16TEST /A /Q /H:5", "ESSSB16 /H5",
            "SETSND", "ESSSB16", "CHOICE Y", "PWORLD", "ESSSB16", "CHOICE Y",
            "PD", "ESSSB16", "CHOICE Y",
            "SETUP", "ESSSB16", "CHOICE N", "ABS", "ESSSB16", "CHOICE Y"])
        self.assertEqual(c.log[-6:], [
            "SETSND tune with DMA 5: yes", "Pinball World with DMA 5: yes",
            "Pinball Dreams with DMA 5: yes",
            "Absolute Pinball's test sound with DMA 5: no",
            "Absolute Pinball with DMA 5: yes", "done"])
        self.assertIn("essprobe: STREAM.TXT", c.log)
        self.assertIn("sb16test on DMA 5: SB16H5.TXT", c.log)
        self.assertEqual(c.env["PW"], "C:\\21STCENT\\PWORLD")
        self.assertEqual(c.env["PD"], "C:\\DREAMS")
        self.assertEqual(c.env["AP"], "C:\\21STCENT\\ABSOLUTE")
        self.assertEqual(c.cwd, "C:\\ESS")

    def test_run_bat(self):
        """a game folder with RUN.BAT, which starts the game from there"""
        c = Command(["C:\\PW"], ESS + ("C:\\PW\\RUN.BAT",),
                    {"ESSSB16": [0] + [5] * 4}, "SYY").run()
        self.assertIn("RUN", c.ran)
        self.assertNotIn("PWORLD", c.ran)
        self.assertEqual(c.ran.count("RUN"), 1)
        self.assertEqual(c.log[-1], "done")

    def test_no_sound_cfg(self):
        """Pinball Dreams without SOUND.CFG: left out, as its SETSOUND
        can't run under EMM386 with much memory"""
        c = Command(["", "C:\\PD"], ESS + ("C:\\PD\\PD.EXE",),
                    {"ESSSB16": [0] + [5] * 2}, "S").run()
        self.assertNotIn("PD", c.ran)
        self.assertIn("Pinball Dreams: no SOUND.CFG", c.log)
        self.assertEqual(c.log[-1], "done")

    def test_esssb16_in_already(self):
        """no probe, then the copy in memory checked and on to the rest"""
        c = Command([], ESS, {"ESSPROBE": 4, "ESSSB16": 5}, "S").run()
        self.assertEqual(c.ran, ["CHOICE S", "ESSPROBE /N /C:stream",
                                 "ESSSB16", "SB16TEST /A /Q", "ESSSB16 /H5",
                                 "SB16TEST /A /Q /H:5", "ESSSB16 /H5"])
        self.assertIn("essprobe: skipped, esssb16 in already", c.log)
        self.assertEqual(c.log[-1], "done")

    def test_no_card(self):
        c = Command([], ESS, {"ESSPROBE": 2}, "S").run()
        self.assertEqual(c.ran, ["CHOICE S", "ESSPROBE /N /C:stream"])
        self.assertIn("stopped: see the lines above", c.log)

    def test_no_traps(self):
        """esssb16 refuses: what it said is on the screen and in the log,
        then nothing after it"""
        c = Command([], ESS, {"ESSSB16": 3}, "S").run()
        self.assertEqual(c.ran, ["CHOICE S", "ESSPROBE /N /C:stream",
                                 "HDPMI32I -r -x", "ESSSB16"])
        self.assertIn("stopped: see the lines above", c.log)
        self.assertEqual(c.env["E"], "1")

    def test_failed_cases_go_on(self):
        """cases that failed are in the report: on to esssb16"""
        c = Command([], ESS, {"ESSPROBE": 6}, "R").run()
        self.assertEqual(c.ran[:3], ["CHOICE R", "ESSPROBE /R /N /C:stream",
                                     "HDPMI32I -r -x"])
        self.assertIn("essprobe: STREAM.TXT", c.log)
        self.assertEqual(c.log[-1], "done")

    def test_no_memory(self):
        """essprobe without the memory for its buffers: on to esssb16"""
        c = Command([], ESS, {"ESSPROBE": 5}, "S").run()
        self.assertEqual(c.ran[:3], ["CHOICE S", "ESSPROBE /N /C:stream",
                                     "HDPMI32I -r -x"])

    def test_run_again(self):
        """run again with the probe's reports there: on to esssb16, or the
        probe again"""
        there = ESS + ("C:\\ESS\\RESULTS\\STREAM.TXT",)
        c = Command([], there, {"ESSSB16": 0}, "S").run()
        self.assertEqual(c.ran, ["CHOICE S", "HDPMI32I -r -x", "ESSSB16",
                                 "SB16TEST /A /Q", "ESSSB16 /H5",
                                 "SB16TEST /A /Q /H:5", "ESSSB16 /H5"])
        c = Command([], there, {}, "RS").run()
        self.assertEqual(c.ran[:3], ["CHOICE R", "CHOICE S",
                                     "ESSPROBE /N /C:stream"])

    def test_not_there(self):
        """the programs elsewhere: it says so and stops"""
        c = Command([], ()).run()
        self.assertEqual(c.ran, [])
        self.assertIn("The programs aren't in C:\\ESS", c.out[0])
