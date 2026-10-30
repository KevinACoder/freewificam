# K2-07 Z4: Z1 (raw consumer, zero-copy RX, TX ring 256) + RX sweep in the ISR.
#
# rx_ru (DMA found no owned descriptor) has held at ~3,880/500k frames across
# every arm: the DMA outruns the thread-context re-arm when the consumer owns
# the sweep. With RX_BH=0 the sweep runs inside the interrupt, so the re-arm
# happens one interrupt earlier; the trade is sweep time in interrupt context.
include $(LINE)/configs/net-netio-raw-tx256.mk

CONFIG_LEVERS += -DCONFIG_GMAC_RX_BH=0
