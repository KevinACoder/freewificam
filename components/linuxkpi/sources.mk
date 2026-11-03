# linuxkpi: the Linux-shaped kernel API the wireless drivers compile against.
# Two consumers select different subsets:
#
#   SRC_TAG=net80211  the four net80211 drivers: the full face plus the bus
#                     engine each line needs and the bridge seam adapter;
#   SRC_TAG=wlan      the 8189FS vendor stack: the face it was ported against
#                     plus the census instrumentation, no net80211 glue.
#
# The per-line order is the one the board-proven images linked.

ifeq ($(SRC_TAG),net80211)
obj-$(CONFIG_LINUXKPI) += \
	rtos/linuxkpi_core.c \
	rtos/linuxkpi_compat.c \
	rtos/linuxkpi_platform_target.c \
	rtos/linuxkpi_80211.c \
	rtos/linuxkpi_80211_helpers.c \
	rtos/linuxkpi_wiphy_work.c \
	rtos/linuxkpi_txq.c \
	rtos/linuxkpi_netdev.c \
	rtos/linuxkpi_cfg80211.c \
	rtos/linuxkpi_osdep.c \
	rtos/lkpi_tcp_trace.c \
	rtos/fw_registry_rtos.c \
	rtos/skbuff/skbuff_pool.c

obj-$(CONFIG_LINUXKPI_USB) += \
	rtos/usb/linuxkpi_usb.c \
	rtos/usb/lkpi_usbh_class.c \
	rtos/usb/lkpi_usbh_modeswitch.c

obj-$(CONFIG_LINUXKPI_PCI) += rtos/pci/linuxkpi_pci.c
obj-$(CONFIG_LINUXKPI_SDIO) += rtos/sdio/linuxkpi_sdio.c rtos/sdio/lkpi_sdio_be.c
endif

ifeq ($(SRC_TAG),cherry)
# The CherryUSB backend sits after the net80211 seam in the link order, the
# same position the pre-component lists had.
obj-$(CONFIG_LINUXKPI_USB) += $(ROOT)/ports/cherryusb/lkpi_usb_cherryusb.c
endif

ifeq ($(SRC_TAG),wlan)
# The vendor port's face, in its own link order: core, census, the services,
# the SDIO engine, then the skb pool.
obj-$(CONFIG_LINUXKPI) += rtos/linuxkpi_core.c
obj-$(CONFIG_LINUXKPI_CENSUS) += rtos/lkpi_census.c
obj-$(CONFIG_LINUXKPI) += \
	rtos/linuxkpi_platform_target.c \
	rtos/linuxkpi_cfg80211.c \
	rtos/linuxkpi_netdev.c \
	rtos/linuxkpi_osdep.c
obj-$(CONFIG_LINUXKPI_SDIO) += rtos/sdio/linuxkpi_sdio.c rtos/sdio/lkpi_sdio_be.c
obj-$(CONFIG_LINUXKPI) += rtos/skbuff/skbuff_pool.c
endif
