/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * usb_msc_driver.c - the clean-room bulk-only-transport consumer driver.
 *
 * Written against the public shape of the Linux USB API face (the in-tree
 * contract header) and the bulk-only transport specification: a command block
 * of 31 bytes goes out on the bulk-OUT pipe, an optional data phase follows,
 * and a 13-byte status block comes back on the bulk-IN pipe.  The command set
 * used here is TEST UNIT READY, INQUIRY and READ CAPACITY, which is what a
 * block-device probe needs and what the identification anchors assert on.
 *
 * @design adapter consumer - ref linux:drivers/usb/storage/transport.c (the
 *         usb_stor_bulk_transfer_* call sequence: cbw out, data, csw in) and
 *         :1 of the in-tree engine's prior experience summary: fill urb,
 *         submit, check status, free.  The bytes here are ours; the protocol
 *         constants come from the bulk-only transport specification.
 */

#include <string.h>

#include "linuxkpi_usb.h"
#include "usb_msc_driver.h"

#define MOCK_MSC_VID	0x1234
#define MOCK_MSC_PID	0x5678

#define CBW_SIGNATURE	0x43425355u	/* 'USBC' little endian */
#define CSW_SIGNATURE	0x53425355u	/* 'USBS' little endian */
#define CBW_LEN		31
#define CSW_LEN		13

#define SCSI_TUR		0x00
#define SCSI_INQUIRY		0x12
#define SCSI_READ_CAPACITY	0x25

#define MSC_RESET		0xff
#define MSC_GET_MAX_LUN		0xfe

static const struct usb_device_id mock_msc_ids[] = {
	{ USB_DEVICE(MOCK_MSC_VID, MOCK_MSC_PID) },
	{ },
};

static unsigned int tag_seq;
static unsigned int tag_mismatches;
static unsigned int command_count;
static u8 last_csw[CSW_LEN];
static int probe_calls;
static int disconnect_calls;

unsigned int
mock_msc_tag_mismatches(void)
{

	return tag_mismatches;
}

const u8 *
mock_msc_last_csw(void)
{

	return last_csw;
}

unsigned int
mock_msc_command_count(void)
{

	return command_count;
}

/* one bulk transfer, synchronous shape: the submit blocks until the transfer
 * finished, so the caller reads urb->status right after the call */
static int
bulk_xfer(struct usb_device *dev, int in, void *buf, unsigned int len,
    unsigned int *actual)
{
	struct urb *urb;
	unsigned int pipe;
	int ret;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (urb == NULL)
		return -ENOMEM;
	pipe = in ? usb_rcvbulkpipe(dev, 1) : usb_sndbulkpipe(dev, 1);
	usb_fill_bulk_urb(urb, dev, pipe, buf, (int)len, NULL, NULL);
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (actual != NULL)
		*actual = urb->actual_length;
	usb_free_urb(urb);
	return ret;
}

static void
put_le32(u8 *p, u32 v)
{

	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static u32
get_le32(const u8 *p)
{

	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	    ((u32)p[3] << 24);
}

int
mock_msc_bot_cmd(struct usb_device *dev, const u8 *cdb, unsigned int cdb_len,
    int data_in, u8 *data, unsigned int data_len, unsigned int *actual_len)
{
	u8 cbw[CBW_LEN];
	u8 csw[CSW_LEN];
	u32 tag = ++tag_seq;
	unsigned int got = 0;
	int ret;

	if (cdb_len > 16)
		return -EINVAL;
	memset(cbw, 0, sizeof(cbw));
	put_le32(cbw, CBW_SIGNATURE);
	put_le32(cbw + 4, tag);
	put_le32(cbw + 8, data_len);	/* dCBWDataTransferLength */
	cbw[12] = data_in ? 0x80 : 0x00;	/* bmCBWFlags bit7 = direction */
	cbw[13] = 0;			/* bCBWLUN */
	cbw[14] = (u8)cdb_len;		/* bCBWCBLength */
	memcpy(cbw + 15, cdb, cdb_len);

	ret = bulk_xfer(dev, 0, cbw, sizeof(cbw), NULL);
	if (ret != 0)
		return ret;
	if (data_len != 0 && data != NULL) {
		ret = bulk_xfer(dev, data_in ? 1 : 0, data, data_len, &got);
		if (ret != 0)
			return ret;
	}
	ret = bulk_xfer(dev, 1, csw, sizeof(csw), NULL);
	if (ret != 0)
		return ret;

	command_count++;
	memcpy(last_csw, csw, CSW_LEN);
	if (get_le32(csw) != CSW_SIGNATURE) {
		tag_mismatches++;
		return -EIO;
	}
	if (get_le32(csw + 4) != tag) {
		tag_mismatches++;
		return -EIO;
	}
	/* report the data phase's actual length before the status verdict */
	if (actual_len != NULL)
		*actual_len = got;
	if (csw[12] != 0)		/* bCSWStatus: 0 = passed */
		return -EIO;
	return 0;
}

int
mock_msc_tur(struct usb_device *dev)
{
	u8 cdb[6] = { SCSI_TUR, 0, 0, 0, 0, 0 };

	return mock_msc_bot_cmd(dev, cdb, sizeof(cdb), 0, NULL, 0, NULL);
}

int
mock_msc_inquiry(struct usb_device *dev, u8 *buf, unsigned int len)
{
	u8 cdb[6] = { SCSI_INQUIRY, 0, 0, 0, 0, 0 };

	return mock_msc_bot_cmd(dev, cdb, sizeof(cdb), 1, buf, len, NULL);
}

int
mock_msc_read_capacity(struct usb_device *dev, u32 *blocks, u32 *block_size)
{
	u8 cdb[10] = { SCSI_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	u8 data[8];
	unsigned int got = 0;
	int ret;

	ret = mock_msc_bot_cmd(dev, cdb, sizeof(cdb), 1, data, sizeof(data),
	    &got);
	if (ret != 0)
		return ret;
	if (got != sizeof(data))
		return -EIO;
	*blocks = (((u32)data[0] << 24) | ((u32)data[1] << 16) |
	    ((u32)data[2] << 8) | data[3]) + 1u;
	*block_size = ((u32)data[4] << 24) | ((u32)data[5] << 16) |
	    ((u32)data[6] << 8) | data[7];
	return 0;
}

/* class requests (the control face of the same device) */
int
mock_msc_reset(struct usb_device *dev)
{

	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0), MSC_RESET,
	    0x21, 0, 0, NULL, 0, 1000);
}

int
mock_msc_get_max_lun(struct usb_device *dev, u8 *lun)
{

	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
	    MSC_GET_MAX_LUN, 0xa1, 0, 0, lun, 1, 1000);
}

/* ------------------------------------------------------------------ */
/* the driver entry points                                             */
/* ------------------------------------------------------------------ */

static int
mock_msc_probe(struct usb_interface *intf, const struct usb_device_id *id)
{

	(void)id;
	probe_calls++;
	usb_set_intfdata(intf, (void *)&probe_calls);
	return 0;
}

static void
mock_msc_disconnect(struct usb_interface *intf)
{

	disconnect_calls++;
	usb_set_intfdata(intf, NULL);
}

int
mock_msc_probe_calls(void)
{

	return probe_calls;
}

int
mock_msc_disconnect_calls(void)
{

	return disconnect_calls;
}

struct usb_driver mock_msc_driver = {
	.name = "mock-msc",
	.probe = mock_msc_probe,
	.disconnect = mock_msc_disconnect,
	.id_table = mock_msc_ids,
};
