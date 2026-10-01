/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lkpi_udisk.h - the acceptance driver's shell face: the driver object the
 * shells register and the acceptance entries they trigger (SPEC
 * S6/linuxkpi R7 dual-shell structure).
 */

#ifndef LKPI_UDISK_H
#define LKPI_UDISK_H

#include <linux/usb.h>

/* the one driver object both shells share (defined in the core) */
extern struct usb_driver lkpi_udisk_driver;

/* acceptance entries (all take the interface the driver claimed) */
int lkpi_udisk_test(struct usb_interface *intf, u32 lba);
int lkpi_udisk_pattern(struct usb_interface *intf, u32 lba, u8 pattern);
int lkpi_udisk_bw(struct usb_interface *intf, u32 mib, bool write);
int lkpi_udisk_inq(struct usb_interface *intf);
int lkpi_udisk_cap(struct usb_interface *intf);

#endif /* LKPI_UDISK_H */
