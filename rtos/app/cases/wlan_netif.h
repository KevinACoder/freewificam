/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   wlan_netif.h
 * @brief  Plain-C control face between the driver-world wlan case and the
 *         lwIP-world netif/ping support.
 *
 * The two worlds cannot share a translation unit: lwIP's headers and the
 * driver's headers disagree about names they both own.  This header is the
 * seam, so the wlan case never includes lwIP and the netif support never
 * includes linuxkpi.
 */

#ifndef RTOS_APP_WLAN_NETIF_H
#define RTOS_APP_WLAN_NETIF_H

/* Bring lwIP up over the driver's net device (the driver-world half must have
 * called lkpinetif_bind() first).  Returns 0 on success. */
int wlan_netif_start(void);

/* Tell the stack the link state the driver reported. */
void wlan_netif_link(int up);

/* Send `count` ICMP echoes to `peer_ip` and wait for the replies.  Returns 0
 * when at least one reply arrived; *rx is the reply count. */
int wlan_netif_ping(const char *peer_ip, unsigned int count,
		    unsigned int *rx);

#endif /* RTOS_APP_WLAN_NETIF_H */
