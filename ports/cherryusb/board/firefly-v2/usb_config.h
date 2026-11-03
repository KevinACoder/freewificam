/*
 * @file   usb_config.h
 * @brief  Board configuration of the imported USB host stack for the
 *         firefly v2 board (the shadow header the imported headers include).
 *
 * Same selection discipline as the RK3568 board: the imported headers
 * include "usb_config.h" themselves, so this file is chosen by putting
 * ports/cherryusb/board/firefly-v2 first on the USB compile domain's
 * include path, and every build key lives here rather than on a compiler
 * command line.  The values mirror the RK3568 board's wherever the two
 * boards agree (DMA coherency discipline, device-model widths, class
 * pools); the differences are the board's own facts and are called out
 * where they occur.
 */

#ifndef FREEWIFICAM_FIREFLY_V2_USB_CONFIG_H
#define FREEWIFICAM_FIREFLY_V2_USB_CONFIG_H

#include <stddef.h>

/* --- console -------------------------------------------------------------- */

/* The stack's own diagnostics go through the board glue's sink (the same
 * one-line-to-board-log contract as the RK3568 board). */
void usbh_console_printf(const char *fmt, ...);
#define CONFIG_USB_PRINTF(...)	usbh_console_printf(__VA_ARGS__)

/* Errors only: the stack's per-transfer chatter would flood a polled
 * console during enumeration. */
#ifndef CONFIG_USB_DBG_LEVEL
/* Diagnostic arm (W7-02): INFO during the join-phase evidence round - the
 * vendor stack's associate/handshake stage lines are INFO. Revert to
 * USB_DBG_ERROR when the round closes. */
#define CONFIG_USB_DBG_LEVEL	USB_DBG_INFO
#endif

/* --- DMA coherency -------------------------------------------------------- */

/* The Cortex-A55 data cache line is 64 bytes - the same alignment contract
 * as the RK3568 board. */
#ifndef CONFIG_USB_ALIGN_SIZE
#define CONFIG_USB_ALIGN_SIZE	64
#endif

/* Pools stay in cacheable memory with per-buffer maintenance, exactly the
 * discipline the RK3568 images run with; making the non-cacheable window
 * real is an experiment, not a default. */
#define USB_NOCACHE_RAM_SECTION

#ifndef CONFIG_USB_DCACHE_ENABLE
#define CONFIG_USB_DCACHE_ENABLE	1
#endif

/* --- host stack width ----------------------------------------------------- */

/* Two controllers are wired out, both xHCI. */
#ifndef CONFIG_USBHOST_MAX_BUS
#define CONFIG_USBHOST_MAX_BUS	2
#endif

/* Multi-controller dispatch, same contract as the RK3568 board. */
#ifndef CONFIG_USBHOST_MULT_HC
#define CONFIG_USBHOST_MULT_HC	1
#endif

/* Roothub ports and hub nesting: each xHCI root exposes the controller's
 * own port count (HCSPARAMS1, read by the port itself); the budget admits
 * both roots plus one external hub level for bench cabling. */
#ifndef CONFIG_USBHOST_MAX_RHPORTS
#define CONFIG_USBHOST_MAX_RHPORTS	8
#endif
#ifndef CONFIG_USBHOST_MAX_EXTHUBS
#define CONFIG_USBHOST_MAX_EXTHUBS	4
#endif
#ifndef CONFIG_USBHOST_MAX_EHPORTS
#define CONFIG_USBHOST_MAX_EHPORTS	8
#endif

/* Device model widths: the AIC8800 runtime device is a three-interface
 * composite (Bluetooth x2 + vendor Wi-Fi), so the interface bound is the
 * reference's own. */
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

/* The ZeroCD mass-storage device the AIC8800 dongle ships as carries a
 * small configuration; the AIC8800 runtime composite carries three
 * interfaces. 2048 admits both with margin. */
#ifndef CONFIG_USBHOST_REQUEST_BUFFER_LEN
#define CONFIG_USBHOST_REQUEST_BUFFER_LEN	2048
#endif

#ifndef CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT	500
#endif

/* --- hub thread ----------------------------------------------------------- */

#ifndef CONFIG_USBHOST_PSC_STACKSIZE
#define CONFIG_USBHOST_PSC_STACKSIZE	16384
#endif
#ifndef CONFIG_USBHOST_PSC_PRIO
#define CONFIG_USBHOST_PSC_PRIO		0
#endif

#ifndef CONFIG_USBHOST_MSOS_VENDOR_CODE
#define CONFIG_USBHOST_MSOS_VENDOR_CODE	0x00
#endif

/* --- xHCI (the two USB3 instances) --------------------------------------- */

/* Ring depths: the values the RK3568 board validated the same port with. */
#ifndef CONFIG_USBHOST_XHCI_EVENT_RING_SIZE
#define CONFIG_USBHOST_XHCI_EVENT_RING_SIZE	256
#endif
#ifndef CONFIG_USBHOST_XHCI_COMMAND_RING_SIZE
#define CONFIG_USBHOST_XHCI_COMMAND_RING_SIZE	256
#endif
#ifndef CONFIG_USBHOST_XHCI_TRANSFER_RING_SIZE
#define CONFIG_USBHOST_XHCI_TRANSFER_RING_SIZE	256
#endif

/* Static pool bounds, per bus (no defaults exist upstream):
 *  - MAX_SLOTS: the dongle sits on a root port; eight is the same headroom
 *    the RK3568 board gives its deeper trees;
 *  - RINGS: endpoint 0 plus every endpoint of the connected device(s);
 *  - MAX_SPBUF: init fails loudly if the controller wants more than this. */
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

/* This image's class set: hub, mass storage (the ZeroCD modeswitch rides
 * the MSC connect path) and the AIC8800 vendor Wi-Fi class. */
#ifndef CONFIG_USBHOST_MSC_TIMEOUT
#define CONFIG_USBHOST_MSC_TIMEOUT	5000
#endif
#ifndef CONFIG_USBHOST_MAX_MSC_CLASS
#define CONFIG_USBHOST_MAX_MSC_CLASS	2
#endif
#ifndef CONFIG_USBHOST_MSC_READY_CHECK_TIMES
#define CONFIG_USBHOST_MSC_READY_CHECK_TIMES	10
#endif

/* Register the AIC8800 1111:1111 ZeroCD in usbh_aic8800 (the modeswitch
 * table the class installs when the case asks for it). */
#define CONFIG_USBHOST_AIC8800_ZEROCD	1

/* The ZeroCD recipe exactly as the PR's own bring-up doc states it for the
 * AIC8800D80 stick (docs/en/demo/usbh_wifi.rst): the modeswitch CBW has no
 * CSW, the reconnect pulse is short enough that the hub port walk must be
 * forced, and the reference board required a 350 ms delay between the
 * switch and the re-enumeration. Starting from the validated combination;
 * dropping pieces is a later experiment, not a default. */
#define CONFIG_USBHOST_MSC_MODESWITCH_NO_CSW
#define CONFIG_USBHOST_HUB_FORCE_REENUMERATE
#define CONFIG_USBHOST_MSC_MODESWITCH_FORCE_REENUMERATE
#ifndef CONFIG_USBHOST_MSC_MODESWITCH_DELAY_MS
#define CONFIG_USBHOST_MSC_MODESWITCH_DELAY_MS	350
#endif

#endif /* FREEWIFICAM_FIREFLY_V2_USB_CONFIG_H */
