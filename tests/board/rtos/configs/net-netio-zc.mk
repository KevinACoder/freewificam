# K2-06 R1a: the A6 arm plus zero-copy receive (the xmac handoff shape).
#
# Why this is the first receive lever: the K2-03 five-segment decomposition puts
# 2.6 us/frame in the copy bucket (two copies: DMA buffer -> driver staging,
# staging -> pbuf) and 11.5 us in feed, and the RTOS spends 14.5k cycles/frame
# against Linux's 9.85k at the same 816 MHz. The copies are expensive mostly
# because each one touches 23 cache lines that the DMA just wrote and the
# driver just invalidated, so the saving is larger than a memcpy's cycle count.
#
# CONFIG_GMAC_RX_ZC lends the DMA buffer to the stack through a per-slot
# pbuf_custom wrapper and re-arms the descriptor with a spare; the driver keeps
# a spare pool of 2x the ring depth and drops the frame rather than parking the
# DMA when the pool empties. The failure shape to watch is the 2026-10-25 one:
# a long-held buffer on the TCP receive path drains the spare pool and the
# receive side collapses (rx_dropped climbs, UDP unaffected). The accept
# criterion is UDP C2S >= 112.5 MB/s at 0% loss WITH the TCP receive path
# healthy, which is why the same boot also runs the TCP cell.
#
# Only this lever changes: everything else is net-netio.mk (A6 + prof off +
# MAC TX counters). The TCP arm uses the same image, so the ZC effect on the
# spare pool is visible in the same run.
include $(LINE)/configs/net-netio.mk

CONFIG_LEVERS += -DCONFIG_GMAC_RX_ZC=1
