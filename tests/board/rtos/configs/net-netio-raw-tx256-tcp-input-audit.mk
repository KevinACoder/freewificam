# K2-16 B diagnostic arm: the input fix with the zero-copy lifetime auditor.
#
# The input fix (input on the tcpip thread) lifts the peer-to-board TCP cell
# from 33 kB/s to ~40 MB/s, but at that rate lwIP's 'pbuf_free: p->ref > 0'
# assert fires. This arm adds three counters to the zero-copy hand-off -
# wrap over a live wrapper, free of a dead wrapper, and a ready-ring handle
# dequeued while not live - and prints them every 250 ms and again from the
# assert itself, so one boot says which owner lost track.
include $(LINE)/configs/net-netio-raw-tx256-tcp-input.mk

CONFIG_LEVERS += -DCONFIG_GMAC_TCP_PROBE=1 -DCONFIG_NETIO_TCP_PROBE=1
