"""xhci1 register map — RK3568 usb_host1_xhci @ 0xfd000000 (xHCI standard register set).

Data-only module: names/offsets/bitfields for the peri-dump tools.  The dump
tools (tools/gdb/dump_peri.py, tools/gdb/peri_compare.py) load this file; adding
a peripheral means adding a map file here, not touching the tools.

PROVENANCE (spec specs/gdb-peri-dump/SPEC.md v1 §3.3; facts only, no code
imported from any GPL tree - cleanroom rule):
  - Node/base/window: [LNX-U] arch/arm64/boot/dts/rockchip/rk356x-base.dtsi:281-283
    usb_host1_xhci: usb@fd000000, reg = <0x0 0xfd000000 0x0 0x400000>;
    gdb-registers/rk3568-mmio.lst:19 (rtw88 dongle seat); INTID 202 = GIC_SPI
    170+32 (dtsi:284, DESIGN.md §10 address table).
  - Register layout: [LNX-U] drivers/usb/host/xhci.h:69-79 (struct xhci_cap_regs),
    :108-123 (struct xhci_op_regs), :88-93 (struct xhci_port_regs, 4 x le32);
    port group starts at op+0x400 (op reserved4[241] ends at 0x3FF).
    Cross-checked against [LNX-R] (BSP v6.1.115) xhci.h:57-75,99-119 - same layout.
  - Bitfields: [LNX-U] drivers/usb/host/xhci-caps.h (HC_LENGTH:12, HC_VERSION:15,
    HCS_SLOTS_MASK:19, HCS_MAX_INTRS:21, HCS_MAX_PORTS:24, HCS_IST_VALUE:37,
    HCS_IST_UNIT:38, HCS_ERST_MAX:40, HCS_MAX_SP_HI:45/LO:48, HCC_*:59-85,
    DBOFF_MASK:90, RTSOFF_MASK:95); xhci-port.h:5-119 (PORT_*); xhci-ext-caps.h
    (XHCI_STS_HALT:14, XHCI_CMD_RUN:74, CMD_EIE:76, CMD_HSEIE:78, CMD_EWE:80,
    XHCI_STS_CNR:85); xhci.h (CMD_RESET:132, STS_FATAL:162, STS_EINT:164,
    STS_PORT:166, STS_SAVE:169, STS_RESTORE:171, STS_SRE:173, STS_HCE:177,
    CMD_RING_CYCLE:193, CMD_RING_RUNNING:199, MAX_DEVS:205, CONFIG_U3E:207,
    CONFIG_CIE:209, XHCI_PAGE_SIZE_MASK:213).
  - Read-safety exclusions (first cases of the "read-safety review line" rule,
    spec §3.9 item 6): doorbell array is WO (xhci.h:308) - never read; portpmsc/
    portli/porthlmpc and the RTSOFF-derived runtime segment and the DWC3 private
    segment (0xC100+, dwc3/core.h:87) are out of the v1 map (spec §1.5).

Sampling class ("cls", spec §1.2):
  config - CPU-written only; CPU frozen at the stop point => dump value IS the
           stopped-moment truth (bit-stable across repeated dumps).
  status - hardware-autonomous (dongle DMA keeps flowing while the CPU is
           stopped) => dump value is live hardware state, drift between repeated
           dumps is expected evidence, not a fault.
"""

MAP_VERSION = "xhci1-v1"
PERI = {"name": "xhci1", "base": 0xFD000000, "window": 0x400000}

# Derived-window sanity bound: xHCI-visible window ends at 0x7fff
# ([DWC3] dwc3/core.h:85-86 DWC3_XHCI_REGS_START 0x0 / END 0x7fff).
MAX_DERIVED_OFFSET = 0x8000

# caplength guard (spec §3.2 item 3): outside [0x20, 0xE0] => degraded-cap-only.
CAPLENGTH_MIN = 0x20
CAPLENGTH_MAX = 0xE0

MAX_PORTS_DUMP = 4  # spec §3.2 item 4 (HCS_MAX_PORTS derived, dump first 4)

# Fields: (name, low_bit, high_bit) inclusive.  Empty list = no named fields.
#
# region: "cap"    offset is relative to PERI.base.
#         "op"     offset is relative to op_base = base + CAPLENGTH.
#         "portsc" one template entry; PORTSC(n) = op_base + 0x400 + 0x10*(n-1).

CAP_REGS = [
    {
        "name": "CAPLENGTH", "offset": 0x00, "size": 4, "access": "RO",
        "cls": "config",
        "fields": [("CAPLENGTH", 0, 7), ("HCIVERSION", 16, 31)],
        "note": "xhci.h:69 hc_capbase; HC_LENGTH xhci-caps.h:12, HC_VERSION :15",
    },
    {
        "name": "HCSPARAMS1", "offset": 0x04, "size": 4, "access": "RO",
        "cls": "config",
        "fields": [("HCS_SLOTS", 0, 7), ("HCS_MAX_INTRS", 8, 18),
                   ("HCS_MAX_PORTS", 24, 31)],
        "note": "xhci.h:70 hcs_params1; xhci-caps.h:19,21,24 (MaxPorts drives "
                "the portsc count)",
    },
    {
        "name": "HCSPARAMS2", "offset": 0x08, "size": 4, "access": "RO",
        "cls": "config",
        "fields": [("HCS_IST_VALUE", 0, 2), ("HCS_IST_UNIT", 3, 3),
                   ("HCS_ERST_MAX", 4, 7), ("HCS_MAX_SP_HI", 21, 25),
                   ("HCS_MAX_SP_LO", 27, 31)],
        "note": "xhci.h:71; xhci-caps.h:37-48 (MAX_SCRATCHPAD = hi<<5|lo, kept "
                "as two raw fields)",
    },
    {
        "name": "HCSPARAMS3", "offset": 0x0C, "size": 4, "access": "RO",
        "cls": "config",
        "fields": [("HCS_U1_LATENCY", 0, 7), ("HCS_U2_LATENCY", 16, 31)],
        "note": "xhci.h:72; xhci-caps.h:52-55",
    },
    {
        "name": "HCCPARAMS1", "offset": 0x10, "size": 4, "access": "RO",
        "cls": "config",
        "fields": [("HCC_64BIT_ADDR", 0, 0), ("HCC_64BYTE_CONTEXT", 2, 2),
                   ("HCC_PPC", 3, 3), ("HCC_EXT_CAPS", 16, 31)],
        "note": "xhci.h:73 hcc_params; xhci-caps.h:59-85",
    },
    {
        "name": "DBOFF", "offset": 0x14, "size": 4, "access": "RO",
        "cls": "config",
        "fields": [("DBOFF_MASK", 2, 31)],
        "note": "xhci.h:74 db_off; xhci-caps.h:90 - offset READ ONLY, the "
                "doorbell array it points at is WO and never read (xhci.h:308)",
    },
    {
        "name": "RTSOFF", "offset": 0x18, "size": 4, "access": "RO",
        "cls": "config",
        "fields": [("RTSOFF_MASK", 5, 31)],
        "note": "xhci.h:75 run_regs_off; xhci-caps.h:95 - runtime segment not "
                "in v1 map (spec 1.5)",
    },
    {
        "name": "HCCPARAMS2", "offset": 0x1C, "size": 4, "access": "RO",
        "cls": "config",
        "fields": [],
        "note": "xhci.h:78 hcc_params2; xhci-caps.h:99-122 HCC2_* bits0-12 "
                "(bit numbers only in v1)",
    },
]

OP_REGS = [
    {
        "name": "USBCMD", "offset": 0x00, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("CMD_RUN", 0, 0), ("CMD_RESET", 1, 1), ("CMD_EIE", 2, 2),
                   ("CMD_HSEIE", 3, 3), ("CMD_EWE", 10, 10)],
        "note": "xhci.h:110 command; xhci-ext-caps.h:74,76,78,80 + xhci.h:132",
    },
    {
        "name": "USBSTS", "offset": 0x04, "size": 4, "access": "RO",
        "cls": "status",
        "fields": [("STS_HALT", 0, 0), ("STS_FATAL", 2, 2), ("STS_EINT", 3, 3),
                   ("STS_PORT", 4, 4), ("STS_SAVE", 8, 8),
                   ("STS_RESTORE", 9, 9), ("STS_SRE", 10, 10),
                   ("STS_CNR", 11, 11), ("STS_HCE", 12, 12)],
        "note": "xhci.h:111 status; xhci-ext-caps.h:14,85 + xhci.h:162-177; "
                "hardware-autonomous while the CPU is stopped",
    },
    {
        "name": "PAGESIZE", "offset": 0x08, "size": 4, "access": "RO",
        "cls": "config",
        "fields": [("PAGESIZE_MASK", 0, 15)],
        "note": "xhci.h:112 page_size; xhci.h:213 XHCI_PAGE_SIZE_MASK",
    },
    {
        "name": "DNCTRL", "offset": 0x14, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("DEV_NOTE_MASK", 0, 15)],
        "note": "xhci.h:115 dev_notification; xhci.h:185",
    },
    {
        "name": "CRCR", "offset": 0x18, "size": 8, "access": "RW",
        "cls": "config",
        "fields": [("CMD_RING_CYCLE", 0, 0), ("CMD_RING_PAUSE", 1, 1),
                   ("CMD_RING_ABORT", 2, 2), ("CMD_RING_RUNNING", 3, 3)],
        "note": "xhci.h:117 cmd_ring (le64); xhci.h:193-201; pointer domain "
                "bits63:6 is WO per xHCI spec - reads zero expected, board "
                "verdict pending (spec open question 1)",
    },
    {
        "name": "DCBAAP", "offset": 0x30, "size": 8, "access": "RW",
        "cls": "config",
        "fields": [],
        "note": "xhci.h:121 dcbaa_ptr (le64); device context base array pointer",
    },
    {
        "name": "CONFIG", "offset": 0x38, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("MAX_DEVS", 0, 7), ("CONFIG_U3E", 8, 8),
                   ("CONFIG_CIE", 9, 9)],
        "note": "xhci.h:122 config_reg; xhci.h:205-209",
    },
]

# Template for PORTSC(n), n = 1..min(HCS_MAX_PORTS, MAX_PORTS_DUMP).
# Change bits (CSC/PEC/WRC/OCC/RC/PLC/CEC) are W1C: read-only in this tool.
PORT_REGS = [
    {
        "name": "PORTSC", "offset": 0x00, "size": 4, "access": "RW",
        "cls": "status",
        "fields": [("PORT_CONNECT", 0, 0), ("PORT_PE", 1, 1), ("PORT_OC", 3, 3),
                   ("PORT_RESET", 4, 4), ("PORT_PLS", 5, 8),
                   ("PORT_POWER", 9, 9), ("DEV_SPEED", 10, 13),
                   ("PORT_LED", 14, 15), ("PORT_LINK_STROBE", 16, 16),
                   ("PORT_CSC", 17, 17), ("PORT_PEC", 18, 18),
                   ("PORT_WRC", 19, 19), ("PORT_OCC", 20, 20),
                   ("PORT_RC", 21, 21), ("PORT_PLC", 22, 22),
                   ("PORT_CEC", 23, 23), ("PORT_CAS", 24, 24),
                   ("PORT_WKCONN_E", 25, 25), ("PORT_WKDISC_E", 26, 26),
                   ("PORT_WKOC_E", 27, 27), ("PORT_DEV_REMOVE", 30, 30),
                   ("PORT_WR", 31, 31)],
        "note": "xhci.h:89 portsc; xhci-port.h:5-119; PORTSC(n) = op_base + "
                "0x400 + 0x10*(n-1) (xhci.h:120-122); change bits are W1C - "
                "read-only here; hardware-autonomous (dongle DMA)",
    },
]

EXCLUDED = [
    {"name": "doorbell[]", "where": "base + DBOFF",
     "why": "WO per xHCI spec (xhci.h:308) - read meaningless, implementation "
            "may fault; first read-safety exclusion (spec 3.9 item 6)"},
    {"name": "portpmsc/portli/porthlmpc", "where": "port group +0x04/+0x08/+0x0C",
     "why": "not in v1 map (spec 1.5)"},
    {"name": "runtime segment", "where": "base + RTSOFF",
     "why": "not in v1 map (spec 1.5)"},
    {"name": "DWC3 private segment", "where": "base + 0xC100..",
     "why": "out of scope, dwc3/core.h:87; later USB round (spec 1.5)"},
]


def validate_map():
    """Static self-check (spec R1 judgment 2): monotonic offsets per region,
    derived window <= MAX_DERIVED_OFFSET, enum legality, every entry carries a
    note and in-range fields.  Raises ValueError on any violation."""
    seen_regions = {"cap": -1, "op": -1, "portsc": -1}
    for entry in CAP_REGS + OP_REGS + PORT_REGS:
        name = entry["name"]
        region = "op" if entry in OP_REGS else ("portsc" if entry in PORT_REGS else "cap")
        if entry["access"] not in ("RO", "RW"):
            raise ValueError("%s: access must be RO|RW (mixed-ness goes to note)" % name)
        if entry["cls"] not in ("config", "status"):
            raise ValueError("%s: cls must be config|status" % name)
        if not entry.get("note"):
            raise ValueError("%s: missing note (provenance/read-safety)" % name)
        if not (0 < entry["size"] <= 8):
            raise ValueError("%s: bad size" % name)
        # portsc template offset is intra-group by design; cap/op must advance.
        if region != "portsc":
            if entry["offset"] <= seen_regions[region]:
                raise ValueError("%s: offset not monotonic in region %s" % (name, region))
            seen_regions[region] = entry["offset"]
        if entry["offset"] > MAX_DERIVED_OFFSET:
            raise ValueError("%s: offset beyond xHCI window" % name)
        for (fname, lo, hi) in entry["fields"]:
            if not (0 <= lo <= hi <= 63):
                raise ValueError("%s/%s: bad bit range" % (name, fname))
    # Derived worst case: op_base(capped) + 0x400 + 4*0x10 <= MAX_DERIVED_OFFSET.
    worst = CAPLENGTH_MAX + 0x400 + MAX_PORTS_DUMP * 0x10
    if worst > MAX_DERIVED_OFFSET:
        raise ValueError("derived window 0x%x exceeds xHCI window" % worst)
    return True
