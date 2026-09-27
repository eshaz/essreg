#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Generate docs/REGISTERS.md from the register catalog src/esscat.tbl.

usage:
  python3 tools/regdoc.py            write docs/REGISTERS.md
  python3 tools/regdoc.py --check    fail if docs/REGISTERS.md is stale

Each line of esscat.tbl holds one REG, FLD, ENUM or ENUMV macro call or a
one-line comment.  Comments right above a REG line become notes.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TBL = os.path.join(ROOT, "src", "esscat.tbl")
OUT = os.path.join(ROOT, "docs", "REGISTERS.md")

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
          "RF_ALIAS": "Sound Blaster compatible view"}
FFLAGS = {"FF_PERSIST": "profile", "FF_DRVOWNED": "driver sets it",
          "FF_VOLATILE": "cleared by DSP reset"}


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
            reg = {"id": rid, "bank": bank, "ldn": int(ldn, 0),
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


def md_escape(text):
    return text.replace("|", "\\|")


def render(regs, fields, enums):
    out = []
    w = out.append
    w("# ES1869 registers")
    w("")
    w("Generated by `tools/regdoc.py` from `src/esscat.tbl`; do not edit.")
    w("Page numbers refer to the ES1869 data sheet in "
      "`docs/datasheet/es1869techmanual.pdf`.")
    w("")
    w("%d registers, %d fields, %d stored in profiles." %
      (len(regs), len(fields),
       sum("FF_PERSIST" in f["flags"] for f in fields)))
    w("")
    w("**Tiers** decide what essctl lets you change:")
    w("")
    w("| Tier | Meaning |")
    w("|---|---|")
    w("| safe | ordinary settings |")
    w("| caution | can mute, distort or confuse Windows' driver, "
      "but not hang anything |")
    w("| expert | can stop playback, hang the DSP until the next reset or "
      "move the card to other resources; needs Expert mode |")
    w("| read-only | status, never written |")
    w("")
    w("**Flags:** *profile* = saved by File > Save profile and "
      "`essctl /save`; *driver sets it* = ES1869.DRV or ES1869.VXD "
      "rewrites it (a profile loaded at startup puts it back); "
      "*cleared by DSP reset* = lost when the DSP is reset, which "
      "the driver does whenever the device changes hands.")
    w("")
    for bank, (title, desc) in BANKS.items():
        group = [r for r in regs if r["bank"] == bank]
        if not group:
            continue
        w("## %s" % title)
        w("")
        w(desc)
        w("")
        ldn = None
        for r in group:
            if bank == "BK_PNPLDN" and r["ldn"] != ldn:
                ldn = r["ldn"]
                w("### Logical device %d" % ldn)
                w("")
            rf = flag_names(r["flags"], RFLAGS)
            head = "%s%02Xh %s" % (BANK_PREFIX[bank], r["addr"], r["name"])
            w("%s %s" % ("####" if bank == "BK_PNPLDN" else "###", head))
            w("")
            w("DS p.%d%s" % (r["page"], ("; " + ", ".join(rf)) if rf else ""))
            w("")
            for note in r["notes"]:
                w("> %s" % md_escape(note))
                w("")
            w("| Bits | Setting | Key | Kind | Tier | Flags | Description |")
            w("|---|---|---|---|---|---|---|")
            for f in sorted(r["fields"], key=lambda f: -f["shift"]):
                desc = f["help"]
                if f["enum"] != "E_NONE":
                    desc += " Values: " + "; ".join(
                        "%d = %s" % v for v in enums[f["enum"]]) + "."
                w("| %s | %s | `%s` | %s | %s | %s | %s |" % (
                    bits(f), md_escape(f["label"]), f["key"],
                    KINDS[f["kind"]], TIERS[f["tier"]],
                    ", ".join(flag_names(f["flags"], FFLAGS)),
                    md_escape(desc)))
            w("")
    return "\n".join(out)


def main(argv):
    text = render(*parse())
    if "--check" in argv:
        try:
            with open(OUT, encoding="utf-8") as f:
                current = f.read()
        except OSError:
            current = ""
        if current != text:
            print("docs/REGISTERS.md is out of date: "
                  "run python3 tools/regdoc.py")
            return 1
        return 0
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote %s" % os.path.relpath(OUT, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
