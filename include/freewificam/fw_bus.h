/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fw_bus.h
 * @brief   Bus boundary — status: NOT READY at this checkpoint.
 *
 * There is no public transfer API yet.  The USB host path is internal to
 * `components/linuxkpi` and the vendored CherryUSB stack: a driver reaches the
 * bus through linuxkpi's URB-shaped interface, and the RK3568 EHCI/DWC3 glue
 * lives in the board layer.  None of that is a stable boundary: it names
 * CherryUSB types and it is compiled per board.  Publishing it now would
 * freeze the wrong shape, so this header deliberately exposes no submit /
 * complete / endpoint call and no bus-specific type.
 *
 * What IS real today, and is forwarded from the net80211 port
 * (`freewificam/deps/port.h`), is bus-neutral identity only:
 *
 *   enum wlan_bus_type          WLAN_BUS_USB / WLAN_BUS_PCIE
 *   struct wlan_chip_driver     name + bus + match table + attach/detach/stop;
 *                               the match tables are opaque here
 *   wlan_port_adapter_register  how a driver adapter registers its control
 *                               and diagnostic hooks with the port
 *
 * Those are driver-registration names, not a consumer data path.  A consumer
 * never submits a transfer through this header.
 *
 * What L3 needs before this header can carry an API (tracked in
 * docs/PORTING.md):
 *
 *   - one bus-neutral request shape: endpoint/address, direction, buffer +
 *     length, a completion callback and an opaque cookie; no CherryUSB,
 *     Zephyr UHC or RT-Thread usbhost type in the signature;
 *   - one adapter per host OS (`libfw_bus_usb_<os>.a`) implementing that shape
 *     over its own USB host stack;
 *   - a recorded URB sequence from the in-tree adapter so an L2 replay can
 *     prove the shape without RF hardware.
 *
 * Until then a port that needs the bus links the internal linuxkpi adapter,
 * which is exactly what the board images do.
 */

#ifndef FREEWIFICAM_FW_BUS_H
#define FREEWIFICAM_FW_BUS_H

#include "deps/port.h"

/* A consumer can gate code on this instead of guessing from the release. */
#define FW_BUS_API_READY 0

#endif /* FREEWIFICAM_FW_BUS_H */
