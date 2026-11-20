/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * pi_net.h - the F429's network face toward the Pi 4B.
 *
 * The implementation lives in the WICED/WWD flag world (pi_net.c) because it
 * talks to the AP6181 driver and lwIP; the camera case lives in the CherryUSB
 * flag world (pi_stream.c).  This header is the boundary: only plain C types,
 * no WICED or lwIP header may leak through it.
 */

#ifndef F429_PI_NET_H
#define F429_PI_NET_H

#include <stddef.h>
#include <stdint.h>

struct pi_link_info {
	int	 join_rc;	/* wwd_wifi_join() result, 0 = success */
	unsigned join_ms;	/* time from wifi_on to ready-to-transceive */
	int	 dhcp;		/* 1 = DHCP address, 0 = static fallback */
	char	 ip[16];
	char	 gw[16];
};

/* Start lwIP and the WICED driver, join the bench AP, add the netif and take
 * an address (DHCP, else the profile's static fallback).  Returns 0 when the
 * link is up; the caller decides whether a fallback address is acceptable. */
int pi_net_up(struct pi_link_info *out);

/* Send `count` ICMP echoes to the Pi and wait for the replies.  Returns 0 when
 * all `count` came back; *recv is always updated. */
int pi_net_ping(unsigned count, unsigned *recv);

/* Open a non-blocking-bounded TCP connection to the Pi.  Returns the fd, or
 * -1.  The socket is left with TCP_NODELAY and SO_SNDTIMEO (100 ms). */
int pi_sender_connect(uint16_t port);

/* Send one FWC message.  On any short/failed write the fd is closed and set
 * to -1; the caller reconnects.  Returns 0 when the whole message was
 * accepted by the stack. */
int pi_sender_send(int *fd, uint8_t type, const void *payload, size_t len,
		   uint32_t timestamp_us, uint32_t seq, uint32_t crc32);

void pi_sender_close(int *fd);

struct pi_port_stats {
	uint32_t sdio_intrs;
	uint32_t rx_frames;
	uint32_t tx_frames;
	uint32_t buffer_fail;
	uint32_t cmd_timeouts;
	uint32_t dma_errors;
};
void pi_net_port_stats(struct pi_port_stats *out);

#endif /* F429_PI_NET_H */
