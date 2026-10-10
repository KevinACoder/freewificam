/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    lkpi_usbh_modeswitch.h
 * @brief   The CD-ROM-persona flip for the RTL8821CU-class dongles: the class
 *          hook's body and the id table the hook matches on.
 *
 * @design class hook + message table. The hook is the third consumer of the
 *         "one claim hook per class" assembly point in lkpi_usbh_class.c; this
 *         unit owns the eject itself. The device powers up as a mass-storage
 *         CD-ROM and only exposes its network interface after a SCSI
 *         START/STOP UNIT (LoEj) command, so the hook sends that command and
 *         never claims the device - the flip re-enumerates it under the
 *         personality the wireless hook matches. The CBW/CSW exchange follows
 *         the vendored host stack's own mass-storage class
 *         (third-party/cherryusb/class/msc/usbh_msc.c) and this tree's rawbulk
 *         diagnostic (components/linuxkpi/usb/usbkpi_cmds.c).
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_USBH_MODESWITCH_H_
#define _LINUXKPI_USBH_MODESWITCH_H_

#include <stdint.h>

struct usbh_hubport;

/* The persona this hook owns: the dongle's CD-ROM mode. */
#define	LKPI_MODESWITCH_VID		0x0bda
#define	LKPI_MODESWITCH_PID		0x1a2b

/* The personality the flip lands on (the one the wireless hook matches). */
#define	LKPI_MODESWITCH_TARGET_PID	0xc820

/*
 * The VID/PID table the class hook matches on. Exposed because the hook's
 * class_info is assembled in lkpi_usbh_class.c (one assembly point for the
 * section) while the behaviour lives here.
 */
extern const uint16_t lkpi_modeswitch_id_table[][2];

/*
 * Class connect/disconnect for the CD-ROM persona. connect sends one eject
 * when the enumerated device is the CD-ROM personality and always returns a
 * negative value: the device is about to re-enumerate and must not be claimed.
 */
int lkpi_modeswitch_connect(struct usbh_hubport *hport, uint8_t intf);
int lkpi_modeswitch_disconnect(struct usbh_hubport *hport, uint8_t intf);

#endif /* _LINUXKPI_USBH_MODESWITCH_H_ */
