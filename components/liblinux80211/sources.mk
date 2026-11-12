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

# ---------------------------------------------------------------------------
# hostap: the imported hostapd AP stack (third-party/wpa_supplicant-rtos, from the NXP
# wpa_supplicant-rtos port) and this port's platform bottom.  These files are
# not the Linux face's world: they carry hostapd's own headers and a different
# OS abstraction (OSA), so they get their own compile group (see wl80211.mk).
#
# The list is the transferable part of the NXP FreeRTOS/Zephyr CONFIG_HOSTAPD
# source set: the supplicant core the entry initialises, the hostapd AP MLME,
# the NXP FreeRTOS os/eloop/l2 files, the internal crypto (no mbedtls) and the
# four first-party platform files.  supp_api.c is not compiled: its NXP
# wlan_network API needs the MCUXpresso wlan.h, which is not part of the
# imported tree (hostap_ap.c applies the same open-AP configuration instead).
#
# The two port-world files of this round (hostap_nl80211.c, hostap_rx_wrap.c)
# live in the Linux face's world and are listed by wl80211.mk instead.
HOSTAP := $(THIRD_PARTY)/wpa_supplicant-rtos

ifeq ($(SRC_TAG),hostap)
obj-$(CONFIG_HOSTAP_AP) += \
	hostap/osa_lkpi.c \
	hostap/netif_lkpi.c \
	hostap/driver_lkpi.c \
	hostap/hostap_ap.c \
	$(HOSTAP)/src/common/wpa_common.c \
	$(HOSTAP)/src/common/ieee802_11_common.c \
	$(HOSTAP)/src/common/hw_features_common.c \
	$(HOSTAP)/src/drivers/driver_common.c \
	$(HOSTAP)/src/drivers/drivers.c \
	$(HOSTAP)/src/l2_packet/l2_packet_none.c \
	$(HOSTAP)/src/utils/base64.c \
	$(HOSTAP)/src/utils/common.c \
	$(HOSTAP)/src/utils/wpabuf.c \
	$(HOSTAP)/src/utils/bitfield.c \
	$(HOSTAP)/src/utils/eloop_freertos.c \
	$(HOSTAP)/src/utils/os_freertos.c \
	$(HOSTAP)/src/utils/wpa_debug.c \
	$(HOSTAP)/src/utils/block_alloc.c \
	$(HOSTAP)/src/utils/crc32.c \
	$(HOSTAP)/src/utils/ip_addr.c \
	$(HOSTAP)/wpa_supplicant/ap.c \
	$(HOSTAP)/wpa_supplicant/config.c \
	$(HOSTAP)/wpa_supplicant/config_none.c \
	$(HOSTAP)/wpa_supplicant/notify.c \
	$(HOSTAP)/wpa_supplicant/bss.c \
	$(HOSTAP)/wpa_supplicant/eap_register.c \
	$(HOSTAP)/wpa_supplicant/wpa_supplicant.c \
	$(HOSTAP)/wpa_supplicant/bssid_ignore.c \
	$(HOSTAP)/wpa_supplicant/wpas_glue.c \
	$(HOSTAP)/wpa_supplicant/scan.c \
	$(HOSTAP)/wpa_supplicant/mbo.c \
	$(HOSTAP)/wpa_supplicant/rrm.c \
	$(HOSTAP)/wpa_supplicant/wmm_ac.c \
	$(HOSTAP)/wpa_supplicant/robust_av.c \
	$(HOSTAP)/wpa_supplicant/op_classes.c \
	$(HOSTAP)/src/ap/ap_config.c \
	$(HOSTAP)/src/ap/ap_drv_ops.c \
	$(HOSTAP)/src/ap/ap_list.c \
	$(HOSTAP)/src/ap/ap_mlme.c \
	$(HOSTAP)/src/ap/authsrv.c \
	$(HOSTAP)/src/ap/beacon.c \
	$(HOSTAP)/src/ap/bss_load.c \
	$(HOSTAP)/src/ap/dfs.c \
	$(HOSTAP)/src/ap/drv_callbacks.c \
	$(HOSTAP)/src/ap/eap_user_db.c \
	$(HOSTAP)/src/ap/hostapd.c \
	$(HOSTAP)/src/ap/hw_features.c \
	$(HOSTAP)/src/ap/ieee802_11.c \
	$(HOSTAP)/src/ap/ieee802_11_auth.c \
	$(HOSTAP)/src/ap/ieee802_11_ht.c \
	$(HOSTAP)/src/ap/ieee802_11_shared.c \
	$(HOSTAP)/src/ap/ieee802_1x.c \
	$(HOSTAP)/src/ap/neighbor_db.c \
	$(HOSTAP)/src/ap/p2p_hostapd.c \
	$(HOSTAP)/src/ap/pmksa_cache_auth.c \
	$(HOSTAP)/src/ap/preauth_auth.c \
	$(HOSTAP)/src/ap/rrm.c \
	$(HOSTAP)/src/ap/sta_info.c \
	$(HOSTAP)/src/ap/tkip_countermeasures.c \
	$(HOSTAP)/src/ap/utils.c \
	$(HOSTAP)/src/ap/wmm.c \
	$(HOSTAP)/src/ap/wpa_auth.c \
	$(HOSTAP)/src/ap/wpa_auth_ie.c \
	$(HOSTAP)/src/ap/wpa_auth_glue.c \
	$(HOSTAP)/src/ap/wpa_auth_ft.c \
	$(HOSTAP)/src/eap_common/eap_common.c \
	$(HOSTAP)/src/eap_server/eap_server.c \
	$(HOSTAP)/src/eap_server/eap_server_identity.c \
	$(HOSTAP)/src/eap_server/eap_server_methods.c \
	$(HOSTAP)/src/eapol_auth/eapol_auth_sm.c \
	$(HOSTAP)/src/rsn_supp/wpa.c \
	$(HOSTAP)/src/rsn_supp/wpa_ie.c \
	$(HOSTAP)/src/rsn_supp/preauth.c \
	$(HOSTAP)/src/rsn_supp/pmksa_cache.c \
	$(HOSTAP)/src/crypto/aes-internal.c \
	$(HOSTAP)/src/crypto/aes-internal-enc.c \
	$(HOSTAP)/src/crypto/aes-internal-dec.c \
	$(HOSTAP)/src/crypto/aes-ccm.c \
	$(HOSTAP)/src/crypto/aes-ctr.c \
	$(HOSTAP)/src/crypto/aes-omac1.c \
	$(HOSTAP)/src/crypto/aes-wrap.c \
	$(HOSTAP)/src/crypto/aes-unwrap.c \
	$(HOSTAP)/src/crypto/sha1.c \
	$(HOSTAP)/src/crypto/sha1-internal.c \
	$(HOSTAP)/src/crypto/sha1-prf.c \
	$(HOSTAP)/src/crypto/sha1-pbkdf2.c \
	$(HOSTAP)/src/crypto/sha256.c \
	$(HOSTAP)/src/crypto/sha256-internal.c \
	$(HOSTAP)/src/crypto/sha256-prf.c \
	$(HOSTAP)/src/crypto/md5.c \
	$(HOSTAP)/src/crypto/md5-internal.c \
	$(HOSTAP)/src/crypto/rc4.c \
	$(HOSTAP)/freertos/src/supp_main.c \
	$(HOSTAP)/hostapd/eap_register.c
endif
