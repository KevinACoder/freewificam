/*
 * @file    stm32f4xx_hal_conf.h
 * @brief   This image's STM32F4 HAL configuration (first-party board config).
 *
 * The HAL driver tree is imported verbatim (third-party/stm32f4xx-hal); this
 * header is the configuration the HAL reads through stm32f4xx_hal.h, and it
 * is deliberately the one place where the board's choices are expressed:
 * which modules exist, the 25 MHz HSE, the ETH ring geometry and the PHY
 * timing.  It is NOT part of the imported tree.
 *
 * Module selection: only RCC and ETH.  The board's clocks, UART, SDRAM and USB
 * are first-party register code (platform/board/stm32f429-challenger/), so
 * nothing else from the HAL is compiled; the ETH pin mux itself is board glue
 * in eth.c, in usb_glue.c's register style, and the one RCC function the ETH
 * driver calls (`HAL_RCC_GetHCLKFreq`) is the same board glue.
 *
 * The Ethernet section is the board's data path, not a knob: 8 + 8 buffers
 * of ETH_MAX_PACKET_SIZE (1524 B) hold one maximum 802.3 frame plus the CRC
 * without chaining, which keeps the netif seam's copy one buffer deep and
 * makes a segmented receive a reported error instead of a silent path.
 *
 * PHY timing: the imported HAL spins on HAL_GetTick() for the MDIO ready bit;
 * the ST template's 0xFFFF ms timeout would turn a dead MDIO bus into a 65 s
 * hang.  100 ms is two orders of magnitude above a clause-22 transaction.
 */

#ifndef FREEWIFICAM_STM32F4XX_HAL_CONF_H
#define FREEWIFICAM_STM32F4XX_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- module selection ----------------------------------------------------- */

#define HAL_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_ETH_MODULE_ENABLED

/* --- oscillator values (the challenger V2 board) -------------------------- */

#if !defined(HSE_VALUE)
#define HSE_VALUE		25000000U	/* 25 MHz crystal, clock.c's PLL source */
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT	100U
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE		16000000U
#endif
#if !defined(LSI_VALUE)
#define LSI_VALUE		32000U
#endif
#if !defined(LSE_VALUE)
#define LSE_VALUE		32768U
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT	5000U
#endif
#if !defined(EXTERNAL_CLOCK_VALUE)
#define EXTERNAL_CLOCK_VALUE	12288000U
#endif

/* --- Ethernet: ring geometry --------------------------------------------- */

#define ETH_RX_BUF_SIZE		ETH_MAX_PACKET_SIZE
#define ETH_TX_BUF_SIZE		ETH_MAX_PACKET_SIZE
#define ETH_RXBUFNB		((uint32_t)8U)
#define ETH_TXBUFNB		((uint32_t)8U)

/* --- Ethernet: PHY (LAN8720A at address 0) ------------------------------- */

/* PHYAD0 is strapped low on this board (LAN8720A datasheet 3.7.1, default 0),
 * and the vendor reference ladder runs at that address; the case prints the
 * ID and the address it read, so a board that ever straps it high is named
 * by its own evidence instead of failing silently. */
#define LAN8720_PHY_ADDRESS	0U

/* Reset/configuration delays in ms (HAL_Delay = osDelay on this board). */
#define PHY_RESET_DELAY		((uint32_t)5U)
#define PHY_CONFIG_DELAY	((uint32_t)5U)
#define PHY_READ_TO		((uint32_t)100U)
#define PHY_WRITE_TO		((uint32_t)100U)

#define PHY_BCR			((uint16_t)0x00U)
#define PHY_BSR			((uint16_t)0x01U)
#define PHY_RESET		((uint16_t)0x8000U)
#define PHY_LOOPBACK		((uint16_t)0x4000U)
#define PHY_FULLDUPLEX_100M	((uint16_t)0x2100U)
#define PHY_HALFDUPLEX_100M	((uint16_t)0x2000U)
#define PHY_FULLDUPLEX_10M	((uint16_t)0x0100U)
#define PHY_HALFDUPLEX_10M	((uint16_t)0x0000U)
#define PHY_AUTONEGOTIATION	((uint16_t)0x1000U)
#define PHY_RESTART_AUTONEGOTIATION ((uint16_t)0x0200U)
#define PHY_POWERDOWN		((uint16_t)0x0800U)
#define PHY_ISOLATE		((uint16_t)0x0400U)
#define PHY_AUTONEGO_COMPLETE	((uint16_t)0x0020U)
#define PHY_LINKED_STATUS	((uint16_t)0x0004U)
#define PHY_JABBER_DETECTION	((uint16_t)0x0002U)

/* PHY_SR (register 31) is the vendor-specific status register.  The HAL's
 * decode masks are the DP83848 pair; on the LAN8720A the field is HCDSPEED
 * [4:2] and those two masks happen to be exact: bit 2 is 1 for 10BASE-T,
 * bit 4 is 1 for full duplex (001/101/010/110 -> 10H/10F/100H/100F). */
#define PHY_SR			((uint16_t)0x1FU)
#define PHY_SPEED_STATUS	((uint16_t)0x0004U)
#define PHY_DUPLEX_STATUS	((uint16_t)0x0010U)

/* --- includes ------------------------------------------------------------- */

#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32f4xx_hal_rcc.h"
#endif /* HAL_RCC_MODULE_ENABLED */


#ifdef HAL_ETH_MODULE_ENABLED
#include "stm32f4xx_hal_eth.h"
#endif /* HAL_ETH_MODULE_ENABLED */

/* --- assertion ------------------------------------------------------------ */

/* Left off: a failed HAL assertion parks the image on a polled console from
 * a context that may hold the print path; the case's own snapshots carry the
 * diagnosis instead.  Define USE_FULL_ASSERT to turn it on while debugging. */
#ifdef USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif /* USE_FULL_ASSERT */

#ifdef __cplusplus
}
#endif

#endif /* FREEWIFICAM_STM32F4XX_HAL_CONF_H */
