/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wm_net.h
 * @brief   The NXP wifi-middleware netif lookup the imported FreeRTOS entry
 *          calls: net_get_sta_interface()/net_get_uap_interface().
 *
 * The NXP port asks the wifi middleware which lwIP netif stands for the STA and
 * the uAP role.  This port has one wireless netif ("wl0", the lkIP egress over
 * mac80211) and plays both roles under the same interface, so the two lookups
 * answer the same netif - the AP round's role is an nl80211 iftype on that
 * interface, not a second netif.
 */
#ifndef WM_NET_H
#define WM_NET_H

/* the NXP wifi middleware's failure code (supp_main.c returns it) */
#ifndef WM_FAIL
#define WM_FAIL (-1)
#endif

struct netif;

struct netif *net_get_sta_interface(void);
struct netif *net_get_uap_interface(void);
struct netif *net_get_wfd_interface(void);

#endif /* WM_NET_H */
