/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   linuxkpi_ap_mlme.c
 * @brief  The minimal open-AP MLME (ap_mlme.h): frame queue, 802.11 parse,
 *         station state and the three responses hostapd would build.
 *
 * Context contract (the reason for the queue):
 *   - rx_push() runs in mac80211's RX path; it only copies bytes, under a
 *     critical section, and never sleeps, allocates or calls nl80211.
 *   - poll() runs in the caller's task.  It pops exactly one frame under the
 *     lock, releases it, then parses and acts: NL80211_CMD_FRAME /
 *     NEW_STATION / SET_STATION can take the wiphy lock and sleep, which is
 *     illegal in the RX path on Linux too.
 *
 * The frame bytes are ordinary 802.11: 24-byte header, then
 *   probe request  : IEs
 *   auth           : auth algorithm (2) + transaction (2) + status (2)
 *   assoc request  : capab (2) + listen interval (2) + IEs
 *   reassoc request: capab (2) + listen interval (2) + current AP (6) + IEs
 * No stack types are used here on purpose: the host bench compiles this file
 * as-is and drives it with synthetic frames.
 */
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include "linux80211/ap_mlme.h"

/* --- 802.11 constants this file needs (no GPL header on purpose) --------- */

#define MLME_HDR_LEN		24u
#define MLME_SUBTYPE_MASK	0x00f0u
#define MLME_TYPE_MASK		0x000cu

#define MLME_ST_ASSOC_REQ	0x0000u
#define MLME_ST_ASSOC_RESP	0x0010u
#define MLME_ST_REASSOC_REQ	0x0020u
#define MLME_ST_PROBE_REQ	0x0040u
#define MLME_ST_PROBE_RESP	0x0050u
#define MLME_ST_AUTH		0x00b0u

#define MLME_EID_SSID		0u
#define MLME_EID_RATES		1u
#define MLME_EID_TIM		5u
#define MLME_EID_HT_CAP		45u
#define MLME_EID_HT_OP		61u
#define MLME_EID_EXT_CAP	127u
#define MLME_EID_WMM		221u

#define MLME_HT_CAP_LEN		26u
/* The largest response this MLME builds is the probe response (head + tail,
 * 164 B for the round's template); 256 B leaves room for a longer SSID/IE set
 * without putting a second frame-sized buffer on the runner's stack. */
#define MLME_RESP_MAX		256u
#define MLME_WMM_MIN_LEN	7u
#define MLME_AID_BASE		1u
#define MLME_AID_RESERVED	0xc000u	/* bits 14-15 of the assoc resp AID */

struct mlme_sta {
	unsigned char mac[6];
	uint16_t aid;
	unsigned int used;
	unsigned int added;
	unsigned int authorized;
};

struct mlme_queue_slot {
	uint16_t len;
	unsigned char data[LKPI_AP_MLME_FRAME_MAX];
};

struct mlme_state {
	unsigned int configured;
	unsigned int enabled;
	unsigned char ap[6];
	unsigned char ssid[LKPI_AP_MLME_SSID_MAX];
	unsigned int ssid_len;
	uint16_t seq;
	unsigned char head[LKPI_AP_MLME_HEAD_MAX];
	unsigned int head_len;
	unsigned char tail[LKPI_AP_MLME_TAIL_MAX];
	unsigned int tail_len;
	const struct lkpi_ap_mlme_ops *ops;
	struct mlme_sta sta[LKPI_AP_MLME_STA_MAX];
	struct lkpi_ap_mlme_stats st;
	spinlock_t lock;
	unsigned int q_head;
	unsigned int q_count;
	struct mlme_queue_slot q[LKPI_AP_MLME_QUEUE_LEN];
};

static struct mlme_state g;

/* --- small helpers ------------------------------------------------------- */

static uint16_t rd16(const unsigned char *p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void wr16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xffu);
	p[1] = (unsigned char)((v >> 8) & 0xffu);
}

static void mac_str(char *out, unsigned int cap, const unsigned char *mac)
{
	snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void emit_line(const char *fmt, ...)
{
	char line[192];
	va_list ap;

	if (g.ops == NULL || g.ops->emit == NULL)
		return;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	line[sizeof(line) - 1u] = '\0';
	g.ops->emit(line);
}

/* Find one IE.  Returns 0 and the body/len on success. */
static int find_ie(const unsigned char *ies, unsigned int len, unsigned int eid,
		   const unsigned char **body, unsigned int *blen)
{
	unsigned int i = 0u;

	while (i + 2u <= len) {
		unsigned int ie_len = ies[i + 1u];

		if (i + 2u + ie_len > len)
			return -1;
		if (ies[i] == eid) {
			if (body != NULL)
				*body = ies + i + 2u;
			if (blen != NULL)
				*blen = ie_len;
			return 0;
		}
		i += 2u + ie_len;
	}
	return -1;
}

/* The WMM information element: 00:50:f2, OUI type 2, subtype 0 or 1. */
static int find_wmm(const unsigned char *ies, unsigned int len,
		    const unsigned char **body, unsigned int *blen)
{
	unsigned int i = 0u;

	while (i + 2u <= len) {
		unsigned int ie_len = ies[i + 1u];

		if (i + 2u + ie_len > len)
			return -1;
		if (ies[i] == MLME_EID_WMM && ie_len >= MLME_WMM_MIN_LEN &&
		    ies[i + 2u] == 0x00u && ies[i + 3u] == 0x50u &&
		    ies[i + 4u] == 0xf2u && ies[i + 5u] == 0x02u) {
			if (body != NULL)
				*body = ies + i + 2u;
			if (blen != NULL)
				*blen = ie_len;
			return 0;
		}
		i += 2u + ie_len;
	}
	return -1;
}

/* Append the IE with `eid` from src[start..slen), in place, if it fits.
 * Returns the new write position; a missing or oversized IE leaves the
 * position untouched.  `start` is not decoration: the head is a full frame and
 * its IEs begin after the 36 fixed bytes. */
static unsigned int append_ie(unsigned char *dst, unsigned int cap,
			      unsigned int pos, const unsigned char *src,
			      unsigned int slen, unsigned int start,
			      unsigned int eid)
{
	unsigned int i = start;

	while (i + 2u <= slen) {
		unsigned int ie_len = src[i + 1u];

		if (i + 2u + ie_len > slen)
			return pos;
		if (src[i] == eid) {
			if (pos + 2u + ie_len > cap)
				return pos;
			memcpy(dst + pos, src + i, 2u + ie_len);
			return pos + 2u + ie_len;
		}
		i += 2u + ie_len;
	}
	return pos;
}

static unsigned int next_seq(void)
{
	uint16_t sc = (uint16_t)((g.seq & 0x0fffu) << 4);

	g.seq = (uint16_t)((g.seq + 1u) & 0x0fffu);
	return sc;
}

static unsigned int build_hdr(unsigned char *out, unsigned int fc,
			      const unsigned char *da, unsigned int cap)
{
	uint16_t sc;

	if (cap < MLME_HDR_LEN)
		return 0u;
	memset(out, 0, MLME_HDR_LEN);
	wr16(out, fc);
	memcpy(out + 4, da, 6);
	memcpy(out + 10, g.ap, 6);
	memcpy(out + 16, g.ap, 6);
	sc = (uint16_t)next_seq();
	wr16(out + 22, sc);
	return MLME_HDR_LEN;
}

/* --- station table ------------------------------------------------------- */

static struct mlme_sta *sta_find(const unsigned char *mac)
{
	unsigned int i;

	for (i = 0u; i < LKPI_AP_MLME_STA_MAX; i++)
		if (g.sta[i].used && memcmp(g.sta[i].mac, mac, 6) == 0)
			return &g.sta[i];
	return NULL;
}

static struct mlme_sta *sta_alloc(const unsigned char *mac)
{
	unsigned int i;

	for (i = 0u; i < LKPI_AP_MLME_STA_MAX; i++) {
		if (g.sta[i].used)
			continue;
		memset(&g.sta[i], 0, sizeof(g.sta[i]));
		memcpy(g.sta[i].mac, mac, 6);
		g.sta[i].aid = (uint16_t)(MLME_AID_BASE + i);
		g.sta[i].used = 1u;
		return &g.sta[i];
	}
	return NULL;
}

/* --- responses ----------------------------------------------------------- */

static unsigned int build_auth_resp(const unsigned char *req,
				    unsigned char *out, unsigned int cap)
{
	if (build_hdr(out, MLME_ST_AUTH, req + 10, cap) == 0u ||
	    cap < MLME_HDR_LEN + 6u)
		return 0u;
	wr16(out + MLME_HDR_LEN, 0u);		/* open system */
	wr16(out + MLME_HDR_LEN + 2u, 2u);	/* transaction 2 */
	wr16(out + MLME_HDR_LEN + 4u, 0u);	/* status: success */
	return MLME_HDR_LEN + 6u;
}

static unsigned int build_probe_resp(const unsigned char *req,
				     unsigned char *out, unsigned int cap)
{
	unsigned int total = g.head_len + g.tail_len;

	if (g.head_len < MLME_HDR_LEN || cap < total)
		return 0u;
	memcpy(out, g.head, g.head_len);
	wr16(out, MLME_ST_PROBE_RESP);
	memcpy(out + 4, req + 10, 6);		/* DA = the requesting STA */
	memset(out + 24, 0, 8);			/* timestamp */
	wr16(out + 22, (unsigned int)next_seq());
	memcpy(out + g.head_len, g.tail, g.tail_len);
	return total;
}

/*
 * The assoc response echoes the AP's own IE set: supported rates come from the
 * head (hostapd-style head carries SSID + rates + DS), the rest from the tail.
 * HT/WMM are only echoed when the station asked for them, as hostapd does for
 * a non-HT/non-WMM station.
 */
static unsigned int build_assoc_resp(const unsigned char *req, uint16_t aid,
				     int ht, int wmm, unsigned char *out,
				     unsigned int cap)
{
	unsigned int pos;
	uint16_t capab = rd16(g.head + 34);

	pos = build_hdr(out, MLME_ST_ASSOC_RESP, req + 10, cap);
	if (pos == 0u || cap < pos + 6u)
		return 0u;
	wr16(out + pos, capab);
	wr16(out + pos + 2u, 0u);		/* status: success */
	wr16(out + pos + 4u,
	     (unsigned int)(aid | MLME_AID_RESERVED));
	pos += 6u;
	pos = append_ie(out, cap, pos, g.head, g.head_len, MLME_HDR_LEN + 12u,
			MLME_EID_RATES);
	pos = append_ie(out, cap, pos, g.tail, g.tail_len, 0u, 50u);
	if (ht) {
		pos = append_ie(out, cap, pos, g.tail, g.tail_len, 0u,
				MLME_EID_HT_CAP);
		pos = append_ie(out, cap, pos, g.tail, g.tail_len, 0u,
				MLME_EID_HT_OP);
	}
	pos = append_ie(out, cap, pos, g.tail, g.tail_len, 0u,
			MLME_EID_EXT_CAP);
	if (wmm)
		pos = append_ie(out, cap, pos, g.tail, g.tail_len, 0u,
				MLME_EID_WMM);
	return pos;
}

/* --- handlers ------------------------------------------------------------ */

static int handle_probe_req(const unsigned char *f, unsigned int len)
{
	const unsigned char *ies = f + MLME_HDR_LEN;
	unsigned int ies_len = len - MLME_HDR_LEN;
	const unsigned char *ssid = NULL;
	unsigned int ssid_len = 0u;
	unsigned char out[MLME_RESP_MAX];
	unsigned int n;
	char mac[18];
	int rc;

	if (find_ie(ies, ies_len, MLME_EID_SSID, &ssid, &ssid_len) == 0 &&
	    ssid_len != 0u) {
		if (ssid_len != g.ssid_len ||
		    memcmp(ssid, g.ssid, g.ssid_len) != 0)
			return 0;	/* directed probe for another SSID */
	}

	g.st.probe_req++;
	mac_str(mac, sizeof(mac), f + 10);
	emit_line("mlme rx probe_req n=%u sa=%s ssid=%s", g.st.probe_req, mac,
		  ssid_len == 0u ? "wildcard" : "match");

	n = build_probe_resp(f, out, sizeof(out));
	if (n == 0u) {
		g.st.probe_fail++;
		emit_line("mlme tx probe_resp n=%u len=0 rc=%d",
			  g.st.probe_req, -EINVAL);
		return -EINVAL;
	}
	rc = (g.ops != NULL && g.ops->mgmt_tx != NULL) ?
		g.ops->mgmt_tx(out, n) : -EOPNOTSUPP;
	if (rc != 0)
		g.st.probe_fail++;
	else
		g.st.probe_resp++;
	emit_line("mlme tx probe_resp n=%u len=%u rc=%d",
		  g.st.probe_req, n, rc);
	return rc;
}

static int handle_auth(const unsigned char *f, unsigned int len)
{
	unsigned char out[MLME_HDR_LEN + 6u];
	unsigned int n;
	unsigned int algo;
	unsigned int seq;
	char mac[18];
	int rc;

	if (len < MLME_HDR_LEN + 6u) {
		g.st.drop_bad++;
		return -EINVAL;
	}
	algo = rd16(f + MLME_HDR_LEN);
	seq = rd16(f + MLME_HDR_LEN + 2u);
	if (memcmp(f + 16, g.ap, 6) != 0)
		return -EOPNOTSUPP;	/* not our BSS */
	if (algo != 0u) {
		g.st.drop_unsupported++;
		return -EOPNOTSUPP;
	}

	g.st.auth_req++;
	mac_str(mac, sizeof(mac), f + 10);
	emit_line("mlme rx auth n=%u sa=%s algo=%u seq=%u", g.st.auth_req, mac,
		  algo, seq);

	n = build_auth_resp(f, out, sizeof(out));
	rc = (n != 0u && g.ops != NULL && g.ops->mgmt_tx != NULL) ?
		g.ops->mgmt_tx(out, n) : -EINVAL;
	if (rc != 0)
		g.st.auth_fail++;
	else
		g.st.auth_resp++;
	emit_line("mlme tx auth_resp n=%u rc=%d", g.st.auth_req, rc);
	return rc;
}

static int handle_assoc(const unsigned char *f, unsigned int len, int reassoc)
{
	unsigned int body = MLME_HDR_LEN + 4u + (reassoc ? 6u : 0u);
	const unsigned char *ies;
	unsigned int ies_len;
	const unsigned char *rates1 = NULL, *rates50 = NULL, *ht = NULL;
	unsigned int len1 = 0u, len50 = 0u, len_ht = 0u;
	const unsigned char *wmm = NULL;
	unsigned int wmm_len = 0u;
	unsigned char rates[32];
	unsigned int rates_len = 0u;
	struct lkpi_ap_sta_add add;
	struct mlme_sta *st;
	unsigned char out[MLME_RESP_MAX];
	unsigned int n;
	char mac[18];
	int rc;

	if (len < body) {
		g.st.drop_bad++;
		return -EINVAL;
	}
	ies = f + body;
	ies_len = len - body;
	g.st.assoc_req++;

	memset(&add, 0, sizeof(add));
	memcpy(add.mac, f + 10, 6);
	add.capab = rd16(f + MLME_HDR_LEN);
	add.listen_interval = rd16(f + MLME_HDR_LEN + 2u);

	if (find_ie(ies, ies_len, MLME_EID_RATES, &rates1, &len1) == 0 &&
	    len1 <= 8u) {
		memcpy(rates + rates_len, rates1, len1);
		rates_len += len1;
	}
	if (find_ie(ies, ies_len, 50u, &rates50, &len50) == 0 &&
	    len50 <= 8u && rates_len + len50 <= sizeof(rates)) {
		memcpy(rates + rates_len, rates50, len50);
		rates_len += len50;
	}
	if (find_ie(ies, ies_len, MLME_EID_HT_CAP, &ht, &len_ht) != 0 ||
	    len_ht != MLME_HT_CAP_LEN)
		ht = NULL;
	find_wmm(ies, ies_len, &wmm, &wmm_len);

	st = sta_find(f + 10);
	if (st == NULL)
		st = sta_alloc(f + 10);
	if (st == NULL) {
		g.st.sta_add_fail++;
		emit_line("mlme sta add n=%u mac=%s aid=0 rc=%d",
			  g.st.sta_add_fail, "table-full", -ENOSPC);
		return -ENOSPC;
	}
	add.aid = st->aid;
	add.rates = rates_len != 0u ? rates : NULL;
	add.rates_len = rates_len;
	add.ht_capa = ht;
	add.wmm = wmm;

	mac_str(mac, sizeof(mac), f + 10);
	emit_line("mlme rx assoc n=%u sa=%s subtype=%u rates=%u ht=%u wme=%u",
		  g.st.assoc_req, mac, reassoc ? 2u : 0u, rates_len,
		  ht != NULL ? 1u : 0u, wmm != NULL ? 1u : 0u);

	/* hostapd adds the station before the response; a retry reuses it. */
	if (!st->added) {
		rc = (g.ops != NULL && g.ops->new_station != NULL) ?
			g.ops->new_station(&add) : -EOPNOTSUPP;
		if (rc == 0) {
			st->added = 1u;
			g.st.sta_add++;
		} else {
			g.st.sta_add_fail++;
		}
		emit_line("mlme sta add n=%u mac=%s aid=%u rc=%d",
			  g.st.sta_add + g.st.sta_add_fail, mac, st->aid, rc);
		if (rc != 0) {
			g.st.assoc_fail++;
			return rc;
		}
	}

	if (!st->authorized) {
		rc = (g.ops != NULL && g.ops->set_authorized != NULL) ?
			g.ops->set_authorized(f + 10) : -EOPNOTSUPP;
		if (rc == 0) {
			st->authorized = 1u;
			g.st.sta_auth++;
		} else {
			g.st.sta_auth_fail++;
		}
		emit_line("mlme sta authorized n=%u mac=%s rc=%d",
			  g.st.sta_auth + g.st.sta_auth_fail, mac, rc);
		if (rc != 0) {
			g.st.assoc_fail++;
			return rc;
		}
	}

	n = build_assoc_resp(f, st->aid, ht != NULL, wmm != NULL, out,
			     sizeof(out));
	rc = (n != 0u && g.ops != NULL && g.ops->mgmt_tx != NULL) ?
		g.ops->mgmt_tx(out, n) : -EINVAL;
	if (rc != 0) {
		g.st.assoc_fail++;
		g.st.tx_fail++;
	} else {
		g.st.assoc_resp++;
	}
	emit_line("mlme tx assoc_resp n=%u len=%u rc=%d", g.st.assoc_req, n, rc);
	return rc;
}

/* --- public API ---------------------------------------------------------- */

int lkpi_ap_mlme_handle_frame(const unsigned char *frame, unsigned int len)
{
	unsigned int fc;
	unsigned int subtype;

	if (frame == NULL || len < MLME_HDR_LEN)
		return -EINVAL;
	fc = rd16(frame);
	if ((fc & MLME_TYPE_MASK) != 0u)
		return -EOPNOTSUPP;
	subtype = fc & MLME_SUBTYPE_MASK;

	switch (subtype) {
	case MLME_ST_PROBE_REQ:
		return handle_probe_req(frame, len);
	case MLME_ST_AUTH:
		return handle_auth(frame, len);
	case MLME_ST_ASSOC_REQ:
		return handle_assoc(frame, len, 0);
	case MLME_ST_REASSOC_REQ:
		return handle_assoc(frame, len, 1);
	default:
		g.st.drop_unsupported++;
		return -EOPNOTSUPP;
	}
}

int lkpi_ap_mlme_configure(const char *ssid, const unsigned char *head,
			   unsigned int head_len, const unsigned char *tail,
			   unsigned int tail_len,
			   const struct lkpi_ap_mlme_ops *ops)
{
	unsigned int ssid_len;

	if (ssid == NULL || head == NULL || tail == NULL || ops == NULL)
		return -EINVAL;
	if (g.configured)
		return -EBUSY;
	if (head_len < MLME_HDR_LEN + 12u || head_len > sizeof(g.head) ||
	    tail_len > sizeof(g.tail))
		return -EINVAL;
	ssid_len = (unsigned int)strlen(ssid);
	if (ssid_len > sizeof(g.ssid))
		return -EINVAL;

	memset(&g, 0, sizeof(g));
	spin_lock_init(&g.lock);
	g.ops = ops;
	memcpy(g.ssid, ssid, ssid_len);
	g.ssid_len = ssid_len;
	memcpy(g.ap, head + 10, 6);
	memcpy(g.head, head, head_len);
	g.head_len = head_len;
	memcpy(g.tail, tail, tail_len);
	g.tail_len = tail_len;
	g.seq = 1u;
	g.configured = 1u;
	emit_line("mlme ready ssid=%s head=%u tail=%u", ssid, head_len, tail_len);
	return 0;
}

void lkpi_ap_mlme_stop(void)
{
	memset(&g, 0, sizeof(g));
}

void lkpi_ap_mlme_enable(int on)
{
	g.enabled = on != 0;
}

int lkpi_ap_mlme_enabled(void)
{
	return g.enabled != 0;
}

int lkpi_ap_mlme_rx_push(const unsigned char *frame, unsigned int len)
{
	unsigned long long flags;
	unsigned int slot;

	if (frame == NULL || len < MLME_HDR_LEN ||
	    len > LKPI_AP_MLME_FRAME_MAX)
		return -EINVAL;

	spin_lock_irqsave(&g.lock, flags);
	if (g.q_count == LKPI_AP_MLME_QUEUE_LEN) {
		g.st.q_full++;
		spin_unlock_irqrestore(&g.lock, flags);
		return -ENOSPC;
	}
	slot = (g.q_head + g.q_count) % LKPI_AP_MLME_QUEUE_LEN;
	g.q[slot].len = (uint16_t)len;
	memcpy(g.q[slot].data, frame, len);
	g.q_count++;
	spin_unlock_irqrestore(&g.lock, flags);
	return 0;
}

int lkpi_ap_mlme_poll(void)
{
	unsigned char frame[LKPI_AP_MLME_FRAME_MAX];
	unsigned long long flags;
	unsigned int len = 0u;
	int rc;

	if (!g.enabled)
		return 0;
	spin_lock_irqsave(&g.lock, flags);
	if (g.q_count == 0u) {
		spin_unlock_irqrestore(&g.lock, flags);
		return 0;
	}
	len = g.q[g.q_head].len;
	memcpy(frame, g.q[g.q_head].data, len);
	g.q_head = (g.q_head + 1u) % LKPI_AP_MLME_QUEUE_LEN;
	g.q_count--;
	spin_unlock_irqrestore(&g.lock, flags);

	rc = lkpi_ap_mlme_handle_frame(frame, len);
	if (rc == -EINVAL)
		g.st.drop_bad++;
	return (rc == -EINVAL) ? -EINVAL : 1;
}

void lkpi_ap_mlme_stats_get(struct lkpi_ap_mlme_stats *out)
{
	unsigned long long flags;

	if (out == NULL)
		return;
	spin_lock_irqsave(&g.lock, flags);
	*out = g.st;
	spin_unlock_irqrestore(&g.lock, flags);
}

unsigned int lkpi_ap_mlme_sta_count(void)
{
	unsigned int i;
	unsigned int n = 0u;

	for (i = 0u; i < LKPI_AP_MLME_STA_MAX; i++)
		if (g.sta[i].used && g.sta[i].added)
			n++;
	return n;
}
