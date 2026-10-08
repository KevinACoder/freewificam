/*
 * @file    seam_stub.c
 * @brief   Stub driver for the port seam: proves the scalar vtable is callable
 *          and counts what came through.
 *
 * The stub is driver-side code by construction: it includes the seam header and
 * nothing from net80211's world, which is the point of the seam (two worlds, no
 * shared structure layout).
 */

#include <stdint.h>
#include <string.h>

#include "seam.h"
#include "seam_stub.h"

static struct {
	unsigned start;
	unsigned add_interface;
	unsigned config;
	unsigned filter;
	unsigned scan_start;
	unsigned scan_complete;
	unsigned tx;
	unsigned set_key;
	unsigned clear_key;
	unsigned bss_info;
	uint8_t last_mac[6];
	uint32_t last_freq;
	uint32_t last_filter_changed;
	uint32_t last_filter_total;
	uint8_t last_tx[64];
	size_t last_tx_len;
	int last_tx_is_mgmt;
	int scan_live;		/* 1 between scan_start and scan_complete */
	int filter_at_scan_start;	/* the promiscuous state when the sweep began */
} counters;

static int stub_start(struct wlan_seam_drv *drv)
{
	(void) drv;
	counters.start++;
	return 0;
}

static int stub_stop(struct wlan_seam_drv *drv)
{
	(void) drv;
	return 0;
}

static int stub_add_interface(struct wlan_seam_drv *drv, const uint8_t mac[6],
    int iftype)
{
	(void) drv;
	(void) iftype;
	memcpy(counters.last_mac, mac, 6);
	counters.add_interface++;
	return 0;
}

static int stub_config(struct wlan_seam_drv *drv, uint32_t changed,
    uint32_t freq)
{
	(void) drv;
	(void) changed;
	counters.last_freq = freq;
	counters.config++;
	return 0;
}

static int stub_filter(struct wlan_seam_drv *drv, uint32_t changed,
    uint32_t total)
{
	(void) drv;
	counters.last_filter_changed = changed;
	counters.last_filter_total = total;
	counters.filter++;
	return 0;
}

static int stub_scan_start(struct wlan_seam_drv *drv)
{
	(void) drv;
	counters.scan_start++;
	counters.scan_live = 1;
	/* the ordering fact the consumer test asserts: the promiscuous bit is
	 * already programmed when the sweep begins */
	counters.filter_at_scan_start =
	    (counters.last_filter_total &
	    WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC) != 0;
	return 0;
}

static void stub_scan_complete(struct wlan_seam_drv *drv)
{
	(void) drv;
	counters.scan_complete++;
	counters.scan_live = 0;
}

static int stub_tx(struct wlan_seam_drv *drv, const uint8_t *frame, size_t len,
    int is_mgmt)
{
	(void) drv;
	if (len > sizeof(counters.last_tx)) {
		len = sizeof(counters.last_tx);
	}
	memcpy(counters.last_tx, frame, len);
	counters.last_tx_len = len;
	counters.last_tx_is_mgmt = is_mgmt;
	counters.tx++;
	return (int) len;
}

static int stub_set_key(struct wlan_seam_drv *drv, int pairwise, int key_idx,
    int cipher, const uint8_t *key, size_t key_len)
{
	(void) drv;
	(void) pairwise;
	(void) key_idx;
	(void) cipher;
	(void) key;
	(void) key_len;
	counters.set_key++;
	return 0;
}

static int stub_clear_key(struct wlan_seam_drv *drv, int pairwise, int key_idx)
{
	(void) drv;
	(void) pairwise;
	(void) key_idx;
	counters.clear_key++;
	return 0;
}

static void stub_bss_info(struct wlan_seam_drv *drv, int joined,
    const uint8_t bssid[6], uint32_t freq, uint16_t aid,
    const uint8_t *rates, size_t rates_len)
{
	(void) drv;
	(void) joined;
	(void) bssid;
	(void) aid;
	(void) rates;
	(void) rates_len;
	counters.last_freq = freq;
	counters.bss_info++;
}

static const struct wlan_seam_drv_ops stub_ops = {
	.start = stub_start,
	.stop = stub_stop,
	.add_interface = stub_add_interface,
	.config = stub_config,
	.filter = stub_filter,
	.sw_scan_start = stub_scan_start,
	.sw_scan_complete = stub_scan_complete,
	.tx = stub_tx,
	.set_key = stub_set_key,
	.clear_key = stub_clear_key,
	.bss_info = stub_bss_info,
};

void wlan_seam_stub_run(void)
{
	struct wlan_seam_drv *drv = NULL;
	static const uint8_t mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
	static const uint8_t bssid[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 };
	static const uint8_t frame[24] = { 0x80, 0x00 }; /* a beacon's opening */
	static const uint8_t key[16] = { 0 };

	/* What the bridge will do once it exists: drive the state machine's asks
	 * through the vtable. Every callback is scalar or byte-array. The scan
	 * group is driven in the order the seam's contract states - program the
	 * receive filter first, then the sweep, then clear the filter - so the
	 * stub observes the same ordering a real consumer must produce. */
	(void) stub_ops.start(drv);
	(void) stub_ops.add_interface(drv, mac, WLAN_SEAM_IFTYPE_STATION);
	(void) stub_ops.config(drv, WLAN_SEAM_CONFIG_CHANNEL, 2412);
	(void) stub_ops.filter(drv, WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC,
	    WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC);
	(void) stub_ops.sw_scan_start(drv);
	stub_ops.sw_scan_complete(drv);
	(void) stub_ops.filter(drv, WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC, 0);
	(void) stub_ops.tx(drv, frame, sizeof(frame), 1);
	(void) stub_ops.set_key(drv, 1, 0, WLAN_SEAM_CIPHER_CCMP, key,
	    sizeof(key));
	stub_ops.bss_info(drv, 1, bssid, 2412, 1,
	    (const uint8_t []){ 0x82, 0x84, 0x0b, 0x16, 0x0c, 0x18 }, 6);
}

unsigned wlan_seam_stub_calls(void)
{
	return counters.start + counters.add_interface + counters.config +
	    counters.filter + counters.scan_start + counters.scan_complete +
	    counters.tx + counters.set_key + counters.clear_key +
	    counters.bss_info;
}

unsigned wlan_seam_stub_tx_len(void)
{
	return (unsigned) counters.last_tx_len;
}

/* the filter ordering the seam contract requires: the promiscuous bit was
 * programmed when the sweep began, and it was cleared when the sweep ended */
int wlan_seam_stub_filter_at_scan_start(void)
{
	return counters.filter_at_scan_start;
}

int wlan_seam_stub_filter_cleared_after_scan(void)
{
	return (counters.last_filter_total &
	    WLAN_SEAM_FIF_BCN_PRBRESP_PROMISC) == 0;
}
