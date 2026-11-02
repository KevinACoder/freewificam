# K2-14 P1 (2026-11-01): the same arm running at the K2-07 clock.
#
# Everything about the uplink cell is 8-30x slower than the K2-07 reference
# (12.57 us per frame, 108.09 MiB/s) while the downlink cell in the same boot
# still matches it frame for frame. The one ruler difference between the two
# rounds is the OPP the case pins: K2-07 ran at the boot clock (821 MHz
# measured, 12.57 us = the wire's own 12.2), today's arm asks for 1.992 GHz
# and measures ~1.69 GHz.  K2-12 measured nominal 816 -> actual 816 MHz, so
# 816000 reproduces the K2-07 ruler inside the same tree with one bit of the
# ruler moved and nothing else.
include $(LINE)/configs/net-netio-raw-tx256.mk

CONFIG_LEVERS += -DCONFIG_NETIO_CPU_KHZ=816000
