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

CONFIG_CASES := net_pps
