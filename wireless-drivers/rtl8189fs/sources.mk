# rtl8189fs: the vendor RTL8188F SDIO driver's own link table.
#
# The list is `8189fs-y` as the vendor Makefile resolves it (the
# in-tree Kbuild's object table, and `make -pn` on this tree, both
# give these 131 units; tools/build/import-driver.py --check
# re-derives it). Paths are relative to the imported tree root src/.
#
# The RTOS feature face (driver_config.h) departs from the GT build
# in three preprocessor answers, and none removes a unit:
# CONFIG_PROC_DEBUG off (rtw_proc.c becomes the empty #ifdef arm;
# this project has no filesystem), CONFIG_WIFI_MONITOR off (the STA
# round's face), CONFIG_BR_EXT off (as the GT Kbuild itself does:
# the bridge extension needs a bridge-module symbol). Every other
# CONFIG_* the vendor Makefile sets stays as the GT build has it.
ifeq ($(SRC_TAG),drv)
obj-$(CONFIG_LIBLINUX80211_RTL8189FS) += \
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
	src/platform/platform_ops.c \
	src/core/rtw_mp.c
endif

# The SDIO world this driver needs: the board's DesignWare MMC host driver, the
# imported fsl_sdmmc card service and its dwmmc host glue, and the seam adapter
# that turns the engine's six calls into that service.  These files live outside
# the driver tree (the port owns them); they are listed here because one
# component's sources.mk is the one place its image's file set is stated, the
# same split the previous line used.  Paths are make variables, so the helper
# keeps them absolute.
ifeq ($(SRC_TAG),sdio)
sdio-obj-$(CONFIG_LIBLINUX80211_RTL8189FS) += \
	$(PLATFORM)/board/rk3568-itx/drivers/dwc_mmc.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/fsl_sdmmc_osa.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_card.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/sdmmc_host_dwmmc.c \
	$(THIRD_PARTY)/fsl_sdmmc/ports/lkpi_sdio_fsl.c \
	$(THIRD_PARTY)/fsl_sdmmc/common/fsl_sdmmc_common.c \
	$(THIRD_PARTY)/fsl_sdmmc/sdio/fsl_sdio.c
endif

# The first-party observation port: a plain-C snapshot of the vendor driver's
# own queue gauges (free recv frames, recv buffers, the SDIO RX FIFO byte
# count, the TX page watermark), read through the adapter the probe left on the
# SDIO function.  It compiles in the driver's world because it includes the
# vendor's headers, and it edits no vendor byte.  Imported from the previous
# line's ports/rtos (same tree, same pin); see PROVENANCE.md.
ifeq ($(SRC_TAG),port)
port-obj-$(CONFIG_LIBLINUX80211_RTL8189FS) += \
	ports/rtos/obs.c
endif
