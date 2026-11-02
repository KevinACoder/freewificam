# K2-06 arm: ZC + TX checksum offload + the K2-03 H2 priority swap.
# Producer (netio-rx) one step above the UDP consumer (netio-udp-rx): the
# discriminating arm for "how much of feed is the consumer preempting the
# drain thread". One axis against net-netio-zc-txc.mk; everything else
# identical.
include $(LINE)/configs/net-netio-zc-txc.mk

CONFIG_LEVERS += -DMN_RX_PRIO=osPriorityAboveNormal -DUDP_RX_PRIO=osPriorityNormal
