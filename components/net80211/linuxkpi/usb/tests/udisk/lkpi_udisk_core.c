/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lkpi_udisk_core.c - the clean-room BOT acceptance driver (SPEC
 * S6/linuxkpi §3.7, R7).  First-party acceptance carrier, NOT a product
 * piece: it exists to drive the linuxkpi surface through the exact
 * consumption shapes the gap list calls out, on both targets.
 *
 * Clean-room basis (protocol FACTS only, no usb-storage code):
 *   - BOT three-phase transfer: CBW 31 B (signature 0x43425355 'USBC'),
 *     data phase (direction declared by CBW flag bit7), CSW 13 B
 *     (signature 0x53425355 'USBS'; status 0=OK 1=FAIL 2=PHASE).
 *     [LNX] include/linux/usb/storage.h:62-79; reference shapes read (not
 *     copied) from [LNX] drivers/usb/storage/transport.c:1120-1253.
 *   - CDB set (opcodes from [LNX] include/scsi/scsi_proto.h):
 *     TEST UNIT READY 0x00 (:20), INQUIRY 0x12 (:33, 36 B standard data,
 *     vendor@8 product@16 rev@32 per SPC-5), READ CAPACITY 0x25 (:48,
 *     8 B = big-endian block count + block size), READ(10) 0x28 (:49),
 *     WRITE(10) 0x2a (:50).
 *   - Class identity: Mass Storage 0x08 ([LNX] ch9.h:329), SCSI transparent
 *     subclass 0x06 ([LNX] storage.h:21), Bulk-Only protocol 0x50
 *     (storage.h:32).
 *   - Control requests: Bulk-Only Mass Storage Reset bRequest 0xff
 *     (storage.h:82, class/interface/out, no data), Get Max LUN 0xfe
 *     (storage.h:83, class/interface/in, 1 B; a single-LUN device STALLs
 *     this legally).
 *
 * Dual-target discipline (R7): this file compiles UNCHANGED on the Linux
 * target (real kernel headers) and on the freewebcam linuxkpi target; the
 * #ifdef count is zero and must stay zero.  Both shells only register and
 * trigger; every transfer here goes fill_urb + submit + completion-wait
 * + timeout-kill, and the control requests are the §3.6-1 control-URB
 * consumers (usb_control_msg is deliberately not used).
 */

#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/usb.h>

#include "lkpi_udisk.h"

/* the two shells drive the driver object and the test entry points */
struct usb_driver lkpi_udisk_driver;

/* ------------------------------------------------------------------ */
/* protocol constants (clean-room facts, see the file header)          */
/* ------------------------------------------------------------------ */

#define UD_CBW_LEN	31
#define UD_CSW_LEN	13
#define UD_CBW_SIG	0x43425355u
#define UD_CSW_SIG	0x53425355u
#define UD_CSW_OK	0u
#define UD_CSW_FAIL	1u
#define UD_CSW_PHASE	2u

#define UD_DIR_IN	0x80u
#define UD_DIR_OUT	0x00u

#define UD_CDB_TUR	0x00u
#define UD_CDB_INQUIRY	0x12u
#define UD_CDB_CAPACITY	0x25u
#define UD_CDB_READ10	0x28u
#define UD_CDB_WRITE10	0x2au

#define UD_CTRL_RESET	0xffu	/* Bulk-Only Mass Storage Reset */
#define UD_CTRL_MAXLUN	0xfeu	/* Get Max LUN */

#define UD_TIMEOUT_MS	10000
#define UD_CTRL_TIMEOUT_MS 5000

#define UD_SECTOR_DEF	512u
#define UD_TEST_SECTORS	8u
/* 128 KiB per command - the vendored msc adapter's chunk (its 37.3/24.2
 * MB/s P5-e baseline amortizes the BOT turnaround at exactly this size;
 * the kpi target's chunk was 32 KiB until the P6-c form round and paid
 * 4x the CBW/CSW round trips for it) */
#define UD_BW_SECTORS	256u
#define UD_BUF_SECTORS	UD_BW_SECTORS

struct ud_cbw {
	u32	signature;
	u32	tag;
	u32	data_len;
	u8	flags;
	u8	lun;
	u8	cdb_len;
	u8	cdb[16];
} __packed;

struct ud_csw {
	u32	signature;
	u32	tag;
	u32	residue;
	u8	status;
} __packed;

/* the 8-byte control-setup line, built locally so both targets' headers
 * stay out of the way ([LNX] spells it usb_ctrlrequest, CherryUSB
 * usb_setup_packet; the wire layout is the USB spec's) */
struct ud_setup {
	u8	bmRequestType;
	u8	bRequest;
	u16	wValue;
	u16	wIndex;
	u16	wLength;
} __packed;

/* ------------------------------------------------------------------ */
/* driver context                                                      */
/* ------------------------------------------------------------------ */

struct ud_ctx {
	struct usb_device	*udev;
	struct usb_interface	*intf;
	struct urb		*urb;
	u8			bulk_in;
	u8			bulk_out;
	u16			maxp;
	u8			max_lun;
	u32			blocks;
	u32			block_size;
	u32			tag;
	/* control-phase scratch, heap-resident: the usb core DMA-maps the
	 * setup line and the data buffer and rejects stack objects with
	 * -EAGAIN (hcd.c object_is_on_stack - first-target round 2 fail);
	 * the usb-storage shape this driver mirrors keeps its iobuf in the
	 * device context for the same reason ([LNX] storage usb.c:544) */
	struct ud_setup		ctrl_setup;
	u8			ctrl_buf[8];
	/* the one sync-completion shape both control and bulk ride */
	struct completion	done;
	int			status;
	u32			actual;
	struct ud_cbw		*cbw;
	struct ud_csw		*csw;
	u8			*data;
	char			inq[36];
	bool			alive;
};

/* ------------------------------------------------------------------ */
/* the sync urb shape ([LNX] usb-storage's usb_stor_msg_common family:  */
/* context = completion, wait with timeout, kill on timeout)            */
/* ------------------------------------------------------------------ */

static void
ud_sync_complete(struct urb *urb)
{
	struct ud_ctx *ctx = urb->context;

	ctx->status = urb->status;
	ctx->actual = urb->actual_length;
	complete(&ctx->done);
}

static int
ud_wait(struct ud_ctx *ctx, unsigned int timeout_ms)
{
	unsigned long left;

	left = wait_for_completion_timeout(&ctx->done,
	    msecs_to_jiffies(timeout_ms));
	if (left == 0) {
		usb_kill_urb(ctx->urb);
		/* the kill's own -ENOENT completion raced the timeout;
		 * report the timeout, not the cancellation */
		return -ETIMEDOUT;
	}
	return ctx->status;
}

/* ------------------------------------------------------------------ */
/* BOT transport                                                       */
/* ------------------------------------------------------------------ */

static void
ud_cbw_build(struct ud_ctx *ctx, const u8 *cdb, u8 cdb_len, u32 data_len,
    u8 dir)
{
	u32 tag = ++ctx->tag;

	memset(ctx->cbw, 0, UD_CBW_LEN);
	ctx->cbw->signature = cpu_to_le32(UD_CBW_SIG);
	ctx->cbw->tag = cpu_to_le32(tag);
	ctx->cbw->data_len = cpu_to_le32(data_len);
	ctx->cbw->flags = dir;
	ctx->cbw->lun = 0;
	ctx->cbw->cdb_len = cdb_len;
	memcpy(ctx->cbw->cdb, cdb, cdb_len);
}

static int
ud_bulk_msg(struct ud_ctx *ctx, u8 ep, void *buf, u32 len, u32 dir,
    unsigned int timeout_ms)
{
	unsigned int pipe;
	int ret;

	pipe = dir == UD_DIR_IN ?
	    usb_rcvbulkpipe(ctx->udev, ep) : usb_sndbulkpipe(ctx->udev, ep);
	usb_fill_bulk_urb(ctx->urb, ctx->udev, pipe, buf, (int)len,
	    ud_sync_complete, ctx);
	reinit_completion(&ctx->done);
	ret = usb_submit_urb(ctx->urb, GFP_NOIO);
	if (ret != 0)
		return ret;
	return ud_wait(ctx, timeout_ms);
}

/* one BOT command: CBW out, optional data phase, CSW in */
static int
ud_transport(struct ud_ctx *ctx, const u8 *cdb, u8 cdb_len, u8 dir,
    void *data, u32 data_len)
{
	struct ud_cbw *cbw = ctx->cbw;
	struct ud_csw *csw = ctx->csw;
	int ret;

	ud_cbw_build(ctx, cdb, cdb_len, data_len, dir);
	ret = ud_bulk_msg(ctx, ctx->bulk_out, cbw, UD_CBW_LEN, UD_DIR_OUT,
	    UD_TIMEOUT_MS);
	if (ret != 0) {
		pr_err("lkpi_udisk: CBW send failed %d\n", ret);
		return ret;
	}
	if (data_len != 0) {
		ret = ud_bulk_msg(ctx,
		    dir == UD_DIR_IN ? ctx->bulk_in : ctx->bulk_out,
		    data, data_len, dir, UD_TIMEOUT_MS);
		if (ret != 0) {
			pr_err("lkpi_udisk: data phase failed %d\n", ret);
			return ret;
		}
	}
	ret = ud_bulk_msg(ctx, ctx->bulk_in, csw, UD_CSW_LEN, UD_DIR_IN,
	    UD_TIMEOUT_MS);
	if (ret != 0) {
		pr_err("lkpi_udisk: CSW recv failed %d\n", ret);
		return ret;
	}
	if (le32_to_cpu(csw->signature) != UD_CSW_SIG) {
		pr_err("lkpi_udisk: CSW signature %08x\n",
		    le32_to_cpu(csw->signature));
		return -EIO;
	}
	if (csw->status == UD_CSW_PHASE) {
		pr_err("lkpi_udisk: CSW PHASE error\n");
		return -EPROTO;
	}
	if (csw->status == UD_CSW_FAIL) {
		pr_err("lkpi_udisk: CSW FAIL (residue %u, REQUEST SENSE "
		    "not in round 1)\n", le32_to_cpu(csw->residue));
		return -EIO;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* control requests (the §3.6-1 consumers: fill_control_urb + submit)   */
/* ------------------------------------------------------------------ */

static int
ud_ctrl_msg(struct ud_ctx *ctx, u8 requesttype, u8 request, u16 value,
    u16 index, void *buf, u16 size, unsigned int timeout_ms)
{
	struct ud_setup *setup = &ctx->ctrl_setup;
	unsigned int pipe;
	int ret;

	setup->bmRequestType = requesttype;
	setup->bRequest = request;
	setup->wValue = cpu_to_le16(value);
	setup->wIndex = cpu_to_le16(index);
	setup->wLength = cpu_to_le16(size);
	/* the pipe direction must match the setup line's (the usb core
	 * rejects a mismatch with -EAGAIN - first-target round 1 fail) */
	pipe = (requesttype & USB_DIR_IN) ?
	    usb_rcvctrlpipe(ctx->udev, 0) : usb_sndctrlpipe(ctx->udev, 0);
	usb_fill_control_urb(ctx->urb, ctx->udev, pipe,
	    (unsigned char *)setup, buf, (int)size, ud_sync_complete, ctx);
	reinit_completion(&ctx->done);
	ret = usb_submit_urb(ctx->urb, GFP_NOIO);
	if (ret != 0)
		return ret;
	return ud_wait(ctx, timeout_ms);
}

/* ------------------------------------------------------------------ */
/* SCSI commands                                                       */
/* ------------------------------------------------------------------ */

static int
ud_test_unit_ready(struct ud_ctx *ctx)
{
	const u8 cdb[6] = { UD_CDB_TUR };
	int ret, i;

	for (i = 0; i < 3; i++) {
		ret = ud_transport(ctx, cdb, 6, UD_DIR_OUT, NULL, 0);
		if (ret == 0)
			return 0;
		msleep(200);
	}
	return ret;
}

static int
ud_inquiry(struct ud_ctx *ctx)
{
	const u8 cdb[6] = { UD_CDB_INQUIRY, 0x00, 0x00, 0x00, 36, 0x00 };
	int ret;

	memset(ctx->inq, 0, sizeof(ctx->inq));
	ret = ud_transport(ctx, cdb, 6, UD_DIR_IN, ctx->inq, 36);
	if (ret == 0)
		pr_info("lkpi_udisk: INQUIRY vendor='%.8s' product='%.16s' "
		    "rev='%.4s'\n", &ctx->inq[8], &ctx->inq[16],
		    &ctx->inq[32]);
	return ret;
}

static int
ud_read_capacity(struct ud_ctx *ctx)
{
	/* READ CAPACITY(10): 8-byte big-endian reply = last block number
	 * (count-1) + block size ([LNX] scsi_proto.h:48); the data buffer
	 * rides the heap-resident context (the usb core rejects stack
	 * objects with -EAGAIN) */
	const u8 cdb[10] = { UD_CDB_CAPACITY };
	u8 *buf = ctx->ctrl_buf;
	u32 last, size;
	int ret;

	ret = ud_transport(ctx, cdb, 10, UD_DIR_IN, buf, 8);
	if (ret != 0)
		return ret;
	last = ((u32)buf[0] << 24) | ((u32)buf[1] << 16) |
	    ((u32)buf[2] << 8) | (u32)buf[3];
	size = ((u32)buf[4] << 24) | ((u32)buf[5] << 16) |
	    ((u32)buf[6] << 8) | (u32)buf[7];
	ctx->blocks = last + 1;
	ctx->block_size = size != 0 ? size : UD_SECTOR_DEF;
	pr_info("lkpi_udisk: READ CAPACITY blocks=%u block_size=%u "
	    "(%llu MiB)\n", ctx->blocks, ctx->block_size,
	    ((u64)ctx->blocks * ctx->block_size) >> 20);
	return 0;
}

static int
ud_rw10(struct ud_ctx *ctx, bool write, u32 lba, u16 nsectors, void *data)
{
	u8 cdb[10];
	u32 len = (u32)nsectors * ctx->block_size;

	memset(cdb, 0, 10);
	cdb[0] = write ? UD_CDB_WRITE10 : UD_CDB_READ10;
	cdb[1] = 0;
	cdb[2] = (u8)(lba >> 24);
	cdb[3] = (u8)(lba >> 16);
	cdb[4] = (u8)(lba >> 8);
	cdb[5] = (u8)lba;
	cdb[7] = (u8)(nsectors >> 8);
	cdb[8] = (u8)nsectors;
	return ud_transport(ctx, cdb, 10,
	    write ? UD_DIR_OUT : UD_DIR_IN, data, len);
}

/* ------------------------------------------------------------------ */
/* probe / disconnect                                                  */
/* ------------------------------------------------------------------ */

static int
ud_find_eps(struct ud_ctx *ctx)
{
	struct usb_host_interface *alt = ctx->intf->cur_altsetting;
	unsigned int i;

	for (i = 0; i < (unsigned int)alt->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *d = &alt->endpoint[i].desc;

		if (usb_endpoint_xfer_bulk(d) && usb_endpoint_dir_in(d))
			ctx->bulk_in = (u8)usb_endpoint_num(d);
		if (usb_endpoint_xfer_bulk(d) && usb_endpoint_dir_out(d))
			ctx->bulk_out = (u8)usb_endpoint_num(d);
	}
	if (ctx->bulk_in == 0 || ctx->bulk_out == 0)
		return -ENODEV;
	ctx->maxp = usb_maxpacket(ctx->udev,
	    (int)usb_rcvbulkpipe(ctx->udev, ctx->bulk_in));
	if (ctx->maxp == 0)
		ctx->maxp = 512;
	return 0;
}

static int
ud_probe_identify(struct ud_ctx *ctx)
{
	u8 *lun = ctx->ctrl_buf;
	int ret;

	/* Get Max LUN (class/in/interface, 1 B): a single-LUN device
	 * STALLs legally - record, not error ([LNX] storage.h:83/:91) */
	ret = ud_ctrl_msg(ctx, USB_DIR_IN | USB_TYPE_CLASS |
	    USB_RECIP_INTERFACE, UD_CTRL_MAXLUN, 0,
	    (u16)ctx->intf->cur_altsetting->desc.bInterfaceNumber,
	    lun, 1, UD_CTRL_TIMEOUT_MS);
	if (ret == 0 && ctx->actual >= 1) {
		ctx->max_lun = *lun;
	} else if (ret == -EPIPE || (ret == 0 && ctx->actual == 0)) {
		ctx->max_lun = 0;
		pr_info("lkpi_udisk: Get Max LUN STALL (single LUN, "
		    "legal)\n");
	} else {
		pr_err("lkpi_udisk: Get Max LUN failed %d\n", ret);
		return ret;
	}
	return 0;
}

static void
ud_disconnect(struct usb_interface *intf)
{
	struct ud_ctx *ctx = usb_get_intfdata(intf);

	if (ctx == NULL)
		return;
	usb_set_intfdata(intf, NULL);
	ctx->alive = false;
	usb_free_urb(ctx->urb);
	kfree(ctx->cbw);
	kfree(ctx->csw);
	kfree(ctx->data);
	kfree(ctx);
	pr_info("lkpi_udisk: disconnected\n");
}

/* ------------------------------------------------------------------ */
/* acceptance entries (the shells and the module param drive these)     */
/* ------------------------------------------------------------------ */

/* the shell's post-probe hook (weak: the RTOS shell keeps the default;
 * the Linux shell overrides it to auto-run the self-test on module
 * params) */
__weak void
ud_post_probe(struct usb_interface *intf)
{
	(void)intf;
}

int
lkpi_udisk_inq(struct usb_interface *intf)
{
	struct ud_ctx *ctx = usb_get_intfdata(intf);

	if (ctx == NULL || !ctx->alive)
		return -ENODEV;
	return ud_inquiry(ctx);
}

int
lkpi_udisk_cap(struct usb_interface *intf)
{
	struct ud_ctx *ctx = usb_get_intfdata(intf);
	int ret;

	if (ctx == NULL || !ctx->alive)
		return -ENODEV;
	ret = ud_read_capacity(ctx);
	if (ret != 0)
		return ret;
	return 0;
}

int
lkpi_udisk_pattern(struct usb_interface *intf, u32 lba, u8 pattern)
{
	struct ud_ctx *ctx = usb_get_intfdata(intf);
	u16 n = UD_TEST_SECTORS;
	u32 len;
	int ret;

	if (ctx == NULL || !ctx->alive)
		return -ENODEV;
	len = n * ctx->block_size;
	memset(ctx->data, (int)pattern, len);
	ret = ud_rw10(ctx, true, lba, n, ctx->data);
	if (ret != 0) {
		pr_err("lkpi_udisk: pattern 0x%02x WRITE failed %d\n",
		    pattern, ret);
		return ret;
	}
	memset(ctx->data, 0, len);
	ret = ud_rw10(ctx, false, lba, n, ctx->data);
	if (ret != 0) {
		pr_err("lkpi_udisk: pattern 0x%02x READ failed %d\n",
		    pattern, ret);
		return ret;
	}
	{
		u8 expect = pattern;
		u32 i;

		for (i = 0; i < len; i++) {
			if (ctx->data[i] != expect) {
				pr_err("lkpi_udisk: pattern 0x%02x MISMATCH "
				    "at %u (read %02x)\n", pattern, i,
				    ctx->data[i]);
				return -EIO;
			}
		}
	}
	pr_info("lkpi_udisk: pattern 0x%02x write/read OK (%u bytes)\n",
	    pattern, len);
	return 0;
}

/* sequential read bandwidth over one lba window (MB/s, recorded not
 * gated); write=true measures the write side instead */
int
lkpi_udisk_bw(struct usb_interface *intf, u32 mib, bool write)
{
	struct ud_ctx *ctx = usb_get_intfdata(intf);
	u32 sectors_per_chunk = UD_BW_SECTORS;
	u64 total = (u64)mib << 20;
	u64 done = 0;
	u64 t0, t1;
	u32 lba;
	int ret;

	if (ctx == NULL || !ctx->alive)
		return -ENODEV;
	if (ctx->block_size == 0)
		ctx->block_size = UD_SECTOR_DEF;
	lba = ctx->blocks > 4096 ? ctx->blocks - 4096 : 1024;
	memset(ctx->data, (int)(write ? 0xA5 : 0x5A),
	    (int)(sectors_per_chunk * ctx->block_size));
	t0 = jiffies;
	while (done < total) {
		ret = ud_rw10(ctx, write, lba, (u16)sectors_per_chunk,
		    ctx->data);
		if (ret != 0) {
			pr_err("lkpi_udisk: bw transfer failed %d\n", ret);
			return ret;
		}
		done += (u64)sectors_per_chunk * ctx->block_size;
		if (++lba + sectors_per_chunk > ctx->blocks)
			lba = 1024;
	}
	t1 = jiffies;
	{
		u64 ms = (u64)jiffies_to_msecs((unsigned long)(t1 - t0));
		u64 mb = done >> 20;

		/* MB/s = MiB transferred * 1000 / elapsed ms; integer +
		 * hundredths, recorded not gated (spec 判据 P6-c-2) */
		pr_info("lkpi_udisk: bw %s %llu MiB in %llu ms = %llu.%02llu "
		    "MB/s\n", write ? "write" : "read", mb, ms,
		    ms != 0 ? (mb * 1000ULL) / ms : 0,
		    ms != 0 ? ((mb * 100000ULL) / ms) % 100ULL : 0);
	}
	return 0;
}

/* Bulk-Only Mass Storage Reset (class OUT on ep0, spec BOT 5.1): the
 * control-side companion to the data plane - P6-b 判据 4's second leg */
int
lkpi_udisk_reset(struct usb_interface *intf)
{
	struct ud_ctx *ctx = usb_get_intfdata(intf);
	int ret;

	if (ctx == NULL || !ctx->alive)
		return -ENODEV;
	ret = ud_ctrl_msg(ctx, USB_DIR_OUT | USB_TYPE_CLASS |
	    USB_RECIP_INTERFACE, UD_CTRL_RESET, 0,
	    (u16)ctx->intf->cur_altsetting->desc.bInterfaceNumber,
	    NULL, 0, UD_CTRL_TIMEOUT_MS);
	if (ret == 0)
		pr_info("lkpi_udisk: BOM reset OK\n");
	else
		pr_err("lkpi_udisk: BOM reset failed %d\n", ret);
	return ret;
}

/* the soak: alternating 128 KiB write/read/verify rounds over a rolling
 * LBA window until the deadline - the 600s stability gate (spec P6-c
 * 判据 3).  Errors are counted, not retried: any nonzero error count
 * fails the gate. */
int
lkpi_udisk_soak(struct usb_interface *intf, u32 seconds)
{
	static const u8 patterns[4] = { 0xA5, 0x5A, 0x00, 0xFF };
	struct ud_ctx *ctx = usb_get_intfdata(intf);
	u32 sectors = UD_BW_SECTORS;
	u64 deadline, next_report;
	u32 lba, round = 0, errs = 0;
	u64 bytes = 0;
	u8 pat = 0;

	if (ctx == NULL || !ctx->alive)
		return -ENODEV;
	if (ctx->block_size == 0)
		ctx->block_size = UD_SECTOR_DEF;
	if (ctx->blocks > sectors * 64)
		lba = ctx->blocks - sectors * 64;
	else
		lba = 1024;
	deadline = jiffies + msecs_to_jiffies(seconds * 1000u);
	next_report = jiffies + msecs_to_jiffies(60000u);
	while ((long)(deadline - jiffies) > 0) {
		u32 len = sectors * ctx->block_size;
		u32 i;
		int ret;

		memset(ctx->data, (int)patterns[pat], len);
		pat = (pat + 1u) % 4u;
		ret = ud_rw10(ctx, true, lba, (u16)sectors, ctx->data);
		if (ret != 0) {
			pr_err("lkpi_udisk: soak WRITE failed %d (round %u "
			    "lba %u)\n", ret, round, lba);
			errs++;
			continue;
		}
		memset(ctx->data, 0, len);
		ret = ud_rw10(ctx, false, lba, (u16)sectors, ctx->data);
		if (ret != 0) {
			pr_err("lkpi_udisk: soak READ failed %d (round %u "
			    "lba %u)\n", ret, round, lba);
			errs++;
			continue;
		}
		for (i = 0; i < len; i++) {
			if (ctx->data[i] != patterns[(pat + 3u) % 4u]) {
				pr_err("lkpi_udisk: soak MISMATCH at round %u "
				    "lba %u off %u\n", round, lba, i);
				errs++;
				break;
			}
		}
		round++;
		bytes += len;
		lba += sectors;
		if (lba + sectors > ctx->blocks)
			lba = 1024;
		if ((long)(next_report - jiffies) <= 0) {
			pr_info("lkpi_udisk: soak %u rounds, %llu MiB, "
			    "%u error(s)\n", round, bytes >> 20, errs);
			next_report = jiffies + msecs_to_jiffies(60000u);
		}
	}
	pr_info("lkpi_udisk: soak done: %u rounds, %llu MiB in %us, "
	    "%u error(s) -> %s\n", round, bytes >> 20, seconds, errs,
	    errs == 0 ? "PASS" : "FAIL");
	return errs == 0 ? 0 : -EIO;
}

/* the full self-test: TUR + INQUIRY + READ CAPACITY + the four-pattern
 * write/read walk (both targets run exactly this: same disk, same CDB
 * set, same LBA and pattern set - the dual-target comparison key, spec
 * §3.7-6) */
int
lkpi_udisk_test(struct usb_interface *intf, u32 lba)
{
	static const u8 patterns[4] = { 0xA5, 0x5A, 0x00, 0xFF };
	struct ud_ctx *ctx = usb_get_intfdata(intf);
	unsigned int i;
	int ret;

	if (ctx == NULL || !ctx->alive)
		return -ENODEV;
	ret = ud_test_unit_ready(ctx);
	if (ret != 0) {
		pr_err("lkpi_udisk: TEST UNIT READY failed %d\n", ret);
		return ret;
	}
	ret = ud_inquiry(ctx);
	if (ret != 0)
		return ret;
	ret = ud_read_capacity(ctx);
	if (ret != 0)
		return ret;
	for (i = 0; i < 4; i++) {
		ret = lkpi_udisk_pattern(intf, lba, patterns[i]);
		if (ret != 0)
			return ret;
	}
	pr_info("lkpi_udisk: selftest PASS (lba %u, %u sectors, patterns "
	    "A5/5A/00/FF)\n", lba, (unsigned)UD_TEST_SECTORS);
	return 0;
}

/* ------------------------------------------------------------------ */
/* usb_driver                                                          */
/* ------------------------------------------------------------------ */

static int
ud_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct ud_ctx *ctx;
	int ret;

	(void)id;
	/* the modeswitch guard, driver-side (the RTOS claim hook carries
	 * the same negative table, R3): the 0bda:1a2b CD-ROM persona also
	 * exposes a 08/06/50 interface (P5-d observed the cherryusb MSC
	 * class claiming it), and its BOT state answers garbage - the
	 * acceptance driver must never grab it */
	if (le16_to_cpu(udev->descriptor.idVendor) == 0x0bda &&
	    le16_to_cpu(udev->descriptor.idProduct) == 0x1a2b) {
		pr_info("lkpi_udisk: 0bda:1a2b CD-ROM persona refused "
		    "(modeswitch owns it)\n");
		return -ENODEV;
	}
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL)
		return -ENOMEM;
	ctx->udev = udev;
	ctx->intf = intf;
	ctx->alive = true;
	init_completion(&ctx->done);
	ctx->cbw = kzalloc(UD_CBW_LEN, GFP_KERNEL);
	ctx->csw = kzalloc(UD_CSW_LEN, GFP_KERNEL);
	ctx->data = kzalloc((size_t)UD_BUF_SECTORS * UD_SECTOR_DEF,
	    GFP_KERNEL);
	ctx->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (ctx->cbw == NULL || ctx->csw == NULL || ctx->data == NULL ||
	    ctx->urb == NULL) {
		ret = -ENOMEM;
		goto fail;
	}
	ret = ud_find_eps(ctx);
	if (ret != 0)
		goto fail;
	ret = ud_probe_identify(ctx);
	if (ret != 0)
		goto fail;
	usb_set_intfdata(intf, ctx);
	pr_info("lkpi_udisk: claimed %04x:%04x bulk in ep%02x out ep%02x "
	    "maxp %u maxlun %u\n", le16_to_cpu(udev->descriptor.idVendor),
	    le16_to_cpu(udev->descriptor.idProduct),
	    ctx->bulk_in, ctx->bulk_out, ctx->maxp, ctx->max_lun);
	/* the shell's post-probe hook: the Linux shell auto-runs the
	 * self-test here (module params), the RTOS shell leaves the weak
	 * default (trigger runs from the shell commands) */
	ud_post_probe(intf);
	return 0;

fail:
	if (ctx->urb != NULL)
		usb_free_urb(ctx->urb);
	kfree(ctx->cbw);
	kfree(ctx->csw);
	kfree(ctx->data);
	kfree(ctx);
	return ret;
}

/* the class identity: Mass Storage / SCSI transparent / Bulk-Only (spec
 * 开放问题 9 - the real-Linux macro for a pure class match is
 * USB_INTERFACE_INFO; the negative table lives in the claim hook,
 * 0bda:1a2b) */
static const struct usb_device_id ud_id_table[] = {
	{
		USB_INTERFACE_INFO(0x08, 0x06, 0x50),
	},
	{ },
};

struct usb_driver lkpi_udisk_driver = {
	.name = "lkpi_udisk",
	.probe = ud_probe,
	.disconnect = ud_disconnect,
	.id_table = ud_id_table,
};
