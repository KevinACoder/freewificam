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
# A later mac80211 world adds `stack` here; the engine groups do not change.
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
