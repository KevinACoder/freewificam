/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sdio_board.h - the F429 SDIO socket: pins, card rail and controller reset.
 *
 * The TF slot carries either the 8189FTV module (the WiFi line) or a plain SD
 * memory card (the SD card line), on the SDIO bus it shares with the AP6181:
 * PC8-PC11 (D0-D3), PC12 (CLK) and PD2 (CMD), all AF12.  PB13 is the
 * AP6181's WIFI_EN; it must stay low so the other card does not answer on the
 * shared bus.
 */
#ifndef SDIO_BOARD_H
#define SDIO_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* Claim the SDIO pins and controller clock, hold the AP6181 off, and leave
 * the controller powered down.  Called from the host glue's init. */
void sdio_board_init(void);

/* Drive the AP6181's WIFI_EN (PB13).  The card rail of the TF slot is not
 * switchable; this only silences the other card on the shared bus.  Safe
 * before sdio_board_init(): it enables the GPIO port itself. */
void sdio_board_wl_reg_on(bool on);

/* The SDIO kernel clock (PLLQ): 48 MHz on this board. */
uint32_t sdio_board_source_hz(void);

/* The card interrupt line (DAT1) is not carried in the first round; these
 * exist so the host glue can arm it later without changing the board API. */
void sdio_board_irq_enable(bool enable);

#endif /* SDIO_BOARD_H */
