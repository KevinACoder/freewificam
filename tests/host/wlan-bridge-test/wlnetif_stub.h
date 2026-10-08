/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    wlnetif_stub.h
 * @brief   The presentation-layer stub test's face: the step functions the
 *          bridge suite's flow calls, and the recorder it asserts against.
 *          Scalars and byte arrays only, so a linuxkpi-world caller can use it.
 */

#ifndef WLNETIF_STUB_H
#define WLNETIF_STUB_H

#include <stdint.h>

struct wlnetif_stub_result {
	unsigned int hwaddr_ok;		/* netif adopted the bridge's address */
	unsigned int tx_chain_calls;	/* linkoutput calls made */
	unsigned int rx_input_frames;	/* frames delivered to netif->input */
	unsigned int rx_ident;		/* the delivered pbuf equals the injected frame */
	unsigned int link_up_seen;	/* ASSOC event -> netif_set_link_up */
	unsigned int link_down_seen;	/* DISASSOC event -> netif_set_link_down */
	unsigned int link_assoc_flag;	/* the link-up flag was set on ASSOC */
	unsigned int link_down_flag;	/* and cleared on DISASSOC */
	unsigned int up_flag;		/* wlannetif_start set the up flag */
	unsigned int down_flag;		/* wlannetif_stop cleared it, DHCP stopped */
	unsigned int dhcp_started;
	unsigned int dhcp_stopped;
	unsigned int diag_rx, diag_rx_fail, diag_tx, diag_tx_fail;
	uint8_t rx_seen[1600];
};

/* assemble the netif over the real bridge (netif_add path) */
int	wlnetif_stub_assemble(uint8_t mac_out[6]);

/* send the two-piece ethernet frame through netif->linkoutput; the payload the
 * chain carries is the returned buffer's content so the caller knows what to
 * look for downstream */
int	wlnetif_stub_linkoutput(void);

/* bring up / stop (the DHCP client starts on the up path) */
void	wlnetif_stub_up(void);
void	wlnetif_stub_down(void);

/* the flow reads the link flag right after each event arrives */
void	wlnetif_stub_note_link_up(void);
void	wlnetif_stub_note_link_down(void);

/* take a snapshot of the recorder */
void	wlnetif_stub_get(struct wlnetif_stub_result *out);

#endif /* WLNETIF_STUB_H */
