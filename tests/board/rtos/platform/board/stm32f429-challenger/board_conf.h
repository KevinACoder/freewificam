/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * board_conf.h - STM32F429 挑战者 V2 board constants.
 *
 * DATA ONLY.  The USB priority is here because build_defs.h names it for every
 * board.  This board's USB host is OTG_HS in full-speed mode on PB14/PB15
 * (V2 base schematic sheet 8; J74-J76 in the HOS position); the line is
 * IRQ 77 and the value below sits exactly at FreeRTOS's
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5).
 */

#ifndef FREEWIFICAM_STM32F429_BOARD_CONF_H
#define FREEWIFICAM_STM32F429_BOARD_CONF_H

#define BOARD_IRQ_PRIORITY_API_CALL_RAW	0x50u

/* Console: onboard CH340 USB-TTL on USART1 PA9/PA10. */
#define BOARD_UART_BASE			0x40011000UL
#define BOARD_UART_BAUD			115200UL

/* FreeRTOS kernel tick: the CM4F port runs it from SysTick. */
#define BOARD_TICK_RATE_HZ		1000u

#define BOARD_FLASH_BASE		0x08000000UL
#define BOARD_FLASH_SIZE		(1024UL * 1024UL)

#endif /* FREEWIFICAM_STM32F429_BOARD_CONF_H */
