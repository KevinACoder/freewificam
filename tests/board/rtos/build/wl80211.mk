# @file    wl80211.mk
# @brief   The wireless world: the imported mac80211/cfg80211 stack and, when
#          the profile asks for it, the vendored iwlwifi (AC 7260 / mvm) driver.
#
# This is a flag world, not a second component rule: the stack set is the one
# components/liblinux80211/sources.mk names, the driver set the one
# wireless-drivers/iwlwifi/sources.mk names.  The platform Makefile compiles
# them here because each needs its own Linux face:
#
#   stack   S0's face: the component's include roots + stack_config.h, the
#           three diagnostics that make a missing API a hard error
#   driver  the same face plus the driver's root and driver_config.h, and the
#           kernel build's warning relaxations (the driver is kernel code)
#   case    the stack face; a board case includes <net/mac80211.h> and calls
#           the public entry points only
#
# The stack must be selected by CONFIG_LIBLINUX80211_WIRELESS; the driver adds
# CONFIG_LIBLINUX80211_IWLWIFI.  A profile that selects neither links none of
# this (the component's kpi/bus images are unaffected).
#
# Link order is part of the contract: the stack's wireless units come before
# its mac80211 units (cfg80211's fs_initcall then mac80211's subsys_initcall),
# and inside the driver iwl-drv.c comes before mvm/ops.c (core then opmode).
# The `.lkpi_init` walk runs the section in link order; this list is where that
# order is decided.

# The board cases that compile in this world.  The platform Makefile's generic
# case list excludes them; this file owns them (defined before the include so
# both lists see the same answer).
WL80211_CASES ?= mac80211_c2 iwl7260_c3 iwl7260_c4 iwl7260_c5 iwl7260_c6 iwl7260_stab iwl7260_ap iwl7260_sta_wpa2 iwl7260_ap5g iwl7260_sta5g iwl7260_mon \
	rtl8189fs_sdio rtl8189fs_sta rtl8189fs_sta_wpa2 rtl8189fs_c6t rtl8189fs_ap

ifeq ($(CONFIG_LIBLINUX80211_WIRELESS),1)

WL80211_STACK_CFLAGS := $(S0_CFLAGS) -I$(RTL8189_DIR)/ports/rtos
WL80211_DRIVER_CFLAGS := $(S1_CFLAGS)
WL80211_CASE_CFLAGS := $(WL80211_STACK_CFLAGS)

# cfg80211 before mac80211: the first owns the wiphy/netdev registration the
# second calls into, and its fs_initcall entry must run before the late
# regulatory-db loader.
WL80211_STACK_SRCS := \
	$(filter $(LIB8)/stack/wireless/%,$(LIBLINUX80211_STACK_SRCS)) \
	$(filter $(LIB8)/stack/mac80211/%,$(LIBLINUX80211_STACK_SRCS))

# The compiled-in firmware unit is generated from the pinned GT blob by
# tools/rtos/fw-blobs.py; it is a build product (git-ignored) and joins the
# driver's flag world because it includes the registry header.
WL80211_FW_SRCS := $(LIB8)/fw/iwl7260_fw_blob.c

ifeq ($(CONFIG_LIBLINUX80211_IWLWIFI),1)
WL80211_DRIVER_SRCS := $(IWLWIFI_IMPL_SRCS) $(WL80211_FW_SRCS)
else
WL80211_DRIVER_SRCS :=
endif

# The 8189FTV vendor stack is a second driver world: its own include roots and
# its own feature face (wireless-drivers/rtl8189fs/driver_config.h), so it gets
# its own source list and compile rule instead of joining the iwlwifi one.
# Same wireless world, different driver.
ifeq ($(CONFIG_LIBLINUX80211_RTL8189FS),1)
WL80211_RTL8189FS_SRCS := $(RTL8189FS_IMPL_SRCS)
else
WL80211_RTL8189FS_SRCS :=
endif

WL80211_CASE_SRCS := $(foreach c,$(filter $(WL80211_CASES),$(CASES)),$(LINE)/app/cases/$(c).c)

# The C4/C5 bench seam, not a stack TU: it builds the real attribute bytes and
# dispatches nl80211's own handlers.  It compiles in the stack flag world but is
# not part of the S0/S1b source lists.  tx_dequeue_context.c is the same kind of
# port shim: a link-time --wrap around the stack's transmit dequeue, stating the
# port's deferred-context answer (see its header).
WL80211_PROBE_SRCS := $(LIB8)/linuxkpi_nl80211_probe.c \
	$(LIB8)/tx_dequeue_context.c \
	$(LIB8)/hostap_rx_wrap.c \
	$(LIB8)/hostap_nl80211.c \
	$(LIB8)/tx_health.c

# The wrap is what makes the shim a seam instead of a stack edit.  It is only
# meaningful when the stack is linked, and it applies to every reference
# (iwlwifi's iTXQ wake path is the one that matters).
WL80211_LDFLAGS := -Wl,--wrap=ieee80211_tx_dequeue \
	-Wl,--wrap=cfg80211_rx_mgmt_ext \
	-Wl,--wrap=ieee80211_tx_status_skb \
	-Wl,--wrap=ieee80211_stop_queue \
	-Wl,--wrap=ieee80211_wake_queue \
	-Wl,--wrap=iwl_trans_pcie_tx \
	-Wl,--wrap=iwl_trans_pcie_send_hcmd \
	-Wl,--wrap=iwl_mvm_send_cmd \
	-Wl,--wrap=iwl_read32 \
	-Wl,--wrap=iwl_write32 \
	-Wl,--wrap=iwl_trans_pci_interrupts \
	-Wl,--wrap=ieee80211_sta_ps_transition \
	-Wl,--wrap=ieee80211_sta_set_buffered

# The hostapd AP stack (third-party/wpa_supplicant-rtos) and its platform bottom: hostapd's
# own headers and OS abstraction, so it compiles in its own flag world.  The
# two files of this round that touch the Linux face (hostap_nl80211.c,
# hostap_rx_wrap.c) are in WL80211_PROBE_SRCS above, not here.
# The hostap include roots come BEFORE the global ones: the NXP tree's
# <fsl_os_abstraction.h> and <wlan.h> must resolve to this component's shims,
# not to the fsl_sdmmc OSA header the global -I list carries.
# The STA profile must force CONFIG_HOSTAPD 0; the AP profile keeps the AP
# entry.  The header is selected by the image's role, never by both at once.
ifeq ($(CONFIG_WPA_STA),1)
WL80211_HOSTAP_CONFIG_HDR := $(LIB8)/hostap/include/hostap_config_sta.h
else
WL80211_HOSTAP_CONFIG_HDR := $(LIB8)/hostap/include/hostap_config.h
endif

# The 8189 vendor tree and the imported hostapd/supplicant tree define the same
# global names, and they are NOT all the same code:
#
#   ap_free_sta      vendor rtw_ap.c (5 args) vs hostapd src/ap/sta_info.c (2)
#   omac1_aes_128    vendor rtw_security.c vs crypto/aes-omac1.c
#   hexstr2bin       vendor osdep_service.c vs utils/common.c
#   Te0 Td0 Td4s rcons   the standard AES T-tables (identical values)
#
# `--allow-multiple-definition` would bind hostapd's own `ap_free_sta(hapd,
# sta)` calls to the vendor's five-argument function: a crash on every station
# teardown, not a table alias.  Both trees stay byte-identical, so the rename is
# done on the vendor objects (COMPILE_C_RENAME in the Makefile) with this map,
# which moves the vendor's definitions and internal references together.  The
# link then needs no flag at all.
WL80211_RTL8189FS_RENAME := \
	--redefine-sym ap_free_sta=rtw_ap_free_sta \
	--redefine-sym omac1_aes_128=rtw_omac1_aes_128 \
	--redefine-sym hexstr2bin=rtw_hexstr2bin \
	--redefine-sym Te0=rtw_Te0 \
	--redefine-sym Td0=rtw_Td0 \
	--redefine-sym Td4s=rtw_Td4s \
	--redefine-sym rcons=rtw_rcons

WL80211_HOSTAP_CFLAGS := \
	-I$(LIB8)/hostap/include \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/src \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/src/utils \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/src/common \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/src/ap \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/src/drivers \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/src/crypto \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/src/eap_common \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/src/eap_server \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/src/eapol_auth \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/src/rsn_supp \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/src/l2_packet \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/hostapd \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/wpa_supplicant \
	-I$(THIRD_PARTY)/wpa_supplicant-rtos/freertos/src \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-Wno-unused-but-set-variable -Wno-type-limits -Wno-address-of-packed-member \
	-Wno-override-init -Wno-implicit-fallthrough -Wno-unused-variable \
	-Wno-unused-function \
	$(VENDORED_CFLAGS) \
	-include $(WL80211_HOSTAP_CONFIG_HDR) \
	-include $(LIB8)/hostap/include/hostap_printf_bind.h

WL80211_HOSTAP_SRCS := $(LIBLINUX80211_HOSTAP_SRCS)

# wpa_supplicant/events.c is the one hostap TU that carries floating point (the
# STA roam heuristic, unreachable in the AP role): it compiles without the
# kernel flag world's -mgeneral-regs-only so the rest of the stack stays
# integer-only.  The flag is what the port's context switch can guarantee.
WL80211_HOSTAP_FP_SRCS := $(THIRD_PARTY)/wpa_supplicant-rtos/wpa_supplicant/events.c
WL80211_HOSTAP_FP_CFLAGS := $(filter-out -mgeneral-regs-only,$(WL80211_HOSTAP_CFLAGS))

WL80211_ALL_SRCS := $(WL80211_STACK_SRCS) $(WL80211_DRIVER_SRCS) \
	$(WL80211_RTL8189FS_SRCS) $(WL80211_CASE_SRCS) $(WL80211_PROBE_SRCS) \
	$(WL80211_HOSTAP_SRCS) $(WL80211_HOSTAP_FP_SRCS)

endif

