/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8xxxu_net.h
 * @brief  The 8188EU lwIP-over-bridge presentation layer, shared by the two
 *         cases that need it: `rtl8xxxu_net` (iperf ladder) and
 *         `rtl8xxxu_netio` (the NETIO server).
 *
 * `rtl8xxxu_net_bringup()` is the one bring-up: it checks the bridge state,
 * starts the TCP/IP stack once per boot, adds the netif, registers the receive
 * seam and gates on the AP answering ICMP.  Both cases call it; only the first
 * call does the work, and a failed ping is reported through the return value so
 * a later case does not silently run against an unassociated link.
 *
 * `rtl8xxxu_net_ip4()` returns the netif's address in network order for the
 * NETIO server's UDP data bound (the socket must bind the board's own address,
 * not INADDR_ANY: see net_mdk_netio.c).
 */

#ifndef RTOS_APP_CASES_RTL8XXXU_NET_H
#define RTOS_APP_CASES_RTL8XXXU_NET_H

#include <stdint.h>

int rtl8xxxu_net_bringup(void);
uint32_t rtl8xxxu_net_ip4(void);

#endif /* RTOS_APP_CASES_RTL8XXXU_NET_H */
