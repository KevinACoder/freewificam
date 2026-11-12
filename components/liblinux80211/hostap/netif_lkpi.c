/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    netif_lkpi.c
 * @brief   wm_net.h over this image's lwIP netif list.
 *
 * The wireless netif is the one the lkIP egress assembled ("wl0",
 * third-party/lwip/ports/lkpinetif_lwip.c:147-148): mac80211's wlan0 is its
 * backing net_device.  One netif carries both the STA and the AP role here -
 * the role is the nl80211 iftype of the same interface - so all three lookups
 * answer it.  The list is only ever appended to (the netif is added once before
 * hostapd starts), so the walk needs no tcpip core lock.
 */
#include <stddef.h>

#include "lwip/netif.h"

#include "wm_net.h"

static struct netif *
lkpi_wireless_netif(void)
{
	struct netif *n;

	for (n = netif_list; n != NULL; n = n->next) {
		if (n->name[0] == 'w' && n->name[1] == 'l')
			return n;
	}
	return NULL;
}

struct netif *
net_get_sta_interface(void)
{
	return lkpi_wireless_netif();
}

struct netif *
net_get_uap_interface(void)
{
	return lkpi_wireless_netif();
}

struct netif *
net_get_wfd_interface(void)
{
	return lkpi_wireless_netif();
}
