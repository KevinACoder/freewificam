/*
 * @file   usb_config.h
 * @brief  CherryUSB configuration (shadow header), resolved ahead of the
 *         vendored tree by the adapter include order.
 *
 * Values are the board-verified set from the net_80211 line (os/freertos,
 * four-port USB host, 2026-09-19 runs), re-cut for this image: two EHCI
 * buses, hub class only, no xHCI yet. The two dcache switches are
 * load-bearing: this image has no .noncacheable MMU window, so the EHCI
 * descriptor pools sit in cacheable .bss and every descriptor access needs
 * explicit maintenance - without them the async list silently stops
 * advancing (measured as iaad timeout / "get device descriptor -14").
 *
 * The multi-HC switch is upstream's own (usbh_register_hc_driver +
 * struct usbh_hc_driver, upstream dev 8b7e58c6) - this board needs two
 * EHCI instances in one image, and the old line's local CONFIG_USBHOST_MULTI_HCD
 * patch is deliberately NOT carried: upstream grew the same capability.
 *
 * @date   27.09.2026
 * @author zhugengyu
 */

#ifndef FREEWEBCAMERA_USB_CONFIG_H
#define FREEWEBCAMERA_USB_CONFIG_H

#include <stddef.h>

/* Adapter-provided console formatter (vsnprintf + the stamped console
 * sink). Kept as a function, not a macro expansion of printf: the log
 * macros are reached from the hub threads and from ISR paths. */
void usbh_console_printf(const char *fmt, ...);
#define CONFIG_USB_PRINTF(...)		usbh_console_printf(__VA_ARGS__)

/* Error-level only: enumeration reporting is the adapter's event
 * handler's job, and the console drops bytes under load. */
#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL		USB_DBG_ERROR
#endif

/* Cache line is 64B on the A55. CherryUSB contract: every DMA buffer is
 * aligned to this (USB_MEM_ALIGNX / USB_ALIGN_UP), otherwise one buffer's
 * boundary shares a cache line with a neighbour and the invalidate side of
 * the discipline clobbers it. */
#ifndef CONFIG_USB_ALIGN_SIZE
#define CONFIG_USB_ALIGN_SIZE		64
#endif

/* No dedicated non-cacheable section exists in this image: the pools stay
 * in cacheable .bss and CONFIG_USB_EHCI_DESC_DCACHE_ENABLE below does the
 * per-descriptor maintenance instead. */
#define USB_NOCACHE_RAM_SECTION

/* --- USB host stack ------------------------------------------------------- */

#define CONFIG_USBHOST_MAX_RHPORTS	8
#define CONFIG_USBHOST_MAX_EXTHUBS	4
#define CONFIG_USBHOST_MAX_EHPORTS	8
#define CONFIG_USBHOST_MAX_INTERFACES	8
/* The AIC8800D80's configuration carries more than the 8 alternate
 * settings the default admits; with 8 its parse fails and the device never
 * enumerates (measured 2026-09-19: "Interface altsetting num 8 overflow",
 * port dead). */
#define CONFIG_USBHOST_MAX_INTF_ALTSETTINGS	16
#define CONFIG_USBHOST_MAX_ENDPOINTS	8
#define CONFIG_USBHOST_DEV_NAMELEN	16

/* PSC (hub) thread: the ThreadX osal passes the priority through raw and
 * ThreadX numbers 0 as most urgent. It sleeps on its message queue between
 * events, so the top priority costs nothing - the net_80211 line ran the
 * same value. */
#ifndef CONFIG_USBHOST_PSC_PRIO
#define CONFIG_USBHOST_PSC_PRIO		0
#endif
#ifndef CONFIG_USBHOST_PSC_STACKSIZE
/* 16 KiB: enough for the hub-thread enumeration chain this image runs
 * (hub class + one level of external hub + MSC later).  The prior repo
 * needed 64 KiB because the hub thread also ran the urtwn attach chain
 * (firmware load + ieee80211_ifattach); that chain does not exist here
 * (SPEC S5 §3.3: downsize from the carried 64 KiB, grow on evidence). */
#define CONFIG_USBHOST_PSC_STACKSIZE	16384
#endif

#ifndef CONFIG_USBHOST_MSOS_VENDOR_CODE
#define CONFIG_USBHOST_MSOS_VENDOR_CODE	0x00
#endif

/* ep0 buffer also bounds the config-descriptor read: the AIC8800D80's
 * configuration is 1433 bytes, well past the 512 default. */
#ifndef CONFIG_USBHOST_REQUEST_BUFFER_LEN
#define CONFIG_USBHOST_REQUEST_BUFFER_LEN	2048
#endif

#ifndef CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT	500
#endif

/* Two EHCI buses link today; the arrays are sized for the full four-port
 * layout (xHCI instances join on their own feat line). */
#ifndef CONFIG_USBHOST_MAX_BUS
#define CONFIG_USBHOST_MAX_BUS		4
#endif

/* Upstream's multi-host-controller registration (usbh_register_hc_driver):
 * busid 0/1 = the two EHCI roots. */
#ifndef CONFIG_USBHOST_MULT_HC
#define CONFIG_USBHOST_MULT_HC		1
#endif

/* --- EHCI (rk3568 usb2host0/1) --------------------------------------------- */

/* Capability registers are at the controller base (no vendor offset). */
#ifndef CONFIG_USB_EHCI_HCCR_OFFSET
#define CONFIG_USB_EHCI_HCCR_OFFSET	0
#endif

#ifndef CONFIG_USB_EHCI_FRAME_LIST_SIZE
#define CONFIG_USB_EHCI_FRAME_LIST_SIZE	1024
#endif

#ifndef CONFIG_USB_EHCI_QH_NUM
#define CONFIG_USB_EHCI_QH_NUM		16
#endif

/* Every concurrent QH needs at least 3 qTDs (setup+data+status). */
#ifndef CONFIG_USB_EHCI_QTD_NUM
#define CONFIG_USB_EHCI_QTD_NUM		(CONFIG_USB_EHCI_QH_NUM * 3)
#endif

/* Standard EHCI 1.0: software sets CONFIGFLAG after reset to take the root
 * ports from the (absent) companion controllers. */
#define CONFIG_USB_EHCI_CONFIGFLAG	1

/* The dcache pair - see the file comment. ALIGN 64 pulls the descriptor
 * maintenance up to cache-line granularity inside the driver. */
#define CONFIG_USB_DCACHE_ENABLE	1
#define CONFIG_USB_EHCI_DESC_DCACHE_ENABLE	1

/* --- xHCI (compiled in by the feat/cherryusb_xhci line) -------------------- */

#ifndef CONFIG_USBHOST_XHCI_EVENT_RING_SIZE
#define CONFIG_USBHOST_XHCI_EVENT_RING_SIZE	256
#endif
#ifndef CONFIG_USBHOST_XHCI_COMMAND_RING_SIZE
#define CONFIG_USBHOST_XHCI_COMMAND_RING_SIZE	256
#endif
#ifndef CONFIG_USBHOST_XHCI_TRANSFER_RING_SIZE
#define CONFIG_USBHOST_XHCI_TRANSFER_RING_SIZE	256
#endif

/* Static pool sizing (the EHCI pools' shape: everything the DMA engine can
 * reach sits in cacheable .bss with explicit maintenance, bounds fixed at
 * compile time).  The compile-in switch itself is the generated config.h's
 * CONFIG_USBHOST_XHCI (a build key, Makefile-driven - not defined here, the
 * two headers would fight over it):
 *  - MAX_SLOTS caps the DCBAA/device-context tables (the tree behind this
 *    board's root ports is two devices deep: the panel hub + one dongle;
 *    HW reports more but enabling what we do not use only spends RAM);
 *  - RINGS is the per-bus transfer-ring pool (ep0 + one ring per endpoint
 *    of every connected device; the panel hub and the wlan dongle fit in
 *    single digits);
 *  - MAX_SPBUF is the scratchpad-buffer bound (this DWC3 reports 1,
 *    measured 2026-09-20 hcs2 spbuf=1). */
#ifndef CONFIG_USBHOST_XHCI_MAX_SLOTS
#define CONFIG_USBHOST_XHCI_MAX_SLOTS		8
#endif
#ifndef CONFIG_USBHOST_XHCI_RINGS
#define CONFIG_USBHOST_XHCI_RINGS		12
#endif
#ifndef CONFIG_USBHOST_XHCI_MAX_SPBUF
#define CONFIG_USBHOST_XHCI_MAX_SPBUF		2
#endif

#endif /* FREEWEBCAMERA_USB_CONFIG_H */
