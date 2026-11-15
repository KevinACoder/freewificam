/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   f429_wifi_ap.c
 * @brief  Case f429_wifi_ap: the RTL8188EUS becomes a 2.4 GHz AP on the
 *         STM32F429, the Pi's MT7921AU joins it, and the data plane is proven
 *         both ways.
 *
 * The attach is the same as the station case's (compiled-in firmware, wlan0,
 * dev_open).  What changes is the role and the owner of the AP MLME:
 *
 *   - the AP MLME is the port's own minimal open-AP MLME
 *     (components/liblinux80211/linuxkpi_ap_mlme.c), not the imported hostapd:
 *     the hostapd stack costs about 320 KiB of flash on this platform, and the
 *     1 MiB part cannot carry it next to the wireless stack and its driver.
 *     The MLME answers probe requests, open authentication and association,
 *     and opens the controlled port; mac80211 keeps the AP state machine and
 *     the driver sends the beacon from the host (the 8188E does not repeat a
 *     host-downloaded beacon, so the driver recomposes one per interval).
 *   - the beacon template is a fixed open HT20/WMM access point's IE set
 *     (linuxkpi_ap_beacon.c): mac80211 inserts the TIM between head and tail.
 *   - because the image has no netlink socket, the station's association is
 *     read from mac80211's own AP station table
 *     (lkpi_nl80211_sta_count()); the peer's `iw link` is the other half.
 *
 * The case is one boot of the AP ladder:
 *
 *   A1  set_iface(AP) + dev_open + START_AP, the beacon on air
 *   A2  the peer's probe/auth/assoc answered, station in the stack's table
 *   A3  56 B and 1400 B ICMP, 20/20 each, both directions
 *   A4  (CONFIG_F429_WIFI_AP_TP) the four report-only throughput phases
 *
 * Anchors (the round harness greps these):
 *   f429_wifi_ap: stage=ap
 *   f429_wifi_ap: core ready
 *   f429_wifi_ap: usb backend ready
 *   f429_wifi_ap: initcalls run
 *   f429_wifi_ap: usb drivers=<n>
 *   f429_wifi_ap: fw registered=0 name=...
 *   f429_wifi_ap: usb bus=0 hcd=dwc2 base=0x40040000 started
 *   f429_wifi_ap: wlan0 present=1 ifindex=.. mac=.. iftype=.. ap=<0|1>
 *   f429_wifi_ap: wiphy bands=.. managed=.. ap=.. ht=.. vht=..
 *   f429_wifi_ap: firmware <driver line>
 *   f429_wifi_ap: chan freq=2432 present=1 flags=0x.. can_beacon=<0|1>
 *   f429_wifi_ap: set_iface rc=0 type=3
 *   f429_wifi_ap: start rc=0
 *   f429_wifi_ap: beacon head_len=.. tail_len=.. ies=.. head_sha=.. tail_sha=..
 *   f429_wifi_ap: mlme ready ssid=... head=.. tail=..
 *   f429_wifi_ap: start_ap rc=0 freq=2432 interval=100 dtim=2
 *   f429_wifi_ap: netif up name=wl0 ip=192.168.99.1/24 mac=...
 *   f429_wifi_ap: link freq=2432
 *   f429_wifi_ap: PASS A1
 *   f429_wifi_ap: mlme stats rx=.. tx=.. sta=..
 *   f429_wifi_ap: sta n=<n> mac=<mac>
 *   f429_wifi_ap: PASS A2
 *   f429_wifi_ap: peer window ms=<n>
 *   f429_wifi_ap: ping payload=<n> tx=<n> rx=<n> rtt_ms=<min>/<avg>/<max>
 *   f429_wifi_ap: net rx_frames=.. tx_frames=.. tx_fail=.. rx_alloc_fail=..
 *   f429_wifi_ap: PASS A3
 *   f429_wifi_ap: phase <n> begin proto=.. role=.. air=.. port=.. window_ms=..
 *                 name=.. peer_ms=.. rate_kbps=..
 *   f429_wifi_ap: phase <n> report bytes=.. ms=.. kbit=.. pkts=..
 *   f429_wifi_ap: phase <n> done ok=<0|1>
 *   f429_wifi_ap: throughput phases moved=<n>/4
 *   f429_wifi_ap: PASS AP | FAIL <reason>
 *
 * What this is not: WPA2, multiple clients, 5 GHz, or a throughput verdict.
 * The four phases of A4 are samples with a peer on the same bench; they carry
 * no threshold.
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "rtl8xxxu_fw.h"
#include "os/cmsis2/cmsis2.h"

/* CherryUSB first (the station case's include order): its <stdlib.h> must be
 * parsed before the Linux face's compiler-attributes header is. */
#include "usbh_core.h"
#include "usb_glue.h"
#include "uart.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/ap_mlme.h"
#include "linux80211/ap_beacon.h"
#include "linux80211/net.h"
#include "linuxkpi_usb.h"
#include "lkpi_usb_cherryusb.h"

#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

/* The component's two start-up entries, declared by hand as the other board
 * cases do. */
void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_usb_cherryusb_init(void);

#define F429_AP_SSID		"rtos-8188-ap"
#define F429_AP_FREQ		2432u
#define F429_AP_CHAN_WIDTH	NL80211_CHAN_WIDTH_20
#define F429_AP_BEACON_INT	100u
#define F429_AP_DTIM		2u

#define F429_AP_BOARD_IP	"192.168.99.1"
#define F429_AP_BOARD_MASK	"255.255.255.0"
#define F429_AP_PEER_IP		"192.168.99.2"

#define F429_AP_WAIT_MS		60000U
#define F429_AP_POLL_MS		100U
#define F429_AP_STA_WAIT_MS	60000U
#define F429_AP_MLME_POLL_MS	20U
#define F429_AP_PEER_WINDOW_MS	12000U
#define F429_AP_SETTLE_MS	3000U
#define F429_AP_PING_COUNT	20U
#define F429_AP_PING_TIMEOUT_MS	1000U
#define F429_AP_PING_SMALL	56U
#define F429_AP_PING_LARGE	1400U
#define F429_AP_LOG_MAX		24U

/* first-party read-only probes (wireless-drivers/rtl8xxxu/ports/rtos) */
void rtl8xxxu_diag_ap_state(void *wiphy);
void rtl8xxxu_diag_beacon_counters(void);

static char fw_line[160];
static struct net_device *ap_dev;
static unsigned int mlme_emit_lines;
static unsigned int mlme_emit_dropped;

/* --- the console sink -----------------------------------------------------
 *
 * Same discipline as the station case: the driver logs from its USB completion,
 * which on this board can run in the OTG_HS hard IRQ, and the console's lock is
 * a blocking one - a print from an exception wedges the machine.  The AP path
 * also must not print per management frame: the 115200 polled UART charges
 * milliseconds per line, and the association/beacon path is where that is
 * paid.  Whitelist the firmware line and the driver/USB identity lines.
 */
static void
fw_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (line == NULL)
		return;
	{
		unsigned int ipsr;

		__asm__ __volatile__("mrs %0, ipsr" : "=r"(ipsr));
		if (ipsr != 0U)
			return;
	}
	if (strstr(line, "Firmware revision") != NULL) {
		strncpy(fw_line, line, sizeof(fw_line) - 1U);
		fw_line[sizeof(fw_line) - 1U] = '\0';
		CASE_ANCHOR("f429_wifi_ap: firmware %s\n", line);
		return;
	}
	if (strstr(line, "rtl8xxxu") != NULL || strstr(line, "usbh") != NULL ||
	    strstr(line, "wlan0") != NULL || strstr(line, "cfg80211") != NULL)
		CASE_INFO("f429_wifi_ap: drv %s\n", line);
}

/* --- the MLME's three actions and its log -------------------------------- */

static int
ap_mgmt_tx(const unsigned char *frame, unsigned int len)
{
	return lkpi_nl80211_mgmt_tx(ap_dev, frame, len, 0);
}

static int
ap_new_station(const struct lkpi_ap_sta_add *sta)
{
	return lkpi_nl80211_new_station(ap_dev, sta);
}

static int
ap_set_authorized(const unsigned char mac[6])
{
	return lkpi_nl80211_sta_authorized(ap_dev, mac);
}

/* The MLME emits one line per management event.  A client's scan adds a probe
 * request, an association adds one probe/auth/assoc round, and a retry repeats
 * them; that is normal.  What must not happen is a line per frame forever, so
 * the callback keeps a bounded window and counts the rest. */
static void
ap_emit(const char *line)
{
	if (line == NULL)
		return;
	if (mlme_emit_lines >= F429_AP_LOG_MAX) {
		mlme_emit_dropped++;
		return;
	}
	mlme_emit_lines++;
	CASE_ANCHOR("f429_wifi_ap: mlme %s\n", line);
}

static const struct lkpi_ap_mlme_ops ap_ops = {
	.mgmt_tx = ap_mgmt_tx,
	.new_station = ap_new_station,
	.set_authorized = ap_set_authorized,
	.emit = ap_emit,
};

static void
ap_mlme_stats(const char *tag)
{
	struct lkpi_ap_mlme_stats st;

	(void)memset(&st, 0, sizeof(st));
	lkpi_ap_mlme_stats_get(&st);
	CASE_ANCHOR("f429_wifi_ap: mlme stats %s rx probe_req=%u auth_req=%u "
		    "assoc_req=%u drop_bad=%u drop_unsupported=%u\n",
		    tag, st.probe_req, st.auth_req, st.assoc_req,
		    st.drop_bad, st.drop_unsupported);
	CASE_ANCHOR("f429_wifi_ap: mlme stats %s tx probe_resp=%u "
		    "probe_fail=%u auth_resp=%u auth_fail=%u assoc_resp=%u "
		    "assoc_fail=%u tx_fail=%u\n",
		    tag, st.probe_resp, st.probe_fail, st.auth_resp,
		    st.auth_fail, st.assoc_resp, st.assoc_fail, st.tx_fail);
	CASE_ANCHOR("f429_wifi_ap: mlme stats %s sta add=%u add_fail=%u "
		    "authorized=%u auth_fail=%u q_full=%u emit_dropped=%u\n",
		    tag, st.sta_add, st.sta_add_fail, st.sta_auth,
		    st.sta_auth_fail, st.q_full, mlme_emit_dropped);
}

static void
ap_usb_ledger(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("f429_wifi_ap: usb submits=%u completes=%u refused=%u "
		    "stage_overflow=%u slots_exhausted=%u stage_size=%u slots=%u "
		    "rx=%u rx_err=%u rx_last_err=%d rx_len=%u rx_max=%u "
		    "babble_len=%u babble_act=%u babble_mps=%u "
		    "tx=%u tx_err=%u tx_last_err=%d tx_len=%u\n",
		    st.submits, st.completed, st.refused, st.stage_overflow,
		    st.slots_exhausted, st.stage_size, st.stage_slots,
		    st.rx_completes, st.rx_errors, st.rx_last_err,
		    st.rx_last_len, st.rx_max_len,
		    st.rx_babble_len, st.rx_babble_actual, st.rx_babble_mps,
		    st.tx_completes, st.tx_errors,
		    st.tx_last_err, st.tx_last_len);
}

/* --- A4: the AP phase table (CONFIG_F429_WIFI_AP_TP) ---------------------
 *
 * The names are the AP role's: air=down means the board (the AP) sends.  The
 * windows are the station case's own (a board send is the short leg on this
 * full-speed host), assigned by direction so the two roles can be compared
 * direction by direction:
 *
 *   tcpdown  board -> peer TCP   15 s
 *   tcpup    peer  -> board TCP  25 s
 *   udpup    peer  -> board UDP  20 s
 *   udpdown  board -> peer UDP   12 s
 *
 * The peer arms are the bench's: iperf2 on port 5001, the raw counting sink on
 * port 5002.  Every number is a sample of one window; no threshold applies.
 */
#if defined(CONFIG_F429_WIFI_AP_TP)
#ifndef CONFIG_F429_WIFI_AP_TP_UDP_KBPS
#define CONFIG_F429_WIFI_AP_TP_UDP_KBPS 40000
#endif
#ifndef CONFIG_F429_WIFI_AP_TP_PEER_MS
#define CONFIG_F429_WIFI_AP_TP_PEER_MS 3000
#endif

#define AP_TP_TCP_PORT		5001u
#define AP_TP_UDP_PORT		5002u
#define AP_TP_TCP_TX_MS		15000u	/* board sends (the station case's tcpup window) */
#define AP_TP_TCP_RX_MS		25000u	/* board receives (the station case's tcpdn window) */
#define AP_TP_UDP_RX_MS		20000u	/* board receives (the station case's udpdn window) */
#define AP_TP_UDP_TX_MS		12000u	/* board sends (the station case's udpup window) */

enum ap_tp_kind {
	AP_TP_TCP_TX,
	AP_TP_TCP_RX,
	AP_TP_UDP_TX,
	AP_TP_UDP_RX,
};

struct ap_tp_phase {
	const char	*name;
	const char	*proto;
	const char	*role;
	const char	*air;
	unsigned int	 port;
	unsigned int	 window_ms;
	unsigned int	 rate_kbps;	/* UDP only, 0 = unpaced */
	enum ap_tp_kind	 kind;
};

static const struct ap_tp_phase ap_tp_plan[] = {
	{ "tcpdown", "tcp", "client", "down", AP_TP_TCP_PORT, AP_TP_TCP_TX_MS,
	  0u, AP_TP_TCP_TX },
	{ "tcpup", "tcp", "server", "up", AP_TP_TCP_PORT, AP_TP_TCP_RX_MS,
	  0u, AP_TP_TCP_RX },
	{ "udpup", "udp", "server", "up", AP_TP_UDP_PORT, AP_TP_UDP_RX_MS,
	  0u, AP_TP_UDP_RX },
	{ "udpdown", "udp", "client", "down", AP_TP_UDP_PORT, AP_TP_UDP_TX_MS,
	  CONFIG_F429_WIFI_AP_TP_UDP_KBPS, AP_TP_UDP_TX },
};

static int
ap_tp_phase(unsigned int n, const struct ap_tp_phase *ph,
	    unsigned int *moved_out)
{
	struct lkpi_net_phase r;
	struct lkpi_net_diag d0, d1;
	int rc;

	(void)memset(&r, 0, sizeof(r));
	(void)memset(&d0, 0, sizeof(d0));
	(void)memset(&d1, 0, sizeof(d1));
	lkpi_net_diag(&d0);

	/* The begin line is printed before the wait: the harness starts the
	 * peer arm from it, and the margin runs on this side. */
	CASE_ANCHOR("f429_wifi_ap: phase %u begin proto=%s role=%s air=%s "
		    "port=%u window_ms=%u name=%s peer_ms=%u rate_kbps=%u\n",
		    n, ph->proto, ph->role, ph->air, ph->port, ph->window_ms,
		    ph->name, (unsigned int)CONFIG_F429_WIFI_AP_TP_PEER_MS,
		    ph->rate_kbps);
	osDelay(CONFIG_F429_WIFI_AP_TP_PEER_MS);

	switch (ph->kind) {
	case AP_TP_TCP_TX:
		rc = lkpi_net_iperf_up(F429_AP_PEER_IP, ph->port,
				       ph->window_ms, &r);
		break;
	case AP_TP_TCP_RX:
		rc = lkpi_net_iperf_down(ph->port, ph->window_ms, &r);
		break;
	case AP_TP_UDP_TX:
		rc = lkpi_net_udp_blast(F429_AP_PEER_IP, ph->port,
					ph->window_ms, ph->rate_kbps, &r);
		break;
	case AP_TP_UDP_RX:
		rc = lkpi_net_udp_sink_window(ph->window_ms, &r);
		break;
	default:
		rc = -1;
		break;
	}

	CASE_ANCHOR("f429_wifi_ap: phase %u report bytes=%u ms=%u kbit=%u "
		    "pkts=%u offered=%u tx_err=%u rx_drop=%u\n",
		    n, r.bytes, r.ms, r.kbit, r.pkts, r.offered, r.tx_err,
		    r.rx_drop);
	lkpi_net_diag(&d1);
	CASE_ANCHOR("f429_wifi_ap: phase %u census rx_frames=%lu "
		    "rx_bytes=%lu tx_frames=%lu tx_bytes=%lu tx_fail=%lu\n",
		    n, d1.rx_frames - d0.rx_frames,
		    d1.rx_bytes - d0.rx_bytes,
		    d1.tx_frames - d0.tx_frames,
		    d1.tx_bytes - d0.tx_bytes,
		    d1.tx_fail - d0.tx_fail);
	CASE_ANCHOR("f429_wifi_ap: phase %u done ok=%d\n", n,
		    (rc == 0) ? 1 : 0);
	if (moved_out != NULL && r.bytes != 0U)
		(*moved_out)++;
	return rc;
}
#endif /* CONFIG_F429_WIFI_AP_TP */

/* --- the TX block-ack refusal --------------------------------------------
 *
 * The station role found that once the 8188E's aggregation engine starts a TX
 * block-ack session it swallows the per-frame URBs: the stack statuses the
 * frames and the AP sees none.  The AP role sends through the same engine, so
 * the session is refused here too and every frame goes as its own USB
 * transfer.  Report-only consequence: no A-MPDU in either direction this
 * board originates.
 */
struct ieee80211_sta;
int __real_ieee80211_start_tx_ba_session(struct ieee80211_sta *pubsta,
					 u16 tid, u16 timeout);
int
__wrap_ieee80211_start_tx_ba_session(struct ieee80211_sta *pubsta, u16 tid,
				     u16 timeout)
{
	(void)pubsta;
	(void)tid;
	(void)timeout;
	return -EINVAL;
}

int
case_f429_wifi_ap_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	struct ieee80211_channel *chan;
	struct cfg80211_chan_def chandef;
	unsigned char head[LKPI_AP_BEACON_HEAD_MAX];
	unsigned char tail[LKPI_AP_BEACON_TAIL_MAX];
	unsigned char sta_macs[4][6];
	unsigned char netif_mac[6];
	unsigned int head_len = 0U, tail_len = 0U;
	unsigned int waited = 0U, sta_n = 0U;
	unsigned int warm = 0U;
	unsigned int rx = 0U, rmin = 0U, ravg = 0U, rmax = 0U;
	unsigned int small_rx = 0U, big_rx = 0U;
	char why[96];
	int can_beacon = 0;
	int freq;
	int rc;

	CASE_ANCHOR("f429_wifi_ap: stage=ap\n");

	lkpi_core_init();
	CASE_ANCHOR("f429_wifi_ap: core ready\n");
	lkpi_log_sink_register(fw_sink);

	lkpi_usb_cherryusb_init();
	CASE_ANCHOR("f429_wifi_ap: usb backend ready\n");

	lkpi_module_init_run();
	CASE_ANCHOR("f429_wifi_ap: initcalls run\n");

	lkpi_usb_init();
	CASE_ANCHOR("f429_wifi_ap: usb drivers=%u\n",
		    (unsigned)lkpi_usb_driver_count_get());

	lkpi_usb_force_sync_set(false);
	if (rtl8xxxu_fw_register() != 0) {
		CASE_ANCHOR("f429_wifi_ap: FAIL firmware blob registration\n");
		return 1;
	}
	CASE_ANCHOR("f429_wifi_ap: fw registered=0 name=%s\n",
		    RTL8XXXU_FW_NAME);

	usb_osal_timer_service_start();
	if (usb_board_init(0) != 0) {
		CASE_ANCHOR("f429_wifi_ap: FAIL usb_board_init bus0\n");
		return 1;
	}
	if (usbh_initialize(0, usb_board_reg_base(0),
			    usb_board_event_handler) != 0) {
		CASE_ANCHOR("f429_wifi_ap: FAIL usbh_initialize bus0\n");
		return 1;
	}
	usb_board_after_start(0);
	CASE_ANCHOR("f429_wifi_ap: usb bus=0 hcd=dwc2 base=0x%lx started\n",
		    (unsigned long)usb_board_reg_base(0));

	for (waited = 0U; waited < F429_AP_WAIT_MS; waited += F429_AP_POLL_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(F429_AP_POLL_MS);
	}
	CASE_ANCHOR("f429_wifi_ap: wlan0 present=%d after=%u ms\n",
		    dev != NULL, (unsigned)waited);
	if (dev == NULL) {
		CASE_ANCHOR("f429_wifi_ap: FAIL attach timeout\n");
		ap_usb_ledger();
		return 2;
	}
	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("f429_wifi_ap: FAIL no wireless_dev\n");
		return 3;
	}
	CASE_ANCHOR("f429_wifi_ap: wlan0 present=1 ifindex=%d "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x iftype=%d\n",
		    dev->ifindex, dev->dev_addr[0], dev->dev_addr[1],
		    dev->dev_addr[2], dev->dev_addr[3], dev->dev_addr[4],
		    dev->dev_addr[5], (int)wdev->iftype);

	{
		struct wiphy *wiphy = wdev->wiphy;
		int bands = 0, managed = 0, ap = 0, ht = 0, vht = 0;

		if (wiphy->bands[NL80211_BAND_2GHZ] != NULL) {
			bands++;
			if (wiphy->bands[NL80211_BAND_2GHZ]->ht_cap.ht_supported)
				ht = 1;
			if (wiphy->bands[NL80211_BAND_2GHZ]->vht_cap.vht_supported)
				vht = 1;
		}
		managed = !!(wiphy->interface_modes & BIT(NL80211_IFTYPE_STATION));
		ap = !!(wiphy->interface_modes & BIT(NL80211_IFTYPE_AP));
		CASE_ANCHOR("f429_wifi_ap: wiphy bands=%d managed=%d ap=%d "
			    "ht=%d vht=%d\n", bands, managed, ap, ht, vht);
		if (!ap) {
			CASE_ANCHOR("f429_wifi_ap: FAIL the wiphy has no AP "
				    "interface mode (driver role gate)\n");
			return 3;
		}
	}

	if (fw_line[0] == '\0') {
		CASE_ANCHOR("f429_wifi_ap: FAIL no firmware revision line\n");
		ap_usb_ledger();
		return 4;
	}

	/* The channel must be in the driver's band and beaconable: this is the
	 * discriminator when START_AP returns -EINVAL. */
	chan = ieee80211_get_channel(wdev->wiphy, (int)F429_AP_FREQ);
	if (chan != NULL) {
		cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_HT20);
		can_beacon = cfg80211_reg_can_beacon(wdev->wiphy, &chandef,
						     NL80211_IFTYPE_AP) ? 1 : 0;
	}
	CASE_ANCHOR("f429_wifi_ap: chan freq=%u present=%d flags=0x%08x "
		    "can_beacon=%d\n", F429_AP_FREQ, chan != NULL ? 1 : 0,
		    chan != NULL ? (unsigned)chan->flags : 0u, can_beacon);
	if (chan == NULL || !can_beacon) {
		CASE_ANCHOR("f429_wifi_ap: FAIL channel not beaconable\n");
		return 4;
	}

	/* --- A1: the default station interface becomes the AP ---------------- */
	if (netif_running(dev))
		(void)dev_close(dev);
	rc = lkpi_nl80211_set_interface(dev, NL80211_IFTYPE_AP);
	CASE_ANCHOR("f429_wifi_ap: set_iface rc=%d type=%d\n", rc,
		    lkpi_nl80211_iftype(dev));
	if (rc != 0 || lkpi_nl80211_iftype(dev) != NL80211_IFTYPE_AP) {
		CASE_ANCHOR("f429_wifi_ap: FAIL set_iface rc=%d type=%d\n", rc,
			    lkpi_nl80211_iftype(dev));
		return 5;
	}

	rc = dev_open(dev);
	CASE_ANCHOR("f429_wifi_ap: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("f429_wifi_ap: FAIL dev_open rc=%d\n", rc);
		return 6;
	}

	rc = lkpi_ap_beacon_build(F429_AP_SSID, F429_AP_FREQ, dev->dev_addr,
				  head, sizeof(head), &head_len,
				  tail, sizeof(tail), &tail_len);
	CASE_ANCHOR("f429_wifi_ap: beacon head_len=%u tail_len=%u ies=%u "
		    "head_sha=%08x tail_sha=%08x\n", head_len, tail_len,
		    lkpi_ap_beacon_ie_count(tail, tail_len),
		    lkpi_ap_beacon_hash(head, head_len),
		    lkpi_ap_beacon_hash(tail, tail_len));
	if (rc != 0) {
		CASE_ANCHOR("f429_wifi_ap: FAIL beacon build rc=%d\n", rc);
		return 7;
	}
	rc = lkpi_ap_beacon_validate(head, head_len, tail, tail_len, why,
				     sizeof(why));
	if (rc != 0) {
		CASE_ANCHOR("f429_wifi_ap: FAIL beacon invalid: %s\n", why);
		return 7;
	}

	ap_dev = dev;
	rc = lkpi_ap_mlme_configure(F429_AP_SSID, head, head_len, tail,
				    tail_len, &ap_ops);
	CASE_ANCHOR("f429_wifi_ap: mlme configure rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("f429_wifi_ap: FAIL mlme configure rc=%d\n", rc);
		return 8;
	}

	rc = lkpi_nl80211_start_ap(dev, F429_AP_SSID, F429_AP_FREQ,
				   F429_AP_CHAN_WIDTH, F429_AP_BEACON_INT,
				   F429_AP_DTIM, head, head_len, tail,
				   tail_len, 0);
	CASE_ANCHOR("f429_wifi_ap: start_ap rc=%d freq=%u interval=%u dtim=%u\n",
		    rc, F429_AP_FREQ, F429_AP_BEACON_INT, F429_AP_DTIM);
	if (rc != 0) {
		CASE_ANCHOR("f429_wifi_ap: FAIL start_ap rc=%d\n", rc);
		return 9;
	}
	/* From here on the RX seam queues the peer's management frames. */
	lkpi_ap_mlme_enable(1);

	rc = lkpi_net_wlan_up(F429_AP_BOARD_IP, F429_AP_BOARD_MASK, NULL,
			      netif_mac);
	CASE_ANCHOR("f429_wifi_ap: netif up rc=%d name=wl0 ip=%s/24 "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x\n", rc,
		    F429_AP_BOARD_IP, (unsigned char)netif_mac[0],
		    (unsigned char)netif_mac[1], (unsigned char)netif_mac[2],
		    (unsigned char)netif_mac[3], (unsigned char)netif_mac[4],
		    (unsigned char)netif_mac[5]);
	if (rc != 0) {
		CASE_ANCHOR("f429_wifi_ap: FAIL netif up rc=%d\n", rc);
		return 10;
	}

	/* Let the first beacon (and the host repeat) land before the anchor is
	 * printed, then read the driver's own beacon counters: "the host
	 * composed one" and "the beacon is on air" are different facts, and the
	 * monitor/scan witness is the other half. */
	osDelay(700U);
	freq = lkpi_nl80211_link_freq(dev);
	CASE_ANCHOR("f429_wifi_ap: link freq=%d\n", freq);
	rtl8xxxu_diag_ap_state(wdev->wiphy);
	rtl8xxxu_diag_beacon_counters();
	/* The receive ledger right after the interface is up: the management
	 * frames of the peer's scan/join never reached the MLME in the first
	 * boots, and these counters say whether the URBs complete at all. */
	ap_usb_ledger();
	CASE_ANCHOR("f429_wifi_ap: PASS A1\n");

	/* --- A2: serve the MLME until the stack has the station -------------- */
	for (waited = 0U; waited < F429_AP_STA_WAIT_MS;
	     waited += F429_AP_MLME_POLL_MS) {
		(void)lkpi_ap_mlme_poll();
		sta_n = 0U;
		if (lkpi_nl80211_sta_count(dev, sta_macs, 4U, &sta_n) != 0)
			sta_n = 0U;
		if (sta_n != 0U)
			break;
		osDelay(F429_AP_MLME_POLL_MS);
	}
	ap_mlme_stats("a2");
	CASE_ANCHOR("f429_wifi_ap: sta n=%u mac=%02x:%02x:%02x:%02x:%02x:%02x "
		    "waited_ms=%u\n", sta_n, sta_macs[0][0], sta_macs[0][1],
		    sta_macs[0][2], sta_macs[0][3], sta_macs[0][4],
		    sta_macs[0][5], waited);
	if (sta_n == 0U) {
		CASE_ANCHOR("f429_wifi_ap: FAIL no station after %u ms\n",
			    F429_AP_STA_WAIT_MS);
		ap_usb_ledger();
		return 11;
	}
	CASE_ANCHOR("f429_wifi_ap: PASS A2\n");

	/* The peer's own ping window starts here, after the station exists: a
	 * counted round that starts before the peer can answer measures the
	 * bench's timing, not the path. */
	CASE_ANCHOR("f429_wifi_ap: peer window ms=%u\n",
		    F429_AP_PEER_WINDOW_MS);
	osDelay(F429_AP_PEER_WINDOW_MS);
	osDelay(F429_AP_SETTLE_MS);

	/* --- A3: both directions, then the receive account ------------------- */
	for (warm = 0U; warm < 20U; warm++) {
		CASE_ANCHOR("f429_wifi_ap: ping warmup call=%u\n", warm + 1U);
		rc = lkpi_net_ping(F429_AP_PEER_IP, 1U, F429_AP_PING_SMALL,
				   F429_AP_PING_TIMEOUT_MS, &rx, &rmin, &ravg,
				   &rmax);
		CASE_ANCHOR("f429_wifi_ap: ping warmup ret=%d rx=%u\n", rc, rx);
		if (rc == 0 && rx == 1U)
			break;
		osDelay(250U);
	}

	rc = lkpi_net_ping(F429_AP_PEER_IP, F429_AP_PING_COUNT,
			   F429_AP_PING_SMALL, F429_AP_PING_TIMEOUT_MS,
			   &rx, &rmin, &ravg, &rmax);
	small_rx = rx;
	CASE_ANCHOR("f429_wifi_ap: ping payload=%u tx=%u rx=%u "
		    "rtt_ms=%u/%u/%u rc=%d\n", F429_AP_PING_SMALL,
		    F429_AP_PING_COUNT, rx, rmin, ravg, rmax, rc);

	rc = lkpi_net_ping(F429_AP_PEER_IP, F429_AP_PING_COUNT,
			   F429_AP_PING_LARGE, F429_AP_PING_TIMEOUT_MS,
			   &rx, &rmin, &ravg, &rmax);
	big_rx = rx;
	CASE_ANCHOR("f429_wifi_ap: ping payload=%u tx=%u rx=%u "
		    "rtt_ms=%u/%u/%u rc=%d\n", F429_AP_PING_LARGE,
		    F429_AP_PING_COUNT, rx, rmin, ravg, rmax, rc);

	{
		struct lkpi_net_diag nd;

		(void)memset(&nd, 0, sizeof(nd));
		lkpi_net_diag(&nd);
		CASE_ANCHOR("f429_wifi_ap: net rx_frames=%lu tx_frames=%lu "
			    "rx_bytes=%lu tx_bytes=%lu rx_alloc_fail=%lu "
			    "rx_foreign=%lu rx_no_netif=%lu tx_fail=%lu\n",
			    nd.rx_frames, nd.tx_frames, nd.rx_bytes,
			    nd.tx_bytes, nd.rx_alloc_fail, nd.rx_foreign,
			    nd.rx_no_netif, nd.tx_fail);
	}
	/* The driver's beacon count after the data plane: it must still be
	 * climbing (the chip is beaconing from the host's timer). */
	rtl8xxxu_diag_beacon_counters();

	if (small_rx == F429_AP_PING_COUNT && big_rx == F429_AP_PING_COUNT)
		CASE_ANCHOR("f429_wifi_ap: PASS A3\n");
	else
		CASE_ANCHOR("f429_wifi_ap: FAIL ping small=%u big=%u\n",
			    small_rx, big_rx);

#if defined(CONFIG_F429_WIFI_AP_TP)
	{
		unsigned int moved = 0U;
		unsigned int pi;
		int lrc;

		lrc = lkpi_net_iperf_server_start(AP_TP_TCP_PORT);
		if (lrc == 0)
			lrc = lkpi_net_udp_sink_start(AP_TP_UDP_PORT);
		CASE_ANCHOR("f429_wifi_ap: server tcp=%u udp=%u ok=%d\n",
			    (unsigned int)AP_TP_TCP_PORT,
			    (unsigned int)AP_TP_UDP_PORT, (lrc == 0) ? 1 : 0);
		for (pi = 0U; pi < sizeof(ap_tp_plan) / sizeof(ap_tp_plan[0]);
		     pi++)
			(void)ap_tp_phase(pi + 1U, &ap_tp_plan[pi], &moved);
		CASE_ANCHOR("f429_wifi_ap: throughput phases moved=%u/4\n",
			    moved);
	}
#endif /* CONFIG_F429_WIFI_AP_TP */

	ap_mlme_stats("end");
	ap_usb_ledger();
	CASE_ANCHOR("f429_wifi_ap: PASS AP\n");
	return 0;
}
