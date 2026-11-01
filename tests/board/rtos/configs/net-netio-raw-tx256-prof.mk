# K2-14 P1 (2026-11-01): the same arm with the driver's service-time
# instrument on.
#
# The uplink cell costs ~100-230 us per frame today against K2-07's 12.57 us,
# and the ring-full path is not where it goes (the ring is full on only ~2% of
# the sends). tx_total_ticks answers "is the time inside dwc_eqos_send_frame
# at all", and the memcpy/flush pair splits it further; anything left over is
# outside the driver (lwIP, pbuf, scheduling) and has to be measured there.
# The instrument's tax is known (11.3% on the A6 arm, AGENTS 1.2), so this
# arm's numbers are for attribution only - never as a throughput baseline.
include $(LINE)/configs/net-netio-raw-tx256.mk

CONFIG_LEVERS += -DCONFIG_GMAC_PROF=1
