"""gmac register map - RK3568 gmac1 @ 0xfe010000 / gmac0 @ 0xfe2a0000
(Synopsys DWC Ethernet QoS dwmac-4.20a, MAC_VERSION 0x3051).

Data-only module: names/offsets/bitfields for the peri-dump tools.  The dump
tools (tools/gdb/dump_peri.py, tools/gdb/peri_compare.py) load this file; adding
a peripheral means adding a map file here, not touching the tools.

Both ports are the same IP; PERI.base is gmac1 (the port the S7/S7b acceptance
traffic ran on).  gmac0 = base 0xfe2a0000, identical map.

PROVENANCE (facts only, no code imported from any GPL tree - cleanroom rule):
  - Register offsets, all MAC/MTL segments: [LNX-U] (local tree,
    rk3568_lab/os/linux-upstream) drivers/net/ethernet/stmicro/stmmac/
    dwmac4.h:16-44 (GMAC_CONFIG 0x0, GMAC_PACKET_FILTER 0x8, GMAC_QX_TX_FLOW_
    CTRL 0x70+x*4, GMAC_RX_FLOW_CTRL 0x90, GMAC_RXQ_CTRL0/1 0xa0/0xa4, GMAC_
    INT_STATUS 0xb0, GMAC_INT_EN 0xb4, GMAC_DEBUG 0x114, GMAC_HW_FEATURE1
    0x120, GMAC_MDIO_ADDR/DATA 0x200/0x204, GMAC_ADDR_HIGH/LOW 0x300/0x304);
    MTL_TXQ0/RXQ0 operation-mode 0xd00/0xd30 (dwmac4.h MTL block, same values
    as the ported board-verified header).  Bitfields: GMAC_CONFIG_IPC BIT(27)
    (dwmac4.h:203 region), GMAC_HW_TXFIFOSIZE GENMASK(10,6) (dwmac4.h:203).
  - DMA segment: [LNX-U] dwmac4_dma.h:51-115 - DMA_CHAN_BASE_ADDR 0x1100,
    per-channel +0x0 CONTROL / +0x4 TX_CONTROL / +0x8 RX_CONTROL / +0x10,14
    TX base / +0x18,1c RX base / +0x20 TX_END / +0x28 RX_END / +0x2c,30 ring
    len / +0x34 INTR_ENA; TXPBL GENMASK(21,16), OSP BIT(4), ST BIT(0),
    RXPBL GENMASK(21,16), RBSZ GENMASK(14,1), SR BIT(0), PBLX8 BIT(16);
    INTR_ENA RBU BIT(7), RPS BIT(8) beside the 4_10a NIE BIT(15)/AIE BIT(14)
    layout.  STATUS lives at +0x60 (W1C - excluded, see below).
  - Ported side (board-verified, configs/lwip-lan acceptance anchors):
    freewebcam board/rk3568-itx/drivers/dwc_eqos.h - identical offsets and
    field positions; golden dump (eqos_rk3568.c) cross-checked the CRU/GRF
    side, this map covers the IP block itself.

READ-SAFETY REVIEW (spec specs/gdb-peri-dump §3.9 item 6, every map must
carry the review line):
  EXCLUDED from the map because a dump read would itself corrupt state:
    - GMAC_INT_STATUS   0x0b0  W1C: the read clears latched MAC events.
    - DMA_CHAN_STATUS   0x1160 W1C: the read clears TX/RX completion state -
      dumping this while the driver is running silently eats the next
      interrupt the ISR is about to see.
    - MAC statistics block 0x0600-0x07ff: read-to-clear counters (each dump
      would reset them; the driver has none of them in use, but the rule
      stands for future readers).
  Included read-only-but-safe status: GMAC_DEBUG, MTL queue DEBUG (RO
  snapshots), DMA_CHAN_DEBUG_STATUS (RO).  MDIO_ADDR/MDIO_DATA are read
  while the CPU is stopped and no MDIO transfer is in flight (this driver
  only runs MDIO during bring-up / explicit PHY access).

Sampling class ("cls"):
  config - CPU-written only; frozen at the stop point => the dump is the
           bit-stable configuration truth (this is the diff surface against
           the Linux side).
  status - hardware/DMA-autonomous (descriptors keep being fetched while the
           CPU is stopped) => live values, drift between dumps is expected.
"""

MAP_VERSION = "gmac-v1"
PERI = {"name": "gmac", "base": 0xFE010000, "window": 0x10000}

# The dump window is bounded by the segments this map actually names
# (DMA channel 0 ends at 0x1164 + 4).  Keep the guard loose but sane.
MAX_DERIVED_OFFSET = 0x1400

REGS = [
    # --- MAC configuration -------------------------------------------------
    {
        "name": "MAC_CONFIGURATION", "offset": 0x000, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("RE", 0, 0), ("TE", 1, 1), ("DCRS", 9, 9), ("DM", 13, 13),
                   ("FES", 14, 14), ("PS", 15, 15), ("JE", 16, 16),
                   ("JD", 17, 17), ("BE", 18, 18), ("IPC", 27, 27)],
        "note": "dwmac4.h:16 GMAC_CONFIG; IPC BIT(27) = rx checksum offload "
                "(Linux enables it, the lwIP build does not - key diff bit)",
    },
    {
        "name": "MAC_PACKET_FILTER", "offset": 0x008, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("PR", 0, 0), ("HMC", 2, 2), ("PM", 4, 4), ("DBF", 5, 5)],
        "note": "dwmac4.h:18 GMAC_PACKET_FILTER; PR = promiscuous (both sides "
                "receive everything the lab switch delivers)",
    },
    {
        "name": "MAC_Q0_TX_FLOW_CTRL", "offset": 0x070, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("PT", 0, 15), ("TFE", 16, 16)],
        "note": "dwmac4.h:21 GMAC_QX_TX_FLOW_CTRL(0); pause frame control",
    },
    {
        "name": "MAC_RX_FLOW_CTRL", "offset": 0x090, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("RFE", 0, 0)],
        "note": "dwmac4.h:20 GMAC_RX_FLOW_CTRL; rx flow control enable",
    },
    {
        "name": "MAC_RXQ_CTRL0", "offset": 0x0A0, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("RXQ0EN", 0, 2)],
        "note": "dwmac4.h:24; RXQ0EN 2 = DCB mode (the ported driver writes "
                "this exact value)",
    },
    {
        "name": "MAC_RXQ_CTRL1", "offset": 0x0A4, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("MCBCQEN", 20, 20)],
        "note": "dwmac4.h:25; multicast/broadcast to queue 0",
    },
    {
        "name": "MAC_INTERRUPT_ENABLE", "offset": 0x0B4, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("TIM", 0, 3), ("LPIIM", 5, 5), ("RGSMIIIM", 6, 6),
                   ("PMTIM", 8, 8), ("PHYIE", 13, 13)],
        "note": "dwmac4.h:29 GMAC_INT_EN; MAC-level interrupts (link change "
                "et al). The ported driver masks ALL of these (polls MDIO); "
                "Linux enables RGSMIIIM - diff is expected and harmless",
    },
    {
        "name": "MAC_1US_TIC_COUNTER", "offset": 0x0DC, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("TIC", 0, 7)],
        "note": "dwmac4.h GMAC_1US_TIC_COUNTER; PCLK MHz - 1",
    },
    {
        "name": "MAC_DEBUG", "offset": 0x114, "size": 4, "access": "RO",
        "cls": "status",
        "fields": [("TPEXSTS", 16, 16), ("TPFCS", 17, 17), ("RFCS", 25, 25),
                   ("RPEXSTS", 24, 24)],
        "note": "dwmac4.h:33 GMAC_DEBUG; live MAC FIFO/flow status - useful "
                "to see a stalled TX FIFO at the stop point",
    },
    {
        "name": "MAC_VERSION", "offset": 0x110, "size": 4, "access": "RO",
        "cls": "config",
        "fields": [("VERSION", 0, 15), ("ID", 16, 31)],
        "note": "0x3051 on this silicon (dwmac-4.20a); bring-up anchor value",
    },
    {
        "name": "MAC_HW_FEATURE1", "offset": 0x120, "size": 4, "access": "RO",
        "cls": "config",
        "fields": [("RXFIFOSZ", 0, 4), ("TXFIFOSZ", 6, 10)],
        "note": "dwmac4.h:35 GMAC_HW_FEATURE1, GMAC_HW_TXFIFOSIZE GENMASK(10,6)"
                "; FIFO size = 128 << field, drives the MTL queue-size writes",
    },
    {
        "name": "MAC_MDIO_ADDRESS", "offset": 0x200, "size": 4, "access": "RW",
        "cls": "status",
        "fields": [("FIA", 0, 1), ("PA", 21, 25), ("RDA", 16, 20),
                   ("CR", 8, 11), ("GB", 0, 0)],
        "note": "dwmac4.h:38 GMAC_MDIO_ADDR; PA[25:21]/RDA[20:16] - this "
                "board's MDIO address layout (the bring-up landmine). Safe "
                "to read while stopped: no transfer in flight",
    },
    {
        "name": "MAC_MDIO_DATA", "offset": 0x204, "size": 4, "access": "RW",
        "cls": "status",
        "fields": [("GD", 0, 15)],
        "note": "dwmac4.h:39; last MDIO read value, diagnostic only",
    },
    {
        "name": "MAC_ADDRESS0_HIGH", "offset": 0x300, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("ADDRHI", 0, 15), ("AE", 31, 31)],
        "note": "dwmac4.h:43 GMAC_ADDR_HIGH(0)",
    },
    {
        "name": "MAC_ADDRESS0_LOW", "offset": 0x304, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("ADDRLO", 0, 31)],
        "note": "dwmac4.h:44 GMAC_ADDR_LOW(0)",
    },
    # --- MTL ---------------------------------------------------------------
    {
        "name": "MTL_TXQ0_OPERATION_MODE", "offset": 0xD00, "size": 4,
        "access": "RW",
        "cls": "config",
        "fields": [("TQS", 16, 24), ("TXQEN", 2, 3), ("TSF", 1, 1),
                   ("FTQ", 0, 0)],
        "note": "queue size = (TQS+1)*256 bytes; TXQEN 2 = enabled; TSF "
                "(store-and-forward) - Linux runs store-and-forward on TX, "
                "the ported driver sets TSF too; key diff field",
    },
    {
        "name": "MTL_TXQ0_DEBUG", "offset": 0xD18, "size": 4, "access": "RO",
        "cls": "status",
        "fields": [("TRCSTS", 1, 2), ("TXQSTS", 4, 4), ("PAUSED", 5, 5)],
        "note": "RO snapshot; TXQSTS=1 at the stop point means frames still "
                "sitting in the MTL TX queue",
    },
    {
        "name": "MTL_RXQ0_OPERATION_MODE", "offset": 0xD30, "size": 4,
        "access": "RW",
        "cls": "config",
        "fields": [("RQS", 20, 29), ("RSF", 5, 5), ("FEP", 4, 4),
                   ("FUP", 3, 3)],
        "note": "queue size = (RQS+1)*256; RSF/FEP/FUP - the ported driver "
                "enables all three (store-and-forward + flush on packets "
                "and on underflow)",
    },
    {
        "name": "MTL_RXQ0_DEBUG", "offset": 0xD38, "size": 4, "access": "RO",
        "cls": "status",
        "fields": [("RRCSTS", 1, 2), ("RWCSTS", 6, 6), ("RPRFCS", 8, 8)],
        "note": "RO snapshot of the RX queue fill state",
    },
    # --- DMA ---------------------------------------------------------------
    {
        "name": "DMA_MODE", "offset": 0x1000, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("SWR", 0, 0), ("DA", 1, 1), ("AA", 2, 2)],
        "note": "dwmac4_dma.h DMA_MODE; SWR reads back its in-progress state",
    },
    {
        "name": "DMA_SYSBUS_MODE", "offset": 0x1004, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("WR_OSR_LMT", 24, 28), ("RD_OSR_LMT", 16, 20),
                   ("EAME", 11, 11), ("BLEN16", 3, 3), ("BLEN8", 2, 2),
                   ("BLEN4", 1, 1)],
        "note": "board dts stmmac-axi-config: wr_osr 4 / rd_osr 8 / blen 16,"
                "8,4 - the ported driver writes the same values",
    },
    {
        "name": "DMA_CH0_CONTROL", "offset": 0x1100, "size": 4, "access": "RW",
        "cls": "config",
        "fields": [("PBLX8", 16, 16), ("DSL", 18, 21)],
        "note": "dwmac4_dma.h:69 PBLX8 BIT(16); DSL = descriptor skip length "
                "(cache-line spacing for separated descriptors) - both "
                "sides run contiguous 16-byte descriptors, DSL=0",
    },
    {
        "name": "DMA_CH0_TX_CONTROL", "offset": 0x1104, "size": 4,
        "access": "RW",
        "cls": "config",
        "fields": [("TXPBL", 16, 21), ("OSP", 4, 4), ("ST", 0, 0)],
        "note": "TXPBL GENMASK(21,16) - ported driver 32, Linux default 8; "
                "OSP = operate-on-second-packet; ST = start",
    },
    {
        "name": "DMA_CH0_RX_CONTROL", "offset": 0x1108, "size": 4,
        "access": "RW",
        "cls": "config",
        "fields": [("RXPBL", 16, 21), ("RBSZ", 1, 14), ("SR", 0, 0)],
        "note": "RBSZ GENMASK(14,1) in 2-byte units - ported driver 2048 "
                "bytes/slot",
    },
    {
        "name": "DMA_CH0_TX_BASE_ADDR_HI", "offset": 0x1110, "size": 4,
        "access": "RW", "cls": "config", "fields": [],
        "note": "dwmac4_dma.h:86; upper 32 bits of the TX ring (0 on this "
                "board: DRAM below 4 GiB)",
    },
    {
        "name": "DMA_CH0_TX_BASE_ADDR_LO", "offset": 0x1114, "size": 4,
        "access": "RW", "cls": "config", "fields": [],
        "note": "TX ring physical base - the ported ring lives in the "
                ".dma_noc section at 0x0c000000 (non-cacheable)",
    },
    {
        "name": "DMA_CH0_RX_BASE_ADDR_HI", "offset": 0x1118, "size": 4,
        "access": "RW", "cls": "config", "fields": [],
        "note": "dwmac4_dma.h:88",
    },
    {
        "name": "DMA_CH0_RX_BASE_ADDR_LO", "offset": 0x111C, "size": 4,
        "access": "RW", "cls": "config", "fields": [],
        "note": "RX ring physical base",
    },
    {
        "name": "DMA_CH0_TX_END_ADDR", "offset": 0x1120, "size": 4,
        "access": "RW", "cls": "status", "fields": [],
        "note": "dwmac4_dma.h:90; the tail pointer the DMA chases - moves "
                "with every armed descriptor (hence status class)",
    },
    {
        "name": "DMA_CH0_RX_END_ADDR", "offset": 0x1128, "size": 4,
        "access": "RW", "cls": "config", "fields": [],
        "note": "dwmac4_dma.h:91; written once by the ported driver at "
                "channel start (whole-ring in-flight discipline)",
    },
    {
        "name": "DMA_CH0_TX_RING_LEN", "offset": 0x112C, "size": 4,
        "access": "RW", "cls": "config", "fields": [],
        "note": "ring length - 1 (0x3f = 64 slots both sides)",
    },
    {
        "name": "DMA_CH0_RX_RING_LEN", "offset": 0x1130, "size": 4,
        "access": "RW", "cls": "config", "fields": [],
        "note": "ring length - 1 (0x3f = 64 slots)",
    },
    {
        "name": "DMA_CH0_INTERRUPT_ENABLE", "offset": 0x1134, "size": 4,
        "access": "RW",
        "cls": "config",
        "fields": [("TIE", 0, 0), ("TXSE", 1, 1), ("TBUE", 2, 2),
                   ("RIE", 6, 6), ("RBUE", 7, 7), ("RSE", 8, 8),
                   ("FBEIE", 12, 12), ("AIE", 14, 14), ("NIE", 15, 15)],
        "note": "dwmac4_dma.h:97-115; 4_10a NIE BIT(15)/AIE BIT(14). Linux "
                "also arms RBUE/RSE in its abnormal set - the ported driver "
                "leaves them masked; diff is informative, not fatal",
    },
    {
        "name": "DMA_CH0_DEBUG_STATUS", "offset": 0x1164, "size": 4,
        "access": "RO",
        "cls": "status",
        "fields": [("AXI_RD_BUSY", 0, 0), ("AXI_WR_BUSY", 1, 1),
                   ("RXP_FETCH", 4, 4), ("RXP_WRITE", 5, 5),
                   ("TXP_FETCH", 8, 8), ("TXP_WRITE", 9, 9)],
        "note": "RO; live DMA engine state at the stop point - TXP_FETCH=1 "
                "means the descriptor fetch path is mid-transaction",
    },
]

# W1C / read-to-clear registers that must never be added (read-safety
# review line, spec §3.9 item 6):
#   GMAC_INT_STATUS      0x0B0   (W1C)
#   DMA_CH0_STATUS       0x1160  (W1C)
#   MAC statistics       0x0600-0x07FF (read-to-clear counters)


def validate_map():
    """Structural checks the dump tool runs after loading the module."""
    offs = set()
    for reg in REGS:
        name = reg.get("name")
        off = reg["offset"]
        assert name and off not in offs, "duplicate/empty name or offset: %s" % name
        offs.add(off)
        assert reg["size"] in (1, 2, 4), name
        assert reg["cls"] in ("config", "status"), name
        assert reg["access"] in ("RO", "RW"), name
        for low, high in [(f[1], f[2]) for f in reg["fields"]]:
            assert 0 <= low <= high <= 31, (name, low, high)
    assert max(offs) < MAX_DERIVED_OFFSET, "register outside the dump window"
