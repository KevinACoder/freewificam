/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan_driver_stub.c
 * @brief   A real usb_driver registered through the module-section mechanism, with
 *          the 8188eu identifier semantics the round's acceptance names.
 *
 * @design registration through the section, matching through the driver's own
 *         id_table. The driver object below is placed into `.lkpi_module` by the
 *         Linux module macro (the same macro the extracted driver uses at its
 *         bottom), so the suite exercises the production path end to end:
 *         lkpi_usb_init() walks the section and calls usb_register(), and the
 *         probe path then matches against this driver's id_table exactly the way
 *         it will match the real driver's. The identifiers are the real ones the
 *         acceptance criterion names - 0bda:8179 with the vendor-specific
 *         interface triple - and the negative rows are the ones that must NOT
 *         match (the mass-storage triple and the CD-ROM persona). A stub rather
 *         than the extracted driver because this suite proves the platform path
 *         (registration, matching, claiming, urb traffic); the driver's own
 *         bring-up is the later board-chain rounds' subject.
 *         Reference: the vendor-specific id_table row is
 *         third-party/rtl8xxxu/core.c:8022.
 * @date    08.10.2026
 */

#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"
#include "linuxkpi_usb.h"
#include <linux/usb.h>

#include "wlan_driver_stub.h"

static unsigned int probe_calls;
static unsigned int probe_matched_vendor;
static unsigned int disconnect_calls;

/*
 * The device the stub probes against is the suite's scripted device model: its
 * identifiers are test constants (0x1234:0x5678 with class 08/06/50), so the
 * stub's table carries an exact row for it plus the 8188eu rows whose matching
 * behaviour the criterion names. The 8188eu row itself is never hit by this
 * model - the suite checks its matching semantics by feeding the matcher the
 * identifier sets directly (see main.c), which is why this table's shape
 * matters more than its rows' order here.
 */
static const struct usb_device_id wlan_id_table[] = {
	/* the model device (a vendor-specific interface) */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1234, 0x5678, 0xff, 0xff, 0xff) },
	/* the real target: RTL8188EUS behind a vendor-specific interface */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0bda, 0x8179, 0xff, 0xff, 0xff) },
	{ }	/* terminator */
};

static int
wlan_stub_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = intf->udev;

	probe_calls++;
	/* the row that matched: this stub claims everything in its table, so a
	 * probe that runs at all is the platform path's own evidence */
	if (id->idVendor == 0x0bda && id->idProduct == 0x8179)
		probe_matched_vendor++;
	(void)udev;
	return 0;
}

static void
wlan_stub_disconnect(struct usb_interface *intf)
{

	(void)intf;
	disconnect_calls++;
}

static struct usb_driver wlan_stub_driver = {
	.name = "wlan-stub",
	.probe = wlan_stub_probe,
	.disconnect = wlan_stub_disconnect,
	.id_table = wlan_id_table,
	.no_dynamic_id = 1,
};

/*
 * The Linux module registration macro: the driver's address lands in the
 * `.lkpi_module` section, which lkpi_usb_init() walks. This is the same
 * mechanism the extracted driver's bottom uses, so a build that links this file
 * has a section entry by construction.
 */
module_usb_driver(wlan_stub_driver);

unsigned int
wlan_stub_probe_calls(void)
{

	return probe_calls;
}

unsigned int
wlan_stub_probe_8188_calls(void)
{

	return probe_matched_vendor;
}

unsigned int
wlan_stub_disconnect_calls(void)
{

	return disconnect_calls;
}

const struct usb_driver *
wlan_stub_driver_get(void)
{

	return &wlan_stub_driver;
}

const struct usb_device_id *
wlan_stub_id_table(void)
{

	return wlan_id_table;
}
