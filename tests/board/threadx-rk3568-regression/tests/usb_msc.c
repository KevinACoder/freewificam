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

#define MSC_BLOCK_BYTES   512U
#define MSC_CHUNK_SECTORS 128U          /* 64 KiB per transfer */
#define MSC_READ_SECTORS  (32U * 1024U * 1024U / MSC_BLOCK_BYTES)
#define MSC_WRITE_SECTORS (16U * 1024U * 1024U / MSC_BLOCK_BYTES)
#define MSC_TAIL_SECTORS  2048U         /* scratch area: the disk's last MiB */
#define MSC_WAIT_SPINS    600U          /* 100 ms per spin -> 60 s */

static uint8_t msc_buf[MSC_CHUNK_SECTORS * MSC_BLOCK_BYTES];
static uint8_t msc_ref[MSC_CHUNK_SECTORS * MSC_BLOCK_BYTES];

static struct usbh_msc *msc_wait(void)
{
	for (unsigned spin = 0; spin < MSC_WAIT_SPINS; spin++) {
		struct usbh_msc *msc = (struct usbh_msc *)usbh_find_class_instance("usbh_msc");

		if (msc != 0) {
			return msc;
		}
		osDelay(100U);
	}
	return 0;
}

static void msc_pattern(uint8_t *buf, unsigned seed)
{
	for (unsigned i = 0; i < sizeof(msc_buf); i++) {
		buf[i] = (uint8_t)(i * 31U + seed);
	}
}

void usb_msc_case(unsigned busid, const char *hcd)
{
	struct usbh_msc *msc;
	uint32_t scratch;
	uint32_t start_ms;
	uint32_t elapsed_ms;

	if (usb_board_init(busid) != 0) {
		board_case_fail("board layer bring-up failed");
		return;
	}
	board_log("usbh: core init bus=%u hcd=%s base=0x%lx\n", busid, hcd,
		  (unsigned long)usb_board_reg_base(busid));

	if (usbh_initialize(busid, usb_board_reg_base(busid), 0) != 0) {
		board_case_fail("usbh_initialize failed");
		return;
	}
	usb_board_after_start(busid);

	msc = msc_wait();
	if (msc == 0) {
		board_case_fail("no mass-storage instance within 60 s");
		return;
	}
	board_log("usbh: class msc claimed lun=0 blk=%u\n", (unsigned)msc->blocknum);

	if (usbh_msc_scsi_read10(msc, 0, msc_buf, 1) != 0) {
		board_case_fail("read of block zero failed");
		return;
	}
	board_log("usbh: msc read blk0=0x%02x%02x%02x%02x ok\n",
		  msc_buf[0], msc_buf[1], msc_buf[2], msc_buf[3]);

	/* The scratch area is the disk's last mebibyte: inside the device,
	 * outside any partition table or filesystem. */
	scratch = msc->blocknum - MSC_TAIL_SECTORS;

	msc_pattern(msc_ref, 0x5AU);
	if (usbh_msc_scsi_write10(msc, scratch, msc_ref, MSC_CHUNK_SECTORS) != 0) {
		board_case_fail("scratch write failed");
		return;
	}
	if (usbh_msc_scsi_read10(msc, scratch, msc_buf, MSC_CHUNK_SECTORS) != 0) {
		board_case_fail("scratch read-back failed");
		return;
	}
	for (unsigned i = 0; i < sizeof(msc_buf); i++) {
		if (msc_buf[i] != msc_ref[i]) {
			board_case_fail("scratch read-back differs from the pattern");
			return;
		}
	}
	board_log("usbh: msc write/read roundtrip ok at lba=%u\n", (unsigned)scratch);

	start_ms = osKernelGetTickCount();
	for (uint32_t done = 0; done < MSC_READ_SECTORS; done += MSC_CHUNK_SECTORS) {
		if (usbh_msc_scsi_read10(msc, done, msc_buf, MSC_CHUNK_SECTORS) != 0) {
			board_case_fail("bulk read failed");
			return;
		}
	}
	elapsed_ms = osKernelGetTickCount() - start_ms;
	board_log("usbh: msc read %u MiB in %u ms\n", (unsigned)(MSC_READ_SECTORS * MSC_BLOCK_BYTES >> 20),
		  (unsigned)elapsed_ms);

	start_ms = osKernelGetTickCount();
	for (uint32_t done = 0; done < MSC_WRITE_SECTORS; done += MSC_CHUNK_SECTORS) {
		if (usbh_msc_scsi_write10(msc, scratch + done, msc_buf, MSC_CHUNK_SECTORS) != 0) {
			board_case_fail("bulk write failed");
			return;
		}
	}
	elapsed_ms = osKernelGetTickCount() - start_ms;
	board_log("usbh: msc write %u MiB in %u ms\n", (unsigned)(MSC_WRITE_SECTORS * MSC_BLOCK_BYTES >> 20),
		  (unsigned)elapsed_ms);
}

