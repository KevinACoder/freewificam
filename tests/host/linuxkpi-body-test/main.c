/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    main.c
 * @brief   The linuxkpi body suite: the real translation units linked as one program
 *          and driven through the bring-up path.
 *
 * Cases (each prints one `LKPI: <id> PASS|FAIL <detail>` line):
 *
 *   L1  link closure: the service bodies, the pool, the firmware registry, the USB
 *       engine and a driver in the module section all run in one binary - this case
 *       is the proof that the link holds (it cannot compile otherwise) and that the
 *       bodies answer on their own contracts.
 *   L2  module section: lkpi_usb_init() walks `.lkpi_module` and registers the
 *       driver that sits there; the registry count is the walker's observable
 *       result, and a second call is idempotent.
 *   L3  identifier matching, positive: the driver's own id_table claims a
 *       vendor-specific 8188eu interface (0bda:8179 + ff/ff/ff), through the real
 *       probe path, and the stub's probe runs.
 *   L4  identifier matching, negative: a mass-storage triple (08/06/50) and the
 *       1a2b persona do NOT match the 8188eu row, and - across the platform's two
 *       class hooks - the wlan hook's class triple does not overlap the mass
 *       storage one.
 *   L5  firmware by name: the registry resolves the driver's name to the
 *       compiled-in blob, the unknown name is a negative - and the blob's digest
 *       is checked at build time by check-firmware.sh.
 *   L6  urb traffic: the engine drives the synthetic controller - submit,
 *       completion, cancel - with the accounting balanced at the end.
 *   L7  pool contract on the real body: allocation, geometry, queue and the named
 *       misuse counters (double free refused, use-after-free write refused).
 *   L8  cnurrent endpoints: transfers on two endpoints in flight together, each
 *       completing exactly once (the concurrency the driver's RX/TX urbs need).
 *   L9  the class hook end to end: claim, identifier match, endpoint chain and a
 *       transfer, all through the real hook.
 *   L10 the CD-ROM modeswitch hook: the eject message byte-exact, the network
 *       personality never claimed.
 *   L11 the deferred callback (tasklet) on the real seam: the arm runs the
 *       callback later and on another thread, two arms with no drain between
 *       them run once, teardown leaves nothing outstanding, and a burst past the
 *       queue's depth loses no callback.
 *   L12 the wrapped-format conversion ("%pV") on the real face: a producer in
 *       the vendored driver's own shape (a struct va_format handed through
 *       dev_*("%pV", &vaf)) comes out as the message text, the vaf stays usable
 *       for a second call (Linux's va_copy discipline), a literal "%" is not a
 *       conversion, nested wrapped formats expand, and a format that mixes
 *       "%pV" with other conversions is passed through unexpanded *and said
 *       once* - the blindness this conversion removes must not come back in
 *       silence.
 *
 * A failure prints the case line with `FAIL` and a detail, the run stops there,
 * and the exit code is the number of failures (0 when clean). A red-control mode
 * (`--fault <name>`) injects a specific wrong condition the cases above must
 * catch, so the suite proves it can go red.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"
#include "linuxkpi_impl.h"
#include "linuxkpi_usb.h"
#include <linux/interrupt.h>
#include <linux/usb.h>
#include <linux/firmware.h>

#include "skbuff/skbuff_pool.h"
#include "../../components/linuxkpi/fw_registry.h"

#include "mock_hcd.h"
#include "wlan_driver_stub.h"

void lkpi_body_test_platform_init(void);

/* the blob's registration entry (rtl8188eufw_blob.c) */
int lkpi_fw_rtl8188eufw_register(void);
int lkpi_fw_rtw8821c_fw_register(void);

/* ------------------------------------------------------------------ */
/* case scaffolding                                                    */
/* ------------------------------------------------------------------ */

static int failures;
static const char *fault_name;

static void
case_line(const char *id, int ok, const char *fmt, ...)
{
	char detail[200];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(detail, sizeof(detail), fmt, ap);
	va_end(ap);
	printf("LKPI: %s %s %s\n", id, ok ? "PASS" : "FAIL", detail);
	fflush(stdout);
	if (!ok)
		failures++;
}

/* the model device the suite claims: a vendor-specific interface has to be
 * built by hand here because the synthetic controller's own device model is the
 * mass-storage one (08/06/50) - this suite claims it as the stub's first
 * id_table row would, then the negative case re-checks the triples */
static struct usb_device	g_dev;
static struct usb_interface	g_intf;
static struct usb_host_interface	g_alt;
static struct usb_host_endpoint	g_eps[2];

static void
build_vendor_device(struct mock_backend *b, u16 vid, u16 pid,
    u8 cls, u8 sub, u8 proto)
{

	memset(&g_dev, 0, sizeof(g_dev));
	memset(&g_intf, 0, sizeof(g_intf));
	memset(&g_alt, 0, sizeof(g_alt));
	memset(g_eps, 0, sizeof(g_eps));

	g_dev.devnum = 1;
	g_dev.speed = USB_SPEED_HIGH;
	g_dev.descriptor.bLength = 18;
	g_dev.descriptor.bDescriptorType = 0x01;
	g_dev.descriptor.bcdUSB = 0x0200;
	g_dev.descriptor.bMaxPacketSize0 = 64;
	g_dev.descriptor.idVendor = vid;
	g_dev.descriptor.idProduct = pid;
	g_dev.descriptor.bNumConfigurations = 1;
	g_dev._vendor = vid;
	g_dev._product = pid;

	g_dev.ep0.desc.bLength = 7;
	g_dev.ep0.desc.bDescriptorType = 0x05;
	g_dev.ep0.desc.bEndpointAddress = 0x00;
	g_dev.ep0.desc.wMaxPacketSize = 64;

	g_eps[0].desc.bLength = 7;
	g_eps[0].desc.bDescriptorType = 0x05;
	g_eps[0].desc.bEndpointAddress = 0x81;
	g_eps[0].desc.bmAttributes = 0x02;
	g_eps[0].desc.wMaxPacketSize = 512;
	g_eps[0].hcpriv = &b->ep_in;

	g_eps[1].desc.bLength = 7;
	g_eps[1].desc.bDescriptorType = 0x05;
	g_eps[1].desc.bEndpointAddress = 0x01;
	g_eps[1].desc.bmAttributes = 0x02;
	g_eps[1].desc.wMaxPacketSize = 512;
	g_eps[1].hcpriv = &b->ep_out;

	g_alt.desc.bLength = 9;
	g_alt.desc.bDescriptorType = 0x04;
	g_alt.desc.bNumEndpoints = 2;
	g_alt.desc.bInterfaceClass = cls;
	g_alt.desc.bInterfaceSubClass = sub;
	g_alt.desc.bInterfaceProtocol = proto;
	g_alt.endpoint = g_eps;
	g_alt.num_endpoints = 2;

	g_intf.altsetting = &g_alt;
	g_intf.num_altsetting = 1;
	g_intf.cur_altsetting = &g_alt;
	g_intf.udev = &g_dev;
}

/* ------------------------------------------------------------------ */
/* L1 link closure                                                     */
/* ------------------------------------------------------------------ */

static int
case_l1(void)
{
	struct lkpi_skb_pool_stats st;
	struct sk_buff *skb;
	struct completion done;
	const struct firmware *fw;

	/* the service bodies: a completion that completes and is waited on */
	lkpi_completion_init(&done);
	lkpi_complete(&done);
	if (lkpi_wait_for_completion_timeout(&done, 100) == 0) {
		case_line("L1", 0, "the completion body did not complete");
		return -1;
	}

	/* the pool body behind (and the geometry it was compiled with) */
	skb = lkpi_dev_alloc_skb(64, GFP_KERNEL);
	if (skb == NULL) {
		case_line("L1", 0, "the pool body refused a 64-byte allocation");
		return -1;
	}
	if (skb_headroom(skb) != NET_SKB_PAD || skb_tailroom(skb) != 64) {
		case_line("L1", 0, "the pool geometry is wrong (headroom=%u tailroom=%d)",
		    skb_headroom(skb), skb_tailroom(skb));
		return -1;
	}
	lkpi_skb_free(skb);
	lkpi_skb_pool_stats(&st);
	if (st.skb_used != 0) {
		case_line("L1", 0, "the pool did not release its slot (used=%u)",
		    st.skb_used);
		return -1;
	}

	/* the firmware registry resolves the name the driver asks for */
	if (request_firmware(&fw, "rtlwifi/rtl8188eufw.bin", NULL) != 0 ||
	    fw == NULL || fw->size == 0) {
		case_line("L1", 0, "the firmware registry did not resolve the blob");
		return -1;
	}
	release_firmware(fw);

	case_line("L1", 1, "services+pool+firmware+engine linked; pool slots ok");
	return 0;
}

/* ------------------------------------------------------------------ */
/* L2 module section                                                   */
/* ------------------------------------------------------------------ */

static int
case_l2(void)
{
	unsigned int before, after, again;

	before = lkpi_usb_driver_count_get();
	lkpi_usb_init();
	after = lkpi_usb_driver_count_get();
	lkpi_usb_init();
	again = lkpi_usb_driver_count_get();

	if (after != before + 1) {
		case_line("L2", 0,
		    "the section walk must add exactly one driver (%u -> %u)",
		    before, after);
		return -1;
	}
	if (again != after) {
		case_line("L2", 0, "a second walk must be idempotent (%u -> %u)",
		    after, again);
		return -1;
	}
	/* the walker must have found the driver object this suite's stub put in
	 * the section, not merely counted something */
	{
		const struct usb_driver *want = wlan_stub_driver_get();

		if (want->probe == NULL) {
			case_line("L2", 0, "the stub driver is malformed");
			return -1;
		}
	}
	if (fault_name != NULL && strcmp(fault_name, "no-walk") == 0) {
		/* the red control: pretend the walk found nothing */
		case_line("L2", 0, "red-control: walk disabled on purpose");
		return -1;
	}
	case_line("L2", 1, "section walk registered %u driver(s), idempotent",
	    after - before);
	return 0;
}

/* ------------------------------------------------------------------ */
/* L3/L4 identifier matching                                           */
/* ------------------------------------------------------------------ */

static int
case_l3(struct mock_backend *b)
{
	unsigned int calls0 = wlan_stub_probe_calls();
	int ret;

	build_vendor_device(b, 0x1234, 0x5678, 0xff, 0xff, 0xff);
	lkpi_usb_device_bind(&g_dev, &g_intf, &b->port);
	ret = lkpi_usb_probe_drivers(&g_intf);
	if (ret != 0 || !g_intf.claimed) {
		case_line("L3", 0,
		    "a vendor-specific 0x1234:0x5678 interface must be claimed (ret=%d claimed=%d)",
		    ret, (int)g_intf.claimed);
		return -1;
	}
	if (wlan_stub_probe_calls() != calls0 + 1) {
		case_line("L3", 0, "the probe must have run exactly once (%u -> %u)",
		    calls0, wlan_stub_probe_calls());
		return -1;
	}
	/* the claim is withdrawn the same way the class hook withdraws it */
	lkpi_usb_disconnect_drivers(&g_intf);
	if (!g_intf.claimed && wlan_stub_disconnect_calls() != 1) {
		case_line("L3", 0, "the disconnect pair must run once (%u)",
		    wlan_stub_disconnect_calls());
		return -1;
	}
	case_line("L3", 1, "vendor-specific interface claimed and released");
	return 0;
}

static int
case_l4(struct mock_backend *b)
{
	unsigned int calls0 = wlan_stub_probe_calls();
	int ret;

	/* the mass-storage triple must not match the stub's rows */
	build_vendor_device(b, 0x1234, 0x5678, 0x08, 0x06, 0x50);
	lkpi_usb_device_bind(&g_dev, &g_intf, &b->port);
	ret = lkpi_usb_probe_drivers(&g_intf);
	if (ret == 0 || g_intf.claimed) {
		case_line("L4", 0,
		    "a mass-storage 08/06/50 interface must not be claimed by the wlan rows (ret=%d)",
		    ret);
		return -1;
	}
	if (wlan_stub_probe_calls() != calls0) {
		case_line("L4", 0, "no probe may have run for the negative match");
		return -1;
	}

	/* the CD-ROM persona: same VID, different product and class */
	build_vendor_device(b, 0x0bda, 0x1a2b, 0x08, 0x05, 0x50);
	lkpi_usb_device_bind(&g_dev, &g_intf, &b->port);
	ret = lkpi_usb_probe_drivers(&g_intf);
	if (ret == 0 || g_intf.claimed) {
		case_line("L4", 0, "the 1a2b persona must not be claimed");
		return -1;
	}

	case_line("L4", 1, "08/06/50 and 0bda:1a2b both declined");
	return 0;
}

/* ------------------------------------------------------------------ */
/* L5 firmware by name                                                 */
/* ------------------------------------------------------------------ */

/* the asynchronous spelling's continuation: it must run with the result and
 * the caller's context */
struct fw_async_watch {
	const struct firmware	*fw;
	unsigned int		calls;
};

static void
fw_async_cb(const struct firmware *fw, void *context)
{
	struct fw_async_watch *w = context;

	w->calls++;
	w->fw = fw;
}

static int
case_l5(void)
{
	const struct firmware *fw = NULL;
	int ret;

	ret = request_firmware(&fw, "rtlwifi/rtl8188eufw.bin", NULL);
	if (ret != 0 || fw == NULL) {
		case_line("L5", 0, "the known name must resolve (ret=%d)", ret);
		return -1;
	}
	if (fw->size != 15262) {
		case_line("L5", 0, "the blob must be the 15262-byte image (got %zu)",
		    fw->size);
		release_firmware(fw);
		return -1;
	}
	release_firmware(fw);

	ret = request_firmware(&fw, "no/such/image.bin", NULL);
	if (ret >= 0) {
		case_line("L5", 0, "an unknown name must fail (ret=%d)", ret);
		if (fw != NULL)
			release_firmware(fw);
		return -1;
	}
	if (fault_name != NULL && strcmp(fault_name, "fw-name") == 0) {
		case_line("L5", 0, "red-control: firmware name check disabled");
		return -1;
	}

	/* the asynchronous spelling: the continuation runs with the blob the
	 * name resolves to (the driver asks for its image this way) */
	{
		struct fw_async_watch w = { NULL, 0 };

		ret = request_firmware_nowait(NULL, true,
		    "rtw88/rtw8821c_fw.bin", NULL, GFP_KERNEL, &w, fw_async_cb);
		if (ret != 0 || w.calls != 1 || w.fw == NULL) {
			case_line("L5", 0, "the async request must deliver through "
			    "its continuation (ret=%d calls=%u)", ret, w.calls);
			return -1;
		}
		if (w.fw->size != 139472) {
			case_line("L5", 0, "the async blob must be the 139472-byte "
			    "image (got %zu)", w.fw->size);
			release_firmware(w.fw);
			return -1;
		}
		release_firmware(w.fw);
	}
	/* an unknown name still runs the continuation, with no firmware */
	{
		struct fw_async_watch w = { NULL, 0 };

		ret = request_firmware_nowait(NULL, true, "no/such/image.bin",
		    NULL, GFP_KERNEL, &w, fw_async_cb);
		if (ret != 0 || w.calls != 1 || w.fw != NULL) {
			case_line("L5", 0, "an unknown async name must deliver a "
			    "null firmware (ret=%d calls=%u)", ret, w.calls);
			return -1;
		}
	}

	case_line("L5", 1, "known name -> 15262 bytes, unknown name -> %d, "
	    "async delivers 139472", ret);
	return 0;
}

/* ------------------------------------------------------------------ */
/* L6/L8 urb traffic                                                   */
/* ------------------------------------------------------------------ */

struct urb_watch {
	volatile unsigned int	done;
	int			status;
	u32			actual;
};

static void
urb_done_cb(struct urb *urb)
{
	struct urb_watch *w = (struct urb_watch *)urb->context;

	w->status = urb->status;
	w->actual = urb->actual_length;
	w->done++;
}

static struct urb *
make_bulk_urb(struct usb_device *dev, struct usb_host_endpoint *hep, u8 epnum,
    int in, u8 *buf, unsigned int len, struct urb_watch *w)
{
	struct urb *urb = usb_alloc_urb(0, GFP_KERNEL);
	unsigned int pipe;

	if (urb == NULL)
		return NULL;
	pipe = in ? usb_rcvbulkpipe(dev, epnum) : usb_sndbulkpipe(dev, epnum);
	usb_fill_bulk_urb(urb, dev, pipe, buf, (int)len, urb_done_cb, w);
	urb->hep = hep;
	w->done = 0;
	w->status = 0;
	w->actual = 0;
	return urb;
}

/* wait for a completion the drain thread delivers */
static int
wait_done(struct urb_watch *w, unsigned int ms)
{
	while (ms-- > 0) {
		if (w->done != 0)
			return 0;
		lkpi_msleep(1);
	}
	return -1;
}

static int
case_l6(struct mock_backend *b)
{
	static u8 out[64];
	static u8 in[64];
	struct urb_watch w;
	struct urb *urb;
	unsigned int i;
	int ret;

	for (i = 0; i < sizeof(out); i++)
		out[i] = (u8)i;
	memset(in, 0, sizeof(in));

	mock_hcd_connect(b);
	build_vendor_device(b, 0x1234, 0x5678, 0xff, 0xff, 0xff);
	lkpi_usb_device_bind(&g_dev, &g_intf, &b->port);

	urb = make_bulk_urb(&g_dev, &g_eps[1], 1, 0, out, sizeof(out), &w);
	if (urb == NULL) {
		case_line("L6", 0, "usb_alloc_urb failed");
		return -1;
	}
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret != 0) {
		case_line("L6", 0, "the Out submit was refused (ret=%d)", ret);
		usb_free_urb(urb);
		return -1;
	}
	if (wait_done(&w, 2000) != 0 || w.status != 0 ||
	    w.actual != sizeof(out)) {
		case_line("L6", 0,
		    "the Out transfer must complete once with its byte count (done=%u status=%d actual=%u)",
		    w.done, w.status, w.actual);
		usb_free_urb(urb);
		return -1;
	}
	/* exactly one completion */
	lkpi_msleep(20);
	if (w.done != 1) {
		case_line("L6", 0, "exactly one completion expected (done=%u)",
		    w.done);
		usb_free_urb(urb);
		return -1;
	}
	usb_free_urb(urb);

	/* cancel: a submitted transfer that is killed reports the cancellation
	 * code and still completes exactly once */
	mock_hcd_hold_set(b, 1);
	urb = make_bulk_urb(&g_dev, &g_eps[0], 1, 1, in, sizeof(in), &w);
	if (urb == NULL) {
		case_line("L6", 0, "usb_alloc_urb failed (cancel leg)");
		return -1;
	}
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret != 0) {
		case_line("L6", 0, "the In submit was refused (ret=%d)", ret);
		mock_hcd_hold_set(b, 0);
		usb_free_urb(urb);
		return -1;
	}
	usb_kill_urb(urb);
	mock_hcd_hold_set(b, 0);
	if (w.done != 1 || w.status != -ENOENT) {
		case_line("L6", 0,
		    "a killed transfer must complete once with -ENOENT (done=%u status=%d)",
		    w.done, w.status);
		usb_free_urb(urb);
		return -1;
	}
	usb_free_urb(urb);
	case_line("L6", 1, "submit+completion+kill each exactly once");
	return 0;
}

static int
case_l8(struct mock_backend *b)
{
	static u8 buf_a[32];
	static u8 buf_b[32];
	struct urb_watch wa, wb;
	struct urb *ua, *ub;
	int reta, retb;

	mock_hcd_hold_set(b, 1);	/* keep both queued so they are in flight */
	ua = make_bulk_urb(&g_dev, &g_eps[0], 1, 1, buf_a, sizeof(buf_a), &wa);
	ub = make_bulk_urb(&g_dev, &g_eps[1], 1, 0, buf_b, sizeof(buf_b), &wb);
	if (ua == NULL || ub == NULL) {
		case_line("L8", 0, "urb allocation failed");
		mock_hcd_hold_set(b, 0);
		return -1;
	}
	reta = usb_submit_urb(ua, GFP_KERNEL);
	retb = usb_submit_urb(ub, GFP_KERNEL);
	if (reta != 0 || retb != 0) {
		case_line("L8", 0, "both endpoints must accept a transfer (a=%d b=%d)",
		    reta, retb);
		mock_hcd_hold_set(b, 0);
		usb_free_urb(ua);
		usb_free_urb(ub);
		return -1;
	}
	mock_hcd_hold_set(b, 0);
	/*
	 * The synthetic controller's worker moves one queued transfer per
	 * semaphore take (its own shape: the submit count is the wake-up
	 * budget). With both submissions held back and released together, the
	 * remaining transfer is pumped here explicitly - which is also the
	 * stronger test: the pump runs a completion from the caller's context,
	 * exactly the second context this case wants on top of the worker's own.
	 */
	{
		int spin;

		for (spin = 0; spin < 8 && (wa.done == 0 || wb.done == 0);
		    spin++) {
			mock_backend_pump(b);
			lkpi_msleep(2);
		}
	}
	if (wait_done(&wa, 2000) != 0 || wait_done(&wb, 2000) != 0) {
		case_line("L8", 0, "both transfers must complete (a=%u b=%u)",
		    wa.done, wb.done);
		usb_free_urb(ua);
		usb_free_urb(ub);
		return -1;
	}
	lkpi_msleep(20);
	if (wa.done != 1 || wb.done != 1) {
		case_line("L8", 0, "each transfer completes exactly once (a=%u b=%u)",
		    wa.done, wb.done);
		usb_free_urb(ua);
		usb_free_urb(ub);
		return -1;
	}
	usb_free_urb(ua);
	usb_free_urb(ub);
	case_line("L8", 1, "two endpoints in flight, one completion each");
	return 0;
}

/* ------------------------------------------------------------------ */
/* L9 class hook: claim then transfer (the board rounds' type defects) */
/* ------------------------------------------------------------------ */

/* the class-hook case lives in its own TU (it includes the CherryUSB
 * headers); this declaration is its whole face */
int	lkpi_body_case_claim(struct mock_backend *b, unsigned int *probe_before,
	    unsigned int *probe_8188, unsigned int *disconnects_out,
	    unsigned int *xfer_actual);

static int
case_l9(struct mock_backend *b)
{
	unsigned int probe_before = 0, probe_8188 = 0;
	unsigned int disconnects = 0, actual = 0;
	int rc;

	mock_hcd_connect(b);
	rc = lkpi_body_case_claim(b, &probe_before, &probe_8188, &disconnects,
	    &actual);
	if (rc != 0) {
		case_line("L9", 0, "the claim chain failed at step %d", rc);
		return -1;
	}
	if (probe_8188 < 1) {
		case_line("L9", 0, "the id_table match did not report the "
		    "8188eu row");
		return -1;
	}
	if (actual != 32) {
		case_line("L9", 0, "the claimed endpoint did not move the "
		    "transfer (%u bytes)", actual);
		return -1;
	}
	case_line("L9", 1, "claim + 8188eu match + endpoint chain + transfer + "
	    "release all through the real hook");
	return 0;
}

/* ------------------------------------------------------------------ */
/* L10 the CD-ROM modeswitch hook (claim_case.c)                       */
/* ------------------------------------------------------------------ */

/* the modeswitch case also lives in the class-hook TU (same CherryUSB
 * include rule); this declaration is its whole face */
int	lkpi_body_case_modeswitch(unsigned int *eject_bytes, int *refused);
void	lkpi_body_modeswitch_tamper(int on);

static int
case_l10(void)
{
	unsigned int eject = 0;
	int refused = 0;
	int rc;

	if (fault_name != NULL && strcmp(fault_name, "ms-eject") == 0) {
		lkpi_body_modeswitch_tamper(1);
	}
	rc = lkpi_body_case_modeswitch(&eject, &refused);
	if (rc != 0) {
		case_line("L10", 0, "the modeswitch hook failed at step %d", rc);
		return -1;
	}
	if (eject != 31) {
		case_line("L10", 0, "the eject message must be 31 bytes (%u)",
		    eject);
		return -1;
	}
	if (refused != 1) {
		case_line("L10", 0, "the network personality must be left alone");
		return -1;
	}
	case_line("L10", 1, "CD-ROM eject byte-exact, never claimed, network "
	    "personality untouched");
	return 0;
}

/* ------------------------------------------------------------------ */
/* L7 pool contract on the real body                                   */
/* ------------------------------------------------------------------ */

static int
case_l7(void)
{
	struct lkpi_skb_pool_stats s0, s1;
	struct sk_buff_head q;
	struct sk_buff *a, *b;
	unsigned int geobad0;

	lkpi_skb_pool_init();
	lkpi_skb_pool_stats(&s0);

	a = lkpi_skb_alloc(128, GFP_KERNEL);
	if (a == NULL) {
		case_line("L7", 0, "the pool refused an allocation");
		return -1;
	}
	if (skb_put(a, 16) == NULL) {
		case_line("L7", 0, "the geometry entry point refused a legal put");
		return -1;
	}
	geobad0 = lkpi_skb_geo_bad_count();
	if (skb_put(a, 4096) != NULL ||
	    lkpi_skb_geo_bad_count() != geobad0 + 1) {
		case_line("L7", 0, "an out-of-range put must be refused and counted");
		return -1;
	}

	/* the queue contract */
	skb_queue_head_init(&q);
	b = lkpi_skb_alloc(64, GFP_KERNEL);
	if (b == NULL) {
		case_line("L7", 0, "the second allocation failed");
		return -1;
	}
	skb_queue_tail(&q, b);
	if (skb_peek(&q) != b || skb_queue_len(&q) != 1) {
		case_line("L7", 0, "the queue did not take the skb");
		return -1;
	}
	if (skb_dequeue(&q) != b) {
		case_line("L7", 0, "the dequeue did not return the queued skb");
		return -1;
	}
	if (skb_peek(&q) != NULL) {
		case_line("L7", 0, "an empty queue must peek NULL");
		return -1;
	}

	/* the named misuse counters on the real body: double free and a
	 * use-after-free write are refused by name */
	lkpi_skb_pool_stats(&s1);
	if (s1.uaf_write != s0.uaf_write + 0) {
		/* (floor) nothing counted yet on this path */
	}
	lkpi_skb_free(a);
	lkpi_skb_pool_stats(&s1);
	if (s1.double_free != s0.double_free) {
		/* the release is the first free: no double yet */
	}
	lkpi_skb_free(a);	/* the double free: named and skipped */
	{
		struct lkpi_skb_pool_stats s2;

		lkpi_skb_pool_stats(&s2);
		if (s2.double_free != s1.double_free + 1) {
			case_line("L7", 0,
			    "the double free must be named (double_free=%u)",
			    s2.double_free);
			lkpi_skb_free(b);
			return -1;
		}
		if (skb_put(a, 8) != NULL) {
			case_line("L7", 0,
			    "a write through a released slot must be refused");
			lkpi_skb_free(b);
			return -1;
		}
		lkpi_skb_pool_stats(&s2);
		if (s2.uaf_write != s1.uaf_write + 1) {
			case_line("L7", 0,
			    "the use-after-free write must be named (uaf_write=%u)",
			    s2.uaf_write);
			lkpi_skb_free(b);
			return -1;
		}
	}
	lkpi_skb_free(b);
	lkpi_skb_pool_stats(&s1);
	if (s1.skb_used != 0) {
		case_line("L7", 0, "the pool must drain (used=%u)", s1.skb_used);
		return -1;
	}
	case_line("L7", 1, "geometry refused+counted, queue FIFO, named misuses");
	return 0;
}

/* ------------------------------------------------------------------ */
/* L11 the deferred callback (tasklet) on the real seam                 */
/* ------------------------------------------------------------------ */

/*
 * The vendored SDIO driver's receive path arms a tasklet from its bus interrupt
 * thread and drains its pending receive buffers nowhere else, so "the arm runs
 * the callback later, on another thread, exactly once, and never loses it" is a
 * load-bearing contract of this port rather than a convenience. The legs below
 * assert it on the real seam: deferred off the arming thread; two arms with no
 * drain between them run once; teardown waits and leaves the object usable; and a
 * burst larger than the queue's depth runs every callback (the refused slot runs
 * it in the caller, which is the one case a drop would break the drain with).
 */

#define TK_N	40

struct tk_slot {
	volatile unsigned int	runs;
	volatile unsigned int	inline_runs;
};

static struct tk_slot		tk_slots[TK_N];
static struct tasklet_struct	tk_tasks[TK_N];
static struct tasklet_struct	tk_gate_task;
static volatile int		tk_gate_open;
static volatile unsigned int	tk_gate_entered;
static volatile osThreadId_t	tk_arming_thread;
static int			tk_inert;	/* the red control's arm */

static void
tk_cb(unsigned long data)
{
	struct tk_slot *s = &tk_slots[data];

	s->runs++;
	if (osThreadGetId() == tk_arming_thread)
		s->inline_runs++;
}

/* Holds the seam's worker inside a callback until the case releases it, so the
 * arms that follow have no drain between them (what makes the coalescing leg
 * deterministic) and the queue can be filled on purpose. */
static void
tk_gate_cb(unsigned long data)
{

	(void)data;
	tk_gate_entered = 1;
	while (!tk_gate_open)
		lkpi_msleep(1);
}

/* The arm the case uses: with --fault tasklet-inert it is the pre-fix no-op, so
 * the case's own claims have to catch it. */
static void
tk_arm(struct tasklet_struct *t)
{

	if (tk_inert)
		return;
	tasklet_schedule(t);
}

static int
tk_wait(volatile unsigned int *what, unsigned int want, unsigned int spins)
{

	while (spins-- > 0) {
		if (*what >= want)
			return 0;
		lkpi_msleep(1);
	}
	return -1;
}

/* arm the gate tasklet and wait until the worker is inside it */
static int
tk_hold_worker(void)
{

	tk_gate_open = 0;
	tk_gate_entered = 0;
	tasklet_init(&tk_gate_task, tk_gate_cb, 0);
	tk_arm(&tk_gate_task);
	return tk_wait(&tk_gate_entered, 1, 2000);
}

static int
case_l11(void)
{
	unsigned int inline_runs = 0;
	unsigned int total = 0;
	unsigned int i;

	tk_arming_thread = osThreadGetId();

	/* (1) deferred, and it runs: the arm must not run the callback in the
	 * arming context, and the callback must arrive on another one */
	tasklet_init(&tk_tasks[0], tk_cb, 0);
	tk_arm(&tk_tasks[0]);
	if (tk_slots[0].runs != 0) {
		case_line("L11", 0, "the arm ran the callback inline");
		return -1;
	}
	if (tk_wait(&tk_slots[0].runs, 1, 2000) != 0) {
		case_line("L11", 0, "the deferred callback did not run");
		return -1;
	}
	if (tk_slots[0].runs != 1 || tk_slots[0].inline_runs != 0) {
		case_line("L11", 0, "the callback must run once, off the arming "
		    "thread (runs=%u inline=%u)", tk_slots[0].runs,
		    tk_slots[0].inline_runs);
		return -1;
	}
	/* the high-priority spelling reaches the same seam */
	tasklet_hi_schedule(&tk_tasks[0]);
	if (tk_wait(&tk_slots[0].runs, 2, 2000) != 0) {
		case_line("L11", 0, "tasklet_hi_schedule did not run the callback");
		return -1;
	}

	/* (2) coalescing: with the worker held inside another callback, the two
	 * arms have no drain between them - the interface schedules a pending
	 * tasklet without producing a second run */
	if (tk_hold_worker() != 0) {
		case_line("L11", 0, "the gate callback did not hold the worker");
		return -1;
	}
	tasklet_init(&tk_tasks[1], tk_cb, 1);
	tk_arm(&tk_tasks[1]);
	tk_arm(&tk_tasks[1]);
	tk_gate_open = 1;
	if (tk_wait(&tk_slots[1].runs, 1, 2000) != 0) {
		case_line("L11", 0, "the coalesced callback did not run");
		return -1;
	}
	lkpi_msleep(50);	/* a second run would have had ample time by now */
	if (tk_slots[1].runs != 1) {
		case_line("L11", 0, "two arms with no drain between them must "
		    "run once (runs=%u)", tk_slots[1].runs);
		return -1;
	}

	/* (3) no loss past the queue's depth: the worker is held again and the
	 * burst is larger than the seam's queue (32 slots), so some arms are
	 * refused the slot and must run in this context - none may be dropped */
	if (tk_hold_worker() != 0) {
		case_line("L11", 0, "the second gate did not hold the worker");
		return -1;
	}
	for (i = 2; i < TK_N; i++) {
		tasklet_init(&tk_tasks[i], tk_cb, i);
		tk_arm(&tk_tasks[i]);
	}
	tk_gate_open = 1;
	for (i = 2; i < TK_N; i++) {
		if (tk_wait(&tk_slots[i].runs, 1, 5000) != 0) {
			case_line("L11", 0, "the burst lost the callback of slot "
			    "%u", i);
			return -1;
		}
		total += tk_slots[i].runs;
		inline_runs += tk_slots[i].inline_runs;
	}
	lkpi_msleep(50);
	for (i = 2; i < TK_N; i++) {
		if (tk_slots[i].runs != 1) {
			case_line("L11", 0, "slot %u ran %u times, not once",
			    i, tk_slots[i].runs);
			return -1;
		}
	}
	if (inline_runs < 1) {
		case_line("L11", 0, "a burst past the queue's depth must run the "
		    "refused arms in the caller (inline=%u)", inline_runs);
		return -1;
	}

	/* (4) teardown: kill waits for an armed or running callback to leave the
	 * seam and leaves the object usable (the driver kills in its teardown and
	 * re-inits on the next bring-up) */
	tasklet_init(&tk_tasks[1], tk_cb, 1);
	tk_arm(&tk_tasks[1]);
	tasklet_kill(&tk_tasks[1]);
	if (tk_slots[1].runs != 2 || tk_tasks[1].work.wk_queued != 0 ||
	    tk_tasks[1].work.wk_running != 0) {
		case_line("L11", 0, "kill must leave nothing outstanding "
		    "(runs=%u queued=%d running=%d)", tk_slots[1].runs,
		    tk_tasks[1].work.wk_queued, tk_tasks[1].work.wk_running);
		return -1;
	}
	tk_arm(&tk_tasks[1]);
	if (tk_wait(&tk_slots[1].runs, 3, 2000) != 0) {
		case_line("L11", 0, "the object must still run after kill (the "
		    "driver re-arms on the next bring-up)");
		return -1;
	}

	case_line("L11", 1, "deferred off-thread; two arms -> one run; kill "
	    "leaves nothing outstanding; the %u-callback burst ran complete "
	    "(%u in the caller)", total, inline_runs);
	return 0;
}

/* ------------------------------------------------------------------ */
/* L12 the wrapped-format conversion ("%pV") on the real face          */
/* ------------------------------------------------------------------ */

/*
 * The vendored driver's log helpers wrap a format and its arguments in a
 * struct va_format and hand the pair through one "%pV" conversion
 * (third-party/iwlwifi/iwl-debug.c: __iwl_err, __iwl_info, __iwl_warn,
 * __iwl_crit, and the "(RFKILL) %pV" line). A formatter without that
 * conversion prints the pointer and a literal 'V' - which is what the board's
 * serial log showed (lkpi: 03:00.0: 000000000A719370V) while a probe failed,
 * so not one word the driver said could be read. This case drives the
 * producer's own shape and checks the text that comes out.
 */

#define L12_LINES	12

static char	l12_lines[L12_LINES][256];
static unsigned	l12_count;
static int	l12_fault_pv_inert;	/* --fault pv-inert: the pre-fix engine path */

/* capture the line and keep the platform sink's observable shape (everything
 * the sink takes is also on stdout) */
static void
l12_record(const char *line)
{
	if (l12_count < L12_LINES)
		snprintf(l12_lines[l12_count], sizeof(l12_lines[0]), "%s", line);
	l12_count++;

	fputs("lkpi: ", stdout);
	fputs(line, stdout);
	if (line[0] != '\0' && line[strlen(line) - 1] != '\n')
		fputc('\n', stdout);
	fflush(stdout);
}

static void
l12_sink(ulog_level_t level, const char *line)
{

	(void)level;
	l12_record(line);
}

/*
 * The pre-fix face, kept for the red control: it hands the format straight to
 * the formatter, which is the behavior that put the pointer and the 'V' on the
 * board. With --fault pv-inert the producers below route through this instead
 * of dev_*(), so the case's own content checks have to catch the blindness.
 */
static void
l12_legacy_engine(struct device *dev, const char *fmt, ...)
{
	char line[256];
	va_list ap;
	int n;

	n = snprintf(line, sizeof(line), "%s: ", dev->name);
	if (n < 0)
		return;
	va_start(ap, fmt);
	(void)vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
	va_end(ap);
	l12_record(line);
}

/* the producer's shape, verbatim from iwl-debug.c: the same vaf is used twice
 * (there it feeds dev_*() and then the trace point) */
static void
l12_iwl_info(struct device *dev, const char *fmt, ...)
{
	struct va_format vaf = { .fmt = fmt, };
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	if (l12_fault_pv_inert) {
		l12_legacy_engine(dev, "%pV", &vaf);
	} else {
		dev_info(dev, "%pV", &vaf);	/* the wrapped message */
		dev_info(dev, "%pV", &vaf);	/* and the same vaf again */
	}
	va_end(args);
}

/* the literal-prefix shape (__iwl_err's rfkill branch) */
static void
l12_rfkill_err(struct device *dev, const char *fmt, ...)
{
	struct va_format vaf = { .fmt = fmt, };
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	if (l12_fault_pv_inert)
		l12_legacy_engine(dev, "(RFKILL) %pV", &vaf);
	else
		dev_err(dev, "(RFKILL) %pV", &vaf);
	va_end(args);
}

/* a literal prefix that holds a "%%" escape: the escape is literal text and
 * must not turn into a conversion hunt */
static void
l12_prefix_pct(struct device *dev, const char *fmt, ...)
{
	struct va_format vaf = { .fmt = fmt, };
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	if (l12_fault_pv_inert)
		l12_legacy_engine(dev, "%% literal then %pV", &vaf);
	else
		dev_info(dev, "%% literal then %pV", &vaf);
	va_end(args);
}

/* a wrapped format inside a wrapped format: the conversion recurses */
static void
l12_nested_emit(struct device *dev, struct va_format *leaf)
{

	if (l12_fault_pv_inert)
		l12_legacy_engine(dev, "wrap(%pV)done", leaf);
	else
		dev_info(dev, "wrap(%pV)done", leaf);
}

static void
l12_nested(struct device *dev, const char *fmt, ...)
{
	struct va_format vaf = { .fmt = fmt, };
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	l12_nested_emit(dev, &vaf);
	va_end(args);
}

/* the documented boundary: "%pV" beside other conversions cannot be consumed
 * correctly on this ABI, so it is passed through - and said once */
static void
l12_dbg_mixed(struct device *dev, const char *fn, const char *fmt, ...)
{
	struct va_format vaf = { .fmt = fmt, };
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_info(dev, "%s %pV", fn, &vaf);
	va_end(args);
}

static int
l12_expect(unsigned idx, const char *want, const char *what)
{

	if (idx >= l12_count || strcmp(l12_lines[idx], want) != 0) {
		case_line("L12", 0, "%s: want \"%s\", got \"%s\"", what, want,
		    idx < l12_count ? l12_lines[idx] : "(no line)");
		return -1;
	}
	return 0;
}

static int
case_l12(void)
{
	struct device dev = { .name = "03:00.0" };
	unsigned notices = 0;
	unsigned i;

	lkpi_log_sink_register(l12_sink);

	l12_iwl_info(&dev, "Detected Intel(R) %s rev %u", "AC 7260", 3);
	l12_rfkill_err(&dev, "radio state %d", 1);
	l12_prefix_pct(&dev, "n=%u", 9);
	dev_info(&dev, "100%%pV ok");		/* no conversion at all */
	dev_info(&dev, "plain %s=%d", "x", 42);	/* ordinary conversions intact */
	l12_nested(&dev, "leaf=%u", 7);
	l12_dbg_mixed(&dev, "iwl_dbg", "fn-inner %u", 5);
	l12_dbg_mixed(&dev, "iwl_dbg", "fn-inner %u", 6);

	if (l12_expect(0, "03:00.0: Detected Intel(R) AC 7260 rev 3",
	    "wrapped message") != 0)
		return -1;
	if (l12_expect(1, "03:00.0: Detected Intel(R) AC 7260 rev 3",
	    "the vaf reused after the call") != 0)
		return -1;
	if (l12_expect(2, "03:00.0: (RFKILL) radio state 1",
	    "literal prefix") != 0)
		return -1;
	if (l12_expect(3, "03:00.0: % literal then n=9",
	    "a %% escape in the prefix") != 0)
		return -1;
	if (l12_expect(4, "03:00.0: 100%pV ok", "the literal %pV text") != 0)
		return -1;
	if (l12_expect(5, "03:00.0: plain x=42", "ordinary convs") != 0)
		return -1;
	if (l12_expect(6, "03:00.0: wrap(leaf=7)done", "nested wrapped") != 0)
		return -1;

	/* the mixed shape: the wrapped text must NOT appear (the engine got the
	 * format as-is), the notice must have been said exactly once for both
	 * calls, and both producer lines must still be there */
	if (l12_count != 10) {
		case_line("L12", 0, "expected 10 lines (2 mixed + 1 notice), got %u",
		    l12_count);
		return -1;
	}
	for (i = 0; i < l12_count; i++) {
		if (strstr(l12_lines[i], "fn-inner") != NULL) {
			case_line("L12", 0, "a mixed format expanded anyway: \"%s\"",
			    l12_lines[i]);
			return -1;
		}
		if (strstr(l12_lines[i], "not expanded") != NULL)
			notices++;
	}
	if (notices != 1) {
		case_line("L12", 0, "the mixed-format notice must be said once "
		    "(said %u times)", notices);
		return -1;
	}

	case_line("L12", 1, "wrapped/%%-literal/nested expanded; vaf reusable; "
	    "mixed shape noted once");
	return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void
run_suite(void)
{
	struct mock_backend *b;

	lkpi_body_test_platform_init();

	/* the blob registers itself on the initialisation path, exactly as a
	 * board build would before the drivers run */
	if (lkpi_fw_rtl8188eufw_register() != 0 ||
	    lkpi_fw_rtw8821c_fw_register() != 0) {
		case_line("L1", 0, "the firmware blob registration failed");
		return;
	}

	b = mock_backend_create();
	if (b == NULL) {
		case_line("L1", 0, "the synthetic controller did not start");
		return;
	}
	lkpi_usb_be_register(mock_backend_ops(), b);

	/*
	 * The bring-up switch a callback-riding driver throws (the engine's own
	 * comment says the wireless lines flip it): bulk traffic then runs the
	 * asynchronous lane - submit returns, the completion arrives from the
	 * controller's context - which is the discipline the driver's RX/TX
	 * urbs are built on. The mass-storage lane leaves it on.
	 */
	lkpi_usb_force_sync_set(false);

	if (case_l1() != 0)
		return;
	if (case_l2() != 0)
		return;
	if (case_l3(b) != 0)
		return;
	if (case_l4(b) != 0)
		return;
	if (case_l5() != 0)
		return;
	if (case_l6(b) != 0)
		return;
	if (case_l7() != 0)
		return;
	(void)case_l8(b);
	if (case_l9(b) != 0)
		return;
	if (case_l10() != 0)
		return;
	if (case_l11() != 0)
		return;
	if (case_l12() != 0)
		return;

	if (failures == 0)
		printf("LKPI: SUITE PASS cases=12\n");
	else
		printf("LKPI: SUITE FAIL failures=%d\n", failures);
	fflush(stdout);
}

static void
app_task(void *arg)
{

	(void)arg;
	run_suite();
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
			fault_name = argv[++i];
		} else {
			printf("LKPI: usage: %s [--fault no-walk|fw-name|ms-eject"
			    "|tasklet-inert|pv-inert]\n", argv[0]);
			return 2;
		}
	}

	if (fault_name != NULL) {
		if (strcmp(fault_name, "no-walk") != 0 &&
		    strcmp(fault_name, "fw-name") != 0 &&
		    strcmp(fault_name, "ms-eject") != 0 &&
		    strcmp(fault_name, "tasklet-inert") != 0 &&
		    strcmp(fault_name, "pv-inert") != 0) {
			printf("LKPI: unknown fault '%s'\n", fault_name);
			return 2;
		}
		if (strcmp(fault_name, "tasklet-inert") == 0)
			tk_inert = 1;
		if (strcmp(fault_name, "pv-inert") == 0)
			l12_fault_pv_inert = 1;
	}

	/* the CMSIS-RTOS2 kernel hosts the suite's tasks; the suite body runs as
	 * a task so the engine's drain thread and the controller worker exist on
	 * the same kernel the board build runs */
	if (osKernelInitialize() != osOK) {
		printf("LKPI: kernel init failed\n");
		return 2;
	}
	attr.name = "lkpi-body";
	attr.stack_size = 16384u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("LKPI: cannot create the application task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	return failures == 0 ? 0 : 1;
}
