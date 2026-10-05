/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lkpi_hub.h - the hub status-change acceptance driver's shell face: the
 * driver object the shells register and the capture/stimulus entries they
 * trigger (SPEC specs/linuxkpi-hub R2 dual-shell structure).
 */

#ifndef LKPI_HUB_H
#define LKPI_HUB_H

#include <linux/usb.h>

/* the one driver object both shells share (defined in the core) */
extern struct usb_driver lkpi_hub_driver;

/* arm the status-change int-URB (fill_int + submit; the Linux shell's
 * post-probe hook calls this, the RTOS capture protocol drives it) */
int lkpi_hub_arm(struct usb_interface *intf);

/* the deterministic stimulus script (spec v1.1 §3.2-1-⑤): SetFeature
 * (PORT_RESET) on the given downstream port, ~500 ms apart */
int lkpi_hub_stim(struct usb_interface *intf, u32 port, u32 rounds);

/* the RTOS capture protocol (spec §3.3-2): one sacrifice stimulus opens
 * the window, the arm retries through -USB_ERR_BUSY, then the counting
 * stimuli run; ends with the stats line */
int lkpi_hub_capture(struct usb_interface *intf, u32 port, u32 rounds);

/* retire the int-URB (kill; the completion may still arrive from the
 * drain thread, so the caller must not free driver memory after) */
void lkpi_hub_stop(struct usb_interface *intf);

/* the event counter (the `count` command readout) */
u32 lkpi_hub_count(struct usb_interface *intf);

#endif /* LKPI_HUB_H */
