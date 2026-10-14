/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * main.c - the linuxkpi SDIO host mock suite: the assertion face.
 *
 * One process, one run: the synthetic card is installed behind the engine's
 * backend seam, the engine brings the card up and builds the function objects,
 * drivers register through the interface's own entry point, and every case
 * asserts as it goes. The anchors it prints (prefix "sdio-test: ") are the
 * machine-checkable result; the exit code is zero only when every assertion
 * held.
 *
 * Cases (the groups the suite covers):
 *   card-up            the card came up and the identity matches the model
 *   driver-before      a driver registered before the card did not probe
 *   driver-match       a matching driver registered afterwards probed at once
 *   driver-claim-once  a second matching driver did not re-claim the function
 *   readb-writeb       one byte written and read back, and function-0 addressing
 *   word-access        the 2-byte and 4-byte calls, byte order and the scratch path
 *   memcpy-split       the transfer splitter: block chunks, the remainder, incr
 *   block-size         the block-size rules and the register bytes they write
 *   enable-disable     the function power-up sequence
 *   claim-serial       two threads claim without overlapping; I/O does not self-claim
 *   irq-pump           pending dispatch under the claim, and release stops delivery
 *   pump-policy        the cadence knob round-trips and defaults to the fixed shape
 *   pump-adaptive      an active card is serviced back-to-back, an idle one backs off
 *   current-identity   one stable task object per thread, and the pump thread's own
 *   init-entries       the module_init section runs in order, once
 *   section-coexist    the module_init section did not disturb the usb driver section
 *   error-mapping      the seam's failures arrive as the interface's errno
 *
 * Modes: the default run above; "--fault expect-tamper" makes one expectation
 * deliberately wrong and must go red; "--fault lying-card" makes the card answer
 * inverted bytes and the readback case must go red.
 *
 * @design composition over the sibling mock suites - ref
 *         tests/host/linuxkpi-usb-test/main.c (a scripted responder behind the
 *         seam, anchors, a named fault invocation) and
 *         tests/host/fsl-sdmmc-adapter-test/main.c (one case per seam mapping,
 *         with the host's own discipline asserted call by call); the runtime
 *         lifecycle (task start, scheduler end) follows
 *         tests/host/freertos-cmsis2-smoke/main.c.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include <linux/sched.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "lkpi_sdio_backend.h"
#include "linuxkpi_sdio.h"
#include "mock_sdio_card.h"

/* ------------------------------------------------------------------ */
/* the run's bookkeeping                                               */
/* ------------------------------------------------------------------ */

static int failures;
static const char *g_fault;
static int g_tamper;

static void
case_report(const char *id, int ok, const char *why)
{

	printf("sdio-test: %s ok=%d %s\n", id, ok ? 1 : 0, why != NULL ? why : "");
	fflush(stdout);
	if (!ok)
		failures++;
}

static void
info(const char *fmt, ...)
{
	va_list ap;

	printf("sdio-test: ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* the driver stand-in                                                 */
/* ------------------------------------------------------------------ */

static int probe_a_calls;
static int probe_b_calls;
static int probe_c_calls;
static unsigned int probe_b_func;
static unsigned int probe_b_vendor;
static unsigned int probe_b_device;
static unsigned int probe_b_class;

static int
probe_a(struct sdio_func *func, const struct sdio_device_id *id)
{

	(void)func;
	(void)id;
	probe_a_calls++;
	return 0;
}

static int
probe_b(struct sdio_func *func, const struct sdio_device_id *id)
{

	(void)id;
	probe_b_calls++;
	probe_b_func = func->num;
	probe_b_vendor = func->vendor;
	probe_b_device = func->device;
	probe_b_class = func->class;
	return 0;
}

static int
probe_c(struct sdio_func *func, const struct sdio_device_id *id)
{

	(void)func;
	(void)id;
	probe_c_calls++;
	return 0;
}

/* A names a part this card is not; B names the part it is; C matches on the
 * standard interface class alone. */
static const struct sdio_device_id ids_a[] = {
	{ SDIO_DEVICE(0x1234, 0x0001) },
	{ },
};

static const struct sdio_device_id ids_b[] = {
	{ SDIO_DEVICE(0x024c, 0xf179) },
	{ },
};

static const struct sdio_device_id ids_c[] = {
	{ SDIO_DEVICE_CLASS(SDIO_CLASS_WLAN) },
	{ },
};

static struct sdio_driver drv_a = { .name = "sdio-stub-a", .id_table = ids_a, .probe = probe_a };
static struct sdio_driver drv_b = { .name = "sdio-stub-b", .id_table = ids_b, .probe = probe_b };
static struct sdio_driver drv_c = { .name = "sdio-stub-c", .id_table = ids_c, .probe = probe_c };

/* ------------------------------------------------------------------ */
/* the module-init section and the module-section coexistence          */
/* ------------------------------------------------------------------ */

static int init_order[4];
static int init_n;

static int
init_first(void)
{

	init_order[init_n++] = 1;
	return 0;
}

static int
init_second(void)
{

	init_order[init_n++] = 2;
	return 0;
}

module_init(init_first);
module_init(init_second);

/* One entry in the usb driver registry, so the two sections are both present in
 * this link and their byte counts can be compared. The macro only takes the
 * address, so any object serves as the anchor. */
static int mock_usb_driver_anchor;
module_usb_driver(mock_usb_driver_anchor);

extern void *__lkpi_module_start[] __attribute__((weak));
extern void *__lkpi_module_end[] __attribute__((weak));
extern lkpi_init_fn_t __lkpi_init_start[] __attribute__((weak));
extern lkpi_init_fn_t __lkpi_init_end[] __attribute__((weak));

/* ------------------------------------------------------------------ */
/* the claim-serial case                                               */
/* ------------------------------------------------------------------ */

static volatile int claim_inside;
static volatile int claim_peak;
static volatile int claim_rounds;
static volatile int claim_held_violations;
static volatile int claim_io_errors;

static void
claim_worker(void *arg)
{
	struct sdio_func *f = arg;
	int i;

	for (i = 0; i < 20; i++) {
		int err = -1;

		sdio_claim_host(f);
		claim_inside++;
		if (claim_inside > claim_peak)
			claim_peak = claim_inside;
		if (!lkpi_sdio_claim_held_by_current())
			claim_held_violations++;
		/* I/O inside the claim must work and must not claim again */
		(void)sdio_readb(f, 0x00, &err);
		if (err != 0)
			claim_io_errors++;
		claim_inside--;
		sdio_release_host(f);
		claim_rounds++;
	}
	osThreadExit();		/* a task must not return to its caller */
}

/* ------------------------------------------------------------------ */
/* the interrupt-pump case                                             */
/* ------------------------------------------------------------------ */

static volatile int irq_hits;
static volatile int irq_claim_held;
static volatile int irq_io_ok;
static volatile int irq_after_release;
static struct task_struct *irq_task_first;
static int irq_task_stable = 1;
static int irq_released;

static void
irq_handler(struct sdio_func *f)
{
	int err = -1;
	struct task_struct *self = lkpi_current();

	if (irq_released != 0) {
		irq_after_release++;
		return;
	}
	irq_hits++;
	if (lkpi_sdio_claim_held_by_current())
		irq_claim_held++;
	if (self == NULL) {
		irq_task_stable = 0;
	} else if (irq_task_first == NULL) {
		irq_task_first = self;
	} else if (irq_task_first != self) {
		irq_task_stable = 0;
	}
	/* I/O inside the handler, with the claim already held */
	(void)sdio_readb(f, 0x00, &err);
	if (err == 0)
		irq_io_ok++;
}

/* ------------------------------------------------------------------ */
/* the task-identity case                                              */
/* ------------------------------------------------------------------ */

static struct task_struct *other_task_obj;

static void
other_task(void *arg)
{

	(void)arg;
	other_task_obj = lkpi_current();
	osThreadExit();		/* a task must not return to its caller */
}

/* ------------------------------------------------------------------ */
/* the cases                                                           */
/* ------------------------------------------------------------------ */

static void
case_card_up(void)
{
	struct sdio_func *f0 = lkpi_sdio_func0();
	struct sdio_func *f1 = lkpi_sdio_func(1);
	struct lkpi_sdio_stats st;
	int ok;

	lkpi_sdio_stats_get(&st);
	ok = lkpi_sdio_ready() == 1 && f0 != NULL && f1 != NULL &&
	    f1->num == 1 && f1->vendor == 0x024c && f1->device == 0xf179 &&
	    f1->card != NULL && f1->card->sdio_funcs == 1 &&
	    f1->card->host != NULL && f1->card->host->max_blk_size == 512 &&
	    f1->card->cccr.multi_block == 1 && st.cards == 1;
	if (ok)
		info("card %04x:%04x func1 class=%u blk=%u hostmax=%u",
		    f1->vendor, f1->device, f1->class, f1->cur_blksize,
		    f1->card->host->max_blk_size);
	case_report("card-up", ok, ok ? "" : "the card or its function objects did not match the model");
}

static void
case_driver_before(void)
{
	int ok = probe_a_calls == 0;

	case_report("driver-before", ok,
	    ok ? "" : "a non-matching driver probed the card");
}

static void
case_driver_match(void)
{
	int ok;

	__sdio_register_driver(&drv_b, NULL);
	ok = probe_b_calls == 1 && probe_b_func == 1 && probe_b_vendor == 0x024c &&
	    probe_b_device == 0xf179 && probe_b_class == SDIO_CLASS_WLAN;
	if (ok)
		info("probe saw function %u %04x:%04x class=%u", probe_b_func,
		    probe_b_vendor, probe_b_device, probe_b_class);
	case_report("driver-match", ok,
	    ok ? "" : "the matching driver did not probe the card's function");
}

static void
case_driver_claim_once(void)
{
	int ok;

	__sdio_register_driver(&drv_c, NULL);
	ok = probe_c_calls == 0;
	case_report("driver-claim-once", ok,
	    ok ? "" : "a second matching driver re-claimed an already claimed function");
}

static void
case_readb_writeb(struct sdio_func *f)
{
	u8 back = 0;
	int err = -1;
	int ok;
	u8 want = 0x5a;
	int lying = g_fault != NULL && strcmp(g_fault, "lying-card") == 0;

	/* the red control: a card that answers inverted bytes must turn the
	 * readback red, so the fault is in place before the transfer */
	if (lying)
		mock_sdio_card_fault(MOCK_SDIO_FAULT_LIE);

	mock_sdio_card_capture_clear();
	sdio_writeb(f, want, 0x500, &err);
	ok = err == 0;
	back = sdio_readb(f, 0x500, &err);
	ok = ok && err == 0 && back == want;
	/* a function-0 read reaches the CCCR even through a function-1 handle */
	ok = ok && sdio_f0_readb(f, SDIO_CCCR_CCCR, &err) == 0x02 && err == 0;

	if (lying)
		mock_sdio_card_fault(MOCK_SDIO_FAULT_NONE);

	case_report("readb-writeb", ok,
	    ok ? "one byte round-tripped and function 0 is addressable" :
	    "a byte did not round-trip, or function 0 was not addressable");
}

static void
case_word_access(struct sdio_func *f)
{
	u8 *m = mock_sdio_card_mem(1, 0x600);
	unsigned int ext2 = 0;
	unsigned int ext4 = 0;
	unsigned int i;
	u16 w = 0;
	u32 l = 0;
	int err = -1;
	int ok;

	/* the model's bytes for two little-endian values */
	m[0] = 0x34; m[1] = 0x12;
	m[4] = 0x78; m[5] = 0x56; m[6] = 0x34; m[7] = 0x12;

	mock_sdio_card_capture_clear();
	w = sdio_readw(f, 0x600, &err);
	ok = err == 0 && w == 0x1234;
	l = sdio_readl(f, 0x604, &err);
	ok = ok && err == 0 && l == 0x12345678;
	if (g_tamper)
		l = 0xdeadbeefu;	/* the red control: a deliberately wrong value */
	ok = ok && l == 0x12345678u;

	/* the two calls ride the buffered path, so the capture must show a 2-byte
	 * and a 4-byte transfer */
	for (i = 0; i < mock_sdio_card_capture_count(); i++) {
		struct mock_sdio_req r;

		if (mock_sdio_card_capture_get(i, &r) != 0)
			continue;
		if (r.extended && r.bytes == 2u)
			ext2++;
		if (r.extended && r.bytes == 4u)
			ext4++;
	}
	ok = ok && ext2 == 1 && ext4 == 1;
	case_report("word-access", ok,
	    ok ? "the 2-byte and 4-byte reads are little endian over the scratch buffer" :
	    "a word read was wrong, or it did not ride the buffered path");
}

static void
case_memcpy_split(struct sdio_func *f)
{
	static u8 buf[4096];
	static u8 wr[600];
	struct mock_sdio_req r;
	unsigned int i;
	int st;
	int ok = 1;
	unsigned int blk1 = 0, blk2 = 0, byte1 = 0;

	/* (a) one 1024-byte read: two blocks, no split, data intact */
	(void)memset(buf, 0, sizeof(buf));
	st = sdio_memcpy_fromio(f, buf, 0x000, 1024);
	ok = ok && st == 0;
	for (i = 0; i < 1024u; i++) {
		if (buf[i] != (u8)(i + 1u)) {
			ok = 0;
			break;
		}
	}

	/* (b) a 2560-byte read: the host caps a command at four blocks, so this
	 * must arrive as four blocks then one */
	mock_sdio_card_capture_clear();
	st = sdio_memcpy_fromio(f, buf, 0x000, 2560);
	ok = ok && st == 0;
	for (i = 0; i < mock_sdio_card_capture_count(); i++) {
		if (mock_sdio_card_capture_get(i, &r) != 0)
			continue;
		if (!r.extended)
			continue;
		if (r.block && r.bytes == 2048u)
			blk1++;
		if (r.block && r.bytes == 512u)
			blk2++;
		if (!r.block)
			byte1++;
	}
	ok = ok && blk1 == 1 && blk2 == 1 && byte1 == 0;

	/* (c) a 600-byte read: one block, then the 88-byte remainder in byte mode */
	mock_sdio_card_capture_clear();
	st = sdio_memcpy_fromio(f, buf, 0x000, 600);
	ok = ok && st == 0;
	blk1 = blk2 = byte1 = 0;
	for (i = 0; i < mock_sdio_card_capture_count(); i++) {
		if (mock_sdio_card_capture_get(i, &r) != 0)
			continue;
		if (!r.extended)
			continue;
		if (r.block && r.bytes == 512u)
			blk1++;
		if (!r.block && r.bytes == 88u)
			byte1++;
	}
	ok = ok && blk1 == 1 && byte1 == 1;

	/* (d) a 600-byte write reaches the model, incrementing */
	for (i = 0; i < sizeof(wr); i++)
		wr[i] = (u8)(0xa0u + i);
	mock_sdio_card_capture_clear();
	st = sdio_memcpy_toio(f, 0x100, wr, 600);
	ok = ok && st == 0;
	{
		u8 *m = mock_sdio_card_mem(1, 0x100);

		for (i = 0; i < 600u && ok; i++) {
			if (m[i] != wr[i])
				ok = 0;
		}
	}

	case_report("memcpy-split", ok,
	    ok ? "block chunks, the remainder and the increment address all hold" :
	    "the transfer splitter or the data did not hold");
}

static void
case_block_size(struct sdio_func *f)
{
	unsigned int i;
	int ok = 1;
	int seen256 = 0;
	u8 *fbr = mock_sdio_card_mem(0, 1u * 0x100u + SDIO_FBR_BLKSIZE);

	ok = ok && sdio_set_block_size(f, 0) == 0 && f->cur_blksize == 512u;
	ok = ok && sdio_set_block_size(f, 513) == -EINVAL;

	mock_sdio_card_capture_clear();
	ok = ok && sdio_set_block_size(f, 256) == 0 && f->cur_blksize == 256u &&
	    mock_sdio_card_blk(1) == 256u;
	for (i = 0; i < mock_sdio_card_capture_count(); i++) {
		struct mock_sdio_req r;

		if (mock_sdio_card_capture_get(i, &r) != 0)
			continue;
		if (r.block_size && r.bytes == 256u)
			seen256++;
	}
	ok = ok && seen256 == 1;
	/* the register bytes the configuration wrote, little endian */
	ok = ok && fbr[0] == 0x00u && fbr[1] == 0x01u;

	/* restore, so the later cases see the default again */
	ok = ok && sdio_set_block_size(f, 512) == 0;

	case_report("block-size", ok,
	    ok ? "the default, the refusal, the register bytes and the readback hold" :
	    "the block-size rules did not hold");
}

static void
case_enable_disable(struct sdio_func *f)
{
	u8 *ioex = mock_sdio_card_mem(0, SDIO_CCCR_IOEx);
	int ok;

	mock_sdio_card_capture_clear();
	ok = sdio_enable_func(f) == 0 && (*ioex & 0x02u) == 0x02u;
	ok = ok && sdio_disable_func(f) == 0 && (*ioex & 0x02u) == 0x00u;
	case_report("enable-disable", ok,
	    ok ? "the function enable wrote IOEx and the ready poll saw it" :
	    "the function enable or disable sequence did not hold");
}

static void
case_error_mapping(struct sdio_func *f)
{
	static u8 buf[16];
	int err = 0;
	u8 v;
	int ok;

	mock_sdio_card_fault(MOCK_SDIO_FAULT_DIRECT);
	v = sdio_readb(f, 0x00, &err);
	ok = v == 0xff && err == -EIO;

	mock_sdio_card_fault(MOCK_SDIO_FAULT_DIRECT);
	sdio_writeb(f, 0x11, 0x00, &err);
	ok = ok && err == -EIO;

	mock_sdio_card_fault(MOCK_SDIO_FAULT_EXTENDED);
	ok = ok && sdio_memcpy_fromio(f, buf, 0x00, 16) == -ETIMEDOUT;

	case_report("error-mapping", ok,
	    ok ? "a failed transfer arrives as the interface's errno" :
	    "a seam failure did not arrive as the interface's errno");
}

static void
case_claim_serial(struct sdio_func *f)
{
	osThreadAttr_t attr = { 0 };
	osThreadId_t t1;
	osThreadId_t t2;
	unsigned int waited = 0;

	claim_inside = 0;
	claim_peak = 0;
	claim_rounds = 0;
	claim_held_violations = 0;
	claim_io_errors = 0;

	attr.name = "sdio-claim-1";
	attr.stack_size = 8192u;
	attr.priority = osPriorityNormal;
	t1 = osThreadNew(claim_worker, f, &attr);
	attr.name = "sdio-claim-2";
	t2 = osThreadNew(claim_worker, f, &attr);
	if (t1 == NULL || t2 == NULL) {
		case_report("claim-serial", 0, "the claim workers could not be created");
		return;
	}
	while (claim_rounds < 40 && waited < 5000u) {
		lkpi_msleep(1);
		waited++;
	}
	case_report("claim-serial",
	    claim_rounds == 40 && claim_peak == 1 && claim_held_violations == 0 &&
	    claim_io_errors == 0,
	    claim_rounds == 40 ? "two threads serialized on the bus claim" :
	    "the claim workers did not finish");
}

static void
case_irq_pump(struct sdio_func *f)
{
	int ok;

	ok = sdio_claim_irq(f, irq_handler) == 0;
	ok = ok && sdio_claim_irq(f, irq_handler) == -EBUSY;

	irq_hits = 0;
	irq_claim_held = 0;
	irq_io_ok = 0;
	irq_released = 0;
	irq_task_first = NULL;
	irq_task_stable = 1;

	mock_sdio_card_set_pending(0x02u);
	lkpi_sdio_pump_kick();
	ok = ok && irq_hits == 1 && irq_claim_held == 1 && irq_io_ok == 1;

	/* a second pending word reaches the handler again, on the same thread */
	mock_sdio_card_set_pending(0x02u);
	lkpi_sdio_pump_kick();
	ok = ok && irq_hits == 2 && irq_task_stable == 1;

	/* nothing pending: the poll ran but dispatched nothing */
	mock_sdio_card_set_pending(0x00u);
	lkpi_sdio_pump_kick();
	ok = ok && irq_hits == 2;

	/* after release the card's pending bit is ignored */
	ok = ok && sdio_release_irq(f) == 0;
	irq_released = 1;
	mock_sdio_card_set_pending(0x02u);
	lkpi_sdio_pump_kick();
	ok = ok && irq_after_release == 0;

	case_report("irq-pump", ok,
	    ok ? "the pump dispatched under the claim and release stopped delivery" :
	    "the interrupt pump contract did not hold");
}

/* ------------------------------------------------------------------ */
/* the pump-cadence cases                                              */
/* ------------------------------------------------------------------ */

static void
case_pump_policy(void)
{
	unsigned int mode = lkpi_sdio_pump_mode_get();
	int ok;

	/* the engine starts at the fixed cadence, and the runtime knob round-trips
	 * with the controller's period reset to the idle one on every switch */
	ok = mode == LKPI_SDIO_PUMP_MODE_FIXED;
	lkpi_sdio_pump_mode_set(LKPI_SDIO_PUMP_MODE_ADAPTIVE);
	ok = ok && lkpi_sdio_pump_mode_get() == LKPI_SDIO_PUMP_MODE_ADAPTIVE &&
	    lkpi_sdio_pump_period_get() == 10u;
	lkpi_sdio_pump_mode_set(LKPI_SDIO_PUMP_MODE_FIXED);
	ok = ok && lkpi_sdio_pump_mode_get() == LKPI_SDIO_PUMP_MODE_FIXED &&
	    lkpi_sdio_pump_period_get() == 10u;

	/* the fairness yield defaults off (the shape every measurement ran under)
	 * and round-trips; the bus identity reads back the negotiated tier */
	ok = ok && !lkpi_sdio_pump_yield_get();
	lkpi_sdio_pump_yield_set(true);
	ok = ok && lkpi_sdio_pump_yield_get();
	lkpi_sdio_pump_yield_set(false);
	ok = ok && !lkpi_sdio_pump_yield_get();

	case_report("pump-policy", ok,
	    ok ? "the cadence knob round-trips and defaults to fixed" :
	    "the cadence knob did not round-trip");
}

static void
case_bus_info(void)
{
	struct lkpi_sdio_bus_info bus;
	int ok;

	lkpi_sdio_bus_info_get(&bus);
	/* the mock card negotiates a real tier, so the readback has to name one:
	 * a clock, four data lines and a non-zero block size */
	ok = bus.clock_hz != 0u && bus.bus_width == 4u && bus.blk_size != 0u;

	case_report("bus-info", ok,
	    ok ? "the bus identity reads back the negotiated clock/width/block size" :
	    "the bus identity did not read back");
}

/* a handler whose cycle moves bytes: one extended read per dispatch, which is
 * what the adaptive controller measures progress by (a direct register read
 * deliberately counts as no progress - see the engine's note) */
static volatile int pump_adaptive_hits;

static void
pump_adaptive_handler(struct sdio_func *f)
{
	u8 buf[16];

	pump_adaptive_hits++;
	(void)sdio_memcpy_fromio(f, buf, 0x00, sizeof(buf));
}

static void
case_pump_adaptive(struct sdio_func *f)
{
	struct lkpi_sdio_stats st;
	unsigned int base, hot, idle;
	int ok;

	ok = sdio_claim_irq(f, pump_adaptive_handler) == 0;
	if (ok == 0) {
		case_report("pump-adaptive", 0, "the IRQ could not be claimed");
		return;
	}

	/* hot: with the card pending and every cycle moving bytes, the period
	 * halves toward zero and cycles run back-to-back - far more polls land in
	 * 40 ms than the fixed cadence's four */
	lkpi_sdio_pump_mode_set(LKPI_SDIO_PUMP_MODE_ADAPTIVE);
	mock_sdio_card_set_pending(0x02u);
	lkpi_sdio_pump_kick();
	lkpi_sdio_stats_get(&st);
	base = st.pump_polls;
	lkpi_msleep(40);
	lkpi_sdio_stats_get(&st);
	hot = st.pump_polls - base;
	ok = ok && hot >= 20u;

	/* idle: once the hot hold has expired the controller backs off a step per
	 * empty cycle, so a minute's one-second window holds a handful of polls,
	 * not a spin (a spin would be thousands) */
	mock_sdio_card_set_pending(0x00u);
	lkpi_msleep(40);		/* let the hot hold lapse, backoff starts */
	lkpi_sdio_stats_get(&st);
	base = st.pump_polls;
	lkpi_msleep(60);
	lkpi_sdio_stats_get(&st);
	idle = st.pump_polls - base;
	ok = ok && idle <= 16u;

	ok = ok && sdio_release_irq(f) == 0;
	lkpi_sdio_pump_mode_set(LKPI_SDIO_PUMP_MODE_FIXED);

	case_report("pump-adaptive", ok,
	    ok ? "an active card is serviced back-to-back and an idle one backs off" :
	    "the adaptive cadence did not follow the workload");
}

static void
case_current_identity(void)
{
	osThreadAttr_t attr = { 0 };
	osThreadId_t t;
	struct task_struct *a;
	struct task_struct *b;
	unsigned int waited = 0;
	int ok;

	a = lkpi_current();
	b = lkpi_current();
	ok = a != NULL && a == b;

	other_task_obj = NULL;
	attr.name = "sdio-other";
	attr.stack_size = 4096u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(other_task, NULL, &attr);
	if (t == NULL) {
		case_report("current-identity", 0, "the identity thread could not be created");
		return;
	}
	while (other_task_obj == NULL && waited < 2000u) {
		lkpi_msleep(1);
		waited++;
	}
	ok = ok && other_task_obj != NULL && other_task_obj != a;

	/* the interrupt handler's own object: a live thread, stable across calls,
	 * and not the application task's */
	ok = ok && irq_task_first != NULL && irq_task_stable == 1 &&
	    irq_task_first != a;

	case_report("current-identity", ok,
	    ok ? "each thread has one stable task object of its own" :
	    "the task identity was not stable or not per-thread");
}

static void
case_init_entries(void)
{
	int ok;

	init_n = 0;
	lkpi_module_init_run();
	ok = init_n == 2 && init_order[0] == 1 && init_order[1] == 2;
	/* idempotent: a second walk runs nothing again */
	lkpi_module_init_run();
	ok = ok && init_n == 2;
	case_report("init-entries", ok,
	    ok ? "the module init section ran in order, once" :
	    "the module init section did not run as declared");
}

static void
case_section_coexist(void)
{
	uintptr_t module_bytes = 0;
	uintptr_t init_bytes = 0;
	int ok;

	if (__lkpi_module_start != NULL && __lkpi_module_end != NULL)
		module_bytes = (uintptr_t)__lkpi_module_end -
		    (uintptr_t)__lkpi_module_start;
	if (__lkpi_init_start != NULL && __lkpi_init_end != NULL)
		init_bytes = (uintptr_t)__lkpi_init_end - (uintptr_t)__lkpi_init_start;

	/* one usb driver entry, two module init entries; each section's byte count
	 * is its entry count times the natural pointer step, which is what proves
	 * the module_init channel did not disturb the usb registry's layout */
	ok = module_bytes == sizeof(void *) &&
	    init_bytes == 2u * sizeof(lkpi_init_fn_t);
	case_report("section-coexist", ok,
	    ok ? "the usb driver section still holds exactly its own entries" :
	    "the two module sections disturbed each other");
}

/* ------------------------------------------------------------------ */
/* the run                                                             */
/* ------------------------------------------------------------------ */

static void
run_scenario(void)
{
	struct sdio_func *f1;

	/* the synthetic card, then a driver that must not match yet */
	mock_sdio_card_install();
	__sdio_register_driver(&drv_a, NULL);
	lkpi_sdio_start();
	f1 = lkpi_sdio_func(1);

	case_card_up();
	case_driver_before();
	case_driver_match();
	case_driver_claim_once();

	if (f1 == NULL) {
		case_report("readb-writeb", 0, "the card is not up");
		case_report("word-access", 0, "the card is not up");
		case_report("memcpy-split", 0, "the card is not up");
		case_report("block-size", 0, "the card is not up");
		case_report("error-mapping", 0, "the card is not up");
		return;
	}

	case_readb_writeb(f1);
	case_word_access(f1);
	case_memcpy_split(f1);
	case_block_size(f1);
	case_enable_disable(f1);
	case_error_mapping(f1);
	case_claim_serial(f1);
	case_irq_pump(f1);
	case_pump_policy();
	case_bus_info();
	case_pump_adaptive(f1);
	case_current_identity();
	case_init_entries();
	case_section_coexist();
}

static void
app_task(void *arg)
{

	(void)arg;
	run_scenario();
	if (failures == 0)
		printf("sdio-test: PASS\n");
	else
		printf("sdio-test: FAIL failures=%d\n", failures);
	fflush(stdout);
	cmsis2_host_end_scheduler();
}

int
main(int argc, char **argv)
{
	osThreadId_t t;
	osThreadAttr_t attr = { 0 };
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--fault") == 0 && i + 1 < argc) {
			g_fault = argv[++i];
			if (strcmp(g_fault, "expect-tamper") == 0)
				g_tamper = 1;
		} else {
			printf("sdio-test: usage: %s [--fault expect-tamper|lying-card]\n",
			    argv[0]);
			return 2;
		}
	}

	if (osKernelInitialize() != osOK) {
		printf("sdio-test: kernel init failed\n");
		return 2;
	}
	attr.name = "sdio-mock";
	attr.stack_size = 32768u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("sdio-test: cannot create the application task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	return failures == 0 ? 0 : 1;
}
