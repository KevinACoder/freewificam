# netio arm: the throughput instrument on the wired bench (PLAT-01).
#
# The board runs the server half of the netio program (the MDK-Middleware
# implementation, ported to lwIP in app/cases/net_mdk_netio.c); the Pi runs the
# unmodified upstream NETIO 1.33 client. Both speak the same wire protocol, so
# one component - the client - is identical across every arm, which is what
# makes readings comparable.
#
# One boot serves any number of connections, so a single image can be measured
# with several packet sizes (`netio -b`) and both protocols, and the difference
# is attributable to what the client asked for rather than to a rebuild.
#
# The case list is this one case: the wired line is the bench's sensitivity
# standard - a link that does not vary - and a boot that also ran a wireless arm
# would answer two questions at once.
CONFIG_BACKEND := freertos
CONFIG_CASES := net_mdk_netio
CONFIG_OPT := -O2
CONFIG_PROFILE := base

# The receive path defaults, moved from one-off command lines into the arm so
# every future measurement starts from the best known configuration:
#   - CONFIG_GMAC_RX_HWCSUM: the MAC validates the IP/L4 checksum; the stack's
#     software pass over every frame was measured at ~18% of the feed segment.
#
# LWIP_TCPIP_CORE_LOCKING_INPUT=0 (with CONFIG_GMAC_RX_BATCH therefore inert):
# the 2026-10-30 A6 set ran the whole receive burst inside the driver's RX
# thread to save a mailbox post per frame (51.6 -> 70.0 MB/s then). It also
# means the socket reader cannot take the core lock until the burst ends, so
# a TCP burst larger than the socket's receive mailbox is refused by the
# stack (tcp.drop / mib2.tcpinerrs) and re-offered only by the 250 ms fast
# timer. Measured on this arm (2026-11-02): TCP peer-to-board 33.0 kB/s with
# input on the RX thread, 40.8 MB/s with input on the tcpip thread, while the
# UDP receive cell stayed at 114.0 MB/s / 0% loss and the UDP and TCP
# transmit directions were unchanged (105.5 and 45.2 MB/s). The lever bought
# nothing this tree still needs and cost the receive direction three orders
# of magnitude.
CONFIG_LEVERS := -DCONFIG_GMAC_RX_HWCSUM=1
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u

# Measurement boots run with the per-frame instrument off. The A6 arm above
# pays 11.3% for the five tick buckets (K2-05), and a baseline that carries the
# instrument is not the number the final 12.8 us/frame budget is against. The
# census counters the transmit ladder needs - tx_waits, tx_wait_ticks,
# tx_occ_max, tx_sem_count, irq_n, irq_tx - accumulate unconditionally
# (CONFIG_GMAC_PROF only folds the tick buckets to zero), so this switch
# silences the prof/proftx tick columns, not the TX evidence. An attribution
# boot that needs the five segments sets CONFIG_GMAC_PROF=1 on the command
# line and is labelled instrumented, never baseline.
CONFIG_GMAC_PROF := 0
