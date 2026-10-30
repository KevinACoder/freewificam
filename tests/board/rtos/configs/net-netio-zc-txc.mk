# K2-06 T2c: the R1a arm (A6 + prof off + MAC TX counters + zero-copy RX) plus
# transmit checksum insertion offload.
#
# Why: after T2a closed the completion-signal hole, the transmit leg runs at
# 11.7k cycles/frame (14.3 us at 816 MHz) against the 10.4k-cycle budget. The
# park cost is no longer the term (38.9k parks on 418k frames, and the drain
# path is cheap); what is left is per-frame work, and the stack's UDP/TCP
# checksum pass is the largest removable piece of it. CIC=3 lets the MAC
# compute and insert the L4 checksum; lwIP then skips its own generation pass
# for IPv4 TCP/UDP on this netif (NETIF_CHECKSUM_GEN_* off, wired in
# ethnetif.c). ICMP stays with the stack, so ARP/ping behaviour is unchanged.
#
# Accept: UDP S2C >= 112.5 MB/s at 0% loss, TCP S2C not worse, and the client's
# receive counters still 0% (a bad inserted checksum would surface as the
# peer's stack dropping frames, i.e. as loss in the client's report).
include $(LINE)/configs/net-netio-zc.mk

CONFIG_LEVERS += -DCONFIG_GMAC_TX_CSUM=1
