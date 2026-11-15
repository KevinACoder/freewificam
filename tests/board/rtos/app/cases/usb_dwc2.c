/*
 * @file   usb_dwc2.c
 * @brief  F429 case usb_dwc2: enumerate the U disk behind the OTG_HS host
 *         port, prove the SCSI identity and the bulk data path, and (when the
 *         profile enables it) write/read one sector in the disk's last 1 MiB
 *         window.
 *
 * The controller is the imported CherryUSB DWC2 port; this file is test code
 * and never implements a storage protocol.  The first-party MSC application
 * half (app/usb/msc_app.c) performs the SCSI identity handshake and leaves the
 * ready disk in its table, which is what usb_msc_instance() answers.
 *
 * The write is a profile decision (CONFIG_USB_MSC_WRITE_TEST): one 512 B
 * sector at the 1 MiB-aligned start of the disk's last 1 MiB window, with the
 * first 4 MiB never touched.  The LBA is printed so the run record can name
 * the sector that changed.
 *
 * Anchors (tools/rtos/judge.py matches these):
 *   usbh: msc bus=0 hcd=dwc2 vid=... pid=... speed=... blocks=... bsize=...
 *   usbh: msc ep in=0x.. type=bulk maxp=... out=0x.. type=bulk maxp=...
 *   usbh: msc blk0 ok sig=....
 *   usbh: msc read lba=0 blocks=8 bytes=4096 ok
 *   usbh: msc window lba=N blocks=2048
 *   usbh: msc roundtrip ok lba=N len=512
 *   usbh: msc PASS | usbh: msc FAIL <reason>
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "usb_glue.h"

#include "usbh_core.h"
#include "usbh_msc.h"

#include "msc_app.h"

#define MSC_BLOCK_BYTES		512U
#define MSC_READ_BLOCKS		8U
#define MSC_READ_BYTES		(MSC_BLOCK_BYTES * MSC_READ_BLOCKS)
#define MSC_WAIT_MS		30000U
#define MSC_WAIT_STEP_MS	100U

/* The write window: the disk's last 1 MiB, 1 MiB aligned down.  At least
 * 4 MiB in front of it is never written, so a small disk fails with a named
 * reason instead of touching its partition table. */
#define MSC_WINDOW_BLOCKS	2048U
#define MSC_FRONT_RESERVE_BLOCKS	8192U

static uint8_t msc_blk0[MSC_BLOCK_BYTES] __attribute__((aligned(4)));
static uint8_t msc_buf[MSC_READ_BYTES] __attribute__((aligned(4)));
static uint8_t msc_ref[MSC_BLOCK_BYTES] __attribute__((aligned(4)));

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

/* The judge's serial field is \S*; a stick with a space in its serial string
 * must not break the anchor. */
static void
msc_serial(char *out, size_t max, const char *in)
{
	size_t n = 0;

	if (in == NULL || max == 0U) {
		return;
	}
	for (; in[n] != '\0' && n + 1U < max; n++) {
		char c = in[n];

		out[n] = (c == ' ' || c == '\t' || c == '\r' || c == '\n') ? '_' : c;
	}
	out[n] = '\0';
	if (n == 0U) {
		(void)snprintf(out, max, "-");
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

int
case_usb_dwc2_run(void)
{
	struct usbh_msc *msc = NULL;
	uint32_t blocks;
	char serial[32] = "-";
	int ret;

	CASE_INFO("usbh: clock hz=%u fallback=%u\n",
		  (unsigned)SystemCoreClock, (unsigned)g_clock_fallback);

	if (usb_board_init(0) != 0) {
		CASE_ANCHOR("usbh: msc FAIL bus0 has no controller\n");
		return 1;
	}
	CASE_INFO("usbh: core init bus=0 hcd=dwc2 base=0x%lx\n",
		  (unsigned long)usb_board_reg_base(0));

	if (usbh_initialize(0, usb_board_reg_base(0),
			    usb_board_event_handler) != 0) {
		CASE_ANCHOR("usbh: msc FAIL usbh_initialize bus0\n");
		return 1;
	}
	usb_board_after_start(0);

	if (msc_wait(&msc, MSC_WAIT_MS) != 0) {
		CASE_ANCHOR("usbh: msc FAIL no mass-storage instance on bus0 "
			    "within %u ms hprt=%08x irq=%u\n", MSC_WAIT_MS,
			    (unsigned)usb_board_port_status(),
			    usb_board_irq_count(0));
		return 2;
	}

	msc_serial(serial, sizeof(serial), msc->hport->iSerialNumber);
	CASE_ANCHOR("usbh: msc bus=%u hcd=dwc2 vid=%04x pid=%04x speed=%u "
		    "blocks=%u bsize=%u serial=%s\n",
		    (unsigned)msc->hport->bus->busid,
		    (unsigned)msc->hport->device_desc.idVendor,
		    (unsigned)msc->hport->device_desc.idProduct,
		    (unsigned)msc->hport->speed, (unsigned)msc->blocknum,
		    (unsigned)msc->blocksize, serial);
	CASE_ANCHOR("usbh: msc ep in=0x%02x type=%s maxp=%u out=0x%02x type=%s "
		    "maxp=%u\n",
		    msc->bulkin->bEndpointAddress, msc_ep_type(msc->bulkin),
		    (unsigned)msc->bulkin->wMaxPacketSize,
		    msc->bulkout->bEndpointAddress, msc_ep_type(msc->bulkout),
		    (unsigned)msc->bulkout->wMaxPacketSize);

	blocks = msc->blocknum;
	if (blocks == 0U || msc->blocksize != MSC_BLOCK_BYTES) {
		CASE_ANCHOR("usbh: msc FAIL geometry blocks=%u bsize=%u\n",
			    (unsigned)blocks, (unsigned)msc->blocksize);
		return 1;
	}

	if (usbh_msc_scsi_read10(msc, 0, msc_blk0, 1) != 0) {
		CASE_ANCHOR("usbh: msc FAIL block-zero read\n");
		return 1;
	}
	CASE_ANCHOR("usbh: msc blk0 ok sig=%04x\n",
		    (unsigned)((msc_blk0[510] << 8) | msc_blk0[511]));

	if (usbh_msc_scsi_read10(msc, 0, msc_buf, MSC_READ_BLOCKS) != 0) {
		CASE_ANCHOR("usbh: msc FAIL multi-block read\n");
		return 1;
	}
	if (!msc_same(msc_blk0, msc_buf, MSC_BLOCK_BYTES)) {
		CASE_ANCHOR("usbh: msc FAIL block-zero repeat differs\n");
		return 1;
	}
	CASE_ANCHOR("usbh: msc read lba=0 blocks=%u bytes=%u ok\n",
		    MSC_READ_BLOCKS, MSC_READ_BYTES);

#if defined(CONFIG_USB_MSC_WRITE_TEST)
	{
		uint32_t start_lba;

		if (blocks < MSC_WINDOW_BLOCKS + MSC_FRONT_RESERVE_BLOCKS) {
			CASE_ANCHOR("usbh: msc FAIL disk too small for a write "
				    "window (%u blocks)\n", (unsigned)blocks);
			return 1;
		}
		start_lba = (blocks - MSC_WINDOW_BLOCKS) &
			    ~(MSC_WINDOW_BLOCKS - 1U);
		if (start_lba < MSC_FRONT_RESERVE_BLOCKS) {
			CASE_ANCHOR("usbh: msc FAIL write window start lba=%u\n",
				    (unsigned)start_lba);
			return 1;
		}
		CASE_ANCHOR("usbh: msc window lba=%u blocks=%u\n",
			    (unsigned)start_lba, MSC_WINDOW_BLOCKS);

		msc_pattern(msc_ref, sizeof(msc_ref), 0xA5U);
		ret = usbh_msc_scsi_write10(msc, start_lba, msc_ref, 1);
		if (ret != 0) {
			CASE_ANCHOR("usbh: msc FAIL roundtrip write rc=%d\n", ret);
			return 1;
		}
		memset(msc_buf, 0, MSC_BLOCK_BYTES);
		ret = usbh_msc_scsi_read10(msc, start_lba, msc_buf, 1);
		if (ret != 0) {
			CASE_ANCHOR("usbh: msc FAIL roundtrip read rc=%d\n", ret);
			return 1;
		}
		if (!msc_same(msc_buf, msc_ref, sizeof(msc_ref))) {
			CASE_ANCHOR("usbh: msc FAIL roundtrip pattern differs\n");
			return 1;
		}
		CASE_ANCHOR("usbh: msc roundtrip ok lba=%u len=%u\n",
			    (unsigned)start_lba, (unsigned)sizeof(msc_ref));
	}
#else
	CASE_ANCHOR("usbh: msc write skipped (read-only profile)\n");
#endif

	CASE_ANCHOR("usbh: msc PASS\n");
	return 0;
}
