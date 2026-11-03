# net80211: the NetBSD MLME core, its port over the RTOS services, and the
# wlan_bridge seam.  Included once for the BSD flag world and once for the
# linuxkpi world (the seam adapter is compiled with the Linux face).
#
# The four wireless lines used to repeat this list in their own fragment; the
# list lives here now, in the order the board-proven images linked it.

ifeq ($(SRC_TAG),core)
obj-$(CONFIG_NET80211) += \
	net80211/ieee80211.c \
	net80211/ieee80211_proto.c \
	net80211/ieee80211_node.c \
	net80211/ieee80211_output.c \
	net80211/ieee80211_input.c \
	net80211/ieee80211_amrr.c \
	net80211/ieee80211_netbsd.c \
	net80211/ieee80211_crypto.c \
	net80211/ieee80211_crypto_none.c \
	net80211/ieee80211_crypto_ccmp.c \
	crypto/aes/aes_bear.c \
	crypto/aes/aes_ct.c \
	crypto/aes/aes_ct_enc.c \
	crypto/aes/aes_ct_dec.c \
	crypto/aes/aes_ccm.c \
	crypto/aes/aes_ccm_mbuf.c \
	port/aes_impl_compat.c

endif

ifeq ($(SRC_TAG),nport)
obj-$(CONFIG_NET80211) += \
	ports/osal/cmsis2/osal_memory_target.c \
	ports/osal/cmsis2/osal_cmsis2.c \
	ports/net/mbuf.c \
	ports/net/ifnet.c
endif

ifeq ($(SRC_TAG),bridge)
obj-$(CONFIG_NET80211) += ports/bridge/wlan_bridge.c
endif

ifeq ($(SRC_TAG),kpi)
# The seam adapter runs in the linuxkpi flag world.
obj-$(CONFIG_LINUXKPI_NET80211) += ports/bridge/wlan_seam_adapter.c
endif

ifeq ($(SRC_TAG),rxba)
# The downlink block-ack receive side (RTOS-02) is a lever, so it keeps its
# own symbol and its own place at the end of the seam's group.
obj-$(CONFIG_WLAN_RX_BA) += ports/bridge/wlan_rx_ba.c
endif
