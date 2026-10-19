#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Generate the register reference from the catalog src/esscat.tbl.

usage:
  python3 tools/regdoc.py            write docs/REGISTERS.md and REG_*.md
  python3 tools/regdoc.py --check    fail if one of them is stale

Each line of esscat.tbl holds one REG, FLD, ENUM or ENUMV macro call or a
one-line comment.  Comments right above a REG line become notes.

docs/REGISTERS.md is the index, and the registers are split over the
REG_*.md pages, because Windows 98's Notepad can't open a file of 64 KB.
The pages are formatted by tools/mdfmt.py.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TBL = os.path.join(ROOT, "src", "esscat.tbl")
DOCS = os.path.join(ROOT, "docs")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mdfmt  # noqa: E402

# the pages after the index: file, title, banks
PARTS = [
    ("REG_MIXER.md", "mixer registers", ["BK_MIXER"]),
    ("REG_AUDIO.md", "controller registers and audio ports",
     ["BK_CTRL", "BK_APORT"]),
    ("REG_PNP.md", "configuration ports and PnP registers",
     ["BK_CPORT", "BK_PNPCARD", "BK_PNPLDN"]),
]

BANKS = {
    "BK_MIXER": ("Mixer registers",
                 "Index written to Audio_Base+4h, data at Audio_Base+5h."),
    "BK_CTRL": ("Controller registers",
                "Written with DSP command Axh/Bxh plus the value, read with "
                "C0h and the register after C6h (extended mode). They go "
                "through the DSP command channel, which Windows' driver uses "
                "for playback, so essctl reads them only on request."),
    "BK_APORT": ("Audio I/O ports", "Ports Audio_Base+0h to +Fh."),
    "BK_CPORT": ("Configuration device ports",
                 "Ports Config_Base+0h to +7h (Config_Base comes from the "
                 "mixer 40h identification sequence or PnP LDN 0)."),
    "BK_PNPCARD": ("PnP card registers",
                   "Index written to Config_Base+0h, data at Config_Base+1h."),
    "BK_PNPLDN": ("PnP logical device registers",
                  "As the card registers, after selecting the logical device "
                  "in register 07h; essctl restores the previous index and "
                  "device afterwards."),
}
BANK_PREFIX = {"BK_MIXER": "", "BK_CTRL": "", "BK_APORT": "Audio_Base+",
               "BK_CPORT": "Config_Base+", "BK_PNPCARD": "",
               "BK_PNPLDN": ""}
TIERS = {"T_RO": "read-only", "T_SAFE": "safe", "T_CAUTION": "caution",
         "T_EXPERT": "expert"}
KINDS = {"K_BOOL": "bit", "K_UINT": "level", "K_ENUM": "choice",
         "K_RO": "status", "K_ACTION": "action", "K_PULSE": "pulse",
         "K_SMAG": "signed", "K_HEX": "value"}
RFLAGS = {"RF_READ_SIDEFX": "reading changes state",
          "RF_WRITEONLY": "write-only",
          "RF_NEEDS_IDLE": "DSP channel",
          "RF_ALIAS": "Sound Blaster compatible view",
          "RF_FM_PORT": "FM port"}
# the optional logical devices of esscat.h, numbered from card register 25h
LDN_NAMES = {
    "LDN_MPU": "MPU-401 device (optional, LDN 3)",
    "LDN_CDROM": "CD-ROM device (optional, LDN 3 or 4)",
    "LDN_MODEM": "Modem device (optional, LDN 3, 4 or 5)",
    "LDN_GP": "General-purpose device (optional, LDN 3 to 6)"}
FFLAGS = {"FF_PERSIST": "profile", "FF_DRVOWNED": "driver sets it",
          "FF_VOLATILE": "reset by DSP reset"}


def split_args(text):
    """Arguments of a macro call, with the strings still quoted."""
    args, cur, quoted, i = [], "", False, 0
    while i < len(text):
        c = text[i]
        if quoted:
            cur += c
            if c == "\\":
                cur += text[i + 1]
                i += 1
            elif c == '"':
                quoted = False
        elif c == '"':
            quoted = True
            cur += c
        elif c == ",":
            args.append(cur.strip())
            cur = ""
        else:
            cur += c
        i += 1
    args.append(cur.strip())
    return args


def unquote(s):
    return s[1:-1].replace('\\"', '"') if s.startswith('"') else s


def parse(path=TBL):
    regs, fields, enums, notes = [], [], {}, []
    by_id = {}
    for line in open(path, encoding="ascii"):
        line = line.strip()
        if not line:
            notes = []
            continue
        if line.startswith("/*"):
            text = line[2:-2].strip()
            if not text.startswith("="):
                notes.append(text)
            continue
        m = re.match(r"(REG|FLD|ENUM|ENUMV)\((.*)\)$", line)
        if not m:
            raise ValueError("unexpected line: " + line)
        kind, args = m.group(1), split_args(m.group(2))
        if kind == "REG":
            rid, bank, ldn, addr, rflags, name, page = args
            reg = {"id": rid, "bank": bank, "ldn": LDN_NAMES.get(ldn, ldn),
                   "addr": int(addr, 0), "flags": rflags,
                   "name": unquote(name), "page": int(page), "notes": notes,
                   "fields": []}
            regs.append(reg)
            by_id[rid] = reg
        elif kind == "FLD":
            (fid, rid, shift, width, fkind, tier, fflags, enum, page, fmt,
             key, label, help_) = args
            fld = {"id": fid, "shift": int(shift), "width": int(width),
                   "kind": fkind, "tier": tier, "flags": fflags,
                   "enum": enum, "page": page, "fmt": fmt,
                   "key": unquote(key), "label": unquote(label),
                   "help": unquote(help_)}
            by_id[rid]["fields"].append(fld)
            fields.append(fld)
        elif kind == "ENUM":
            enums[args[0]] = []
        else:
            enums[args[0]].append((int(args[1], 0), unquote(args[2])))
        notes = []
    return regs, fields, enums


def flag_names(flags, table):
    return [table[f.strip()] for f in flags.split("|")
            if f.strip() in table]


def bits(f):
    hi = f["shift"] + f["width"] - 1
    return str(f["shift"]) if hi == f["shift"] else "%d:%d" % (hi, f["shift"])


def field_item(f, enums):
    """one setting as a list item: bits and name, then key, kind, tier and
    flags, then the description"""
    desc = f["help"]
    if not desc.endswith("."):
        desc += "."
    if f["enum"] != "E_NONE":
        desc += " Values: " + "; ".join(
            "%d = %s" % v for v in enums[f["enum"]]) + "."
    attrs = ["`%s`" % f["key"], KINDS[f["kind"]], TIERS[f["tier"]]]
    attrs += flag_names(f["flags"], FFLAGS)
    return "* **%s %s** (%s). %s" % (bits(f), f["label"], ", ".join(attrs),
                                     desc)


def render_bank(w, bank, regs, enums):
    title, desc = BANKS[bank]
    group = [r for r in regs if r["bank"] == bank]
    w("## %s" % title)
    w("")
    w(desc)
    w("")
    ldn = None
    for r in group:
        if bank == "BK_PNPLDN" and r["ldn"] != ldn:
            ldn = r["ldn"]
            w("### " + (ldn if not ldn.isdigit()
                        else "Logical device %s" % ldn))
            w("")
        rf = flag_names(r["flags"], RFLAGS)
        head = "%s%02Xh %s" % (BANK_PREFIX[bank], r["addr"], r["name"])
        w("%s %s" % ("####" if bank == "BK_PNPLDN" else "###", head))
        w("")
        page = ("DS p.%d" % r["page"] if r["page"]
                else "Not in the data sheet")
        w("%s%s." % (page, (", " + ", ".join(rf)) if rf else ""))
        w("")
        for note in r["notes"]:
            w("> %s" % note)
            w("")
        for f in sorted(r["fields"], key=lambda f: -f["shift"]):
            w(field_item(f, enums))
        w("")


def render(regs, fields, enums):
    """{file name: markdown} of the index and the pages"""
    pages = {}
    out = []
    w = out.append
    w("# ES1869 registers")
    w("")
    w("This reference is generated by `tools/regdoc.py` from the catalog "
      "`src/esscat.tbl`, so edit the catalog rather than these pages. Page "
      "numbers refer to the ES1869 data sheet, "
      "`docs/datasheet/es1869techmanual.pdf`.")
    w("")
    w("The catalog has %d registers with %d settings, and profiles store %d "
      "of the settings. The registers are split over three pages, because "
      "Windows 98's Notepad can't open a file of 64 KB:" %
      (len(regs), len(fields),
       sum("FF_PERSIST" in f["flags"] for f in fields)))
    for name, title, banks in PARTS:
        w("* [The %s](%s)" % (title, name))
    w("")
    w("Each register lists its data sheet page, and then its settings. A "
      "setting starts with its bits and its name, followed by its key in "
      "essctl's profiles, its kind, its tier and its flags, and then its "
      "description.")
    w("")
    w("The tier decides what essctl lets you change:")
    w("* *safe* settings are ordinary ones.")
    w("* *caution* settings can mute, distort or confuse Windows' driver, "
      "but can't hang anything.")
    w("* *expert* settings can stop playback, hang the DSP until the next "
      "reset, or move the card to other resources, so essctl changes them "
      "only in Expert mode.")
    w("* *read-only* settings are status, and are never written.")
    w("")
    w("The flags say more about a setting:")
    w("* *profile*: File > *Save profile* and `essctl /save` store it.")
    w("* *driver sets it*: ES1869.DRV or ES1869.VXD rewrites it, and a "
      "profile loaded at startup puts it back.")
    w("* *reset by DSP reset*: a DSP reset sets it back, and the driver "
      "resets the DSP whenever the device changes hands.")
    pages["REGISTERS.md"] = out
    for name, title, banks in PARTS:
        out = []
        w = out.append
        w("# ES1869 %s" % title)
        w("")
        w("This page is part of the [ES1869 register reference]"
          "(REGISTERS.md), generated from `src/esscat.tbl`.")
        w("")
        for bank in banks:
            render_bank(w, bank, regs, enums)
        pages[name] = out
    return {name: mdfmt.format_text("\n".join(lines), name)
            for name, lines in pages.items()}


def main(argv):
    pages = render(*parse())
    stale = []
    for name, text in pages.items():
        path = os.path.join(DOCS, name)
        data = text.replace("\n", "\r\n").encode("ascii")
        try:
            with open(path, "rb") as f:
                current = f.read()
        except OSError:
            current = b""
        if current == data:
            continue
        stale.append(name)
        if "--check" not in argv:
            with open(path, "wb") as f:
                f.write(data)
            print("wrote docs/%s" % name)
    if "--check" in argv and stale:
        print("out of date: %s, run python3 tools/regdoc.py" %
              ", ".join("docs/" + n for n in stale))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
