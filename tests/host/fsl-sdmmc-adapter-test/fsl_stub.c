/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * fsl_stub.c - the scripted stand-in for the imported SDIO stack.
 *
 * Every recorded call carries the arguments the adapter passed, so a test can
 * assert the shape of a transfer rather than only its result: how many calls a
 * request became, at which addresses, with which flags, and in which order.
 *
 * The one piece of real state is the card descriptor: the adapter reads its
 * block sizes and its staging buffer's size from it, so those values are
 * scripted here.
 */

#include <string.h>

#include "fsl_stub.h"

static struct fsl_stub_call calls[FSL_STUB_MAX_CALLS];
static unsigned int call_count;
static status_t scripted_status = kStatus_Success;
static uint8_t rx_pattern;

static sdmmchost_t stub_host;
static sdio_card_t stub_card;

void fsl_stub_reset(void)
{
	(void)memset(calls, 0, sizeof(calls));
	call_count = 0U;
	scripted_status = kStatus_Success;
	rx_pattern = 0U;

	(void)memset(&stub_host, 0, sizeof(stub_host));
	(void)memset(&stub_card, 0, sizeof(stub_card));
	stub_card.host = &stub_host;
	stub_host.card = &stub_card;

	/* the values the adapter reads */
	stub_card.io0blockSize = 0U;
	stub_card.internalBufferSize = 4608U;
	stub_card.ioTotalNumber = 1U;
	stub_card.cccrVersioin = 2U;
	stub_card.sdioVersion = 3U;
	stub_card.busClock_Hz = 50000000U;
	stub_card.cccrflags = 0x00000002U; /* multi-block support */
	stub_card.currentTiming = kSD_TimingSDR25HighSpeedMode;
	stub_card.commonCIS.mID = 0x024cU;
	stub_card.commonCIS.mInfo = 0xf179U;
	stub_card.commonCIS.fn0MaxBlkSize = 512U;
	stub_host.currBusWidth = 4U;
}

unsigned int fsl_stub_count(void)
{
	return call_count;
}

const struct fsl_stub_call *fsl_stub_call(unsigned int index)
{
	return (index < call_count) ? &calls[index] : NULL;
}

void fsl_stub_fail_with(status_t status)
{
	scripted_status = status;
}

sdio_card_t *fsl_stub_card(void)
{
	return &stub_card;
}

void fsl_stub_set_block_size(unsigned int func, uint32_t size)
{
	if (func == 0U) {
		stub_card.io0blockSize = size;
	} else {
		stub_card.ioFBR[func - 1U].ioBlockSize = (uint16_t)size;
	}
}

void fsl_stub_set_internal_buffer_size(uint32_t size)
{
	stub_card.internalBufferSize = size;
}

void fsl_stub_set_pending_interrupt(uint8_t value)
{
	/* the stub answers with this byte; the adapter's irq_pending passes it out */
	stub_card.ioIntIndex = value;
}

void fsl_stub_set_rx_pattern(uint8_t base)
{
	rx_pattern = base;
}

static struct fsl_stub_call *record(unsigned int op, unsigned int func,
				    unsigned int addr, unsigned int count,
				    unsigned int flags, bool write)
{
	struct fsl_stub_call *call;

	if (call_count >= FSL_STUB_MAX_CALLS) {
		return NULL;
	}

	call = &calls[call_count++];
	call->op = op;
	call->func = func;
	call->addr = addr;
	call->count = count;
	call->flags = flags;
	call->write = write;
	call->status = scripted_status;

	return call;
}

/* --- the entry points the adapter uses ------------------------------------ */

status_t sdmmc_card_start(void)
{
	(void)record(FSL_STUB_OP_CARD_START, 0U, 0U, 0U, 0U, false);

	return scripted_status;
}

sdio_card_t *sdmmc_card(void)
{
	return &stub_card;
}

void sdmmc_card_stop(void)
{
	(void)record(FSL_STUB_OP_CARD_STOP, 0U, 0U, 0U, 0U, false);
}

void sdmmc_card_int_set(void (*cb)(void *), void *arg)
{
	(void)record(FSL_STUB_OP_CARD_INT_SET, 0U, 0U, 0U,
		     (cb != NULL) ? 1U : 0U, false);
	(void)arg;
}

void FDWMMCHOST_CardIntEnable(sdmmchost_t *host, bool enable)
{
	(void)record(FSL_STUB_OP_CARD_INT_ENABLE, 0U, 0U,
		     enable ? 1U : 0U, 0U, false);
	(void)host;
}

status_t SDIO_IO_Read_Direct(sdio_card_t *card, sdio_func_num_t func,
			     uint32_t regAddr, uint8_t *data)
{
	struct fsl_stub_call *call;

	(void)card;
	call = record(FSL_STUB_OP_DIRECT, (unsigned int)func, regAddr, 1U, 0U, false);
	if (call == NULL) {
		return kStatus_Fail;
	}
	if ((scripted_status == kStatus_Success) && (data != NULL)) {
		*data = (uint8_t)(rx_pattern + (uint8_t)regAddr);
	}

	return scripted_status;
}

status_t SDIO_IO_Write_Direct(sdio_card_t *card, sdio_func_num_t func,
			      uint32_t regAddr, uint8_t *data, bool raw)
{
	(void)card;
	(void)data;
	(void)raw;

	(void)record(FSL_STUB_OP_DIRECT, (unsigned int)func, regAddr, 1U, 0U, true);

	return scripted_status;
}

status_t SDIO_IO_Read_Extended(sdio_card_t *card, sdio_func_num_t func,
			       uint32_t regAddr, uint8_t *buffer, uint32_t count,
			       uint32_t flags)
{
	struct fsl_stub_call *call;

	(void)card;
	call = record(FSL_STUB_OP_EXTENDED, (unsigned int)func, regAddr, count, flags, false);
	if (call == NULL) {
		return kStatus_Fail;
	}
	if ((scripted_status == kStatus_Success) && (buffer != NULL)) {
		for (uint32_t i = 0U; i < count; i++) {
			buffer[i] = (uint8_t)(rx_pattern + (uint8_t)i);
		}
	}

	return scripted_status;
}

status_t SDIO_IO_Write_Extended(sdio_card_t *card, sdio_func_num_t func,
				uint32_t regAddr, uint8_t *buffer, uint32_t count,
				uint32_t flags)
{
	(void)card;
	(void)buffer;

	(void)record(FSL_STUB_OP_EXTENDED, (unsigned int)func, regAddr, count, flags, true);

	return scripted_status;
}

status_t SDIO_SetBlockSize(sdio_card_t *card, sdio_func_num_t func, uint32_t blockSize)
{
	struct fsl_stub_call *call;

	(void)card;
	call = record(FSL_STUB_OP_SET_BLOCK_SIZE, (unsigned int)func, 0U, blockSize, 0U, false);
	if (call == NULL) {
		return kStatus_Fail;
	}
	if (scripted_status == kStatus_Success) {
		fsl_stub_set_block_size((unsigned int)func, blockSize);
	}

	return scripted_status;
}

status_t SDIO_GetPendingInterrupt(sdio_card_t *card, uint8_t *pendingInt)
{
	struct fsl_stub_call *call;

	(void)card;
	call = record(FSL_STUB_OP_IRQ_PENDING, 0U, 0U, 0U, 0U, false);
	if (call == NULL) {
		return kStatus_Fail;
	}
	if ((scripted_status == kStatus_Success) && (pendingInt != NULL)) {
		*pendingInt = stub_card.ioIntIndex;
	}

	return scripted_status;
}
