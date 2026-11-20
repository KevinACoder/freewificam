/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * platform_mcu_peripheral.h - compatibility shim.
 *
 * One vendor unit (wwd_bus_common.c) includes the STM32F4 platform's peripheral
 * header and uses nothing from it.  That header would drag the whole StdPeriph
 * and CMSIS tree into this driver world, so the include resolves here instead:
 * this port talks to the SDIO peripheral through its own register definitions
 * (sdio_host.c) and has no MCU peripheral abstraction to expose.
 */

#ifndef AP6181_PLATFORM_MCU_PERIPHERAL_SHIM_H
#define AP6181_PLATFORM_MCU_PERIPHERAL_SHIM_H

#include "platform_constants.h"
#include "platform_toolchain.h"

#endif /* AP6181_PLATFORM_MCU_PERIPHERAL_SHIM_H */
