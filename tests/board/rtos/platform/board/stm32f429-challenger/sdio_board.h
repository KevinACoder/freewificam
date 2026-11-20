/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sdio_board.h - the F429 SDIO socket's board glue (AP6181 / BCM43362).
 *
 * Four facts live here and nowhere else: which pins carry the bus, which pin
 * is the module's WL_REG_ON rail, the SDIO peripheral's clock source, and the
 * interrupt priority.  The transfer engine is the driver port's
 * (wireless-drivers/ap6181/ports/f429/sdio_host.c); this file only brings the
 * socket up.
 */

#ifndef FREEWIFICAM_STM32F429_SDIO_BOARD_H
#define FREEWIFICAM_STM32F429_SDIO_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* Bring the socket up: clocks, pins, controller in a known state, interrupts
 * off.  The module's rail is left off. */
void sdio_board_init(void);

/* The AP6181's WL_REG_ON (WIFI_EN), PB13 on the Challenger V2. */
void sdio_board_wl_reg_on(bool on);
bool sdio_board_wl_reg_state(void);

/* The SDIO peripheral's input clock (PLL48CLK, MHz-qualified below). */
uint32_t sdio_board_source_hz(void);

/* The controller interrupt and the DMA stream's interrupt, both at the one
 * priority the board assigns to the bus. */
void sdio_board_irq_enable(bool enable);
void sdio_board_dma_irq_enable(bool enable);

#endif /* FREEWIFICAM_STM32F429_SDIO_BOARD_H */