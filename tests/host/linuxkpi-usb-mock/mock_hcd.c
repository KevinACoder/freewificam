/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * mock_hcd.c - the synthetic host controller and the scripted device model.
 *
 * One object with two faces.  The controller face implements the backend seam
 * (submit / kill / control transfer) and owns the transfer queue and its
 * worker: an asynchronous submit is queued, the worker moves the bytes and
 * then delivers exactly one completion from the worker's own context.  The
 * device face answers control requests from constant tables and runs the
 * bulk-only-transport phases (command block out, optional data, status in).
 *
 * Kill semantics follow the seam contract: a transfer still in the queue is
 * cancelled and its single completion is delivered by the killer; a transfer
 * the worker has already claimed is reported as busy, and its ordinary
 * completion follows.
 *
 * @design test double + scripted responder - ref linux:drivers/usb/gadget/udc/
 *         dummy_hcd.c:1429 (dummy_perform_transfer: the single place bytes move
 *         across a synthetic link), :392 (link state advanced by injected
 *         edges) and :2393 (protocol stall signalled per request).
 */

#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include <linux/usb.h>

#include "mock_hcd.h"

/* ------------------------------------------------------------------ */
/* the constant tables (the truth of the assertion face)               */
/* ------------------------------------------------------------------ */

/* USB 2.0 chapter 9: device descriptor, 18 bytes.  The identifiers are
 * test constants, not a claim about any real product. */
static const u8 g_desc_dev[18] = {
	18,		/* bLength */
	0x01,		/* bDescriptorType = DEVICE */
	0x00, 0x02,	/* bcdUSB 2.00 */
	0x00,		/* bDeviceClass (class is at the interface) */
	0x00,		/* bDeviceSubClass */
	0x00,		/* bDeviceProtocol */
	64,		/* bMaxPacketSize0 */
	0x34, 0x12,	/* idVendor 0x1234 */
	0x78, 0x56,	/* idProduct 0x5678 */
	0x00, 0x01,	/* bcdDevice 1.00 */
	0, 0, 0,	/* iManufacturer, iProduct, iSerialNumber */
	1,		/* bNumConfigurations */
};

/* configuration + interface + two bulk endpoints: 9 + 9 + 7 + 7 = 32 */
static const u8 g_desc_cfg[32] = {
	9, 0x02,		/* CONFIGURATION, 9 bytes */
	32, 0,			/* wTotalLength */
	1,			/* bNumInterfaces */
	1,			/* bConfigurationValue */
	0,			/* iConfiguration */
	0x80,			/* bmAttributes: bus powered */
	50,			/* bMaxPower */
	9, 0x04,		/* INTERFACE, 9 bytes */
	0,			/* bInterfaceNumber */
	0,			/* bAlternateSetting */
	2,			/* bNumEndpoints */
	0x08, 0x06, 0x50,	/* class MSC / subclass SCSI / protocol BOT */
	0,			/* iInterface */
	7, 0x05,		/* ENDPOINT, 7 bytes */
	0x81,			/* bEndpointAddress: IN, endpoint 1 */
	0x02,			/* bmAttributes: bulk */
	0x00, 0x02,		/* wMaxPacketSize 512 */
	0,			/* bInterval */
	7, 0x05,
	0x01,			/* bEndpointAddress: OUT, endpoint 1 */
	0x02,
	0x00, 0x02,
	0,
};

/* the INQUIRY response: 36 bytes, the standard layout (peripheral header,
 * vendor identification at offset 8, product identification at offset 16,
 * revision at offset 32) */
static const u8 g_inquiry[36] = {
	0x00,			/* direct access block device */
	0x00,			/* not removable */
	0x04,			/* version */
	0x02,			/* response data format */
	35,			/* additional length */
	0x00, 0x00,
	0x00,			/* soft reset */
	'V', 'M', 'O', 'C', 'K', ' ', ' ', ' ',	/* vendor */
	'U', 'M', 'A', 'S', 'S', '1', ' ', ' ',
	' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',	/* product padding */
	'0', '0', '0', '1',	/* revision */
};

/* READ CAPACITY (10): last logical block, block length (big endian) */
static const u8 g_capacity[8] = {
	0x00, 0x00, 0x07, 0xFF,	/* last LBA = 2047 -> 2048 blocks */
	0x00, 0x00, 0x02, 0x00,	/* block length = 512 */
};

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

/* the pending queue holds at most one transfer per engine endpoint; the
 * engine's per-endpoint single-flight rule bounds it by LKPI_EP_N */
#define MOCK_QUEUE_MAX	16

enum bot_phase {
	BOT_IDLE = 0,
	BOT_DATA_IN,	/* the next bulk IN carries the command's data */
	BOT_DATA_OUT,	/* the next bulk OUT carries write data */
	BOT_CSW_IN,	/* the next bulk IN carries the status block */
};

struct mock_hcd {
	volatile int	link;
	int		fault;
	volatile int	hold;
	unsigned int	speed;
	unsigned int	port;

	/* scripted responder switches */
	int		stall_get_interface;
	int		stall_iface_desc;
	int		stall_ep_desc;
	int		short_device;
	int		no_string;
	int		max_lun_stall;
	int		ep_halt;

	/* bulk-only-transport device state */
	int		bot_phase;
	u8		bot_cbw[31];
	u32		bot_tag;
	u32		bot_residue;
	u8		bot_status;
	u8		bot_data[512];
	unsigned int	bot_data_len;

	struct mock_capture cap;
	unsigned int	n_control;
	unsigned int	n_bulk_in;
	unsigned int	n_bulk_out;

	/* the pending queue and its worker */
	struct lkpi_usb_be_urb	*q[MOCK_QUEUE_MAX];
	unsigned int		q_n;
	osThreadId_t		worker;
	osSemaphoreId_t		work;
	volatile int		stop;
};

static struct mock_backend g_backend;
static struct mock_hcd g_hcd;

/* ------------------------------------------------------------------ */
/* the device face: control requests                                   */
/* ------------------------------------------------------------------ */

static int
control_in(struct mock_hcd *h, const u8 *setup, u8 *buffer,
    unsigned int *moved)
{
	u8 desc_type = setup[3];
	u16 wlength = (u16)(setup[6] | ((u16)setup[7] << 8));
	const u8 *from = NULL;
	unsigned int avail = 0;

	*moved = 0;
	switch (setup[1]) {
	case 0x06:	/* GET_DESCRIPTOR */
		switch (desc_type) {
		case 0x01:	/* DEVICE */
			from = g_desc_dev;
			avail = sizeof(g_desc_dev);
			if (h->short_device && wlength >= 18) {
				/* scripted short read: the device answers
				 * fewer bytes than the request asks for */
				memcpy(buffer, from, 8);
				*moved = 8;
				return -LKPI_USB_BE_ERR_SHORT;
			}
			break;
		case 0x02:	/* CONFIGURATION */
			from = g_desc_cfg;
			avail = sizeof(g_desc_cfg);
			break;
		case 0x03:	/* STRING */
			if (h->no_string) {
				/* no string table: a short read */
				buffer[0] = 4;
				buffer[1] = 0x03;
				*moved = 2;
				return -LKPI_USB_BE_ERR_SHORT;
			}
			buffer[0] = 4;
			buffer[1] = 0x03;
			buffer[2] = 0x09;
			buffer[3] = 0x04;	/* LANGID en-US */
			*moved = 4;
			return *moved < wlength ? -LKPI_USB_BE_ERR_SHORT : 0;
		case 0x04:	/* INTERFACE: this device has no such descriptor */
		case 0x05:	/* ENDPOINT */
		case 0x06:	/* DEVICE_QUALIFIER: a high-speed device may
				 * answer or stall; this one stalls */
		default:
			return -LKPI_USB_BE_ERR_STALL;
		}
		break;
	case 0x00:	/* GET_STATUS */
		if (wlength < 2)
			return -LKPI_USB_BE_ERR_INVAL;
		if ((setup[0] & 0x1f) == 0x02)	/* endpoint recipient */
			buffer[0] = h->ep_halt ? 1 : 0;
		else
			buffer[0] = 0;
		buffer[1] = 0;
		*moved = 2;
		return 0;
	default:
		return -LKPI_USB_BE_ERR_STALL;
	}

	if (from == NULL)
		return -LKPI_USB_BE_ERR_STALL;
	if (wlength < avail)
		avail = wlength;
	memcpy(buffer, from, avail);
	*moved = avail;
	/* a data stage that could not fill wLength is the short read the
	 * Linux error contract reports as -EREMOTEIO */
	return avail < wlength ? -LKPI_USB_BE_ERR_SHORT : 0;
}

static int
control_out(struct mock_hcd *h, const u8 *setup, unsigned int *moved)
{
	u8 type = setup[1];

	*moved = 0;
	switch (type) {
	case 0x05:	/* SET_ADDRESS */
		h->link = MOCK_LINK_ADDRESSED;
		return 0;
	case 0x09:	/* SET_CONFIGURATION */
		h->link = MOCK_LINK_CONFIGURED;
		return 0;
	case 0x01:	/* CLEAR_FEATURE(endpoint) */
		if ((setup[0] & 0x1f) == 0x02) {
			h->ep_halt = 0;
			return 0;
		}
		return -LKPI_USB_BE_ERR_STALL;
	case 0xff:	/* MSC class reset */
		h->bot_phase = BOT_IDLE;
		return 0;
	default:
		return -LKPI_USB_BE_ERR_STALL;
	}
}

static int
control_transfer(struct mock_hcd *h, const u8 *setup, u8 *buffer,
    unsigned int *moved)
{
	int is_in = (setup[0] & USB_DIR_IN) != 0;
	int ret;

	*moved = 0;
	h->n_control++;
	h->cap.n_control = h->n_control;
	if (h->cap.setup_n < MOCK_SETUP_LOG)
		memcpy(h->cap.setup[h->cap.setup_n++], setup, 8);

	if (h->link == MOCK_LINK_DETACHED) {
		ret = -LKPI_USB_BE_ERR_SHUTDOWN;
	} else if (h->fault == MOCK_FAULT_STALL) {
		h->fault = MOCK_FAULT_NONE;
		ret = -LKPI_USB_BE_ERR_STALL;
	} else if (h->stall_get_interface && setup[1] == 0x0a) {
		ret = -LKPI_USB_BE_ERR_STALL;
	} else if (h->stall_iface_desc && setup[1] == 0x06 &&
	    setup[3] == 0x04) {
		ret = -LKPI_USB_BE_ERR_STALL;
	} else if (h->stall_ep_desc && setup[1] == 0x06 && setup[3] == 0x05) {
		ret = -LKPI_USB_BE_ERR_STALL;
	} else if (h->max_lun_stall && setup[1] == 0xfe) {
		ret = -LKPI_USB_BE_ERR_STALL;
	} else if (is_in) {
		ret = control_in(h, setup, buffer, moved);
	} else {
		ret = control_out(h, setup, moved);
	}

	if (h->cap.setup_n > 0)
		h->cap.setup_ret[h->cap.setup_n - 1] = ret;

	/* capture the first DEVICE descriptor read that moved bytes */
	if (setup[1] == 0x06 && setup[3] == 0x01 && *moved > 0 &&
	    h->cap.desc_dev_len == 0) {
		unsigned int n = *moved;

		if (n > sizeof(h->cap.desc_dev))
			n = sizeof(h->cap.desc_dev);
		memcpy(h->cap.desc_dev, buffer, n);
		h->cap.desc_dev_len = n;
	}
	return ret;
}

/* ------------------------------------------------------------------ */
/* the device face: bulk transfers (the bulk-only transport phases)     */
/* ------------------------------------------------------------------ */

static void
bot_parse_cbw(struct mock_hcd *h, const u8 *cbw)
{
	u32 dlen = (u32)cbw[8] | ((u32)cbw[9] << 8) |
	    ((u32)cbw[10] << 16) | ((u32)cbw[11] << 24);
	int dir_in = (cbw[12] & 0x80) != 0;

	h->bot_tag = (u32)cbw[4] | ((u32)cbw[5] << 8) |
	    ((u32)cbw[6] << 16) | ((u32)cbw[7] << 24);
	h->bot_residue = 0;
	h->bot_status = 0;
	h->bot_data_len = 0;
	if (h->cap.cbw_n < MOCK_CBW_LOG)
		memcpy(h->cap.cbw[h->cap.cbw_n++], cbw, 31);

	switch (cbw[15]) {	/* the operation code of the command block */
	case 0x00:	/* TEST UNIT READY */
		h->bot_phase = BOT_CSW_IN;
		break;
	case 0x12:	/* INQUIRY */
		memcpy(h->bot_data, g_inquiry, sizeof(g_inquiry));
		h->bot_data_len = sizeof(g_inquiry);
		if (dlen < h->bot_data_len)
			h->bot_data_len = dlen;
		h->bot_phase = BOT_DATA_IN;
		break;
	case 0x25:	/* READ CAPACITY (10) */
		memcpy(h->bot_data, g_capacity, sizeof(g_capacity));
		h->bot_data_len = sizeof(g_capacity);
		h->bot_phase = BOT_DATA_IN;
		break;
	case 0x28:	/* READ (10) */
	case 0x2a:	/* WRITE (10) */
		h->bot_data_len = dlen > sizeof(h->bot_data) ?
		    (unsigned int)sizeof(h->bot_data) : dlen;
		h->bot_phase = dir_in ? BOT_DATA_IN : BOT_DATA_OUT;
		break;
	default:
		h->bot_status = 1;	/* command failed */
		h->bot_phase = BOT_CSW_IN;
		break;
	}
}

static unsigned int
bot_csw(struct mock_hcd *h, u8 *out)
{
	u32 tag = h->bot_tag;

	out[0] = 0x55; out[1] = 0x53; out[2] = 0x42; out[3] = 0x53;	/* USBS */
	out[4] = (u8)tag;
	out[5] = (u8)(tag >> 8);
	out[6] = (u8)(tag >> 16);
	out[7] = (u8)(tag >> 24);
	out[8] = (u8)h->bot_residue;
	out[9] = (u8)(h->bot_residue >> 8);
	out[10] = (u8)(h->bot_residue >> 16);
	out[11] = (u8)(h->bot_residue >> 24);
	out[12] = h->bot_status;
	memcpy(h->cap.csw, out, 13);
	h->cap.csw_valid = 1;
	h->bot_phase = BOT_IDLE;
	return 13;
}

/* the two bulk endpoints are told apart by the identity tag the harness put
 * in hcpriv (mock_backend_create) */
static int
bulk_transfer(struct mock_hcd *h, struct lkpi_usb_be_urb *urb)
{
	u8 *buf = urb->transfer_buffer;
	u32 len = urb->transfer_buffer_length;

	if (h->link == MOCK_LINK_DETACHED)
		return -LKPI_USB_BE_ERR_SHUTDOWN;

	if (urb->ep->hcpriv == (void *)&h->n_bulk_in) {		/* IN */
		h->n_bulk_in++;
		h->cap.n_bulk_in = h->n_bulk_in;
		if (h->fault == MOCK_FAULT_STALL) {
			h->fault = MOCK_FAULT_NONE;
			return -LKPI_USB_BE_ERR_STALL;
		}
		if (h->ep_halt)
			return -LKPI_USB_BE_ERR_STALL;
		if (h->bot_phase == BOT_DATA_IN) {
			unsigned int n = h->bot_data_len < len ?
			    h->bot_data_len : len;

			memcpy(buf, h->bot_data, n);
			urb->actual_length = n;
			h->bot_residue = h->bot_data_len - n;
			h->bot_phase = BOT_CSW_IN;
			return 0;
		}
		if (h->bot_phase == BOT_CSW_IN) {
			unsigned int n = bot_csw(h, buf);

			urb->actual_length = n < len ? n : len;
			return 0;
		}
		urb->actual_length = 0;
		return 0;
	}

	h->n_bulk_out++;
	h->cap.n_bulk_out = h->n_bulk_out;
	if (h->fault == MOCK_FAULT_STALL) {
		h->fault = MOCK_FAULT_NONE;
		return -LKPI_USB_BE_ERR_STALL;
	}
	if (h->ep_halt)
		return -LKPI_USB_BE_ERR_STALL;
	if (len == 31 && buf[0] == 0x55 && buf[1] == 0x53 &&
	    buf[2] == 0x42 && buf[3] == 0x43) {		/* 'USBC' */
		bot_parse_cbw(h, buf);
		urb->actual_length = 31;
		return 0;
	}
	if (h->bot_phase == BOT_DATA_OUT) {
		unsigned int n = len < h->bot_data_len ? len : h->bot_data_len;

		memcpy(h->bot_data, buf, n);
		h->bot_data_len = n;
		urb->actual_length = n;
		h->bot_phase = BOT_CSW_IN;
		return 0;
	}
	urb->actual_length = len;
	return 0;
}

/* ------------------------------------------------------------------ */
/* the controller face                                                 */
/* ------------------------------------------------------------------ */

static void
process_urb(struct mock_hcd *h, struct lkpi_usb_be_urb *urb)
{
	unsigned int moved = 0;
	int ret;

	urb->actual_length = 0;
	urb->errorcode = 0;
	if (urb->ep == NULL) {
		urb->errorcode = -LKPI_USB_BE_ERR_INVAL;
		return;
	}
	if (urb->setup != NULL) {	/* control class rides the setup line */
		ret = control_transfer(h, (const u8 *)urb->setup,
		    (u8 *)urb->transfer_buffer, &moved);
		urb->actual_length = moved;
		if (ret < 0)
			urb->errorcode = ret;
		return;
	}
	/* the bulk path reports bytes moved through actual_length itself */
	ret = bulk_transfer(h, urb);
	if (ret < 0)
		urb->errorcode = ret;
}

/* run the oldest queued transfer to completion (the one place the
 * asynchronous path manufactures a completion, always from this context) */
static void
run_one(struct mock_hcd *h)
{
	struct lkpi_usb_be_urb *urb;
	unsigned int i;

	if (h->q_n == 0)
		return;
	urb = h->q[0];
	for (i = 1; i < h->q_n; i++)
		h->q[i - 1] = h->q[i];
	h->q_n--;
	process_urb(h, urb);
	if (urb->complete != NULL)
		urb->complete(urb->arg,
		    urb->errorcode == 0 ? (int)urb->actual_length : -1);
}

void
mock_backend_pump(struct mock_backend *b)
{

	run_one(b->hcd);
}

static void
worker_entry(void *arg)
{
	struct mock_hcd *h = arg;

	for (;;) {
		(void)osSemaphoreAcquire(h->work, osWaitForever);
		if (h->stop)
			break;
		if (h->hold)
			continue;	/* keep transfers queued (kill window) */
		run_one(h);
	}
}

/* ------------------------------------------------------------------ */
/* the seam operations                                                 */
/* ------------------------------------------------------------------ */

static int
seam_submit(void *ctx, struct lkpi_usb_be_urb *urb)
{
	struct mock_hcd *h = ((struct mock_backend *)ctx)->hcd;

	if (urb == NULL || urb->ep == NULL || urb->hport == NULL)
		return -LKPI_USB_BE_ERR_INVAL;
	if (urb->timeout != 0) {
		/* blocking submit: move the transfer now, report on return */
		if (urb->complete != NULL)
			return -LKPI_USB_BE_ERR_INVAL;
		process_urb(h, urb);
		return urb->errorcode;
	}
	if (urb->complete == NULL)
		return -LKPI_USB_BE_ERR_INVAL;
	if (h->q_n >= MOCK_QUEUE_MAX)
		return -LKPI_USB_BE_ERR_NOMEM;
	h->q[h->q_n++] = urb;
	(void)osSemaphoreRelease(h->work);
	return 0;
}

static int
seam_kill(void *ctx, struct lkpi_usb_be_urb *urb)
{
	struct mock_hcd *h = ((struct mock_backend *)ctx)->hcd;
	unsigned int i;

	if (urb == NULL)
		return -LKPI_USB_BE_ERR_INVAL;
	for (i = 0; i < h->q_n; i++) {
		if (h->q[i] != urb)
			continue;
		/* still queued: cancel it and deliver the one completion
		 * this transfer owed, from the killer's own context */
		for (; i + 1 < h->q_n; i++)
			h->q[i] = h->q[i + 1];
		h->q_n--;
		urb->actual_length = 0;
		urb->errorcode = -LKPI_USB_BE_ERR_CANCELLED;
		if (urb->complete != NULL)
			urb->complete(urb->arg, -1);
		return 0;
	}
	/* the worker has already claimed it (or it finished): the ordinary
	 * completion is on its way; report "not ours to cancel" */
	return -LKPI_USB_BE_ERR_BUSY;
}

static int
seam_control(void *ctx, struct lkpi_usb_be_hubport *port,
    const struct lkpi_usb_setup_packet *setup, void *buffer)
{
	struct mock_hcd *h = ((struct mock_backend *)ctx)->hcd;
	unsigned int moved = 0;
	u16 wlength;
	int ret;

	if (port == NULL || setup == NULL)
		return -LKPI_USB_BE_ERR_INVAL;
	wlength = setup->wLength;
	ret = control_transfer(h, (const u8 *)setup, buffer, &moved);
	if (ret < 0)
		return ret;
	if (moved < wlength)
		return -LKPI_USB_BE_ERR_SHORT;
	return 0;
}

static const struct lkpi_usb_be_ops g_ops = {
	.submit_urb = seam_submit,
	.kill_urb = seam_kill,
	.control_transfer = seam_control,
};

const struct lkpi_usb_be_ops *
mock_backend_ops(void)
{

	return &g_ops;
}

/* ------------------------------------------------------------------ */
/* lifecycle and injected edges                                        */
/* ------------------------------------------------------------------ */

struct mock_backend *
mock_backend_create(void)
{
	struct mock_backend *b = &g_backend;
	struct mock_hcd *h = &g_hcd;
	osThreadAttr_t attr = { 0 };

	memset(b, 0, sizeof(*b));
	memset(h, 0, sizeof(*h));
	b->hcd = h;
	h->link = MOCK_LINK_DETACHED;
	h->speed = USB_SPEED_HIGH;
	h->port = 1;
	h->work = osSemaphoreNew(1u, 0u, NULL);

	b->port.hcpriv = h;
	b->port.port = 1;
	b->port.ep0.hcpriv = &b->port.ep0;	/* the ep0 identity is the port */
	b->ep_in.hcpriv = &h->n_bulk_in;	/* the bulk router's tags */
	b->ep_out.hcpriv = &h->n_bulk_out;

	attr.name = "mock_hcd";
	attr.stack_size = 4096u;
	attr.priority = osPriorityNormal;
	h->worker = osThreadNew(worker_entry, h, &attr);
	if (h->worker == NULL)
		return NULL;
	return b;
}

void
mock_backend_free(struct mock_backend *b)
{

	if (b == NULL || b->hcd == NULL)
		return;
	b->hcd->stop = 1;
	(void)osSemaphoreRelease(b->hcd->work);
}

void
mock_hcd_connect(struct mock_backend *b)
{

	b->hcd->link = MOCK_LINK_ATTACHED;
}

void
mock_hcd_disconnect(struct mock_backend *b)
{

	b->hcd->link = MOCK_LINK_DETACHED;
}

enum mock_link_state
mock_hcd_link(const struct mock_backend *b)
{

	return (enum mock_link_state)b->hcd->link;
}

void
mock_hcd_fault_set(struct mock_backend *b, enum mock_fault f)
{

	b->hcd->fault = (int)f;
}

void
mock_hcd_hold_set(struct mock_backend *b, int on)
{

	b->hcd->hold = on;
}

void
mock_hcd_script(struct mock_backend *b, int key, int value)
{
	struct mock_hcd *h = b->hcd;

	switch (key) {
	case MOCK_SCRIPT_STALL_GET_INTERFACE:
		h->stall_get_interface = value;
		break;
	case MOCK_SCRIPT_STALL_IFACE_DESC:
		h->stall_iface_desc = value;
		break;
	case MOCK_SCRIPT_STALL_EP_DESC:
		h->stall_ep_desc = value;
		break;
	case MOCK_SCRIPT_SHORT_DEVICE:
		h->short_device = value;
		break;
	case MOCK_SCRIPT_NO_STRING:
		h->no_string = value;
		break;
	case MOCK_SCRIPT_MAX_LUN_STALL:
		h->max_lun_stall = value;
		break;
	default:
		break;
	}
}

void
mock_hcd_counters(const struct mock_backend *b, unsigned int *n_control,
    unsigned int *n_bulk_in, unsigned int *n_bulk_out)
{

	*n_control = b->hcd->n_control;
	*n_bulk_in = b->hcd->n_bulk_in;
	*n_bulk_out = b->hcd->n_bulk_out;
}
