/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    main.c
 * @brief   The cfg80211 substitute suite: the downward seam driven by a stand-in
 *          driver whose ops table records every call.
 *
 * @design ports & adapters, the test-suite side of the same seam the component
 *         documents (components/linuxkpi/linuxkpi_cfg80211.h:1-28). The suite is
 *         the seam's consumer: it installs a consumer-ops record, registers a
 *         wiphy and a net device through the substitute, and then drives the
 *         downward entry points (lkpicfg_connect / lkpicfg_set_key / ...) exactly
 *         as the board test does - asserting what the driver's own cfg80211_ops
 *         saw, so the parameter building the substitute performs is checked where
 *         it happens rather than inferred from a chip. The case scaffolding and
 *         the anchor-line report mirror the sibling single-file harness
 *         (tests/host/linuxkpi-body-test/main.c:1-37); the red-control wrapper
 *         mirrors tools/mock/linuxkpi-body-test/check.sh:1-40.
 *
 * Cases (each prints one `CFG: <ID> PASS|FAIL <detail>` line):
 *
 *   C1  wiphy lifecycle: wiphy_new_nm + wiphy_register make lkpicfg_wiphy()
 *       return the wiphy and fire the consumer's attached callback.
 *   C2  BSS table + element stream: a synthetic beacon's identity, name,
 *       frequency, signal and - the point - its element stream come back out of
 *       lkpicfg_bss_at, with the RSN element findable inside it.
 *   C3  connect parameter building: a WPA2-PSK RSN element maps to every field
 *       the driver's connect op reads, including the group cipher (a zero there
 *       silently disables the driver's encryption).
 *   C4  WPA1 element mapping: the 00:50:f2 vendor element's selectors are mapped
 *       into the 802.11 00:0f:ac selector space, not passed through.
 *   C5  open network: a NULL ie yields no security selection at all.
 *   C6  connect refusals: a NULL SSID and an over-long SSID are refused before
 *       the driver's op runs.
 *   C7  set_key: the CCMP key material, sequence and identity are passed through;
 *       an empty key is refused without reaching the driver.
 *   C8  disconnect: the reason code reaches the driver.
 *   C9  scan: the built request names the wiphy and the driver's own wdev and
 *       carries the advertised channel set.
 *   C10 truncated element: a stream that runs out mid-way still reaches the
 *       driver, with the fields the parse could read (the group cipher and the
 *       first pairwise suite) and the version and AKM list left unset.
 *   C11 ap start: the ordering contract (the interface reads AP inside the op,
 *       because the driver switches the card's mode from that field before it
 *       takes the beacon), the beacon/SSID pass-through, and the refusals (a
 *       head that cannot hold an 802.11 header, an empty SSID) that must never
 *       reach the driver.
 *   C12 ap stop: the op runs with link_id 0 and the interface's recorded type
 *       returns to station.
 *   C13 beacon head: the 24 zero header bytes, the zero timestamp (what keeps
 *       the network open), the interval and the capability - each byte named.
 *   C14 beacon tail: the element stream byte for byte against the capture's
 *       oracle (the template's elements with the name and channel substituted).
 *   C15 station events: cfg80211_new_sta/del_sta reach the consumer with the
 *       wiphy and the address.
 *
 * A failure prints the case line with `FAIL` and a detail, the run stops there,
 * and the exit code is non-zero. `--fault weak-expect` makes C3 compare the WPA
 * version against 0 instead of NL80211_WPA_VERSION_2, `--fault ap-order` makes
 * C11 expect the wrong interface type inside the op, `--fault ap-head` one byte
 * less of beacon tail than was handed in, `--fault ap-beacon` corrupts one byte
 * of the beacon oracle, `--fault ap-ie-len` shortens one element's declared
 * length and `--fault sta-event` expects one station event too many, so the
 * suite proves those checks can go red.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"
#include "linuxkpi_core.h"
#include "linuxkpi_impl.h"
#include "linuxkpi_cfg80211.h"
#include "skbuff/skbuff_pool.h"

#include <linux/ieee80211.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>

#include "wlan8189_ap_beacon.h"

/* ------------------------------------------------------------------ */
/* the host platform hooks the pool body declares                      */
/* ------------------------------------------------------------------ */

/*
 * The pool body's platform bindings (skbuff/skbuff_pool.h:144-148) are the only
 * OS-facing surface under it; this suite supplies the host spellings the way the
 * sibling body suite does (tests/host/linuxkpi-body-test/lkpi_platform_host.c:35-74):
 * the critical section is a single-threaded no-op, the allocator is libc, and the
 * pool's own log goes to the same sink the component's lines do.
 */
unsigned long
lkpi_spl_irqsave(void)
{

	return 0;		/* single-threaded host run */
}

void
lkpi_spl_irqrestore(unsigned long flags)
{

	(void)flags;
}

void
lkpi_spl_log(const char *fmt, ...)
{
	va_list ap;

	fputs("lkpi_skb: ", stdout);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
	fflush(stdout);
}

void *
lkpi_spl_malloc(unsigned int size)
{

	return malloc(size != 0 ? size : 1u);
}

void
lkpi_spl_free(void *p)
{

	free(p);
}

/* the fragment release's page binding (skbuff_pool.h): the harness has no page
 * allocator of its own - a page in these suites is whatever the test hands the
 * pool - so the reference's return is a no-op here */
void
lkpi_spl_page_put(struct page *page)
{

	(void)page;
}

/* the console sink, so the component's own lines stay visible in the log */
static void
lkpi_platform_sink(ulog_level_t level, const char *line)
{

	(void)level;
	if (line == NULL)
		return;
	fputs("lkpi: ", stdout);
	fputs(line, stdout);
	if (line[0] != '\0' && line[strlen(line) - 1] != '\n')
		fputc('\n', stdout);
	fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* case scaffolding                                                    */
/* ------------------------------------------------------------------ */

static unsigned int g_cases;
static unsigned int g_fail;
static const char *g_fault;

static void
case_line(const char *id, int ok, const char *fmt, ...)
{
	char detail[200];
	va_list ap;

	detail[0] = '\0';
	if (fmt != NULL && *fmt != '\0') {
		va_start(ap, fmt);
		vsnprintf(detail, sizeof(detail), fmt, ap);
		va_end(ap);
	}
	g_cases++;
	if (!ok)
		g_fail++;
	printf("CFG: %s %s%s%s\n", id, ok ? "PASS" : "FAIL",
	    detail[0] != '\0' ? " " : "", detail);
	fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* the stand-in driver: an ops table that records what it was called    */
/* ------------------------------------------------------------------ */

static struct wiphy *g_wiphy;
static struct net_device *g_netdev;
static struct wireless_dev g_wdev;

static unsigned int g_attached;
static struct wiphy *g_attached_wiphy;

static void
consumer_attached(struct wiphy *wiphy)
{

	g_attached++;
	g_attached_wiphy = wiphy;
}

/* the station events: what the consumer receives is the whole claim of the
 * forwarding, so both halves are recorded (the wiphy and the address) */
static unsigned int g_sta_joined;
static unsigned int g_sta_left;
static struct wiphy *g_sta_wiphy;
static u8 g_sta_mac[6];

static void
consumer_sta_joined(struct wiphy *wiphy, const u8 mac[6])
{

	g_sta_joined++;
	g_sta_wiphy = wiphy;
	memcpy(g_sta_mac, mac, sizeof(g_sta_mac));
}

static void
consumer_sta_left(struct wiphy *wiphy, const u8 mac[6])
{

	(void)wiphy;
	(void)mac;
	g_sta_left++;
}

static const struct lkpicfg_consumer_ops g_consumer = {
	.attached = consumer_attached,
	.sta_joined = consumer_sta_joined,
	.sta_left = consumer_sta_left,
};

struct rec_connect {
	unsigned int			calls;
	struct net_device		*dev;
	const u8			*ssid;
	size_t				ssid_len;
	const u8			*bssid;
	enum nl80211_auth_type		auth_type;
	const u8			*ie;
	size_t				ie_len;
	bool				privacy;
	struct cfg80211_crypto_settings	crypto;
};

struct rec_disconnect {
	unsigned int	calls;
	u16		reason;
};

struct rec_key {
	unsigned int		calls;
	struct wireless_dev	*wdev;
	int			link_id;
	u8			key_index;
	bool			pairwise;
	const u8		*mac_addr;
	struct key_params	params;
};

struct rec_scan {
	unsigned int		calls;
	struct wiphy		*wiphy;
	struct wireless_dev	*wdev;
	u32			n_channels;
	int			n_ssids;
};

static struct rec_connect	g_connect;
static struct rec_disconnect	g_disconnect;
static struct rec_key		g_key;
static struct rec_scan		g_scan;

static int
drv_connect(struct wiphy *wiphy, struct net_device *dev,
    struct cfg80211_connect_params *sme)
{

	(void)wiphy;
	g_connect.calls++;
	g_connect.dev = dev;
	g_connect.ssid = sme->ssid;
	g_connect.ssid_len = sme->ssid_len;
	g_connect.bssid = sme->bssid;
	g_connect.auth_type = sme->auth_type;
	g_connect.ie = sme->ie;
	g_connect.ie_len = sme->ie_len;
	g_connect.privacy = sme->privacy;
	g_connect.crypto = sme->crypto;
	return 0;
}

static int
drv_disconnect(struct wiphy *wiphy, struct net_device *dev, u16 reason_code)
{

	(void)wiphy;
	(void)dev;
	g_disconnect.calls++;
	g_disconnect.reason = reason_code;
	return 0;
}

static int
drv_add_key(struct wiphy *wiphy, struct wireless_dev *wdev, int link_id,
    u8 key_index, bool pairwise, const u8 *mac_addr, struct key_params *params)
{

	(void)wiphy;
	g_key.calls++;
	g_key.wdev = wdev;
	g_key.link_id = link_id;
	g_key.key_index = key_index;
	g_key.pairwise = pairwise;
	g_key.mac_addr = mac_addr;
	g_key.params = *params;
	return 0;
}

static int
drv_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
{

	(void)wiphy;
	g_scan.calls++;
	g_scan.wiphy = request->wiphy;
	g_scan.wdev = request->wdev;
	g_scan.n_channels = request->n_channels;
	g_scan.n_ssids = request->n_ssids;
	return 0;
}

/* the access-point ops: what they record is what the ordering contract is made
 * of - the interface type the op itself sees (the driver switches the card from
 * it, so AP must already be set when start_ap runs) and the beacon the caller
 * handed over */
struct rec_ap {
	unsigned int		start_calls;
	unsigned int		stop_calls;
	struct net_device	*dev;
	struct wireless_dev	*wdev;
	unsigned int		iftype_at_call;
	const u8		*head;
	size_t			head_len;
	const u8		*tail;
	size_t			tail_len;
	const u8		*ssid;
	size_t			ssid_len;
	int			auth_type;
	int			hidden_ssid;
	unsigned int		stop_link_id;
};

static struct rec_ap	g_ap;

static int
drv_start_ap(struct wiphy *wiphy, struct net_device *dev,
    struct cfg80211_ap_settings *settings)
{

	(void)wiphy;
	g_ap.start_calls++;
	g_ap.dev = dev;
	g_ap.wdev = dev->ieee80211_ptr;
	g_ap.iftype_at_call = (unsigned int)dev->ieee80211_ptr->iftype;
	g_ap.head = settings->beacon.head;
	g_ap.head_len = settings->beacon.head_len;
	g_ap.tail = settings->beacon.tail;
	g_ap.tail_len = settings->beacon.tail_len;
	g_ap.ssid = settings->ssid;
	g_ap.ssid_len = settings->ssid_len;
	g_ap.auth_type = (int)settings->auth_type;
	g_ap.hidden_ssid = (int)settings->hidden_ssid;
	return 0;
}

static int
drv_stop_ap(struct wiphy *wiphy, struct net_device *dev, unsigned int link_id)
{

	(void)wiphy;
	(void)dev;
	g_ap.stop_calls++;
	g_ap.stop_link_id = link_id;
	return 0;
}

static const struct cfg80211_ops g_drv_ops = {
	.connect = drv_connect,
	.disconnect = drv_disconnect,
	.add_key = drv_add_key,
	.scan = drv_scan,
	.start_ap = drv_start_ap,
	.stop_ap = drv_stop_ap,
};

/* ------------------------------------------------------------------ */
/* the frames and elements the cases hand to the substitute            */
/* ------------------------------------------------------------------ */

/*
 * The elements are wire-faithful: their two-byte counts are little-endian and
 * their four-byte cipher/AKM selectors are in wire order, exactly as the port's
 * own supplicant builds them (IEEE 802.11-2020; the imported net80211 reads the
 * same two spellings with LE_READ_2 and rsn_cipher at
 * components/net80211/net80211/ieee80211_input.c:1859-1886).
 */

/* A WPA2-PSK RSN element body: version 1, group CCMP (00:0f:ac:04), one
 * pairwise CCMP, one AKM PSK (00:0f:ac:02), zero RSN capabilities. */
static const u8 g_rsn_body[] = {
	0x01, 0x00,			/* version 1 */
	0x00, 0x0f, 0xac, 0x04,		/* group cipher: CCMP */
	0x01, 0x00,			/* pairwise count 1 (little-endian) */
	0x00, 0x0f, 0xac, 0x04,		/* pairwise: CCMP */
	0x01, 0x00,			/* AKM count 1 (little-endian) */
	0x00, 0x0f, 0xac, 0x02,		/* AKM: PSK */
	0x00, 0x00,			/* RSN capabilities */
};

/* A WPA1 element body (id 221, OUI 00:50:f2, type 1): the same field order, its
 * selectors spelled 00:50:f2:02 (TKIP for the ciphers, PSK for the AKM). */
static const u8 g_wpa_body[] = {
	0x00, 0x50, 0xf2, 0x01,		/* OUI + type */
	0x01, 0x00,			/* version 1 */
	0x00, 0x50, 0xf2, 0x02,		/* group cipher: TKIP */
	0x01, 0x00,			/* pairwise count 1 (little-endian) */
	0x00, 0x50, 0xf2, 0x02,		/* pairwise: TKIP */
	0x01, 0x00,			/* AKM count 1 (little-endian) */
	0x00, 0x50, 0xf2, 0x02,		/* AKM: PSK */
};

/* A deliberately truncated RSN element body: the pairwise count claims two
 * suites but only one selector is present, so the parse cannot reach the AKM
 * list.  C10 pins what the substitute does with it. */
static const u8 g_rsn_trunc_body[] = {
	0x01, 0x00,			/* version 1 */
	0x00, 0x0f, 0xac, 0x04,		/* group cipher: CCMP */
	0x02, 0x00,			/* pairwise count 2, one selector follows */
	0x00, 0x0f, 0xac, 0x04,		/* pairwise: CCMP */
};

static const u8 g_bssid[ETH_ALEN] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const u8 g_ssid[] = "CFG-TEST-NET";

/*
 * The beacon a caller assembles for the access point: 24 header bytes plus the
 * 12 fixed bytes (timestamp, beacon interval, capability), then the elements.
 * The fixed bytes are all zero here on purpose - the driver reads its
 * capability from the stream's first two bytes, i.e. the timestamp
 * (third-party/rtl8189fs/core/rtw_ap.c:1979), so a zero timestamp is what keeps
 * the network open, while the capability that goes on air is the one at
 * offset 10.
 */
static u8 g_beacon_head[36];

/* SSID + supported rates + DS: the minimum the driver's parser needs to find a
 * network name, a rate set and a channel */
static const u8 g_beacon_tail[] = {
	0x00, 0x0b, 'C', 'F', 'G', '-', 'T', 'E', 'S', 'T', '-', 'A', 'P',
	0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
	0x03, 0x01, 0x01,
};

static const u8 g_ap_ssid[] = "CFG-TEST-AP";
#define SSID_LEN	(sizeof(g_ssid) - 1)

/* the 2.4 GHz band the driver fills in before registering */
static struct ieee80211_channel g_channels[] = {
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2412, .hw_value = 1 },
};
static struct ieee80211_supported_band g_band = {
	.channels = g_channels,
	.n_channels = 1,
	.band = NL80211_BAND_2GHZ,
};

/* the beacon frame is built in this buffer: the mgmt fixed fields plus the
 * element stream the substitute copies out */
#define FRAME_BUF	256
static u8 g_frame[FRAME_BUF];

static size_t
build_rsn_elem(u8 *out)
{

	out[0] = WLAN_EID_RSN;
	out[1] = (u8)sizeof(g_rsn_body);
	memcpy(out + 2, g_rsn_body, sizeof(g_rsn_body));
	return 2 + sizeof(g_rsn_body);
}

static size_t
build_wpa_elem(u8 *out)
{

	out[0] = WLAN_EID_VENDOR_SPECIFIC;
	out[1] = (u8)sizeof(g_wpa_body);
	memcpy(out + 2, g_wpa_body, sizeof(g_wpa_body));
	return 2 + sizeof(g_wpa_body);
}

static size_t
build_rsn_trunc_elem(u8 *out)
{

	out[0] = WLAN_EID_RSN;
	out[1] = (u8)sizeof(g_rsn_trunc_body);
	memcpy(out + 2, g_rsn_trunc_body, sizeof(g_rsn_trunc_body));
	return 2 + sizeof(g_rsn_trunc_body);
}

/* Build a beacon/probe-response: BSSID in addr3, the capability word, then an
 * SSID element followed by an RSN element. Returns the frame length, which is
 * the fixed prefix the substitute measures the element stream against. */
static size_t
build_beacon(u16 capab)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)(void *)g_frame;
	u8 *ies;
	size_t ielen;

	memset(g_frame, 0, sizeof(g_frame));
	mgmt->frame_control =
	    cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON);
	memcpy(mgmt->bssid, g_bssid, ETH_ALEN);
	mgmt->u.beacon.capab_info = cpu_to_le16(capab);

	ies = mgmt->u.beacon.variable;
	ies[0] = WLAN_EID_SSID;
	ies[1] = (u8)SSID_LEN;
	memcpy(ies + 2, g_ssid, SSID_LEN);
	ielen = 2 + SSID_LEN;
	ielen += build_rsn_elem(ies + ielen);

	return offsetofend(struct ieee80211_mgmt, u.beacon.capab_info) + ielen;
}

static const u8 *
find_ie(const u8 *ies, size_t ielen, u8 id)
{
	size_t i = 0;

	while (i + 2 <= ielen) {
		u8 elen = ies[i + 1];

		if (i + 2 + (size_t)elen > ielen)
			break;
		if (ies[i] == id)
			return ies + i;
		i += 2 + (size_t)elen;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* setup: register the wiphy and the interface through the substitute  */
/* ------------------------------------------------------------------ */

static int
setup(void)
{
	if (lkpicfg_consumer_register(&g_consumer, NULL) != 0)
		return -1;

	g_wiphy = wiphy_new_nm(&g_drv_ops, 0, NULL);
	if (g_wiphy == NULL)
		return -1;
	/* the driver fills perm_addr from the chip's eFuse before registering */
	memcpy(g_wiphy->perm_addr, g_bssid, ETH_ALEN);
	/* the 2.4 GHz band the scan request is built from */
	g_wiphy->bands[NL80211_BAND_2GHZ] = &g_band;
	if (wiphy_register(g_wiphy) != 0)
		return -1;

	/* the interface the driver registered for this wiphy: the substitute finds
	 * it through dev->ieee80211_ptr->wiphy, so the wireless-dev record must be
	 * set before register_netdev (the driver's own order) */
	g_netdev = alloc_etherdev_mqs(0, 1, 1);
	if (g_netdev == NULL)
		return -1;
	g_wdev.wiphy = g_wiphy;
	g_netdev->ieee80211_ptr = &g_wdev;
	if (register_netdev(g_netdev) != 0)
		return -1;
	return 0;
}

/* ------------------------------------------------------------------ */
/* the cases                                                           */
/* ------------------------------------------------------------------ */

static void
case_c1(void)
{
	int ok = 1;

	if (lkpicfg_wiphy() != g_wiphy)
		ok = 0;
	else if (g_attached != 1 || g_attached_wiphy != g_wiphy)
		ok = 0;
	case_line("C1", ok, "wiphy=%p attached=%u", (void *)lkpicfg_wiphy(),
	    g_attached);
}

static void
case_c2(void)
{
	struct lkpicfg_bss bss;
	size_t len = build_beacon(0x0411);
	size_t ielen = len - offsetofend(struct ieee80211_mgmt,
	    u.beacon.capab_info);
	int ok = 1;

	lkpicfg_bss_clear(g_wiphy);
	if (cfg80211_inform_bss_frame(g_wiphy, &g_channels[0],
	    (struct ieee80211_mgmt *)(void *)g_frame, len, -4200,
	    GFP_KERNEL) == NULL) {
		case_line("C2", 0, "inform_bss_frame returned NULL");
		return;
	}
	if (lkpicfg_bss_count(g_wiphy) != 1) {
		case_line("C2", 0, "bss_count=%u want 1",
		    lkpicfg_bss_count(g_wiphy));
		return;
	}
	if (lkpicfg_bss_at(g_wiphy, 0, &bss) != 0) {
		case_line("C2", 0, "bss_at(0) failed");
		return;
	}
	if (memcmp(bss.bssid, g_bssid, ETH_ALEN) != 0)
		ok = 0;
	else if (bss.ssid_len != SSID_LEN ||
	    memcmp(bss.ssid, g_ssid, SSID_LEN) != 0)
		ok = 0;
	else if (bss.freq != 2412)
		ok = 0;
	else if (bss.signal != -42)
		ok = 0;
	else if (bss.ies_len != ielen)
		ok = 0;
	else if (find_ie(bss.ies, bss.ies_len, WLAN_EID_RSN) == NULL)
		ok = 0;
	case_line("C2", ok, "ssid=%.*s freq=%u sig=%d ies_len=%u want=%u rsn=%s",
	    (int)bss.ssid_len, bss.ssid, (unsigned)bss.freq, bss.signal,
	    (unsigned)bss.ies_len, (unsigned)ielen,
	    find_ie(bss.ies, bss.ies_len, WLAN_EID_RSN) != NULL ? "found" :
	    "missing");
}

static void
case_c3(void)
{
	u8 ie[2 + sizeof(g_rsn_body)];
	size_t ie_len = build_rsn_elem(ie);
	unsigned int before = g_connect.calls;
	/* the red control compares the version against 0 instead of WPA2 */
	u32 want_ver = (g_fault != NULL &&
	    strcmp(g_fault, "weak-expect") == 0) ? 0 : NL80211_WPA_VERSION_2;
	int rc, ok = 1;

	rc = lkpicfg_connect(g_wiphy, g_ssid, SSID_LEN, g_bssid, ie, ie_len);
	if (rc != 0 || g_connect.calls != before + 1) {
		case_line("C3", 0, "connect rc=%d calls=%u", rc, g_connect.calls);
		return;
	}
	if (g_connect.ssid != g_ssid || g_connect.ssid_len != SSID_LEN)
		ok = 0;
	else if (g_connect.bssid != g_bssid)
		ok = 0;
	else if (g_connect.auth_type != NL80211_AUTHTYPE_OPEN_SYSTEM)
		ok = 0;
	else if (g_connect.ie != ie || g_connect.ie_len != ie_len)
		ok = 0;
	else if (!g_connect.privacy)
		ok = 0;
	else if (g_connect.crypto.wpa_versions != want_ver)
		ok = 0;
	else if (g_connect.crypto.cipher_group != WLAN_CIPHER_SUITE_CCMP)
		ok = 0;
	else if (g_connect.crypto.n_ciphers_pairwise != 1 ||
	    g_connect.crypto.ciphers_pairwise[0] != WLAN_CIPHER_SUITE_CCMP)
		ok = 0;
	else if (g_connect.crypto.n_akm_suites != 1 ||
	    g_connect.crypto.akm_suites[0] != WLAN_AKM_SUITE_PSK)
		ok = 0;

	case_line("C3", ok, "ver=0x%x group=0x%x npair=%d pair0=0x%x nakm=%d "
	    "akm0=0x%x auth=%d privacy=%d", (unsigned)g_connect.crypto.wpa_versions,
	    (unsigned)g_connect.crypto.cipher_group,
	    g_connect.crypto.n_ciphers_pairwise,
	    (unsigned)g_connect.crypto.ciphers_pairwise[0],
	    g_connect.crypto.n_akm_suites,
	    (unsigned)g_connect.crypto.akm_suites[0],
	    (int)g_connect.auth_type, (int)g_connect.privacy);
}

static void
case_c4(void)
{
	u8 ie[2 + sizeof(g_wpa_body)];
	size_t ie_len = build_wpa_elem(ie);
	unsigned int before = g_connect.calls;
	int rc, ok = 1;

	rc = lkpicfg_connect(g_wiphy, g_ssid, SSID_LEN, NULL, ie, ie_len);
	if (rc != 0 || g_connect.calls != before + 1) {
		case_line("C4", 0, "connect rc=%d calls=%u", rc, g_connect.calls);
		return;
	}
	if (g_connect.crypto.wpa_versions != NL80211_WPA_VERSION_1)
		ok = 0;
	else if (g_connect.crypto.cipher_group != WLAN_CIPHER_SUITE_TKIP)
		ok = 0;
	else if (g_connect.crypto.n_ciphers_pairwise != 1 ||
	    g_connect.crypto.ciphers_pairwise[0] != WLAN_CIPHER_SUITE_TKIP)
		ok = 0;
	else if (g_connect.crypto.n_akm_suites != 1 ||
	    g_connect.crypto.akm_suites[0] != WLAN_AKM_SUITE_PSK)
		ok = 0;

	case_line("C4", ok, "ver=0x%x group=0x%x pair0=0x%x akm0=0x%x",
	    (unsigned)g_connect.crypto.wpa_versions,
	    (unsigned)g_connect.crypto.cipher_group,
	    (unsigned)g_connect.crypto.ciphers_pairwise[0],
	    (unsigned)g_connect.crypto.akm_suites[0]);
}

static void
case_c5(void)
{
	unsigned int before = g_connect.calls;
	int rc, ok = 1;

	rc = lkpicfg_connect(g_wiphy, g_ssid, SSID_LEN, NULL, NULL, 0);
	if (rc != 0 || g_connect.calls != before + 1) {
		case_line("C5", 0, "connect rc=%d calls=%u", rc, g_connect.calls);
		return;
	}
	if (g_connect.crypto.wpa_versions != 0)
		ok = 0;
	else if (g_connect.crypto.cipher_group != 0)
		ok = 0;
	else if (g_connect.crypto.n_ciphers_pairwise != 0)
		ok = 0;
	else if (g_connect.crypto.n_akm_suites != 0)
		ok = 0;
	else if (g_connect.privacy)
		ok = 0;

	case_line("C5", ok, "ver=0x%x group=0x%x npair=%d nakm=%d privacy=%d",
	    (unsigned)g_connect.crypto.wpa_versions,
	    (unsigned)g_connect.crypto.cipher_group,
	    g_connect.crypto.n_ciphers_pairwise,
	    g_connect.crypto.n_akm_suites, (int)g_connect.privacy);
}

static void
case_c6(void)
{
	unsigned int before = g_connect.calls;
	int rc_null, rc_long, ok = 1;

	rc_null = lkpicfg_connect(g_wiphy, NULL, 5, NULL, NULL, 0);
	rc_long = lkpicfg_connect(g_wiphy, g_ssid, IEEE80211_MAX_SSID_LEN + 1,
	    NULL, NULL, 0);
	if (rc_null != -EINVAL)
		ok = 0;
	else if (rc_long != -EINVAL)
		ok = 0;
	else if (g_connect.calls != before)
		ok = 0;

	case_line("C6", ok, "rc_null=%d rc_long=%d calls_delta=%u", rc_null,
	    rc_long, g_connect.calls - before);
}

static void
case_c7(void)
{
	static const u8 key[16] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
	};
	static const u8 seq[6] = { 0, 0, 0, 0, 0, 1 };
	unsigned int before;
	int rc, ok = 1;

	before = g_key.calls;
	rc = lkpicfg_set_key(g_wiphy, 1, 0, NULL, key, sizeof(key), seq,
	    sizeof(seq));
	if (rc != 0 || g_key.calls != before + 1) {
		case_line("C7", 0, "set_key rc=%d calls=%u", rc, g_key.calls);
		return;
	}
	if (g_key.key_index != 1 || g_key.pairwise || g_key.mac_addr != NULL)
		ok = 0;
	else if (g_key.wdev != g_netdev->ieee80211_ptr)
		ok = 0;
	else if (g_key.params.cipher != WLAN_CIPHER_SUITE_CCMP)
		ok = 0;
	else if (g_key.params.key != key || g_key.params.key_len != 16)
		ok = 0;
	else if (g_key.params.seq != seq || g_key.params.seq_len != 6)
		ok = 0;

	/* an empty key is refused before the driver's op runs (the driver would
	 * otherwise mark the key installed and program stale material) */
	before = g_key.calls;
	rc = lkpicfg_set_key(g_wiphy, 0, 0, NULL, NULL, 0, NULL, 0);
	if (rc != -EINVAL || g_key.calls != before)
		ok = 0;

	case_line("C7", ok, "cipher=0x%x key_len=%d seq_len=%d idx=%u pair=%d "
	    "empty_rc=%d", (unsigned)g_key.params.cipher, g_key.params.key_len,
	    g_key.params.seq_len, g_key.key_index, (int)g_key.pairwise, rc);
}

static void
case_c8(void)
{
	unsigned int before = g_disconnect.calls;
	int rc, ok = 1;

	rc = lkpicfg_disconnect(g_wiphy, 3);
	if (rc != 0 || g_disconnect.calls != before + 1)
		ok = 0;
	else if (g_disconnect.reason != 3)
		ok = 0;

	case_line("C8", ok, "rc=%d reason=%u", rc, g_disconnect.reason);
}

static void
case_c9(void)
{
	unsigned int before = g_scan.calls;
	int rc, ok = 1;

	rc = lkpicfg_scan_start(g_wiphy);
	if (rc != 0 || g_scan.calls != before + 1) {
		case_line("C9", 0, "scan rc=%d calls=%u", rc, g_scan.calls);
		return;
	}
	if (g_scan.wiphy != g_wiphy)
		ok = 0;
	else if (g_scan.wdev != g_netdev->ieee80211_ptr)
		ok = 0;
	else if (g_scan.n_channels < 1)
		ok = 0;

	case_line("C9", ok, "rc=%d wiphy=%p wdev=%p n_channels=%u n_ssids=%d",
	    rc, (void *)g_scan.wiphy, (void *)g_scan.wdev,
	    (unsigned)g_scan.n_channels, g_scan.n_ssids);
}

/*
 * A malformed element must not take the connect down: the element parse is
 * best-effort (lkpicfg_parse_security tries each element and keeps what a
 * partial read produced), so the call still reaches the driver with the fields
 * it could read.  The version is set only after the whole element is consumed,
 * so a stream that runs out mid-way leaves it 0 - which is exactly the state the
 * driver reads as "no security" - and the AKM list is never reached.
 */
static void
case_c10(void)
{
	u8 ie[2 + sizeof(g_rsn_trunc_body)];
	size_t ie_len = build_rsn_trunc_elem(ie);
	unsigned int before = g_connect.calls;
	int rc, ok = 1;

	rc = lkpicfg_connect(g_wiphy, g_ssid, SSID_LEN, NULL, ie, ie_len);
	if (rc != 0 || g_connect.calls != before + 1) {
		case_line("C10", 0, "connect rc=%d calls=%u", rc, g_connect.calls);
		return;
	}
	/* what the parse read before the stream ran out */
	if (g_connect.crypto.cipher_group != WLAN_CIPHER_SUITE_CCMP)
		ok = 0;
	else if (g_connect.crypto.n_ciphers_pairwise != 1 ||
	    g_connect.crypto.ciphers_pairwise[0] != WLAN_CIPHER_SUITE_CCMP)
		ok = 0;
	/* what it could not: the version and the AKM list stay unset */
	else if (g_connect.crypto.wpa_versions != 0)
		ok = 0;
	else if (g_connect.crypto.n_akm_suites != 0)
		ok = 0;

	case_line("C10", ok, "rc=%d group=0x%x npair=%d pair0=0x%x ver=0x%x "
	    "nakm=%d", rc, (unsigned)g_connect.crypto.cipher_group,
	    g_connect.crypto.n_ciphers_pairwise,
	    (unsigned)g_connect.crypto.ciphers_pairwise[0],
	    (unsigned)g_connect.crypto.wpa_versions,
	    g_connect.crypto.n_akm_suites);
}

/*
 * C11 ap start: the ordering contract and the pass-through. The interface must
 * already read AP inside the op (the driver switches the card's mode from that
 * field before it takes the beacon), the beacon pointers and lengths reach the
 * driver unchanged, and the refusals - a head that cannot hold an 802.11 header,
 * an empty SSID - never reach it at all.
 */
static void
case_c11(void)
{
	unsigned int want_iftype = NL80211_IFTYPE_AP;
	size_t want_head = sizeof(g_beacon_head);
	size_t want_tail = sizeof(g_beacon_tail);
	int rc;
	int ok = 1;

	/* the red controls move the expectation, not the code: ap-order expects
	 * the wrong interface type inside the op, ap-head one byte less than was
	 * handed in */
	if (g_fault != NULL && strcmp(g_fault, "ap-order") == 0)
		want_iftype = NL80211_IFTYPE_STATION;
	if (g_fault != NULL && strcmp(g_fault, "ap-head") == 0)
		want_tail = sizeof(g_beacon_tail) - 1;

	/* the interface is a station before the call: the type the op sees is
	 * therefore the substitute's doing, not the fixture's */
	g_wdev.iftype = NL80211_IFTYPE_STATION;

	/* refusals first: neither may reach the driver */
	rc = lkpicfg_ap_start(g_wiphy, g_ap_ssid, sizeof(g_ap_ssid) - 1,
	    g_beacon_head, 12, g_beacon_tail, sizeof(g_beacon_tail));
	if (rc != -EINVAL || g_ap.start_calls != 0)
		ok = 0;
	rc = lkpicfg_ap_start(g_wiphy, NULL, 0, g_beacon_head,
	    sizeof(g_beacon_head), g_beacon_tail, sizeof(g_beacon_tail));
	if (rc != -EINVAL || g_ap.start_calls != 0)
		ok = 0;

	rc = lkpicfg_ap_start(g_wiphy, g_ap_ssid, sizeof(g_ap_ssid) - 1,
	    g_beacon_head, sizeof(g_beacon_head), g_beacon_tail,
	    sizeof(g_beacon_tail));
	if (rc != 0 || g_ap.start_calls != 1)
		ok = 0;
	else if (g_ap.iftype_at_call != want_iftype)
		ok = 0;
	else if (g_ap.head != g_beacon_head || g_ap.head_len != want_head)
		ok = 0;
	else if (g_ap.tail != g_beacon_tail || g_ap.tail_len != want_tail)
		ok = 0;
	else if (g_ap.ssid != g_ap_ssid ||
	    g_ap.ssid_len != sizeof(g_ap_ssid) - 1)
		ok = 0;
	else if (g_ap.auth_type != NL80211_AUTHTYPE_OPEN_SYSTEM)
		ok = 0;
	else if (g_ap.hidden_ssid != NL80211_HIDDEN_SSID_NOT_IN_USE)
		ok = 0;
	else if (g_ap.wdev != &g_wdev || g_ap.dev != g_netdev)
		ok = 0;

	case_line("C11", ok, "rc=%d calls=%u iftype=%u head=%u tail=%u ssid=%u",
	    rc, g_ap.start_calls, g_ap.iftype_at_call, (unsigned)g_ap.head_len,
	    (unsigned)g_ap.tail_len, (unsigned)g_ap.ssid_len);
}

/*
 * C12 ap stop: the op runs with link_id 0, and the interface's recorded type
 * returns to station - that field is the port's own record of the type and the
 * driver reads it to sync the card, so it must not stay at AP after the mode
 * has been left.
 */
static void
case_c12(void)
{
	int rc;
	int ok = 1;

	rc = lkpicfg_ap_stop(g_wiphy);
	if (rc != 0 || g_ap.stop_calls != 1)
		ok = 0;
	else if (g_ap.stop_link_id != 0)
		ok = 0;
	else if (g_wdev.iftype != NL80211_IFTYPE_STATION)
		ok = 0;

	case_line("C12", ok, "rc=%d calls=%u link_id=%u iftype=%u", rc,
	    g_ap.stop_calls, g_ap.stop_link_id, (unsigned)g_wdev.iftype);
}

/*
 * The oracle for the beacon assembly: the element stream the captured template
 * carried - the open hostapd access point on the measurement host's own radio
 * (the round's ledger holds the capture; this is its frame 7) - with the two
 * values this build chooses, the network name and the channel, substituted.
 * It is written here as the capture's bytes, deliberately not as a second run
 * of the builder, so C14 compares the one implementation against the air.
 */
static const u8 g_ap_tail_oracle[] = {
	0x00, 0x0b, 'C', 'F', 'G', '-', 'T', 'E', 'S', 'T', '-', 'A', 'P',
	0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
	0x03, 0x01, 0x01,
	0x2a, 0x01, 0x00,
	0x32, 0x04, 0x0c, 0x12, 0x18, 0x60,
	0x2d, 0x1a, 0x21, 0x00, 0x17, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,
	0x3d, 0x16, 0x01, 0x08, 0x11,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x80, 0x00, 0x03, 0xa4,
	0x00, 0x00, 0x27, 0xa4, 0x00, 0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32,
	0x2f, 0x00,
};

/*
 * C13 beacon head: 24 zero header bytes (the driver strips them), the zero
 * timestamp (the field the driver reads its capability from, which is what
 * keeps the network open), the interval and the capability - each byte named -
 * and a buffer one byte short refused rather than overrun.
 */
static void
case_c13(void)
{
	static const u8 want[WLAN8189_AP_BEACON_HEAD_LEN] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,		/* timestamp */
		0x64, 0x00,			/* interval 100 TU, LE */
		0x01, 0x15,			/* capability 0x1501, LE */
	};
	u8 buf[WLAN8189_AP_BEACON_HEAD_LEN + 8u];
	unsigned int n;
	int ok = 1;

	n = wlan8189_ap_beacon_head(buf, sizeof(buf), WLAN8189_AP_INTERVAL_TU,
	    WLAN8189_AP_CAPABILITY);
	if (n != WLAN8189_AP_BEACON_HEAD_LEN)
		ok = 0;
	else if (memcmp(buf, want, sizeof(want)) != 0)
		ok = 0;
	/* one byte short: refused, and nothing written past the caller's word */
	else if (wlan8189_ap_beacon_head(buf, WLAN8189_AP_BEACON_HEAD_LEN - 1u,
	    WLAN8189_AP_INTERVAL_TU, WLAN8189_AP_CAPABILITY) != 0)
		ok = 0;
	else if (wlan8189_ap_beacon_head(NULL, sizeof(buf),
	    WLAN8189_AP_INTERVAL_TU, WLAN8189_AP_CAPABILITY) != 0)
		ok = 0;

	case_line("C13", ok, "n=%u want=%u", n, WLAN8189_AP_BEACON_HEAD_LEN);
}

/*
 * C14 beacon tail: the assembled element stream walked element by element
 * (every declared length must fit and the ids must appear in the template's
 * order, with the stream fully consumed) and then compared byte for byte
 * against the capture's oracle, plus the refusals (no name, an over-long name,
 * a short buffer).
 *
 * The walk is the check that does not depend on the oracle being transcribed
 * correctly: a hand-copied expectation can be wrong in the same way as the
 * table it checks (it was, once: an element two bytes short passed the byte
 * comparison because both sides carried the same mistake). A declared length
 * that does not match its content, or a stream that does not end exactly where
 * the last element says, fails here whatever the oracle says.
 */
static void
case_c14(void)
{
	static const u8 want_ids[] = { 0x00, 0x01, 0x03, 0x2a, 0x32, 0x2d, 0x3d, 0xdd };
	u8 buf[160];
	u8 oracle[sizeof(g_ap_tail_oracle)];
	unsigned int n;
	unsigned int i;
	unsigned int ie;
	int ok = 1;

	memcpy(oracle, g_ap_tail_oracle, sizeof(oracle));
	if (g_fault != NULL && strcmp(g_fault, "ap-beacon") == 0)
		oracle[sizeof(oracle) - 1u] ^= 0x01u;

	n = wlan8189_ap_beacon_tail(buf, sizeof(buf), g_ap_ssid,
	    sizeof(g_ap_ssid) - 1u, 1u);

	/* the red control for the walk: shorten one element's declared length by
	 * a byte, the exact shape of the defect the walk exists for */
	if (g_fault != NULL && strcmp(g_fault, "ap-ie-len") == 0) {
		for (i = 0u; i + 1u < n; i++) {
			if (buf[i] == 0x2d && buf[i + 1u] == 26u) {
				buf[i + 1u] = 25u;
				break;
			}
		}
	}

	/* the walk */
	for (i = 0u, ie = 0u; ok && i < n; ie++) {
		unsigned int elen;

		if (i + 2u > n || ie >= sizeof(want_ids)) {
			ok = 0;
			break;
		}
		if (buf[i] != want_ids[ie]) {
			ok = 0;
			break;
		}
		elen = buf[i + 1u];
		if (i + 2u + elen > n) {
			ok = 0;
			break;
		}
		i += 2u + elen;
	}
	if (ok && i != n)
		ok = 0;			/* the stream must end where the last IE says */

	if (!ok) {
		case_line("C14", 0, "walk failed at %u", i);
		return;
	}

	if (n != sizeof(oracle))
		ok = 0;
	else if (memcmp(buf, oracle, sizeof(oracle)) != 0)
		ok = 0;
	/* refusals: no name, an over-long name, a buffer one byte short */
	else if (wlan8189_ap_beacon_tail(buf, sizeof(buf), g_ap_ssid, 0u, 1u) != 0)
		ok = 0;
	else if (wlan8189_ap_beacon_tail(buf, sizeof(buf), g_ap_ssid, 33u, 1u) != 0)
		ok = 0;
	else if (wlan8189_ap_beacon_tail(buf, sizeof(oracle) - 1u, g_ap_ssid,
	    sizeof(g_ap_ssid) - 1u, 1u) != 0)
		ok = 0;
	else if (wlan8189_ap_beacon_tail(NULL, sizeof(buf), g_ap_ssid,
	    sizeof(g_ap_ssid) - 1u, 1u) != 0)
		ok = 0;

	case_line("C14", ok, "n=%u want=%u", n, (unsigned)sizeof(oracle));
}

/*
 * C15 station events: the driver's AP-mode reports reach the consumer - the
 * wiphy and the address are passed through, and the two halves (joined, left)
 * are separate notifications. The driver's own call sites are
 * ioctl_cfg80211.c:4327/:4381, gated on RTW_USE_CFG80211_STA_EVENT, which this
 * port's builds define; the substitute used to drop both on the floor.
 */
static void
case_c15(void)
{
	static const u8 sta[6] = { 0x02, 0x00, 0x5e, 0x11, 0x22, 0x33 };
	unsigned int want_joined = 1;
	int ok = 1;

	/* the red control moves the expectation, not the code */
	if (g_fault != NULL && strcmp(g_fault, "sta-event") == 0)
		want_joined = 2;

	g_sta_joined = 0;
	g_sta_left = 0;
	g_sta_wiphy = NULL;

	cfg80211_new_sta(&g_wdev, sta, NULL, 0);
	cfg80211_del_sta(&g_wdev, sta, 0);

	if (g_sta_joined != want_joined || g_sta_left != 1)
		ok = 0;
	else if (g_sta_wiphy != g_wiphy)
		ok = 0;
	else if (memcmp(g_sta_mac, sta, sizeof(sta)) != 0)
		ok = 0;

	case_line("C15", ok, "joined=%u left=%u wiphy=%s", g_sta_joined,
	    g_sta_left, g_sta_wiphy == g_wiphy ? "match" : "mismatch");
}

/* ------------------------------------------------------------------ */
/* the run                                                             */
/* ------------------------------------------------------------------ */

static void
run_suite(void)
{
	lkpi_log_sink_register(lkpi_platform_sink);
	lkpi_core_init();

	if (setup() != 0) {
		printf("CFG: SETUP FAIL the substitute could not be brought up\n");
		fflush(stdout);
		g_fail = 1;
		g_cases = 1;
		goto done;
	}

	case_c1();
	if (g_fail != 0)
		goto done;
	case_c2();
	if (g_fail != 0)
		goto done;
	case_c3();
	if (g_fail != 0)
		goto done;
	case_c4();
	if (g_fail != 0)
		goto done;
	case_c5();
	if (g_fail != 0)
		goto done;
	case_c6();
	if (g_fail != 0)
		goto done;
	case_c7();
	if (g_fail != 0)
		goto done;
	case_c8();
	if (g_fail != 0)
		goto done;
	case_c9();
	if (g_fail != 0)
		goto done;
	case_c10();
	if (g_fail != 0)
		goto done;
	case_c11();
	if (g_fail != 0)
		goto done;
	case_c12();
	if (g_fail != 0)
		goto done;
	case_c13();
	if (g_fail != 0)
		goto done;
	case_c14();
	if (g_fail != 0)
		goto done;
	case_c15();

done:
	if (g_fail == 0)
		printf("CFG: SUITE PASS cases=%u\n", g_cases);
	else
		printf("CFG: SUITE FAIL failures=%u\n", g_fail);
	fflush(stdout);
}

static void
app_task(void *arg)
{

	(void)arg;
	run_suite();
	cmsis2_host_end_scheduler();
}

int
main(int argc, char **argv)
{
	osThreadAttr_t attr = { 0 };
	osThreadId_t t;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--fault") == 0 && i + 1 < argc) {
			g_fault = argv[++i];
		} else {
			printf("CFG: usage: %s [--fault weak-expect|ap-order|"
			    "ap-head|ap-beacon|ap-ie-len|sta-event]\n", argv[0]);
			return 2;
		}
	}
	if (g_fault != NULL && strcmp(g_fault, "weak-expect") != 0 &&
	    strcmp(g_fault, "ap-order") != 0 &&
	    strcmp(g_fault, "ap-head") != 0 &&
	    strcmp(g_fault, "ap-beacon") != 0 &&
	    strcmp(g_fault, "sta-event") != 0 &&
	    strcmp(g_fault, "ap-ie-len") != 0) {
		printf("CFG: unknown fault '%s'\n", g_fault);
		return 2;
	}

	if (osKernelInitialize() != osOK) {
		printf("CFG: kernel init failed\n");
		return 2;
	}
	attr.name = "lkpi-cfg80211";
	attr.stack_size = 16384u;
	attr.priority = osPriorityNormal;
	t = osThreadNew(app_task, NULL, &attr);
	if (t == NULL) {
		printf("CFG: cannot create the case task\n");
		return 2;
	}
	if (osKernelStart() != osOK)
		return 2;
	return g_fail == 0 ? 0 : 1;
}
