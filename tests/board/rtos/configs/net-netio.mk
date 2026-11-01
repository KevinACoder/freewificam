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

# The receive path defaults are the K2-03 A6 set plus the K2-16 input-path
# correction, so every future measurement starts from the best known
# configuration instead of re-deriving it:
#   - LWIP_TCPIP_CORE_LOCKING_INPUT=0: input runs on the tcpip thread, the
#     shape upstream lwIP ships. The A6 choice (=1, the RX thread runs the
#     whole stack input itself) saved a mailbox post per frame at 70 MB/s on
#     a UDP flood, but it starves the socket reader: the receive burst holds
#     the core lock, the socket's one-slot receive mailbox overruns, lwIP
#     refuses the rest (tcp.drop / mib2.tcpinerrs) and re-offers it only on
#     the 250 ms fast timer - measured on the peer-to-board TCP cell as
#     33.0 kB/s with a wide-open window. With input on the tcpip thread the
#     same cell runs 43.4 / 45.0 MB/s and the UDP cells do not move
#     (113.99 MB/s, 0% both directions, 2/2, 2026-11-02).
#   - CONFIG_GMAC_RX_HWCSUM: the MAC validates the IP/L4 checksum; the stack's
#     software pass over every frame was measured at ~18% of the feed segment.
#
# CONFIG_GMAC_RX_BATCH is gone with the input lever: it folded the per-frame
# core-lock acquisitions of the in-RX-thread shape, and that shape no longer
# exists (the tcpip thread takes one lock over its whole mailbox batch).
#
# The zero-copy receive bridge needed its own fix for this: its free list and
# ready ring are updated from three threads and were guarded by the port-line
# mask, which does not stop the tick - a lost half of head/count handed a live
# slot out again, two pbufs ended up over one custom wrapper, and lwIP died on
# 'pbuf_free: p->ref > 0' within seconds of a real TCP rate (audited: six
# wrap-over-live events before the assert). Now masked-IRQ critical sections;
# the lifetime counters (netio: zc) stay at zero across TCP and UDP 2/2.
CONFIG_LEVERS := -DLWIP_TCPIP_CORE_LOCKING_INPUT=0 -DCONFIG_GMAC_RX_HWCSUM=1
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
