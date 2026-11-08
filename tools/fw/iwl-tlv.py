#!/usr/bin/env python3
"""Decode an Intel iwlwifi firmware container (read-only).

A modern iwlwifi image is a TLV container, not a code blob with a header:

  * the file starts with ``struct iwl_tlv_ucode_header`` (88 bytes) -
    a zero word, the magic ``0x0a4c5749``, a 64-byte human-readable
    version string, a version word, a build word and an 8-byte reserved
    field;
  * the rest is a sequence of ``struct iwl_ucode_tlv`` records - a type,
    a length, and that many bytes of payload, padded to a 4-byte boundary.

The driver reads two of those records as capability bit vectors
(``IWL_UCODE_TLV_API_CHANGES_SET`` and
``IWL_UCODE_TLV_ENABLED_CAPABILITIES``) and gates whole code paths on
single bits of them, and it reads ``IWL_UCODE_TLV_CMD_VERSIONS`` to decide
which layout of a host command body it may send.  Decoding the container
here answers "which bits and which command versions does this exact file
carry" without a board.

The type names below are the enumerators of ``enum iwl_ucode_tlv_type`` in
``wireless-drivers/iwlwifi/fw/file.h``.  Bit numbers are printed as
numbers: the names live in ``enum iwl_ucode_tlv_api`` and
``enum iwl_ucode_tlv_capa`` in the same header, and the two vectors index
different enumerations.

    tools/fw/iwl-tlv.py firmware/iwlwifi/iwlwifi-7260-17.ucode
    tools/fw/iwl-tlv.py --json <image>
    tools/fw/iwl-tlv.py --selftest
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

MAGIC = 0x0A4C5749
HDR_FMT = "<II64sIIQ"
HDR_SIZE = struct.calcsize(HDR_FMT)
TLV_FMT = "<II"
TLV_SIZE = struct.calcsize(TLV_FMT)

# enum iwl_ucode_tlv_type, fw/file.h.  Type 7 is PAN on the DVM op mode and
# MEM_DESC on everything else; this decoder prints the non-DVM meaning.
TLV_NAMES = {
    0: "INVALID", 1: "INST", 2: "DATA", 3: "INIT", 4: "INIT_DATA",
    5: "BOOT", 6: "PROBE_MAX_LEN", 7: "MEM_DESC", 8: "RUNT_EVTLOG_PTR",
    9: "RUNT_EVTLOG_SIZE", 10: "RUNT_ERRLOG_PTR", 11: "INIT_EVTLOG_PTR",
    12: "INIT_EVTLOG_SIZE", 13: "INIT_ERRLOG_PTR", 14: "ENHANCE_SENS_TBL",
    15: "PHY_CALIBRATION_SIZE", 16: "WOWLAN_INST", 17: "WOWLAN_DATA",
    18: "FLAGS", 19: "SEC_RT", 20: "SEC_INIT", 21: "SEC_WOWLAN",
    22: "DEF_CALIB", 23: "PHY_SKU", 24: "SECURE_SEC_RT",
    25: "SECURE_SEC_INIT", 26: "SECURE_SEC_WOWLAN", 27: "NUM_OF_CPU",
    28: "CSCHEME", 29: "API_CHANGES_SET", 30: "ENABLED_CAPABILITIES",
    31: "N_SCAN_CHANNELS", 32: "PAGING", 34: "SEC_RT_USNIFFER",
    36: "FW_VERSION", 38: "FW_DBG_DEST", 39: "FW_DBG_CONF",
    40: "FW_DBG_TRIGGER", 48: "CMD_VERSIONS", 50: "FW_GSCAN_CAPA",
    51: "FW_MEM_SEG", 52: "IML", 54: "UMAC_DEBUG_ADDRS",
    55: "LMAC_DEBUG_ADDRS", 57: "FW_RECOVERY_INFO", 58: "HW_TYPE",
    60: "FW_FSEQ_VERSION", 61: "PHY_INTEGRATION_VERSION",
    62: "PNVM_VERSION", 64: "PNVM_SKU", 66: "SEC_TABLE_ADDR",
    67: "D3_KEK_KCK_ADDR", 68: "CURRENT_PC", 72: "FSEQ_BIN_VERSION",
    73: "CMD_BIOS_TABLE", 74: "PNVM_DATA",
}

TLV_API_CHANGES = 29
TLV_ENABLED_CAPABILITIES = 30
TLV_CMD_VERSIONS = 48
TLV_FLAGS = 18
TLV_FW_VERSION = 36

# struct iwl_fw_cmd_version, fw/file.h: cmd, group, cmd_ver, notif_ver.
CMD_VER_FMT = "<BBBB"
CMD_VER_SIZE = struct.calcsize(CMD_VER_FMT)
CMD_VER_UNKNOWN = 99

# The types whose payload this decoder reads as one or two little-endian words.
U32_TYPES = {
    6: "probe_max_len",
    18: "flags",
    23: "phy_sku",
    27: "num_of_cpu",
    31: "n_scan_channels",
}


def _bits(word: int, base: int) -> list[int]:
    return [base + i for i in range(32) if (word >> i) & 1]


def parse(data: bytes) -> dict:
    if len(data) < HDR_SIZE:
        raise ValueError(f"shorter than the TLV header ({len(data)} < {HDR_SIZE})")
    zero, magic, human, ver, build, _reserved = struct.unpack_from(HDR_FMT, data, 0)
    if magic != MAGIC:
        raise ValueError(f"not a TLV container: magic 0x{magic:08x}")

    out = {
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
        "header": {
            "zero": zero,
            "magic": f"0x{magic:08x}",
            "human_readable": human.rstrip(b"\0").decode("ascii", "replace"),
            "version": f"0x{ver:08x}",
            "version_number": ver,
            "build": build,
        },
        "tlvs": [],
        "api_bits": [],
        "capa_bits": [],
        "cmd_versions": [],
        "truncated_at": None,
    }

    off = HDR_SIZE
    while off + TLV_SIZE <= len(data):
        tlv_type, tlv_len = struct.unpack_from(TLV_FMT, data, off)
        body = off + TLV_SIZE
        if body + tlv_len > len(data):
            out["truncated_at"] = off
            break
        payload = data[body:body + tlv_len]
        entry = {
            "offset": off,
            "type": tlv_type,
            "name": TLV_NAMES.get(tlv_type, f"UNKNOWN({tlv_type})"),
            "length": tlv_len,
            "payload": "",
        }

        if tlv_type == TLV_API_CHANGES and tlv_len == 8:
            index, word = struct.unpack("<II", payload)
            bits = _bits(word, index * 32)
            entry["payload"] = f"word {index} 0x{word:08x} bits {_bit_text(bits)}"
            out["api_bits"].extend(bits)
        elif tlv_type == TLV_ENABLED_CAPABILITIES and tlv_len == 8:
            index, word = struct.unpack("<II", payload)
            bits = _bits(word, index * 32)
            entry["payload"] = f"word {index} 0x{word:08x} bits {_bit_text(bits)}"
            out["capa_bits"].extend(bits)
        elif tlv_type == TLV_CMD_VERSIONS:
            count = tlv_len // CMD_VER_SIZE
            for i in range(count):
                cmd, group, cmd_ver, notif_ver = struct.unpack_from(
                    CMD_VER_FMT, payload, i * CMD_VER_SIZE)
                out["cmd_versions"].append({
                    "cmd": cmd, "group": group,
                    "cmd_ver": cmd_ver, "notif_ver": notif_ver,
                })
            entry["payload"] = f"{count} x {CMD_VER_SIZE} B entries"
        elif tlv_type == TLV_FW_VERSION and tlv_len == 12:
            major, minor, local = struct.unpack_from("<III", payload, 0)
            entry["payload"] = (f"major {major}, minor 0x{minor:08x},"
                                f" local {local}")
        elif tlv_type in U32_TYPES and tlv_len == 4:
            (value,) = struct.unpack("<I", payload)
            entry["payload"] = f"0x{value:08x}"
        elif tlv_len:
            entry["payload"] = payload[:16].hex()
            if tlv_len > 16:
                entry["payload"] += "..."

        out["tlvs"].append(entry)
        off = body + ((tlv_len + 3) & ~3)

    out["consumed"] = off
    out["api_bits"] = sorted(set(out["api_bits"]))
    out["capa_bits"] = sorted(set(out["capa_bits"]))
    return out


def _bit_text(bits: list[int]) -> str:
    return ", ".join(str(b) for b in bits) if bits else "(none)"


def render_markdown(res: dict) -> str:
    h = res["header"]
    lines = [
        "# iwlwifi firmware container",
        "",
        f"- sha256 `{res['sha256']}`, {res['size']} bytes",
        f"- header: magic {h['magic']}, human-readable {h['human_readable']!r},"
        f" version {h['version']} ({h['version_number']}),"
        f" build {h['build']} (0x{h['build']:08x})",
        f"- {len(res['tlvs'])} TLVs, {res['consumed']} of {res['size']} bytes consumed"
        + (" -- TRUNCATED" if res["truncated_at"] is not None else ""),
        "",
        "## TLV sections",
        "",
        "| Offset | Type | Name | Length | Payload |",
        "|---|---|---|---|---|",
    ]
    for e in res["tlvs"]:
        lines.append(f"| {e['offset']} | {e['type']} | {e['name']} |"
                     f" {e['length']} | {e['payload'] or '-'} |")
    lines += [
        "",
        "## Capability bitvectors",
        "",
        f"- API bits set (TLV {TLV_API_CHANGES}): {_bit_text(res['api_bits'])}",
        f"- capability bits set (TLV {TLV_ENABLED_CAPABILITIES}):"
        f" {_bit_text(res['capa_bits'])}",
    ]
    if res["cmd_versions"]:
        lines += [
            "",
            "## Command versions (TLV 48)",
            "",
            "| # | cmd | group | cmd_ver | notif_ver |",
            "|---|---|---|---|---|",
        ]
        for i, e in enumerate(res["cmd_versions"], start=1):
            ver = "unknown" if e["cmd_ver"] == CMD_VER_UNKNOWN else str(e["cmd_ver"])
            nver = "unknown" if e["notif_ver"] == CMD_VER_UNKNOWN else str(e["notif_ver"])
            lines.append(f"| {i} | {e['cmd']} | {e['group']} | {ver} | {nver} |")
    lines.append("")
    return "\n".join(lines)


def selftest() -> int:
    def tlv(tlv_type: int, payload: bytes) -> bytes:
        pad = (-len(payload)) % 4
        return struct.pack("<II", tlv_type, len(payload)) + payload + b"\0" * pad

    header = struct.pack(HDR_FMT, 0, MAGIC, b"N/A::test", 0x11, 0x12345678, 0)
    api = tlv(TLV_API_CHANGES, struct.pack("<II", 0, 1 << 30))
    capa = tlv(TLV_ENABLED_CAPABILITIES, struct.pack("<II", 2, 1 << 4))
    cmds = tlv(TLV_CMD_VERSIONS, struct.pack(CMD_VER_FMT, 0x18, 0, 7, 7))
    res = parse(header + api + capa + cmds)

    assert res["header"]["version_number"] == 0x11
    assert res["api_bits"] == [30], res["api_bits"]
    assert res["capa_bits"] == [68], res["capa_bits"]   # word 2 bit 4
    assert res["cmd_versions"] == [
        {"cmd": 0x18, "group": 0, "cmd_ver": 7, "notif_ver": 7}], res["cmd_versions"]
    assert res["truncated_at"] is None

    bad = parse(header + api + struct.pack("<II", 1, 999))
    assert bad["truncated_at"] is not None
    try:
        parse(b"x" * HDR_SIZE)
    except ValueError:
        pass
    else:
        raise AssertionError("a wrong magic must be rejected")

    print("selftest: ok (container, API/capa words, command versions, truncation)")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", nargs="?", help="firmware image to decode")
    ap.add_argument("--json", action="store_true", help="print the decoded fields as JSON")
    ap.add_argument("--markdown", action="store_true", help="print the markdown report")
    ap.add_argument("--selftest", action="store_true", help="run the decoder's tests")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.image:
        ap.error("an image path is required (or --selftest)")

    data = Path(args.image).read_bytes()
    try:
        res = parse(data)
    except ValueError as exc:
        print(f"iwl-tlv: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(res, indent=2) if args.json else render_markdown(res))
    return 0


def argc_error(exc: ValueError) -> str:
    return f"{exc}"


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
