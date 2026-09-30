"""dump-peri — peripheral register dump as a native gdb (py3) command.

freewebcam tools/gdb, spec specs/gdb-peri-dump/SPEC.md (v1).  Load in
aarch64-none-elf-gdb-py3 after connecting:

    (gdb) source tools/gdb/dump_peri.py
    (gdb) dump-peri xhci1                # human-readable table
    (gdb) dump-peri xhci1 --json /tmp/xhci1-rtos.json

Strictly read-only: every memory access goes through gdb 'm' packets issued
below; the tool never writes a target register (spec §3.3.4).

Memory reads are chunked to <=256 bytes because the on-target stub rejects
'm' lengths > 256 with E01 (freewebcam arch/aarch64/gdb/gdb_main.c:230-233);
gdb's own PacketSize (1024) is larger than the stub limit, so `x`-style bulk
reads must not be used against the bare-metal stub.

Register maps live in tools/gdb/regmap/<name>.py (data only).  Adding a
peripheral = adding a map file, this tool is map-agnostic.
"""

import importlib.util
import json
import os
import datetime

import gdb

TOOL_VERSION = "1.0"
SCHEMA = "freewebcam.peri-dump/1"

# Stub hard limit: 'm' length > 256 => E01 (gdb_main.c:224,230-233).
_MAX_CHUNK = 256

_USAGE = """usage: dump-peri <map> [--json PATH] [--anchor STRING] [--probe STRING]
  <map>            register map name, e.g. xhci1 (tools/gdb/regmap/<map>.py)
  --json PATH      also write dump JSON (schema %s)
  --anchor STRING  stop-point description for sample.anchor (default "break *board_log")
  --probe STRING   override sample.probe (default auto: direct-read-ok |
                   degraded-cap-only); use "cru-ungate" after the U-Boot gate
                   clearing path (spec 3.4.3 P2)""" % SCHEMA


def _regmap_dir():
    try:
        here = os.path.dirname(os.path.abspath(__file__))
    except NameError:  # sourced without __file__ (old gdb) - fall back to cwd
        here = os.path.join(os.getcwd(), "tools", "gdb")
    return os.path.join(here, "regmap")


def _load_regmap(name):
    path = os.path.join(_regmap_dir(), "%s.py" % name)
    if not os.path.isfile(path):
        avail = sorted(f[:-3] for f in os.listdir(_regmap_dir())
                       if f.endswith(".py") and f != "__init__.py")
        raise ValueError("unknown map '%s' (available: %s)" % (name, ", ".join(avail)))
    spec = importlib.util.spec_from_file_location("regmap_%s" % name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.validate_map()
    return mod


def _read_bytes(inf, addr, size):
    """Chunked inferior read; every chunk <= _MAX_CHUNK (stub 'm' limit)."""
    out = bytearray()
    while size > 0:
        n = min(size, _MAX_CHUNK)
        out += bytes(inf.read_memory(addr + len(out), n))
        size -= n
    return bytes(out)


def _word(buf, off):
    return int.from_bytes(buf[off:off + 4], "little")


def _value(buf, off, size):
    return int.from_bytes(buf[off:off + size], "little")


def _fmt(value, size):
    return "0x%0*x" % (size * 2, value)


def _decode(entry, value):
    return {name: (value >> lo) & ((1 << (hi - lo + 1)) - 1)
            for (name, lo, hi) in entry["fields"]}


def _fmt_plain(value):
    return "0x%08x" % value


class DumpPeriCommand(gdb.Command):
    """Dump a peripheral's registers (read-only) from the register map.

    dump-peri <map> [--json PATH] [--anchor STRING] [--probe STRING]
    Maps: tools/gdb/regmap/<map>.py.  See specs/gdb-peri-dump/SPEC.md."""

    def __init__(self):
        super(DumpPeriCommand, self).__init__("dump-peri", gdb.COMMAND_DATA,
                                              gdb.COMPLETE_NONE)

    def invoke(self, arg, from_tty):
        try:
            self._run(arg.split())
        except ValueError as exc:
            print("dump-peri: %s\n%s" % (exc, _USAGE))
        except (gdb.MemoryError, gdb.error) as exc:
            print("dump-peri: target read failed: %s" % exc)
            print("  read may have hung/faulted on a gated clock - see spec "
                  "3.4.3 probe protocol (P2: U-Boot 'mw 0xfdd20328 0x10000000' "
                  "clears the ACLK_USB3OTG1 gate bit)")

    def _run(self, argv):
        if not argv or "--help" in argv:
            print(_USAGE)
            return
        name, json_path, anchor, probe = None, None, "break *board_log", None
        i = 0
        while i < len(argv):
            a = argv[i]
            if a == "--json" and i + 1 < len(argv):
                json_path = argv[i + 1]
                i += 2
            elif a == "--anchor" and i + 1 < len(argv):
                anchor = argv[i + 1]
                i += 2
            elif a == "--probe" and i + 1 < len(argv):
                probe = argv[i + 1]
                i += 2
            elif a.startswith("--"):
                raise ValueError("unknown option %s" % a)
            elif name is None:
                name = a
                i += 1
            else:
                raise ValueError("unexpected argument %s" % a)
        if name is None:
            print(_USAGE)
            return

        regmod = _load_regmap(name)
        base = regmod.PERI["base"]
        notes = []

        inf = gdb.selected_inferior()
        cap = _read_bytes(inf, base, 0x40)
        caplength = _word(cap, 0) & 0xFF
        if probe is None:
            probe = "direct-read-ok"

        regs, raw = [], [{"addr": _fmt_plain(base), "len": 0x40,
                          "hex": cap.hex()}]
        regs.extend(self._entries(regmod.CAP_REGS, 0, cap))

        op_base = caplength
        if regmod.CAPLENGTH_MIN <= caplength <= regmod.CAPLENGTH_MAX:
            op = _read_bytes(inf, base + op_base, 0x40)
            raw.append({"addr": _fmt_plain(base + op_base), "len": 0x40,
                        "hex": op.hex()})
            regs.extend(self._entries(regmod.OP_REGS, op_base, op))
            max_ports = (_word(cap, 4) >> 24) & 0xFF
            if max_ports == 0:
                notes.append("HCS_MAX_PORTS=0 - port group skipped (implausible "
                             "with a valid CAPLENGTH)")
            n_ports = min(max_ports, regmod.MAX_PORTS_DUMP)
            if max_ports > regmod.MAX_PORTS_DUMP:
                notes.append("HCS_MAX_PORTS=%d > dump cap %d - dumping first %d"
                             % (max_ports, regmod.MAX_PORTS_DUMP, n_ports))
            if n_ports:
                off = op_base + 0x400
                end = off + n_ports * 0x10
                if end > regmod.MAX_DERIVED_OFFSET:
                    raise ValueError("derived port window 0x%x out of range" % end)
                ports = _read_bytes(inf, base + off, n_ports * 0x10)
                raw.append({"addr": _fmt_plain(base + off),
                            "len": n_ports * 0x10, "hex": ports.hex()})
                for n in range(1, n_ports + 1):
                    regs.extend(self._entries(regmod.PORT_REGS, off + (n - 1) * 0x10,
                                              ports[(n - 1) * 0x10:n * 0x10],
                                              name_suffix=str(n)))
        else:
            probe = "degraded-cap-only"
            notes.append("CAPLENGTH=0x%02x outside [%#x, %#x] - cap block only, "
                         "no op/port decode (spec 3.2 item 3)"
                         % (caplength, regmod.CAPLENGTH_MIN, regmod.CAPLENGTH_MAX))

        sample = {
            "side": "rtos",
            "state": "gdb-stall",
            "anchor": anchor,
            "probe": probe,
            "timestamp": datetime.datetime.now(datetime.timezone.utc)
                              .strftime("%Y-%m-%dT%H:%M:%SZ"),
            "host": "aarch64-none-elf-gdb-py3 %s" % gdb.VERSION,
            "notes": notes,
        }
        doc = {
            "schema": SCHEMA,
            "tool": "dump_peri.py",
            "tool_version": TOOL_VERSION,
            "peri": regmod.PERI["name"],
            "base": _fmt_plain(base),
            "map_version": regmod.MAP_VERSION,
            "sample": sample,
            "regs": regs,
            "raw": raw,
        }

        self._print_table(doc)
        if json_path:
            with open(json_path, "w") as fh:
                json.dump(doc, fh, indent=1)
                fh.write("\n")
            print("# json written: %s" % json_path)

    @staticmethod
    def _entries(entries, region_base, buf, name_suffix=""):
        out = []
        for entry in entries:
            value = _value(buf, entry["offset"], entry["size"])
            out.append({
                "name": entry["name"] + name_suffix,
                "offset": "0x%04x" % (region_base + entry["offset"]),
                "size": entry["size"],
                "access": entry["access"],
                "cls": entry["cls"],
                "value": _fmt(value, entry["size"]),
                "fields": _decode(entry, value),
                "note": entry["note"],
            })
        return out

    @staticmethod
    def _print_table(doc):
        print("# dump-peri %s base=%s map=%s probe=%s"
              % (doc["peri"], doc["base"], doc["map_version"],
                 doc["sample"]["probe"]))
        for note in doc["sample"]["notes"]:
            print("# note: %s" % note)
        for reg in doc["regs"]:
            fields = " ".join("%s=%x" % (k, v)
                              for k, v in reg["fields"].items() if v)
            print("%-12s %-6s %-6s %-2s %s  %s"
                  % (reg["name"], reg["offset"], reg["cls"], reg["access"],
                     reg["value"], fields))


DumpPeriCommand()
