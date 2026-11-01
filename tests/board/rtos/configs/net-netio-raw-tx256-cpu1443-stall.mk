# K2-16 A arm: the frequency ladder's middle rung (request 1443 MHz).
#
# K2-15 H5 asks whether the stall frame count falls with the clock. Three
# points, three boots, same image otherwise: 816 (net-netio-raw-tx256-stall),
# 1443 (this one), 1992 (net-netio-raw-tx256-cpu1992-stall).
include $(LINE)/configs/net-netio-raw-tx256-stall.mk

CONFIG_LEVERS += -DCONFIG_NETIO_CPU_KHZ=1443000000
