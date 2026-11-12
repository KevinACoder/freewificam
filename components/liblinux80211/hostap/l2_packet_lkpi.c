/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    l2_packet_lkpi.c
 * @brief   wpa_supplicant's layer-2 EAPOL socket, replaced by this port's
 *          netdev seam.
 *
 * On Linux l2_packet_linux.c opens a packet socket and eloop watches it.  This
 * image has no socket; lkpinetif already classifies ETH_P_PAE and offers a sink.
 * The sink copies the EAPOL PDU into the supplicant's event queue, and
 * l2_packet_send() hands the Ethernet frame to lkpinetif_tx_frame(), the same
 * path the AP's EAPOL used.
 */
#include "includes.h"

#include "utils/common.h"
#include "l2_packet/l2_packet.h"

#include "lkpinetif_seam.h"
#include "supp_main.h"

#include "linux80211/sta_wpa.h"

struct l2_packet_data {
	char	ifname[17];
	u8	own_addr[ETH_ALEN];
	void	(*rx_callback)(void *ctx, const u8 *src_addr, const u8 *buf,
			       size_t len);
	void	*rx_callback_ctx;
	int	l2_hdr;
	u16	protocol;
};

static void
lkpi_l2_rx(const uint8_t src[6], const uint8_t *pdu, size_t len, void *arg)
{
	struct l2_packet_data *l2 = arg;
	struct wpa_supplicant_event_msg *msg;
	union wpa_event_data *ev;

	if (l2 == NULL || l2->rx_callback == NULL || pdu == NULL || len == 0u)
		return;
	msg = os_zalloc(sizeof(*msg));
	ev = os_zalloc(sizeof(*ev));
	if (msg == NULL || ev == NULL) {
		os_free(msg);
		os_free(ev);
		return;
	}
	msg->hostapd = 0;
	msg->ctx = l2->rx_callback_ctx;
	msg->event = EVENT_EAPOL_RX;
	msg->data = ev;
	ev->eapol_rx.src = src;
	ev->eapol_rx.data = pdu;
	ev->eapol_rx.data_len = len;
	if (wpa_supplicant_event_wrapper_deep_copy(msg, EVENT_EAPOL_RX, ev) < 0) {
		os_free(ev);
		os_free(msg);
		return;
	}
	lkpi_sta_note_eapol_rx();
	if (send_wpa_supplicant_event(msg) < 0)
		return;
}

int
l2_packet_get_own_addr(struct l2_packet_data *l2, u8 *addr)
{
	if (l2 == NULL || addr == NULL)
		return -1;
	os_memcpy(addr, l2->own_addr, ETH_ALEN);
	return 0;
}

int
l2_packet_send(struct l2_packet_data *l2, const u8 *dst_addr, u16 proto,
	       const u8 *buf, size_t len)
{
	struct l2_ethhdr *eth;
	unsigned char *frame;
	size_t flen;
	int rc;

	if (l2 == NULL || buf == NULL || len == 0u)
		return -1;
	if (l2->l2_hdr) {
		rc = lkpinetif_tx_frame(buf, len);
		lkpi_sta_note_eapol_tx(rc);
		return rc == 0 ? 0 : -1;
	}
	if (dst_addr == NULL)
		return -1;
	flen = sizeof(*eth) + len;
	frame = os_malloc(flen);
	if (frame == NULL)
		return -1;
	eth = (struct l2_ethhdr *)frame;
	os_memcpy(eth->h_dest, dst_addr, ETH_ALEN);
	os_memcpy(eth->h_source, l2->own_addr, ETH_ALEN);
	eth->h_proto = host_to_be16(proto);
	os_memcpy(frame + sizeof(*eth), buf, len);
	rc = lkpinetif_tx_frame(frame, flen);
	os_free(frame);
	lkpi_sta_note_eapol_tx(rc);
	return rc == 0 ? 0 : -1;
}

struct l2_packet_data *
l2_packet_init(const char *ifname, const u8 *own_addr, unsigned short protocol,
	       void (*rx_callback)(void *ctx, const u8 *src_addr,
				   const u8 *buf, size_t len),
	       void *rx_callback_ctx, int l2_hdr)
{
	struct l2_packet_data *l2;

	l2 = os_zalloc(sizeof(*l2));
	if (l2 == NULL)
		return NULL;
	if (ifname != NULL)
		os_strlcpy(l2->ifname, ifname, sizeof(l2->ifname));
	l2->rx_callback = rx_callback;
	l2->rx_callback_ctx = rx_callback_ctx;
	l2->l2_hdr = l2_hdr;
	l2->protocol = (u16)protocol;
	if (own_addr != NULL) {
		os_memcpy(l2->own_addr, own_addr, ETH_ALEN);
	} else if (lkpinetif_hwaddr(l2->own_addr) != 0) {
		os_free(l2);
		return NULL;
	}
	if (rx_callback != NULL)
		lkpinetif_set_eapol_sink(lkpi_l2_rx, l2);
	printf("lkpi: l2_packet ifname=%s mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
	       l2->ifname, l2->own_addr[0], l2->own_addr[1], l2->own_addr[2],
	       l2->own_addr[3], l2->own_addr[4], l2->own_addr[5]);
	return l2;
}

struct l2_packet_data *
l2_packet_init_bridge(const char *br_ifname, const char *ifname,
		      const u8 *own_addr, unsigned short protocol,
		      void (*rx_callback)(void *ctx, const u8 *src_addr,
					  const u8 *buf, size_t len),
		      void *rx_callback_ctx, int l2_hdr)
{
	(void)br_ifname;
	return l2_packet_init(ifname, own_addr, protocol, rx_callback,
			      rx_callback_ctx, l2_hdr);
}

void
l2_packet_deinit(struct l2_packet_data *l2)
{
	if (l2 == NULL)
		return;
	lkpinetif_set_eapol_sink(NULL, NULL);
	os_free(l2);
}

int
l2_packet_get_ip_addr(struct l2_packet_data *l2, char *buf, size_t len)
{
	(void)l2;
	(void)buf;
	(void)len;
	return -1;
}

void
l2_packet_notify_auth_start(struct l2_packet_data *l2)
{
	(void)l2;
}

int
l2_packet_set_packet_filter(struct l2_packet_data *l2,
			    enum l2_packet_filter_type type)
{
	(void)l2;
	(void)type;
	return 0;
}
