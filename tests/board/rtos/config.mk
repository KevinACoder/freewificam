# The active RTOS build profile. Pick exactly one file from configs/; no
# per-build command-line switches.
CONFIG := wlan-obs
include $(LINE)/configs/$(CONFIG).mk
