/*
 * @file
 * @brief l2_packet carrier A (direct): EAPOL TX through the vendor
 * backend's eapol_xmit op ([S7a] FR-4, spec §3.6(b)).
 *
 * RX is not routed through this file; the driver worker delivers
 * EAPOL frames as EVENT_EAPOL_RX via the event pump (the l2_packet rx
 * callback is kept for API compatibility but never fires) - the same
 * shape the prior project board-proved on the net80211 line.
 *
 * No-backend shape: init succeeds with a zero own_addr and send
 * fails - the smoke image's supplicant stays alive.
 */

#include <includes.h>

#include "utils/common.h"
#include "l2_packet/l2_packet.h"

#include "wpa_vendor_port.h"

struct l2_packet_data {
	u8 own_addr[ETH_ALEN];
	void (*rx_callback)(void *ctx, const u8 *src_addr, const u8 *buf,
	    size_t len);
	void *rx_callback_ctx;
};

struct l2_packet_data *l2_packet_init(const char *ifname,
    const u8 *own_addr, unsigned short protocol,
    void (*rx_callback)(void *ctx, const u8 *src_addr, const u8 *buf,
    size_t len),
    void *rx_callback_ctx, int l2_hdr) {
	const struct wpa_vendor_port *vp = wpa_vendor_backend();
	struct l2_packet_data *l2;

	(void) ifname;
	(void) protocol;
	(void) l2_hdr;

	l2 = os_zalloc(sizeof(*l2));
	if (l2 == NULL) {
		return NULL;
	}
	if (own_addr != NULL) {
		os_memcpy(l2->own_addr, own_addr, ETH_ALEN);
	} else if (vp != NULL && vp->get_hwaddr != NULL &&
	    vp->get_hwaddr(l2->own_addr) != 0) {
		os_free(l2);
		return NULL;
	}
	l2->rx_callback = rx_callback;
	l2->rx_callback_ctx = rx_callback_ctx;
	return l2;
}

/* the fork's wpa_supplicant.c calls this on the bridge_ifname path
 * (never taken here - bridge_ifname stays NULL); the vendored none stub
 * provides its own in the smoke build, so this one must not clash */
struct l2_packet_data *l2_packet_init_bridge(const char *br_ifname,
    const char *ifname, const u8 *own_addr, unsigned short protocol,
    void (*rx_callback)(void *ctx, const u8 *src_addr, const u8 *buf,
	size_t len),
    void *rx_callback_ctx, int l2_hdr)
{
	(void) br_ifname;
	return l2_packet_init(ifname, own_addr, protocol, rx_callback,
	    rx_callback_ctx, l2_hdr);
}

void l2_packet_deinit(struct l2_packet_data *l2) {
	os_free(l2);
}

int l2_packet_get_own_addr(struct l2_packet_data *l2, u8 *addr) {
	os_memcpy(addr, l2->own_addr, ETH_ALEN);
	return 0;
}

int l2_packet_send(struct l2_packet_data *l2, const u8 *dst_addr,
    u16 proto, const u8 *buf, size_t len) {
	const struct wpa_vendor_port *vp = wpa_vendor_backend();

	(void) l2;
	(void) proto; /* the payload is already an Ethernet frame body;
		       * the vendor xmit builds the 0x888E header */

	if (dst_addr == NULL || buf == NULL) {
		return -1;
	}
	if (vp == NULL || vp->eapol_xmit == NULL) {
		wpa_printf(MSG_ERROR, "wpa: no vendor backend");
		return -1;
	}
	return vp->eapol_xmit(dst_addr, buf, len) >= 0 ? (int) len : -1;
}

int l2_packet_get_ip_addr(struct l2_packet_data *l2, char *buf,
    size_t len) {
	(void) l2;
	(void) buf;
	(void) len;
	return -1;
}

void l2_packet_notify_auth_start(struct l2_packet_data *l2) {
	(void) l2;
}

int l2_packet_set_packet_filter(struct l2_packet_data *l2,
    enum l2_packet_filter_type type) {
	(void) l2;
	(void) type;
	return 0;
}
