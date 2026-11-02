# The active RTOS build profile. Pick exactly one file from configs/; no
# per-build command-line switches.
CONFIG := firefly-v2-sdio-mci
include $(LINE)/configs/$(CONFIG).mk
