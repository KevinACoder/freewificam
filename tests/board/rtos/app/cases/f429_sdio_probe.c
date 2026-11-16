/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   f429_sdio_probe.c
 * @brief  Case f429_sdio_probe: enumerate the SDIO card on the F429's own
 *         SDIO peripheral, before any function driver claims it.
 *
 * The image carries the imported fsl_sdmmc card service and the ST LL SDMMC
 * host glue, not the vendor driver.  The case starts the service and reports
 * the identity the card's common CIS carries, the negotiated bus width and
 * the clock.  The expected card is the 8189FTV: vendor 0x024c, device 0xf179,
 * four data lines, 24 MHz (the F429's SDIO ceiling).
 */

#include "cases.h"

#include "fsl_sdmmc.h"
#include "sdmmc_port.h"

#define F429_SDIO_VENDOR	0x024cU
#define F429_SDIO_DEVICE	0xf179U
#define F429_SDIO_MIN_CLOCK	20000000U

int
case_f429_sdio_probe_run(void)
{
	sdio_card_t *card;
	status_t status;
	uint32_t vendor;
	uint32_t device;
	uint32_t bus;
	uint32_t clock;

	CASE_ANCHOR("f429_sdio_probe: start\n");

	status = sdmmc_card_start();
	if (status != kStatus_Success) {
		CASE_ANCHOR("f429_sdio_probe: FAIL card start=%d\n", (int)status);
		return 1;
	}

	card = sdmmc_card();
	if (card == NULL) {
		CASE_ANCHOR("f429_sdio_probe: FAIL no card record\n");
		return 1;
	}

	vendor = card->commonCIS.mID;
	device = card->commonCIS.mInfo;
	bus = (card->host != NULL) ? card->host->currBusWidth : 0U;
	clock = card->busClock_Hz;

	CASE_ANCHOR("f429_sdio_probe: card vendor=0x%04x device=0x%04x "
		    "funcs=%u blk=%u bus=%u clock=%u\n",
		    (unsigned)vendor, (unsigned)device,
		    (unsigned)card->ioTotalNumber,
		    (unsigned)card->commonCIS.fn0MaxBlkSize,
		    (unsigned)bus, (unsigned)clock);

	if (vendor != F429_SDIO_VENDOR || device != F429_SDIO_DEVICE) {
		CASE_ANCHOR("f429_sdio_probe: FAIL identity 0x%04x:0x%04x\n",
			    (unsigned)vendor, (unsigned)device);
		return 1;
	}
	if (bus != 4U) {
		CASE_ANCHOR("f429_sdio_probe: FAIL bus=%u\n", (unsigned)bus);
		return 1;
	}
	if (clock < F429_SDIO_MIN_CLOCK) {
		CASE_ANCHOR("f429_sdio_probe: FAIL clock=%u\n",
			    (unsigned)clock);
		return 1;
	}

	CASE_ANCHOR("f429_sdio_probe: PASS PROBE\n");
	return 0;
}
