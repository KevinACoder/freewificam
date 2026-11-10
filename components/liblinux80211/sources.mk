# liblinux80211 component sources.
#
# The component owns one sources.mk, in the repository's kbuild shape.  The
# groups are the flag worlds the platform Makefile compiles separately:
#
#   kpi       the kernel-API bodies (one world, the Linux face)
#   bus-pci   the PCI engine (Linux semantics over the backend seam)
#   bus-sdio  the SDIO engine and the backend registration
#   port-pci  the RK3568 adapter (its own world: board drivers + arch)
#
# The stack group is the imported Linux wireless stack: it is compiled only by
# the S0 inventory target (tools/build/rtos.mk s0-compile), never linked into an
# image by this build; the link comes with S1.  It is listed here because the
# component owns exactly one sources.mk.
#
# The file names are the imported ones on purpose: the Linux face reaches the
# kernel-API core with `#include "../../linuxkpi_core.h"`, a path that only
# resolves with the core header beside `include/`.

ifeq ($(SRC_TAG),kpi)
obj-$(CONFIG_LIBLINUX80211) += \
	linuxkpi_core.c \
	linuxkpi_compat.c \
	linuxkpi_osdep.c \
	linuxkpi_netdev.c \
	linuxkpi_platform_target.c \
	linuxkpi_containers.c \
	linuxkpi_device.c \
	linuxkpi_rfkill.c \
	linuxkpi_netlink.c \
	linuxkpi_util.c \
	linuxkpi_crypto.c \
	fw_registry.c \
	net_egress.c \
	skbuff/skbuff_pool.c
endif

ifeq ($(SRC_TAG),bus-pci)
obj-$(CONFIG_LIBLINUX80211_PCI) += bus/pci.c
endif

ifeq ($(SRC_TAG),bus-sdio)
obj-$(CONFIG_LIBLINUX80211_SDIO) += bus/sdio.c bus/sdio_be.c
endif

ifeq ($(SRC_TAG),port-pci)
port-obj-$(CONFIG_LIBLINUX80211_PCI) += ports/pci-rk3568/adapter.c
endif

ifeq ($(SRC_TAG),stack)
obj-$(CONFIG_LIBLINUX80211) += \
	stack/mac80211/main.c \
	stack/mac80211/status.c \
	stack/mac80211/driver-ops.c \
	stack/mac80211/sta_info.c \
	stack/mac80211/wep.c \
	stack/mac80211/aead_api.c \
	stack/mac80211/wpa.c \
	stack/mac80211/scan.c \
	stack/mac80211/offchannel.c \
	stack/mac80211/ht.c \
	stack/mac80211/agg-tx.c \
	stack/mac80211/agg-rx.c \
	stack/mac80211/vht.c \
	stack/mac80211/he.c \
	stack/mac80211/s1g.c \
	stack/mac80211/ibss.c \
	stack/mac80211/iface.c \
	stack/mac80211/link.c \
	stack/mac80211/rate.c \
	stack/mac80211/tkip.c \
	stack/mac80211/aes_cmac.c \
	stack/mac80211/aes_gmac.c \
	stack/mac80211/fils_aead.c \
	stack/mac80211/cfg.c \
	stack/mac80211/ethtool.c \
	stack/mac80211/rx.c \
	stack/mac80211/spectmgmt.c \
	stack/mac80211/tx.c \
	stack/mac80211/key.c \
	stack/mac80211/util.c \
	stack/mac80211/parse.c \
	stack/mac80211/wme.c \
	stack/mac80211/chan.c \
	stack/mac80211/ap.c \
	stack/mac80211/trace.c \
	stack/mac80211/mlme.c \
	stack/mac80211/tdls.c \
	stack/mac80211/ocb.c \
	stack/mac80211/airtime.c \
	stack/mac80211/eht.c \
	stack/mac80211/uhr.c \
	stack/mac80211/nan.c \
	stack/mac80211/wbrf.c \
	stack/mac80211/rc80211_minstrel_ht.c \
	stack/wireless/core.c \
	stack/wireless/sysfs.c \
	stack/wireless/radiotap.c \
	stack/wireless/util.c \
	stack/wireless/reg.c \
	stack/wireless/scan.c \
	stack/wireless/nl80211.c \
	stack/wireless/mlme.c \
	stack/wireless/ibss.c \
	stack/wireless/sme.c \
	stack/wireless/chan.c \
	stack/wireless/ethtool.c \
	stack/wireless/mesh.c \
	stack/wireless/ap.c \
	stack/wireless/trace.c \
	stack/wireless/ocb.c \
	stack/wireless/michael-mic.c \
	stack/wireless/pmsr.c
endif
