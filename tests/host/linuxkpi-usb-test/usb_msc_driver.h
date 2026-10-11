/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * usb_msc_driver.h - the clean-room bulk-only-transport consumer driver.
 *
 * The driver the suite registers through usb_register(): a probe that matches
 * the toy device's identifiers and a small command helper that speaks the
 * bulk-only transport (command block out, optional data phase, status in) over
 * the engine's bulk and control faces.
 */

#ifndef USB_MSC_DRIVER_H
#define USB_MSC_DRIVER_H

#include <linux/usb.h>

extern struct usb_driver mock_msc_driver;

/* run one bulk-only-transport command; returns 0 on a PASS status block or
 * a negative errno.  data_in selects the data-phase direction and data_len
 * is the buffer size (0 = no data phase). */
int mock_msc_bot_cmd(struct usb_device *dev, const u8 *cdb, unsigned int cdb_len,
    int data_in, u8 *data, unsigned int data_len,
    unsigned int *actual_len);
/* how many status blocks echoed a tag this driver did not send */
unsigned int mock_msc_tag_mismatches(void);
/* the last status block the driver accepted */
const u8 *mock_msc_last_csw(void);
/* the number of commands this driver ran */
unsigned int mock_msc_command_count(void);
/* probe/disconnect invocation counts (the registration face) */
int mock_msc_probe_calls(void);
int mock_msc_disconnect_calls(void);
/* TUR / INQUIRY / READ CAPACITY convenience wrappers (the identification
 * face the smoke run asserts on) */
int mock_msc_tur(struct usb_device *dev);
int mock_msc_inquiry(struct usb_device *dev, u8 *buf, unsigned int len);
int mock_msc_read_capacity(struct usb_device *dev, u32 *blocks,
    u32 *block_size);

#endif /* USB_MSC_DRIVER_H */
