/**
 * @file    usb_msc.c
 * @brief   Cases usb-xhci / usb-ehci: enumerate a U-disk and round-trip blocks.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case bodies of the regression table (tests/board/common/board_suite.h);
 *          the anchor chain and the block-transfer shape follow the import
 *          spec's board-test design (ports/cherryusb) and the prior board-proven
 *          measurement shape (read 32 MiB, write 16 MiB).
 *
 * The two case rows live in their own files (execution order = the image's
 * source list order); this file carries the shared body.
 *
 * One case per controller: bring the board layer up, start the stack, wait for
 * the mass-storage class instance (the devices sit behind hubs, so the wait
 * covers hub enumeration too), then read block zero and round-trip a pattern
 * at the disk's tail. Writes never touch sector zero: a prior line learned
 * that the bootloader finds and repairs the partition table after such a
 * write, and a regression run must not damage the target disk.
 *
 * Speed is recorded, not judged: no threshold is a criterion yet (the
 * comparison baseline belongs to the USB camera milestone).
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "usb_glue.h"

#include "usbh_core.h"
#include "usbh_msc.h"

#include "msc_app.h"

#define MSC_BLOCK_BYTES   512U
#define MSC_CHUNK_SECTORS 128U          /* 64 KiB per transfer */
#define MSC_READ_SECTORS  (32U * 1024U * 1024U / MSC_BLOCK_BYTES)
#define MSC_WRITE_SECTORS (16U * 1024U * 1024U / MSC_BLOCK_BYTES)
/* The scratch area sits inside the disk's tail: the bulk write of
 * MSC_WRITE_SECTORS plus the roundtrip chunk must not run past the end, and
 * nothing below it may be touched (the device STALLs an out-of-range write -
 * measured). */
#define MSC_TAIL_SECTORS  2048U
#define MSC_SCRATCH_SECTORS (MSC_WRITE_SECTORS + MSC_CHUNK_SECTORS + MSC_TAIL_SECTORS)
#define MSC_WAIT_SPINS    600U          /* 100 ms per spin -> 60 s */

/* The chunk sweep's geometry (see msc_sweep): one command count per size, and
 * the largest command it issues sizes the transfer buffers below. */
#define MSC_SWEEP_CMDS    256U
#define MSC_SWEEP_SECTORS 256U          /* 128 KiB - the sweep's largest chunk */

/* The stack asserts DMA buffers are 64-byte aligned (CONFIG_USB_ALIGN_SIZE),
 * so the transfer buffers carry the alignment explicitly. */
static uint8_t msc_buf[MSC_SWEEP_SECTORS * MSC_BLOCK_BYTES] __attribute__((aligned(64)));
static uint8_t msc_ref[MSC_SWEEP_SECTORS * MSC_BLOCK_BYTES] __attribute__((aligned(64)));

static struct usbh_msc *msc_wait(void)
{
	for (unsigned spin = 0; spin < MSC_WAIT_SPINS; spin++) {
		struct usbh_msc *msc = usb_msc_instance();

		if (msc != 0) {
			return msc;
		}
		osDelay(100U);
	}
	return 0;
}

/* The buffers are sized for the sweep's largest chunk, so every fill and every
 * compare names its own length rather than the array's - a compare over the
 * whole array would cover bytes this workload never transferred. */
static void msc_pattern(uint8_t *buf, size_t len, unsigned seed)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = (uint8_t)(i * 31U + seed);
	}
}

/*
 * The chunk sweep: the same read (or write) workload at several command sizes,
 * so the per-command fixed cost separates from the per-byte cost.  It is the
 * native twin of the linuxkpi row's sweep (tests/board/common/udisk/
 * lkpi_udisk_core.c) - the same command count, the same sizes, and the same
 * whole-run wall clock - so the two stacks' curves are directly comparable and
 * the difference between their fixed costs is the stack's own per-transfer
 * cost.  A BOT command is three bulk transfers (CBW 31 B / data / CSW 13 B),
 * so the fit of 1/rate against 1/chunk gives the fixed cost as its slope.
 *
 * Reads and writes both walk the disk's tail window (the largest run is
 * MSC_SWEEP_SECTORS * MSC_SWEEP_CMDS sectors), so nothing near the front of the
 * device is touched: a prior line learned the bootloader repairs a partition
 * table it finds written over.
 */
static const uint16_t msc_sweep_sectors[] = { 16U, 32U, 64U, 128U, 256U };
#define MSC_SWEEP_N (sizeof(msc_sweep_sectors) / sizeof(msc_sweep_sectors[0]))

static int msc_sweep(struct usbh_msc *msc, int write)
{
	uint32_t span = (uint32_t)MSC_SWEEP_SECTORS * MSC_SWEEP_CMDS;
	uint32_t base = msc->blocknum > span + 4096U ? msc->blocknum - span - 4096U : 2048U;

	for (unsigned i = 0; i < MSC_SWEEP_N; i++) {
		uint16_t sectors = msc_sweep_sectors[i];
		uint32_t chunk = (uint32_t)sectors * MSC_BLOCK_BYTES;
		uint64_t bytes = (uint64_t)chunk * MSC_SWEEP_CMDS;
		uint32_t lba = base;
		uint32_t start_ms;
		uint32_t elapsed_ms;
		uint64_t rate100;

		msc_pattern(msc_buf, (size_t)sectors * MSC_BLOCK_BYTES,
			    write ? 0xA5U : 0x5AU);
		start_ms = osKernelGetTickCount();
		for (unsigned n = 0; n < MSC_SWEEP_CMDS; n++) {
			int ret = write
			    ? usbh_msc_scsi_write10(msc, lba, msc_buf, sectors)
			    : usbh_msc_scsi_read10(msc, lba, msc_buf, sectors);

			if (ret != 0) {
				LOG_ERROR("usbh: msc sweep %s chunk=%u KiB failed\n",
					  write ? "write" : "read", chunk >> 10);
				return ret;
			}
			lba += sectors;
			if (lba + sectors > base + span) {
				lba = base;
			}
		}
		elapsed_ms = osKernelGetTickCount() - start_ms;
		/* hundredths of MiB/s: the sizes differ 16x, so the small end's
		 * integer part alone would lose most of the curve */
		rate100 = elapsed_ms != 0
		    ? (bytes * 100000ULL) / ((uint64_t)elapsed_ms * 1048576ULL) : 0;
		LOG_ANCHOR("usbh: msc sweep %s chunk=%u KiB %u MiB in %u ms "
			   "= %llu.%02llu MB/s per_cmd_us=%u\n",
			   write ? "write" : "read", chunk >> 10,
			   (unsigned)(bytes >> 20), (unsigned)elapsed_ms,
			   rate100 / 100ULL, rate100 % 100ULL,
			   elapsed_ms != 0
			   ? (unsigned)((uint64_t)elapsed_ms * 1000ULL / MSC_SWEEP_CMDS)
			   : 0U);
	}
	return 0;
}

void usb_msc_case(unsigned busid, const char *hcd)
{
	struct usbh_msc *msc;
	uint32_t scratch;
	uint32_t start_ms;
	uint32_t elapsed_ms;

	if (usb_board_init(busid) != 0) {
		LOG_ERROR("usbh: fail board layer bring-up at bus%u\n", busid);
		return -1;
	}
	LOG_INFO("usbh: core init bus=%u hcd=%s base=0x%lx\n", busid, hcd,
		  (unsigned long)usb_board_reg_base(busid));

	if (usbh_initialize(busid, usb_board_reg_base(busid), usb_board_event_handler) != 0) {
		board_case_fail("usbh_initialize failed");
		return;
	}
	usb_board_after_start(busid);

	msc = msc_wait();
	if (msc == 0) {
		board_case_fail("no mass-storage instance within 60 s");
		return;
	}
	LOG_ANCHOR("usbh: class msc claimed lun=0 blk=%u\n", (unsigned)msc->blocknum);

	if (usbh_msc_scsi_read10(msc, 0, msc_buf, 1) != 0) {
		board_case_fail("read of block zero failed");
		return;
	}
	LOG_ANCHOR("usbh: msc read blk0=0x%02x%02x%02x%02x ok\n",
		  msc_buf[0], msc_buf[1], msc_buf[2], msc_buf[3]);

	/* Inside the device, outside any partition table or filesystem. */
	scratch = msc->blocknum - MSC_SCRATCH_SECTORS;

	msc_pattern(msc_ref, MSC_CHUNK_SECTORS * MSC_BLOCK_BYTES, 0x5AU);
	if (usbh_msc_scsi_write10(msc, scratch, msc_ref, MSC_CHUNK_SECTORS) != 0) {
		board_case_fail("scratch write failed");
		return;
	}
	if (usbh_msc_scsi_read10(msc, scratch, msc_buf, MSC_CHUNK_SECTORS) != 0) {
		board_case_fail("scratch read-back failed");
		return;
	}
	for (unsigned i = 0; i < MSC_CHUNK_SECTORS * MSC_BLOCK_BYTES; i++) {
		if (msc_buf[i] != msc_ref[i]) {
			board_case_fail("scratch read-back differs from the pattern");
			return;
		}
	}
	LOG_ANCHOR("usbh: msc write/read roundtrip ok at lba=%u\n", (unsigned)scratch);

	start_ms = osKernelGetTickCount();
	for (uint32_t done = 0; done < MSC_READ_SECTORS; done += MSC_CHUNK_SECTORS) {
		if (usbh_msc_scsi_read10(msc, 2048U + done, msc_buf, MSC_CHUNK_SECTORS) != 0) {
			board_case_fail("bulk read failed");
			return;
		}
	}
	elapsed_ms = osKernelGetTickCount() - start_ms;
	LOG_INFO("usbh: msc read %u MiB in %u ms\n", (unsigned)(MSC_READ_SECTORS * MSC_BLOCK_BYTES >> 20),
		  (unsigned)elapsed_ms);

	start_ms = osKernelGetTickCount();
	for (uint32_t done = 0; done < MSC_WRITE_SECTORS; done += MSC_CHUNK_SECTORS) {
		if (usbh_msc_scsi_write10(msc, scratch + done, msc_buf, MSC_CHUNK_SECTORS) != 0) {
			board_case_fail("bulk write failed");
			return;
		}
	}
	elapsed_ms = osKernelGetTickCount() - start_ms;
	LOG_INFO("usbh: msc write %u MiB in %u ms\n", (unsigned)(MSC_WRITE_SECTORS * MSC_BLOCK_BYTES >> 20),
		  (unsigned)elapsed_ms);

	/* the size sweep the linuxkpi row also runs: its curve is what says how
	 * much of a stack's cost is per transfer rather than per byte */
	if (msc_sweep(msc, 0) != 0) {
		board_case_fail("the read chunk sweep failed");
		return;
	}
	if (msc_sweep(msc, 1) != 0) {
		board_case_fail("the write chunk sweep failed");
		return;
	}
	return 0;
}

