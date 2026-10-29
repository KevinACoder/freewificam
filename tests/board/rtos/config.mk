# The active RTOS build profile. Pick exactly one file from configs/; no
# per-build command-line switches.
CONFIG := rtl8xxxu-8188eu-net
include $(LINE)/configs/$(CONFIG).mk
