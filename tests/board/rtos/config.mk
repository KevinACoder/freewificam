# The active RTOS build profile. Pick exactly one file from configs/; no
# per-build command-line switches.
CONFIG := uvc
include $(LINE)/configs/$(CONFIG).mk
