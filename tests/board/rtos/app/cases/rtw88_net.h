/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw88_net.h
 * @brief  The rtw88 IP presentation layer's two entry points, for the netio arm.
 *
 * rtw88_net.c owns the netif: it brings the bridge-backed interface up, sets
 * its address from the profile (DHCP or static) and binds the receive seam.
 * Case rtw88_net then adds the ARP/ICMP gate on top; case rtw88_netio uses the
 * same interface with the netio server instead.  Both symbols are compiled in
 * either case's image, so the header carries no case guard.
 */

#ifndef RTW88_NET_H
#define RTW88_NET_H

#include <stdint.h>

/* Attach check, lwIP init, netif add/up and the profile's address.  Returns 0
 * once the interface can carry; the static arm also needs CONFIG_RTW88_NET_IP/
 * MASK/GW (checked at build time by rtw88_usb.mk). */
int rtw88_net_bringup(void);

/* The interface's IPv4 address in network order (the shape the netio server
 * binds its sockets to).  Valid after rtw88_net_bringup() returned 0. */
uint32_t rtw88_net_ip4(void);

#endif /* RTW88_NET_H */
