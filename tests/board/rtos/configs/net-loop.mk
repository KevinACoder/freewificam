# net-loop arm: the lwIP stack's per-packet cost with the driver removed.
#
# K2-04 W1. The question this arm answers is one-sided: the GMAC round puts
# 11.4 us/frame on the feed segment, and before that can be called "the stack"
# the driver, its DMA rings and the staging copy have to leave the equation.
# lwIP 2.2 creates its loopback interface in netif_init, so a datagram sent to
# 127.0.0.1 walks pbuf allocation, ip4 output, netif_loop_output, the tcpip
# thread, ip4 input, udp demux and the receive mailbox with no GMAC involved.
#
# The measurement is a one-thread ping-pong, so it carries the two thread
# switches the loopback path costs; the ledger records it as an upper bound.
# An upper bound below the GMAC feed figure excludes the stack as the cause; an
# upper bound above it says nothing about the driver.
#
# No GMAC is brought up and no PHY is probed: a boot of this arm does not care
# whether the cable is in.
CONFIG_BACKEND := freertos
CONFIG_CASES := net_loop
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
