# iwlwifi: the vendored AC 7260 (iwlmvm) driver.
#
# The 72 translation units the kernel's CONFIG_IWLWIFI/CONFIG_IWLMVM resolves
# for the 7260: core, PCIe gen1/2 transport, the cfg families, fw/* and mvm/*.
# The firmware blob is compiled in the linuxkpi world; the single case too.

ifeq ($(SRC_TAG),drv)
obj-$(CONFIG_WLAN_LINE_IWLWIFI) += \
	iwl-drv.c \
	iwl-debug.c \
	iwl-io.c \
	iwl-nvm-parse.c \
	iwl-nvm-utils.c \
	iwl-phy-db.c \
	iwl-trans.c \
	iwl-utils.c \
	iwl-dbg-tlv.c \
	fw/img.c \
	fw/notif-wait.c \
	fw/rs.c \
	fw/dbg.c \
	fw/dbg-old.c \
	fw/pnvm.c \
	fw/dump.c \
	fw/regulatory.c \
	fw/paging.c \
	fw/smem.c \
	fw/init.c \
	pcie/ctxt-info.c \
	pcie/ctxt-info-v2.c \
	pcie/drv.c \
	pcie/utils.c \
	pcie/gen1_2/rx.c \
	pcie/gen1_2/tx.c \
	pcie/gen1_2/trans.c \
	pcie/gen1_2/trans-gen2.c \
	pcie/gen1_2/tx-gen2.c \
	cfg/7000.c \
	cfg/8000.c \
	cfg/9000.c \
	cfg/22000.c \
	cfg/ax210.c \
	cfg/bz.c \
	cfg/sc.c \
	cfg/rf-jf.c \
	cfg/rf-hr.c \
	cfg/rf-gf.c \
	mvm/fw.c \
	mvm/mac80211.c \
	mvm/nvm.c \
	mvm/ops.c \
	mvm/phy-ctxt.c \
	mvm/mac-ctxt.c \
	mvm/utils.c \
	mvm/rx.c \
	mvm/rxmq.c \
	mvm/tx.c \
	mvm/binding.c \
	mvm/quota.c \
	mvm/sta.c \
	mvm/sf.c \
	mvm/scan.c \
	mvm/time-event.c \
	mvm/rs.c \
	mvm/rs-fw.c \
	mvm/power.c \
	mvm/coex.c \
	mvm/tt.c \
	mvm/offloading.c \
	mvm/tdls.c \
	mvm/ftm-responder.c \
	mvm/ftm-initiator.c \
	mvm/rfi.c \
	mvm/mld-key.c \
	mvm/mld-mac.c \
	mvm/link.c \
	mvm/mld-sta.c \
	mvm/mld-mac80211.c \
	mvm/ptp.c \
	mvm/time-sync.c
endif

ifeq ($(SRC_TAG),kpi)
obj-$(CONFIG_WLAN_LINE_IWLWIFI) += \
	$(ROOT)/components/linuxkpi/fw/iwl7260_fw_blob.c
endif

ifeq ($(SRC_TAG),case)
obj-$(CONFIG_WLAN_LINE_IWLWIFI) += \
	$(LINE)/app/cases/iwl7260.c
endif
