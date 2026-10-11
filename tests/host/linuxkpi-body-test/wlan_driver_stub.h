/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wlan_driver_stub.h - the stub driver's observable face for the suite.
 */
#ifndef WLAN_DRIVER_STUB_H
#define WLAN_DRIVER_STUB_H

struct usb_driver;
struct usb_device_id;

unsigned int wlan_stub_probe_calls(void);
unsigned int wlan_stub_probe_8188_calls(void);
unsigned int wlan_stub_disconnect_calls(void);
const struct usb_driver *wlan_stub_driver_get(void);
const struct usb_device_id *wlan_stub_id_table(void);

#endif /* WLAN_DRIVER_STUB_H */
