# K2-16 A/B counter-arm: input on the receive thread (the pre-fix shape).
#
# The wired netio arm now leaves lwIP's input path on the tcpip thread. This
# arm puts it back on the driver's RX thread (with the one-lock-per-burst
# shape that only works there) so the comparison stays reproducible: on the
# same board the peer-to-board TCP cell runs at tens of kB/s here and at tens
# of MB/s on the default arm, while the UDP cells are unchanged.
include $(LINE)/configs/net-netio-raw-tx256.mk

CONFIG_LEVERS += -DLWIP_TCPIP_CORE_LOCKING_INPUT=1 -DCONFIG_GMAC_RX_BATCH=1
