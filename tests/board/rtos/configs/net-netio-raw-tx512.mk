# K2-07 Z5: Z1 + a 512-descriptor TX ring.
#
# At ring 64 the sender finds the ring full on ~73% of its calls (tx_waits /
# tx_frames), so the CPU parks on the completion semaphore almost every frame.
# A deeper ring lets the CPU stay ahead of the wire instead of rendezvousing
# with every completion: the discriminator is the client's Rx rate and
# tx_waits falling, not tx_occ_max.
include $(LINE)/configs/net-netio-raw.mk

CONFIG_LEVERS += -DCONFIG_GMAC_TX_RING=512
