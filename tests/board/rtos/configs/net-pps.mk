# net_pps arm: the pps-bound receiver (64-byte UDP, serial-gated window).
#
# The data path is the K2-07 best-known receive configuration (raw UDP PCB,
# zero-copy RX, 256-descriptor TX ring); the difference from the netio arm is
# the case, not the levers - a pps number measured on other levers would not
# be comparable with the throughput arm's cells.
#
# The case list is one case, for the same reason the netio arm has one: a boot
# that also ran another network case would answer two questions at once.
include $(LINE)/configs/net-netio-raw-tx256.mk

# K2-12/13 (pps bound) were measured with the in-RX-thread input shape; the
# T0 numbers in runs/plat-01/K2-12|13 belong to THIS lever, and the netio
# default moved to the tcpip-thread shape on 2026-11-02. Keep the pps arm
# pinned so its ledger stays reproducible; an input-shape comparison for
# 64 B pps is a separate experiment that has not been run.
CONFIG_LEVERS := $(filter-out -DLWIP_TCPIP_CORE_LOCKING_INPUT=0,$(CONFIG_LEVERS)) \
	-DLWIP_TCPIP_CORE_LOCKING_INPUT=1 -DCONFIG_GMAC_RX_BATCH=1

CONFIG_CASES := net_pps
