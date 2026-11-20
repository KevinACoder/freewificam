/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * wwd_network.c - the seam between WWD's buffer world and lwIP 2.2.
 *
 * Transmit: lwIP's linkoutput hands over a pbuf; the seam takes one reference
 * and gives the same pbuf to the driver, which frees its reference when SDPCM
 * finishes with it.  Receive: SDPCM hands the seam a pbuf that already points
 * at the 802.3 frame, and the seam gives it to the stack's input.  No copy on
 * either side for unfragmented frames (the only kind this driver produces).
 *
 * The ethertype filter is what WICED's lwIP glue uses for EAPOL; this round
 * joins an open AP, so the hook exists and is recorded, not exercised.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "lwip/def.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/mem.h"
#include "lwip/netif.h"
#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

#include "network/wwd_network_interface.h"
#include "wwd_constants.h"
#include "wwd_debug.h"
#include "wwd_wifi.h"

#include "wiced_port.h"
#include "wiced_port_config.h"

static struct netif *s_netif;

static uint16_t s_filter_ethertype;
static wwd_interface_t s_filter_interface;
static wwd_network_filter_ethernet_packet_t s_filter_callback;
static void *s_filter_userdata;

static err_t
ap6181_linkoutput(struct netif *netif, struct pbuf *p)
{
	wwd_interface_t interface;

	if (netif == NULL || p == NULL)
		return ERR_ARG;

	interface = (wwd_interface_t)netif->state;
	if (wwd_wifi_is_ready_to_transceive(interface) != WWD_SUCCESS)
		return ERR_INPROGRESS;

	/* SDPCM on this chip takes one contiguous piece: the stack hands us the
	 * frame as the driver expects it, and a chained pbuf is a seam bug, not
	 * something to silently linearise on the transmit path. */
	if (p->next != NULL || p->tot_len != p->len)
		return ERR_ARG;

	pbuf_ref(p);
	ap6181_stats.tx_frames++;
	wwd_network_send_ethernet_data(p, interface);
	return ERR_OK;
}

err_t
ap6181_netif_init(struct netif *netif)
{
	wiced_mac_t mac;
	wwd_interface_t interface;

	if (netif == NULL)
		return ERR_ARG;
	interface = (wwd_interface_t)netif->state;

	netif->name[0] = 'w';
	netif->name[1] = 'l';
	netif->hwaddr_len = ETH_HWADDR_LEN;
	if (wwd_wifi_get_mac_address(&mac, interface) == WWD_SUCCESS)
		memcpy(netif->hwaddr, mac.octet, ETH_HWADDR_LEN);

	netif->mtu = (u16_t)WICED_PAYLOAD_MTU;
	netif->flags = (u8_t)(NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
			      NETIF_FLAG_LINK_UP);
	netif->output = etharp_output;
	netif->linkoutput = ap6181_linkoutput;
	s_netif = netif;
	return ERR_OK;
}

void
ap6181_netif_attach(struct netif *netif)
{
	s_netif = netif;
}

void
host_network_process_ethernet_data(wiced_buffer_t buffer,
				   wwd_interface_t interface)
{
	struct netif *netif = s_netif;
	struct pbuf *p = buffer;

	if (p == NULL)
		return;

	if (netif == NULL || netif_is_up(netif) == 0) {
		ap6181_stats.rx_dropped++;
		(void)pbuf_free(p);
		return;
	}

	/* The seam promises the stack a single, complete piece.  A chained
	 * buffer would mean the driver changed its receive shape; copy it
	 * rather than hand lwIP a chain it will misinterpret. */
	if (p->next != NULL || p->tot_len != p->len) {
		struct pbuf *q = pbuf_alloc(PBUF_RAW, p->tot_len, PBUF_RAM);

		if (q == NULL) {
			ap6181_stats.rx_dropped++;
			(void)pbuf_free(p);
			return;
		}
		(void)pbuf_copy(q, p);
		(void)pbuf_free(p);
		p = q;
	}

	if (s_filter_callback != NULL && s_filter_interface == interface &&
	    p->len >= SIZEOF_ETH_HDR) {
		const uint8_t *frame = (const uint8_t *)p->payload;
		uint16_t ethertype = (uint16_t)((frame[12] << 8) | frame[13]);

		if (ethertype == s_filter_ethertype)
			s_filter_callback(p->payload, s_filter_userdata);
	}

	if (netif->input(p, netif) != ERR_OK) {
		ap6181_stats.rx_dropped++;
		(void)pbuf_free(p);
		return;
	}
	ap6181_stats.rx_frames++;
}

void
host_network_set_ethertype_filter(uint16_t ethertype,
				  wwd_interface_t interface,
				  wwd_network_filter_ethernet_packet_t ethernet_packet_callback,
				  void *userdata)
{
	s_filter_ethertype = ethertype;
	s_filter_interface = interface;
	s_filter_callback = ethernet_packet_callback;
	s_filter_userdata = userdata;
}
/* --- the stack's two bring-up shapes -------------------------------------- */

/*
 * A WWD buffer is an lwIP pbuf, so the stack's memory pools must exist before
 * the driver asks for its first buffer.  The stages that do not put a netif up
 * (attach, scan, join) only need the core; the data stage needs the TCP/IP
 * thread as well, because its netif input is tcpip_input.  Each image calls one
 * of these exactly once, and a second call in the same boot is refused rather
 * than silently re-initialising the stack.
 */
void
ap6181_lwip_core_init(void)
{
	static bool done;

	if (done)
		return;
	lwip_init();
	done = true;
}

static SemaphoreHandle_t s_stack_ready;

static void
ap6181_tcpip_done(void *arg)
{
	(void)arg;
	if (s_stack_ready != NULL)
		(void)xSemaphoreGive(s_stack_ready);
}

int
ap6181_lwip_stack_start(void)
{
	static int state;	/* 0 cold, 1 up, -1 failed */

	if (state != 0)
		return (state == 1) ? 0 : -1;

	s_stack_ready = xSemaphoreCreateBinary();
	if (s_stack_ready == NULL) {
		state = -1;
		return -1;
	}
	tcpip_init(ap6181_tcpip_done, NULL);
	if (xSemaphoreTake(s_stack_ready, pdMS_TO_TICKS(5000)) != pdTRUE) {
		state = -1;
		return -1;
	}
	state = 1;
	return 0;
}
