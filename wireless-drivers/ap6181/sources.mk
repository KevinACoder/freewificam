# AP6181 (BCM43362) WICED port: the driver's own link table.
#
# Two tags, one flag world (tests/board/rtos/build/wiced.mk):
#   port    first-party files, the F429 side of the driver (SDIO host, RTOS,
#           buffers, lwIP seam, power/reset)
#   vendor  the Cypress WWD core and the generic platform layer it calls,
#           materialized into vendor/ from the external tree by
#           tools/build/wiced-tree.py (git-ignored; see PROVENANCE.md)
#
# The vendor list is the WWD.mk table minus every file the port replaces
# (wwd_SDIO.c, wwd_platform.c, wwd_rtos.c, wwd_buffer.c, wwd_network.c,
# platform.c, the STM32 peripheral set and the vendor FreeRTOS/lwIP trees), plus
# the generic resource layer (wwd_resources.c, platform_resource.c), the three
# utility libraries and the 43362 firmware/NVRAM units.  A file appears here
# only if the driver compiles, includes or links it.

ifeq ($(SRC_TAG),port)
obj-$(CONFIG_WICED_AP6181) += \
	ports/f429/wiced_pins.c \
	ports/f429/wwd_platform.c \
	ports/f429/wwd_rtos.c \
	ports/f429/wwd_buffer.c \
	ports/f429/wwd_network.c \
	ports/f429/wiced_console.c
endif

ifeq ($(SRC_TAG),vendor)
obj-$(CONFIG_WICED_AP6181) += \
	vendor/WiFi_SDK/WICED/WWD/internal/bus_protocols/SDIO/wwd_bus_protocol.c \
	vendor/WiFi_SDK/WICED/WWD/internal/bus_protocols/wwd_bus_common.c \
	vendor/WiFi_SDK/WICED/WWD/internal/chips/43362/wwd_ap.c \
	vendor/WiFi_SDK/WICED/WWD/internal/chips/43362/wwd_chip_specific_functions.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_ap_common.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_debug.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_eapol.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_internal.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_logging.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_management.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_rtos_interface.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_sdpcm.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_thread.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_thread_internal.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_wifi.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_wifi_chip_common.c \
	vendor/WiFi_SDK/WICED/WWD/internal/wwd_wifi_sleep.c \
	\
	vendor/WiFi_SDK/WICED/platform/MCU/STM32F4xx/WWD/wwd_SDIO.c \
	vendor/WiFi_SDK/WICED/platform/MCU/STM32F4xx/peripherals/platform_gpio.c \
	vendor/WiFi_SDK/WICED/platform/MCU/STM32F4xx/peripherals/platform_mcu_powersave.c \
	vendor/wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_sdio.c \
	vendor/wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_gpio.c \
	vendor/wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_rcc.c \
	vendor/wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/misc.c \
	vendor/wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_syscfg.c \
	vendor/wifi_lwip_AP/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_exti.c \
	vendor/WiFi_SDK/WICED/platform/MCU/wwd_resources.c \
	vendor/WiFi_SDK/WICED/platform/MCU/platform_resource.c \
	vendor/WiFi_SDK/libraries/utilities/TLV/tlv.c \
	vendor/WiFi_SDK/libraries/utilities/ring_buffer/ring_buffer.c \
	vendor/wifi_lwip_AP/User/43362A2_bin.c
endif