# K2-16 B arm: the independent TCP instrument (iperf2 client -> lwiperf server).
#
# The netio server and its client are one instrument pair, so "the c2s phase
# stalls" cannot by itself separate a stack/driver defect from the netio
# protocol or its client. This arm builds the plain `net` case with a single
# long lwiperf downlink window (NET_PHASE_PLAN=2) and no board-initiated
# session, so a client that does not speak netio at all - iperf2 `-c <board>
# -t 10` - drives the same stack and the same driver.
#
# The driver/stack levers are the netio arm's (zero-copy RX included), so the
# two instruments see the same receive path.
include $(LINE)/configs/net-netio-raw.mk

CONFIG_CASES := net
CONFIG_LEVERS += -DNET_PHASE_PLAN=2
