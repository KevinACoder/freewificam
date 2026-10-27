/*
 * @file   usb_config.h
 * @brief  Board configuration of the imported USB host stack for the RK3568
 *         board (the shadow header the imported headers include).
 *
 * The imported headers include "usb_config.h" themselves, so this file is
 * selected by include path alone (ports/cherryusb/board/rk3568-itx comes
 * first for the USB compile domain) and every build key lives here rather
 * than on a compiler command line - one place to read, one place to change.
 *
 * The values are the board's: they size the controllers' static pools, the
 * class instances and the hub thread, and they describe how this image makes
 * DMA coherent. Where a value is a hardware fact (cache line, controller
 * capabilities) the reason is stated next to it.
 *
 * @author zhugengyu
 * @date   07.10.2026
 * @design config header selection - one configuration header per target,
 *         resolved by include order rather than by -D flags: every imported
 *         header that needs a build key includes "usb_config.h" itself
 *         (core/usbh_core.h:14), so putting this directory first on the
 *         include path is what selects the board's values. Each key below is
 *         one the imported headers actually reference: the alignment switch
 *         in common/usb_dcache.h:8-12, the DMA-visible section attribute the
 *         controller drivers apply to their pools (port/ehci/usb_hc_ehci.c:
 *         31-40 is the shape), the pool bounds in port/ehci/usb_hc_ehci.h:
 *         21-32 and the xHCI port's ring/pool sizes, and the hub thread's
 *         stack and priority in class/hub/usbh_hub.c:738.
 */

#ifndef FREEWIFICAM_RK3568_USB_CONFIG_H
#define FREEWIFICAM_RK3568_USB_CONFIG_H

#include <stddef.h>

/* --- console -------------------------------------------------------------- */

/* The stack's own diagnostics go through the board glue's sink: one line
 * formatted and handed to the board log, so every USB line carries the
 * image's stamp and CR LF like the rest of the console output. It is a
 * function rather than a printf macro because the formatter is reached from
 * hub and class threads and from error paths. */
void usbh_console_printf(const char *fmt, ...);
#define CONFIG_USB_PRINTF(...)	usbh_console_printf(__VA_ARGS__)

/* Errors only. The stack's per-transfer chatter would flood a polled console
 * during enumeration; the glue reports device and class events itself. */
#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL	USB_DBG_ERROR
#endif

/* --- DMA coherency -------------------------------------------------------- */

/* The A55's data cache line is 64 bytes. The imported stack's contract is
 * that every DMA buffer is aligned to this value, otherwise one buffer's
 * maintenance invalidates a neighbour that shares its cache line. */
#ifndef CONFIG_USB_ALIGN_SIZE
#define CONFIG_USB_ALIGN_SIZE	64
#endif

/* The pools stay in cacheable memory: this image has no dedicated
 * non-cacheable section, and the per-buffer maintenance the stack does when
 * CONFIG_USB_DCACHE_ENABLE is on is the coherence discipline - the
 * configuration the same stack ran with on this board when the controller
 * path was proven there. Making the section real again is an experiment, not
 * a default. */
#define USB_NOCACHE_RAM_SECTION

/* The maintenance primitives are implemented by the board glue on top of the
 * arch cache API. They stay compiled in even though the non-cacheable window
 * makes them redundant for the pools: the descriptors and transfer buffers
 * move through both worlds, and the drivers' invalidation of data the device
 * wrote is what keeps the ordering explicit. The EHCI descriptor switch is
 * the one that is load-bearing in practice - with the pools reachable by the
 * CPU cache the async list stops advancing and no device descriptor can be
 * read. */
#ifndef CONFIG_USB_DCACHE_ENABLE
#define CONFIG_USB_DCACHE_ENABLE	1
#endif
#define CONFIG_USB_EHCI_DESC_DCACHE_ENABLE	1

/* --- host stack width ----------------------------------------------------- */

/* Four controllers are wired out: two USB2 roots (busid 0/1) and two USB3
 * instances (busid 2/3). The stack's per-bus arrays are sized by this, and
 * the busid layout itself is the board's (board/rk3568-itx/usb/usb_domain.h). */
#ifndef CONFIG_USBHOST_MAX_BUS
#define CONFIG_USBHOST_MAX_BUS	4
#endif

/* Multi-controller dispatch: one registered driver table per bus
 * (usbh_register_hc_driver), selected by busid. Without this key the
 * imported drivers link their entry points under their generic names and
 * only one controller per image would be addressable. */
#ifndef CONFIG_USBHOST_MULT_HC
#define CONFIG_USBHOST_MULT_HC	1
#endif

/* Roothub ports and hub nesting: each USB2 root leads to a four-port
 * on-board hub, and the panel ports are behind it, so one level of external
 * hub is required. The per-hub port budget covers the root ports and the
 * external hubs' children with room for a second hub level. */
#ifndef CONFIG_USBHOST_MAX_RHPORTS
#define CONFIG_USBHOST_MAX_RHPORTS	8
#endif
#ifndef CONFIG_USBHOST_MAX_EXTHUBS
#define CONFIG_USBHOST_MAX_EXTHUBS	4
#endif
#ifndef CONFIG_USBHOST_MAX_EHPORTS
#define CONFIG_USBHOST_MAX_EHPORTS	8
#endif

/* Device model: interfaces, alternate settings and endpoints per interface.
 * The alternate-setting bound matters: a configuration that lists more
 * alternate settings than this fails to parse and the device never
 * enumerates, silently, at the class-claim step. */
#ifndef CONFIG_USBHOST_MAX_INTERFACES
#define CONFIG_USBHOST_MAX_INTERFACES	8
#endif
#ifndef CONFIG_USBHOST_MAX_INTF_ALTSETTINGS
#define CONFIG_USBHOST_MAX_INTF_ALTSETTINGS	16
#endif
#ifndef CONFIG_USBHOST_MAX_ENDPOINTS
#define CONFIG_USBHOST_MAX_ENDPOINTS	8
#endif
#ifndef CONFIG_USBHOST_DEV_NAMELEN
#define CONFIG_USBHOST_DEV_NAMELEN	16
#endif

/* The endpoint-0 request buffer also bounds the configuration-descriptor
 * read. 2048 bytes admits every configuration this board has to enumerate
 * with margin; the default of 512 is not enough for a composite device whose
 * configuration carries interfaces and class-specific descriptors. */
#ifndef CONFIG_USBHOST_REQUEST_BUFFER_LEN
#define CONFIG_USBHOST_REQUEST_BUFFER_LEN	2048
#endif

#ifndef CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT	500
#endif

/* --- hub thread ----------------------------------------------------------- */

/* The hub thread is created by the stack once per bus; it sleeps on its
 * message queue between events and runs the enumeration chain (hub class
 * plus one level of external hub). 16 KiB is the stack this image gives it:
 * sized for that chain, and it is a build key precisely because the chain
 * grows with the class set.
 *
 * Priority 0 is the most urgent value the CMSIS-RTOS2 adapter maps, matching
 * the imported stack's own convention that a smaller number is more urgent.
 * The thread is event-driven, so the top band costs nothing while it waits. */
#ifndef CONFIG_USBHOST_PSC_STACKSIZE
#define CONFIG_USBHOST_PSC_STACKSIZE	16384
#endif
#ifndef CONFIG_USBHOST_PSC_PRIO
#define CONFIG_USBHOST_PSC_PRIO		0
#endif

/* Windows-descriptor requests are not used by this image's device set; the
 * vendor code is a non-zero byte when they are. */
#ifndef CONFIG_USBHOST_MSOS_VENDOR_CODE
#define CONFIG_USBHOST_MSOS_VENDOR_CODE	0x00
#endif

/* --- EHCI (the two USB2 host roots) -------------------------------------- */

/* The capability registers sit at the controller base: unlike PCI EHCI
 * implementations there is no vendor offset to skip. */
#ifndef CONFIG_USB_EHCI_HCCR_OFFSET
#define CONFIG_USB_EHCI_HCCR_OFFSET	0
#endif

/* Frame list size must be 1024 with this port: the driver maps the list size
 * to the controller's program bits from this value. */
#ifndef CONFIG_USB_EHCI_FRAME_LIST_SIZE
#define CONFIG_USB_EHCI_FRAME_LIST_SIZE	1024
#endif

/* Queue heads and queue element descriptors, per bus (the pools are
 * [bus][count]). Every concurrent transfer needs its own queue head, and
 * each control transfer needs at least three descriptors (setup, data,
 * status), so the descriptor pool is sized as three per queue head. */
#ifndef CONFIG_USB_EHCI_QH_NUM
#define CONFIG_USB_EHCI_QH_NUM		16
#endif
/* Six descriptors per queue head rather than three: the storage line's
 * largest single command is 1 MiB and an EHCI queue element covers at most
 * five 4 KiB pages (20 KiB), so one such transfer needs 52 queue elements.
 * Three per head (48) would refuse the 1 MiB arm at the pool, not at the
 * controller, and the refusal would look like a device timeout. */
#ifndef CONFIG_USB_EHCI_QTD_NUM
#define CONFIG_USB_EHCI_QTD_NUM		(CONFIG_USB_EHCI_QH_NUM * 6)
#endif

/* Standard EHCI 1.0 behaviour: software sets the configuration flag after
 * reset, which routes every root port to this controller instead of the
 * (unimplemented here) companion controllers. */
#define CONFIG_USB_EHCI_CONFIGFLAG	1

/* The isochronous engine is per image, not per board. The EHCI port compiles
 * its three engine calls under `#ifdef CONFIG_USB_EHCI_ISO`, so the key stays
 * **undefined** here: the camera image defines it and compiles the engine
 * (`tests/board/rtos/build/uvc.mk`), and every other image neither links the
 * engine nor pays for its per-bus state. A defined 0 would be wrong - `#ifdef`
 * would still compile the calls. */

/* Isochronous transaction descriptors, per bus. One descriptor covers eight
 * micro-frames (a whole USB frame at high speed); the stream engine lays a
 * frame's worth of transactions into one descriptor, so this bounds how many
 * USB frames a single isochronous URB may span. Five is the import's own
 * default; the board stream round retunes it from measured behaviour. */
#ifndef CONFIG_USB_EHCI_ITD_NUM
#define CONFIG_USB_EHCI_ITD_NUM	5
#endif

/* --- xHCI (the two USB3 instances) --------------------------------------- */

/* Ring depths, in transfer request blocks. 256 per ring is the value this
 * board's enumeration and mass-storage traffic was validated with; the
 * command and event rings are article-shared per bus, the transfer rings are
 * per endpoint. */
#ifndef CONFIG_USBHOST_XHCI_EVENT_RING_SIZE
#define CONFIG_USBHOST_XHCI_EVENT_RING_SIZE	256
#endif
#ifndef CONFIG_USBHOST_XHCI_COMMAND_RING_SIZE
#define CONFIG_USBHOST_XHCI_COMMAND_RING_SIZE	256
#endif
#ifndef CONFIG_USBHOST_XHCI_TRANSFER_RING_SIZE
#define CONFIG_USBHOST_XHCI_TRANSFER_RING_SIZE	256
#endif

/* Static pool bounds, per bus (no defaults exist upstream - the driver
 * references these directly, so every one has to be defined here):
 *  - MAX_SLOTS caps the device slot/context tables. The deepest tree this
 *    board carries is a root port, one external hub and the device behind
 *    it; the controller offers more slots than the tree needs and enabling
 *    what is never used only spends RAM.
 *  - RINGS is the transfer-ring pool: endpoint 0 plus one ring per endpoint
 *    of every connected device.
 *  - MAX_SPBUF bounds the scratchpad buffers. The controller reports how
 *    many it wants, and init fails loudly if that is more than this. */
#ifndef CONFIG_USBHOST_XHCI_MAX_SLOTS
#define CONFIG_USBHOST_XHCI_MAX_SLOTS	8
#endif
#ifndef CONFIG_USBHOST_XHCI_RINGS
#define CONFIG_USBHOST_XHCI_RINGS	12
#endif
#ifndef CONFIG_USBHOST_XHCI_MAX_SPBUF
#define CONFIG_USBHOST_XHCI_MAX_SPBUF	2
#endif

/* --- class drivers -------------------------------------------------------- */

/* Class set of this image: hub (root and external hubs) and mass storage.
 * The keys below belong to the storage class: the per-command synchronisation
 * timeout, the pool of class instances (one per attached disk), and how often
 * a unit is polled for ready before a command is declared failed. */
#ifndef CONFIG_USBHOST_MSC_TIMEOUT
#define CONFIG_USBHOST_MSC_TIMEOUT	5000
#endif
#ifndef CONFIG_USBHOST_MAX_MSC_CLASS
#define CONFIG_USBHOST_MAX_MSC_CLASS	2
#endif
#ifndef CONFIG_USBHOST_MSC_READY_CHECK_TIMES
#define CONFIG_USBHOST_MSC_READY_CHECK_TIMES	10
#endif

/* Video class (UVC camera): one class instance per attached camera. The
 * format/frame tables inside it keep the import's defaults (12 frames,
 * 3 formats) - the board's camera reports 7 frame descriptors over 2
 * formats, so the defaults hold. Only the camera image compiles this
 * class. */
#ifndef CONFIG_USBHOST_MAX_VIDEO_CLASS
#define CONFIG_USBHOST_MAX_VIDEO_CLASS	1
#endif

/* The UVC stream engine (class/video/usbh_uvc_stream.c): the isochronous urbs
 * in flight, the micro-frames one urb spans (one USB frame at the streaming
 * endpoints' bInterval of one), and the pump thread's stack and priority.
 * The ring tiles the frame list, so four urbs of one frame each cover the
 * controller's four isochronous slots; the pump is event-driven and sits just
 * below the stack's own threads. */
#ifndef CONFIG_UVC_STREAM_URBS
#define CONFIG_UVC_STREAM_URBS	4
#endif
#ifndef CONFIG_UVC_STREAM_PACKETS
#define CONFIG_UVC_STREAM_PACKETS	8
#endif
#ifndef CONFIG_UVC_STREAM_STACKSIZE
#define CONFIG_UVC_STREAM_STACKSIZE	4096
#endif
#ifndef CONFIG_UVC_STREAM_PRIO
#define CONFIG_UVC_STREAM_PRIO	1
#endif

#endif /* FREEWIFICAM_RK3568_USB_CONFIG_H */