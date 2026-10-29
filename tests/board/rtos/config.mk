# The active RTOS build profile. Pick exactly one file from configs/; no
# per-build command-line switches.
CONFIG := metal-8188eu
include $(LINE)/configs/$(CONFIG).mk
