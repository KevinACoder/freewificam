# K2-16 A arm: the high-OPP stall with the transmit reclaim serialised.
#
# eqos_tx_complete is entered from the interrupt, from the sender's full-ring
# spin and from the 100 ms watchdog. eqos_lock masks the port line but not the
# tick, so a thread inside the reclaim walk can be preempted and the two then
# read-modify-write tx_tail; the preempted one's stale value moves the tail
# backwards, the software then believes descriptors are in flight that the
# channel has already passed, and the two deadlock at a full ring with an
# empty MTL queue. The receive sweep has carried a test-and-set guard since
# 2026-10-19 for exactly this reason; the transmit sweep never got one.
include $(LINE)/configs/net-netio-raw-tx256-cpu1992-stall.mk

CONFIG_LEVERS += -DCONFIG_GMAC_TX_SWEEP_GUARD=1
