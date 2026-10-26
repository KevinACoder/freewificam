# The active RTOS build profile. Pick exactly one file from configs/; no
# per-build command-line switches.
CONFIG := iwlwifi-7260-sjd
include $(LINE)/configs/$(CONFIG).mk
