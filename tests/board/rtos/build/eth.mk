# @file    eth.mk
# @brief   The F429 wired-Ethernet world: the imported STM32F4 HAL ETH
#          closure, the board glue (eth.c) and the lwIP netif bridge.
#
# Included by the platform Makefile right after the board selection, before
# INCLUDES is expanded: the HAL's include roots have to be on the search path
# for the board glue and for the HAL's own two sources.  Everything here is
# empty unless the image's case list carries `f429_eth`.
#
# The HAL sources compile in the vendored flag world (CFLAGS minus -Wextra,
# the same discipline as the other imported trees): they are ST's code and
# the project does not police their warnings.  The build-contract gate still
# sees KCONTRACT_CFLAGS and $(OPT) through that set, which is what it checks.
#
# The face is the closure, not the package: only hal_eth.c is compiled.  The
# one HAL RCC function the ETH driver calls (HAL_RCC_GetHCLKFreq) is a board
# shim in eth.c, because the RCC module's own source drags the FLASH module in
# for dead code; the tree's PROVENANCE.md records the rule.

ETH_HAL := $(ROOT)/third-party/stm32f4xx-hal

ifneq ($(filter f429_eth,$(CASES)),)
ifeq ($(BOARD),stm32f429-challenger)
BOARD_INCLUDES += \
	-I$(ETH_HAL)/CMSIS/Device/ST/STM32F4xx/Include \
	-I$(ETH_HAL)/CMSIS/Include \
	-I$(ETH_HAL)/STM32F4xx_HAL_Driver/Inc
ETH_HAL_SRCS := $(ETH_HAL)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_eth.c
ETH_NETIF_SRCS := $(THIRD_PARTY)/lwip/ports/stm32eth_netif.c
endif
endif
