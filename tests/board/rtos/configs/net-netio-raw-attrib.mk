# K2-07 Z7: Z1 + prof on, and the S2C cell now reports too.
#
# The S2C send path never had a prof report, so the TX buckets (memcpy, flush,
# interrupt) were only visible cumulatively. This arm is attribution only:
# nothing here is a throughput number (prof tax ~11%).
include $(LINE)/configs/net-netio-raw.mk

CONFIG_GMAC_PROF := 1
