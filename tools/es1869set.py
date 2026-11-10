#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0 or later
"""Generate the Linux driver's Kconfig, oot_config.h and settings list.

usage:
  python3 tools/es1869set.py            write them
  python3 tools/es1869set.py --check    fail if one of them is stale

linux/es1869/settings.def lists every setting once, as ES1869_SET rows.
From them this writes linux/es1869/Kconfig (the driver's options and a
"Default settings" menu with a CONFIG_SND_ES1869_DEFAULT_<ID> per row),
linux/es1869/oot_config.h (the same defaults for builds outside the kernel
tree, which have no Kconfig), the settings section of docs/LINUX.md, between
its <!-- settings --> markers, and the lists of docs/LINUX_CONTROLS.md,
between its <!-- controls --> markers, by the groups of GROUPS, the pages of
linux/tools/es1869ctl.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRV = os.path.join(ROOT, "linux", "es1869")
DEF = os.path.join(DRV, "settings.def")
KCONFIG = os.path.join(DRV, "Kconfig")
OOT = os.path.join(DRV, "oot_config.h")
DOC = os.path.join(ROOT, "docs", "LINUX.md")
CONTROLS_DOC = os.path.join(ROOT, "docs", "LINUX_CONTROLS.md")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mdfmt  # noqa: E402
from regdoc import split_args, unquote  # noqa: E402

FIELDS = ("id", "ID", "kind", "bank", "reg", "shift", "width", "min", "max",
          "def", "tlv", "flags", "index", "control", "items", "desc")
MARK_BEGIN = "<!-- settings -->"
MARK_END = "<!-- /settings -->"
CTL_BEGIN = "<!-- controls -->"
CTL_END = "<!-- /controls -->"

# the groups of settings.def, as es1869ctl's pages: a title, the row it
# starts at
GROUPS = (("Output", "master_volume"), ("Record", "capture_source"),
          ("DACs", "audio2_oversampling"), ("Mono", "mono_out"),
          ("3-D", "spatial_switch"), ("Buttons", "hwvol_split"),
          ("Power", "gpo0"), ("DMA", "dma_single"), ("Rate", "a2_resample"),
          ("Limits", "a1p_max_rate"), ("Devices", "fmdig_device"),
          ("FM", "opl3"))

# the controls that aren't rows, by the row they follow
EXTRA = {
    "capture_source": "* **ADC Input** (`ADC Input Capture Enum`, F4): the "
                      "same value as Capture Source, as a plain list.\n",
    "hwvol_count3": "* **Hardware Master** (`Hardware Master Playback "
                    "Volume` and `Hardware Master Playback Switch`, F3): "
                    "61h and 63h, the counters the buttons move in split "
                    "mode.\n",
    "a2_treble": "* **Audio 2 Treble Correction Taps** (`Audio 2 Treble "
                 "Correction Taps`): the DAC's correction that a2_treble "
                 "folds into the rate filter, 16 side taps at each of 1x, "
                 "2x and 3x in Q30, as `tools/es1869treble.py` fits them; "
                 "treble.h's until others are loaded, for a DAC measured "
                 "apart. A playing stream takes them within milliseconds, between the "
                 "filter's runs, with no break.\n"
                 "* **Audio 2 Treble Correction Phase Taps** (`Audio 2 "
                 "Treble Correction Phase Taps`): the same correction's "
                 "phase taps, 16 at each of 1x, 2x and 3x in Q30: tap k "
                 "adds that much of the frame k after and takes as much "
                 "of the one k before, which takes back the turn of the "
                 "phase that the DAC's last sample makes, 2 degrees at "
                 "20 kHz at 1x. treble.h's until others are loaded, and "
                 "taken the same way.\n",
    "a1c_filter": "* **Audio 1 Correction Taps** (`Audio 1 Correction "
                  "Capture Taps`): the correction filter's taps, 17 for "
                  "each channel, the left's first, each the centre then "
                  "outward, in units of 1/16384; the identity until a "
                  "calibration (`es1869scope -C`) loads its own. A "
                  "recording under way takes them at its next pass.\n"
                  "* **Audio 1 Correction Phase Taps** (`Audio 1 Correction "
                  "Capture Phase Taps`): the correction's antisymmetric "
                  "taps, 16 for each channel, the left's first, outward "
                  "from the centre, in units of 1/16384: tap m adds that "
                  "much of the sample m frames before the centre and takes "
                  "as much of the one m after, which turns the phase where "
                  "the symmetric taps can only scale. All 0 until a "
                  "calibration loads its own.\n",
    "a1c_linear": "* **Audio 1 Linearity Tables** (`Audio 1 Linearity "
                  "Capture Curve`, `Gain` and `Turn`): the stage's tables, "
                  "the left's first. The curve: its centre, a recorded "
                  "value, then what it adds at 33 knots from -512 to +512 "
                  "about it (0, 2, 4, 6, 8, 12, 16 and on in half octaves "
                  "each way), in 1/256 LSB. The gains, 16384 for 1, and the "
                  "turns of the phase, 16384 for a radian ahead, 40 each: "
                  "4 bands of the instantaneous frequency (under 200, 800 "
                  "and 3200 Hz at 44.1 kHz, and over), each 10 bins of the "
                  "envelope from 4 to 2048 LSB, an octave apart. The "
                  "identity until a measurement loads its own; a recording "
                  "under way takes them at its next pass.\n",
}

SUFFIXES = (" Playback Enum", " Playback Switch", " Playback Route",
            " Playback Volume", " Capture Enum", " Capture Switch",
            " Capture Route", " Capture Volume", " Enum", " Switch",
            " Route", " Volume")


def parse(path=DEF):
    """the rows of settings.def, in order"""
    with open(path, encoding="ascii") as f:
        text = f.read()
    # drop the comments, then take each call whole, over its lines
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    rows = []
    pos = 0
    while True:
        start = text.find("ES1869_SET(", pos)
        if start < 0:
            break
        i, depth, quoted = start + len("ES1869_SET("), 1, False
        while depth:
            c = text[i]
            if quoted:
                if c == "\\":
                    i += 1
                elif c == '"':
                    quoted = False
            elif c == '"':
                quoted = True
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            i += 1
        body = " ".join(text[start + len("ES1869_SET("):i - 1].split())
        args = split_args(body)
        if len(args) != len(FIELDS):
            raise ValueError("ES1869_SET with %d arguments: %s" %
                             (len(args), body[:60]))
        row = dict(zip(FIELDS, args))
        for k in ("control", "items", "desc"):
            row[k] = unquote(row[k])
        for k in ("reg", "shift", "width", "min", "max", "def", "index"):
            row[k] = int(row[k], 0)
        row["flags"] = {f.strip() for f in row["flags"].split("|")}
        if row["ID"] != row["id"].upper():
            raise ValueError("%s: ID %s" % (row["id"], row["ID"]))
        rows.append(row)
        pos = i
    ids = [r["id"] for r in rows]
    if len(set(ids)) != len(ids):
        raise ValueError("a setting twice")
    return rows


def kconfig(rows):
    out = ["""\
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The ES1869 driver. The defaults menu is generated by tools/es1869set.py
# from settings.def: change the rows there and run it again.

config SND_ES1869
\ttristate "ESS ES1869 AudioDrive, every feature"
\tdepends on ISA_DMA_API
\tselect SND_OPL3_LIB
\tselect SND_MPU401_UART
\tselect SND_PCM
\tselect SND_HWDEP
\tselect SND_SEQ_DEVICE if SND_SEQUENCER != n
\thelp
\t  Say Y or M for the ESS ES1869 AudioDrive with every feature of
\t  the chip: both DACs (Audio 2 on a 16-bit DMA channel too), full
\t  duplex, every rate of its clocks, the record mixer, the 3-D
\t  effect, the DAC stages, the hardware volume buttons, the FM
\t  recorded digitally, OPL3 and native ESFM synthesis, and the
\t  MPU-401. Each setting is a module parameter that can be written
\t  at runtime and, where it is chip state, an ALSA control.

\t  It takes the same devices as snd-es18xx, so blacklist that one.

\t  To compile this driver as a module, choose M here: the module
\t  will be called snd-es1869.

config SND_ES1869_ESFM
\ttristate "ES1869 native ESFM General MIDI synthesizer"
\tdepends on SND_ES1869 && SND_SEQUENCER
\tdefault SND_ES1869
\thelp
\t  A sequencer synthesizer on the ES1869's FM in its native ESFM
\t  mode, playing General MIDI as ESS's fixed ESFM.DRV does, with a
\t  bank loaded as firmware (es1869/esfm-gm.bin). The module will be
\t  called snd-es1869-esfm.

menu "ES1869 default settings"
\tdepends on SND_ES1869
"""]
    for r in rows:
        sym = "SND_ES1869_DEFAULT_" + r["ID"]
        out.append("\nconfig %s\n" % sym)
        if r["kind"] == "S_STR":
            out.append('\tstring "%s"\n' % r["id"])
            out.append('\tdefault "%s"\n' % r["items"])
        else:
            out.append('\tint "%s"\n' % r["id"])
            out.append("\trange %d %d\n" % (r["min"], r["max"]))
            out.append("\tdefault %d\n" % r["def"])
        out.append("\thelp\n")
        text = r["desc"][0].upper() + r["desc"][1:] + "."
        if r["control"]:
            text += ' The ALSA control "%s".' % r["control"]
        text += (" The module parameter %s sets it at load and at runtime,"
                 " through /sys/module/snd_es1869/parameters/%s."
                 % (r["id"], r["id"]))
        out += ["\t  %s\n" % line for line in wrap(text, 62)]
    out.append("\nendmenu\n")
    return "".join(out)


def oot_config(rows):
    out = ["""\
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The default settings for builds outside the kernel tree, which have no
 * Kconfig, generated by tools/es1869set.py from settings.def. A kernel
 * built with the driver's Kconfig defines them first.
 */

#ifndef ES1869_OOT_CONFIG_H
#define ES1869_OOT_CONFIG_H
"""]
    for r in rows:
        sym = "CONFIG_SND_ES1869_DEFAULT_" + r["ID"]
        value = ('"%s"' % r["items"] if r["kind"] == "S_STR"
                 else str(r["def"]))
        out.append("\n#ifndef %s\n#define %s %s\n#endif\n" %
                   (sym, sym, value))
    out.append("\n#endif /* ES1869_OOT_CONFIG_H */\n")
    return "".join(out)


def wrap(text, width):
    lines, cur = [], ""
    for word in text.split():
        if cur and len(cur) + 1 + len(word) > width:
            lines.append(cur)
            cur = word
        else:
            cur = word if not cur else cur + " " + word
    if cur:
        lines.append(cur)
    return lines


# what the docs add to a setting's description, for what its flags change
WHEN = (("F_NEXT", " It takes effect at the next prepare."),
        ("F_OPEN", " It takes effect at the next open."),
        ("F_IDLE", " A write fails with EBUSY while Audio 2 is open."),
        ("F_IDLE_A1", " A write fails with EBUSY while Audio 1 is open."),
        ("F_FOUND", " Without a parameter at load, the chip keeps its level."))


def range_text(r):
    """a setting's values and default"""
    if r["kind"] == "S_STR":
        return 'default "%s"' % r["items"]
    if r["kind"] == "S_ENUM":
        items = r["items"].split("|")
        return "%s, default %s" % (", ".join(
            "%d %s" % (i, t) for i, t in enumerate(items)),
            items[r["def"]] if r["def"] < len(items) else r["def"])
    if r["kind"] in ("S_STEREO", "S_MASTER"):
        return "%d to %d a channel, default %d" % (r["min"], r["max"],
                                                   r["def"])
    return "%d to %d, default %d" % (r["min"], r["max"], r["def"])


def sentence(text):
    return text[0].upper() + text[1:]


def doc_section(rows):
    out = [MARK_BEGIN + "\n\n"]
    for r in rows:
        ctl = ' ("%s")' % r["control"] if r["control"] else ""
        when = "".join(text for flag, text in WHEN if flag in r["flags"])
        out.append("* `%s`%s: %s. %s.%s\n" % (r["id"], ctl, range_text(r),
                                              sentence(r["desc"]), when))
    out.append("\n" + MARK_END)
    return "".join(out)


def element(name):
    """the name alsamixer shows for a control, as alsa-lib's base_len()"""
    if name in ("Capture Volume", "Capture Switch"):
        return "Capture"
    for s in SUFFIXES:
        if name.endswith(s) and len(name) > len(s) and \
                name[-len(s) - 1] != "-":
            return name[:-len(s)]
    return name


def view(r):
    """alsamixer's views of a control, by the simple mixer's rules: a list
    is in F3 unless it is a capture enum, a volume without a direction is
    in both, and a switch without one is a playback switch"""
    name = r["control"]
    if name == "Capture Source":
        return "F4, a CAPTURE switch on each source"
    if name.endswith(("Capture Volume", "Capture Switch", " Capture Enum")):
        return "F4"
    if r["kind"] in ("S_BOOL", "S_MASTERSW", "S_ENUM") or \
            name.endswith(" Playback Volume"):
        return "F3"
    return "F3 and F4"


def controls_section(rows):
    """the lists of LINUX_CONTROLS.md: each group's controls, then the
    settings it has with no control"""
    starts = {first: title for title, first in GROUPS}
    groups, title = [], None
    for r in rows:
        if r["id"] in starts:
            title = starts[r["id"]]
            groups.append((title, []))
        groups[-1][1].append(r)
    out = [CTL_BEGIN + "\n\n"]
    for title, members in groups:
        out.append("### %s\n\n" % title)
        ctls = [r for r in members if r["control"]]
        params = [r for r in members if not r["control"]]
        for r in ctls:
            when = "".join(t for f, t in WHEN if f in r["flags"])
            out.append("* **%s** (`%s`, %s): %s. %s. Parameter `%s`.%s\n" %
                       (element(r["control"]), r["control"], view(r),
                        range_text(r), sentence(r["desc"]), r["id"], when))
            out.append(EXTRA.get(r["id"], ""))
        if params:
            out.append("\nParameters only:\n\n")
        for r in params:
            when = "".join(t for f, t in WHEN if f in r["flags"])
            out.append("* `%s`: %s. %s.%s\n" %
                       (r["id"], range_text(r), sentence(r["desc"]), when))
        out.append("\n")
    out.append(CTL_END)
    return "".join(out)


def between(path, begin, end, section):
    """the text of a doc with what its markers hold replaced, or None"""
    with open(path, encoding="ascii", newline="") as f:
        doc = f.read()
    nl = "\r\n" if "\r\n" in doc else "\n"
    a, b = doc.find(begin), doc.find(end)
    if a < 0 or b <= a:
        return None
    text = mdfmt.format_text(section).rstrip("\n").replace("\n", nl)
    return doc[:a] + text + doc[b + len(end):]


def outputs(rows):
    """{path: text} of what this writes; the doc only if it has markers"""
    files = {KCONFIG: kconfig(rows), OOT: oot_config(rows)}
    for path, begin, end, section in (
            (DOC, MARK_BEGIN, MARK_END, doc_section),
            (CONTROLS_DOC, CTL_BEGIN, CTL_END, controls_section)):
        if os.path.exists(path):
            text = between(path, begin, end, section(rows))
            if text is not None:
                files[path] = text
    return files


def main():
    rows = parse()
    files = outputs(rows)
    stale = []
    for path, text in files.items():
        newline = "" if path == DOC else "\n"
        try:
            with open(path, encoding="ascii", newline="") as f:
                old = f.read()
        except FileNotFoundError:
            old = None
        if old == text:
            continue
        if "--check" in sys.argv[1:]:
            stale.append(path)
            continue
        with open(path, "w", encoding="ascii", newline=newline) as f:
            f.write(text)
    for path in stale:
        print("%s is stale: run python3 tools/es1869set.py" % path)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
