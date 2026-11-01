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

# The receive path defaults are the K2-03 A6 set, moved from a one-off command
# line into the arm so every future measurement starts from the best known
# configuration instead of re-deriving it:
#   - LWIP_TCPIP_CORE_LOCKING_INPUT: the driver's RX thread runs the stack's
#     input path itself; without it every frame pays a mailbox post and two
#     thread switches before anything looks at the headers.
#   - CONFIG_GMAC_RX_BATCH: the poll drains a burst through one core-lock
#     acquisition instead of one per frame.
#   - CONFIG_GMAC_RX_HWCSUM: the MAC validates the IP/L4 checksum; the stack's
#     software pass over every frame was measured at ~18% of the feed segment.
# Measured together on 2026-10-30: 51.6 -> 70.0 MB/s, feed 19.3 -> 11.4 us.
#
# K2-16 (2026-11-02): this shape is also why the peer-to-board TCP direction
# crawls. The whole receive burst runs in the RX thread, so the socket reader
# cannot take the core lock until it ends; the socket's receive mailbox (one
# slot) overruns, lwIP refuses the rest (tcp.drop / mib2.tcpinerrs) and only
# re-offers them on the 250 ms fast timer. Moving input back to the tcpip
# thread measured 33.0 kB/s -> 40.8 MB/s on the same arm with the UDP cells
# unchanged (114.0 MB/s, 0% loss), but that arm then trips lwIP's
# 'pbuf_free: p->ref > 0' assert through the zero-copy receive bridge within
# tens of milliseconds of a real TCP rate, and the copying bridge (no ZC)
# costs the UDP direction 114 -> 84 MB/s at 26% loss. Not shipped as the
# default until the ZC slot lifecycle is fixed; see
# configs/net-netio-raw-tx256-tcp-input.mk and ...-tcp-nozc.mk.
CONFIG_LEVERS := -DLWIP_TCPIP_CORE_LOCKING_INPUT=1 -DCONFIG_GMAC_RX_BATCH=1 \
	-DCONFIG_GMAC_RX_HWCSUM=1
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
