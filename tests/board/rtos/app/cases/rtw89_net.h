/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw89_net.h
 * @brief  The rtw89 IP presentation layer's two entry points, for the netio arm.
 *
 * rtw89_net.c owns the netif: it brings the bridge-backed interface up, sets
 * its address from the profile (DHCP or static) and binds the receive seam.
 * Case rtw89_net then adds the ARP/ICMP gate on top; case rtw89_netio uses the
 * same interface with the netio server instead.  Both symbols are compiled in
 * either case's image, so the header carries no case guard.
 */

#ifndef RTW89_NET_H
#define RTW89_NET_H

#include <stdint.h>

/* Attach check, lwIP init, netif add/up and the profile's address.  Returns 0
 * once the interface can carry; the static arm also needs CONFIG_RTW89_NET_IP/
 * MASK/GW (checked at build time by rtw89_usb.mk). */
int rtw89_net_bringup(void);

/* The interface's IPv4 address in network order (the shape the netio server
 * binds its sockets to).  Valid after rtw89_net_bringup() returned 0. */
uint32_t rtw89_net_ip4(void);

#endif /* RTW89_NET_H */
