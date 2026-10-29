# The active RTOS build profile. Pick exactly one file from configs/; no
# per-build command-line switches.
CONFIG := net-netio
include $(LINE)/configs/$(CONFIG).mk
