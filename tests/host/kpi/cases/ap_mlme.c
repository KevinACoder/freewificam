/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   ap_mlme.c
 * @brief  Host semantic case for the port's minimal AP MLME: the beacon split
 *         (hostapd's 63/101), the three response bytes, and the station state
 *         sequence, driven with synthetic 802.11 frames and a recording ops
 *         table.
 *
 * What this proves on the host: the frame parsing, the byte layout of auth /
 * probe / assoc responses, the AID/WMM/HT pass-through to NEW_STATION and the
 * idempotence of a repeated assoc request.  What it cannot prove: that the
 * stack accepts the frames (that is A2 on the board) or that the queue's
 * critical section is enough on the target (that is the boot).
 */
#include <string.h>

#include "kpi_test.h"
#include "linuxkpi_core.h"
#include "linux80211/ap_beacon.h"
#include "linux80211/ap_mlme.h"

#define AP_MAC	{ 0x6c, 0x29, 0x95, 0x12, 0xf0, 0xd9 }
#define STA_MAC	{ 0x90, 0xde, 0x80, 0x4c, 0x38, 0xc5 }
#define AP_SSID	"rtos-7260-ap"

static const unsigned char ap_mac[6] = AP_MAC;
static const unsigned char sta_mac[6] = STA_MAC;

/* --- recording ops ------------------------------------------------------- */

static unsigned char tx_last[512];
static unsigned int tx_last_len;
static unsigned int tx_count;
static int tx_rc;

static struct lkpi_ap_sta_add add_last;
static unsigned char add_rates[32];
static unsigned char add_ht[26];
static unsigned int add_count;
static int add_rc;

static unsigned char auth_mac[6];
static unsigned int auth_count;
static int auth_rc;

static int
rec_tx(const unsigned char *frame, unsigned int len)
{
	if (len > sizeof(tx_last))
		len = sizeof(tx_last);
	memcpy(tx_last, frame, len);
	tx_last_len = len;
	tx_count++;
	return tx_rc;
}

static int
rec_new_station(const struct lkpi_ap_sta_add *sta)
{
	if (sta == NULL)
		return -1;
	add_last = *sta;
	if (sta->rates != NULL) {
		unsigned int n = sta->rates_len;

		if (n > sizeof(add_rates))
			n = sizeof(add_rates);
		memcpy(add_rates, sta->rates, n);
		add_last.rates = add_rates;
	}
	if (sta->ht_capa != NULL) {
		memcpy(add_ht, sta->ht_capa, sizeof(add_ht));
		add_last.ht_capa = add_ht;
	}
	add_count++;
	return add_rc;
}

static int
rec_authorized(const unsigned char mac[6])
{
	memcpy(auth_mac, mac, 6);
	auth_count++;
	return auth_rc;
}

static void
rec_emit(const char *line)
{
	(void)line;
}

static const struct lkpi_ap_mlme_ops rec_ops = {
	.mgmt_tx = rec_tx,
	.new_station = rec_new_station,
	.set_authorized = rec_authorized,
	.emit = rec_emit,
};

/* --- synthetic frames ---------------------------------------------------- */

static unsigned int
put_hdr(unsigned char *p, unsigned int fc, const unsigned char *da,
	const unsigned char *sa)
{
	memset(p, 0, 24);
	p[0] = (unsigned char)(fc & 0xffu);
	p[1] = (unsigned char)((fc >> 8) & 0xffu);
	memcpy(p + 4, da, 6);
	memcpy(p + 10, sa, 6);
	memcpy(p + 16, ap_mac, 6);
	return 24;
}

static unsigned int
put_ie(unsigned char *p, unsigned int eid, const void *data, unsigned int len)
{
	p[0] = (unsigned char)eid;
	p[1] = (unsigned char)len;
	if (len != 0u)
		memcpy(p + 2, data, len);
	return 2u + len;
}

static unsigned int
build_auth_req(unsigned char *p)
{
	unsigned int n = put_hdr(p, 0x00b0u, ap_mac, sta_mac);

	memset(p + n, 0, 6);		/* algo 0, seq 1, status 0 */
	p[n + 2] = 1;
	return n + 6u;
}

static unsigned int
build_probe_req(unsigned char *p, const char *ssid)
{
	unsigned int n = put_hdr(p, 0x0040u, (const unsigned char[]){0xff,0xff,0xff,0xff,0xff,0xff}, sta_mac);
	unsigned int i = n;

	if (ssid != NULL)
		i += put_ie(p + i, 0, ssid, (unsigned int)strlen(ssid));
	i += put_ie(p + i, 1, "\x82\x84\x8b\x96\x0c\x12\x18\x24", 8);
	return i;
}

static unsigned int
build_assoc_req(unsigned char *p)
{
	static const unsigned char ht_cap[26] = {
		0x2c, 0x00, 0x17, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	static const unsigned char wmm[7] = {
		0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x80,
	};
	unsigned int n = put_hdr(p, 0x0000u, ap_mac, sta_mac);
	unsigned int i;

	p[n + 0] = 0x11;		/* capab: ESS | short preamble */
	p[n + 1] = 0x04;
	p[n + 2] = 0x0a;		/* listen interval 10 */
	p[n + 3] = 0x00;
	i = n + 4u;
	i += put_ie(p + i, 0, AP_SSID, (unsigned int)strlen(AP_SSID));
	i += put_ie(p + i, 1, "\x82\x84\x8b\x96\x0c\x12\x18\x24", 8);
	i += put_ie(p + i, 50, "\x30\x48\x60\x6c", 4);
	i += put_ie(p + i, 45, ht_cap, sizeof(ht_cap));
	i += put_ie(p + i, 221, wmm, sizeof(wmm));
	return i;
}

/* --- the case ------------------------------------------------------------ */

int kpi_case_ap_mlme(void);

int
kpi_case_ap_mlme(void)
{
	unsigned char head[LKPI_AP_BEACON_HEAD_MAX];
	unsigned char tail[LKPI_AP_BEACON_TAIL_MAX];
	unsigned char frame[512];
	unsigned int head_len = 0u, tail_len = 0u;
	unsigned int len;
	struct lkpi_ap_mlme_stats st;
	int rc;

	/* 1. the beacon split is hostapd's: 36 + SSID + rates + DS, tail after
	 *    the TIM (the 2026-11-11 image put the TIM before the SSID). */
	rc = lkpi_ap_beacon_build(AP_SSID, 2462u, ap_mac, head, sizeof(head),
				  &head_len, tail, sizeof(tail), &tail_len);
	KPI_ASSERT_EQ(rc, 0, "beacon build");
	KPI_ASSERT_EQ(head_len, 63, "head is 36 fixed + SSID + rates + DS");
	KPI_ASSERT_EQ(tail_len, 101, "tail starts after the TIM");
	KPI_ASSERT_EQ(head[32], 100, "beacon interval low byte");
	KPI_ASSERT_EQ(head[33], 0, "beacon interval high byte");
	KPI_ASSERT_EQ(head[34], 0x01, "capability low byte");
	KPI_ASSERT_EQ(head[35], 0x04, "capability high byte");
	KPI_ASSERT(head[36] == 0u && head[37] == 12u, "head starts with SSID");
	KPI_ASSERT(memcmp(head + 38, AP_SSID, 12) == 0, "SSID patched");
	KPI_ASSERT_EQ(lkpi_ap_beacon_validate(head, head_len, tail, tail_len,
					     NULL, 0u), 0, "validate");

	/* a TIM in the tail must be refused (mac80211 inserts it) */
	memmove(tail + 3, tail, tail_len);
	tail[0] = 5u;
	tail[1] = 4u;
	tail[2] = 0u;
	tail[3] = 0u;
	KPI_ASSERT_EQ(lkpi_ap_beacon_validate(head, head_len, tail, tail_len + 3,
					      NULL, 0u), -1,
		      "TIM in the tail is refused");
	(void)lkpi_ap_beacon_build(AP_SSID, 2462u, ap_mac, head, sizeof(head),
				   &head_len, tail, sizeof(tail), &tail_len);

	/* 2. configure + enable, then the auth exchange */
	rc = lkpi_ap_mlme_configure(AP_SSID, head, head_len, tail, tail_len,
				    &rec_ops);
	KPI_ASSERT_EQ(rc, 0, "mlme configure");
	KPI_ASSERT_EQ(lkpi_ap_mlme_configure(AP_SSID, head, head_len, tail,
					     tail_len, &rec_ops), -EBUSY,
		      "second configure is refused");
	lkpi_ap_mlme_enable(1);

	tx_rc = 0;
	tx_count = 0u;
	len = build_auth_req(frame);
	KPI_ASSERT_EQ(lkpi_ap_mlme_handle_frame(frame, len), 0, "auth handled");
	KPI_ASSERT_EQ(tx_count, 1, "one auth response");
	KPI_ASSERT_EQ(tx_last_len, 30, "auth response length");
	KPI_ASSERT_EQ(tx_last[0], 0xb0, "auth response fc low");
	KPI_ASSERT_EQ(tx_last[1], 0x00, "auth response fc high");
	KPI_ASSERT(memcmp(tx_last + 4, sta_mac, 6) == 0, "auth DA = STA");
	KPI_ASSERT(memcmp(tx_last + 10, ap_mac, 6) == 0, "auth SA = AP");
	KPI_ASSERT(memcmp(tx_last + 16, ap_mac, 6) == 0, "auth BSSID = AP");
	KPI_ASSERT_EQ(tx_last[24], 0, "auth algorithm 0");
	KPI_ASSERT_EQ(tx_last[26], 2, "auth transaction 2");
	KPI_ASSERT_EQ(tx_last[28], 0, "auth status 0");

	/* 3. a wildcard probe request gets the GT-derived probe response */
	tx_count = 0u;
	len = build_probe_req(frame, NULL);
	KPI_ASSERT_EQ(lkpi_ap_mlme_handle_frame(frame, len), 0, "probe handled");
	KPI_ASSERT_EQ(tx_count, 1, "one probe response");
	KPI_ASSERT_EQ(tx_last_len, head_len + tail_len,
		      "probe response = head + tail");
	KPI_ASSERT_EQ(tx_last[0], 0x50, "probe response fc low");
	KPI_ASSERT(memcmp(tx_last + 4, sta_mac, 6) == 0, "probe DA = STA");
	KPI_ASSERT(memcmp(tx_last + 36, head + 36, head_len - 36) == 0,
		   "probe head IEs are the AP's");
	KPI_ASSERT(memcmp(tx_last + head_len, tail, tail_len) == 0,
		   "probe tail is the AP's");
	KPI_ASSERT_EQ(tx_last[24], 0, "probe timestamp low");

	/* 4. assoc: NEW_STATION + AUTHORIZED + assoc response, then idempotence */
	tx_count = 0u;
	add_count = 0u;
	auth_count = 0u;
	add_rc = 0;
	auth_rc = 0;
	len = build_assoc_req(frame);
	KPI_ASSERT_EQ(lkpi_ap_mlme_handle_frame(frame, len), 0, "assoc handled");
	KPI_ASSERT_EQ(add_count, 1, "one new_station");
	KPI_ASSERT_EQ(auth_count, 1, "one set_authorized");
	KPI_ASSERT_EQ(tx_count, 1, "one assoc response");
	KPI_ASSERT_EQ(add_last.aid, 1, "first AID");
	KPI_ASSERT_EQ(add_last.listen_interval, 10, "listen interval");
	KPI_ASSERT_EQ(add_last.capab, 0x0411, "capability");
	KPI_ASSERT_EQ(add_last.rates_len, 12, "rates = IE1 + IE50");
	KPI_ASSERT(add_last.ht_capa != NULL, "HT capability passed");
	KPI_ASSERT(add_last.wmm != NULL, "WMM present");
	KPI_ASSERT(memcmp(auth_mac, sta_mac, 6) == 0, "authorized the STA");
	KPI_ASSERT_EQ(tx_last_len, 134, "assoc response length");
	KPI_ASSERT_EQ(tx_last[24], 0x01, "assoc capab low");
	KPI_ASSERT_EQ(tx_last[25], 0x04, "assoc capab high");
	KPI_ASSERT_EQ(tx_last[26], 0, "assoc status 0");
	KPI_ASSERT_EQ((unsigned int)(tx_last[28] | (tx_last[29] << 8)), 0xc001u,
		      "assoc AID with the reserved bits");
	KPI_ASSERT_EQ(lkpi_ap_mlme_handle_frame(frame, len), 0,
		      "repeated assoc still answered");
	KPI_ASSERT_EQ(add_count, 1, "no second new_station");
	KPI_ASSERT_EQ(auth_count, 1, "no second set_authorized");
	KPI_ASSERT_EQ(tx_count, 2, "a second assoc response");
	KPI_ASSERT_EQ(lkpi_ap_mlme_sta_count(), 1u, "one MLME station");

	/* 5. a failure from NEW_STATION is not sent as a success */
	lkpi_ap_mlme_stop();
	KPI_ASSERT_EQ(lkpi_ap_mlme_configure(AP_SSID, head, head_len, tail,
					     tail_len, &rec_ops), 0,
		      "reconfigure after stop");
	lkpi_ap_mlme_enable(1);
	tx_count = 0u;
	add_count = 0u;
	add_rc = -12;
	KPI_ASSERT(lkpi_ap_mlme_handle_frame(frame, len) != 0,
		   "add failure surfaces");
	KPI_ASSERT_EQ(tx_count, 0, "no assoc response on add failure");

	/* 6. the queue path: push from the RX side, drain from the task side */
	tx_count = 0u;
	add_count = 0u;
	auth_count = 0u;
	add_rc = 0;
	auth_rc = 0;
	lkpi_ap_mlme_stop();
	KPI_ASSERT_EQ(lkpi_ap_mlme_configure(AP_SSID, head, head_len, tail,
					     tail_len, &rec_ops), 0,
		      "third configure");
	lkpi_ap_mlme_enable(1);
	len = build_auth_req(frame);
	KPI_ASSERT_EQ(lkpi_ap_mlme_rx_push(frame, len), 0, "rx_push");
	KPI_ASSERT_EQ(lkpi_ap_mlme_poll(), 1, "poll handles one frame");
	KPI_ASSERT_EQ(lkpi_ap_mlme_poll(), 0, "second poll is empty");
	KPI_ASSERT_EQ(tx_count, 1, "the queued auth was answered");

	lkpi_ap_mlme_stats_get(&st);
	KPI_ASSERT_EQ(st.auth_req, 1, "stats auth_req");
	KPI_ASSERT_EQ(st.auth_resp, 1, "stats auth_resp");
	KPI_ASSERT_EQ(lkpi_ap_mlme_sta_count(), 0u, "no station in this instance");
	lkpi_ap_mlme_stop();
	return KPI_PASS;
}
