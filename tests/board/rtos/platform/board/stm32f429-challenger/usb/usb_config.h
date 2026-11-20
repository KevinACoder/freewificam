/*
 * @file   usb_config.h
 * @brief  Board configuration of the imported CherryUSB host stack for the
 *         STM32F429 挑战者 V2 (OTG_HS in full-speed host mode).
 *
 * The imported headers include "usb_config.h" themselves, so this file is
 * selected by include path alone (the F429 USB board directory is first in
 * USB_CFLAGS).  Every build key lives here; no compiler -D carries one.
 *
 * Hardware facts (V2 base schematic, sheet 8):
 *   - the USB HOST socket is P2 (USB-A), its VBUS is tied to 5V;
 *   - HOST_DN/HOST_DP are PB14/PB15, selected by J74-J76 in the HOS position;
 *   - the OTG_HS core is used in FS mode, 48 MHz from PLLQ (clock.c).
 *
 * This is a single-controller image: CONFIG_USBHOST_MULT_HC stays undefined,
 * so the DWC2 port supplies usb_hc_init()/usbh_submit_urb()/USBH_IRQHandler()
 * directly and the core's per-bus dispatch branch is not compiled.
 */

#ifndef FREEWIFICAM_STM32F429_USB_CONFIG_H
#define FREEWIFICAM_STM32F429_USB_CONFIG_H

#include <stddef.h>

/* --- console -------------------------------------------------------------- */

/* The stack's diagnostics go through the board glue's sink, which formats one
 * piece and hands it to the image log.  The sink does not add a newline: the
 * USB formatter writes a header fragment and then the message, and the log
 * sink assembles them into one stamped line. */
void usbh_console_printf(const char *fmt, ...);
#define CONFIG_USB_PRINTF(...)	usbh_console_printf(__VA_ARGS__)

/* Info level during bring-up: the DWC2 GHWCFG lines and the enumeration
 * milestones are the C2 evidence.  No colour escapes on the polled console. */
#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL	USB_DBG_INFO
#endif

/* --- DMA / alignment ------------------------------------------------------ */

/* Cortex-M4 has no data cache, so the stack's cache-maintenance contract is
 * compiled out (CONFIG_USB_DCACHE_ENABLE stays undefined) and USB_NOCACHE_-
 * RAM_SECTION is empty.  DWC2 host DMA still requires 4-byte aligned buffers;
 * the linker places .bss in SRAM at 0x20000000, never in CCM. */
#define CONFIG_USB_ALIGN_SIZE	4
#define USB_NOCACHE_RAM_SECTION

/* --- host stack width ----------------------------------------------------- */

#ifndef CONFIG_USBHOST_MAX_BUS
#define CONFIG_USBHOST_MAX_BUS	1
#endif

/* One root port, one optional external hub level, four hub ports: the U disk
 * sits directly on P2, but the hub class's tables are sized for one level so
 * a self-powered hub can be inserted without relinking. */
#ifndef CONFIG_USBHOST_MAX_RHPORTS
#define CONFIG_USBHOST_MAX_RHPORTS	1
#endif
#ifndef CONFIG_USBHOST_MAX_EXTHUBS
#define CONFIG_USBHOST_MAX_EXTHUBS	1
#endif
#ifndef CONFIG_USBHOST_MAX_EHPORTS
#define CONFIG_USBHOST_MAX_EHPORTS	4
#endif

/* Device model: a mass-storage disk has one interface and two bulk endpoints;
 * four interfaces leave room for a composite stick without spending much RAM.
 *
 * Alt settings: 16.  The bench's Realtek dongles disagree on this - the
 * 8188EUS presents one altsetting, while the 8723BU, the 8188CUS and the
 * 8851BU each present a config with bAlternateSetting 2 in interface 0, and
 * the parser's `cur_alt_setting >= MAX` check then fails the whole config
 * ("Interface altsetting num 2 overflow", 2026-11-15 four-dongle boot).  A
 * UVC camera presents one alternate setting per bandwidth rung: the old
 * line's Pro 9000 reported 12 for its streaming interface, so 8 is not enough.
 * The cost is a static array per interface
 * (CONFIG_USBHOST_MAX_INTERFACES x 16 altsettings), not per device. */
#ifndef CONFIG_USBHOST_MAX_INTERFACES
#define CONFIG_USBHOST_MAX_INTERFACES	4
#endif
#ifndef CONFIG_USBHOST_MAX_INTF_ALTSETTINGS
#define CONFIG_USBHOST_MAX_INTF_ALTSETTINGS	16
#endif
#ifndef CONFIG_USBHOST_MAX_ENDPOINTS
/* 8, matching the xHCI reference port: the bench's Realtek dongles present
 * configs with 6 (8723BU) and 8 (8851BU) endpoints in interface 0; the 4 the
 * MSC image needed failed them with "Endpoint num N overflow" (2026-11-15
 * four-dongle boot).  Static cost: MAX_INTERFACES x MAX_INTF_ALTSETTINGS x 8
 * endpoint structs per hubport. */
#define CONFIG_USBHOST_MAX_ENDPOINTS	8
#endif
#ifndef CONFIG_USBHOST_DEV_NAMELEN
#define CONFIG_USBHOST_DEV_NAMELEN	16
#endif

/* 2 KiB: a U disk's configuration descriptor fits in 1 KiB, but a UVC
 * camera carries a video-control interface plus a dozen streaming alternate
 * settings and their format/frame descriptors in the same configuration. */
#ifndef CONFIG_USBHOST_REQUEST_BUFFER_LEN
#define CONFIG_USBHOST_REQUEST_BUFFER_LEN	2048
#endif
#ifndef CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT	5000
#endif

/* --- hub thread ----------------------------------------------------------- */

/* The hub thread runs the enumeration chain and the MSC connect hook's SCSI
 * identity handshake; 8 KiB is the measured-safe end of the range that still
 * fits the 64 KiB FreeRTOS heap.  Priority 1 is one band below the top (the
 * OSAL maps 0 to osPriorityISR); it blocks on the hub queue between events. */
#ifndef CONFIG_USBHOST_PSC_STACKSIZE
#define CONFIG_USBHOST_PSC_STACKSIZE	8192
#endif
#ifndef CONFIG_USBHOST_PSC_PRIO
#define CONFIG_USBHOST_PSC_PRIO		1
#endif

/* --- class drivers -------------------------------------------------------- */

/* One MSC instance (one disk), five ready polls with a 5 s per-transfer
 * timeout: a stick that is not ready within 25 s fails with a named line
 * instead of hanging the runner. */
#ifndef CONFIG_USBHOST_MSC_TIMEOUT
#define CONFIG_USBHOST_MSC_TIMEOUT	5000
#endif
#ifndef CONFIG_USBHOST_MAX_MSC_CLASS
#define CONFIG_USBHOST_MAX_MSC_CLASS	1
#endif
#ifndef CONFIG_USBHOST_MSC_READY_CHECK_TIMES
#define CONFIG_USBHOST_MSC_READY_CHECK_TIMES	5
#endif

#ifndef CONFIG_USBHOST_MSOS_VENDOR_CODE
#define CONFIG_USBHOST_MSOS_VENDOR_CODE	0x00
#endif

/* --- video class (UVC camera) --------------------------------------------- */

/* One class instance per camera; only the UVC images compile class/video. */
#ifndef CONFIG_USBHOST_MAX_VIDEO_CLASS
#define CONFIG_USBHOST_MAX_VIDEO_CLASS	1
#endif

/* The stream engine's ring.  The F429 DWC2 port arms one packet per USB frame
 * and completes a URB after its whole packet array; 64 packets = 64 ms of
 * full-speed bus time, which keeps the pump's one-URB-at-a-time resubmit gap
 * small relative to a 5-15 fps MJPEG frame.  CONFIG_UVC_STREAM_URBS=1 is the
 * DWC2 ISO engine's one-channel-per-endpoint contract, not a tuning default. */
#ifndef CONFIG_UVC_STREAM_URBS
#define CONFIG_UVC_STREAM_URBS	1
#endif
#ifndef CONFIG_UVC_STREAM_PACKETS
#define CONFIG_UVC_STREAM_PACKETS	64
#endif
#ifndef CONFIG_UVC_STREAM_STACKSIZE
#define CONFIG_UVC_STREAM_STACKSIZE	4096
#endif
#ifndef CONFIG_UVC_STREAM_PRIO
#define CONFIG_UVC_STREAM_PRIO	1
#endif

/* --- audio class (UAC1 microphone) ---------------------------------------- */

/* One class instance per camera microphone; only the UAC images compile
 * class/audio.  The camera's AudioControl and AudioStreaming interfaces are
 * claimed by the same instance. */
#ifndef CONFIG_USBHOST_MAX_AUDIO_CLASS
#define CONFIG_USBHOST_MAX_AUDIO_CLASS	1
#endif

/* The capture ring and its resubmit cadence.  One urb spans 16 one-millisecond
 * packets: the DWC2 engine completes it at the last packet and the completion
 * callback arms the next one inside that frame, so no task scheduling sits
 * between two urbs.  One in-flight urb is the DWC2 engine's contract; a second
 * one for the same endpoint would be refused with -USB_ERR_BUSY. */
#ifndef CONFIG_UAC_STREAM_PACKETS
#define CONFIG_UAC_STREAM_PACKETS	16
#endif
#ifndef CONFIG_UAC_RING_MS
#define CONFIG_UAC_RING_MS	200
#endif

#endif /* FREEWIFICAM_STM32F429_USB_CONFIG_H */
