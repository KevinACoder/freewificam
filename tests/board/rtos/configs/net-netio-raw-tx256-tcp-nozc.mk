# K2-16 B arm: the receive direction with the copying bridge instead of ZC.
#
# With a real TCP receive rate the zero-copy bridge trips lwIP's
# 'pbuf_free: p->ref > 0' assert within tens of milliseconds: the queued
# frames hold driver slots, and a slot appears to be released while a pbuf
# still references it (the driver's own slot_push carries a "better leaked
# than wrapped" guard, so the shape was known to be fragile). This arm keeps
# the input-path fix and turns the zero-copy hand-off off, which is the
# single-variable test of "the crash is the ZC slot lifecycle".
include $(LINE)/configs/net-netio-raw-tx256.mk

CONFIG_LEVERS += -DCONFIG_GMAC_RX_ZC=0
