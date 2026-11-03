# platform/arch: the AArch64 boot and exception layer, plus the GICv3 ITS/MSI
# bridge the PCIe domain needs.
#
# The arch files are one world (the platform's own flags).  The PCIe group is
# selected by the case string because the ITS/MSI domain is a case-level
# facility: `pcie` brings it on the RK3568 root complex, and `nvme` needs it on
# the firefly ECAM board.

ifeq ($(SRC_TAG),base)
obj-y += \
	aarch64/start.S \
	aarch64/gicv3.c \
	aarch64/generic_timer.c \
	aarch64/mmu.c \
	aarch64/cache.c
endif

ifeq ($(SRC_TAG),pcie)
ifneq ($(filter pcie,$(CASES))$(if $(filter firefly-v1,$(BOARD)),$(filter nvme,$(CASES))),)
obj-y += \
	aarch64/gicv3_its.c \
	aarch64/gicv3_msi.c \
	aarch64/irq_ctrl.c
endif
endif
