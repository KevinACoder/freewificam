# K2-16 A arm: the transmit-stall probe at the boot OPP.
#
# The probe is the 1 ms timeline (head/tail/CUR_TX_DESC/TX_END/MTL queue
# status), the TX ring's OWN scan at give-up, one explicit tail-pointer write
# and a raw MMC dump. Running it at the boot OPP first is the probe's own
# control: the healthy 1440 cell must still come out at ~104 MiB/s with
# tx_drop=0, otherwise the instrument is what broke it and no high-OPP cell
# from this image counts.
#
# The registers it reads are status words only (never the read-clear
# DMA_CH0_STATUS / MTL_INT_STATUS): an observer that eats an interrupt would
# reproduce the symptom it is looking for.
include $(LINE)/configs/net-netio-raw-tx256.mk

CONFIG_LEVERS += -DCONFIG_GMAC_STALL_PROBE=1 -DCONFIG_NETIO_TX_STALL_PROBE=1
