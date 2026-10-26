/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   nvme.c
 * @brief  NVMe block driver: controller enable, admin/IO queue pairs, PRP
 *         transfers, and completions delivered as MSI-X over the ITS.
 *
 * The queue geometry, the PRP rules, the doorbell order and the cache
 * discipline are not optional:
 *
 *   - A transfer is split so that PRP1 plus PRP2 can describe it (at most two
 *     pages); anything else goes through a page-aligned bounce buffer, which
 *     also covers caller buffers that are not 4-byte aligned or not a whole
 *     number of blocks. A PRP pair that does not describe the buffer sends the
 *     device to the wrong memory without any error.
 *   - The submission doorbell announces the tail *after* the entry is written
 *     (advance first, then ring), with a dsb between the entry/buffer stores
 *     and the doorbell. Getting this wrong lets the controller fetch a stale
 *     entry.
 *   - A completion is read only after invalidating its cache line, and its
 *     phase tag is compared against the expected phase - the tag inverts on
 *     every pass of the ring, so a stale cached line reads as "no completion
 *     yet" forever or as a completion that never happened.
 *   - CC.EN=0 resets controller state, so the sequence is: enable and wait for
 *     CSTS.RDY, only then arm MSI-X, only then create the I/O queue pair with
 *     interrupts enabled (IEN=1, vector 0). Admin commands and any path
 *     without message interrupts stay polled.
 *   - Completion bookkeeping belongs to the submitter: the interrupt handler
 *     records that a completion arrived, the thread that submitted consumes
 *     the entry and rings the CQ doorbell. A handler that advanced the head
 *     would race the submitter's own copy of head/phase.
 *
 * PCI access goes through board/firefly/drivers/pcie_ecam.h (config access
 * plus a flat device list). MSI-X is programmed by
 * board/firefly/drivers/pcie_msix.c and the ITS mapping comes from the board's MSI domain (arch/aarch64/gicv3_msi.c) through
 * arch/aarch64/msi.h. The completion wait is "poll the CQE phase, sleeping one
 * tick between rounds" with the handler only bumping a counter: this project's
 * ISR-safe wake-up (osThreadFlagsSetFromISR) is adapter-scoped and the waiter
 * here is the calling task inside the driver. The interrupt is still what makes
 * the wait cheap - the counter it bumps is also the evidence that message
 * interrupts arrived at all, which completion alone cannot show (the device
 * posts the CQE whether or not the interrupt was delivered). The queue arrays
 * are per controller: sizing both I/O queue arrays with the admin depth and
 * sharing one set across controllers would alias with two controllers.
 *
 * @author zhugengyu
 * @date   16.09.2026
 */

#include <stdint.h>
#include <string.h>

#include "Driver_Common.h"
#include "arch_asm.h"
#include "cache.h"
#include "cmsis_os2.h"
#include "irq_ctrl.h"
#include "log.h"
#include "nvme.h"
#include "regs.h"

#include "nvme_internal.h"
#include "nvme_regs.h"

#include "../pcie_ecam.h"
#include "../pcie_msix.h"

#define NVME_PAGE_SIZE 4096
#define NVME_ADMIN_QD  8
#define NVME_IO_QD     8
/* A command covers at most two pages: PRP1 up to the end of its page plus one
 * page-aligned PRP2, no PRP list needed. */
#define NVME_MAX_CHUNK (2 * NVME_PAGE_SIZE)

/* Poll loops are bounded busy waits, the pattern used by the other on-board
 * drivers. */
#define NVME_READY_TRIES 400000000u
#define NVME_CMD_TRIES	 50000000u

/* Interrupt-mode completion wait: rounds of one tick each, only exhausted when
 * the device stops completing commands (~1 s at the project's tick rate). */
#define NVME_IRQ_WAIT_ROUNDS 1000u

#define NVME_DEFAULT_NSID 1

struct nvme_ctrl {
	const struct pcie_ecam_func *pdev;
	uint32_t index;		/* slot in nvme_ctrls / nvme_q */
	uintptr_t regs;
	uint32_t dbr_stride;	/* byte stride of a doorbell register */
	uint32_t nsid;
	uint64_t ns_blocks;
	uint32_t lba_size;

	int irq_mode;
	uint32_t irq_intid;
	/* Bumped by the interrupt handler, read by the submitter and by the
	 * status command: the only way to tell "message interrupts were
	 * delivered" apart from "the device completed anyway". */
	volatile uint32_t irq_count;
	/* Direction of the command in flight, for the completion event the
	 * handler raises (it deliberately does not read the CQE). */
	volatile uint8_t irq_opcode;
	/* Set once if a completion ever arrived with no interrupt delivery, so
	 * the note is printed once per controller rather than per command. */
	volatile uint8_t irq_silent;
	/* Set once when a delivery has been observed, for one positive line. */
	volatile uint8_t irq_first_logged;
	NVME_SignalEvent_t cb_event;

	uint16_t admin_sq_tail;
	uint16_t admin_cq_head;
	uint8_t admin_cq_phase;

	uint16_t io_sq_tail;
	uint16_t io_cq_head;
	uint8_t io_cq_phase;

	/* Completions consumed, reported by GetTransferStatus() and reset by
	 * the next call. */
	uint32_t completions;

	uint8_t present;
};

static struct nvme_ctrl nvme_ctrls[NVME_CTRL_MAX];
static uint32_t nvme_ctrls_found;
static int nvme_probed;

/* DMA areas, one set per controller. The queue arrays must each be page
 * aligned (the admin queues by the specification, the I/O queues because that
 * is what keeps PRP1 single-entry); member alignment plus the compiler's
 * padding gives each of them its own page. */
struct nvme_queues {
	uint8_t admin_sq[NVME_ADMIN_QD * 64] __attribute__((aligned(NVME_PAGE_SIZE)));
	uint8_t admin_cq[NVME_ADMIN_QD * 16] __attribute__((aligned(NVME_PAGE_SIZE)));
	uint8_t io_sq[NVME_IO_QD * 64] __attribute__((aligned(NVME_PAGE_SIZE)));
	uint8_t io_cq[NVME_IO_QD * 16] __attribute__((aligned(NVME_PAGE_SIZE)));
	uint8_t ident[NVME_PAGE_SIZE] __attribute__((aligned(NVME_PAGE_SIZE)));
	uint8_t bounce[NVME_MAX_CHUNK] __attribute__((aligned(NVME_PAGE_SIZE)));
};

static struct nvme_queues nvme_q[NVME_CTRL_MAX]
	__attribute__((aligned(NVME_PAGE_SIZE)));

/* The kernel runs identity mapped and the board has no SMMU in the PCIe path,
 * so a DMA address is the virtual address itself. */
static uint64_t nvme_va2pa(const void *va)
{
	return (uint64_t)(uintptr_t)va;
}

static struct nvme_ctrl *nvme_ctrl_from_index(uint32_t ctrl)
{
	if (ctrl >= NVME_CTRL_MAX || !nvme_ctrls[ctrl].present) {
		return NULL;
	}

	return &nvme_ctrls[ctrl];
}

/* --- queue mechanics ------------------------------------------------------ */

static int32_t nvme_wait_ready(struct nvme_ctrl *c, int ready)
{
	uint32_t tries = NVME_READY_TRIES;

	while (tries-- > 0) {
		uint32_t rdy = reg_rd32(c->regs + NVME_CSTS) & NVME_CSTS_RDY;

		if ((rdy != 0) == (ready != 0)) {
			return ARM_DRIVER_OK;
		}
	}

	return ARM_DRIVER_ERROR_TIMEOUT;
}

static void nvme_ring_sq_doorbell(struct nvme_ctrl *c, int io, uint16_t tail)
{
	uint16_t qid = io ? 1 : 0;

	ARCH_DSB();
	reg_wr32(NVME_DBR_SQ(c->regs, c->dbr_stride, qid), tail);
}

static void nvme_ring_cq_doorbell(struct nvme_ctrl *c, int io, uint16_t head)
{
	uint16_t qid = io ? 1 : 0;

	/* dmb, as in the original: nothing here depends on a later store being
	 * visible to a device that is only being told where the hardware is. */
	ARCH_DSB();
	reg_wr32(NVME_DBR_CQ(c->regs, c->dbr_stride, qid), head);
}

static volatile struct nvme_completion *nvme_cq_entry(struct nvme_ctrl *c,
						      int io, uint16_t head)
{
	uint8_t *cq = io ? nvme_q[c->index].io_cq
			 : nvme_q[c->index].admin_cq;

	return (volatile struct nvme_completion *)(cq + head * 16);
}

/* Submit one command on the given queue pair and wait for its completion.
 * Exactly one command is in flight at a time. */
static int32_t nvme_submit(struct nvme_ctrl *c, int io, struct nvme_command *cmd,
			   void *buf, size_t len)
{
	struct nvme_queues *q = &nvme_q[c->index];
	uint8_t *sq = io ? q->io_sq : q->admin_sq;
	uint16_t *tail = io ? &c->io_sq_tail : &c->admin_sq_tail;
	uint16_t *head = io ? &c->io_cq_head : &c->admin_cq_head;
	uint8_t *phase = io ? &c->io_cq_phase : &c->admin_cq_phase;
	volatile struct nvme_completion *cqe;
	uint32_t tries;

	cmd->cid = 0;

	memcpy(sq + *tail * 64, cmd, sizeof(*cmd));
	cache_dcache_flush((uintptr_t)(sq + *tail * 64), 64);
	if (buf != NULL) {
		if (cmd->opcode == NVME_OPC_IO_WRITE) {
			cache_dcache_flush((uintptr_t)buf, len);
		} else {
			/* The endpoint owns the buffer until the completion:
			 * drop the stale lines before it DMAs and again before
			 * reading the data. */
			cache_dcache_invalidate((uintptr_t)buf, len);
		}
	}
	ARCH_DSB();

	/* The doorbell announces the tail one past the entry just written:
	 * advance first, then ring. The direction is recorded for the completion
	 * event the handler raises (it does not read the CQE). */
	c->irq_opcode = cmd->opcode;
	*tail = (uint16_t)((*tail + 1) % (io ? NVME_IO_QD : NVME_ADMIN_QD));
	nvme_ring_sq_doorbell(c, io, *tail);

	cqe = nvme_cq_entry(c, io, *head);
	if (io && c->irq_mode) {
		uint32_t rounds = 0;
		uint32_t irqs = c->irq_count;

		/* Fast path first: the device usually completes within the
		 * microseconds before this thread gets to sleep. Then one tick
		 * per round - the interrupt makes the wait cheap, but the
		 * completion itself is detected in the CQE, which the device
		 * writes whether or not its interrupt was delivered. That is
		 * why the delivery count, not the completion, is the evidence
		 * that message interrupts work. */
		for (;;) {
			cache_dcache_invalidate((uintptr_t)cqe, sizeof(*cqe));
			if (NVME_CQE_PHASE(cqe) == *phase) {
				break;
			}
			if (rounds++ >= NVME_IRQ_WAIT_ROUNDS) {
				board_log("nvme: completion timed out (opcode %#x,"
					  " irq %u, deliveries %u)\n",
					  cmd->opcode, c->irq_intid, c->irq_count);
				return ARM_DRIVER_ERROR_TIMEOUT;
			}
			osDelay(1);
		}
		if (c->irq_count == irqs && c->irq_silent == 0) {
			/* The completion arrived without a delivery during this
			 * command: the interrupt is either not reaching this
			 * core yet or is slower than the poll. Worth one line
			 * per controller, not one per command. */
			c->irq_silent = 1;
			board_log("nvme%u: completion without a message interrupt"
				  " delivery (intid %u, deliveries %u)\n",
				  c->index, c->irq_intid, c->irq_count);
		} else if (c->irq_count != irqs && c->irq_first_logged == 0) {
			/* Positive evidence, printed once and from task context:
			 * an interrupt really was delivered for this completion. */
			c->irq_first_logged = 1;
			board_log("nvme%u: message interrupts delivered (intid %u,"
				  " %u so far)\n",
				  c->index, c->irq_intid, c->irq_count);
		}

		/* The interrupt handler only records the delivery; the
		 * submitter advances the completion queue and releases the
		 * slot back to the device. */
		*head = (uint16_t)((*head + 1) % (io ? NVME_IO_QD : NVME_ADMIN_QD));
		if (*head == 0) {
			/* The phase tag inverts on every pass of the ring */
			*phase ^= 1;
		}
		nvme_ring_cq_doorbell(c, io, *head);
	} else {
		for (tries = 0; tries < NVME_CMD_TRIES; tries++) {
			cache_dcache_invalidate((uintptr_t)cqe, sizeof(*cqe));
			if (NVME_CQE_PHASE(cqe) == *phase) {
				break;
			}
		}
		if (tries == NVME_CMD_TRIES) {
			board_log("nvme: command %#x timed out (phase %u head %u"
				  " tail %u)\n", cmd->opcode, *phase, *head, *tail);
			board_log("nvme: cqe %08x %08x %08x %08x\n", cqe->result,
				  cqe->reserved,
				  ((uint32_t)cqe->sq_head << 16) | cqe->sq_id,
				  ((uint32_t)cqe->cid << 16) | cqe->status);
			board_log("nvme: csts %08x aqa %08x asq %08x%08x acq"
				  " %08x%08x stride %u\n",
				  reg_rd32(c->regs + NVME_CSTS),
				  reg_rd32(c->regs + NVME_AQA),
				  reg_rd32(c->regs + NVME_ASQ + 4),
				  reg_rd32(c->regs + NVME_ASQ),
				  reg_rd32(c->regs + NVME_ACQ + 4),
				  reg_rd32(c->regs + NVME_ACQ), c->dbr_stride);
			return ARM_DRIVER_ERROR_TIMEOUT;
		}

		*head = (uint16_t)((*head + 1) % (io ? NVME_IO_QD : NVME_ADMIN_QD));
		if (*head == 0) {
			/* The phase tag inverts on every pass of the ring */
			*phase ^= 1;
		}
		nvme_ring_cq_doorbell(c, io, *head);
	}

	c->completions++;

	if (!NVME_CQE_OK(cqe)) {
		board_log("nvme: command %#x failed, status %#x\n", cmd->opcode,
			  cqe->status);
	}

	if (buf != NULL && cmd->opcode != NVME_OPC_IO_WRITE) {
		cache_dcache_invalidate((uintptr_t)buf, len);
	}

	return NVME_CQE_OK(cqe) ? ARM_DRIVER_OK : ARM_DRIVER_ERROR;
}

static int32_t nvme_admin(struct nvme_ctrl *c, struct nvme_command *cmd,
			  void *buf, size_t len)
{
	return nvme_submit(c, 0, cmd, buf, len);
}

static int32_t nvme_create_io_queue(struct nvme_ctrl *c, uint8_t opcode,
				    uint32_t cdw11)
{
	struct nvme_queues *q = &nvme_q[c->index];
	struct nvme_command cmd = {0};
	uint8_t *ring = (opcode == NVME_OPC_CREATE_CQ) ? q->io_cq : q->io_sq;

	cmd.opcode = opcode;
	cmd.prp1 = nvme_va2pa(ring);
	cmd.cdw10 = ((NVME_IO_QD - 1) << 16) | 1; /* size | qid 1 */
	cmd.cdw11 = cdw11;

	return nvme_admin(c, &cmd, NULL, 0);
}

/* Enable the controller and bring up the admin queue pair. The CC.EN=0
 * transition resets controller state, so message interrupt resources must only
 * be armed after this has completed - the proven bare-metal setup arms MSI-X
 * strictly after the controller reports ready. */
static int32_t nvme_ctrl_enable(struct nvme_ctrl *c)
{
	struct nvme_queues *q = &nvme_q[c->index];
	uint64_t cap = reg_rd64(c->regs + NVME_CAP);
	uint32_t cc;
	int32_t ret;

	if (NVME_CAP_MQES(cap) < NVME_ADMIN_QD - 1) {
		board_log("nvme: MQES %u too small\n", (unsigned)NVME_CAP_MQES(cap));
		return ARM_DRIVER_ERROR_PARAMETER;
	}
	c->dbr_stride = 4u << NVME_CAP_DSTRD(cap);

	/* Disable and wait for not-ready */
	reg_wr32(c->regs + NVME_CC, 0);
	ret = nvme_wait_ready(c, 0);
	if (ret != ARM_DRIVER_OK) {
		board_log("nvme: controller did not leave ready state\n");
		return ret;
	}

	reg_wr32(c->regs + NVME_AQA,
		 ((NVME_ADMIN_QD - 1) << 16) | (NVME_ADMIN_QD - 1));
	reg_wr64(c->regs + NVME_ASQ, nvme_va2pa(q->admin_sq));
	reg_wr64(c->regs + NVME_ACQ, nvme_va2pa(q->admin_cq));

	c->admin_sq_tail = 0;
	c->admin_cq_head = 0;
	c->admin_cq_phase = 1; /* entries start with a zero phase tag */
	c->io_sq_tail = 0;
	c->io_cq_head = 0;

	cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS(NVME_CAP_MPSMIN(cap)) |
	     NVME_CC_IOSQES6 | NVME_CC_IOCQES4;
	reg_wr32(c->regs + NVME_CC, cc);

	ret = nvme_wait_ready(c, 1);
	if (ret != ARM_DRIVER_OK) {
		board_log("nvme: controller enable timed out (CSTS 0x%08x)\n",
			  reg_rd32(c->regs + NVME_CSTS));
		return ret;
	}

	return ARM_DRIVER_OK;
}

/* Create the I/O queue pair; called after the message interrupt resources are
 * armed so the CQ is born with interrupts enabled. */
static int32_t nvme_io_queues_setup(struct nvme_ctrl *c)
{
	/* Interrupts on the I/O completion queue only, and only vector 0 (IV
	 * in cdw11 stays 0); the admin queue stays polled, it completes before
	 * I/O ever starts. */
	int32_t ret = nvme_create_io_queue(c, NVME_OPC_CREATE_CQ,
					   c->irq_mode ? 0x3 /* PC, IEN=1 */
						       : 0x1 /* PC, IEN=0 */);
	if (ret != ARM_DRIVER_OK) {
		return ret;
	}
	ret = nvme_create_io_queue(c, NVME_OPC_CREATE_SQ,
				   0x10003 /* PC, medium prio, cqid 1 */);
	if (ret != ARM_DRIVER_OK) {
		return ret;
	}
	c->io_cq_phase = 1;

	return ARM_DRIVER_OK;
}

/* Issue IDENTIFY (CNS 0 or 1) into the controller's identify page. */
static int32_t nvme_identify(struct nvme_ctrl *c, uint32_t cns)
{
	struct nvme_queues *q = &nvme_q[c->index];
	struct nvme_command cmd = {0};

	cmd.opcode = NVME_OPC_IDENTIFY;
	/* NSID belongs to the namespace identify only: the controller-level
	 * identify (CNS=1) requires NSID=0, and this SSD's firmware rejects
	 * NSID=1 there (board-proven 2026-09-21, status 5). */
	cmd.nsid = (cns == NVME_CNS_NAMESPACE) ? c->nsid : 0U;
	cmd.prp1 = nvme_va2pa(q->ident);
	cmd.cdw10 = cns;

	cache_dcache_invalidate((uintptr_t)q->ident, NVME_PAGE_SIZE);
	if (nvme_admin(c, &cmd, q->ident, NVME_PAGE_SIZE) != ARM_DRIVER_OK) {
		return ARM_DRIVER_ERROR;
	}

	return ARM_DRIVER_OK;
}

static int32_t nvme_identify_namespace(struct nvme_ctrl *c)
{
	struct nvme_queues *q = &nvme_q[c->index];
	uint64_t nsze;
	uint8_t lbaf;
	uint8_t lbads;

	if (nvme_identify(c, NVME_CNS_NAMESPACE) != ARM_DRIVER_OK) {
		return ARM_DRIVER_ERROR;
	}

	memcpy(&nsze, q->ident + NVME_ID_NS_NSZE_OFF, sizeof(nsze));
	lbaf = q->ident[NVME_ID_NS_LBAF_OFF] & 0xf;
	lbads = q->ident[NVME_ID_NS_LBAFS_OFF + 4 * lbaf + 2];
	if (lbads < 9 || lbads > 31) {
		board_log("nvme: bad LBADS %u\n", lbads);
		return ARM_DRIVER_ERROR;
	}
	c->ns_blocks = nsze;
	c->lba_size = 1u << lbads;

	return ARM_DRIVER_OK;
}

static int nvme_prp_fit(const void *buf, size_t len)
{
	uintptr_t pa = (uintptr_t)buf;
	size_t first = NVME_PAGE_SIZE - (pa & (NVME_PAGE_SIZE - 1));

	return len <= first + NVME_PAGE_SIZE;
}

/* Single NVMe read/write command of at most NVME_MAX_CHUNK bytes. Falls back
 * to the bounce buffer when the caller buffer cannot be described by PRP1/PRP2
 * or is not a whole number of blocks. */
static int32_t nvme_rw_chunk(struct nvme_ctrl *c, uint8_t opcode, void *buf,
			     size_t len, uint64_t lba)
{
	struct nvme_queues *q = &nvme_q[c->index];
	struct nvme_command cmd = {0};
	void *xfer = buf;
	size_t xfer_len = (len + c->lba_size - 1) & ~((size_t)c->lba_size - 1);
	int bounced = 0;
	int32_t ret;

	if (xfer_len > NVME_MAX_CHUNK) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}
	if ((len & (c->lba_size - 1)) != 0 || ((uintptr_t)buf & 3) != 0 ||
	    !nvme_prp_fit(buf, xfer_len)) {
		xfer = q->bounce;
		bounced = 1;
		if (opcode == NVME_OPC_IO_WRITE) {
			memcpy(xfer, buf, len);
			/* The padded tail of the last block must not leak
			 * stale bounce buffer content to disk. */
			memset((char *)xfer + len, 0, xfer_len - len);
		}
	}

	cmd.opcode = opcode;
	cmd.nsid = c->nsid;
	cmd.prp1 = nvme_va2pa(xfer);
	if (xfer_len > NVME_PAGE_SIZE - ((uintptr_t)xfer & (NVME_PAGE_SIZE - 1))) {
		cmd.prp2 = nvme_va2pa((void *)(((uintptr_t)xfer + NVME_PAGE_SIZE) &
					       ~(uintptr_t)(NVME_PAGE_SIZE - 1)));
	}
	cmd.cdw10 = (uint32_t)lba;
	cmd.cdw11 = (uint32_t)(lba >> 32);
	cmd.cdw12 = (uint32_t)(xfer_len / c->lba_size - 1); /* NLB */

	ret = nvme_submit(c, 1, &cmd, xfer, xfer_len);

	if (bounced && ret == ARM_DRIVER_OK && opcode == NVME_OPC_IO_READ) {
		memcpy(buf, xfer, len);
	}

	return ret;
}

/* Copy a block range in or out, one command-sized chunk at a time. */
static int32_t nvme_rw(struct nvme_ctrl *c, uint8_t opcode, uint64_t lba,
		       void *data, uint32_t bytes)
{
	uint8_t *buf = (uint8_t *)data;
	uint32_t done = 0;

	if (data == NULL || bytes == 0 || (bytes % c->lba_size) != 0 ||
	    lba + bytes / c->lba_size > c->ns_blocks) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	while (done < bytes) {
		uint32_t chunk = bytes - done;
		int32_t ret;

		if (chunk > NVME_MAX_CHUNK) {
			chunk = NVME_MAX_CHUNK;
		}
		ret = nvme_rw_chunk(c, opcode, buf + done, chunk,
				    lba + done / c->lba_size);
		if (ret != ARM_DRIVER_OK) {
			return ret;
		}
		done += chunk;
	}

	return ARM_DRIVER_OK;
}

/* --- interrupts ----------------------------------------------------------- */

/* The handler records the delivery and nothing else. Completion bookkeeping
 * (consuming the CQE, advancing the head, ringing the CQ doorbell) belongs to
 * the submitter: doing it here would race the submitter's own head/phase. The
 * device posts the completion into the CQ before it raises the message
 * interrupt, so the entry is already visible in memory by the time this runs. */
static void nvme_irq_handle(struct nvme_ctrl *c)
{
	if (c->present == 0) {
		return;
	}

	c->irq_count++;

	if (c->cb_event != NULL) {
		/* The frozen interface's completion event, raised where it says
		 * it is raised (interrupt context). The event says "something
		 * completed" in the direction of the command in flight; whether
		 * it succeeded is the submitter's return value, since the
		 * handler deliberately does not read the CQE. */
		uint32_t event = (c->irq_opcode == NVME_OPC_IO_WRITE)
					 ? NVME_EVENT_WRITE_DONE
					 : NVME_EVENT_READ_DONE;

		c->cb_event(event, c->index);
	}
}

/* One thunk per controller: CMSIS IRQ handlers take no argument, the same
 * reason the GMAC driver generates one per port. */
static void nvme_irq0(void) { nvme_irq_handle(&nvme_ctrls[0]); }
static void nvme_irq1(void) { nvme_irq_handle(&nvme_ctrls[1]); }

static void (*const nvme_irq_thunks[NVME_CTRL_MAX])(void) = {
	nvme_irq0,
	nvme_irq1,
};

/* Ask the PCIe layer for message vectors; one vector drives the I/O
 * completion queue. Failure keeps the driver in polled mode. */
static void nvme_setup_irqs(struct nvme_ctrl *c, uint32_t ctrl)
{
	const struct pcie_ecam_func *pdev = c->pdev;
	MSI_VECTOR vectors[1];
	int32_t nvec;

	nvec = pcie_msix_alloc(pdev, 1, vectors);
	if (nvec < 1) {
		board_log("nvme: no message vectors available, staying polled\n");
		return;
	}

	if (IRQ_SetHandler((IRQn_ID_t)vectors[0].intid, nvme_irq_thunks[ctrl]) != 0) {
		board_log("nvme: IRQ_SetHandler failed for intid %u, staying polled\n",
			  vectors[0].intid);
		(void)pcie_msix_release(pdev);
		return;
	}
	/* No IRQ_SetPriority: an LPI's priority lives in the ITS property
	 * table and IRQ_SetPriority refuses LPIs by design. */
	(void)IRQ_Enable((IRQn_ID_t)vectors[0].intid);

	c->irq_intid = vectors[0].intid;
	c->irq_mode = 1;

	board_log("nvme: %d message vector(s), completion intid %u\n",
		  (int)nvec, c->irq_intid);
}

/* --- probe ---------------------------------------------------------------- */

static int32_t nvme_probe_one(struct nvme_ctrl *c, uint32_t ctrl,
			      const struct pcie_ecam_func *pdev)
{
	uint32_t pci_cmd;
	uintptr_t bar0 = pdev->bars[0].base;

	memset(c, 0, sizeof(*c));
	c->pdev = pdev;
	c->index = ctrl;
	c->nsid = NVME_DEFAULT_NSID;

	if (bar0 == 0 || bar0 == (uintptr_t)0xffffffffu) {
		board_log("nvme: %04x:%04x has no BAR0\n", pdev->vendor_id,
			  pdev->device_id);
		return ARM_DRIVER_ERROR;
	}
	c->regs = bar0 & ~(uintptr_t)0xf;

	/* Enable memory space decoding and bus mastering */
	if (pcie_ecam_cfg_read(pdev->bus, pdev->dev, pdev->fn, PCI_CFG_COMMAND, 2,
			      &pci_cmd) != ARM_DRIVER_OK) {
		return ARM_DRIVER_ERROR;
	}
	pci_cmd |= PCI_CMD_MEMORY | PCI_CMD_MASTER;
	(void)pcie_ecam_cfg_write(pdev->bus, pdev->dev, pdev->fn, PCI_CFG_COMMAND, 2,
				 pci_cmd);

	/* Enable the controller first: arming message interrupts while the
	 * controller is disabled lets the CC.EN reset tear down the freshly
	 * programmed endpoint state behind our back. */
	if (nvme_ctrl_enable(c) != ARM_DRIVER_OK) {
		return ARM_DRIVER_ERROR;
	}
	nvme_setup_irqs(c, ctrl);
	if (nvme_io_queues_setup(c) != ARM_DRIVER_OK) {
		board_log("nvme: I/O queue setup failed\n");
		return ARM_DRIVER_ERROR;
	}
	if (nvme_identify_namespace(c) != ARM_DRIVER_OK) {
		return ARM_DRIVER_ERROR;
	}

	c->present = 1;

	board_log("nvme%u: %04x:%04x at 0x%lx, ns%u: %llu blocks of %u bytes"
		  " (%llu MiB)\n",
		  ctrl, pdev->vendor_id, pdev->device_id, (unsigned long)c->regs, c->nsid,
		  (unsigned long long)c->ns_blocks, c->lba_size,
		  (unsigned long long)(c->ns_blocks * c->lba_size >> 20));

	return ARM_DRIVER_OK;
}

void nvme_init(void)
{
	uint32_t i;
	uint32_t found = 0;

	if (nvme_probed) {
		return;
	}
	nvme_probed = 1;

	/* The PCIe backend is idempotent; bringing it up here is what makes
	 * this driver's own init the only entry point a caller needs. */
	pcie_ecam_init();

	for (i = 0; i < pcie_ecam_dev_count(); i++) {
		const struct pcie_ecam_func *dev = pcie_ecam_dev(i);

		if (found >= NVME_CTRL_MAX) {
			break;
		}
		if (((dev->class_code >> 16) & 0xffu) != PCI_CLASS_STORAGE ||
		    ((dev->class_code >> 8) & 0xffu) != PCI_SUBCLASS_NVME) {
			continue;
		}

		if (nvme_probe_one(&nvme_ctrls[found], found, dev) == ARM_DRIVER_OK) {
			found++;
		}
	}

	nvme_ctrls_found = found;

	if (found == 0) {
		board_log("nvme: no NVMe controller found\n");
	}
}

uint32_t nvme_ctrl_count(void)
{
	return nvme_ctrls_found;
}

uint32_t nvme_irq_count(uint32_t ctrl)
{
	if (ctrl >= NVME_CTRL_MAX) {
		return 0;
	}

	return nvme_ctrls[ctrl].irq_count;
}

/* --- CMSIS ARM_DRIVER_NVME (drivers/nvme/nvme.h) --------------------------- */

static ARM_DRIVER_VERSION nvme_get_version(void)
{
	return (ARM_DRIVER_VERSION){ .api = NVME_API_VERSION, .drv = 0x0100 };
}

static NVME_CAPABILITIES nvme_get_capabilities(uint32_t ctrl)
{
	NVME_CAPABILITIES caps;
	struct nvme_ctrl *c = nvme_ctrl_from_index(ctrl);

	memset(&caps, 0, sizeof(caps));
	if (c == NULL) {
		return caps;
	}

	caps.sector_count = c->ns_blocks;
	caps.sector_size = c->lba_size;
	caps.max_transfer = NVME_MAX_CHUNK;
	caps.queue_depth = NVME_IO_QD;
	caps.timeout_ms = (uint32_t)NVME_CAP_TO(reg_rd64(c->regs + NVME_CAP)) * 500u;

	return caps;
}

static int32_t nvme_initialize(uint32_t ctrl, NVME_SignalEvent_t cb_event)
{
	nvme_init();

	if (nvme_ctrl_from_index(ctrl) == NULL) {
		return ARM_DRIVER_ERROR;
	}
	nvme_ctrls[ctrl].cb_event = cb_event;

	return ARM_DRIVER_OK;
}

static int32_t nvme_uninitialize(uint32_t ctrl)
{
	struct nvme_ctrl *c = nvme_ctrl_from_index(ctrl);

	if (c == NULL) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	if (c->irq_mode) {
		(void)IRQ_Disable((IRQn_ID_t)c->irq_intid);
		(void)pcie_msix_release(c->pdev);
		c->irq_mode = 0;
	}
	c->cb_event = NULL;

	return ARM_DRIVER_OK;
}

static int32_t nvme_power_control(uint32_t ctrl, uint32_t state)
{
	if (nvme_ctrl_from_index(ctrl) == NULL) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}
	if (state != ARM_POWER_FULL) {
		return ARM_DRIVER_ERROR_UNSUPPORTED;
	}

	return ARM_DRIVER_OK;
}

static int32_t nvme_identify_controller(uint32_t ctrl, char *model,
					uint32_t model_size)
{
	struct nvme_ctrl *c = nvme_ctrl_from_index(ctrl);
	struct nvme_queues *q;
	uint32_t n;
	uint32_t i;

	if (c == NULL || model == NULL || model_size == 0) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}
	q = &nvme_q[c->index];

	if (nvme_identify(c, NVME_CNS_CONTROLLER) != ARM_DRIVER_OK) {
		return ARM_DRIVER_ERROR;
	}

	/* The model number field is 40 bytes of space-padded ASCII; hand back
	 * a trimmed, terminated string. */
	n = (model_size - 1u < NVME_ID_CTRL_MODEL_LEN) ? model_size - 1u
						      : NVME_ID_CTRL_MODEL_LEN;
	for (i = 0; i < n; i++) {
		char ch = (char)q->ident[NVME_ID_CTRL_MODEL_OFF + i];

		model[i] = (ch == '\0') ? ' ' : ch;
	}
	while (n > 0 && (model[n - 1] == ' ' || model[n - 1] == '\0')) {
		n--;
	}
	model[n] = '\0';

	return ARM_DRIVER_OK;
}

static int32_t nvme_read_blocks(uint32_t ctrl, uint64_t lba, void *data,
				uint32_t block_count)
{
	struct nvme_ctrl *c = nvme_ctrl_from_index(ctrl);

	if (c == NULL) {
		return ARM_DRIVER_ERROR;
	}

	return nvme_rw(c, NVME_OPC_IO_READ, lba, data,
		       block_count * c->lba_size);
}

static int32_t nvme_write_blocks(uint32_t ctrl, uint64_t lba, const void *data,
				 uint32_t block_count)
{
	struct nvme_ctrl *c = nvme_ctrl_from_index(ctrl);

	if (c == NULL) {
		return ARM_DRIVER_ERROR;
	}

	return nvme_rw(c, NVME_OPC_IO_WRITE, lba, (void *)data,
		       block_count * c->lba_size);
}

/* NVMe FLUSH (opcode 0x00 on the I/O queue): a real command, added for the
 * frozen interface's Flush. */
static int32_t nvme_flush(uint32_t ctrl)
{
	struct nvme_ctrl *c = nvme_ctrl_from_index(ctrl);
	struct nvme_command cmd = {0};

	if (c == NULL) {
		return ARM_DRIVER_ERROR;
	}

	cmd.opcode = NVME_OPC_IO_FLUSH;
	cmd.nsid = c->nsid;

	return nvme_submit(c, 1, &cmd, NULL, 0);
}

static uint32_t nvme_get_transfer_status(uint32_t ctrl)
{
	struct nvme_ctrl *c = nvme_ctrl_from_index(ctrl);
	uint32_t done;

	if (c == NULL) {
		return 0;
	}

	done = c->completions;
	c->completions = 0;

	return done;
}

ARM_DRIVER_NVME Driver_NVME = {
	nvme_get_version,
	nvme_get_capabilities,
	nvme_initialize,
	nvme_uninitialize,
	nvme_power_control,
	nvme_identify_controller,
	nvme_read_blocks,
	nvme_write_blocks,
	nvme_flush,
	nvme_get_transfer_status,
};