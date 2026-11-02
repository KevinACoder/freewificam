# The active RTOS build profile. Pick exactly one file from configs/; no
# per-build command-line switches.
CONFIG := firefly-v2-sdio-mci
# The active file also selects the board (CONFIG_BOARD inside it): net-netio is
# the rk3568-itx wired instrument arm, firefly-v2-xmac the firefly v2 NIC arm,
# firefly-v2-sdio-mci the board's SDIO socket.
include $(LINE)/configs/$(CONFIG).mk
