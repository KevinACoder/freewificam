/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fw_wifi.h
 * @brief   Wi-Fi control plane: bring-up, scan, interface identity, events.
 *
 * The declarations are the net80211 port's stable control interface
 * (`components/net80211/port/port.h`, shipped as `freewificam/deps/port.h`).
 * The port owns the imported net80211 stack, the chip drivers and the
 * presentation hooks; a consumer sees only plain C types and opaque handles.
 * No net80211, linuxkpi or bus type crosses this boundary.
 *
 * Ready in this checkpoint (implemented and compiled into libfw_net80211 plus
 * its dependencies; see the library selection table in docs/PORTING.md):
 *
 *   wlan_port_up()              firmware/power on, bring the first attached
 *                               interface up; 0 on success, -1 otherwise
 *   wlan_port_deinit()          stop and detach
 *   wlan_port_scan(ssid, len)   start one complete scan, no automatic join;
 *                               0 on success, -1 otherwise
 *   wlan_port_get_hwaddr(addr)  the interface hardware address after attach
 *   wlan_port_select(name)      focus the diagnostics on one adapter
 *   wlan_port_status_dump()     driver softc/counters dump (console)
 *   wlan_port_scan_dump()       the net80211 scan table (console)
 *   wlan_port_get_ic()          the active `struct ieee80211com *` as an
 *                               opaque `void *` for a supplicant bridge
 *   wlan_port_set_event_handler(fn, arg)
 *                               SCAN_DONE / ASSOC / DISASSOC, with the peer
 *                               address; the address is borrowed for the call
 *
 * Documented gaps in this checkpoint (do not pretend they exist):
 *
 *   - `wlan_port_scan` ignores its SSID argument today; the scan is a full
 *     sweep and the filter "arrives with the supplicant round".
 *   - There is no join/associate entry point.  Association is driven by the
 *     port state machine after a scan and the supplied supplicant event path
 *     (`wlan_port_set_eapol_rx` in fw_netif.h); a consumer cannot yet ask for
 *     "join this BSS" by name.
 *   - There is no AP/IBSS start entry point and no channel/rate/HT
 *     configuration here; those are driver/adapter concerns in this round.
 *   - `wlan_port_get_ic` is an escape hatch for the in-tree supplicant bridge,
 *     not a stable object model: the pointed-to type is internal.
 *
 * Status of the link: the archives that implement this header exist only for
 * profiles that compile the wireless drivers (`libfw_net80211.a`,
 * `libfw_linuxkpi.a`, `libfw_usbhost.a`, `libfw_drivers.a`).  A wired profile
 * publishes the header and no such archive, so a scan call fails to link there
 * by design.
 *
 * On a wireless profile the headers compile, but "links" and "works" are two
 * different claims in this checkpoint:
 *
 *   - A call such as `wlan_port_get_hwaddr()` links against the SDK alone with
 *     only the CMSIS-RTOS2 implementation unresolved.  That link is NOT
 *     useful: without the profile's link-forcing flags the driver adapter and
 *     the USB host are never pulled out of the archives, no adapter registers,
 *     and `wlan_port_up()` returns failure at run time.
 *   - Forcing the adapter (`-Wl,-u,wlan_seam_adapter_init`, plus the USB and
 *     firmware registrations; the exact set is in docs/PORTING.md) pulls the
 *     real path and leaves two linker-script symbols
 *     (`__noncacheable_start`/`__noncacheable_end`) and the CMSIS-RTOS2 calls
 *     unresolved.  Those belong to the consumer's linker script and OS.
 *
 * The SDK does not export the per-profile `-Wl,-u` set in `freewificam.pc` yet;
 * that is the main L1-to-L2 gap for this header.  docs/PORTING.md records the
 * measured unresolved set and the command that produced it.
 */

#ifndef FREEWIFICAM_FW_WIFI_H
#define FREEWIFICAM_FW_WIFI_H

#include "deps/port.h"

#endif /* FREEWIFICAM_FW_WIFI_H */
