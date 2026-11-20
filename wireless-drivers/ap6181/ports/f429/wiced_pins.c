/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * wiced_pins.c - the module's pin table, in the vendor's own vocabulary.
 *
 * The vendor platform layer (platform_gpio.c, compiled unchanged) addresses
 * pins through platform_gpio_t and these two tables.  They are the vendor's
 * board description for this board, copied verbatim from its platform.c: the
 * reset line is WL_REG_ON on PB13 (the Challenger V2 wiring), the OOB line is
 * PA0 and the SDIO bus is PC8-PC11/PC12/PD2.
 */

#include "wwd_platform_common.h"
#include "platform_peripheral.h"

const platform_gpio_t wifi_control_pins[] =
{
    [WWD_PIN_RESET] = { GPIOB, 13 },
};

const platform_gpio_t wifi_sdio_pins[] =
{
    [WWD_PIN_SDIO_OOB_IRQ] = { GPIOA,  0 },
    [WWD_PIN_SDIO_CLK    ] = { GPIOC, 12 },
    [WWD_PIN_SDIO_CMD    ] = { GPIOD,  2 },
    [WWD_PIN_SDIO_D0     ] = { GPIOC,  8 },
    [WWD_PIN_SDIO_D1     ] = { GPIOC,  9 },
    [WWD_PIN_SDIO_D2     ] = { GPIOC, 10 },
    [WWD_PIN_SDIO_D3     ] = { GPIOC, 11 },
};
