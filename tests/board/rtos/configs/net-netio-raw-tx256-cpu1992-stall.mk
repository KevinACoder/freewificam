# K2-16 A arm: the transmit-stall probe with the CPU pinned to the failing OPP.
#
# This is the K2-14/K2-15 failing arm (request 1992 MHz, delivered 1.689 GHz)
# plus the probe. The cell should stop after 1000-26000 frames and leave the
# timeline, the OWN map and the kick result on the console.
include $(LINE)/configs/net-netio-raw-tx256-stall.mk

CONFIG_LEVERS += -DCONFIG_NETIO_CPU_KHZ=1992000
