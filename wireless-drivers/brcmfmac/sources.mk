# brcmfmac: the compiled set for the BCM43362 (AP6181) over SDIO
# under the feature face wireless-drivers/brcmfmac/driver_config.h.
# The tree is brcm80211/ imported whole; only brcmfmac/ and
# brcmutil/ units are compiled (SDIO + BCDC; no PCIe/USB/MSGBUF,
# no debug/tracing, no OF/ACPI/DMI/PM).  The built-in Makefile
# branch also names the wcc/cyw/bca core units, so they are in.
ifeq ($(SRC_TAG),drv)
obj-$(CONFIG_LIBLINUX80211_BRCMFMAC) += \
	brcmfmac/cfg80211.c \
	brcmfmac/chip.c \
	brcmfmac/fwil.c \
	brcmfmac/fweh.c \
	brcmfmac/p2p.c \
	brcmfmac/proto.c \
	brcmfmac/common.c \
	brcmfmac/core.c \
	brcmfmac/firmware.c \
	brcmfmac/fwvid.c \
	brcmfmac/feature.c \
	brcmfmac/btcoex.c \
	brcmfmac/vendor.c \
	brcmfmac/pno.c \
	brcmfmac/xtlv.c \
	brcmfmac/bcdc.c \
	brcmfmac/fwsignal.c \
	brcmfmac/sdio.c \
	brcmfmac/bcmsdh.c \
	brcmfmac/wcc/core.c \
	brcmfmac/cyw/core.c \
	brcmfmac/bca/core.c \
	brcmutil/utils.c \
	brcmutil/d11.c
endif
