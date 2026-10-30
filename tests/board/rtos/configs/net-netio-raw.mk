# K2-07 Z1: the zero-copy receive arm plus the raw-API UDP consumer.
#
# Why: the socket data plane (recvmbox 16 deep + a second thread + a 1440 B
# copy) is where the 2026-10-30 runs lost frames invisibly (recv_udp's
# mbox-full branch frees the pbuf without touching any lwIP counter). The raw
# PCB's recv callback counts and frees on the RX thread's input call, so the
# loss window shrinks to the driver's own counters, which the `netio: win`
# line diffs per cell. Everything else identical to net-netio-zc-txc.
include $(LINE)/configs/net-netio-zc-txc.mk

CONFIG_LEVERS += -DCONFIG_NETIO_RAW_UDP=1
