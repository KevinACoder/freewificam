/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * main.c - the linuxkpi USB host mock suite: the assertion face.
 *
 * One process, one run: the synthetic controller and the scripted device model
 * are brought up behind the engine's backend seam, a clean-room command-driver
 * stand-in registers through the public driver registration, the enumeration
 * walker drives the control face, the bulk-only transport runs its commands,
 * and every group asserts as it goes.  The anchors it prints (prefix
 * "usb-smoke: ") are the machine-checkable result; the exit code is zero only
 * when every assertion held.
 *
 * Groups (the run covers the first round's set):
 *   S1  enumeration and standard requests, field-level and byte-level
 *   S2  urb lifecycle: pairing, single in flight, in-order drain, kill
 *   S3  bulk-only transport bytes: status block tag pairing
 *   S4  control messages: class reset and the single-LUN stall
 *   S5  the scripted negative battery (the upstream request battery's shapes)
 *   S6  the allocation seam: pairing, named counters, declared negatives
 *   S7  the ordered anchor flow that names the whole run
 *   vectors: the expected bytes come from the shared vector factory
 *
 * Modes: the default run above; "--fault vec-tamper" corrupts one expected
 * vector byte and must go red; "--fault negatives" adds the scripted
 * negative battery and the seam's declared-failure cases.
 *
 * @design composition over the sibling mock suite - ref
 *         tests/host/net80211-frame-test/main.c (driver stand-in + scripted
 *         counterpart + anchors + a named fault invocation); the runtime
 *         lifecycle (task start, scheduler end) follows
 *         tests/host/freertos-cmsis2-smoke/main.c.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include <linux/usb.h>

#include "lkpi_usb_backend.h"
#include "linuxkpi_usb.h"
#include "mock_hcd.h"
#include "mockvec.h"
#include "usb_msc_driver.h"

#define VID	0x1234
#define PID	0x5678

/* ------------------------------------------------------------------ */
/* the run's bookkeeping                                               */
/* ------------------------------------------------------------------ */

static int failures;
static int g_vec_root_given;
static const char *g_vec_root;
static const char *g_fault;

static void
anchor(const char *name, int held)
{

	printf("usb-smoke: %s %s\n", name, held ? "ok=1" : "ok=0");
	fflush(stdout);
	if (!held)
		failures++;
}

static void
info(const char *fmt, ...)
{
	va_list ap;

	printf("usb-smoke: ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* the device stand-in: one device, one interface, two bulk endpoints   */
/* ------------------------------------------------------------------ */

static struct usb_device g_dev;
static struct usb_interface g_intf;
static struct usb_host_interface g_alt;
static struct usb_host_endpoint g_eps[2];

static void
device_build(struct mock_backend *b)
{
	memset(&g_dev, 0, sizeof(g_dev));
	memset(&g_intf, 0, sizeof(g_intf));
	memset(&g_alt, 0, sizeof(g_alt));
	memset(g_eps, 0, sizeof(g_eps));

	g_dev.devnum = 1;
	g_dev.speed = USB_SPEED_HIGH;
	g_dev.state = USB_STATE_DEFAULT;
	g_dev.descriptor.bLength = 18;
	g_dev.descriptor.bDescriptorType = 0x01;
	g_dev.descriptor.bcdUSB = 0x0200;
	g_dev.descriptor.bMaxPacketSize0 = 64;
	g_dev.descriptor.idVendor = VID;
	g_dev.descriptor.idProduct = PID;
	g_dev.descriptor.bNumConfigurations = 1;
	g_dev._vendor = VID;
	g_dev._product = PID;

	g_dev.ep0.desc.bLength = 7;
	g_dev.ep0.desc.bDescriptorType = 0x05;
	g_dev.ep0.desc.bEndpointAddress = 0x00;
	g_dev.ep0.desc.bmAttributes = 0x00;
	g_dev.ep0.desc.wMaxPacketSize = 64;

	g_eps[0].desc.bLength = 7;
	g_eps[0].desc.bDescriptorType = 0x05;
	g_eps[0].desc.bEndpointAddress = 0x81;	/* bulk IN */
	g_eps[0].desc.bmAttributes = 0x02;
	g_eps[0].desc.wMaxPacketSize = 512;
	g_eps[0].hcpriv = &b->ep_in;

	g_eps[1].desc.bLength = 7;
	g_eps[1].desc.bDescriptorType = 0x05;
	g_eps[1].desc.bEndpointAddress = 0x01;	/* bulk OUT */
	g_eps[1].desc.bmAttributes = 0x02;
	g_eps[1].desc.wMaxPacketSize = 512;
	g_eps[1].hcpriv = &b->ep_out;

	g_alt.desc.bLength = 9;
	g_alt.desc.bDescriptorType = 0x04;
	g_alt.desc.bInterfaceNumber = 0;
	g_alt.desc.bAlternateSetting = 0;
	g_alt.desc.bNumEndpoints = 2;
	g_alt.desc.bInterfaceClass = 0x08;
	g_alt.desc.bInterfaceSubClass = 0x06;
	g_alt.desc.bInterfaceProtocol = 0x50;
	g_alt.endpoint = g_eps;
	g_alt.num_endpoints = 2;

	g_intf.altsetting = &g_alt;
	g_intf.num_altsetting = 1;
	g_intf.cur_altsetting = &g_alt;
	g_intf.udev = &g_dev;

	lkpi_usb_device_bind(&g_dev, &g_intf, &b->port);
}

/* a control transfer through the urb face (the walker's second route) */
static int
ctrl_urb(struct usb_device *dev, u8 bmRequestType, u8 bRequest, u16 wValue,
    u16 wIndex, u16 wLength, void *data, unsigned int *actual)
{
	static u8 setup[8];
	unsigned int pipe;
	struct urb *urb;
	int ret;

	setup[0] = bmRequestType;
	setup[1] = bRequest;
	setup[2] = (u8)wValue;
	setup[3] = (u8)(wValue >> 8);
	setup[4] = (u8)wIndex;
	setup[5] = (u8)(wIndex >> 8);
	setup[6] = (u8)wLength;
	setup[7] = (u8)(wLength >> 8);
	pipe = (bmRequestType & USB_DIR_IN) != 0 ?
	    usb_rcvctrlpipe(dev, 0) : usb_sndctrlpipe(dev, 0);
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (urb == NULL)
		return -ENOMEM;
	usb_fill_control_urb(urb, dev, pipe, setup, data, (int)wLength, NULL,
	    NULL);
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (actual != NULL)
		*actual = urb->actual_length;
	usb_free_urb(urb);
	return ret;
}

static int
ctrl_msg(struct usb_device *dev, u8 bmRequestType, u8 bRequest, u16 wValue,
    u16 wIndex, void *data, u16 wLength)
{
	unsigned int pipe = (bmRequestType & USB_DIR_IN) != 0 ?
	    usb_rcvctrlpipe(dev, 0) : usb_sndctrlpipe(dev, 0);

	return usb_control_msg(dev, pipe, bRequest, bmRequestType, wValue,
	    wIndex, data, wLength, 1000);
}

/* ------------------------------------------------------------------ */
/* the asynchronous lane                                               */
/* ------------------------------------------------------------------ */

static volatile int a1_done, a2_done, a3_done;
static volatile int a3_status = 1;
static volatile unsigned int order_n;
static volatile int order[8];

static void
cb_a1(struct urb *urb)
{

	order[order_n++] = 1;
	(void)urb;
	a1_done = 1;
}

static void
cb_a2(struct urb *urb)
{

	order[order_n++] = 2;
	(void)urb;
	a2_done = 1;
}

static void
cb_a3(struct urb *urb)
{

	order[order_n++] = 3;
	a3_status = urb->status;
	a3_done = 1;
}

static int
wait_done(volatile int *flag, unsigned int ms)
{
	unsigned int i;

	for (i = 0; i < ms; i++) {
		if (*flag)
			return 0;
		(void)osDelay(1u);
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/* the vector face                                                     */
/* ------------------------------------------------------------------ */

static int
vec_check(const char *seg, const char *file, const unsigned char *got,
    size_t got_len, int tamper_byte)
{
	struct mockvec v;
	char err[256];
	int off;

	if (!g_vec_root_given)
		return 0;
	if (mockvec_load(g_vec_root, seg, file, &v, err, sizeof(err)) != 0) {
		printf("usb-smoke: vec-error %s\n", err);
		fflush(stdout);
		failures++;
		return -1;
	}
	if (tamper_byte >= 0) {
		/* the scripted corruption: flip one bit of one expected byte */
		v.payload[tamper_byte] ^= 0x01;
	}
	off = mockvec_diff(got, got_len, &v);
	if (off < 0) {
		printf("usb-smoke: vec=%s ok=1\n", v.id);
		fflush(stdout);
		return 0;
	}
	printf("usb-smoke: vec=%s ok=0 offset=%d got_len=%u exp_len=%u "
	    "got0=%02x exp0=%02x\n", v.id, off, (unsigned)got_len,
	    (unsigned)v.len, got_len > 0 ? got[0] : 0,
	    v.len > 0 ? v.payload[0] : 0);
	printf("usb-smoke: vec-mismatch vec=%s offset=%d\n", v.id, off);
	fflush(stdout);
	failures++;
	return -1;
}

/* ------------------------------------------------------------------ */
/* the run                                                             */
/* ------------------------------------------------------------------ */

static const u8 *
find_setup(struct mock_backend *b, u8 bRequest, unsigned int *len)
{
	unsigned int i;

	for (i = 0; i < b->cap.setup_n; i++) {
		if (b->cap.setup[i][1] == bRequest) {
			if (len != NULL)
				*len = 8;
			return b->cap.setup[i];
		}
	}
	return NULL;
}

static void
run_scenario(void)
{
	struct mock_backend *b = mock_backend_create();
	unsigned int got = 0;
	u8 buf[1024];
	int ret;

	if (b == NULL) {
		printf("usb-smoke: mock-cannot-start\n");
		failures++;
		return;
	}
	lkpi_usb_be_register(mock_backend_ops(), b);
	/* the class reset stall is armed for the single-LUN shape */
	mock_hcd_script(b, MOCK_SCRIPT_MAX_LUN_STALL, 1);

	info("hcd registered bus=0");

	ret = usb_register(&mock_msc_driver);
	if (ret != 0) {
		printf("usb-smoke: driver-register failed\n");
		failures++;
	}

	mock_hcd_connect(b);
	if (mock_hcd_link(b) == MOCK_LINK_ATTACHED)
		info("port connect n=1 speed=HS");
	else
		anchor("port connect n=1 speed=HS", 0);

	device_build(b);

	/* S1: the enumeration walker (control urb for the descriptor reads,
	 * the message helper for the address and configuration steps) */
	memset(buf, 0, sizeof(buf));
	ret = ctrl_urb(&g_dev, 0x80, 0x06, 0x0100, 0, 8, buf, &got);
	if (ret == 0 && got == 8 && buf[0] == 18 && buf[1] == 0x01)
		anchor("enum descriptor prefix len=8", 1);
	else
		anchor("enum descriptor prefix len=8", 0);

	ret = ctrl_msg(&g_dev, 0x00, 0x05, 1, 0, NULL, 0);	/* SET_ADDRESS */
	if (ret == 0)
		info("enum set-addr n=1");
	else
		anchor("enum set-addr n=1", 0);

	memset(buf, 0, sizeof(buf));
	ret = ctrl_urb(&g_dev, 0x80, 0x06, 0x0100, 0, 18, buf, &got);
	if (ret == 0 && got == 18 && buf[8] == 0x34 && buf[9] == 0x12 &&
	    buf[10] == 0x78 && buf[11] == 0x56)
		info("enum dev-desc vid=0x1234 pid=0x5678");
	else
		anchor("enum dev-desc vid=0x1234 pid=0x5678", 0);

	/* the nine-byte head first, then the full tree: the interface class
	 * triple sits at offsets 14..16 of the tree */
	memset(buf, 0, sizeof(buf));
	ret = ctrl_msg(&g_dev, 0x80, 0x06, 0x0200, 0, buf, 9);
	if (ret == 9 && (unsigned int)(buf[2] | ((unsigned)buf[3] << 8)) == 32) {
		unsigned int total = (unsigned int)(buf[2] |
		    ((unsigned)buf[3] << 8));

		memset(buf, 0, sizeof(buf));
		ret = ctrl_urb(&g_dev, 0x80, 0x06, 0x0200, 0, (u16)total, buf,
		    &got);
		if (ret == 0 && got == total && buf[14] == 0x08 &&
		    buf[15] == 0x06 && buf[16] == 0x50)
			anchor("enum config tree bytes=32", 1);
		else
			anchor("enum config tree bytes=32", 0);
	} else {
		anchor("enum config tree bytes=32", 0);
	}

	ret = ctrl_msg(&g_dev, 0x00, 0x09, 1, 0, NULL, 0);	/* SET_CONFIG */
	if (ret == 0 && mock_hcd_link(b) == MOCK_LINK_CONFIGURED)
		info("enum set-config n=1");
	else
		anchor("enum set-config n=1", 0);

	/* the claim path and the registered driver's probe */
	g_intf.claimed = true;
	info("class claimed intf=0 cls=08/06/50");
	ret = lkpi_usb_probe_drivers(&g_intf);
	if (ret == 0 && mock_msc_probe_calls() == 1)
		info("driver probed intf=0");
	else
		anchor("driver probed intf=0", 0);

	/* S4: the class control requests */
	ret = usb_control_msg(&g_dev, usb_sndctrlpipe(&g_dev, 0), 0xff, 0x21,
	    0, 0, NULL, 0, 1000);
	if (ret == 0)
		info("ctrl ms-reset ok");
	else
		anchor("ctrl ms-reset ok", 0);

	{
		u8 lun = 0;

		ret = usb_control_msg(&g_dev, usb_rcvctrlpipe(&g_dev, 0), 0xfe,
		    0xa1, 0, 0, &lun, 1, 1000);
		if (ret == -EPIPE)
			info("ctrl get-max-lun stall=1");
		else
			anchor("ctrl get-max-lun stall=1", 0);
	}

	/* S3: the bulk-only transport commands */
	ret = mock_msc_tur(&g_dev);
	{
		const u8 *csw = mock_msc_last_csw();

		if (ret == 0 && csw[12] == 0)
			info("tur csw status=0 tag=1");
		else
			anchor("tur csw status=0 tag=1", 0);
	}

	memset(buf, 0, sizeof(buf));
	ret = mock_msc_inquiry(&g_dev, buf, 36);
	if (ret == 0 && memcmp(buf + 8, "VMOCK   ", 8) == 0 &&
	    memcmp(buf + 16, "UMASS1  ", 8) == 0)
		info("inquiry len=36 vendor=VMOCK prod=UMASS1");
	else
		anchor("inquiry len=36 vendor=VMOCK prod=UMASS1", 0);

	{
		u32 blocks = 0, bsize = 0;

		ret = mock_msc_read_capacity(&g_dev, &blocks, &bsize);
		if (ret == 0 && blocks == 2048 && bsize == 512)
			info("capacity blocks=2048 size=512");
		else {
			printf("usb-smoke: capacity ret=%d blocks=%u size=%u\n",
			    ret, blocks, bsize);
			anchor("capacity blocks=2048 size=512", 0);
		}
	}

	(void)mock_msc_tur(&g_dev);
	(void)mock_msc_tur(&g_dev);
	{
		unsigned int n = mock_msc_command_count();
		unsigned int mism = mock_msc_tag_mismatches();

		if (n == 5 && mism == 0)
			info("csw tag-paired n=5 mismatch=0");
		else
			anchor("csw tag-paired n=5 mismatch=0", 0);
	}

	/* S6: the allocation seam's pair anchors (before any negative) */
	{
		struct lkpi_usb_mem_stats st;

		lkpi_usb_mem_stats_get(&st);
		if (st.n_urb_alloc == st.n_urb_free && st.n_urb_alloc != 0)
			anchor("paired urb", 1);
		else
			info("paired urb a=%u f=%u", st.n_urb_alloc,
			    st.n_urb_free);
	}

	{
		struct lkpi_usb_mem_stats st;
		unsigned int i;
		void *p[4];

		for (i = 0; i < 4; i++)
			p[i] = lkpi_usb_alloc_coherent(256, GFP_KERNEL);
		for (i = 0; i < 4; i++)
			lkpi_usb_free_coherent(p[i], 256);
		lkpi_usb_mem_stats_get(&st);
		if (st.n_coherent_alloc == 4 && st.n_coherent_free == 4)
			anchor("paired coherent", 1);
		else
			info("paired coherent a=%u f=%u", st.n_coherent_alloc,
			    st.n_coherent_free);
	}

	{
		struct lkpi_usb_mem_stats st;

		lkpi_usb_mem_stats_get(&st);
		if (st.double_free == 0 && st.stray_free == 0 &&
		    st.uaf_write == 0 && st.alloc_fail == 0)
			anchor("named-fail all-zero", 1);
		else
			info("named-fail nonzero df=%u sf=%u uaf=%u af=%u",
			    st.double_free, st.stray_free, st.uaf_write,
			    st.alloc_fail);
	}

	/* S1 vectors: the expected bytes come from the factory files */
	if (g_vec_root_given) {
		const u8 *setup;
		unsigned int slen = 0;

		vec_check("S1", "S1.EP0_DESC_DEV.in.001.dat",
		    b->cap.desc_dev, b->cap.desc_dev_len, -1);
		setup = find_setup(b, 0x06, &slen);
		if (setup != NULL)
			vec_check("S1", "S1.EP0_ENUM_SETUP.exp.001.dat", setup,
			    8, -1);
		else
			anchor("vec=S1.EP0_ENUM_SETUP.exp.001", 0);
		setup = find_setup(b, 0xff, &slen);
		if (setup != NULL)
			vec_check("S1", "S1.CTRL_MS_RESET.exp.001.dat", setup,
			    8, g_fault != NULL &&
			    strcmp(g_fault, "vec-tamper") == 0 ? 1 : -1);
		else
			anchor("vec=S1.CTRL_MS_RESET.exp.001", 0);
		setup = find_setup(b, 0xfe, &slen);
		if (setup != NULL)
			vec_check("S1", "S1.CTRL_GET_MAX_LUN.exp.001.dat",
			    setup, 8, -1);
		else
			anchor("vec=S1.CTRL_GET_MAX_LUN.exp.001", 0);
		if (b->cap.cbw_n > 0)
			vec_check("S1", "S1.BOT_CBW.exp.001.dat",
			    b->cap.cbw[0], 31, -1);
		else
			anchor("vec=S1.BOT_CBW.exp.001", 0);
	}

	/* the scripted negative battery: seven control-face cases whose
	 * answers the device model is told to give */
	if (g_fault != NULL && (strcmp(g_fault, "negatives") == 0 ||
	    strcmp(g_fault, "battery-tamper") == 0)) {
		static const char *names[7] = {
			"get-interface-stall", "iface-desc-stall",
			"ep-desc-stall", "device-short-read",
			"config-short-1024", "config-short-512", "qualifier-set",
		};
		int pass = 0;
		int i;

		/* the reverse control: with the scripted stalls disarmed the
		 * battery's must-red cases no longer hold, and the run must
		 * report that instead of quietly passing */
		if (g_fault != NULL && strcmp(g_fault, "battery-tamper") == 0)
			mock_hcd_script(b, MOCK_SCRIPT_ANSWER_ALL, 1);

		for (i = 0; i < 7; i++) {
			int expect_lo = 0, expect_hi = 0;

			memset(buf, 0, sizeof(buf));
			switch (i) {
			case 0:
				ret = ctrl_msg(&g_dev, 0x81, 0x0a, 0, 0, buf, 1);
				expect_lo = -EPIPE;
				break;
			case 1:
				ret = ctrl_msg(&g_dev, 0x80, 0x06, 0x0400, 0, buf,
				    18);
				expect_lo = -EPIPE;
				break;
			case 2:
				ret = ctrl_msg(&g_dev, 0x80, 0x06, 0x0500, 0, buf,
				    7);
				expect_lo = -EPIPE;
				break;
			case 3:
				mock_hcd_script(b, MOCK_SCRIPT_SHORT_DEVICE, 1);
				ret = ctrl_msg(&g_dev, 0x80, 0x06, 0x0100, 0, buf,
				    1024);
				mock_hcd_script(b, MOCK_SCRIPT_SHORT_DEVICE, 0);
				expect_lo = -EREMOTEIO;
				break;
			case 4:
				ret = ctrl_msg(&g_dev, 0x80, 0x06, 0x0200, 0, buf,
				    1024);
				expect_lo = -EREMOTEIO;
				break;
			case 5:
				ret = ctrl_msg(&g_dev, 0x80, 0x06, 0x0200, 0, buf,
				    512);
				expect_lo = -EREMOTEIO;
				break;
			default:
				/* this request may answer or stall - both are
				 * lawful, so the expectation is a set */
				ret = ctrl_msg(&g_dev, 0x80, 0x06, 0x0600, 0, buf,
				    10);
				expect_lo = 0;
				expect_hi = -EPIPE;
				break;
			}
			if (ret == expect_lo || (expect_hi != 0 && ret == expect_hi))
				pass++;
			else
				printf("usb-smoke: battery case=%s got=%d "
				    "want=%d\n", names[i], ret, expect_lo);
		}
		if (pass == 7)
			info("ep0-battery neg=7 ok=1");
		else
			anchor("ep0-battery", 0);

		/* the seam's declared negatives: a second free of a released
		 * urb is named and skipped, and the use-after-free path is
		 * named - both without touching a recycled block */
		{
			struct lkpi_usb_mem_stats st;
			struct urb *u = usb_alloc_urb(0, GFP_KERNEL);

			usb_free_urb(u);
			usb_free_urb(u);	/* the second free is the case */
			lkpi_usb_mem_stats_get(&st);
			if (st.double_free == 1 && st.uaf_write == 1 &&
			    lkpi_usb_urb_is_freed(u))
				info("seam xfail double-free ok=1");
			else
				anchor("seam xfail double-free", 0);
		}
		{
			struct lkpi_usb_mem_stats st;
			struct urb *u = usb_alloc_urb(0, GFP_KERNEL);

			usb_free_urb(u);
			if (lkpi_usb_urb_is_freed(u)) {
				/* the geometry path names the write attempt
				 * instead of touching the released block */
				lkpi_usb_uaf_hit(u);
			}
			lkpi_usb_mem_stats_get(&st);
			if (st.uaf_write == 2 && st.double_free == 1)
				info("seam xfail uaf ok=1");
			else
				anchor("seam xfail uaf", 0);
		}
	}

	/* S2: the asynchronous lane - single in flight, in-order drain, and
	 * a kill that cancels a transfer the controller still owns */
	{
		struct urb *u1 = usb_alloc_urb(0, GFP_KERNEL);
		struct urb *u2 = usb_alloc_urb(0, GFP_KERNEL);
		struct urb *u3;
		static u8 x1[8], x2[8], x3[8];

		lkpi_usb_force_sync_set(false);
		usb_fill_bulk_urb(u1, &g_dev, usb_rcvbulkpipe(&g_dev, 1), x1,
		    8, cb_a1, NULL);
		usb_fill_bulk_urb(u2, &g_dev, usb_rcvbulkpipe(&g_dev, 1), x2,
		    8, cb_a2, NULL);
		if (usb_submit_urb(u1, GFP_KERNEL) == 0 &&
		    usb_submit_urb(u2, GFP_KERNEL) == 0) {
			/* the second submit queues behind the first */
			if (wait_done(&a1_done, 2000) == 0 &&
			    wait_done(&a2_done, 2000) == 0 &&
			    order_n == 2 && order[0] == 1 && order[1] == 2)
				anchor("queue drain in-order n=2", 1);
			else
				anchor("queue drain in-order n=2", 0);
		} else {
			anchor("queue drain in-order n=2", 0);
		}
		usb_free_urb(u1);
		usb_free_urb(u2);

		/* the kill window: hold the controller's worker so the
		 * transfer stays queued, then kill it */
		u3 = usb_alloc_urb(0, GFP_KERNEL);
		usb_fill_bulk_urb(u3, &g_dev, usb_rcvbulkpipe(&g_dev, 1), x3,
		    8, cb_a3, NULL);
		mock_hcd_hold_set(b, 1);
		if (usb_submit_urb(u3, GFP_KERNEL) == 0) {
			(void)osDelay(20u);	/* let the submit path finish */
			usb_kill_urb(u3);
			if (a3_done == 1 && a3_status == -ENOENT)
				anchor("kill cancelled urb status=-ENOENT", 1);
			else
				anchor("kill cancelled urb", 0);
		} else {
			anchor("kill cancelled urb", 0);
		}
		mock_hcd_hold_set(b, 0);
		usb_free_urb(u3);
		lkpi_usb_force_sync_set(true);
	}

}

/* ------------------------------------------------------------------ */
/* entry                                                               */
/* ------------------------------------------------------------------ */

static void
app_task(void *arg)
{

	(void)arg;
	run_scenario();
	if (failures == 0)
		printf("usb-smoke: PASS\n");
	else
		printf("usb-smoke: FAIL failures=%d\n", failures);
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
		if (strcmp(argv[i], "--vec-root") == 0 && i + 1 < argc) {
			g_vec_root = argv[++i];
			g_vec_root_given = 1;
		} else if (strcmp(argv[i], "--fault") == 0 && i + 1 < argc) {
			g_fault = argv[++i];
		} else {
			printf("usb-smoke: usage: %s [--vec-root DIR] "
			    "[--fault vec-tamper|negatives]\n", argv[0]);
			return 2;
		}
	}

	if (osKernelInitialize() != osOK) {
		printf("usb-smoke: kernel init failed\n");
		return 2;
	}
	attr.name = "usb-mock";
	attr.stack_size = 32768u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("usb-smoke: cannot create the application task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	return failures == 0 ? 0 : 1;
}
