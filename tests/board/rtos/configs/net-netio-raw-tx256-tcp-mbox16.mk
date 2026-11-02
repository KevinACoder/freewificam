# K2-16 B arm: the TCP socket receive mailbox, sized.
#
# The receive direction collapses while the window stays wide open and the
# peer retransmits every segment after its ~200 ms RTO: the stack's own
# counters say the segments are refused (tcp.drop / mib2.tcpinerrs), and the
# wire says the ACK for each one leaves ~250 ms later - the fast-timer
# cadence, which is where lwIP re-offers a pbuf the application refused.
#
# lwIP's own default for the TCP socket mailbox is 0 and this port rounds that
# up to a single slot (third-party/lwip/ports/sys_arch.c), so a burst of
# segments overruns it as soon as the reader is not parked in recv() at that
# instant. 16 matches the UDP receive mailbox and MEMP_NUM_NETBUF.
include $(LINE)/configs/net-netio-raw-tx256.mk

CONFIG_LEVERS += -DDEFAULT_TCP_RECVMBOX_SIZE=16
# Measured 2026-11-02 on the pre-fix base (input on the RX thread). On the
# current base this arm is the fix plus this mailbox depth: a record of the
# scaling that identified the refusal path (0.07/0.51 MB/s at 16/64 slots),
# not an arm to re-run as-is. 96 hit lwIP's pbuf assert (zero-copy slot
# pool), so it is kept only as that negative result.
