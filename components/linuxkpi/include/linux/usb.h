/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linux/usb.h - the Linux USB host API the verbatim drivers compile
 * against.  The backend is CherryUSB's usbh_urb engine (linuxkpi_usb.c);
 * completion callbacks are deferred to the lkpi workqueue before they
 * reach driver code, which is what lets the driver's spinlocks stay
 * passive mutexes.
 */

#ifndef _LINUXKPI_LINUX_USB_H_
#define _LINUXKPI_LINUX_USB_H_

#include "../../linuxkpi_core.h"

/* ------------------------------------------------------------------ */
/* constants                                                           */
/* ------------------------------------------------------------------ */

#define	USB_DIR_OUT			0x00
#define	USB_DIR_IN			0x80

#define	USB_TYPE_STANDARD		0x00
#define	USB_TYPE_CLASS			0x20
#define	USB_TYPE_VENDOR			0x40
#define	USB_TYPE_MASK			0x60

#define	USB_RECIP_DEVICE		0x00
#define	USB_RECIP_INTERFACE		0x01
#define	USB_RECIP_ENDPOINT		0x02
#define	USB_RECIP_OTHER			0x03

#define	USB_ENDPOINT_XFER_CONTROL	0x00
#define	USB_ENDPOINT_XFER_ISOC		0x01
#define	USB_ENDPOINT_XFER_BULK		0x02
#define	USB_ENDPOINT_XFER_INT		0x03
#define	USB_ENDPOINT_XFER_INT_MASK	0x03

#define	USB_ENDPOINT_XFERTYPE_MASK	0x03
#define	USB_ENDPOINT_NUMBER_MASK	0x0f
#define	USB_ENDPOINT_DIR_MASK		0x80

#ifndef USB_SPEED_UNKNOWN
enum usb_device_speed {
	USB_SPEED_UNKNOWN = 0,
	USB_SPEED_LOW,
	USB_SPEED_FULL,
	USB_SPEED_HIGH,
	USB_SPEED_WIRELESS,
	USB_SPEED_SUPER,
	USB_SPEED_SUPER_PLUS,
};
#endif

/* the state enum has no CherryUSB counterpart; values are ours */
enum usb_device_state {
	USB_STATE_NOTATTACHED = 0,
	USB_STATE_ATTACHED,
	USB_STATE_POWERED,
	USB_STATE_DEFAULT,
	USB_STATE_ADDRESS,
	USB_STATE_CONFIGURED,
};

/* workqueue flags the usb path uses (6.15+ added WQ_PERCPU; this port's
 * workqueue is the singleton, so the flags select nothing) */
#define	WQ_BH				0x00000020
#define	WQ_PERCPU			0x00000040
#define	WQ_UNBOUND			0x00000001
#define	WQ_HIGHPRI			0x00000004
#define	WQ_SYSFS			0x00000010

#define	USB_CTRL_GET_TIMEOUT		5000
#define	USB_CTRL_SET_TIMEOUT		5000

#define	URB_ZERO_PACKET			0x01
#define	URB_NO_TRANSFER_DMA_MAP		0x02
#define	URB_KILLED			0x04

/* ------------------------------------------------------------------ */
/* descriptor shapes (the USB spec layout, byte-identical upstream).    */
/* The driver world defines them here; when CherryUSB's usb_def.h came  */
/* in first (the bridge and the class hook), USB_DEF_H is set, these    */
/* definitions are skipped, and CherryUSB's own spellings win - one     */
/* descriptor type per image either way.                                */
/* ------------------------------------------------------------------ */

#ifndef USB_DEF_H
struct usb_device_descriptor {
	u8	bLength;
	u8	bDescriptorType;
	u16	bcdUSB;
	u8	bDeviceClass;
	u8	bDeviceSubClass;
	u8	bDeviceProtocol;
	u8	bMaxPacketSize0;
	u16	idVendor;
	u16	idProduct;
	u16	bcdDevice;
	u8	iManufacturer;
	u8	iProduct;
	u8	iSerialNumber;
	u8	bNumConfigurations;
} __packed;

struct usb_endpoint_descriptor {
	u8	bLength;
	u8	bDescriptorType;
	u8	bEndpointAddress;
	u8	bmAttributes;
	u16	wMaxPacketSize;
	u8	bInterval;
} __packed;

struct usb_interface_descriptor {
	u8	bLength;
	u8	bDescriptorType;
	u8	bInterfaceNumber;
	u8	bAlternateSetting;
	u8	bNumEndpoints;
	u8	bInterfaceClass;
	u8	bInterfaceSubClass;
	u8	bInterfaceProtocol;
	u8	iInterface;
} __packed;

struct usb_config_descriptor {
	u8	bLength;
	u8	bDescriptorType;
	u16	wTotalLength;
	u8	bNumInterfaces;
	u8	bConfigurationValue;
	u8	iConfiguration;
	u8	bmAttributes;
	u8	bMaxPower;
} __packed;
/* the active-config face usb_intf.c walks (parsed at claim time; this
 * port exposes only the descriptor header - endpoint walk goes through
 * the interface array) */
struct usb_host_config {
	struct usb_config_descriptor	desc;
};
#endif /* USB_DEF_H */


/* ------------------------------------------------------------------ */
/* host-side shapes                                                    */
/* ------------------------------------------------------------------ */

struct usb_host_endpoint {
	struct usb_endpoint_descriptor desc;
	void		*hcpriv;	/* the CherryUSB descriptor behind it */
	bool		enabled;
	/* bulk-OUT data-toggle chain (patches/cherryusb/0003 keeps the
	 * toggle in software): a Linux urb is fresh per submit here, so
	 * the next-transfer toggle persists on the endpoint, not in the
	 * usbh_urb (which is memset on every submit) */
	u8		_data_toggle;
};

struct usb_host_interface {
	struct usb_interface_descriptor desc;
	struct usb_host_endpoint *endpoint;
	int		num_endpoints;
	u8		*extra;
	int		extralen;
};

struct usb_device {
	u8		devnum;
	u8		speed;
	enum usb_device_state state;
	struct device	*dev;
	/* the device descriptor ([LNX] usb_device.descriptor: the driver-
	 * visible VID/PID path; the claim hook fills it) */
	struct usb_device_descriptor descriptor;
	struct usb_host_endpoint ep0;
	char		*product;
	char		*manufacturer;
	char		*serial;
	struct usb_host_config *actconfig;
	/* linuxkpi plumbing (set at claim time by the class hook) */
	void		*_hport;	/* the usbh_hubport behind it */
	void		*_intf;		/* the claiming usb_interface */
	u16		_vendor;
	u16		_product;
};

struct usb_interface {
	struct usb_host_interface *altsetting;	/* the altsetting array */
	int		num_altsetting;
	struct usb_host_interface *cur_altsetting;
	struct device	dev;
	void		*privdata;
	struct usb_device *udev;
	bool		claimed;
	int		pm_usage_cnt;	/* [S6] B7: PM stub, always 0 */
};

static __always_inline __unused struct usb_device *
interface_to_usbdev(struct usb_interface *intf)
{
	return intf->udev;
}

/* ------------------------------------------------------------------ */
/* pipes: devnum | type | dir | endpoint, packed by linuxkpi_usb        */
/* ------------------------------------------------------------------ */

#define	__lkpi_pipe(dev, dir, type, ep)				\
	((unsigned int)(((dev) != NULL ? (dev)->devnum : 0) << 16 |	\
	((type) << 8) | ((dir) & USB_ENDPOINT_DIR_MASK) |		\
	((ep) & USB_ENDPOINT_NUMBER_MASK)))

#define	usb_sndctrlpipe(dev, ep)	__lkpi_pipe(dev, USB_DIR_OUT, USB_ENDPOINT_XFER_CONTROL, ep)
#define	usb_rcvctrlpipe(dev, ep)	__lkpi_pipe(dev, USB_DIR_IN, USB_ENDPOINT_XFER_CONTROL, ep)
#define	usb_sndbulkpipe(dev, ep)	__lkpi_pipe(dev, USB_DIR_OUT, USB_ENDPOINT_XFER_BULK, ep)
#define	usb_rcvbulkpipe(dev, ep)	__lkpi_pipe(dev, USB_DIR_IN, USB_ENDPOINT_XFER_BULK, ep)
#define	usb_sndintpipe(dev, ep)		__lkpi_pipe(dev, USB_DIR_OUT, USB_ENDPOINT_XFER_INT, ep)
#define	usb_rcvintpipe(dev, ep)		__lkpi_pipe(dev, USB_DIR_IN, USB_ENDPOINT_XFER_INT, ep)

#define	usb_pipein(pipe)		(((pipe) & USB_ENDPOINT_DIR_MASK) != 0)
#define	usb_pipeout(pipe)		(!usb_pipein(pipe))
#define	usb_pipetype(pipe)		(((pipe) >> 8) & USB_ENDPOINT_XFER_INT_MASK)
#define	usb_pipeendpoint(pipe)		((pipe) & USB_ENDPOINT_NUMBER_MASK)
#define	usb_pipedevice(pipe)		(((pipe) >> 16) & 0x7f)

/* ------------------------------------------------------------------ */
/* endpoint helpers                                                    */
/* ------------------------------------------------------------------ */

static __always_inline __unused int
usb_endpoint_num(const struct usb_endpoint_descriptor *d)
{
	return d->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
}

static __always_inline __unused int
usb_endpoint_dir_in(const struct usb_endpoint_descriptor *d)
{
	return (d->bEndpointAddress & USB_ENDPOINT_DIR_MASK) != 0;
}

static __always_inline __unused int
usb_endpoint_dir_out(const struct usb_endpoint_descriptor *d)
{
	return !usb_endpoint_dir_in(d);
}

static __always_inline __unused int
usb_endpoint_xfer_bulk(const struct usb_endpoint_descriptor *d)
{
	return (d->bmAttributes & USB_ENDPOINT_XFER_INT_MASK) ==
	    USB_ENDPOINT_XFER_BULK;
}

static __always_inline __unused int
usb_endpoint_xfer_control(const struct usb_endpoint_descriptor *d)
{
	return (d->bmAttributes & USB_ENDPOINT_XFER_INT_MASK) ==
	    USB_ENDPOINT_XFER_CONTROL;
}

static __always_inline __unused int
usb_endpoint_xfer_int(const struct usb_endpoint_descriptor *d)
{
	return (d->bmAttributes & USB_ENDPOINT_XFER_INT_MASK) ==
	    USB_ENDPOINT_XFER_INT;
}

static __always_inline __unused int
usb_endpoint_xfer_isoc(const struct usb_endpoint_descriptor *d)
{
	return (d->bmAttributes & USB_ENDPOINT_XFER_INT_MASK) ==
	    USB_ENDPOINT_XFER_ISOC;
}

static __always_inline __unused int
usb_endpoint_is_bulk_in(const struct usb_endpoint_descriptor *d)
{
	return usb_endpoint_xfer_bulk(d) && usb_endpoint_dir_in(d);
}

static __always_inline __unused int
usb_endpoint_is_bulk_out(const struct usb_endpoint_descriptor *d)
{
	return usb_endpoint_xfer_bulk(d) && usb_endpoint_dir_out(d);
}

static __always_inline __unused int
usb_endpoint_is_int_in(const struct usb_endpoint_descriptor *d)
{
	return usb_endpoint_xfer_int(d) && usb_endpoint_dir_in(d);
}

static __always_inline __unused int
usb_endpoint_maxp(const struct usb_endpoint_descriptor *d)
{
	return d->wMaxPacketSize;
}

/* [S6] B7: the SS multipler (usb_intf.c:328 compile face); HS/SS only, and
 * this port runs high-speed, so the burst side is always 1 */
static __always_inline __unused int
usb_endpoint_maxp_mult(const struct usb_endpoint_descriptor *d)
{
	return 1 + ((le16_to_cpu(d->wMaxPacketSize) >> 11) & 0x3);
}

static __always_inline __unused int
usb_endpoint_maxp_packet_sz(const struct usb_endpoint_descriptor *d)
{
	return usb_endpoint_maxp(d) * usb_endpoint_maxp_mult(d);
}

/* the ch9 control-request line (usb_ops_linux.c embeds one per vendor
 * command; cherryusb spells it usb_setup_packet - no clash) */
struct usb_ctrlrequest {
	u8	bRequestType;
	u8	bRequest;
	u16	wValue;
	u16	wIndex;
	u16	wLength;
} __packed;

/* ------------------------------------------------------------------ */
/* urb                                                                 */
/* ------------------------------------------------------------------ */

struct urb;
typedef void (*usb_complete_t)(struct urb *);

struct urb {
	void			*hcpriv;	/* the usbh_urb behind it */
	struct usb_device	*dev;
	unsigned int		pipe;
	int			status;
	unsigned int		transfer_flags;
	void			*transfer_buffer;
	dma_addr_t		transfer_dma;
	u32			transfer_buffer_length;
	u32			actual_length;
	usb_complete_t		complete;
	void			*context;
	/* the control-request line (8-byte usb_ctrlrequest, gap §3.6-1/9:
	 * a control urb cannot be expressed without it); filled by
	 * usb_fill_control_urb, staged aligned at submit time */
	unsigned char		*setup_packet;
	/* int-urb polling interval (usb_fill_int_urb; endpoint-descriptor
	 * bInterval semantics, rtl8xxxu's int-in arms with it) */
	int			interval;
	/* per-urb error bookkeeping (iso/int accounting; 0 on the paths
	 * this port runs) */
	int			error_count;
	struct usb_host_endpoint *hep;	/* resolved endpoint */
	bool			submitted;
	/* the deferred-completion linkage (linuxkpi_usb.c) */
	void			*_defer_next;
	/* embedded backend urb (allocated with this struct) */
	void			*_usbh_urb;
	/* 64-byte-aligned DMA stage handed to the controller instead of
	 * the driver buffer (linuxkpi_usb.c; allocated at first submit,
	 * reused across resubmits, freed with the urb) */
	void			*_stage;
	void			*_stage_raw;
	u32			_stage_len;
	/* 64-byte-aligned stage for the control-setup line (§3.6-1: the
	 * xHCI port asserts urb->setup alignment; the driver's storage is
	 * arbitrary, so the kpi stages the 8 bytes like the data stage) */
	void			*_setup_stage;
	void			*_setup_raw;
	/* Linux urb refcount: alloc=1, submit takes the HC reference and
	 * the completion path releases it - rtw88 frees the urb right
	 * after an async submit and expects it to outlive the call */
	int			_refs;
	/* anchor membership (usb_anchor_urb; the anchor's list iterates
	 * these - spec §3.3 urb gap row / §3.6-4) */
	struct list_head	anchor_list;
	struct usb_anchor	*anchor;
	/* synchronous control paths wait here */
	struct completion	_done;
	bool			_sync;
	/* [W6] RX stall recovery: set by the KPI watchdog just before it
	 * kills a wedged bulk-IN TD, so the kill's completion reports
	 * -ETIME (a status the vendor re-arms on) instead of the fatal
	 * -ENODEV the generic kill mapping would produce */
	bool			_stall_recover;
};

struct urb *usb_alloc_urb(int iso_packets, gfp_t mem_flags);
void usb_free_urb(struct urb *urb);
static inline struct urb *usb_get_urb(struct urb *urb)
{
	if (urb != NULL)
		urb->_refs++;
	return urb;
}
#define usb_put_urb(urb)	usb_free_urb(urb)
int usb_submit_urb(struct urb *urb, gfp_t mem_flags);
void usb_kill_urb(struct urb *urb);
void usb_unlink_urb(struct urb *urb);

static __always_inline __unused void
usb_fill_bulk_urb(struct urb *urb, struct usb_device *dev, unsigned int pipe,
    void *transfer_buffer, int buffer_length, usb_complete_t complete_fn,
    void *context)
{
	/* Linux's fill resets the public fields only.  The linuxkpi-private
	 * linkage this struct carries (_usbh_urb, _stage, _refs, hep) MUST
	 * survive: rx urbs are filled once and resubmitted many times, and
	 * a wiped _usbh_urb hands the dispatcher a NULL urb (-EINVAL). */
	urb->status = 0;
	urb->transfer_flags = 0;
	urb->dev = dev;
	urb->pipe = pipe;
	urb->transfer_buffer = transfer_buffer;
	urb->transfer_buffer_length = (u32)buffer_length;
	urb->complete = complete_fn;
	urb->context = context;
}

static __always_inline __unused void
usb_fill_control_urb(struct urb *urb, struct usb_device *dev,
    unsigned int pipe, unsigned char *setup_packet, void *transfer_buffer,
    int buffer_length, usb_complete_t complete_fn, void *context)
{
	urb->status = 0;
	urb->transfer_flags = 0;
	urb->dev = dev;
	urb->pipe = pipe;
	urb->transfer_buffer = transfer_buffer;
	urb->transfer_buffer_length = (u32)buffer_length;
	urb->complete = complete_fn;
	urb->context = context;
	/* the setup line rides the urb now (§3.6-1): usb_submit_urb stages
	 * it 64-byte aligned for the HC; the caller keeps the storage
	 * addressable until completion, same contract as Linux */
	urb->setup_packet = setup_packet;
}

static __always_inline __unused void
usb_fill_int_urb(struct urb *urb, struct usb_device *dev, unsigned int pipe,
    void *transfer_buffer, int buffer_length, usb_complete_t complete_fn,
    void *context, int interval)
{
	usb_fill_bulk_urb(urb, dev, pipe, transfer_buffer, buffer_length,
	    complete_fn, context);
	/* [LNX] usb.h:1785-1792 truth (linuxkpi-hub spec §3.5-1): the
	 * caller passes the endpoint descriptor's bInterval encoding.
	 * HS/SS re-express it in microframes (2^(n-1) of 1/8 ms, clamped
	 * to 1..16); LS/FS keep the raw frame count.  Without this the
	 * two targets of the dual-target validation would carry
	 * human-divergent urb->interval values (microframes vs raw). */
	if (dev != NULL && (dev->speed == USB_SPEED_HIGH ||
	    dev->speed == USB_SPEED_SUPER ||
	    dev->speed == USB_SPEED_SUPER_PLUS)) {
		if (interval < 1)
			interval = 1;
		if (interval > 16)
			interval = 16;
		urb->interval = 1 << (interval - 1);
	} else {
		urb->interval = interval;
	}
}

/* the packet-size ceiling a bulk segmentation loop needs (§3.6-2):
 * shape-identical to [LNX] usb.h:2060 (static inline there - the Linux
 * compile of a consumer sees the kernel's own); our side resolves through
 * the claiming interface's descriptors, defined in linuxkpi_usb.c */
u16 usb_maxpacket(struct usb_device *udev, int pipe);

int usb_control_msg(struct usb_device *dev, unsigned int pipe, u8 request,
    u8 requesttype, u16 value, u16 index, void *data, u16 size, int timeout);
int usb_clear_halt(struct usb_device *dev, int pipe);
int usb_reset_device(struct usb_device *dev);

static __always_inline __unused void *
usb_alloc_coherent(struct usb_device *dev, size_t size, gfp_t mem_flags,
    dma_addr_t *dma)
{

	(void)dev;
	(void)dma;
	return kzalloc(size, mem_flags);
}

static __always_inline __unused void
usb_free_coherent(struct usb_device *dev, size_t size, void *addr,
    dma_addr_t dma)
{

	(void)dev;
	(void)dma;
	kfree(addr);
	(void)size;
}

/* ------------------------------------------------------------------ */
/* device/interface plumbing                                           */
/* ------------------------------------------------------------------ */

static __always_inline __unused void
usb_set_intfdata(struct usb_interface *intf, void *data)
{
	intf->privdata = data;
}

static __always_inline __unused void *
usb_get_intfdata(struct usb_interface *intf)
{
	return intf->privdata;
}

static __always_inline __unused struct usb_host_interface *
usb_altnum_to_altsetting(struct usb_interface *intf, unsigned int altnum)
{

	(void)altnum;
	return intf->cur_altsetting;
}

static __always_inline __unused struct usb_host_endpoint *
usb_pipe_endpoint(struct usb_device *dev, unsigned int pipe)
{
	(void)dev;
	(void)pipe;
	return NULL;	/* resolved inside linuxkpi_usb */
}

void usb_get_dev(struct usb_device *dev);
void usb_put_dev(struct usb_device *dev);

/* ------------------------------------------------------------------ */
/* driver registration                                                 */
/* ------------------------------------------------------------------ */

struct usb_device_id {
	u16	match_flags;
	u16	idVendor;
	u16	idProduct;
	u8	bInterfaceClass;
	u8	bInterfaceSubClass;
	u8	bInterfaceProtocol;
	u8	bInterfaceNumber;
	unsigned long	driver_info;
};

/* The match flags the drivers use. */
#define	USB_DEVICE_ID_MATCH_VENDOR		0x0001
#define	USB_DEVICE_ID_MATCH_PRODUCT		0x0002
#define	USB_DEVICE_ID_MATCH_DEVICE		(USB_DEVICE_ID_MATCH_VENDOR | USB_DEVICE_ID_MATCH_PRODUCT)
#define	USB_DEVICE_ID_MATCH_INT_CLASS		0x0080
#define	USB_DEVICE_ID_MATCH_INT_SUBCLASS	0x0100
#define	USB_DEVICE_ID_MATCH_INT_PROTOCOL	0x0200
#define	USB_DEVICE_ID_MATCH_INT_NUMBER		0x0400

#define	USB_DEVICE(vend, prod)					\
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE,		\
	.idVendor = (vend),					\
	.idProduct = (prod)

#define	USB_DEVICE_AND_INTERFACE_INFO(vend, prod, cl, sc, pr)	\
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE |		\
	    USB_DEVICE_ID_MATCH_INT_CLASS |			\
	    USB_DEVICE_ID_MATCH_INT_SUBCLASS |			\
	    USB_DEVICE_ID_MATCH_INT_PROTOCOL,			\
	.idVendor = (vend),					\
	.idProduct = (prod),					\
	.bInterfaceClass = (cl),				\
	.bInterfaceSubClass = (sc),				\
	.bInterfaceProtocol = (pr)

#define	USB_INTERFACE_INFO(cl, sc, pr)				\
	.match_flags = USB_DEVICE_ID_MATCH_INT_CLASS |		\
	    USB_DEVICE_ID_MATCH_INT_SUBCLASS |			\
	    USB_DEVICE_ID_MATCH_INT_PROTOCOL,			\
	.bInterfaceClass = (cl),				\
	.bInterfaceSubClass = (sc),				\
	.bInterfaceProtocol = (pr)

/* [S6] B7 PM stub family: CONFIG_AUTOSUSPEND is off, so most vendor call
 * sites vanish; these keep any survivor linking and semantics-neutral. */
typedef struct pm_message {
	int	event;
} pm_message_t;

struct usb_driver {
	const char		*name;
	int			(*probe)(struct usb_interface *,
				    const struct usb_device_id *);
	void			(*disconnect)(struct usb_interface *);
	const struct usb_device_id *id_table;
	bool			soft_unbind;
	bool			disable_hub_initiated_lpm;
	/* [S6] B7: registered by the vendor (rtl8821cu) but never called -
	 * this image has no PM and no shutdown path; `driver` carries the
	 * `.driver.shutdown` initializer site (the W0L-S8 flattened form). */
	void			(*suspend)(struct usb_interface *,
				    pm_message_t message);
	void			(*resume)(struct usb_interface *);
	void			(*reset_resume)(struct usb_interface *);
	struct device_driver	driver;
};

#define	usb_autopm_get_interface(intf)		((void)(intf), 0)
#define	usb_autopm_put_interface(intf)		do { (void)(intf); } while (0)
#define	usb_autopm_get_interface_async(intf)	((void)(intf), 0)
#define	usb_autopm_put_interface_async(intf)	do { (void)(intf); } while (0)
#define	usb_autopm_enable(intf)			do { (void)(intf); } while (0)
#define	usb_autopm_disable(intf)		do { (void)(intf); } while (0)
#define	usb_enable_autosuspend(udev)		do { (void)(udev); } while (0)
#define	usb_disable_autosuspend(udev)		do { (void)(udev); } while (0)
#define	usb_autoresume_device(udev)		((void)(udev), 0)
#define	usb_autosuspend_device(udev)		do { (void)(udev); } while (0)
#define	usb_mark_last_busy(udev)		do { (void)(udev); } while (0)

/* [S6] B7: coherent-allocation family mapping (same slot as the §3.5 row 9
 * face; the urb layer stages through its own 64B-aligned pools, so the
 * driver-side allocation is plain heap) */
#define	usb_buffer_alloc(udev, size, flags, dma)				\
	usb_alloc_coherent((udev), (size), (flags), (dma))
#define	usb_buffer_free(udev, size, addr, dma)					\
	usb_free_coherent((udev), (size), (addr), (dma))

int usb_register(struct usb_driver *driver);
void usb_deregister(struct usb_driver *driver);

/* called from the USB domain start: walks the .lkpi_module section */
void lkpi_usb_init(void);

/* the busid the class hook hands to probe diagnostics */
int lkpi_usb_debug_level(void);

#endif
