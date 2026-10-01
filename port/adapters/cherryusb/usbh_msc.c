/*
 * @file   usbh_msc.c
 * @brief  Minimal USB Mass Storage (BOT) host class driver + the `usbh_msc`
 *         acceptance command set (SPEC S5/cherryusb-xhci-port §3.7, the only
 *         new-design code of the milestone).
 *
 * Scope is deliberately narrow: the block path for "读写对拍 + soak + 带宽
 * 打点" - no partitions, no filesystem, no full SCSI state machine (sense
 * handling is "retry once, then report", spec §3.7). LUN 0 only, one
 * interface, the two bulk endpoints of altsetting 0.
 *
 * BOT chain per command: 31-byte CBW ("USBC") on bulk-OUT, the optional
 * data stage, the 13-byte CSW ("USBS") on bulk-IN - signature, tag,
 * bCSWStatus and dCSWDataResidue are all checked. Commands run under a
 * per-device mutex (BOT has no concurrent commands).
 *
 * DMA buffers are static and the data buffer is 64 KiB aligned so a
 * 32 KiB chunk can never straddle a 64 KiB boundary (the NetBSD-line
 * restriction the xHCI port inherited, §3.8); cache maintenance is the
 * HCD port's business (the modeswitch-line contract).
 *
 * URB shape follows the board-proven modeswitch pattern: async submit
 * (timeout 0) + own semaphore + explicit kill_urb on timeout, one
 * completion semaphore reused under the command mutex.
 *
 * The shell command set (CSH_CMD_EXPORT_ALIAS_FULL, S4 discipline):
 *
 *   usbh_msc list                      instances + INQUIRY/CAPACITY identity
 *   usbh_msc [idx] read <lba> <n>      read n blocks (32 KiB chunks); verifies
 *                                      against the matching last write and
 *                                      hexdumps the first 32 bytes
 *   usbh_msc [idx] write <lba> <n> <p> fill n blocks with pattern byte <p>
 *   usbh_msc [idx] bw [<MiB>]          sequential-read bandwidth (default 32)
 *   usbh_msc soak <sec> [<idx>]        write/read/verify loop - the 600s
 *                                      acceptance long-test, progress line
 *                                      every 30 s, summary at the end
 *
 * <idx> defaults to 0 (the first claimed instance); the acceptance run
 * addresses the USB stick explicitly (bus3 hub tree, index printed by
 * `usbh_msc list`).
 *
 * @date   01.10.2026
 * @author zhugengyu
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "cmsis_os2.h"

#include "usbh_core.h"
#include "usbh_hub.h"
#include "usb_errno.h"
#include "usb_config.h"

#include "cherrysh_adapter.h"
#include "csh.h"

#define MSC_DEV_MAX			2U
#define MSC_BULK_TIMEOUT_MS		1000U
#define MSC_READY_TRIES			10U
#define MSC_READY_STEP_MS		100U

/* BOT command data reference (USB 2.0 §5.3.x): 31-byte Command Block
 * Wrapper, little-endian fields, signature "USBC". */
#define MSC_CBW_SIZE			31U
#define MSC_CSW_SIZE			13U
#define MSC_CBW_TAG_SIG			0x43425355U
#define MSC_CSW_TAG_SIG			0x53425355U
#define MSC_CBW_FLAGS_IN		0x80U

/* SCSI command opcodes (the §3.7 minimal set; opcodes match
 * linux include/scsi/scsi_proto.h). */
#define SCSI_TEST_UNIT_READY		0x00U
#define SCSI_REQUEST_SENSE		0x03U
#define SCSI_INQUIRY			0x12U
#define SCSI_READ_CAPACITY10		0x25U
#define SCSI_READ10			0x28U
#define SCSI_WRITE10			0x2AU

#define SCSI_SENSE_DATA_SIZE		18U
#define SCSI_INQUIRY_DATA_SIZE		36U
#define SCSI_CAPACITY_DATA_SIZE		8U

/* Transfer chunk: never crosses a 64 KiB boundary (see file comment), so
 * the buffer is 64 KiB aligned and the chunk half of that. */
#define MSC_CHUNK_BYTES			32768U
#define MSC_CHUNK_ALIGN			65536U
#define MSC_CHUNK_BLOCKS		(MSC_CHUNK_BYTES / 512U)

struct usbh_msc_dev {
	struct usbh_hubport *hport;
	struct usb_endpoint_descriptor *ep_out;
	struct usb_endpoint_descriptor *ep_in;
	usb_osal_mutex_t lock;
	usb_osal_sem_t done;
	int nbytes;
	uint32_t tag;

	uint32_t block_size;
	uint32_t block_count;
	char vendor[9];
	char product[17];

	/* last write, for `read`'s verify pass */
	uint32_t w_lba;
	uint32_t w_blocks;
	uint8_t w_pattern;

	uint8_t inuse;
	uint8_t cbw[MSC_CBW_SIZE];
	uint8_t csw[MSC_CSW_SIZE];
	uint8_t sense[SCSI_SENSE_DATA_SIZE];
	uint8_t data[MSC_CHUNK_BYTES] __attribute__((aligned(MSC_CHUNK_ALIGN)));
};

static struct usbh_msc_dev g_msc_dev[MSC_DEV_MAX];

/* --- one blocking bulk transfer (modeswitch-line shape) --------------------- */

static void msc_bulk_complete(void *arg, int nbytes_or_err)
{
	struct usbh_msc_dev *msc = arg;

	msc->nbytes = nbytes_or_err;
	usb_osal_sem_give(msc->done);
}

static int msc_bulk(struct usbh_msc_dev *msc,
		    struct usb_endpoint_descriptor *ep,
		    uint8_t *buf, uint32_t len)
{
	struct usbh_urb urb;

	/* drop a completion left over from a timed-out attempt */
	(void)usb_osal_sem_take(msc->done, 0);
	msc->nbytes = 0;

	memset(&urb, 0, sizeof(urb));
	usbh_bulk_urb_fill(&urb, msc->hport, ep, buf, len, 0,
			   msc_bulk_complete, msc);
	if (usbh_submit_urb(&urb) < 0) {
		return -USB_ERR_IO;
	}
	if (usb_osal_sem_take(msc->done, MSC_BULK_TIMEOUT_MS) != 0) {
		usbh_kill_urb(&urb);
		return -USB_ERR_TIMEOUT;
	}
	return msc->nbytes;
}

/* --- BOT wrap: CBW - optional data stage - CSW ------------------------------ */

static int msc_bot_xfer(struct usbh_msc_dev *msc, const uint8_t *cb,
			uint8_t cb_len, uint8_t dir_in, uint8_t *data,
			uint32_t data_len)
{
	int n;

	if ((cb_len == 0U) || (cb_len > 16U)) {
		return -USB_ERR_INVAL;
	}

	memset(msc->cbw, 0, MSC_CBW_SIZE);
	msc->cbw[0] = 0x55U;
	msc->cbw[1] = 0x53U;
	msc->cbw[2] = 0x42U;
	msc->cbw[3] = 0x43U;			/* "USBC" */
	msc->tag++;
	msc->cbw[4] = (uint8_t)(msc->tag & 0xffU);
	msc->cbw[5] = (uint8_t)((msc->tag >> 8) & 0xffU);
	msc->cbw[6] = (uint8_t)((msc->tag >> 16) & 0xffU);
	msc->cbw[7] = (uint8_t)((msc->tag >> 24) & 0xffU);
	msc->cbw[8] = (uint8_t)(data_len & 0xffU);
	msc->cbw[9] = (uint8_t)((data_len >> 8) & 0xffU);
	msc->cbw[10] = (uint8_t)((data_len >> 16) & 0xffU);
	msc->cbw[11] = (uint8_t)((data_len >> 24) & 0xffU);
	msc->cbw[12] = dir_in ? MSC_CBW_FLAGS_IN : 0x00U;
	/* LUN 0 */
	msc->cbw[14] = cb_len;
	memcpy(&msc->cbw[15], cb, cb_len);

	n = msc_bulk(msc, msc->ep_out, msc->cbw, MSC_CBW_SIZE);
	if (n != (int)MSC_CBW_SIZE) {
		return (n < 0) ? n : -USB_ERR_IO;
	}

	if ((data != NULL) && (data_len > 0U)) {
		n = msc_bulk(msc, dir_in ? msc->ep_in : msc->ep_out,
			     data, data_len);
		if (n < 0) {
			return n;
		}
	}

	memset(msc->csw, 0, MSC_CSW_SIZE);
	n = msc_bulk(msc, msc->ep_in, msc->csw, MSC_CSW_SIZE);
	if (n != (int)MSC_CSW_SIZE) {
		return (n < 0) ? n : -USB_ERR_IO;
	}

	if ((msc->csw[0] != 0x55U) || (msc->csw[1] != 0x53U) ||
	    (msc->csw[2] != 0x42U) || (msc->csw[3] != 0x53U)) {
		usbh_console_printf("usbh_msc: CSW signature %02x%02x%02x%02x\n",
				    msc->csw[0], msc->csw[1], msc->csw[2],
				    msc->csw[3]);
		return -USB_ERR_IO;
	}
	if (((uint32_t)msc->csw[4] | ((uint32_t)msc->csw[5] << 8) |
	     ((uint32_t)msc->csw[6] << 16) | ((uint32_t)msc->csw[7] << 24)) !=
	    msc->tag) {
		usbh_console_printf("usbh_msc: CSW tag mismatch\n");
		return -USB_ERR_IO;
	}

	/* dCSWDataResidue (little-endian): what the device did not move;
	 * nonzero with a nominal data stage is a short transfer the caller
	 * judges. */
	if (((uint32_t)msc->csw[8] | ((uint32_t)msc->csw[9] << 8) |
	     ((uint32_t)msc->csw[10] << 16) |
	     ((uint32_t)msc->csw[11] << 24)) > data_len) {
		usbh_console_printf("usbh_msc: CSW residue overflow\n");
		return -USB_ERR_IO;
	}

	switch (msc->csw[12]) {
	case 0x00U:
		return 0;
	case 0x01U: /* command failed: the caller's sense pass */
		return -USB_ERR_IO;
	case 0x02U: /* phase error: unrecoverable on this minimal chain */
		usbh_console_printf("usbh_msc: CSW phase error\n");
		return -USB_ERR_IO;
	default:
		return -USB_ERR_IO;
	}
}

/* --- the SCSI minimal set ---------------------------------------------------- */

static int msc_test_unit_ready(struct usbh_msc_dev *msc)
{
	static const uint8_t cb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };

	return msc_bot_xfer(msc, cb, 6U, 0U, NULL, 0U);
}

/* One sense pass; returns the ASC/ASCQ pair (or 0) via the out params.
 * The §3.7 sense policy is the caller's: retry once, then report. */
static int msc_request_sense(struct usbh_msc_dev *msc, uint8_t *key)
{
	static const uint8_t cb[6] = { SCSI_REQUEST_SENSE, 0, 0, 0,
				       SCSI_SENSE_DATA_SIZE, 0 };
	int ret;

	memset(msc->sense, 0, sizeof(msc->sense));
	ret = msc_bot_xfer(msc, cb, 6U, 1U, msc->sense, SCSI_SENSE_DATA_SIZE);
	*key = (ret == 0) ? (msc->sense[2] & 0x0fU) : 0U;
	return ret;
}

static int msc_rw10(struct usbh_msc_dev *msc, uint8_t op, uint32_t lba,
		    uint16_t blocks, uint8_t *data)
{
	uint8_t cb[10];

	memset(cb, 0, sizeof(cb));
	cb[0] = op;
	cb[2] = (uint8_t)((lba >> 24) & 0xffU);	/* big-endian LBA */
	cb[3] = (uint8_t)((lba >> 16) & 0xffU);
	cb[4] = (uint8_t)((lba >> 8) & 0xffU);
	cb[5] = (uint8_t)(lba & 0xffU);
	cb[7] = (uint8_t)((blocks >> 8) & 0xffU);
	cb[8] = (uint8_t)(blocks & 0xffU);

	return msc_bot_xfer(msc, cb, 10U, (op == SCSI_READ10) ? 1U : 0U,
			    data, (uint32_t)blocks * msc->block_size);
}

/* One command with the §3.7 sense policy: on command-fail, one
 * REQUEST_SENSE retry of the original, then report. */
static int msc_command(struct usbh_msc_dev *msc,
		       int (*cmd)(struct usbh_msc_dev *))
{
	uint8_t key = 0;
	int ret;

	ret = cmd(msc);
	if (ret == 0) {
		return 0;
	}

	(void)msc_request_sense(msc, &key);
	ret = cmd(msc);
	if (ret != 0) {
		usbh_console_printf("usbh_msc: command fail, sense key=%u\n",
				    (uint32_t)key);
	}
	return ret;
}

/* --- identity chain (connect) ------------------------------------------------ */

static int msc_read_capacity(struct usbh_msc_dev *msc)
{
	static const uint8_t cb[10] = { SCSI_READ_CAPACITY10, 0, 0, 0, 0, 0,
					0, 0, 0, 0 };
	uint8_t cap[SCSI_CAPACITY_DATA_SIZE];
	int ret;

	memset(cap, 0, sizeof(cap));
	ret = msc_command(msc, msc_test_unit_ready);
	if (ret != 0) {
		return ret;
	}
	ret = msc_bot_xfer(msc, cb, 10U, 1U, cap, SCSI_CAPACITY_DATA_SIZE);
	if (ret != 0) {
		return ret;
	}

	msc->block_count =
		((uint32_t)cap[0] << 24) | ((uint32_t)cap[1] << 16) |
		((uint32_t)cap[2] << 8) | (uint32_t)cap[3];
	msc->block_size =
		((uint32_t)cap[4] << 24) | ((uint32_t)cap[5] << 16) |
		((uint32_t)cap[6] << 8) | (uint32_t)cap[7];
	msc->block_count += 1U; /* READ CAPACITY reports the last LBA */
	return 0;
}

static int msc_inquiry(struct usbh_msc_dev *msc)
{
	static const uint8_t cb[6] = { SCSI_INQUIRY, 0, 0, 0,
				       SCSI_INQUIRY_DATA_SIZE, 0 };
	uint8_t inq[SCSI_INQUIRY_DATA_SIZE];
	uint32_t i;
	int ret;

	memset(inq, 0, sizeof(inq));
	ret = msc_bot_xfer(msc, cb, 6U, 1U, inq, SCSI_INQUIRY_DATA_SIZE);
	if (ret != 0) {
		return ret;
	}

	for (i = 0U; i < 8U; i++) {
		msc->vendor[i] = (char)inq[8U + i];
	}
	msc->vendor[8] = '\0';
	for (i = 0U; i < 16U; i++) {
		msc->product[i] = (char)inq[16U + i];
	}
	msc->product[16] = '\0';
	return 0;
}

/* --- class driver glue -------------------------------------------------------- */

static struct usbh_msc_dev *msc_dev_alloc(void)
{
	uint32_t i;

	for (i = 0U; i < MSC_DEV_MAX; i++) {
		if (g_msc_dev[i].inuse == 0U) {
			memset(&g_msc_dev[i], 0, sizeof(g_msc_dev[i]));
			g_msc_dev[i].inuse = 1U;
			return &g_msc_dev[i];
		}
	}
	return NULL;
}

static void msc_dev_free(struct usbh_msc_dev *msc)
{
	msc->inuse = 0U;
}

static int usbh_msc_connect(struct usbh_hubport *hport, uint8_t intf)
{
	struct usbh_interface *intf_desc = &hport->config.intf[intf];
	struct usb_interface_descriptor *id =
		&intf_desc->altsetting[0].intf_desc;
	struct usb_endpoint_descriptor *ep_out = NULL, *ep_in = NULL;
	struct usbh_msc_dev *msc;
	uint32_t i;
	int ret;

	if ((id->bInterfaceClass != USB_DEVICE_CLASS_MASS_STORAGE) ||
	    (id->bInterfaceSubClass != 0x06U) ||
	    (id->bInterfaceProtocol != 0x50U)) {
		return -USB_ERR_INVAL;
	}
	for (i = 0U; i < id->bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep =
			&intf_desc->altsetting[0].ep[i].ep_desc;

		if (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) !=
		    USB_ENDPOINT_TYPE_BULK) {
			continue;
		}
		if ((ep->bEndpointAddress & 0x80U) != 0U) {
			if (ep_in == NULL) {
				ep_in = ep;
			}
		} else if (ep_out == NULL) {
			ep_out = ep;
		}
	}
	if ((ep_out == NULL) || (ep_in == NULL)) {
		return -USB_ERR_INVAL;
	}

	msc = msc_dev_alloc();
	if (msc == NULL) {
		usbh_console_printf("usbh_msc: no free instance slot\n");
		return -USB_ERR_NOMEM;
	}

	msc->hport = hport;
	msc->ep_out = ep_out;
	msc->ep_in = ep_in;
	msc->tag = 0x1234U;
	msc->lock = usb_osal_mutex_create();
	msc->done = usb_osal_sem_create(0);
	if ((msc->lock == NULL) || (msc->done == NULL)) {
		msc_dev_free(msc);
		return -USB_ERR_NOMEM;
	}

	/* The §4/P5-d 判据 1 identity chain, on the hub thread: TUR (the
	 * stick may need a few hundred ms to settle), INQUIRY, READ
	 * CAPACITY(10) - vendor/容量打印 is the anchor. */
	usb_osal_mutex_take(msc->lock);
	ret = -USB_ERR_IO;
	for (i = 0U; i < MSC_READY_TRIES; i++) {
		uint8_t sense_key;

		ret = msc_test_unit_ready(msc);
		if (ret == 0) {
			break;
		}
		(void)msc_request_sense(msc, &sense_key);
		usb_osal_msleep(MSC_READY_STEP_MS);
	}
	if (ret == 0) {
		ret = msc_inquiry(msc);
	}
	if (ret == 0) {
		ret = msc_read_capacity(msc);
	}
	usb_osal_mutex_give(msc->lock);

	if (ret != 0) {
		usbh_console_printf("usbh_msc: identity chain fail (%d)\n",
				    ret);
		usb_osal_mutex_delete(msc->lock);
		usb_osal_sem_delete(msc->done);
		msc_dev_free(msc);
		return ret;
	}

	intf_desc->priv = msc;
	usbh_console_printf("usbh_msc: bus%u addr%u '%s' '%s' blocks=%u "
			    "bsize=%u (%u MiB)\n",
			    (uint32_t)hport->bus->busid,
			    (uint32_t)hport->dev_addr, msc->vendor,
			    msc->product, msc->block_count, msc->block_size,
			    (uint32_t)(((uint64_t)msc->block_count *
					msc->block_size) >> 20));
	return 0;
}

static int usbh_msc_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
	struct usbh_interface *intf_desc = &hport->config.intf[intf];
	struct usbh_msc_dev *msc = intf_desc->priv;

	if (msc == NULL) {
		return 0;
	}
	usbh_console_printf("usbh_msc: bus%u gone\n",
			    (uint32_t)hport->bus->busid);
	usb_osal_mutex_delete(msc->lock);
	usb_osal_sem_delete(msc->done);
	msc_dev_free(msc);
	intf_desc->priv = NULL;
	return 0;
}

static const struct usbh_class_driver msc_class_driver = {
	.driver_name = "usbh_msc",
	.driver_desc = "minimal BOT mass storage (S5 §3.7)",
	.connect = usbh_msc_connect,
	.disconnect = usbh_msc_disconnect,
};

CLASS_INFO_DEFINE const struct usbh_class_info msc_class_info = {
	.match_flags = USB_CLASS_MATCH_INTF_CLASS |
		       USB_CLASS_MATCH_INTF_SUBCLASS |
		       USB_CLASS_MATCH_INTF_PROTOCOL,
	.bInterfaceClass = 0x08U,
	.bInterfaceSubClass = 0x06U,
	.bInterfaceProtocol = 0x50U,
	.bInterfaceNumber = 0,
	.id_table = NULL,
	.class_driver = &msc_class_driver,
};

/* --- the `usbh_msc` acceptance command set (S4 registration discipline) ------ */

static struct usbh_msc_dev *msc_lookup(uint32_t idx)
{
	return (idx < MSC_DEV_MAX) ? (g_msc_dev[idx].inuse ? &g_msc_dev[idx]
							   : NULL)
				   : NULL;
}

static void msc_print_list(chry_shell_t *csh)
{
	uint32_t i;
	int live = 0;

	for (i = 0U; i < MSC_DEV_MAX; i++) {
		struct usbh_msc_dev *msc = &g_msc_dev[i];

		if (msc->inuse == 0U) {
			continue;
		}
		live++;
		csh_printf(csh, "msc%u: bus%u addr%u '%s' '%s' blocks=%u "
			   "bsize=%u\n",
			   i, (uint32_t)msc->hport->bus->busid,
			   (uint32_t)msc->hport->dev_addr, msc->vendor,
			   msc->product, msc->block_count, msc->block_size);
	}
	if (live == 0) {
		csh_printf(csh, "no msc device claimed\n");
	}
}

static int msc_check_range(chry_shell_t *csh, struct usbh_msc_dev *msc,
			   uint32_t lba, uint32_t blocks)
{
	if (msc->block_size == 0U) {
		csh_printf(csh, "msc: capacity unknown\n");
		return -1;
	}
	if ((lba >= msc->block_count) ||
	    (blocks > (msc->block_count - lba))) {
		csh_printf(csh, "msc: range %u+%u beyond capacity %u\n",
			   lba, blocks, msc->block_count);
		return -1;
	}
	if (msc->block_size == 0U) {
		return -1;
	}
	return 0;
}

/* Fill/verify one chunk; `fill` selects write-vs-check. Returns 0 or -1. */
static int msc_chunk_op(chry_shell_t *csh, struct usbh_msc_dev *msc,
			uint32_t lba, uint32_t blocks, uint8_t pattern,
			uint8_t fill)
{
	uint32_t off;
	int ret;

	for (off = 0U; off < MSC_CHUNK_BYTES; off++) {
		msc->data[off] = pattern;
	}
	ret = msc_rw10(msc, fill ? SCSI_WRITE10 : SCSI_READ10,
		       lba, (uint16_t)blocks, msc->data);
	if (ret != 0) {
		csh_printf(csh, "msc: %s %u@%u fail (%d)\n",
			   fill ? "write" : "read", blocks, lba, ret);
		return -1;
	}
	if (fill == 0U) {
		for (off = 0U; off < (blocks * msc->block_size); off++) {
			if (msc->data[off] != pattern) {
				csh_printf(csh, "msc: verify MISMATCH at "
					   "byte %u (got %02x want %02x)\n",
					   off, msc->data[off], pattern);
				return -1;
			}
		}
	}
	return 0;
}

/* Split a block range into 32 KiB chunks and run op on each. */
static int msc_range_op(chry_shell_t *csh, struct usbh_msc_dev *msc,
			uint32_t lba, uint32_t blocks, uint8_t pattern,
			uint8_t fill)
{
	uint32_t done = 0U;

	while (done < blocks) {
		uint32_t step = blocks - done;
		uint32_t bytes;

		if (step > MSC_CHUNK_BLOCKS) {
			step = MSC_CHUNK_BLOCKS;
		}
		bytes = step * msc->block_size;
		if (bytes > MSC_CHUNK_BYTES) {
			/* non-512 bsize: keep within the buffer */
			step = MSC_CHUNK_BYTES / msc->block_size;
			bytes = step * msc->block_size;
		}
		if (msc_chunk_op(csh, msc, lba + done, (uint32_t)step,
				 pattern, fill) != 0) {
			return -1;
		}
		done += step;
	}
	return 0;
}

static void msc_print_head(chry_shell_t *csh, struct usbh_msc_dev *msc)
{
	uint32_t i;

	csh_printf(csh, "msc: data[0..31]");
	for (i = 0U; i < 32U; i++) {
		csh_printf(csh, "%c%02x",
			   ((i % 8U) == 0U) ? ' ' : '-', msc->data[i]);
	}
	csh_printf(csh, "\n");
}

static int cmd_usbh_msc(int argc, char **argv)
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	uint32_t idx = 0U;
	char **arg = &argv[1];
	int left = argc - 1;
	struct usbh_msc_dev *msc;

	if ((left >= 1) && (*arg[0] >= '0') && (*arg[0] <= '9')) {
		idx = (uint32_t)atoi(arg[0]);
		arg++;
		left--;
	}

	if ((left == 0) || ((left == 1) && (strcmp(arg[0], "list") == 0))) {
		msc_print_list(csh);
		return 0;
	}

	msc = msc_lookup(idx);
	if (msc == NULL) {
		csh_printf(csh, "msc%u: not claimed (see `usbh_msc list`)\n",
			   idx);
		return 1;
	}

	if (strcmp(arg[0], "read") == 0) {
		uint32_t lba, blocks;
		int mismatch;

		if (left != 3) {
			goto usage;
		}
		lba = (uint32_t)strtol(arg[1], NULL, 0);
		blocks = (uint32_t)strtol(arg[2], NULL, 0);
		if ((blocks == 0U) || msc_check_range(csh, msc, lba, blocks)) {
			return 1;
		}
		usb_osal_mutex_take(msc->lock);
		mismatch = msc_range_op(csh, msc, lba, blocks, msc->w_pattern,
					0);
		usb_osal_mutex_give(msc->lock);
		msc_print_head(csh, msc);
		if (mismatch == 0) {
			if ((msc->w_blocks == blocks) && (msc->w_lba == lba)) {
				csh_printf(csh, "msc: read %u@%u verified "
					   "against write pattern %02x OK\n",
					   blocks, lba, msc->w_pattern);
			} else {
				csh_printf(csh, "msc: read %u@%u OK (no "
					   "matching write recorded)\n",
					   blocks, lba);
			}
			return 0;
		}
		return 1;
	}

	if (strcmp(arg[0], "write") == 0) {
		uint32_t lba, blocks, pattern;

		if (left != 4) {
			goto usage;
		}
		lba = (uint32_t)strtol(arg[1], NULL, 0);
		blocks = (uint32_t)strtol(arg[2], NULL, 0);
		pattern = (uint32_t)strtol(arg[3], NULL, 0) & 0xffU;
		if ((blocks == 0U) || msc_check_range(csh, msc, lba, blocks)) {
			return 1;
		}
		usb_osal_mutex_take(msc->lock);
		if (msc_range_op(csh, msc, lba, blocks, (uint8_t)pattern,
				 1) != 0) {
			usb_osal_mutex_give(msc->lock);
			return 1;
		}
		msc->w_lba = lba;
		msc->w_blocks = blocks;
		msc->w_pattern = (uint8_t)pattern;
		usb_osal_mutex_give(msc->lock);
		csh_printf(csh, "msc: wrote %u blocks @%u pattern %02x\n",
			   blocks, lba, pattern);
		return 0;
	}

	if (strcmp(arg[0], "bw") == 0) {
		uint32_t mib = 32U;
		uint32_t mib_done = 0U, i;
		uint32_t t0, elapsed_ms;
		uint32_t blocks_per_chunk;

		if (left == 2) {
			mib = (uint32_t)strtol(arg[1], NULL, 0);
		} else if (left != 1) {
			goto usage;
		}
		if (msc_check_range(csh, msc, 0U, 2U) != 0) {
			return 1;
		}
		blocks_per_chunk = MSC_CHUNK_BYTES / msc->block_size;
		usb_osal_mutex_take(msc->lock);
		t0 = osKernelGetTickCount();
		while (mib_done < mib) {
			/* sequential read from LBA 0 */
			uint32_t chunks = (1024U * 1024U) /
					  (blocks_per_chunk *
					   msc->block_size);

			for (i = 0U; i < chunks; i++) {
				if (msc_rw10(msc, SCSI_READ10,
					     i * blocks_per_chunk,
					     (uint16_t)blocks_per_chunk,
					     msc->data) != 0) {
					usb_osal_mutex_give(msc->lock);
					csh_printf(csh, "msc: bw read fail\n");
					return 1;
				}
			}
			mib_done++;
		}
		elapsed_ms = osKernelGetTickCount() - t0;
		usb_osal_mutex_give(msc->lock);
		if (elapsed_ms == 0U) {
			elapsed_ms = 1U;
		}
		csh_printf(csh, "msc: read %u MiB in %u ms = %u.%u MB/s\n",
			   mib_done, elapsed_ms,
			   (uint32_t)(((uint64_t)mib_done * 1048576U) /
				      (elapsed_ms * 1000U)),
			   (uint32_t)((((uint64_t)mib_done * 1048576U) * 10U /
					(elapsed_ms * 1000U)) % 10U));
		return 0;
	}

	if (strcmp(arg[0], "soak") == 0) {
		uint32_t seconds, idx2 = idx;
		uint32_t t0, last_report;
		uint32_t cycles = 0U, errs = 0U;
		static const uint8_t pat[4] = { 0xA5U, 0x5AU, 0x00U, 0xFFU };
		uint32_t blocks_per_chunk;

		if ((left != 2) && (left != 3)) {
			goto usage;
		}
		seconds = (uint32_t)strtol(arg[1], NULL, 0);
		if (left == 3) {
			idx2 = (uint32_t)atoi(arg[2]);
			msc = msc_lookup(idx2);
			if (msc == NULL) {
				csh_printf(csh, "msc%u: not claimed\n", idx2);
				return 1;
			}
		}
		if (msc_check_range(csh, msc, 0U, MSC_CHUNK_BLOCKS) != 0) {
			return 1;
		}
		blocks_per_chunk = MSC_CHUNK_BYTES / msc->block_size;

		csh_printf(csh, "msc: soak %us on msc%u (%u-block chunks)\n",
			   seconds, idx2, blocks_per_chunk);
		usb_osal_mutex_take(msc->lock);
		t0 = osKernelGetTickCount();
		last_report = t0;
		while ((osKernelGetTickCount() - t0) < (seconds * 1000U)) {
			uint8_t pattern = pat[cycles & 3U];

			if (msc_chunk_op(csh, msc, 0U, blocks_per_chunk,
					 pattern, 1) != 0) {
				errs++;
				continue;
			}
			if (msc_chunk_op(csh, msc, 0U, blocks_per_chunk,
					 pattern, 0) != 0) {
				errs++;
				continue;
			}
			cycles++;
			if ((osKernelGetTickCount() - last_report) >=
			    30000U) {
				csh_printf(csh, "msc: soak %us %u cycles "
					   "%u MiB moved %u errs\n",
					   (osKernelGetTickCount() - t0) /
					   1000U, cycles,
					   (uint32_t)(((uint64_t)cycles * 2U *
					    MSC_CHUNK_BYTES) >> 20), errs);
				last_report = osKernelGetTickCount();
			}
		}
		usb_osal_mutex_give(msc->lock);
		csh_printf(csh, "msc: soak done %us cycles=%u bytes=%u MiB "
			   "errs=%u\n",
			   (osKernelGetTickCount() - t0) / 1000U, cycles,
			   (uint32_t)(((uint64_t)cycles * 2U *
			    MSC_CHUNK_BYTES) >> 20), errs);
		return (errs == 0U) ? 0 : 1;
	}

usage:
	csh_printf(csh, "usage: usbh_msc [idx] "
		   "[list|read <lba> <n>|write <lba> <n> <pat>|bw [<MiB>]|"
		   "soak <sec> [idx]]\n");
	return 1;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_usbh_msc, usbh_msc,
			  "usbh_msc [idx] [list|read|write|bw|soak]",
			  "minimal BOT mass-storage acceptance set (S5 §3.7)");
