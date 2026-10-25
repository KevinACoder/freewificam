# @file    wlan.mk
# @brief   The 8189FTV vendor-stack port's source set and flag worlds.
#
# The vendor driver is consumed byte-identically from an external tree (GPL-2.0,
# embedded firmware array): the release repository does not carry it.  A build
# that selects the wlan case must name the tree:
#
#   CASES="tick cmsis2 wlan" VENDOR_8189FS=<rtl8189fs tree> \
#       HEAP_SIZE=16M tools/rtos/build.sh
#
# Three worlds meet here and are kept apart by their include sets:
#   - the vendor driver (its own config/warning set, linuxkpi headers only);
#   - the linuxkpi runtime face (CMSIS-RTOS2 + the Linux headers);
#   - the platform's SDIO port (fsl_sdmmc + dwc_mmc) and the wlan case.
# The driver never sees the platform's case/backend defines, and the platform's
# units never see the vendor tree's `include/linux` copies.

ifneq ($(filter wlan,$(CASES)),)

ifeq ($(strip $(VENDOR_8189FS)),)
$(error the wlan case needs VENDOR_8189FS=<rtl8189fs tree at pin 13bbdbc>)
endif

# Scan diagnostic arms: WLAN_SCAN_DIAG=1 runs the four request shapes before the
# production full scan.  WLAN_SCAN_BATCH=1 is the production shape: the band is
# covered by four <=4-channel requests, which the vendor duplicates 3x (300 ms
# per channel) so the 100 ms beacon is not missed; the one-shot 14-channel
# request is the diagnostic/fallback shape.  Both are build-time so one boot is
# one arm.
WLAN_SCAN_DIAG ?= 0
WLAN_SCAN_BATCH ?= 1
WLAN_TCP_SINK ?= 0
WLAN_TCP_SINK_SECONDS ?= 30

LKPI := $(COMPONENTS)/linuxkpi/rtos

WLAN_SKB_CFLAGS := -DCONFIG_LKPI_SKB_POOL_NUM=128 \
	-DCONFIG_LKPI_SKB_DATA_POOL_NUM=64 \
	-DCONFIG_LKPI_SKB_HEAP_HDR_MAX=64

# --- source sets -------------------------------------------------------------

WLAN_APP_SRCS := $(LINE)/app/cases/wlan.c \
	$(THIRD_PARTY)/lwip/ports/lkpinetif.c

WLAN_SDIO_SRCS := \
	$(PLATFORM)/board/rk3568-itx/drivers/dwc_mmc.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/fsl_sdmmc_osa.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_card.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_host_dwmmc.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/lkpi_sdio_fsl.c \
	$(THIRD_PARTY)/fsl_sdmmc/common/fsl_sdmmc_common.c \
	$(THIRD_PARTY)/fsl_sdmmc/sdio/fsl_sdio.c

WLAN_LKPI_SRCS := \
	$(LKPI)/linuxkpi_core.c \
	$(LKPI)/lkpi_census.c \
	$(LKPI)/linuxkpi_platform_target.c \
	$(LKPI)/linuxkpi_cfg80211.c \
	$(LKPI)/linuxkpi_netdev.c \
	$(LKPI)/linuxkpi_osdep.c \
	$(LKPI)/sdio/linuxkpi_sdio.c \
	$(LKPI)/sdio/lkpi_sdio_be.c \
	$(LKPI)/skbuff/skbuff_pool.c

# The 131 RTL8188F translation units: the ground-truth module link table
# (37 core + 16 os_dep + 29 hal + 48 hal/phydm + 1 platform).
WLAN_DRV_SRCS := $(addprefix $(VENDOR_8189FS)/,\
	core/rtw_cmd.c core/rtw_security.c core/rtw_debug.c core/rtw_io.c \
	core/rtw_ioctl_query.c core/rtw_ioctl_set.c core/rtw_ieee80211.c core/rtw_mlme.c \
	core/rtw_mlme_ext.c core/rtw_mi.c core/rtw_wlan_util.c core/rtw_vht.c \
	core/rtw_pwrctrl.c core/rtw_rf.c core/rtw_chplan.c core/rtw_recv.c \
	core/rtw_sta_mgt.c core/rtw_ap.c \
	core/mesh/rtw_mesh.c core/mesh/rtw_mesh_pathtbl.c core/mesh/rtw_mesh_hwmp.c \
	core/rtw_xmit.c core/rtw_p2p.c core/rtw_rson.c core/rtw_tdls.c core/rtw_br_ext.c \
	core/rtw_iol.c core/rtw_sreset.c core/rtw_btcoex_wifionly.c core/rtw_btcoex.c \
	core/rtw_beamforming.c core/rtw_odm.c core/rtw_rm.c core/rtw_rm_fsm.c \
	core/efuse/rtw_efuse.c core/rtw_sdio.c core/rtw_mp.c \
	os_dep/osdep_service.c os_dep/linux/os_intfs.c os_dep/linux/sdio_intf.c \
	os_dep/linux/sdio_ops_linux.c os_dep/linux/ioctl_linux.c os_dep/linux/xmit_linux.c \
	os_dep/linux/mlme_linux.c os_dep/linux/recv_linux.c os_dep/linux/ioctl_cfg80211.c \
	os_dep/linux/rtw_cfgvendor.c os_dep/linux/wifi_regd.c os_dep/linux/rtw_android.c \
	os_dep/linux/rtw_proc.c os_dep/linux/rtw_rhashtable.c os_dep/linux/ioctl_mp.c \
	os_dep/linux/custom_gpio_linux.c \
	hal/hal_intf.c hal/hal_com.c hal/hal_com_phycfg.c hal/hal_phy.c hal/hal_dm.c \
	hal/hal_dm_acs.c hal/hal_btcoex_wifionly.c hal/hal_btcoex.c hal/hal_mp.c \
	hal/hal_mcc.c hal/hal_hci/hal_sdio.c hal/led/hal_led.c hal/led/hal_sdio_led.c \
	hal/HalPwrSeqCmd.c hal/rtl8188f/Hal8188FPwrSeq.c hal/rtl8188f/rtl8188f_sreset.c \
	hal/rtl8188f/rtl8188f_hal_init.c hal/rtl8188f/rtl8188f_phycfg.c \
	hal/rtl8188f/rtl8188f_rf6052.c hal/rtl8188f/rtl8188f_dm.c \
	hal/rtl8188f/rtl8188f_rxdesc.c hal/rtl8188f/rtl8188f_cmd.c \
	hal/rtl8188f/hal8188f_fw.c hal/rtl8188f/sdio/sdio_halinit.c \
	hal/rtl8188f/sdio/rtl8189fs_led.c hal/rtl8188f/sdio/rtl8189fs_xmit.c \
	hal/rtl8188f/sdio/rtl8189fs_recv.c hal/rtl8188f/sdio/sdio_ops.c \
	hal/efuse/rtl8188f/HalEfuseMask8188F_SDIO.c \
	hal/phydm/phydm_debug.c hal/phydm/phydm_antdiv.c hal/phydm/phydm_soml.c \
	hal/phydm/phydm_smt_ant.c hal/phydm/phydm_antdect.c hal/phydm/phydm_interface.c \
	hal/phydm/phydm_phystatus.c hal/phydm/phydm_hwconfig.c hal/phydm/phydm.c \
	hal/phydm/phydm_dig.c hal/phydm/phydm_pathdiv.c hal/phydm/phydm_rainfo.c \
	hal/phydm/phydm_dynamictxpower.c hal/phydm/phydm_adaptivity.c \
	hal/phydm/phydm_cfotracking.c hal/phydm/phydm_noisemonitor.c \
	hal/phydm/phydm_beamforming.c hal/phydm/phydm_direct_bf.c hal/phydm/phydm_dfs.c \
	hal/phydm/txbf/halcomtxbf.c hal/phydm/txbf/haltxbfinterface.c \
	hal/phydm/txbf/phydm_hal_txbf_api.c hal/phydm/phydm_adc_sampling.c \
	hal/phydm/phydm_ccx.c hal/phydm/phydm_psd.c hal/phydm/phydm_primary_cca.c \
	hal/phydm/phydm_cck_pd.c hal/phydm/phydm_rssi_monitor.c hal/phydm/phydm_auto_dbg.c \
	hal/phydm/phydm_math_lib.c hal/phydm/phydm_api.c hal/phydm/phydm_pow_train.c \
	hal/phydm/phydm_lna_sat.c hal/phydm/phydm_pmac_tx_setting.c hal/phydm/phydm_mp.c \
	hal/phydm/halrf/halrf.c hal/phydm/halrf/halrf_debug.c hal/phydm/halrf/halphyrf_ce.c \
	hal/phydm/halrf/halrf_powertracking_ce.c hal/phydm/halrf/halrf_powertracking.c \
	hal/phydm/halrf/halrf_kfree.c hal/phydm/halrf/halrf_psd.c \
	hal/phydm/rtl8188f/halhwimg8188f_bb.c hal/phydm/rtl8188f/halhwimg8188f_mac.c \
	hal/phydm/rtl8188f/halhwimg8188f_rf.c hal/phydm/rtl8188f/phydm_regconfig8188f.c \
	hal/phydm/halrf/rtl8188f/halrf_8188f.c hal/phydm/rtl8188f/phydm_rtl8188f.c \
	platform/platform_ops.c)

WLAN_LWIP_SRCS := 	$(THIRD_PARTY)/lwip/ports/sys_arch.c 	$(THIRD_PARTY)/lwip/ports/platform_hooks.c 	$(THIRD_PARTY)/lwip/ports/lkpinetif_lwip.c 	$(LINE)/app/cases/wlan_netif.c 	$(LWIP)/src/core/init.c $(LWIP)/src/core/def.c $(LWIP)/src/core/dns.c 	$(LWIP)/src/core/inet_chksum.c $(LWIP)/src/core/ip.c 	$(LWIP)/src/core/mem.c $(LWIP)/src/core/memp.c 	$(LWIP)/src/core/netif.c $(LWIP)/src/core/pbuf.c 	$(LWIP)/src/core/raw.c $(LWIP)/src/core/stats.c $(LWIP)/src/core/sys.c 	$(LWIP)/src/core/altcp.c $(LWIP)/src/core/altcp_alloc.c 	$(LWIP)/src/core/altcp_tcp.c $(LWIP)/src/core/tcp.c 	$(LWIP)/src/core/tcp_in.c $(LWIP)/src/core/tcp_out.c 	$(LWIP)/src/core/timeouts.c $(LWIP)/src/core/udp.c 	$(LWIP)/src/core/ipv4/acd.c $(LWIP)/src/core/ipv4/autoip.c 	$(LWIP)/src/core/ipv4/dhcp.c $(LWIP)/src/core/ipv4/etharp.c 	$(LWIP)/src/core/ipv4/icmp.c $(LWIP)/src/core/ipv4/igmp.c 	$(LWIP)/src/core/ipv4/ip4_frag.c $(LWIP)/src/core/ipv4/ip4.c 	$(LWIP)/src/core/ipv4/ip4_addr.c 	$(LWIP)/src/api/api_lib.c $(LWIP)/src/api/api_msg.c 	$(LWIP)/src/api/err.c $(LWIP)/src/api/if_api.c 	$(LWIP)/src/api/netbuf.c $(LWIP)/src/api/netdb.c 	$(LWIP)/src/api/netifapi.c $(LWIP)/src/api/sockets.c 	$(LWIP)/src/api/tcpip.c 	$(LWIP)/src/netif/ethernet.c

WLAN_ALL_SRCS := $(WLAN_APP_SRCS) $(WLAN_SDIO_SRCS) $(WLAN_LKPI_SRCS) \
	$(WLAN_DRV_SRCS) $(WLAN_LWIP_SRCS)
WLAN_OBJS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(WLAN_ALL_SRCS))))

# --- flag worlds -------------------------------------------------------------

# The SDIO port and the case live in the platform's world; they need the
# imported stack's SDIO directory and the linuxkpi seam headers.
WLAN_SDIO_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -std=gnu11 \
	-I$(LKPI) -I$(LKPI)/include -I$(LKPI)/sdio \
	-I$(THIRD_PARTY)/fsl_sdmmc/sdio

# The linuxkpi face compiles against the CMSIS-RTOS2 facade with the Linux
# headers it carries; the board's device header is replaced by cmsis_os2.h,
# exactly as the reference image compiles it.
WLAN_LKPI_CFLAGS := $(filter-out -Wextra -DCMSIS_device_header=%,$(CFLAGS)) \
	-std=gnu11 -DLKPI_ABS_PLATFORM_PROVIDED $(WLAN_SKB_CFLAGS) \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	-I$(ROOT) -I$(LKPI) -I$(LKPI)/include -I$(LKPI)/sdio -I$(LKPI)/skbuff

WLAN_APP_CFLAGS := $(filter-out -Wextra -DCMSIS_device_header=%,$(CFLAGS)) \
	-std=gnu11 -DLKPI_ABS_PLATFORM_PROVIDED $(WLAN_SKB_CFLAGS) \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	-DWLAN_SCAN_DIAG=$(WLAN_SCAN_DIAG) -DWLAN_SCAN_BATCH=$(WLAN_SCAN_BATCH) \
	-DWLAN_TCP_SINK=$(WLAN_TCP_SINK) -DWLAN_TCP_SINK_SECONDS=$(WLAN_TCP_SINK_SECONDS) \
	-I$(LKPI) -I$(LKPI)/include -I$(LKPI)/sdio \
	-I$(THIRD_PARTY)/fsl_sdmmc/sdio

# The lwIP half keeps the platform's CMSIS/board flags (the same set the net
# case compiles with): lkPInetif's stack side, sys_arch and the stack must not
# see the linuxkpi device-header substitution.
WLAN_LWIP_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -std=gnu11

# The driver's build configuration is the reference image's resolved set
# (transcribed from the driver's own build), with the log level lowered for
# the 115200 console.  The first bring-up keeps the reference's feature set;
# PS/concurrent/MP are removed only after the attach baseline exists.
WLAN_DRV_CONFIG := -D__KERNEL__ -DMODULE -D__KBUILD_MODNAME=8189fs \
	-DKBUILD_MODNAME='"8189fs"' \
	-DCONFIG_LITTLE_ENDIAN -DCONFIG_IOCTL_CFG80211 -DRTW_USE_CFG80211_STA_EVENT \
	-DCONFIG_CONCURRENT_MODE -DCONFIG_RTL8188F -DCONFIG_MP_INCLUDED \
	-DCONFIG_BR_EXT -DCONFIG_BR_EXT_BRNAME='"br0"' -DCONFIG_POWER_SAVING \
	-DCONFIG_RTW_DEBUG -DRTW_LOG_LEVEL=2 -DCONFIG_EFUSE_CONFIG_FILE \
	-DEFUSE_MAP_PATH='"/system/etc/wifi/wifi_efuse_8189fs.map"' \
	-DCONFIG_LOAD_PHY_PARA_FROM_FILE -DREALTEK_CONFIG_PATH='"/lib/firmware/"' \
	-DCONFIG_TXPWR_BY_RATE=1 -DCONFIG_TXPWR_BY_RATE_EN=1 \
	-DCONFIG_TXPWR_LIMIT=1 -DCONFIG_TXPWR_LIMIT_EN=0 \
	-DCONFIG_RTW_ADAPTIVITY_EN=0 -DCONFIG_RTW_ADAPTIVITY_MODE=0 \
	-DCONFIG_RTW_NETIF_SG -DCONFIG_RTW_SDIO_PM_KEEP_POWER \
	-DDM_ODM_SUPPORT_TYPE=0x04 -DHIGH_ACTIVE_HST2DEV=0 \
	-DWIFIMAC_PATH='"/data/wifimac.txt"'

WLAN_DRV_WARN := -Wall -Wno-address-of-packed-member -Wno-maybe-uninitialized \
	-Wno-missing-field-initializers -Wno-shift-negative-value -Wno-sign-compare \
	-Wno-unused-parameter -Wno-type-limits -Wno-pointer-sign -Wno-unused \
	-Wno-unused-function -Wno-unused-variable -Wno-array-bounds

WLAN_DRV_CFLAGS := -std=c11 -O2 -g3 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-strict-aliasing -fms-extensions \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections $(WLAN_DRV_WARN) $(WLAN_DRV_CONFIG) \
	-DLKPI_ABS_PLATFORM_PROVIDED $(WLAN_SKB_CFLAGS) \
	-I$(LKPI) -I$(LKPI)/include -I$(LKPI)/sdio \
	-I$(THIRD_PARTY)/ulog/src \
	-I$(VENDOR_8189FS) -I$(VENDOR_8189FS)/include -I$(VENDOR_8189FS)/platform \
	-I$(VENDOR_8189FS)/hal/phydm -I$(VENDOR_8189FS)/hal/btc \
	-I$(CMSIS)/CMSIS/RTOS2/Include \
	-include stdio.h -include string.h -include strings.h -include stdlib.h \
	-D_DEFAULT_SOURCE=1

WLAN_FLAGS_STAMP := $(WLAN_APP_CFLAGS) | $(WLAN_SDIO_CFLAGS) | \
	$(WLAN_LKPI_CFLAGS) | $(WLAN_DRV_CFLAGS) | $(VENDOR_8189FS)
WLAN_STAMP := $(BUILD)/wlan-flags-$(BACKEND).stamp
WLAN_LDFLAGS := -Wl,-u,lkpi_module_init_run \
	-Wl,-u,lkpi_sdio_fsl_install -Wl,-u,lkpi_sdio_be_card_init

endif # wlan
