# The active RTOS build profile. Pick exactly one file from configs/; no
# per-build command-line switches.
CONFIG := firefly-v2-xmac
include $(LINE)/configs/$(CONFIG).mk
