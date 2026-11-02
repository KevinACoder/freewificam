# K2-16 B arm: the TCP receive-direction probe.
#
# The peer-to-board TCP direction collapses (41 kB/s, the c2s phase ends after
# ~256 packets) while the board's transmit direction in the same cell is
# healthy. This arm adds the sampler that says which layer stalls: the PCB
# window, the socket read path, lwIP's tcp/MIB2 counters, the driver's receive
# counters and the quiet ring snapshot, once per 250 ms into RAM and dumped
# after the phase.
#
# CONFIG_GMAC_TCP_PROBE also turns MIB2_STATS on in third-party/lwip's
# lwipopts.h (retransmits, in/out segments); CONFIG_NETIO_TCP_PROBE arms the
# case-side sampler. Instrumented arm: its throughput is not the acceptance
# number, the clean arm re-runs the same cell for that.
include $(LINE)/configs/net-netio-raw-tx256.mk

CONFIG_LEVERS += -DCONFIG_GMAC_TCP_PROBE=1 -DCONFIG_NETIO_TCP_PROBE=1
