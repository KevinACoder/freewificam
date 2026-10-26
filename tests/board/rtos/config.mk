# The active RTOS build profile. Pick exactly one file from configs/; no
# per-build command-line switches.
CONFIG := firefly-v1-pcie
include $(LINE)/configs/$(CONFIG).mk
