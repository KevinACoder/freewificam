# Query the built image's source lists without compiling anything.
#
#   make -C tests/board/rtos/build -f ../../../tools/build/query.mk \
#        PROFILE=<profile> MODULE=<name> SRCS
#
# MODULE names are the component boundaries, not the profiles: `image` is the
# full link list, `net80211`, `linuxkpi`, each driver's implementation, and
# `net80211-ko` is the net80211 subset the kernel module links.  Paths are
# printed relative to the repository root, one per line; ABS=1 keeps them
# absolute.  This is the single query point gen_sources.py and the linux-ko
# build use, so a source list never has to be copied into a second language.

REPO := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
QUERY_BUILD := $(REPO)/tests/board/rtos/build

PROFILE ?=

ifeq ($(strip $(PROFILE)),)
# Native mode: no Kconfig, no Python, no generated config.  This is what the
# kernel-module build uses; it needs only the net80211 component, whose core
# gate is the one switch below.  The list still comes from the component's
# sources.mk, so it cannot drift from the RTOS image.
ROOT := $(REPO)
CONFIG_NET80211 := 1
CONFIG_LINUXKPI_NET80211 := 1
CONFIG_WLAN_RX_BA := 0
include $(REPO)/tools/build/sources.mk
$(eval $(call component_sources,$(REPO)/components/net80211,core,NET80211_CORE))
$(eval $(call component_sources,$(REPO)/components/net80211,nport,NET80211_NPORT))
$(eval $(call component_sources,$(REPO)/components/net80211,bridge,NET80211_BRIDGE))
$(eval $(call component_sources,$(REPO)/components/net80211,kpi,NET80211_KPI))
$(eval $(call component_sources,$(REPO)/components/net80211,rxba,NET80211_RXBA))
NET80211_SRCS := $(NET80211_CORE_SRCS) $(NET80211_NPORT_SRCS) \
	$(NET80211_BRIDGE_SRCS)
else
# Profile mode: include the build Makefile from its own directory so ROOT,
# LINE and the fragment includes resolve as if the caller had built there.
CURDIR := $(QUERY_BUILD)
include $(QUERY_BUILD)/Makefile
endif

.DEFAULT_GOAL := SRCS

# The net80211-ko subset: the same core and bridge units, but the port layer
# without osal_memory_target.c and in the order the module has always linked
# (mbuf, ifnet, cmsis2).  The kernel's OSAL owns memory there.
MODULE_image = $(ALL_SRCS) $(WLAN_ALL_SRCS)
MODULE_image-wlan = $(WLAN_ALL_SRCS)
MODULE_net80211 = $(NET80211_SRCS)
MODULE_net80211-core = $(NET80211_CORE_SRCS)
MODULE_net80211-nport = $(NET80211_NPORT_SRCS)
MODULE_net80211-bridge = $(NET80211_BRIDGE_SRCS)
MODULE_net80211-kpi = $(NET80211_KPI_SRCS)
MODULE_net80211-rxba = $(NET80211_RXBA_SRCS)
MODULE_net80211-ko = $(NET80211_CORE_SRCS) \
	$(filter %/ports/net/mbuf.c,$(NET80211_NPORT_SRCS)) \
	$(filter %/ports/net/ifnet.c,$(NET80211_NPORT_SRCS)) \
	$(filter %/ports/osal/cmsis2/osal_cmsis2.c,$(NET80211_NPORT_SRCS)) \
	$(NET80211_BRIDGE_SRCS)
MODULE_linuxkpi = $(LINUXKPI_SRCS)
MODULE_linuxkpi-cherry = $(LINUXKPI_CHERRY_SRCS)
MODULE_linuxkpi-wlan = $(LINUXKPI_WLAN_SRCS)
MODULE_rtw89 = $(RTW89_DRV_SRCS)
MODULE_rtl8xxxu = $(RTL8XXXU_DRV_SRCS)
MODULE_rtw88 = $(RTW88_DRV_SRCS)
MODULE_iwlwifi = $(IWL_DRV_SRCS)
MODULE_rtl8189fs = $(WLAN_DRV_SRCS)

QUERY_PATHS = $(if $(ABS),$(MODULE_$(MODULE)),\
	$(patsubst $(ROOT)/%,%,$(MODULE_$(MODULE))))

SRCS:
	@: $(if $(MODULE),,$(error MODULE is required; use MODULE=<name> SRCS))
	@: $(if $(MODULE_$(MODULE)),,$(error unknown MODULE '$(MODULE)'))
	@printf '%s\n' $(QUERY_PATHS)
