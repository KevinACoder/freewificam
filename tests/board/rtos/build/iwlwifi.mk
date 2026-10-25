# @file    iwlwifi.mk
# @brief   The B3 image's wireless world: the vendored iwlwifi driver (AC 7260,
#          mvm) over the linuxkpi PCI engine, the NetBSD net80211 stack and its
#          bridge, and the compiled-in firmware blob.
#
# This is the same three-layer composition the reference iwlwifi image proved:
# net80211 is the MLME, linuxkpi supplies the Linux face the driver compiles
# against, and the board PCIe drivers carry the bus.  Attach + ALIVE is the B3
# gate; scan/join are not carried by this round's case.
#
# Selected only when the case list contains `iwlwifi`; the `pcie` case group must
# also be selected so the arch ITS/MSI bridge and the board root complex are in
# the image.

IWL_LKPI := $(ROOT)/components/linuxkpi/rtos
IWL_NET := $(ROOT)/components/net80211
IWL_NPORT := $(IWL_NET)/ports
IWL_DRV := $(ROOT)/wireless-drivers/iwlwifi
IWL_PCIE_PORT := $(ROOT)/ports/pcie

IWL_IMAGE_NAMES := -DWLAN_BRIDGE_ADAPTER_NAME='"iwlwifi"' \
	-DWLAN_BRIDGE_FIRMWARE_NAME='"iwlwifi-7260-17.ucode"'

IWL_SKB_CFLAGS := -DCONFIG_LKPI_SKB_POOL_NUM=256 \
	-DCONFIG_LKPI_SKB_DATA_POOL_NUM=64 \
	-DCONFIG_LKPI_SKB_HEAP_HDR_MAX=128

IWL_BSD_INCLUDES := -I$(ROOT) -I$(IWL_NPORT)/compat -I$(IWL_NET)/compat/netbsd \
	-I$(IWL_NET) -I$(IWL_NPORT)

IWL_KPI_INCLUDES := -I$(ROOT) -I$(ROOT)/components/linuxkpi -I$(IWL_LKPI) -I$(IWL_LKPI)/include -I$(IWL_PCIE_PORT) \
	-I$(IWL_LKPI)/pci -I$(IWL_NET) -I$(IWL_NPORT) -I$(IWL_NPORT)/bridge

IWL_BSD_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-D_KERNEL -DWLAN_PORT_TARGET -Wno-unused-function -Wno-pointer-sign \
	$(IWL_IMAGE_NAMES) -include $(IWL_NPORT)/compat/prelude.h \
	$(IWL_BSD_INCLUDES) $(IWL_KPI_INCLUDES)

IWL_KPI_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED $(IWL_SKB_CFLAGS) \
	-DCMSIS_device_header='"cmsis_os2.h"' $(IWL_KPI_INCLUDES)

IWL_DRV_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-DCONFIG_IWLWIFI -DCONFIG_IWLMVM -DCONFIG_IWLWIFI_DEBUG \
	-DLKPI_ABS_PLATFORM_PROVIDED $(IWL_SKB_CFLAGS) \
	-I$(IWL_DRV) $(IWL_KPI_INCLUDES) \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-Wno-address-of-packed-member -Wno-type-limits -Wno-unused-but-set-variable

IWL_PCIE_PORT_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-DLKPI_ABS_PLATFORM_PROVIDED -DCMSIS_device_header='"cmsis_os2.h"' \
	-I$(IWL_LKPI) -I$(IWL_LKPI)/include -I$(IWL_LKPI)/pci \
	-I$(PLATFORM)/board/rk3568-itx/drivers/pcie \
	-I$(PLATFORM)/arch/aarch64

IWL_NET80211_SRCS := \
	$(IWL_NET)/net80211/ieee80211.c \
	$(IWL_NET)/net80211/ieee80211_proto.c \
	$(IWL_NET)/net80211/ieee80211_node.c \
	$(IWL_NET)/net80211/ieee80211_output.c \
	$(IWL_NET)/net80211/ieee80211_input.c \
	$(IWL_NET)/net80211/ieee80211_amrr.c \
	$(IWL_NET)/net80211/ieee80211_netbsd.c \
	$(IWL_NET)/net80211/ieee80211_crypto.c \
	$(IWL_NET)/net80211/ieee80211_crypto_none.c \
	$(IWL_NET)/net80211/ieee80211_crypto_ccmp.c \
	$(IWL_NET)/crypto/aes/aes_bear.c \
	$(IWL_NET)/crypto/aes/aes_ct.c \
	$(IWL_NET)/crypto/aes/aes_ct_enc.c \
	$(IWL_NET)/crypto/aes/aes_ct_dec.c \
	$(IWL_NET)/crypto/aes/aes_ccm.c \
	$(IWL_NET)/crypto/aes/aes_ccm_mbuf.c \
	$(IWL_NET)/port/aes_impl_compat.c

IWL_NPORT_SRCS := \
	$(IWL_NPORT)/osal/cmsis2/osal_memory_target.c \
	$(IWL_NPORT)/osal/cmsis2/osal_cmsis2.c \
	$(IWL_NPORT)/net/mbuf.c \
	$(IWL_NPORT)/net/ifnet.c

IWL_BRIDGE_SRCS := $(IWL_NPORT)/bridge/wlan_bridge.c

IWL_LKPI_SRCS := \
	$(IWL_LKPI)/linuxkpi_core.c \
	$(IWL_LKPI)/linuxkpi_compat.c \
	$(IWL_LKPI)/linuxkpi_platform_target.c \
	$(IWL_LKPI)/linuxkpi_80211.c \
	$(IWL_LKPI)/linuxkpi_80211_helpers.c \
	$(IWL_LKPI)/linuxkpi_wiphy_work.c \
	$(IWL_LKPI)/linuxkpi_txq.c \
	$(IWL_LKPI)/linuxkpi_netdev.c \
	$(IWL_LKPI)/linuxkpi_cfg80211.c \
	$(IWL_LKPI)/linuxkpi_osdep.c \
	$(IWL_LKPI)/lkpi_tcp_trace.c \
	$(IWL_LKPI)/fw_registry_rtos.c \
	$(IWL_LKPI)/skbuff/skbuff_pool.c \
	$(IWL_LKPI)/pci/linuxkpi_pci.c \
	$(IWL_NPORT)/bridge/wlan_seam_adapter.c

IWL_DRV_SRCS := \
	$(IWL_DRV)/iwl-drv.c $(IWL_DRV)/iwl-debug.c $(IWL_DRV)/iwl-io.c \
	$(IWL_DRV)/iwl-nvm-parse.c $(IWL_DRV)/iwl-nvm-utils.c \
	$(IWL_DRV)/iwl-phy-db.c $(IWL_DRV)/iwl-trans.c $(IWL_DRV)/iwl-utils.c \
	$(IWL_DRV)/iwl-dbg-tlv.c \
	$(IWL_DRV)/fw/img.c $(IWL_DRV)/fw/notif-wait.c $(IWL_DRV)/fw/rs.c \
	$(IWL_DRV)/fw/dbg.c $(IWL_DRV)/fw/dbg-old.c $(IWL_DRV)/fw/pnvm.c \
	$(IWL_DRV)/fw/dump.c $(IWL_DRV)/fw/regulatory.c $(IWL_DRV)/fw/paging.c \
	$(IWL_DRV)/fw/smem.c $(IWL_DRV)/fw/init.c \
	$(IWL_DRV)/pcie/ctxt-info.c $(IWL_DRV)/pcie/ctxt-info-v2.c \
	$(IWL_DRV)/pcie/drv.c $(IWL_DRV)/pcie/utils.c \
	$(IWL_DRV)/pcie/gen1_2/rx.c $(IWL_DRV)/pcie/gen1_2/tx.c \
	$(IWL_DRV)/pcie/gen1_2/trans.c $(IWL_DRV)/pcie/gen1_2/trans-gen2.c \
	$(IWL_DRV)/pcie/gen1_2/tx-gen2.c \
	$(IWL_DRV)/cfg/7000.c $(IWL_DRV)/cfg/8000.c $(IWL_DRV)/cfg/9000.c \
	$(IWL_DRV)/cfg/22000.c $(IWL_DRV)/cfg/ax210.c $(IWL_DRV)/cfg/bz.c \
	$(IWL_DRV)/cfg/sc.c $(IWL_DRV)/cfg/rf-jf.c $(IWL_DRV)/cfg/rf-hr.c \
	$(IWL_DRV)/cfg/rf-gf.c \
	$(IWL_DRV)/mvm/fw.c $(IWL_DRV)/mvm/mac80211.c $(IWL_DRV)/mvm/nvm.c \
	$(IWL_DRV)/mvm/ops.c $(IWL_DRV)/mvm/phy-ctxt.c \
	$(IWL_DRV)/mvm/mac-ctxt.c $(IWL_DRV)/mvm/utils.c $(IWL_DRV)/mvm/rx.c \
	$(IWL_DRV)/mvm/rxmq.c $(IWL_DRV)/mvm/tx.c $(IWL_DRV)/mvm/binding.c \
	$(IWL_DRV)/mvm/quota.c $(IWL_DRV)/mvm/sta.c $(IWL_DRV)/mvm/sf.c \
	$(IWL_DRV)/mvm/scan.c $(IWL_DRV)/mvm/time-event.c $(IWL_DRV)/mvm/rs.c \
	$(IWL_DRV)/mvm/rs-fw.c $(IWL_DRV)/mvm/power.c $(IWL_DRV)/mvm/coex.c \
	$(IWL_DRV)/mvm/tt.c $(IWL_DRV)/mvm/offloading.c $(IWL_DRV)/mvm/tdls.c \
	$(IWL_DRV)/mvm/ftm-responder.c $(IWL_DRV)/mvm/ftm-initiator.c \
	$(IWL_DRV)/mvm/rfi.c $(IWL_DRV)/mvm/mld-key.c $(IWL_DRV)/mvm/mld-mac.c \
	$(IWL_DRV)/mvm/link.c $(IWL_DRV)/mvm/mld-sta.c \
	$(IWL_DRV)/mvm/mld-mac80211.c $(IWL_DRV)/mvm/ptp.c \
	$(IWL_DRV)/mvm/time-sync.c

IWL_FW_SRCS := $(ROOT)/components/linuxkpi/fw/iwl7260fw_blob.c

IWL_PCIE_PORT_SRCS := $(IWL_PCIE_PORT)/lkpi_pcie_port.c

IWL_CASE_SRCS := $(LINE)/app/cases/iwl7260.c

IWL_ALL_SRCS := $(IWL_NET80211_SRCS) $(IWL_CASE_SRCS) $(IWL_NPORT_SRCS) $(IWL_BRIDGE_SRCS) \
	$(IWL_LKPI_SRCS) $(IWL_DRV_SRCS) $(IWL_FW_SRCS) $(IWL_PCIE_PORT_SRCS)

# The image's own C surface (the iwl7260 case, the pcie case and the board
# drivers) needs the same Linux face the linuxkpi units compile against: the
# case includes linuxkpi_core.h/wlan_bridge.h and the driver's public headers.
IWL_CASE_CFLAGS := -std=gnu11 -DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED \
	$(IWL_KPI_INCLUDES)
BASE_CFLAGS += $(IWL_CASE_CFLAGS)
CFLAGS += $(IWL_CASE_CFLAGS)
