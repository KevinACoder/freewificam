# K2-16 B arm: the TCP socket receive mailbox sized for a whole window.
#
# Same single variable as net-netio-raw-tx256-tcp-mbox16, one step further:
# lwIP's TCP socket mailbox defaults to 0 (this port rounds that to one slot)
# and a burst of segments overruns it; every overrun sets the "refused data"
# state, in which tcp_input drops every further segment (tcp.drop /
# mib2.tcpinerrs) and the pbuf is only re-offered by the 250 ms fast timer.
#
# 16 slots helped (70 -> 163 kB/s) but a slow-start burst is bigger than
# that: the peer may put a whole TCP_WND (131072 B / 1460 = 90 segments) on
# the wire before the reader gets a turn, and with core-locking input the
# reader cannot run while the receive thread is inside the burst at all. 128
# covers a full window with slack. With CONFIG_GMAC_RX_ZC the queued frames
# are the driver's own buffers, so depth here does not spend the pbuf pool.
include $(LINE)/configs/net-netio-raw-tx256.mk

CONFIG_LEVERS += -DDEFAULT_TCP_RECVMBOX_SIZE=96
# Measured 2026-11-02 on the pre-fix base (input on the RX thread). On the
# current base this arm is the fix plus this mailbox depth: a record of the
# scaling that identified the refusal path (0.07/0.51 MB/s at 16/64 slots),
# not an arm to re-run as-is. 96 hit lwIP's pbuf assert (zero-copy slot
# pool), so it is kept only as that negative result.
