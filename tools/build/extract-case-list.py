#!/usr/bin/env python3
"""Extract the BOARD_TESTS case-name list from a linked board image.

Usage: extract-case-list.py <elf> <objcopy> <readelf> <scratch-dir>

Prints the case names in section order, one per line. The test table's output section
(.board_tests; the input section BOARD_TESTS is merged into it by the linker
script) holds {name_ptr, fn_ptr} pairs - two pointers per entry, 16 bytes on a
64-bit image and 8 on a 32-bit one - and each name pointer points into .rodata,
so dumping the two sections is enough to resolve every name without running the
image.

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


def elf_class(elf: str, readelf: str) -> int:
    """The image's pointer size in bytes (4 for ELF32, 8 for ELF64)."""
    out = subprocess.run(
        [readelf, "-h", elf], check=True, capture_output=True, text=True
    ).stdout
    for line in out.splitlines():
        if "Class:" in line:
            if "ELF64" in line:
                return 8
            if "ELF32" in line:
                return 4
    raise SystemExit("extract-case-list: FAIL: cannot read the ELF class")


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
    ptr = elf_class(elf, readelf)
    entry = 2 * ptr
    table = ".board_tests" if ".board_tests" in secs else "BOARD_TESTS"
    if table not in secs:
        print("extract-case-list: FAIL: no test-table section in the image", file=sys.stderr)
        return 1
    tests_addr, tests_size = secs[table]

    # The name strings live in whichever read-only output section the image's
    # linker script created for them: a 64-bit script keeps .rodata, the small
    # 32-bit one folds *(.rodata*) into .text.  Resolve each pointer against
    # the section that contains it instead of assuming one name.
    candidates = [name for name in (".rodata", ".text", ".data") if name in secs]

    with tempfile.TemporaryDirectory(dir=scratch) as tmp:
        tests = dump_section(elf, objcopy, table, Path(tmp))
        blobs = {name: dump_section(elf, objcopy, name, Path(tmp)) for name in candidates}

    if tests_size % entry != 0:
        print(
            "extract-case-list: FAIL: test-table size %d is not a whole number of "
            "%d-byte entries" % (tests_size, entry),
            file=sys.stderr,
        )
        return 1

    for off in range(0, tests_size, entry):
        name_ptr = int.from_bytes(tests[off : off + ptr], "little")
        text = None
        for name in candidates:
            addr, size = secs[name]
            if addr <= name_ptr < addr + size:
                blob = blobs[name]
                delta = name_ptr - addr
                if delta >= len(blob):
                    break
                end = blob.find(b"\0", delta)
                if end < 0:
                    break
                text = blob[delta:end].decode("ascii", errors="replace")
                break
        if text is None:
            print(
                "extract-case-list: FAIL: name pointer 0x%x is not in a read-only "
                "section (%s)" % (name_ptr, " ".join(candidates)),
                file=sys.stderr,
            )
            return 1
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
