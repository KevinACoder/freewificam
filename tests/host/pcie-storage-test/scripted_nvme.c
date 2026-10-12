/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    scripted_nvme.c
 * @brief   The scripted NVMe controller behind the storage stack.
 *
 * See scripted_nvme.h for the model. The transfer core is a submission followed
 * by a BOUNDED completion wait, so a command whose completion never arrives (or
 * whose doorbell never lands) returns a timeout error in a fixed number of
 * iterations instead of looping - the property the fault cases assert.
 */

#include <stdint.h>
#include <string.h>

#include "scripted_nvme.h"

#define SCRIPTED_NVME_CTRL	0u

enum { SCRIPTED_OP_READ = 1, SCRIPTED_OP_WRITE = 2, SCRIPTED_OP_FLUSH = 3 };

/* The backing store: the "media". Zero-initialised (BSS). */
static uint8_t store[SCRIPTED_NVME_BLOCKS * SCRIPTED_NVME_BLOCK_SIZE];

static uint32_t fault;

static uint32_t submissions;
static uint32_t completions;
static uint32_t consumed;
static uint32_t irqs;
static uint32_t spurious_irqs;
static uint32_t polls;
static uint32_t doorbell_dropped;

static uint64_t last_lba;
static uint32_t last_blocks;

/* The completion queue (depth 1): an entry posted by the device and reaped by
 * the driver's wait loop. */
static uint8_t cq_full;

void scripted_nvme_reset(void)
{
	(void)memset(store, 0, sizeof(store));

	fault = SCRIPTED_FAULT_NONE;
	submissions = 0;
	completions = 0;
	consumed = 0;
	irqs = 0;
	spurious_irqs = 0;
	polls = 0;
	doorbell_dropped = 0;
	last_lba = 0;
	last_blocks = 0;
	cq_full = 0;
}

void scripted_nvme_set_fault(uint32_t f)
{
	fault |= f;
}

void scripted_nvme_clear_fault(uint32_t f)
{
	fault &= ~f;
}

uint32_t scripted_nvme_submissions(void)	{ return submissions; }
uint32_t scripted_nvme_completions(void)	{ return completions; }
uint32_t scripted_nvme_consumed(void)		{ return consumed; }
uint32_t scripted_nvme_irqs(void)		{ return irqs; }
uint32_t scripted_nvme_spurious_irqs(void)	{ return spurious_irqs; }
uint32_t scripted_nvme_polls(void)		{ return polls; }
uint32_t scripted_nvme_doorbell_dropped(void)	{ return doorbell_dropped; }

uint64_t scripted_nvme_last_lba(void)		{ return last_lba; }
uint32_t scripted_nvme_last_blocks(void)	{ return last_blocks; }

/* Reap one completion entry. Returns 1 when an entry was consumed, 0 when the
 * queue was empty (an interrupt with nothing behind it). */
static int reap(void)
{
	if (cq_full) {
		cq_full = 0;
		consumed++;
		return 1;
	}

	spurious_irqs++;
	return 0;
}

void scripted_nvme_late_irq(void)
{
	/* The interrupt arrives now; the completion it referred to was already
	 * reaped by the wait loop, so the queue is empty and nothing is
	 * consumed - this is what "no double consume" means. */
	irqs++;
	(void)reap();
}

/* The transfer core: doorbell, bounded completion wait, execute, post + reap,
 * then assert the interrupt. */
static int scripted_transfer(int op, uint64_t lba, void *buf, uint32_t blocks)
{
	uint32_t i;
	int visible;

	if (lba + (uint64_t)blocks > (uint64_t)SCRIPTED_NVME_BLOCKS) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	last_lba = lba;
	last_blocks = blocks;

	/* Doorbell: the submission becomes visible to the device only if the
	 * doorbell is written. A dropped doorbell means the device never sees
	 * the command - it never completes, so the wait below times out. */
	if (fault & SCRIPTED_FAULT_DROP_DOORBELL) {
		doorbell_dropped++;
		visible = 0;
	} else {
		submissions++;
		visible = 1;
	}

	/* Bounded completion wait. The device executes a visible command and
	 * posts a completion unless the completion is being dropped. */
	for (i = 0; i < SCRIPTED_NVME_MAX_POLLS; i++) {
		polls++;
		if (visible && !(fault & SCRIPTED_FAULT_DROP_COMPLETION)) {
			break;
		}
	}
	if (i == SCRIPTED_NVME_MAX_POLLS) {
		return ARM_DRIVER_ERROR_TIMEOUT;
	}

	/* Execute. A lying device acknowledges the command but hands back
	 * inverted bytes on a read - the "it said ok, the data is wrong" case
	 * a checksum catches and a status check does not. */
	if (op == SCRIPTED_OP_READ) {
		(void)memcpy(buf, store + lba * SCRIPTED_NVME_BLOCK_SIZE,
			     blocks * SCRIPTED_NVME_BLOCK_SIZE);
		if (fault & SCRIPTED_FAULT_LIE) {
			uint8_t *p = buf;
			uint32_t n = blocks * SCRIPTED_NVME_BLOCK_SIZE;

			for (i = 0; i < n; i++) {
				p[i] = (uint8_t)~p[i];
			}
		}
	} else if (op == SCRIPTED_OP_WRITE) {
		(void)memcpy(store + lba * SCRIPTED_NVME_BLOCK_SIZE, buf,
			     blocks * SCRIPTED_NVME_BLOCK_SIZE);
	}

	/* Post the completion and reap it (the driver's wait loop consumes it). */
	completions++;
	cq_full = 1;
	(void)reap();

	/* Assert the interrupt, unless it is suppressed or deferred to the
	 * caller (the late-IRQ model). */
	if (!(fault & SCRIPTED_FAULT_SUPPRESS_IRQ)) {
		if (fault & SCRIPTED_FAULT_LATE_IRQ) {
			/* delivered later by scripted_nvme_late_irq() */
		} else {
			irqs++;
		}
	}

	return ARM_DRIVER_OK;
}

/* --- ARM_DRIVER_NVME ------------------------------------------------------- */

static ARM_DRIVER_VERSION nvme_get_version(void)
{
	return (ARM_DRIVER_VERSION){ .api = NVME_API_VERSION, .drv = 0x0100 };
}

static NVME_CAPABILITIES nvme_get_capabilities(uint32_t ctrl)
{
	NVME_CAPABILITIES caps;

	(void)memset(&caps, 0, sizeof(caps));
	if (ctrl != SCRIPTED_NVME_CTRL) {
		return caps;
	}

	caps.sector_count = SCRIPTED_NVME_BLOCKS;
	caps.sector_size = SCRIPTED_NVME_BLOCK_SIZE;
	caps.max_transfer = 64u * 1024u;
	caps.queue_depth = 16u;
	caps.timeout_ms = 1000u;

	return caps;
}

static int32_t nvme_initialize(uint32_t ctrl, NVME_SignalEvent_t cb_event)
{
	(void)cb_event;

	return (ctrl == SCRIPTED_NVME_CTRL) ? ARM_DRIVER_OK
					    : ARM_DRIVER_ERROR;
}

static int32_t nvme_uninitialize(uint32_t ctrl)
{
	return (ctrl == SCRIPTED_NVME_CTRL) ? ARM_DRIVER_OK
					    : ARM_DRIVER_ERROR_PARAMETER;
}

static int32_t nvme_power_control(uint32_t ctrl, uint32_t state)
{
	(void)state;

	return (ctrl == SCRIPTED_NVME_CTRL) ? ARM_DRIVER_OK
					    : ARM_DRIVER_ERROR_PARAMETER;
}

static int32_t nvme_identify(uint32_t ctrl, char *model, uint32_t model_size)
{
	static const char name[] = "scripted-nvme";

	if (ctrl != SCRIPTED_NVME_CTRL || model == NULL || model_size == 0) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	if (model_size > sizeof(name)) {
		(void)memcpy(model, name, sizeof(name));
	}

	return ARM_DRIVER_OK;
}

static int32_t nvme_read_blocks(uint32_t ctrl, uint64_t lba, void *data,
				uint32_t block_count)
{
	if (ctrl != SCRIPTED_NVME_CTRL || data == NULL) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	return scripted_transfer(SCRIPTED_OP_READ, lba, data, block_count);
}

static int32_t nvme_write_blocks(uint32_t ctrl, uint64_t lba, const void *data,
				 uint32_t block_count)
{
	if (ctrl != SCRIPTED_NVME_CTRL || data == NULL) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	return scripted_transfer(SCRIPTED_OP_WRITE, lba, (void *)data,
				 block_count);
}

static int32_t nvme_flush(uint32_t ctrl)
{
	if (ctrl != SCRIPTED_NVME_CTRL) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	return scripted_transfer(SCRIPTED_OP_FLUSH, 0, NULL, 0);
}

static uint32_t nvme_get_transfer_status(uint32_t ctrl)
{
	(void)ctrl;

	return 0;
}

ARM_DRIVER_NVME Driver_NVME = {
	nvme_get_version,
	nvme_get_capabilities,
	nvme_initialize,
	nvme_uninitialize,
	nvme_power_control,
	nvme_identify,
	nvme_read_blocks,
	nvme_write_blocks,
	nvme_flush,
	nvme_get_transfer_status,
};

/* --- the board driver's bring-up entry points ------------------------------ */

void dwc_nvme_init(void)
{
	/* idempotent: the scripted controller is always "probed" */
}

uint32_t dwc_nvme_ctrl_count(void)
{
	return 1;
}

uint32_t dwc_nvme_irq_count(uint32_t ctrl)
{
	return (ctrl == SCRIPTED_NVME_CTRL) ? irqs : 0;
}
