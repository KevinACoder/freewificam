# K2-14: the failing arm, kept as a named configuration.
#
# The uplink cell only fails when the case pins the CPU to the 1.992 GHz OPP
# (SCMI readback 1992 MHz, PMCCNTR 1.69 GHz). Same tree, same client, same
# bootless A/B: this arm aborts with tx_drop>0, the 816 MHz arm of the same
# tree delivers 104.5 MB/s at zero loss. Keeping the arm here means the
# comparison is a named pair of files rather than a command-line edit.
include $(LINE)/configs/net-netio-raw-tx256.mk

CONFIG_LEVERS += -DCONFIG_NETIO_CPU_KHZ=1992000
