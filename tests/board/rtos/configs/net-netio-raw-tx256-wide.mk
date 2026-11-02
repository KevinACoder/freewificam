# K2-07 Z10: Z5 (raw+ZC+TXCSM+TX ring 256) plus CONFIG_GMAC_SWEEP_WIDE.
#
# Z9's 64-byte flood exposed the sweep as the dominant per-frame cost at high
# frame rates: 3.7 of 9.0 us/frame under prof, i.e. ~37 us for every
# whole-ring walk. SWEEP_WIDE turns the walk into one pure-load pass over the
# non-cacheable status words (the lever exists in dwc_eqos.c, 2026-10-19) so
# the core can keep several non-cacheable misses in flight instead of
# serialising a load per slot. Single axis: nothing else changes from Z5.
include $(LINE)/configs/net-netio-raw-tx256.mk

CONFIG_GMAC_PROF := 0
CONFIG_LEVERS += -DCONFIG_GMAC_SWEEP_WIDE=1
