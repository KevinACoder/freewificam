/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fw_netif.h
 * @brief   The data path between the Wi-Fi port and the host network stack.
 *
 * The declarations are the net80211 port's presentation hooks
 * (`components/net80211/port/port.h`): plain byte buffers, plain callbacks
 * and an int status.  The host stack (lwIP in this tree) stays on the
 * consumer's side; the port neither includes nor names lwIP here.
 *
 * Ready in this checkpoint (implemented in libfw_net80211):
 *
 *   wlan_port_set_data_rx(fn, arg)
 *       Every non-EAPOL delivered frame, as a full Ethernet frame (14-byte
 *       header included).  The handler runs in the USB worker context: copy
 *       the frame and return; do not block.  `fn == NULL` removes the handler.
 *   wlan_port_set_eapol_rx(fn, arg)
 *       EAPOL (ethertype 0x888e) frames; `src` is the Ethernet source address
 *       and `buf` points at the payload behind the 14-byte header.
 *   wlan_port_xmit(frame, len)
 *       Send one full Ethernet frame out of the WLAN interface; the queue
 *       encrypts/encapsulates it.  Returns `len` on queue success, -1 when the
 *       interface is down or the frame is rejected.
 *   wlan_port_get_hwaddr(addr)
 *       The interface hardware address (also in fw_wifi.h).
 *
 * Documented gaps in this checkpoint:
 *
 *   - There is no netif registration API.  A consumer registers its own
 *     `struct netif`/`net_if`/netdev and wires the two callbacks above plus
 *     `wlan_port_xmit`; the in-tree examples do exactly that
 *     (`tests/board/rtos/app/cases/wlan_netif.c`).  A portable
 *     `fw_netif_register()` is an L2 item, not present here.
 *   - RX buffers are borrowed for the duration of the callback; there is no
 *     zero-copy hand-off contract yet.
 *   - TX completion is not reported to the consumer; the port owns the
 *     queue and the driver owns the completion.
 *
 * A note on link closure: the net80211 archive depends on the linuxkpi
 * archive and the driver archive, which in turn need the USB host adapter and
 * the CMSIS-RTOS2 implementation.  The USB host adapter is archived as
 * `libfw_usbhost.a` in wireless profiles; the CMSIS-RTOS2 implementation and
 * the two baremetal linker-script symbols stay the consumer's.  Note that a
 * link of the data API alone does not pull the driver adapter: the profile's
 * `-Wl,-u` registration set has to be supplied by the consumer (it is not in
 * `freewificam.pc` yet).  See docs/PORTING.md for the exact unresolved set
 * measured on a wireless profile.
 */

#ifndef FREEWIFICAM_FW_NETIF_H
#define FREEWIFICAM_FW_NETIF_H

#include "deps/port.h"

#endif /* FREEWIFICAM_FW_NETIF_H */
