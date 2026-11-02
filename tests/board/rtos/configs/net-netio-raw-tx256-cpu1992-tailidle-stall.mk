# K2-16 A arm: the high-OPP stall with the tail pointer written only on progress.
#
# The full-ring path calls eqos_tx_complete once per spin and that function
# rewrites DMA_CH0_TX_END_ADDR whenever the ring is non-empty - thousands of
# identical writes per frame, from a tight loop, while the channel is mid
# fetch. This arm stops the redundant writes (the register only moves when a
# descriptor was actually reclaimed), which is the discriminating test for
# "the repeated tail write leaves the channel believing it is caught up".
include $(LINE)/configs/net-netio-raw-tx256-cpu1992-stall.mk

CONFIG_LEVERS += -DCONFIG_GMAC_TX_TAIL_IDLE=1
