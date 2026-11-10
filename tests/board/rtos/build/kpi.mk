# @file    kpi.mk
# @brief   The liblinux80211 world: the component's kernel-API bodies, the bus
#          engines the profile selected, the board adapters those engines use,
#          and the two component cases.
#
# This is a flag world, not a second component rule: the component owns its
# sources (components/liblinux80211/sources.mk) and the platform Makefile
# compiles them here because the Linux face needs gnu11 and its own warning set
# (the same reason the USB and wireless worlds have their own sets).
#
# Two profiles carry it:
#   rk3568-kpi-live   the PCI engine over the board's real root complex
#   rk3568-kpi-mock   the same engines over a synthetic host/card, in-image
#
# The live profile also selects the `pcie` case group, because the RK3568 PCI
# adapter reaches the board's root complex through the arch ITS/MSI bridge and
# the board driver that group brings.

LIB8 := $(ROOT)/components/liblinux80211

KPI_INCLUDES := \
	-I$(LIB8)/include \
	-I$(LIB8)/include/uapi \
	-I$(LIB8)/include/linux80211 \
	-I$(LIB8) \
	-I$(LIB8)/bus \
	-I$(LIB8)/ports/pci-rk3568 \
	-I$(PLATFORM)/board/rk3568-itx/drivers/pcie \
	-I$(PLATFORM)/arch/aarch64

KPI_CFLAGS := $(filter-out -Wextra,$(BASE_CFLAGS)) -std=gnu11 \
	-Wno-unused-function -Wno-pointer-sign -Wno-unused-parameter \
	-Wno-sign-compare -Wno-missing-field-initializers \
	$(KPI_INCLUDES)

KPI_CASE_SRCS := $(foreach c,$(filter kpi_live kpi_mock,$(CASES)),$(LINE)/app/cases/$(c).c)

KPI_ALL_SRCS := $(LIBLINUX80211_KPI_SRCS) $(LIBLINUX80211_BUS_PCI_SRCS) \
	$(LIBLINUX80211_BUS_SDIO_SRCS) $(LIBLINUX80211_PCIE_PORT_SRCS) \
	$(KPI_CASE_SRCS)
