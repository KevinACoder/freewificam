#!/usr/bin/env python3
"""peri_compare — parse gdb 'x' register-window dumps and compare peri-dump JSONs.

freewebcam tools/gdb, spec specs/gdb-peri-dump/SPEC.md (v1).  stdlib only; runs
on the host (python3), never on target.

  peri_compare.py parse-xw FILE --base 0xVA --peri xhci1 --json OUT.json
      Parse a gdb `x/Nxw <VA>` dump of the peri MMIO window (Linux kgdb-stall
      side) into the freewebcam.peri-dump/1 schema, using the register map for
      named registers.  --base is the ioremap VA the dump starts at; register
      offsets in the JSON are relative to the peri base (VA_base maps to it).
  peri_compare.py compare A.json B.json [--json OUT]
      Per-register SAME / DIFF / NA verdicts.  DIFF notes carry a=/b= values
      plus field-level deltas; status-class DIFFs get the [LIVE] tag (drift is
      expected - spec 1.2).  Never force equality: every DIFF carries a note.
      Exit 0 = full coverage; 1 = NA present; 2 = usage/parse/schema error.
  peri_compare.py --selftest
      Canned-fixture checks of parse-xw + compare branches (no board needed).
"""

import argparse
import datetime
import importlib.util
import io
import json
import os
import re
import sys
import contextlib

SCHEMA = "freewebcam.peri-dump/1"

_REGMAP_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "regmap")
_XW_LINE = re.compile(r"^0x([0-9a-fA-F]+):\s+(.*)$")
_WORD = re.compile(r"0x([0-9a-fA-F]+)")


def load_regmap(name):
    path = os.path.join(_REGMAP_DIR, "%s.py" % name)
    if not os.path.isfile(path):
        raise ValueError("unknown map '%s' (%s)" % (name, _REGMAP_DIR))
    spec = importlib.util.spec_from_file_location("regmap_%s" % name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.validate_map()
    return mod


def _now():
    return datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ")


def _fmt(value, size):
    return "0x%0*x" % (size * 2, value)


def _decode(entry, value):
    return {name: (value >> lo) & ((1 << (hi - lo + 1)) - 1)
            for (name, lo, hi) in entry["fields"]}


def _required_len(caplength, max_ports, dump_cap):
    """Bytes of the MMIO window the map needs (spec 3.5.3 coverage rule)."""
    ports = min(max_ports, dump_cap) if max_ports else 0
    return max(caplength + 0x40, caplength + 0x400 + ports * 0x10)


def _regs_from_words(regmod, words):
    """Slice named registers out of a little-endian 32-bit word window."""
    def at(off, size):
        raw = b"".join(w.to_bytes(4, "little") for w in words[off // 4:])
        return int.from_bytes(raw[:size], "little")

    out = []
    for entry in regmod.CAP_REGS:
        v = at(entry["offset"], entry["size"])
        out.append((entry, entry["offset"], v))
    caplength = words[0] & 0xFF
    for entry in regmod.OP_REGS:
        off = caplength + entry["offset"]
        out.append((entry, off, at(off, entry["size"])))
    max_ports = (words[1] >> 24) & 0xFF
    n_ports = min(max_ports, regmod.MAX_PORTS_DUMP) if max_ports else 0
    for n in range(1, n_ports + 1):
        entry = regmod.PORT_REGS[0]
        off = caplength + 0x400 + (n - 1) * 0x10
        out.append((entry, off, at(off, entry["size"]), str(n)))
    return out, caplength, max_ports


def cmd_parse_xw(argv):
    ap = argparse.ArgumentParser(prog="peri_compare.py parse-xw")
    ap.add_argument("file")
    ap.add_argument("--base", required=True, help="VA the x dump starts at")
    ap.add_argument("--peri", default="xhci1")
    ap.add_argument("--json", dest="json_path")
    args = ap.parse_args(argv)

    words, start_va = [], None
    with open(args.file) as fh:
        for line in fh:
            m = _XW_LINE.match(line.strip())
            if not m:
                continue
            va = int(m.group(1), 16)
            if start_va is None:
                start_va = va
            words.extend(int(t, 16) for t in _WORD.findall(m.group(2)))
    if start_va is None or not words:
        print("parse-xw: no 'x' dump lines found in %s" % args.file)
        return 2

    regmod = load_regmap(args.peri)
    caplength = words[0] & 0xFF
    max_ports = (words[1] >> 24) & 0xFF
    required = _required_len(caplength, max_ports, regmod.MAX_PORTS_DUMP)
    if len(words) * 4 < required:
        print("parse-xw: window short - have %d words (%d B), map needs %d B "
              "(caplength=0x%02x maxports=%d); enlarge the x dump (spec 3.5.3)"
              % (len(words), len(words) * 4, required, caplength, max_ports))
        return 2

    base_va = int(args.base, 16)
    if start_va != base_va:
        print("parse-xw: dump starts at 0x%x, --base says 0x%x" % (start_va, base_va))
        return 2

    entries, caplength, max_ports = _regs_from_words(regmod, words)
    regs = []
    for item in entries:
        entry, off, value = item[0], item[1], item[2]
        suffix = item[3] if len(item) > 3 else ""
        regs.append({
            "name": entry["name"] + suffix,
            "offset": "0x%04x" % off,
            "size": entry["size"],
            "access": entry["access"],
            "cls": entry["cls"],
            "value": _fmt(value, entry["size"]),
            "fields": _decode(entry, value),
            "note": entry["note"],
        })

    doc = {
        "schema": SCHEMA,
        "tool": "peri_compare.py parse-xw",
        "tool_version": "1.0",
        "peri": regmod.PERI["name"],
        "base": _fmt(regmod.PERI["base"], 4),
        "map_version": regmod.MAP_VERSION,
        "sample": {
            "side": "linux",
            "state": "kgdb-stall",
            "anchor": "sysrq-g",
            "probe": "n/a",
            "timestamp": _now(),
            "host": "peri_compare.py parse-xw",
            "notes": ["va=%s" % _fmt(base_va, 16),
                      "words=%d (0x%x B)" % (len(words), len(words) * 4)],
        },
        "regs": regs,
        "raw": [{"addr": _fmt(base_va, 16), "len": len(words) * 4,
                 "hex": b"".join(w.to_bytes(4, "little") for w in words).hex()}],
    }
    if args.json_path:
        with open(args.json_path, "w") as fh:
            json.dump(doc, fh, indent=1)
            fh.write("\n")
        print("# json written: %s" % args.json_path)
    print("# parse-xw: %d regs from %d words (caplength=0x%02x maxports=%d)"
          % (len(regs), len(words), caplength, max_ports))
    return 0


def cmd_compare(argv):
    ap = argparse.ArgumentParser(prog="peri_compare.py compare")
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--json", dest="json_path")
    args = ap.parse_args(argv)

    with open(args.a) as fh:
        a = json.load(fh)
    with open(args.b) as fh:
        b = json.load(fh)
    for doc, path in ((a, args.a), (b, args.b)):
        if doc.get("schema") != SCHEMA:
            print("compare: %s is not %s (schema=%r)" % (path, SCHEMA,
                                                         doc.get("schema")))
            return 2
    if a["map_version"] != b["map_version"]:
        print("compare: map_version mismatch (%s vs %s) - offsets "
              "incomparable" % (a["map_version"], b["map_version"]))
        return 2

    ra = {r["name"]: r for r in a["regs"]}
    rb = {r["name"]: r for r in b["regs"]}
    names = [r["name"] for r in a["regs"]] + \
            [n for n in (r["name"] for r in b["regs"]) if n not in ra]

    print("# peri-compare a=%s b=%s" % (args.a, args.b))
    print("# a: %s/%s probe=%s  b: %s/%s probe=%s"
          % (a["sample"]["side"], a["sample"]["state"],
             a["sample"]["probe"], b["sample"]["side"],
             b["sample"]["state"], b["sample"]["probe"]))
    out_rows, n_same, n_diff, n_na = [], 0, 0, 0
    for name in names:
        x, y = ra.get(name), rb.get(name)
        if x is None or y is None:
            n_na += 1
            missing = "a" if x is None else "b"
            row = [name, x["offset"] if x else y["offset"],
                   (x or y)["cls"], "NA",
                   "missing in %s (coverage gap)" % missing]
        elif x["value"] == y["value"]:
            n_same += 1
            row = [name, x["offset"], x["cls"], "SAME",
                   "a=%s b=%s" % (x["value"], y["value"])]
        else:
            n_diff += 1
            field_diffs = []
            for k in x["fields"]:
                if k in y["fields"] and x["fields"][k] != y["fields"][k]:
                    field_diffs.append("%s=%x->%x" % (k, x["fields"][k],
                                                      y["fields"][k]))
            note = "a=%s b=%s %s" % (x["value"], y["value"],
                                     "; ".join(field_diffs))
            if x["cls"] == "status":
                row = [name, x["offset"], x["cls"], "DIFF", "[LIVE] " + note]
            else:
                row = [name, x["offset"], x["cls"], "DIFF", note]
        out_rows.append(row)
        print("%-12s %-6s %-6s %-5s %s" % tuple(row))
    print("# summary: regs=%d same=%d diff=%d na=%d"
          % (len(out_rows), n_same, n_diff, n_na))

    if args.json_path:
        with open(args.json_path, "w") as fh:
            json.dump({"schema": SCHEMA, "tool": "peri_compare.py compare",
                       "a": args.a, "b": args.b, "rows": [
                           {"name": r[0], "offset": r[1], "cls": r[2],
                            "verdict": r[3], "note": r[4]} for r in out_rows],
                       "summary": {"regs": len(out_rows), "same": n_same,
                                   "diff": n_diff, "na": n_na}}, fh, indent=1)
            fh.write("\n")
    return 1 if n_na else 0


# ---------------------------------------------------------------- selftest

def _mini_doc(regs, side="rtos", state="gdb-stall", probe="direct-read-ok"):
    return {"schema": SCHEMA, "tool": "fixture", "tool_version": "1.0",
            "peri": "xhci1", "base": "0xfd000000", "map_version": "xhci1-v1",
            "sample": {"side": side, "state": state, "anchor": "x",
                       "probe": probe, "timestamp": _now(), "host": "fixture",
                       "notes": []},
            "regs": regs, "raw": []}


def _reg(name, offset, cls, value, size=4):
    return {"name": name, "offset": offset, "size": size,
            "access": "RO" if cls != "x" else "RW", "cls": cls, "value": value,
            "fields": {}, "note": "fixture"}


def _run_capture(fn, argv):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        code = fn(argv)
    return code, buf.getvalue()


def cmd_selftest(argv):
    checks = []

    # compare: all-SAME -> exit 0
    a = _mini_doc([_reg("CAPLENGTH", "0x0000", "config", "0x00000040"),
                   _reg("USBCMD", "0x0020", "config", "0x00000c05"),
                   _reg("USBSTS", "0x0024", "status", "0x00000001")])

    import tempfile
    tmp = tempfile.mkdtemp(prefix="peri-compare-selftest-")
    pa = os.path.join(tmp, "a.json")
    pb = os.path.join(tmp, "b.json")
    with open(pa, "w") as fh:
        json.dump(a, fh)
    with open(pb, "w") as fh:
        json.dump(a, fh)  # identical -> all SAME

    code, out = _run_capture(cmd_compare, [pa, pb])
    checks.append(("compare identical -> exit 0",
                   code == 0 and "same=3" in out))

    # compare: DIFF config + DIFF status([LIVE]) + NA -> exit 1
    b = _mini_doc([_reg("CAPLENGTH", "0x0000", "config", "0x00000040"),
                   _reg("USBCMD", "0x0020", "config", "0x00000000"),
                   _reg("USBSTS", "0x0024", "status", "0x00000005"),
                   _reg("PORTSC1", "0x0440", "status", "0x000002e7")],
                  side="linux", state="kgdb-stall", probe="n/a")
    pb2 = os.path.join(tmp, "b2.json")
    with open(pb2, "w") as fh:
        json.dump(b, fh)
    code, out = _run_capture(cmd_compare, [pa, pb2])
    checks.append(("compare mixed -> exit 1 with NA", code == 1 and "na=1" in out))
    checks.append(("status DIFF tagged [LIVE]", "[LIVE]" in out and
                   "DIFF" in out))
    checks.append(("DIFF note carries a=/b= values", "a=0x00000c05 b=0x00000000" in out))

    # schema guard -> exit 2
    bad = dict(a)
    bad["schema"] = "other/9"
    pbad = os.path.join(tmp, "bad.json")
    with open(pbad, "w") as fh:
        json.dump(bad, fh)
    code, _ = _run_capture(cmd_compare, [pbad, pb2])
    checks.append(("schema mismatch -> exit 2", code == 2))

    # parse-xw: synthesized 320-word window (caplength=0x20, maxports=2)
    regmod = load_regmap("xhci1")
    words = [0] * 320
    words[0] = 0x01100020          # CAPLENGTH=0x20, HCIVERSION=0x0110
    words[1] = (2 << 24) | 0x40    # HCS_MAX_PORTS=2, slots=64
    words[0x20 // 4] = 0x00000c05  # USBCMD (op_base=0x20)
    words[(0x20 + 0x400) // 4] = 0x000002e7  # PORTSC1
    words[(0x20 + 0x410) // 4] = 0x000002a3  # PORTSC2
    lines = []
    for i in range(0, 320, 4):
        lines.append("0x%016x:\t%s" %
                     (0xFFFF800012340000 + 16 * i,
                      "\t".join("0x%08x" % w for w in words[i:i + 4])))
    px = os.path.join(tmp, "xw.txt")
    with open(px, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    code, out = _run_capture(cmd_parse_xw, [px, "--base", "0xffff800012340000"])
    checks.append(("parse-xw 320w -> exit 0", code == 0))
    pout = os.path.join(tmp, "parsed.json")
    code, out = _run_capture(cmd_parse_xw,
                             [px, "--base", "0xffff800012340000",
                              "--json", pout])
    with open(pout) as fh:
        parsed = json.load(fh)
    checks.append(("parse-xw json schema/regs",
                   parsed["schema"] == SCHEMA and
                   parsed["sample"]["state"] == "kgdb-stall" and
                   {r["name"] for r in parsed["regs"]} >= {
                       "CAPLENGTH", "USBCMD", "USBSTS", "PORTSC1", "PORTSC2"}))
    usbcmd = next(r for r in parsed["regs"] if r["name"] == "USBCMD")
    checks.append(("parse-xw USBCMD sliced at op_base",
                   usbcmd["offset"] == "0x0020" and
                   usbcmd["value"] == "0x00000c05"))
    portsc2 = next(r for r in parsed["regs"] if r["name"] == "PORTSC2")
    checks.append(("parse-xw PORTSC2 offset 0x0430",
                   portsc2["offset"] == "0x0430" and
                   portsc2["value"] == "0x000002a3"))

    # parse-xw: short window -> exit 2
    ps = os.path.join(tmp, "short.txt")
    with open(ps, "w") as fh:
        fh.write("0xffff800012340000:\t0x01100020\t0x02000040\n")
    code, out = _run_capture(cmd_parse_xw, [ps, "--base", "0xffff800012340000"])
    checks.append(("parse-xw short window -> exit 2", code == 2 and
                   "window short" in out))

    ok = True
    for name, passed in checks:
        print("%s %s" % ("PASS" if passed else "FAIL", name))
        ok = ok and passed
    print("# selftest: %d checks, %s" % (len(checks), "PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main(argv):
    if argv and argv[0] == "--selftest":
        return cmd_selftest(argv[1:])
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "parse-xw":
        return cmd_parse_xw(argv[1:])
    if argv[0] == "compare":
        return cmd_compare(argv[1:])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
