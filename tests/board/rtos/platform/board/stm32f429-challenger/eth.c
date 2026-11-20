/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   eth.c
 * @brief  The F429 challenger V2's wired Ethernet: the board seam around the
 *         imported ST HAL ETH driver.
 *
 * What is imported and what is written here:
 *
 *  - The MAC, the DMA descriptor rings, MDIO and the PHY auto-negotiation are
 *    the ST HAL's (`third-party/stm32f4xx-hal`, HAL 1.7.3).  This file calls
 *    HAL_ETH_Init / HAL_ETH_DMATxDescListInit / HAL_ETH_DMARxDescListInit /
 *    HAL_ETH_Start and the frame entry points; it does not re-implement any
 *    of that state machine.
 *  - Written here: the board's clocks and pin mux (the HAL's MSP hook), the
 *    HAL time base (FreeRTOS owns SysTick, so HAL_GetTick/HAL_Delay are the
 *    RTOS's), the MAC address source (the device UID), and the frame-level
 *    seam `eth.h` publishes to the lwIP bridge.
 *
 * Receive is polled, not interrupt-driven: HAL_ETH_Init is given
 * ETH_RXPOLLING_MODE, so the DMA interrupt line stays disabled and there is
 * no ISR to race the handle.  The ring is 8 frames deep and the netif thread
 * drains it every millisecond, which is three orders of magnitude above this
 * profile's DHCP/ICMP load.  The interrupt arm is a later, additive change.
 *
 * Serialisation: the HAL's handle is not re-entrant (`__HAL_LOCK` + State,
 * plus one shared TxDesc/RxDesc cursor).  Transmit runs on the TCP/IP thread
 * and receive on the netif drain thread, so every HAL handle call is taken
 * under one CMSIS-RTOS2 mutex.  The critical sections are one memcpy and a
 * handful of register writes; nothing inside them prints or takes another
 * lock.
 *
 * Board facts (V2 base schematic, sheet 9): LAN8720A at PHY address 0 in RMII
 * mode; the PHY's 25 MHz crystal drives its REFCLKO, which is the 50 MHz
 * REF_CLK PA1 expects; the PHY's nRST is the MCU's NRST (there is no GPIO
 * reset line on V2 - PI1 is the MPU6050 interrupt, and the vendor V1 example's
 * PI1 reset pulse is deliberately not reproduced).
 */

#include <stdint.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include "stm32f4xx_hal.h"

#include "eth.h"

/* --- the rings and the handle --------------------------------------------- */

static ETH_HandleTypeDef g_heth;

static ETH_DMADescTypeDef g_rx_desc[F429_ETH_RX_RING] __attribute__((aligned(4)));
static ETH_DMADescTypeDef g_tx_desc[F429_ETH_TX_RING] __attribute__((aligned(4)));
static uint8_t g_rx_buf[F429_ETH_RX_RING][F429_ETH_FRAME_MAX]
	__attribute__((aligned(4)));
static uint8_t g_tx_buf[F429_ETH_TX_RING][F429_ETH_FRAME_MAX]
	__attribute__((aligned(4)));

static uint8_t g_mac[6];
static osMutexId_t g_eth_lock;
static int g_eth_started;

static struct f429_eth_stats g_stats;

/* The HAL's register-write delay unit is a millisecond; a PHY/MDIO wait
 * that never returns is the one hang this board's errata document warns
 * about (ES0227 2.16.6), so every mutex wait is bounded too. */
#define F429_ETH_LOCK_MS	100U

/* --- the HAL time base ---------------------------------------------------- */

/*
 * HAL_ETH_Init inside HAL_ETH_ReadPHYRegister and the link wait spin on
 * HAL_GetTick(); HAL_Delay covers the PHY reset and the register-write
 * delays.  Both are the RTOS's, because the CM4F FreeRTOS port owns SysTick.
 * Consequence, stated for the caller: the HAL can only be initialised once
 * the scheduler is running (the case thread is; nothing before it may call
 * f429_eth_init()).
 */
uint32_t HAL_GetTick(void)
{
	return (uint32_t)osKernelGetTickCount();
}

void HAL_Delay(uint32_t delay_ms)
{
	if (delay_ms != 0U) {
		(void)osDelay(delay_ms);
	}
}

/*
 * The only RCC function the ETH driver calls: its MDC divider table is picked
 * from HCLK.  The RCC module's own source is not imported - it pulls the
 * FLASH module in for dead MCO/clock-config code - so the board answers this
 * one question from the clock tree it already set up (clock.c sets 168 MHz).
 */
uint32_t HAL_RCC_GetHCLKFreq(void)
{
	return SystemCoreClock;
}

/* --- board glue: clocks and pins ------------------------------------------ */

static void eth_pin_af11(GPIO_TypeDef *port, unsigned int pin)
{
	uint32_t shift = pin * 2U;
	uint32_t af_shift = (pin & 7U) * 4U;
	uint32_t af_index = pin >> 3;

	port->MODER = (port->MODER & ~(3UL << shift)) | (2UL << shift);
	port->OSPEEDR |= (3UL << shift);	/* very high speed */
	port->PUPDR &= ~(3UL << shift);
	port->AFR[af_index] = (port->AFR[af_index] & ~(0xFUL << af_shift)) |
			      (11UL << af_shift);	/* AF11 = ETH */
}

/*
 * The HAL calls this from HAL_ETH_Init when the handle is in its reset state
 * (our static handle is zeroed, so exactly once).  The SYSCFG clock and the
 * RMII select bit are the HAL's own next steps; the ETH and GPIO clocks are
 * the board's.
 */
void HAL_ETH_MspInit(ETH_HandleTypeDef *heth)
{
	if (heth == NULL || heth->Instance != ETH) {
		return;
	}

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN |
			RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIOGEN |
			RCC_AHB1ENR_ETHMACEN |
			RCC_AHB1ENR_ETHMACTXEN | RCC_AHB1ENR_ETHMACRXEN;
	(void)RCC->AHB1ENR;

	/* RMII: TX_EN PB11, TXD0 PG13, TXD1 PG14, RXD0 PC4, RXD1 PC5,
	 * CRS_DV PA7, REF_CLK PA1, MDC PC1, MDIO PA2. */
	eth_pin_af11(GPIOB, 11U);
	eth_pin_af11(GPIOG, 13U);
	eth_pin_af11(GPIOG, 14U);
	eth_pin_af11(GPIOC, 4U);
	eth_pin_af11(GPIOC, 5U);
	eth_pin_af11(GPIOA, 7U);
	eth_pin_af11(GPIOA, 1U);
	eth_pin_af11(GPIOC, 1U);
	eth_pin_af11(GPIOA, 2U);

	__asm__ __volatile__("dsb" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

/* --- MAC address: the device UID ------------------------------------------ */

/*
 * The board carries no MAC EEPROM, so the address is derived from the 96-bit
 * unique ID at 0x1FFF7A10.  The locally-administered bit is set and the
 * multicast bit is clear: a stable unicast address per board, no collision
 * between two boards on the same LAN, and no claim on a vendor OUI.
 */
static void eth_mac_from_uid(uint8_t mac[6])
{
	const volatile uint32_t *uid = (const volatile uint32_t *)0x1FFF7A10UL;
	uint32_t uid_word0 = uid[0];
	uint32_t uid_word1 = uid[1];

	mac[0] = 0x02U;
	mac[1] = (uint8_t)(uid_word0 >> 24);
	mac[2] = (uint8_t)(uid_word0 >> 16);
	mac[3] = (uint8_t)(uid_word0 >> 8);
	mac[4] = (uint8_t)uid_word0;
	mac[5] = (uint8_t)uid_word1;
}

/* --- DMA recovery helpers (the HAL's polling path leaves these to us) ----- */

/* Give every descriptor of the last frame back to the DMA.  The HAL filled
 * RxFrameInfos; one segment is the normal case (the buffers hold a maximum
 * frame), but an oversized frame counted as segmented is released whole. */
static void eth_rx_release(void)
{
	ETH_DMADescTypeDef *desc = g_heth.RxFrameInfos.FSRxDesc;
	uint32_t segments = g_heth.RxFrameInfos.SegCount;

	if (desc == NULL) {
		return;
	}
	if (segments == 0U) {
		segments = 1U;
	}
	while (segments-- != 0U) {
		desc->Status |= ETH_DMARXDESC_OWN;
		desc = (ETH_DMADescTypeDef *)desc->Buffer2NextDescAddr;
	}
	g_heth.RxFrameInfos.SegCount = 0U;
}

/* Receive buffer unavailable: the DMA stopped at a descriptor the host had
 * not returned yet.  Clearing the flag and poking the poll demand restarts
 * it (the vendor reference does the same; without it the ring stays dead). */
static void eth_rx_resume(void)
{
	if ((g_heth.Instance->DMASR & ETH_DMASR_RBUS) != 0U) {
		g_heth.Instance->DMASR = ETH_DMASR_RBUS;
		g_heth.Instance->DMARPDR = 0U;
	}
}

static void eth_tx_resume(void)
{
	if ((g_heth.Instance->DMASR & ETH_DMASR_TUS) != 0U) {
		g_heth.Instance->DMASR = ETH_DMASR_TUS;
		g_heth.Instance->DMATPDR = 0U;
	}
}

/* --- public API ----------------------------------------------------------- */

int f429_eth_init(void)
{
	HAL_StatusTypeDef st;

	if (g_eth_started != 0) {
		return 0;
	}
	if (g_eth_lock == NULL) {
		g_eth_lock = osMutexNew(NULL);
		if (g_eth_lock == NULL) {
			return -2;
		}
	}

	eth_mac_from_uid(g_mac);

	memset(&g_heth, 0, sizeof(g_heth));
	memset(&g_stats, 0, sizeof(g_stats));
	g_heth.Instance = ETH;
	g_heth.Init.AutoNegotiation = ETH_AUTONEGOTIATION_ENABLE;
	g_heth.Init.Speed = ETH_SPEED_100M;
	g_heth.Init.DuplexMode = ETH_MODE_FULLDUPLEX;
	g_heth.Init.PhyAddress = F429_ETH_PHY_ADDR;
	g_heth.Init.MACAddr = g_mac;
	g_heth.Init.RxMode = ETH_RXPOLLING_MODE;
	g_heth.Init.ChecksumMode = ETH_CHECKSUM_BY_SOFTWARE;
	g_heth.Init.MediaInterface = ETH_MEDIA_INTERFACE_RMII;

	/* PHY reset, link wait, auto-negotiation and the MAC/DMA configuration
	 * are the HAL's; the return value is the failing step's status. */
	st = HAL_ETH_Init(&g_heth);
	if (st != HAL_OK) {
		return (int)st + 1;
	}

	st = HAL_ETH_DMATxDescListInit(&g_heth, g_tx_desc, &g_tx_buf[0][0],
				       F429_ETH_TX_RING);
	if (st != HAL_OK) {
		return (int)st + 1;
	}
	st = HAL_ETH_DMARxDescListInit(&g_heth, g_rx_desc, &g_rx_buf[0][0],
				       F429_ETH_RX_RING);
	if (st != HAL_OK) {
		return (int)st + 1;
	}
	st = HAL_ETH_Start(&g_heth);
	if (st != HAL_OK) {
		return (int)st + 1;
	}

	g_eth_started = 1;
	return 0;
}

void f429_eth_mac(uint8_t mac[6])
{
	if (mac == NULL) {
		return;
	}
	memcpy(mac, g_mac, 6);
}

int f429_eth_link(struct f429_eth_link *out)
{
	uint16_t bsr = 0U;
	uint16_t sr = 0U;

	if (out == NULL) {
		return -1;
	}
	out->speed_mbit = 0U;
	out->full_duplex = 0U;
	if (g_eth_started == 0) {
		return 0;
	}
	if (f429_eth_phy_read(PHY_BSR, &bsr) != 0) {
		return -1;
	}
	if (f429_eth_phy_read(PHY_SR, &sr) != 0) {
		return -1;
	}
	if ((bsr & PHY_LINKED_STATUS) == 0U) {
		return 0;
	}
	/* LAN8720A HCDSPEED[4:2] through the DP83848 masks: bit 2 => 10M,
	 * bit 4 => full duplex.  See stm32f4xx_hal_conf.h. */
	out->speed_mbit = ((sr & PHY_SPEED_STATUS) != 0U) ? 10U : 100U;
	out->full_duplex = ((sr & PHY_DUPLEX_STATUS) != 0U) ? 1U : 0U;
	return 1;
}

int f429_eth_phy_read(uint16_t reg, uint16_t *val)
{
	uint32_t raw = 0U;
	HAL_StatusTypeDef st;

	if (val == NULL || g_eth_lock == NULL) {
		return -1;
	}
	if (osMutexAcquire(g_eth_lock, F429_ETH_LOCK_MS) != osOK) {
		return -1;
	}
	st = HAL_ETH_ReadPHYRegister(&g_heth, reg, &raw);
	(void)osMutexRelease(g_eth_lock);
	if (st != HAL_OK) {
		return -1;
	}
	*val = (uint16_t)raw;
	return 0;
}

int f429_eth_phy_write(uint16_t reg, uint16_t val)
{
	HAL_StatusTypeDef st;

	if (g_eth_lock == NULL) {
		return -1;
	}
	if (osMutexAcquire(g_eth_lock, F429_ETH_LOCK_MS) != osOK) {
		return -1;
	}
	st = HAL_ETH_WritePHYRegister(&g_heth, reg, (uint32_t)val);
	(void)osMutexRelease(g_eth_lock);
	return (st == HAL_OK) ? 0 : -1;
}

int f429_eth_read_frame(uint8_t *dst, unsigned int cap, unsigned int *len)
{
	unsigned int frame_len;

	if (dst == NULL || len == NULL) {
		return -1;
	}
	*len = 0U;
	if (g_eth_started == 0 || g_eth_lock == NULL) {
		return -1;
	}
	if (osMutexAcquire(g_eth_lock, F429_ETH_LOCK_MS) != osOK) {
		return 0;
	}

	if (HAL_ETH_GetReceivedFrame(&g_heth) != HAL_OK) {
		eth_rx_resume();
		(void)osMutexRelease(g_eth_lock);
		return 0;
	}

	frame_len = g_heth.RxFrameInfos.length;
	if (g_heth.RxFrameInfos.SegCount != 1U || frame_len == 0U ||
	    frame_len > F429_ETH_FRAME_MAX) {
		eth_rx_release();
		g_stats.rx_len_err++;
		eth_rx_resume();
		(void)osMutexRelease(g_eth_lock);
		return 0;
	}
	if (frame_len > cap) {
		eth_rx_release();
		g_stats.rx_copy_err++;
		eth_rx_resume();
		(void)osMutexRelease(g_eth_lock);
		return 0;
	}

	memcpy(dst, (const void *)(uintptr_t)g_heth.RxFrameInfos.buffer, frame_len);
	eth_rx_release();
	g_stats.rx_frames++;
	*len = frame_len;
	eth_rx_resume();
	(void)osMutexRelease(g_eth_lock);
	return 1;
}

int f429_eth_send_frame(const uint8_t *frame, unsigned int len)
{
	if (frame == NULL || len == 0U || len > F429_ETH_FRAME_MAX) {
		return -1;
	}
	if (g_eth_started == 0 || g_eth_lock == NULL) {
		return -1;
	}
	if (osMutexAcquire(g_eth_lock, F429_ETH_LOCK_MS) != osOK) {
		g_stats.tx_busy++;
		return -1;
	}

	if (g_heth.TxDesc == NULL ||
	    (g_heth.TxDesc->Status & ETH_DMATXDESC_OWN) != 0U) {
		g_stats.tx_busy++;
		eth_tx_resume();
		(void)osMutexRelease(g_eth_lock);
		return -1;
	}

	memcpy((void *)(uintptr_t)g_heth.TxDesc->Buffer1Addr, frame, len);
	if (HAL_ETH_TransmitFrame(&g_heth, len) != HAL_OK) {
		g_stats.tx_err++;
		eth_tx_resume();
		(void)osMutexRelease(g_eth_lock);
		return -1;
	}

	g_stats.tx_frames++;
	eth_tx_resume();
	(void)osMutexRelease(g_eth_lock);
	return 0;
}

void f429_eth_stats_snapshot(struct f429_eth_stats *out)
{
	uint32_t mfb = 0U;

	if (out == NULL) {
		return;
	}
	*out = g_stats;
	if (g_eth_started != 0) {
		mfb = g_heth.Instance->DMAMFBOCR;
		out->dma_missed = (mfb & ETH_DMAMFBOCR_MFA) >>
				  ETH_DMAMFBOCR_MFA_Pos;
		out->dma_fifo_ovf = mfb & ETH_DMAMFBOCR_MFC;
	}
}

void f429_eth_diag(struct f429_eth_diag *out)
{
	if (out == NULL) {
		return;
	}
	memset(out, 0, sizeof(*out));
	if (g_heth.Instance != ETH) {
		return;
	}
	out->dmabmr = g_heth.Instance->DMABMR;
	out->dmasr = g_heth.Instance->DMASR;
	out->maccr = g_heth.Instance->MACCR;
	out->macffr = g_heth.Instance->MACFFR;
	out->macmiiar = g_heth.Instance->MACMIIAR;
	out->macmiidr = g_heth.Instance->MACMIIDR;
	if (f429_eth_phy_read(PHY_BCR, &out->phy_bcr) != 0) {
		out->phy_errors++;
	}
	if (f429_eth_phy_read(PHY_BSR, &out->phy_bsr) != 0) {
		out->phy_errors++;
	}
	if (f429_eth_phy_read(2U, &out->phy_id1) != 0) {
		out->phy_errors++;
	}
	if (f429_eth_phy_read(3U, &out->phy_id2) != 0) {
		out->phy_errors++;
	}
	if (f429_eth_phy_read(PHY_SR, &out->phy_sr) != 0) {
		out->phy_errors++;
	}
}
