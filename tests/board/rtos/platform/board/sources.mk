# platform/board: the board's own drivers.
#
# The base set is chosen by the Kconfig board choice; the PCIe and NVMe groups
# are board facts selected by the case (the RK3568 DesignWare root complex
# versus the firefly ECAM root complex, and the two NVMe drivers).  The bus
# ports that need their own flag worlds (USB, SDIO/MCI, the wired NIC) stay in
# their fragments: their source lists are compiled with flags this component's
# one world cannot express.

ifeq ($(SRC_TAG),base)
obj-$(CONFIG_BOARD_RK3568_ITX) += \
	rk3568-itx/drivers/uart.c \
	rk3568-itx/drivers/cpu_freq.c \
	rk3568-itx/param.c \
	rk3568-itx/reset.c
obj-$(CONFIG_BOARD_FIREFLY_V1) += \
	firefly/drivers/uart.c \
	firefly/param.c \
	firefly/reset.c
obj-$(CONFIG_BOARD_FIREFLY_V2) += \
	firefly/drivers/uart.c \
	firefly/param.c \
	firefly/reset.c
endif

ifeq ($(SRC_TAG),pcie)
ifneq ($(filter pcie,$(CASES)),)
ifeq ($(BOARD),rk3568-itx)
obj-y += \
	rk3568-itx/drivers/pcie/pcie_dw.c \
	rk3568-itx/drivers/pcie/dwc_msix.c \
	rk3568-itx/drivers/pcie/dwc_msi.c \
	rk3568-itx/drivers/pcie/dwc_pcie.c
endif
endif
endif

ifeq ($(SRC_TAG),nvme)
# The `pcie` case owns the RK3568 domain bring-up and always carries the NVMe
# block driver (pcie.c uses it); the firefly `nvme` case uses the fnvme group.
ifneq ($(filter pcie nvme,$(CASES)),)
ifeq ($(BOARD),rk3568-itx)
obj-y += rk3568-itx/drivers/nvme/dwc_nvme.c
endif
endif
endif

ifeq ($(SRC_TAG),fnvme)
ifneq ($(filter nvme,$(CASES)),)
ifeq ($(BOARD),firefly-v1)
obj-y += \
	firefly/drivers/pcie_msix.c \
	firefly/drivers/nvme/nvme.c
endif
endif
endif

ifeq ($(SRC_TAG),ecam)
ifneq ($(filter pcie_ecam nvme,$(CASES)),)
ifeq ($(BOARD),firefly-v1)
obj-y += firefly/drivers/pcie_ecam.c
endif
endif
endif
