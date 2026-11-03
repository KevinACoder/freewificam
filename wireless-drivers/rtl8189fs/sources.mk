# rtl8189fs: the vendor RTL8188F SDIO driver.
#
# The 131 RTL8188F translation units: the ground-truth module link table
# (37 core + 16 os_dep + 29 hal + 48 hal/phydm + 1 platform).  Paths are
# relative to the vendored tree root (src/), so an override of the tree still
# uses this one list.  The application, SDIO-port and lwIP halves of the image
# are this component's too: they are the vendor stack's own port seams, not
# shared platform code.

ifeq ($(SRC_TAG),drv)
obj-$(CONFIG_WLAN_LINE_RTL8189FS) += \
	src/core/rtw_cmd.c \
	src/core/rtw_security.c \
	src/core/rtw_debug.c \
	src/core/rtw_io.c \
	src/core/rtw_ioctl_query.c \
	src/core/rtw_ioctl_set.c \
	src/core/rtw_ieee80211.c \
	src/core/rtw_mlme.c \
	src/core/rtw_mlme_ext.c \
	src/core/rtw_mi.c \
	src/core/rtw_wlan_util.c \
	src/core/rtw_vht.c \
	src/core/rtw_pwrctrl.c \
	src/core/rtw_rf.c \
	src/core/rtw_chplan.c \
	src/core/rtw_recv.c \
	src/core/rtw_sta_mgt.c \
	src/core/rtw_ap.c \
	src/core/mesh/rtw_mesh.c \
	src/core/mesh/rtw_mesh_pathtbl.c \
	src/core/mesh/rtw_mesh_hwmp.c \
	src/core/rtw_xmit.c \
	src/core/rtw_p2p.c \
	src/core/rtw_rson.c \
	src/core/rtw_tdls.c \
	src/core/rtw_br_ext.c \
	src/core/rtw_iol.c \
	src/core/rtw_sreset.c \
	src/core/rtw_btcoex_wifionly.c \
	src/core/rtw_btcoex.c \
	src/core/rtw_beamforming.c \
	src/core/rtw_odm.c \
	src/core/rtw_rm.c \
	src/core/rtw_rm_fsm.c \
	src/core/efuse/rtw_efuse.c \
	src/core/rtw_sdio.c \
	src/core/rtw_mp.c \
	src/os_dep/osdep_service.c \
	src/os_dep/linux/os_intfs.c \
	src/os_dep/linux/sdio_intf.c \
	src/os_dep/linux/sdio_ops_linux.c \
	src/os_dep/linux/ioctl_linux.c \
	src/os_dep/linux/xmit_linux.c \
	src/os_dep/linux/mlme_linux.c \
	src/os_dep/linux/recv_linux.c \
	src/os_dep/linux/ioctl_cfg80211.c \
	src/os_dep/linux/rtw_cfgvendor.c \
	src/os_dep/linux/wifi_regd.c \
	src/os_dep/linux/rtw_android.c \
	src/os_dep/linux/rtw_proc.c \
	src/os_dep/linux/rtw_rhashtable.c \
	src/os_dep/linux/ioctl_mp.c \
	src/os_dep/linux/custom_gpio_linux.c \
	src/hal/hal_intf.c \
	src/hal/hal_com.c \
	src/hal/hal_com_phycfg.c \
	src/hal/hal_phy.c \
	src/hal/hal_dm.c \
	src/hal/hal_dm_acs.c \
	src/hal/hal_btcoex_wifionly.c \
	src/hal/hal_btcoex.c \
	src/hal/hal_mp.c \
	src/hal/hal_mcc.c \
	src/hal/hal_hci/hal_sdio.c \
	src/hal/led/hal_led.c \
	src/hal/led/hal_sdio_led.c \
	src/hal/HalPwrSeqCmd.c \
	src/hal/rtl8188f/Hal8188FPwrSeq.c \
	src/hal/rtl8188f/rtl8188f_sreset.c \
	src/hal/rtl8188f/rtl8188f_hal_init.c \
	src/hal/rtl8188f/rtl8188f_phycfg.c \
	src/hal/rtl8188f/rtl8188f_rf6052.c \
	src/hal/rtl8188f/rtl8188f_dm.c \
	src/hal/rtl8188f/rtl8188f_rxdesc.c \
	src/hal/rtl8188f/rtl8188f_cmd.c \
	src/hal/rtl8188f/hal8188f_fw.c \
	src/hal/rtl8188f/sdio/sdio_halinit.c \
	src/hal/rtl8188f/sdio/rtl8189fs_led.c \
	src/hal/rtl8188f/sdio/rtl8189fs_xmit.c \
	src/hal/rtl8188f/sdio/rtl8189fs_recv.c \
	src/hal/rtl8188f/sdio/sdio_ops.c \
	src/hal/efuse/rtl8188f/HalEfuseMask8188F_SDIO.c \
	src/hal/phydm/phydm_debug.c \
	src/hal/phydm/phydm_antdiv.c \
	src/hal/phydm/phydm_soml.c \
	src/hal/phydm/phydm_smt_ant.c \
	src/hal/phydm/phydm_antdect.c \
	src/hal/phydm/phydm_interface.c \
	src/hal/phydm/phydm_phystatus.c \
	src/hal/phydm/phydm_hwconfig.c \
	src/hal/phydm/phydm.c \
	src/hal/phydm/phydm_dig.c \
	src/hal/phydm/phydm_pathdiv.c \
	src/hal/phydm/phydm_rainfo.c \
	src/hal/phydm/phydm_dynamictxpower.c \
	src/hal/phydm/phydm_adaptivity.c \
	src/hal/phydm/phydm_cfotracking.c \
	src/hal/phydm/phydm_noisemonitor.c \
	src/hal/phydm/phydm_beamforming.c \
	src/hal/phydm/phydm_direct_bf.c \
	src/hal/phydm/phydm_dfs.c \
	src/hal/phydm/txbf/halcomtxbf.c \
	src/hal/phydm/txbf/haltxbfinterface.c \
	src/hal/phydm/txbf/phydm_hal_txbf_api.c \
	src/hal/phydm/phydm_adc_sampling.c \
	src/hal/phydm/phydm_ccx.c \
	src/hal/phydm/phydm_psd.c \
	src/hal/phydm/phydm_primary_cca.c \
	src/hal/phydm/phydm_cck_pd.c \
	src/hal/phydm/phydm_rssi_monitor.c \
	src/hal/phydm/phydm_auto_dbg.c \
	src/hal/phydm/phydm_math_lib.c \
	src/hal/phydm/phydm_api.c \
	src/hal/phydm/phydm_pow_train.c \
	src/hal/phydm/phydm_lna_sat.c \
	src/hal/phydm/phydm_pmac_tx_setting.c \
	src/hal/phydm/phydm_mp.c \
	src/hal/phydm/halrf/halrf.c \
	src/hal/phydm/halrf/halrf_debug.c \
	src/hal/phydm/halrf/halphyrf_ce.c \
	src/hal/phydm/halrf/halrf_powertracking_ce.c \
	src/hal/phydm/halrf/halrf_powertracking.c \
	src/hal/phydm/halrf/halrf_kfree.c \
	src/hal/phydm/halrf/halrf_psd.c \
	src/hal/phydm/rtl8188f/halhwimg8188f_bb.c \
	src/hal/phydm/rtl8188f/halhwimg8188f_mac.c \
	src/hal/phydm/rtl8188f/halhwimg8188f_rf.c \
	src/hal/phydm/rtl8188f/phydm_regconfig8188f.c \
	src/hal/phydm/halrf/rtl8188f/halrf_8188f.c \
	src/hal/phydm/rtl8188f/phydm_rtl8188f.c \
	src/platform/platform_ops.c
endif

ifeq ($(SRC_TAG),app)
obj-$(CONFIG_WLAN_LINE_RTL8189FS) += \
	$(LINE)/app/cases/wlan.c \
	$(THIRD_PARTY)/lwip/ports/lkpinetif.c
endif

ifeq ($(SRC_TAG),sdio)
obj-$(CONFIG_WLAN_LINE_RTL8189FS) += \
	$(PLATFORM)/board/rk3568-itx/drivers/dwc_mmc.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/fsl_sdmmc_osa.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_card.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_host_dwmmc.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/lkpi_sdio_fsl.c \
	$(THIRD_PARTY)/fsl_sdmmc/common/fsl_sdmmc_common.c \
	$(THIRD_PARTY)/fsl_sdmmc/sdio/fsl_sdio.c
endif

ifeq ($(SRC_TAG),lwip)
obj-$(CONFIG_WLAN_LINE_RTL8189FS) += \
	$(THIRD_PARTY)/lwip/ports/sys_arch.c \
	$(THIRD_PARTY)/lwip/ports/platform_hooks.c \
	$(THIRD_PARTY)/lwip/ports/lkpinetif_lwip.c \
	$(LINE)/app/cases/wlan_netif.c \
	$(filter-out $(LWIP)/src/apps/lwiperf/lwiperf.c,$(LWIP_STACK_SRCS))
endif

ifeq ($(SRC_TAG),obs)
obj-$(CONFIG_WLAN_OBS) += $(ROOT)/wireless-drivers/rtl8189fs/ports/rtos/obs.c
endif
