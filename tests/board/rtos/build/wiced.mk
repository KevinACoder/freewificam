# @file wiced.mk
# @brief The AP6181 (BCM43362) WICED world: the vendor driver core over the
#        first-party F429 port, and the cases that walk its ladder.
#
# This is a flag world in the same sense as wl80211.mk: one include set and one
# set of defines for every unit of the driver, the vendor core and the port
# alike, so a header change cannot be seen by only half of the driver.  It is
# not the Linux world: nothing here includes linuxkpi, mac80211 or cfg80211, and
# the image gate below proves it.
#
# The vendor core lives in vendor/ (git-ignored, materialized by
# tools/build/wiced-tree.py from the external tree named by WICED_ROOT).  Its
# absence is a configuration error with one readable line, not a cascade of
# missing headers.

# Every case this world can carry; the selected ones come from CONFIG_CASES.
WICED_CASES ?= ap6181_attach ap6181_scan ap6181_join ap6181_data

ifeq ($(CONFIG_WICED_AP6181),1)

AP6181_DIR := $(ROOT)/wireless-drivers/ap6181
AP6181_VENDOR := $(AP6181_DIR)/vendor

ifeq ($(wildcard $(AP6181_VENDOR)/WiFi_SDK/WICED/WWD/include/wwd_management.h),)
$(error wireless-drivers/ap6181/vendor is not materialized: set WICED_ROOT and run tools/build/wiced-tree.py)
endif

$(eval $(call component_sources,$(AP6181_DIR),port,AP6181_PORT))
$(eval $(call component_sources,$(AP6181_DIR),vendor,AP6181_VENDOR_SRC))

# A WWD buffer is an lwIP pbuf, so the stack core and its port face are a
# requirement of this driver, not a profile option: without them the driver
# cannot allocate its first buffer.  The data stage additionally starts the
# TCP/IP thread (ap6181_lwip_stack_start).
NET_APP_SRCS += $(THIRD_PARTY)/lwip/ports/sys_arch.c \
	$(THIRD_PARTY)/lwip/ports/platform_hooks.c
NET_STACK_SRCS += $(LWIP_STACK_SRCS)

AP6181_VENDOR_INC := \
	-I$(AP6181_DIR)/ports/f429 \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/WWD \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/WWD/include \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/WWD/include/network \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/WWD/include/platform \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/WWD/include/RTOS \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/WWD/internal \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/WWD/internal/bus_protocols \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/WWD/internal/bus_protocols/SDIO \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/WWD/internal/chips/43362 \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/security/BESL/include \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/RTOS/FreeRTOS/WICED \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/platform/include \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/platform/MCU \
	-I$(AP6181_VENDOR)/WiFi_SDK/WICED/platform/MCU/STM32F4xx \
	-I$(AP6181_VENDOR)/WiFi_SDK/include \
	-I$(AP6181_VENDOR)/WiFi_SDK/libraries/utilities/TLV \
	-I$(AP6181_VENDOR)/WiFi_SDK/libraries/utilities/ring_buffer \
	-I$(AP6181_VENDOR)/WiFi_SDK/libraries/utilities/linked_list \
	-I$(AP6181_VENDOR)/WiFi_SDK/resources/firmware/43362

# WICED_DISABLE_BOOTLOADER: no DCT and no bootloader in this image, which is
# also what the vendor's own example build defines.  FIRMWARE_WITH_PMK_CALC_SUPPORT
# is the 43362's face in WWD.mk (PMK computed in firmware, no host crypto).
AP6181_DEFS := -DWICED_DISABLE_BOOTLOADER -DFIRMWARE_WITH_PMK_CALC_SUPPORT

ifeq ($(CONFIG_WICED_AP6181_NO_OOB),1)
AP6181_DEFS += -DWICED_DISABLE_MCU_POWERSAVE
endif

ifeq ($(CONFIG_WICED_AP6181_LOG),1)
AP6181_DEFS += -DWICED_VERBOSE
endif

# The vendor tree is C99 with its own warning taste; the port files are ours and
# could take -Wextra, but one world for both is the point of this file.  The
# kernel contract flags (check-build-contract) and $(OPT) come from CFLAGS.
WICED_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -std=gnu11 \
	$(AP6181_DEFS) $(AP6181_VENDOR_INC)

AP6181_ALL_SRCS := $(AP6181_PORT_SRCS) $(AP6181_VENDOR_SRC_SRCS)
AP6181_CASE_SRCS := $(foreach c,$(filter $(WICED_CASES),$(CASES)),$(LINE)/app/cases/$(c).c)
AP6181_ALL_SRCS += $(AP6181_CASE_SRCS)

endif # CONFIG_WICED_AP6181