/*
 * @file   usb_msc.c
 * @brief  The mass-storage body shared by the xHCI and EHCI cases: bring one
 *         controller up, enumerate the U-disk, verify a block round trip and
 *         measure the bulk throughput at several command sizes.
 *
 * The class itself is the imported CherryUSB one (`class/msc/usbh_msc.c`);
 * this file is test code only and never implements a storage protocol.  The
 * first-party application half (`ports/cherryusb/class/msc_app.c`) performs the
 * SCSI identity handshake and leaves the ready disk in its table, which is
 * what `usb_msc_instance()` answers.
 *
 * One controller per boot on purpose: `usb_msc_instance()` returns the first
 * ready disk, so starting two buses in one boot would silently measure
 * whichever enumerated first.  The wrapper case names the controller.
 *
 * Disk safety: the only read below sector 0 is the block-zero read; every
 * write lives in the disk's last 32 MiB window, 1 MiB aligned.  The same
 * window formula is used by the Linux ground-truth arm
 * (`probes/driver/usb/fio-udisk.sh`), so the two legs transfer the same bytes.
 *
 * Anchors (tools/rtos/judge.py matches these):
 *   usbh: msc bus=N hcd=... vid=... pid=... speed=N blocks=N bsize=N serial=...
 *   usbh: msc ep in=0x81 type=bulk maxp=512 / ep out=0x02 ...
 *   usbh: msc blk0 ok
 *   usbh: msc roundtrip ok lba=N len=N
 *   usbh: msc io read|write size_kib=N runs=3 bytes=N ms=N rate=N.NN mib_s per_cmd_us=N
 *   usbh: msc sweep read|write chunk=N KiB M MiB in N ms = R.RR MiB/s per_cmd_us=N
 *   usbh: msc PASS | usbh: msc FAIL <reason>
 */

#include <stdint.h>
#include <stddef.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "usb_glue.h"

#include "usbh_core.h"
#include "usbh_msc.h"

#include "msc_app.h"

#define MSC_BLOCK_BYTES		512U
#define MSC_WAIT_MS		30000U
#define MSC_WAIT_STEP_MS	100U
/* After the disk is ready the bus may still be enumerating another device on
 * the same root (the bench carries the 8851BU next to the U-disk).  The xHCI
 * port's command completion is occasionally lost on that device's Address
 * Device command and the port's timeout drain recovers it after 5 s; measuring
 * through that window turned one 4 KiB write arm into 0.28 MiB/s.  Settle
 * before the first measured transfer instead. */
#define MSC_SETTLE_MS		8000U

/* The test window: the disk's last 32 MiB, aligned down to 1 MiB. */
#define MSC_WINDOW_BYTES	(32U * 1024U * 1024U)
#define MSC_ALIGN_BYTES		(1U * 1024U * 1024U)

/* The throughput arms: a fixed command count per size, not a fixed byte
 * count.  The first board run showed the per-command fixed cost is the number
 * under test (a 512-byte command and a 64 KiB command differ by the data
 * phase only), and a byte-sized batch at 4 KiB made one arm run for minutes
 * and overrun the capture window.  These counts keep every arm inside the
 * window and still span tens of milliseconds at the measured fixed cost. */
#define MSC_ARM_CMDS_4K		128U
#define MSC_ARM_CMDS_64K	32U
#define MSC_ARM_CMDS_512K	8U
#define MSC_ARM_CMDS_1M		4U

/* The size sweep: a fixed command count at each size, so the per-command
 * fixed cost is the fit's slope. */
#define MSC_SWEEP_CMDS		32U
#define MSC_SWEEP_MAX_SECTORS	256U		/* 128 KiB */

/* 64 KiB alignment, not 64 B: the xHCI port caps one bulk TD at eight 64 KiB
 * TRBs, and a buffer whose first chunk is split by a 64 KiB boundary needs a
 * ninth TRB - a 512 KiB command then fails at the port ("submit ret=-2")
 * before the device sees it.  The DMA rule is 64 B; this is the command-size
 * rule. */
/* The buffer covers the largest arm (1 MiB on EHCI; xHCI tops out at 512 KiB
 * because of its eight-TRB TD bound), not the sweep's 128 KiB: the arms read
 * and write their command bytes straight through it. */
#define MSC_BUF_BYTES		(1024U * 1024U)
static uint8_t msc_buf[MSC_BUF_BYTES] __attribute__((aligned(65536)));
static uint8_t msc_ref[MSC_BUF_BYTES] __attribute__((aligned(65536)));
static uint8_t msc_blk0[MSC_BLOCK_BYTES] __attribute__((aligned(64)));

/* Both controllers carry a 1 MiB single command after the port patches: the
 * xHCI TD chains sixteen 64 KiB TRBs, the EHCI qTD chain takes the same span
 * in 20 KiB pieces.  The 1 MiB arm needs the buffers 64 KiB aligned (below). */
static const uint16_t msc_arm_xhci_kib[] = { 4U, 64U, 1024U };
static const uint16_t msc_arm_ehci_kib[] = { 4U, 64U, 1024U };
static const uint16_t msc_sweep_sectors[] = { 16U, 32U, 64U, 128U, 256U };

static void
msc_pattern(uint8_t *buf, size_t len, unsigned int seed)
{
	for (size_t i = 0; i < len; i++)
		buf[i] = (uint8_t)(i * 31U + seed);
}

static int
msc_same(const uint8_t *a, const uint8_t *b, size_t len)
{
	for (size_t i = 0; i < len; i++)
		if (a[i] != b[i])
			return 0;
	return 1;
}

static const char *
msc_ep_type(const struct usb_endpoint_descriptor *ep)
{
	switch (ep->bmAttributes & 0x03U) {
	case 0x00U: return "control";
	case 0x01U: return "iso";
	case 0x02U: return "bulk";
	case 0x03U: return "int";
	default: return "?";
	}
}

static int
msc_wait(struct usbh_msc **out, unsigned int wait_ms)
{
	for (unsigned int waited = 0; waited < wait_ms; waited += MSC_WAIT_STEP_MS) {
		*out = usb_msc_instance();
		if (*out != NULL)
			return 0;
		osDelay(MSC_WAIT_STEP_MS);
	}
	return -1;
}

/* One timed batch of `cmds` commands of `sectors` sectors each, walking the
 * window and wrapping inside it.  Returns 0, or the class error. */
static int
msc_batch(struct usbh_msc *msc, int write, unsigned int sectors,
	  unsigned int cmds, uint32_t start_lba, uint32_t end_lba,
	  uint32_t *ms_out, uint64_t *bytes_out)
{
	uint32_t lba = start_lba;
	uint32_t t0;
	unsigned int seed = write ? 0xA5U : 0x5AU;

	msc_pattern(msc_buf, (size_t)sectors * MSC_BLOCK_BYTES, seed);
	t0 = osKernelGetTickCount();
	for (unsigned int n = 0; n < cmds; n++) {
		int ret = write
		    ? usbh_msc_scsi_write10(msc, lba, msc_buf, sectors)
		    : usbh_msc_scsi_read10(msc, lba, msc_buf, sectors);

		if (ret != 0)
			return ret;
#if defined(MSC_DIAG)
		if ((n & 3U) == 0U)
			CASE_INFO("usbh: msc progress n=%u t=%ums irq=%u\n", n,
				  (unsigned)(osKernelGetTickCount() - t0),
				  usb_board_irq_count(msc->hport->bus->busid));
#endif
		lba += sectors;
		if (lba + sectors > end_lba)
			lba = start_lba;
	}
	*ms_out = osKernelGetTickCount() - t0;
	*bytes_out = (uint64_t)sectors * MSC_BLOCK_BYTES * cmds;
	return 0;
}

static void
msc_rate_anchor(const char *op, unsigned int size_kib, unsigned int runs,
		uint64_t bytes, uint32_t ms, unsigned int cmds)
{
	uint64_t rate100 = ms != 0
	    ? (bytes * 100000ULL) / ((uint64_t)ms * 1048576ULL) : 0;

	CASE_ANCHOR("usbh: msc io %s size_kib=%u runs=%u bytes=%llu ms=%u "
		    "rate=%llu.%02llu mib_s per_cmd_us=%u\n",
		    op, size_kib, runs, (unsigned long long)bytes, (unsigned)ms,
		    (unsigned long long)(rate100 / 100ULL),
		    (unsigned long long)(rate100 % 100ULL),
		    ms != 0 ? (unsigned int)((uint64_t)ms * 1000ULL / cmds) : 0U);
}

static int
msc_sweep(struct usbh_msc *msc, int write, uint32_t start_lba, uint32_t end_lba)
{
	uint32_t span = (uint32_t)MSC_SWEEP_MAX_SECTORS * MSC_SWEEP_CMDS;

	if (start_lba + span > end_lba)
		return -1;

	for (unsigned int i = 0; i < sizeof(msc_sweep_sectors) /
	     sizeof(msc_sweep_sectors[0]); i++) {
		uint16_t sectors = msc_sweep_sectors[i];
		uint32_t chunk = (uint32_t)sectors * MSC_BLOCK_BYTES;
		uint32_t lba = start_lba;
		uint32_t ms;
		uint64_t bytes;
		uint64_t rate100;
		int ret;

		ret = msc_batch(msc, write, sectors, MSC_SWEEP_CMDS, lba,
				start_lba + span, &ms, &bytes);
		if (ret != 0) {
			CASE_ANCHOR("usbh: msc sweep %s chunk=%u KiB failed rc=%d\n",
				    write ? "write" : "read", chunk >> 10, ret);
			return ret;
		}
		rate100 = ms != 0
		    ? (bytes * 100000ULL) / ((uint64_t)ms * 1048576ULL) : 0;
		CASE_ANCHOR("usbh: msc sweep %s chunk=%u KiB %u MiB in %u ms "
			    "= %llu.%02llu MiB/s per_cmd_us=%u\n",
			    write ? "write" : "read", chunk >> 10,
			    (unsigned)(bytes >> 20), (unsigned)ms,
			    (unsigned long long)(rate100 / 100ULL),
			    (unsigned long long)(rate100 % 100ULL),
			    ms != 0 ? (unsigned int)((uint64_t)ms * 1000ULL /
						    MSC_SWEEP_CMDS) : 0U);
	}
	return 0;
}

int
usb_msc_case(unsigned int busid, const char *hcd)
{
	struct usbh_msc *msc = NULL;
	uint32_t blocks;
	uint32_t start_lba;
	uint32_t end_lba;
	int ret;

	if (usb_board_init(busid) != 0) {
		CASE_ANCHOR("usbh: msc FAIL bus%u has no controller\n", busid);
		return 1;
	}
	usb_osal_timer_service_start();
	CASE_INFO("usbh: core init bus=%u hcd=%s base=0x%lx\n", busid, hcd,
		  (unsigned long)usb_board_reg_base(busid));
	if (usbh_initialize((uint8_t)busid, usb_board_reg_base(busid),
			    usb_board_event_handler) != 0) {
		CASE_ANCHOR("usbh: msc FAIL usbh_initialize bus%u\n", busid);
		return 1;
	}
	usb_board_after_start((uint8_t)busid);

	if (msc_wait(&msc, MSC_WAIT_MS) != 0) {
		CASE_ANCHOR("usbh: msc FAIL no mass-storage instance on bus%u "
			    "within %u ms\n", busid, MSC_WAIT_MS);
		return 2;	/* no device: the wrapper may try another root */
	}

	CASE_ANCHOR("usbh: msc bus=%u hcd=%s vid=%04x pid=%04x speed=%u "
		    "blocks=%u bsize=%u serial=%s\n",
		    (unsigned)msc->hport->bus->busid, hcd,
		    (unsigned)msc->hport->device_desc.idVendor,
		    (unsigned)msc->hport->device_desc.idProduct,
		    (unsigned)msc->hport->speed, (unsigned)msc->blocknum,
		    (unsigned)msc->blocksize,
		    msc->hport->iSerialNumber != NULL ?
			msc->hport->iSerialNumber : "-");
	CASE_ANCHOR("usbh: msc ep in=0x%02x type=%s maxp=%u out=0x%02x type=%s "
		    "maxp=%u\n",
		    msc->bulkin->bEndpointAddress, msc_ep_type(msc->bulkin),
		    (unsigned)msc->bulkin->wMaxPacketSize,
		    msc->bulkout->bEndpointAddress, msc_ep_type(msc->bulkout),
		    (unsigned)msc->bulkout->wMaxPacketSize);

	/* let a sibling device on the same root finish enumerating */
	osDelay(MSC_SETTLE_MS);
	CASE_INFO("usbh: msc settle ms=%u irq=%u\n", MSC_SETTLE_MS,
		  usb_board_irq_count(busid));

	blocks = msc->blocknum;
	if (blocks == 0 || msc->blocksize != MSC_BLOCK_BYTES) {
		CASE_ANCHOR("usbh: msc FAIL geometry blocks=%u bsize=%u\n",
			    (unsigned)blocks, (unsigned)msc->blocksize);
		return 1;
	}
	if (blocks <= (MSC_WINDOW_BYTES / MSC_BLOCK_BYTES) + 8192U) {
		CASE_ANCHOR("usbh: msc FAIL disk too small for the window "
			    "(%u blocks)\n", (unsigned)blocks);
		return 1;
	}

	if (usbh_msc_scsi_read10(msc, 0, msc_blk0, 1) != 0) {
		CASE_ANCHOR("usbh: msc FAIL block-zero read\n");
		return 1;
	}
	CASE_ANCHOR("usbh: msc blk0 ok\n");

	start_lba = (blocks - (MSC_WINDOW_BYTES / MSC_BLOCK_BYTES)) &
	    ~(MSC_ALIGN_BYTES / MSC_BLOCK_BYTES - 1U);
	end_lba = start_lba + (MSC_WINDOW_BYTES / MSC_BLOCK_BYTES);

	/* The round trip: a 64 KiB write at the window start and a read back. */
	msc_pattern(msc_ref, 64U * 1024U, 0x5AU);
	if (usbh_msc_scsi_write10(msc, start_lba, msc_ref, 128U) != 0) {
		CASE_ANCHOR("usbh: msc FAIL roundtrip write\n");
		return 1;
	}
	if (usbh_msc_scsi_read10(msc, start_lba, msc_buf, 128U) != 0) {
		CASE_ANCHOR("usbh: msc FAIL roundtrip read\n");
		return 1;
	}
	if (!msc_same(msc_buf, msc_ref, 64U * 1024U)) {
		CASE_ANCHOR("usbh: msc FAIL roundtrip pattern differs "
			    "(cache/DMA coherency)\n");
		return 1;
	}
	CASE_ANCHOR("usbh: msc roundtrip ok lba=%u len=%u\n",
		    (unsigned)start_lba, 64U * 1024U);

	/* Single-command timings: the fixed cost per BOT command (three bulk
	 * transfers) at two data lengths.  They separate "the controller is
	 * slow" from "the batch loop is slow" before the arms run. */
	{
		uint32_t t0 = osKernelGetTickCount();
		uint32_t ms;

		if (usbh_msc_scsi_read10(msc, start_lba, msc_buf, 8U) != 0) {
			CASE_ANCHOR("usbh: msc FAIL probe read 4 KiB\n");
			return 1;
		}
		ms = osKernelGetTickCount() - t0;
		CASE_ANCHOR("usbh: msc probe read size_kib=4 ms=%u\n",
			    (unsigned)ms);

		t0 = osKernelGetTickCount();
		if (usbh_msc_scsi_read10(msc, start_lba, msc_buf, 128U) != 0) {
			CASE_ANCHOR("usbh: msc FAIL probe read 64 KiB\n");
			return 1;
		}
		ms = osKernelGetTickCount() - t0;
		CASE_ANCHOR("usbh: msc probe read size_kib=64 ms=%u\n",
			    (unsigned)ms);
	}

#if defined(MSC_DIAG) && !defined(MSC_DIAG_FULL)
	/*
	 * Diagnostic arm: eight sequential 4 KiB reads with a short class
	 * timeout, one line each.  A transfer that completes only after the
	 * port's timeout drain shows up as `ms=800` with the interrupt count
	 * standing still; a genuinely slow device shows a growing `irq`.
	 */
	{
		unsigned int irq0 = usb_board_irq_count(busid);
		uint32_t t0 = osKernelGetTickCount();

		for (unsigned int n = 1; n <= 8U; n++) {
			int rc = usbh_msc_scsi_read10(msc, start_lba + n * 8U,
						      msc_buf, 8U);

			CASE_INFO("usbh: msc diag n=%u rc=%d t=%ums irq=%u\n", n,
				  rc,
				  (unsigned)(osKernelGetTickCount() - t0),
				  usb_board_irq_count(busid));
			if (rc != 0)
				break;
		}
		CASE_ANCHOR("usbh: msc diag done irq0=%u irq=%u t=%ums\n", irq0,
			    usb_board_irq_count(busid),
			    (unsigned)(osKernelGetTickCount() - t0));

		/* The same path the delivery arms use: msc_batch's loop and the
		 * rate anchor.  Bisects "the batch loop hangs" against "the
		 * anchor/printf hangs" in one boot. */
		{
			uint32_t ms = 0;
			uint64_t bytes = 0;
			int rc = msc_batch(msc, 0, 8U, 128U, start_lba, end_lba,
					   &ms, &bytes);

			CASE_INFO("usbh: msc diag batch rc=%d ms=%u bytes=%llu\n",
				  rc, (unsigned)ms, (unsigned long long)bytes);
			msc_rate_anchor("read", 4U, 1U, bytes, ms, 8U);
		}
		return 0;
	}
#endif

	/* The throughput arms, read then write, at three command sizes. */
	const uint16_t *arm_kib = (hcd[0] == 'x') ? msc_arm_xhci_kib :
	    msc_arm_ehci_kib;

	for (unsigned int i = 0; i < 3U; i++) {
		unsigned int size_kib = arm_kib[i];
		unsigned int sectors = (size_kib * 1024U) / MSC_BLOCK_BYTES;
		unsigned int cmds = size_kib == 4U ? MSC_ARM_CMDS_4K :
		    size_kib == 64U ? MSC_ARM_CMDS_64K :
		    size_kib == 512U ? MSC_ARM_CMDS_512K : MSC_ARM_CMDS_1M;

		for (int pass = 0; pass < 2; pass++) {
			int write = pass;
			uint64_t total = 0;
			unsigned int total_ms = 0;

			for (unsigned int run = 0; run < 3U; run++) {
				uint32_t ms = 0;
				uint64_t bytes = 0;

				ret = msc_batch(msc, write, sectors, cmds,
						start_lba, end_lba, &ms, &bytes);
				if (ret != 0) {
					CASE_ANCHOR("usbh: msc io %s size_kib=%u "
						    "failed rc=%d\n",
						    write ? "write" : "read",
						    size_kib, ret);
					return 1;
				}
				total += bytes;
				total_ms += ms;
			}
			msc_rate_anchor(write ? "write" : "read", size_kib, 3U,
					total, total_ms, cmds * 3U);
		}
	}

	/* The size sweep (fixed command count) for the per-command cost fit. */
	if (msc_sweep(msc, 0, start_lba, end_lba) != 0) {
		CASE_ANCHOR("usbh: msc FAIL read sweep\n");
		return 1;
	}
	if (msc_sweep(msc, 1, start_lba, end_lba) != 0) {
		CASE_ANCHOR("usbh: msc FAIL write sweep\n");
		return 1;
	}

	CASE_ANCHOR("usbh: msc PASS\n");
	return 0;
}
