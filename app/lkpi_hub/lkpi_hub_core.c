/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lkpi_hub_core.c - the clean-room hub status-change acceptance driver
 * (SPEC specs/linuxkpi-hub v1.1, R2).  First-party acceptance carrier,
 * NOT a hub manager: it exists to drive the linuxkpi interrupt-transfer
 * surface through the exact consumption shape the kernel hub driver uses
 * (usb_fill_int_urb + usb_submit_urb + irq-callback re-arm), on both
 * targets.  It does NO hub protocol handling - no port power, no
 * enumeration, no TT - except the minimal change-bit clear walk, without
 * which the hub's level-resend bitmap turns event counting into a
 * function of the poll rate ([USB2] §11.12.4: an uncleared bitmap is
 * re-delivered on every poll) and the two targets' counts diverge.
 *
 * Clean-room basis (protocol FACTS only, no hub.c code):
 *   - Class identity: hub 0x09 ([LNX] ch9.h:330), single-TT interface
 *     subclass/protocol 0/0 (the cherryusb hub class claims the same
 *     triple on this board, P5-b).
 *   - Status-change bitmap layout: bit 0 = hub-local change, bit p =
 *     port p change (p counts from 1); byte count = ceil((bNbrPorts+1)/8)
 *     ([USB2] §11.12.4 Figure 11-22, byte-granularity note).
 *   - Level-resend: any poll while bits are non-zero returns the bitmap
 *     ([USB2] §11.12.4); port change bits clear ONLY via ClearPortFeature
 *     ([USB2] §11.24.2.7.2), never by reading.
 *   - Feature selectors ([LNX] ch11.h:69-84 == [USB2] Table 11-22,
 *     dual-source per the P5-e lesson): PORT_CONNECTION 0 / PORT_ENABLE 1
 *     / PORT_RESET 4 / PORT_POWER 8; C_PORT_CONNECTION 16 / C_PORT_ENABLE
 *     17 / C_PORT_SUSPEND 18 / C_PORT_OVER_CURRENT 19 / C_PORT_RESET 20.
 *   - Hub descriptor ([USB2] §11.23.2.1): GET_DESCRIPTOR class, two-step
 *     read (1 byte for bDescLength, then the full record); bNbrPorts at
 *     offset 2.  Request shape per [CHY] usbh_hub.c:59-79 (read, not
 *     copied).
 *   - Completion semantics ([LNX] hub.c:776-809 hub_irq, read not
 *     copied): cancellation statuses do NOT re-arm; other errors count
 *     and stop after ten consecutive; success re-arms the same urb.
 *   - Stimulus (spec v1.1 §3.2-1-⑤, the unattended event source):
 *     SetFeature(PORT_RESET) on a downstream port latches C_PORT_RESET
 *     (plus C_PORT_ENABLE when the reset completes) and fires the same
 *     int-EP event a physical unplug would.
 *
 * Dual-target discipline (R2): this file compiles UNCHANGED on the Linux
 * target (real kernel headers) and on the freewebcam linuxkpi target;
 * the #ifdef count is zero and must stay zero.  The shells only
 * register, trigger and print.
 */

#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/usb.h>

#include "lkpi_hub.h"

/* the two shells drive the driver object and the trigger entries */
struct usb_driver lkpi_hub_driver;

/* ------------------------------------------------------------------ */
/* protocol constants (clean-room facts, see the file header)          */
/* ------------------------------------------------------------------ */

#define UH_GET_STATUS		0x00u
#define UH_CLEAR_FEATURE	0x01u
#define UH_SET_FEATURE		0x03u
#define UH_GET_DESCRIPTOR	0x06u

#define UH_DT_HUB		0x29u

#define UH_PORT_CONNECTION	0u
#define UH_PORT_ENABLE		1u
#define UH_PORT_RESET		4u
#define UH_PORT_POWER		8u

#define UH_CPORT_CONNECTION	16u
#define UH_CPORT_ENABLE		17u
#define UH_CPORT_SUSPEND	18u
#define UH_CPORT_OVER_CURRENT	19u
#define UH_CPORT_RESET		20u

/* bmRequestType bytes: class request to the hub itself (device) and to
 * its downstream ports (recipient OTHER per [USB2] §11.24.2) */
#define UH_RT_DEV_IN	(USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE)
#define UH_RT_PORT_IN	(USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER)
#define UH_RT_PORT_OUT	(USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER)

#define UH_CTRL_TIMEOUT_MS	5000
#define UH_DESC_MAX		16u

/* the stimulus script (spec v1.1 §3.2-1-⑤): N rounds, half a second
 * apart; each round latches at least C_PORT_RESET on the target port */
#define UH_STIM_ROUNDS		6u
#define UH_STIM_GAP_MS		500u

/* hub_irq's consecutive-error stop threshold ([LNX] hub.c:789-798 uses
 * the same ten; identical on both targets keeps the behaviour aligned) */
#define UH_IRQ_ERR_LIMIT	10u

/* the capture arm retry: 10 ms beat, ten seconds ceiling (spec §3.3-2;
 * the window opens when the hub class consumes the sacrifice event and
 * its one-shot timer's submit loses the race) */
#define UH_CAPTURE_TRIES	1000u
#define UH_CAPTURE_BEAT_MS	10u

/* ------------------------------------------------------------------ */
/* driver context                                                      */
/* ------------------------------------------------------------------ */

struct uh_ctx {
	struct usb_device	*udev;
	struct usb_interface	*intf;
	struct urb		*urb;
	struct work_struct	dwork;
	u8			int_in;		/* the status-change EP */
	u8			bInterval;
	u16			maxp;
	u8			nports;		/* bNbrPorts from the hub desc */
	u8			bitmap_len;	/* ceil((nports+1)/8) */
	u8			urb_len;	/* min(bitmap_len, maxp) */
	u32			events;		/* delta-event count */
	u32			errors;		/* consecutive-error count */
	bool			armed;
	bool			stopped;	/* cancel/limit: no re-arm */
	bool			alive;
	bool			delivered;	/* a completion reached irq */
	unsigned long		arm_j;		/* armed->event latency ref */
	/* control-line scratch, heap-resident: the core DMA-maps the data
	 * buffer and rejects stack objects with -EAGAIN (hcd.c
	 * object_is_on_stack - the udisk first-target round-2 lesson) */
	u8			hub_desc[UH_DESC_MAX];
	u8			port_status[4];
};

/* the int-URB's DMA-facing buffer: static aligned storage (P5-d board
 * lesson - the xHCI port asserts buffer alignment; here every byte also
 * rides the kpi stage pool, this is belt and braces), 8 bytes covers any
 * hub with up to 63 ports */
static u8 uh_bitmap[8] __attribute__((aligned(64)));

/* ------------------------------------------------------------------ */
/* control helpers (usb_control_msg consumers, spec §3.4-1/-4)         */
/* ------------------------------------------------------------------ */

static int
uh_get_hub_desc(struct uh_ctx *ctx)
{
	int ret;

	/* two-step read: one byte fetches bDescLength, the second takes
	 * the whole record (the shape usbh_hub.c:59-79 and hub.c share) */
	ret = usb_control_msg(ctx->udev, usb_rcvctrlpipe(ctx->udev, 0),
	    UH_GET_DESCRIPTOR, UH_RT_DEV_IN, (int)(UH_DT_HUB << 8), 0,
	    ctx->hub_desc, 1, UH_CTRL_TIMEOUT_MS);
	if (ret < 0)
		return ret;
	if (ctx->hub_desc[0] < 7u || ctx->hub_desc[0] > UH_DESC_MAX)
		return -EIO;
	ret = usb_control_msg(ctx->udev, usb_rcvctrlpipe(ctx->udev, 0),
	    UH_GET_DESCRIPTOR, UH_RT_DEV_IN, (int)(UH_DT_HUB << 8), 0,
	    ctx->hub_desc, (u16)ctx->hub_desc[0], UH_CTRL_TIMEOUT_MS);
	if (ret < 0)
		return ret;
	return 0;
}

static int
uh_get_port_status(struct uh_ctx *ctx, u32 port)
{

	return usb_control_msg(ctx->udev, usb_rcvctrlpipe(ctx->udev, 0),
	    UH_GET_STATUS, UH_RT_PORT_IN, 0, (u16)port,
	    ctx->port_status, 4, UH_CTRL_TIMEOUT_MS);
}

static int
uh_port_feature(struct uh_ctx *ctx, u32 port, u8 request, u16 feature)
{

	return usb_control_msg(ctx->udev, usb_sndctrlpipe(ctx->udev, 0),
	    request, UH_RT_PORT_OUT, feature, (u16)port, NULL, 0,
	    UH_CTRL_TIMEOUT_MS);
}

/* the change-bit clear walk (spec §3.4-4, v1.1 per-bit shape): for every
 * set port bit in the delivered bitmap read that port's wPortChange and
 * clear each latched C_ selector.  Clearing only C_PORT_CONNECTION (the
 * v1.0 text) would leak C_RESET/C_ENABLE latched by the software
 * stimulus - the bitmap would level-resend forever and event counting
 * would die.  Bit 0 (hub-local change) is recorded, not cleared - rare,
 * the resend is accepted, identical on both targets. */
static void
uh_clear_walk(struct uh_ctx *ctx)
{
	unsigned int p, c;
	u16 change;
	int ret;

	for (p = 1; p <= ctx->nports && p < 64u; p++) {
		if ((uh_bitmap[p / 8] & (u8)(1u << (p % 8))) == 0)
			continue;
		ret = uh_get_port_status(ctx, p);
		if (ret < 0) {
			/* usb_control_msg returns the byte count: 4 is the
			 * SUCCESS shape here, only negatives fail (the
			 * round-2 flood bug - `!= 0` skipped every clear
			 * and the latch level-resend never stopped) */
			pr_err("lkpi_hub: clear walk port %u GetPortStatus "
			    "ret %d\n", p, ret);
			continue;
		}
		change = (u16)(ctx->port_status[2] |
		    ((u16)ctx->port_status[3] << 8));
		pr_info("lkpi_hub: clear walk port %u wPortChange=%04x "
		    "wPortStatus=%04x\n", p, change,
		    (u16)(ctx->port_status[0] |
		    ((u16)ctx->port_status[1] << 8)));
		/* wPortChange bits 0..4 map to selectors 16..20; the rest
		 * are reserved ([USB2] Table 11-22) */
		for (c = UH_CPORT_CONNECTION; c <= UH_CPORT_RESET; c++) {
			if ((change & (u16)(1u << (c - UH_CPORT_CONNECTION)))
			    != 0) {
				ret = uh_port_feature(ctx, p,
				    UH_CLEAR_FEATURE, (u16)c);
				pr_info("lkpi_hub: clear port %u selector "
				    "%u ret %d\n", p, c, ret);
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* the int-URB line (spec §3.4-1/-2)                                   */
/* ------------------------------------------------------------------ */

static void
uh_hex(const u8 *b, unsigned int n, char *out)
{
	static const char hexd[] = "0123456789abcdef";
	unsigned int i, pos = 0;

	if (n > 8)
		n = 8;
	for (i = 0; i < n; i++) {
		out[pos++] = hexd[b[i] >> 4];
		out[pos++] = hexd[b[i] & 0xf];
		out[pos++] = ' ';
	}
	out[pos] = '\0';
}

/* the completion callback - the hub_irq family shape at its ATOMIC-TRUE
 * form ([LNX] hub.c:776-809: the irq only rejects cancellation, counts
 * errors and hands the bitmap to the deferred handler; hub.c itself
 * wakes hub_wq).  The first-target round crashed the kernel here with
 * "scheduling while atomic": on Linux the completion runs from
 * usb_giveback_urb_bh (softirq), where the clear walk's
 * usb_control_msg sleeps - illegal.  Everything event-shaped (hexdump,
 * delta, clear walk, re-arm) therefore lives in the work below, on both
 * targets; the irq stays atomic-safe and re-arms retryable errors with
 * GFP_ATOMIC. */
static void uh_irq(struct urb *urb);
static int uh_arm_one(struct uh_ctx *ctx);

static void
uh_irq(struct urb *urb)
{
	struct uh_ctx *ctx = urb->context;

	ctx->delivered = true;
	if (urb->status == -ENOENT || urb->status == -ECONNRESET ||
	    urb->status == -ESHUTDOWN || urb->status == -ENODEV) {
		/* cancellation: the stop command, the unbind/rmmod or the
		 * hub going away - retire, never re-arm (hub.c:782-787) */
		ctx->stopped = true;
		ctx->armed = false;
		pr_info("lkpi_hub: irq stop status %d\n", urb->status);
		return;
	}
	if (urb->status != 0) {
		ctx->errors++;
		pr_err("lkpi_hub: irq error %d (consecutive %u)\n",
		    urb->status, ctx->errors);
		if (ctx->errors > UH_IRQ_ERR_LIMIT) {
			ctx->stopped = true;
			ctx->armed = false;
			pr_err("lkpi_hub: %u consecutive errors, stop\n",
			    ctx->errors);
			return;
		}
		/* retryable: re-arm right here (atomic-safe submit) */
		if (ctx->alive)
			(void)usb_submit_urb(urb, GFP_ATOMIC);
		return;
	}
	schedule_work(&ctx->dwork);
}

/* the deferred event handler (the hub_wq counterpart): hexdump, delta
 * count, the per-bit clear walk, then re-arm */
static void
uh_event_work(struct work_struct *w)
{
	struct uh_ctx *ctx = container_of(w, struct uh_ctx, dwork);
	unsigned long lat;
	char hex[3 * 8 + 1];

	if (!ctx->alive || ctx->stopped)
		return;
	ctx->errors = 0;
	lat = jiffies - ctx->arm_j;
	uh_hex(uh_bitmap, ctx->urb->actual_length, hex);
	/* the event unit = one NON-ZERO bitmap delivery: the hub only
	 * asserts the int-EP when a change bit latched, and the clear
	 * walk below returns the latch to zero - so the next stimulus's
	 * delivery carries the SAME bits as the previous one, and a
	 * compare-against-last-delivered would swallow every repeat
	 * event (the first-target round-1 finding: six stimuli, one
	 * event).  A zero delivery (latch raced by another clear)
	 * carries no event. */
	if (ctx->urb->actual_length == 0 || uh_bitmap[0] == 0) {
		pr_info("lkpi_hub: poll zero bitmap (raced clear)\n");
	} else {
		ctx->events++;
		pr_info("lkpi_hub: event #%u bitmap=0x%02x raw %s latency "
		    "%lu ms\n", ctx->events, uh_bitmap[0], hex,
		    (unsigned long)jiffies_to_msecs(lat));
		uh_clear_walk(ctx);
	}
	if (!ctx->stopped && ctx->alive)
		(void)uh_arm_one(ctx);
}

static int
uh_arm_one(struct uh_ctx *ctx)
{
	unsigned int pipe = usb_rcvintpipe(ctx->udev, ctx->int_in);
	int ret;

	usb_fill_int_urb(ctx->urb, ctx->udev, pipe, uh_bitmap,
	    (int)ctx->urb_len, uh_irq, ctx, (int)ctx->bInterval);
	ctx->arm_j = jiffies;
	ret = usb_submit_urb(ctx->urb, GFP_KERNEL);
	if (ret != 0) {
		ctx->armed = false;
		return ret;
	}
	ctx->armed = true;
	return 0;
}

int
lkpi_hub_arm(struct usb_interface *intf)
{
	struct uh_ctx *ctx = usb_get_intfdata(intf);
	int ret;

	if (ctx == NULL || !ctx->alive)
		return -ENODEV;
	if (ctx->armed)
		return 0;
	ret = uh_arm_one(ctx);
	if (ret == 0)
		pr_info("lkpi_hub: armed int-in ep%02x bInterval=%u "
		    "urb->interval=%d len=%u\n", ctx->int_in, ctx->bInterval,
		    ctx->urb->interval, ctx->urb_len);
	return ret;
}

/* ------------------------------------------------------------------ */
/* the stimulus script + capture protocol (spec v1.1 §3.2-1-⑤/§3.3-2)  */
/* ------------------------------------------------------------------ */

int
lkpi_hub_stim(struct usb_interface *intf, u32 port, u32 rounds)
{
	struct uh_ctx *ctx = usb_get_intfdata(intf);
	u32 i;
	int ret;

	if (ctx == NULL || !ctx->alive)
		return -ENODEV;
	if (port == 0 || port > (u32)ctx->nports) {
		pr_err("lkpi_hub: port %u out of range 1..%u\n", port,
		    ctx->nports);
		return -EINVAL;
	}
	for (i = 0; i < rounds; i++) {
		ret = uh_port_feature(ctx, port, UH_SET_FEATURE,
		    (u16)UH_PORT_RESET);
		pr_info("lkpi_hub: stim #%u port %u SetFeature(PORT_RESET) "
		    "ret %d\n", i + 1, port, ret);
		if (ret != 0 && ret != -EPIPE)
			return ret;
		msleep(UH_STIM_GAP_MS);
	}
	return 0;
}

int
lkpi_hub_capture(struct usb_interface *intf, u32 port, u32 rounds)
{
	struct uh_ctx *ctx = usb_get_intfdata(intf);
	unsigned int tries;
	int ret;

	if (ctx == NULL || !ctx->alive)
		return -ENODEV;
	if (!ctx->armed) {
		/* the window opener (spec §3.3-2): the sacrifice stimulus
		 * is consumed by the hub class, whose events handling
		 * restarts its one-shot timer - the gap until that submit
		 * is where this driver's arm lands and stays (the hub
		 * class's next timer submit then eats -BUSY and its
		 * polling dies, which is the protocol, not an accident) */
		ret = lkpi_hub_stim(intf, port, 1);
		if (ret != 0)
			return ret;
		ret = -EIO;
		for (tries = 0; tries < UH_CAPTURE_TRIES; tries++) {
			ret = lkpi_hub_arm(intf);
			if (ret == 0)
				break;
			if ((tries % 100u) == 0)
				pr_info("lkpi_hub: window retry %u ret %d\n",
				    tries, ret);
			msleep(UH_CAPTURE_BEAT_MS);
		}
		if (ret != 0) {
			pr_err("lkpi_hub: window never opened (ret %d)\n",
			    ret);
			return ret;
		}
	}
	ret = lkpi_hub_stim(intf, port, rounds);
	pr_info("lkpi_hub: stats events=%u errors=%u stopped=%d\n",
	    ctx->events, ctx->errors, (int)ctx->stopped);
	return ret;
}

void
lkpi_hub_stop(struct usb_interface *intf)
{
	struct uh_ctx *ctx = usb_get_intfdata(intf);
	unsigned int i;

	if (ctx == NULL)
		return;
	ctx->alive = false;
	if (ctx->armed) {
		usb_kill_urb(ctx->urb);
		ctx->armed = false;
		/* the kill's completion may still be queued on the drain
		 * thread; wait it out so the caller can safely retire the
		 * driver memory (the deliver runs uh_irq on this ctx) */
		for (i = 0; i < 100u && !ctx->delivered; i++)
			msleep(2);
	}
	cancel_work_sync(&ctx->dwork);
	pr_info("lkpi_hub: stop events=%u errors=%u\n", ctx->events,
	    ctx->errors);
}

u32
lkpi_hub_count(struct usb_interface *intf)
{
	struct uh_ctx *ctx = usb_get_intfdata(intf);

	return ctx != NULL ? ctx->events : 0u;
}

/* ------------------------------------------------------------------ */
/* probe / disconnect                                                  */
/* ------------------------------------------------------------------ */

/* the shell's post-probe hook (weak): the Linux shell overrides it to
 * arm + run the stimulus script from module params; the RTOS shell only
 * remembers the interface (the capture protocol triggers from the
 * shell, after attach) */
__weak void
uh_post_probe(struct usb_interface *intf)
{
	(void)intf;
}

static int
uh_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *alt;
	struct usb_endpoint_descriptor *d = NULL;
	struct uh_ctx *ctx;
	unsigned int i;
	u16 maxp;
	int ret;

	(void)id;
	pr_info("lkpi_hub: probe %04x:%04x dev %u\n",
	    dev->descriptor.idVendor, dev->descriptor.idProduct,
	    dev->devnum);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL)
		return -ENOMEM;
	ctx->udev = dev;
	ctx->intf = intf;
	INIT_WORK(&ctx->dwork, uh_event_work);
	usb_set_intfdata(intf, ctx);

	/* the status-change int-in EP (spec §3.4-1) */
	alt = intf->cur_altsetting;
	for (i = 0; i < (unsigned int)alt->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *e = &alt->endpoint[i].desc;

		if (usb_endpoint_xfer_int(e) && usb_endpoint_dir_in(e))
			d = e;
	}
	if (d == NULL) {
		pr_err("lkpi_hub: no int-in endpoint\n");
		ret = -ENODEV;
		goto fail;
	}
	ctx->int_in = (u8)usb_endpoint_num(d);
	ctx->bInterval = d->bInterval;
	maxp = usb_maxpacket(dev, (int)usb_rcvintpipe(dev, ctx->int_in));
	ctx->maxp = maxp != 0 ? maxp : 64u;

	/* the hub descriptor: bNbrPorts sizes the bitmap (both targets
	 * self-fetch - the shared-source discipline, spec §3.4-1) */
	ret = uh_get_hub_desc(ctx);
	if (ret != 0) {
		pr_err("lkpi_hub: hub descriptor failed %d\n", ret);
		goto fail;
	}
	ctx->nports = ctx->hub_desc[2];
	ctx->bitmap_len = (u8)((ctx->nports + 1u + 7u) / 8u);
	if (ctx->bitmap_len < 1u)
		ctx->bitmap_len = 1u;
	if (ctx->bitmap_len > 8u)
		ctx->bitmap_len = 8u;
	/* the urb length: the descriptor bound, never above maxp (the
	 * [LNX] hub.c:1716-1726 rationale - some hubs send more) */
	ctx->urb_len = ctx->bitmap_len;
	if (ctx->urb_len > ctx->maxp)
		ctx->urb_len = (u8)ctx->maxp;

	ctx->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (ctx->urb == NULL) {
		ret = -ENOMEM;
		goto fail;
	}
	ctx->alive = true;

	pr_info("lkpi_hub: claimed dev %u (%04x:%04x) interface, int-in "
	    "ep%02x, bInterval=%u, bitmap_bytes=%u, nports=%u\n",
	    dev->devnum, dev->descriptor.idVendor, dev->descriptor.idProduct,
	    ctx->int_in, ctx->bInterval, ctx->bitmap_len, ctx->nports);

	/* the spec's RTOS probe records only (the capture protocol arms
	 * inside its window); arming is the shells' post-probe decision */
	uh_post_probe(intf);
	return 0;

fail:
	usb_set_intfdata(intf, NULL);
	usb_free_urb(ctx->urb);
	kfree(ctx);
	return ret;
}

static void
uh_disconnect(struct usb_interface *intf)
{
	struct uh_ctx *ctx = usb_get_intfdata(intf);
	unsigned int i;

	if (ctx == NULL)
		return;
	usb_set_intfdata(intf, NULL);
	ctx->alive = false;
	if (ctx->armed) {
		usb_kill_urb(ctx->urb);
		ctx->armed = false;
		/* let the drain thread's delivery land before freeing the
		 * ctx it will run against (the stop-path rationale) */
		for (i = 0; i < 100u && !ctx->delivered; i++)
			msleep(2);
	}
	cancel_work_sync(&ctx->dwork);
	usb_free_urb(ctx->urb);
	kfree(ctx);
	pr_info("lkpi_hub: disconnected\n");
}

/* the id_table: generic hub class match, no VID whitelist (the
 * acceptance carrier's world has one 0x09 interface; spec §3.2-3).
 * [USB_INTERFACE_INFO, not USB_DEVICE_AND_INTERFACE_INFO - the latter
 * would pin VID/PID 0/0 into the match.] */
static const struct usb_device_id lkpi_hub_ids[] = {
	{ USB_INTERFACE_INFO(0x09, 0x00, 0x00) },
	{ }
};

struct usb_driver lkpi_hub_driver = {
	.name = "lkpi_hub",
	.probe = uh_probe,
	.disconnect = uh_disconnect,
	.id_table = lkpi_hub_ids,
};
