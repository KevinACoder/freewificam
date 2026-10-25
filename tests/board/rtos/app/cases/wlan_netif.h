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

/* TCP downlink sink on port 5001: listen, accept one connection, run for
 * `seconds` one-second windows and print bytes/segments per window.  Returns
 * 0 when at least one byte arrived. */
int wlan_netif_tcp_sink(unsigned int seconds);

#endif /* RTOS_APP_WLAN_NETIF_H */

/* --- observation (WLAN_OBS image) ---------------------------------------- */

/* Plain-C snapshot of the lwIP half. The driver half prints its own line from
 * wlan.c; this shape carries no lwIP type across the world boundary. */
struct wlan_netif_obs {
	unsigned long rx_frames, rx_bytes;
	unsigned long rx_alloc_fail, rx_post_fail, rx_input_fail;
	unsigned long tx_frames, tx_bytes, tx_fail;
	unsigned long tcp_recv, tcp_xmit, tcp_drop, tcp_memerr, tcp_err;
	unsigned long pcb_avail, pcb_err;
	unsigned long pbuf_avail, pbuf_err;
	unsigned long tcpseg_avail, tcpseg_err;
	unsigned long inpkt_avail, inpkt_err;
	unsigned long mbox_post_full, mbox_fetch_ok, mbox_fetch_empty;
	unsigned long heartbeat;
	unsigned long lock_held_ms;
	const char *lock_owner;
	unsigned int tcp_state;
	unsigned int rcv_wnd, snd_wnd, snd_queuelen, unacked, mss, nrtx;
	unsigned int pcb_valid;
};

/* Called once per second by the TCP/UDP sink with the stack's snapshot. The
 * driver-world case registers it to print the combined observation line. */
void wlan_netif_set_obs_hook(void (*fn)(unsigned int sec,
			     const struct wlan_netif_obs *obs));

/* UDP downlink sink on WLAN_UDP_PORT: count datagrams/bytes for `seconds`. */
int wlan_netif_udp_sink(unsigned int seconds);

/* ICMP with a caller-chosen interval; the counted shape for the loss round. */
int wlan_netif_ping_ex(const char *peer_ip, unsigned int count,
		       unsigned int interval_ms, unsigned int *rx);
