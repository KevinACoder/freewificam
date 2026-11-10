#!/usr/bin/env python3
"""Extract the BOARD_TESTS case-name list from a linked board image.

Usage: extract-case-list.py <elf> <objcopy> <readelf> <scratch-dir>

Prints the case names in section order, one per line. The test table's output section
(.board_tests; the input section BOARD_TESTS is merged into it by the linker
script) holds {name_ptr, fn_ptr} pairs - 16 bytes per entry on aarch64 - and
each name pointer points into .rodata, so dumping the two sections is enough to
resolve every name without running the image.

This is one of the three case-name sources reconciled by check-case-list.sh
(image section, suite declaration, judge table); the build fails when they
disagree, so a case can never silently disappear from the judgment channel.
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

SECTION_RE = re.compile(
    r"\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)"
)


def sections(elf: str, readelf: str) -> dict[str, tuple[int, int]]:
    """section name -> (address, size)"""
    out = subprocess.run(
        [readelf, "-SW", elf], check=True, capture_output=True, text=True
    ).stdout
    found = {}
    for line in out.splitlines():
        match = SECTION_RE.search(line)
        if match:
            name, _type, addr, _off, size = match.groups()
            found[name] = (int(addr, 16), int(size, 16))
    return found


def dump_section(elf: str, objcopy: str, name: str, scratch: Path) -> bytes:
    path = scratch / (name.strip(".") + ".bin")
    subprocess.run(
        [objcopy, "--only-section=" + name, "-O", "binary", elf, str(path)],
        check=True,
    )
    return path.read_bytes()


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2
    elf, objcopy, readelf, scratch = sys.argv[1], sys.argv[2], sys.argv[3], Path(sys.argv[4])

    secs = sections(elf, readelf)
    table = ".board_tests" if ".board_tests" in secs else "BOARD_TESTS"
    if table not in secs:
        print("extract-case-list: FAIL: no test-table section in the image", file=sys.stderr)
        return 1
    tests_addr, tests_size = secs[table]
    rodata_addr, rodata_size = secs.get(".rodata", (0, 0))

    with tempfile.TemporaryDirectory(dir=scratch) as tmp:
        tests = dump_section(elf, objcopy, table, Path(tmp))
        rodata = dump_section(elf, objcopy, ".rodata", Path(tmp))

    if tests_size % 16 != 0:
        print(
            "extract-case-list: FAIL: test-table size %d is not a whole number of entries"
            % tests_size,
            file=sys.stderr,
        )
        return 1

    for off in range(0, tests_size, 16):
        name_ptr = int.from_bytes(tests[off : off + 8], "little")
        delta = name_ptr - rodata_addr
        if delta < 0 or delta >= len(rodata):
            print(
                "extract-case-list: FAIL: name pointer 0x%x outside .rodata" % name_ptr,
                file=sys.stderr,
            )
            return 1
        end = rodata.find(b"\0", delta)
        if end < 0:
            print("extract-case-list: FAIL: unterminated name at 0x%x" % name_ptr, file=sys.stderr)
            return 1
        print(rodata[delta:end].decode("ascii"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
