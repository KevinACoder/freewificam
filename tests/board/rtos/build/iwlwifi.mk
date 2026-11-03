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

IWL_IMAGE_NAMES :=

IWL_SKB_CFLAGS :=

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
	-DWLAN_SEAM_NO_USB_OBS=1 \
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

IWL_NET80211_SRCS := $(NET80211_CORE_SRCS)

IWL_NPORT_SRCS := $(NET80211_NPORT_SRCS)

IWL_BRIDGE_SRCS := $(NET80211_BRIDGE_SRCS)

IWL_LKPI_SRCS := $(LINUXKPI_SRCS) $(NET80211_KPI_SRCS)

IWL_DRV_SRCS := $(IWLWIFI_IMPL_SRCS)

IWL_FW_SRCS := $(IWLWIFI_BLOB_SRCS)

IWL_PCIE_PORT_SRCS := $(IWL_PCIE_PORT)/lkpi_pcie_port.c

IWL_CASE_SRCS := $(IWLWIFI_APP_SRCS)

IWL_ALL_SRCS := $(IWL_NET80211_SRCS) $(IWL_CASE_SRCS) $(IWL_NPORT_SRCS) $(IWL_BRIDGE_SRCS) \
	$(IWL_LKPI_SRCS) $(IWL_DRV_SRCS) $(IWL_FW_SRCS) $(IWL_PCIE_PORT_SRCS)

# The lwIP-over-bridge image (case iwl7260_net): the stack's port glue and the
# lwIP core in the platform's flag world, plus the case itself.  This must stay
# out of the driver world: lwIP's headers and the driver's headers own the same
# names, and the case is the only file that sees the bridge's scalar face
# (wlan_bridge.h -> seam.h).  The flags are taken from BASE_CFLAGS *before* the
# Linux face below is appended, so the lwIP units never compile against
# <net/mac80211.h>; deriving them from BASE_CFLAGS also keeps them inside the
# existing flag stamp.
ifneq ($(filter iwl7260_net,$(CASES)),)
IWL_NET_SRCS := \
	$(THIRD_PARTY)/lwip/ports/sys_arch.c \
	$(THIRD_PARTY)/lwip/ports/platform_hooks.c \
	$(LWIP_STACK_SRCS) \
	$(LINE)/app/cases/iwl7260_net.c
IWL_NET_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-I$(IWL_NPORT)/bridge
endif

# The image's own C surface (the iwl7260 case, the pcie case and the board
# drivers) needs the same Linux face the linuxkpi units compile against: the
# case includes linuxkpi_core.h/wlan_bridge.h and the driver's public headers.
IWL_CASE_CFLAGS := -std=gnu11 -DWLAN_PORT_TARGET -DLKPI_ABS_PLATFORM_PROVIDED \
	$(IWL_KPI_INCLUDES)
BASE_CFLAGS += $(IWL_CASE_CFLAGS)
CFLAGS += $(IWL_CASE_CFLAGS)
