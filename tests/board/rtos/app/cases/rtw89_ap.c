/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtw89_ap.c
 * @brief  Case rtw89_ap: the RTL8851BU becomes a 2.4 GHz AP (A1), the Pi's
 *         MT7921AU joins it (A2), the data plane is proven both ways (A3),
 *         then the AP phase table runs (A4).
 *
 * The attach is rtw89_c5's (CherryUSB, the URB engine, the compiled-in
 * firmware, wlan0, dev_open).  What changes is the role and the owner of the
 * MLME:
 *
 *   - The AP MLME is the imported NXP hostapd (third-party/wpa_supplicant-rtos,
 *     CONFIG_HOSTAP_AP), the same one the AC 7260 AP arm uses.  hostapd builds
 *     the beacon's head/tail and drives NL80211_CMD_START_AP; mac80211 keeps
 *     the AP state machine, and rtw89 pushes the beacon to the firmware through
 *     its H2C beacon-update path (`rtw89_fw_h2c_update_beacon`).  The firmware
 *     sends the beacons, not this task.
 *   - Because there is no netlink socket in this image, A2's witness is
 *     mac80211's own AP station table (`lkpi_nl80211_sta_count`, the atomic
 *     iterator), with hostapd's count as the second opinion.
 *   - The channel comes from nl80211's chandef and from hostapd's config; both
 *     are the same fact here (ch5/2432), and the case prints the wiphy's
 *     channel flags and `cfg80211_reg_can_beacon` before touching the role.
 *
 * Direction words are the round's (runs/2026-11-14-rtw89-8851bu-ap/PREREG.md):
 * air=down is AP->STA (this board sends; the Pi's server/sink receives),
 * air=up is STA->AP (the Pi sends; this board's lwIP receives) - the opposite
 * of the C6 case's board roles with the same phase names.
 *
 * Console discipline: the 8189 AP round and the GT AP path both showed that a
 * per-frame line on the association/beacon path is paid in milliseconds.  The
 * sink prints only the firmware/start whitelist and a bounded set of driver
 * error lines; no data-path line is formatted per frame.
 *
 * Anchors:
 *   rtw89_ap: core ready
 *   rtw89_ap: usb backend ready
 *   rtw89_ap: initcalls run
 *   rtw89_ap: usb drivers=<n>
 *   rtw89_ap: fw registered=0 name=rtw89/rtw8851b_fw-1.bin
 *   rtw89_ap: usb buses=3,2
 *   rtw89_ap: usb bus=<n> hcd=<ehci|xhci> base=0x... started
 *   rtw89_ap: firmware <driver line>
 *   rtw89_ap: wlan0 present=1 ifindex=<n> mac=<mac> iftype=<n>
 *   rtw89_ap: chan freq=2432 present=<0|1> flags=0x<hex> can_beacon=<0|1>
 *   rtw89_ap: set_iface rc=<n> type=<n>
 *   rtw89_ap: start rc=<n>
 *   rtw89_ap: netif up name=wl0 ip=<ip>/24 mac=<mac>
 *   rtw89_ap: security wpa2=<0|1> ssid=<ssid> channel=<n> bcn_int=<n> dtim_req=<n>
 *   rtw89_ap: hostap start rc=<n> ready=<0|1>
 *   rtw89_ap: link freq=<mhz>
 *   rtw89_ap: PASS A1
 *   rtw89_ap: waiting station ms=<n>
 *   rtw89_ap: sta n=<n> hostap=<n> mac=<mac>
 *   rtw89_ap: PASS A2
 *   rtw89_ap: peer window ms=<n>   (after A2: the peer's ping window)
 *   rtw89_ap: ping payload=<n> tx=<n> rx=<n> dup=<n> rtt_ms=<min>/<avg>/<max>
 *   rtw89_ap: link window rx_frames=<n> rx_foreign=<n> rx_no_netif=<n> rx_input_fail=<n> rx_bytes=<n> tx_frames=<n> tx_fail=<n>
 *   rtw89_ap: PASS A3
 *   rtw89_ap: server tcp=<n> udp=<n> ok=<0|1>
 *   rtw89_ap: phase <n> begin proto=<p> role=<r> air=<down|up> port=<n> window_ms=<n> name=<n> peer_ms=<n> rate_kbps=<n>
 *   rtw89_ap: phase <n> report bytes=<n> ms=<n> kbit=<n> pkts=<n>
 *   rtw89_ap: phase <n> stack tcp_recv=<n> tcp_xmit=<n> tcp_drop=<n> tcp_memerr=<n> tcp_err=<n> pbuf_err=<n> tcpseg_err=<n>
 *   rtw89_ap: phase <n> usb submits=<n> completes=<n> refused=<n> stage_overflow=<n> slots_exhausted=<n> rx=<n> rx_err=<n>
 *   rtw89_ap: phase <n> net rx_frames=<n> tx_frames=<n> rx_foreign=<n> rx_no_netif=<n> rx_input_fail=<n>
 *   rtw89_ap: phase <n> done ok=<0|1>
 *   rtw89_ap: PASS AP          (A1-A3 only)
 *   rtw89_ap: PASS APT         (A1-A3 plus all A4 phases)
 *   rtw89_ap: FAIL <reason>
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

/* CherryUSB first (see rtw89_c3.c): its <stdlib.h> must be parsed before the
 * Linux face's compiler-attributes header is. */
#include "usbh_core.h"
#include "usb_glue.h"
#include "usb_domain.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/hostap_ap.h"
#include "linux80211/hostap_drv.h"	/* the hw_mode values, plain C */
#include "linux80211/net.h"
#include "linuxkpi_usb.h"
#include "lkpi_usb_cherryusb.h"

#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

/* --- this AP ---------------------------------------------------------------
 *
 * The SSID must not collide with the bench AP's (labwifi-ar-ht20s): the Pi's
 * STA scans for this one.  The channel is the AP round's ch5, the channel the
 * GT AP arm uses on this card, so the two arms put the same BSS on the same
 * air.
 */
#ifndef RTW89_AP_SSID
#define RTW89_AP_SSID		"rtos-8851-ap"
#endif
#ifndef RTW89_AP_FREQ_MHZ
#define RTW89_AP_FREQ_MHZ	2432u
#endif
#ifndef RTW89_AP_CHANNEL
#define RTW89_AP_CHANNEL	5u
#endif
#ifndef RTW89_AP_BOARD_IP
#define RTW89_AP_BOARD_IP	"192.168.99.1"
#endif
#ifndef RTW89_AP_BOARD_MASK
#define RTW89_AP_BOARD_MASK	"255.255.255.0"
#endif
#ifndef RTW89_AP_PEER_IP
#define RTW89_AP_PEER_IP	"192.168.99.2"
#endif

#define AP_BEACON_INT		100u
#define AP_DTIM			2u
#define AP_WAIT_MS		40000U
#define AP_POLL_MS		100U
#define AP_STA_WAIT_MS		45000U
#define AP_PEER_WINDOW_MS	12000U
#define AP_SETTLE_MS		3000U
#define AP_PING_COUNT		20U
#define AP_PING_TIMEOUT_MS	300U
#define AP_PING_SMALL		56U
#define AP_PING_LARGE		1400U
#define AP_TCP_PORT		5001u
#define AP_UDP_PORT		5002u
#define AP_LOG_MAX		120U

#if defined(CONFIG_RTW89_AP_TP)
#define AP_TP			1
#else
#define AP_TP			0
#endif

/* The phase windows: the lwIP client is time-limited by lwiperf (10 s), so
 * the board-sent TCP phases use 12 s; the board-received TCP phase can run
 * longer.  No loopback phase: lwiperf has one listener and the C6T round
 * showed a loopback client can make the peer's later TCP connect time out. */
#define AP_TCPDN_MS		12000u
#define AP_UDPDN_MS		12000u
#define AP_TCPUP_MS		30000u
#define AP_UDPUP_MS		20000u
#define AP_PEER_MS		3000u
#define AP_UDP_RATE_10		10000u

#if CONFIG_RTW89_USB_BUS == 0
static const unsigned int ap_buses[] = { 3U, 2U };
#define AP_BUS_LIST_TXT	"3,2"
#else
static const unsigned int ap_buses[] = { 1U, 0U };
#define AP_BUS_LIST_TXT	"1,0"
#endif

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_usb_cherryusb_init(void);
void usb_osal_timer_service_start(void);
int lkpi_fw_rtw8851b_fw_register(void);

static unsigned int ap_log_lines;

static void
ap_sink(ulog_level_t level, const char *line)
{
	(void)level;

	if (line == NULL)
		return;
	if (strstr(line, "Firmware version") != NULL) {
		CASE_ANCHOR("rtw89_ap: firmware %s\n", line);
		return;
	}
	if (strstr(line, "loaded firmware") != NULL) {
		CASE_ANCHOR("rtw89_ap: fw image %s\n", line);
		return;
	}
	if (strstr(line, "read swsi busy") != NULL)
		return;		/* counted noise, never an anchor */
	if (ap_log_lines >= AP_LOG_MAX)
		return;
	if (strstr(line, "failed to") != NULL ||
	    strstr(line, "error") != NULL ||
	    strstr(line, "timeout") != NULL) {
		CASE_INFO("rtw89_ap: drv %s\n", line);
		ap_log_lines++;
	}
}

static void
ap_usb_ledger(void)
{
	struct lkpi_usb_chusb_stats st;

	lkpi_usb_cherryusb_stats(&st);
	CASE_ANCHOR("rtw89_ap: usb submits=%u completes=%u refused=%u "
		    "stage_overflow=%u slots_exhausted=%u rx=%u rx_err=%u\n",
		    st.submits, st.completed, st.refused, st.stage_overflow,
		    st.slots_exhausted, st.rx_completes, st.rx_errors);
}

static int
ap_usb_up(void)
{
	unsigned int i;
	int started = 0;

	lkpi_usb_cherryusb_init();
	CASE_ANCHOR("rtw89_ap: usb backend ready\n");

	lkpi_module_init_run();
	CASE_ANCHOR("rtw89_ap: initcalls run\n");

	lkpi_usb_init();
	CASE_ANCHOR("rtw89_ap: usb drivers=%u\n",
		    (unsigned)lkpi_usb_driver_count_get());

	lkpi_usb_force_sync_set(false);
	if (lkpi_fw_rtw8851b_fw_register() != 0) {
		CASE_ANCHOR("rtw89_ap: FAIL firmware blob registration\n");
		return -1;
	}
	CASE_ANCHOR("rtw89_ap: fw registered=0 name=rtw89/rtw8851b_fw-1.bin\n");

	usb_osal_timer_service_start();
	CASE_ANCHOR("rtw89_ap: usb buses=%s\n", AP_BUS_LIST_TXT);
	for (i = 0U; i < sizeof(ap_buses) / sizeof(ap_buses[0]); i++) {
		unsigned int bus = ap_buses[i];

		if (usb_board_init(bus) != 0) {
			CASE_INFO("rtw89_ap: bus%u board layer failed\n", bus);
			continue;
		}
		if (usbh_initialize((uint8_t)bus, usb_board_reg_base(bus),
				    usb_board_event_handler) != 0) {
			CASE_INFO("rtw89_ap: bus%u usbh_initialize failed\n", bus);
			continue;
		}
		usb_board_after_start((uint8_t)bus);
		CASE_ANCHOR("rtw89_ap: usb bus=%u hcd=%s base=0x%lx started\n", bus,
			    usb_board_bus_is_xhci(bus) ? "xhci" : "ehci",
			    (unsigned long)usb_board_reg_base(bus));
		started++;
	}
	return started;
}

/* --- A2: wait for the station, read mac80211's own table ------------------ */
static int
ap_sta_wait(struct net_device *dev, unsigned char mac[6], unsigned int *n_out,
	    unsigned int *hostap_out)
{
	unsigned int waited = 0U;
	unsigned char macs[4][6];
	unsigned int n = 0U;

	while (waited < AP_STA_WAIT_MS) {
		(void)lkpi_hostap_poll();
		memset(macs, 0, sizeof(macs));
		n = 0U;
		if (lkpi_nl80211_sta_count(dev, macs, 4U, &n) != 0)
			n = 0U;
		if (n > 0U)
			break;
		osDelay(AP_POLL_MS);
		waited += AP_POLL_MS;
	}
	*n_out = n;
	*hostap_out = (unsigned int)lkpi_hostap_sta_count();
	if (n > 0U)
		memcpy(mac, macs[0], 6);
	return n > 0U ? 0 : -1;
}

/* --- A4: the AP phase table (CONFIG_RTW89_AP_TP) -------------------------- */
#if AP_TP
enum ap_phase_kind {
	AP_TCP_DOWN,
	AP_UDP_DOWN,
	AP_TCP_UP,
	AP_UDP_UP,
};

struct ap_phase {
	const char	*name;
	const char	*proto;
	const char	*role;
	const char	*air;
	unsigned int	 port;
	unsigned int	 window_ms;
	unsigned int	 rate_kbps;	/* UDP only, 0 = unpaced */
	enum ap_phase_kind kind;
};

/* Same phase names as the C6 arm (so the report's gates read alike) but with
 * the AP's roles: down = this board sends.
 *
 * The order is not cosmetic.  The first A4 boot (rtos-a4-1) put the 40 Mbit/s
 * UDP blast between the two TCP downlinks; the blast is offered far above
 * this AP role's ~10-14 Mbit/s ceiling, the board's TX pushed it anyway (the
 * peer's iperf self-limits, the raw blast does not), the skb pool was
 * starved, and the next phases collapsed (tcpdn2 96 kbit/s, tcpup 0 bytes /
 * the Pi's ARP got "No route to host").  The table now keeps the TCP phases
 * before the UDP blast and the two TCP downlinks apart (the Pi's one iperf
 * server needs time to leave the port), and offers the downlink UDP at
 * 10 Mbit/s, inside the AP role's measured ceiling.  The uplink UDP keeps the
 * peer's 40M iperf, because the sender there is the Pi on both arms. */
static const struct ap_phase ap_plan[] = {
	{ "tcpdn",  "tcp", "client", "down", AP_TCP_PORT, AP_TCPDN_MS, 0u,
	  AP_TCP_DOWN },
	{ "tcpup",  "tcp", "server", "up",   AP_TCP_PORT, AP_TCPUP_MS, 0u,
	  AP_TCP_UP },
	{ "tcpdn2", "tcp", "client", "down", AP_TCP_PORT, AP_TCPDN_MS, 0u,
	  AP_TCP_DOWN },
	{ "udpdn",  "udp", "client", "down", AP_UDP_PORT, AP_UDPDN_MS,
	  AP_UDP_RATE_10, AP_UDP_DOWN },
	{ "udpup",  "udp", "server", "up",   AP_UDP_PORT, AP_UDPUP_MS, 0u,
	  AP_UDP_UP },
};

#define AP_PLAN_LEN	(sizeof(ap_plan) / sizeof(ap_plan[0]))
#define AP_TRAFFIC_PHASES	5u

static int
ap_run_phase(unsigned int n, const struct ap_phase *ph)
{
	struct lkpi_net_phase r;
	struct lkpi_net_stack_diag s0, s1;
	struct lkpi_net_diag d0, d1;
	struct lkpi_usb_chusb_stats u0, u1;
	int rc;

	(void)memset(&r, 0, sizeof(r));
	(void)memset(&s0, 0, sizeof(s0));
	(void)memset(&s1, 0, sizeof(s1));
	(void)memset(&d0, 0, sizeof(d0));
	(void)memset(&d1, 0, sizeof(d1));
	(void)memset(&u0, 0, sizeof(u0));
	(void)memset(&u1, 0, sizeof(u1));
	lkpi_net_stack_diag(&s0);
	lkpi_net_diag(&d0);
	lkpi_usb_cherryusb_stats(&u0);
	CASE_ANCHOR("rtw89_ap: phase %u begin proto=%s role=%s air=%s "
		    "port=%u window_ms=%u name=%s peer_ms=%u rate_kbps=%u\n",
		    n, ph->proto, ph->role, ph->air, ph->port, ph->window_ms,
		    ph->name, AP_PEER_MS, ph->rate_kbps);

	/* The harness starts the peer on this line; the margin is bench latency
	 * and sits outside the flow.  It is kept for every phase: the v3 arm
	 * (no margin on the up phases) let the Pi's client connect while the
	 * lwIP iperf server was still entering its serve loop, and the second
	 * A4 boot hung in that phase (rtos-a4-3, no output after 12 min).  The
	 * 3 s is the 8189 AP table's accepted shape; the up sink's first 3 s is
	 * the price, and it is reported, not hidden. */
	osDelay(AP_PEER_MS);

	switch (ph->kind) {
	case AP_TCP_DOWN:
		rc = lkpi_net_iperf_up(RTW89_AP_PEER_IP, ph->port,
				       ph->window_ms, &r);
		break;
	case AP_UDP_DOWN:
		rc = lkpi_net_udp_blast(RTW89_AP_PEER_IP, ph->port,
					ph->window_ms, ph->rate_kbps, &r);
		break;
	case AP_TCP_UP:
		rc = lkpi_net_iperf_down(ph->port, ph->window_ms, &r);
		break;
	case AP_UDP_UP:
		rc = lkpi_net_udp_sink_window(ph->window_ms, &r);
		break;
	default:
		rc = -1;
		break;
	}

	CASE_ANCHOR("rtw89_ap: phase %u report bytes=%u ms=%u kbit=%u "
		    "pkts=%u\n", n, r.bytes, r.ms, r.kbit, r.pkts);
	/* The stack's side of the same window: a phase that moved fewer bytes
	 * than the air could carry either never sent (tcp_xmit), had segments
	 * refused (memerr/pbuf_err/tcpseg_err), or its peer's segments were
	 * dropped (tcp_recv vs tcp_drop).  This is the A4 first-round
	 * observation surface, not a pass criterion. */
	lkpi_net_stack_diag(&s1);
	lkpi_net_diag(&d1);
	lkpi_usb_cherryusb_stats(&u1);
	CASE_ANCHOR("rtw89_ap: phase %u stack tcp_recv=%lu tcp_xmit=%lu "
		    "tcp_drop=%lu tcp_memerr=%lu tcp_err=%lu pbuf_err=%lu "
		    "tcpseg_err=%lu\n",
		    n, s1.tcp_recv - s0.tcp_recv, s1.tcp_xmit - s0.tcp_xmit,
		    s1.tcp_drop - s0.tcp_drop, s1.tcp_memerr - s0.tcp_memerr,
		    s1.tcp_err - s0.tcp_err, s1.pbuf_err - s0.pbuf_err,
		    s1.tcpseg_err - s0.tcpseg_err);
	CASE_ANCHOR("rtw89_ap: phase %u usb submits=%u completes=%u "
		    "refused=%u stage_overflow=%u slots_exhausted=%u rx=%u "
		    "rx_err=%u\n",
		    n, u1.submits - u0.submits, u1.completed - u0.completed,
		    u1.refused - u0.refused,
		    u1.stage_overflow - u0.stage_overflow,
		    u1.slots_exhausted - u0.slots_exhausted,
		    u1.rx_completes - u0.rx_completes,
		    u1.rx_errors - u0.rx_errors);
	CASE_ANCHOR("rtw89_ap: phase %u net rx_frames=%lu tx_frames=%lu "
		    "rx_foreign=%lu rx_no_netif=%lu rx_input_fail=%lu\n",
		    n, d1.rx_frames - d0.rx_frames,
		    d1.tx_frames - d0.tx_frames,
		    d1.rx_foreign - d0.rx_foreign,
		    d1.rx_no_netif - d0.rx_no_netif,
		    d1.rx_input_fail - d0.rx_input_fail);
	CASE_ANCHOR("rtw89_ap: phase %u done ok=%d\n", n, (rc == 0) ? 1 : 0);
	return rc;
}
#endif /* AP_TP */

static int
ap_ping(const char *what, unsigned int payload)
{
	unsigned int rx = 0U;
	unsigned int rtt_min = 0U, rtt_avg = 0U, rtt_max = 0U;
	int rc;

	rc = lkpi_net_ping(RTW89_AP_PEER_IP, AP_PING_COUNT, payload,
			   AP_PING_TIMEOUT_MS, &rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("rtw89_ap: ping payload=%u tx=%u rx=%u dup=%u "
		    "rtt_ms=%u/%u/%u\n", payload, AP_PING_COUNT, rx,
		    rx > AP_PING_COUNT ? rx - AP_PING_COUNT : 0u,
		    rtt_min, rtt_avg, rtt_max);
	if (rx < AP_PING_COUNT) {
		CASE_ANCHOR("rtw89_ap: FAIL ping %s rc=%d rx=%u\n", what, rc,
			    rx);
		return -1;
	}
	return 0;
}

int
case_rtw89_ap_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	struct ieee80211_channel *chan;
	struct cfg80211_chan_def chandef;
	struct lkpi_hostap_cfg ap_cfg;
	struct lkpi_net_diag d0, d1;
	unsigned char mac[6];
	unsigned char peer_mac[6];
	char netif_mac[6];
	unsigned int waited;
	unsigned int sta_n = 0U;
	unsigned int hostap_n = 0U;
	int can_beacon;
	int freq;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("rtw89_ap: core ready\n");
	lkpi_log_sink_register(ap_sink);

	rc = ap_usb_up();
	if (rc <= 0) {
		CASE_ANCHOR("rtw89_ap: FAIL no controller started\n");
		ap_usb_ledger();
		return 1;
	}

	while (waited < AP_WAIT_MS) {
		dev = dev_get_by_name(&init_net, "wlan0");
		if (dev != NULL)
			break;
		osDelay(AP_POLL_MS);
		waited += AP_POLL_MS;
	}
	if (dev == NULL) {
		CASE_ANCHOR("rtw89_ap: FAIL no wlan0 after %u ms\n",
			    (unsigned)waited);
		ap_usb_ledger();
		return 2;
	}
	memcpy(mac, dev->dev_addr, 6);
	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("rtw89_ap: FAIL no wireless_dev\n");
		return 3;
	}
	CASE_ANCHOR("rtw89_ap: wlan0 present=1 ifindex=%d mac=%02x:%02x:%02x:%02x:%02x:%02x iftype=%d\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		    (int)wdev->iftype);

	/* The channel must be in the driver's band and beaconable: this is the
	 * discriminator when START_AP returns -EINVAL or the role change
	 * silently falls back.  Same shape as the 7260 AP case's R6 anchor. */
	chan = ieee80211_get_channel(wdev->wiphy, (int)RTW89_AP_FREQ_MHZ);
	can_beacon = 0;
	if (chan != NULL) {
		cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_HT20);
		can_beacon = cfg80211_reg_can_beacon(wdev->wiphy, &chandef,
						     NL80211_IFTYPE_AP) ? 1 : 0;
	}
	CASE_ANCHOR("rtw89_ap: chan freq=%u present=%d flags=0x%08x "
		    "can_beacon=%d\n",
		    RTW89_AP_FREQ_MHZ, chan != NULL ? 1 : 0,
		    chan != NULL ? (unsigned)chan->flags : 0u, can_beacon);
	if (chan == NULL || !can_beacon) {
		CASE_ANCHOR("rtw89_ap: FAIL channel not beaconable\n");
		return 4;
	}

	/* --- A1: the default STA interface becomes the AP ---------------- */
	if (netif_running(dev))
		(void)dev_close(dev);
	rc = lkpi_nl80211_set_interface(dev, NL80211_IFTYPE_AP);
	CASE_ANCHOR("rtw89_ap: set_iface rc=%d type=%d\n", rc,
		    lkpi_nl80211_iftype(dev));
	if (rc != 0 || lkpi_nl80211_iftype(dev) != NL80211_IFTYPE_AP) {
		CASE_ANCHOR("rtw89_ap: FAIL set_iface rc=%d type=%d\n", rc,
			    lkpi_nl80211_iftype(dev));
		return 5;
	}

	rc = dev_open(dev);
	CASE_ANCHOR("rtw89_ap: start rc=%d\n", rc);
	if (rc != 0) {
		CASE_ANCHOR("rtw89_ap: FAIL dev_open rc=%d\n", rc);
		return 6;
	}

	/* The lwIP netif comes before hostapd: the NXP entry derives the
	 * interface name it configures from the port's uAP netif. */
	rc = lkpi_net_wlan_up(RTW89_AP_BOARD_IP, RTW89_AP_BOARD_MASK, NULL,
			      netif_mac);
	if (rc != 0) {
		CASE_ANCHOR("rtw89_ap: FAIL netif up rc=%d\n", rc);
		return 7;
	}
	CASE_ANCHOR("rtw89_ap: netif up name=wl0 ip=%s/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    RTW89_AP_BOARD_IP, (unsigned char)netif_mac[0],
		    (unsigned char)netif_mac[1], (unsigned char)netif_mac[2],
		    (unsigned char)netif_mac[3], (unsigned char)netif_mac[4],
		    (unsigned char)netif_mac[5]);

	/* --- A1: hostapd brings the AP up (beacon via src/ap/beacon.c) --- */
	(void)memset(&ap_cfg, 0, sizeof(ap_cfg));
	ap_cfg.ifname = "wlan0";
	ap_cfg.ssid = RTW89_AP_SSID;
	ap_cfg.channel = (unsigned int)RTW89_AP_CHANNEL;
	ap_cfg.beacon_int = AP_BEACON_INT;
	ap_cfg.dtim = AP_DTIM;
	ap_cfg.passphrase = NULL;
	ap_cfg.freq_mhz = (unsigned int)RTW89_AP_FREQ_MHZ;
	ap_cfg.hw_mode = LKPI_HOSTAP_MODE_11G;
	CASE_ANCHOR("rtw89_ap: security wpa2=%d ssid=%s channel=%u "
		    "bcn_int=%u dtim_req=%u\n", 0, RTW89_AP_SSID,
		    (unsigned int)RTW89_AP_CHANNEL, AP_BEACON_INT, AP_DTIM);
	rc = lkpi_hostap_start(&ap_cfg);
	CASE_ANCHOR("rtw89_ap: hostap start rc=%d ready=%d\n", rc,
		    lkpi_hostap_ready());
	if (rc != 0 || !lkpi_hostap_ready()) {
		CASE_ANCHOR("rtw89_ap: FAIL hostap start rc=%d\n", rc);
		return 10;
	}

	freq = lkpi_nl80211_link_freq(dev);
	CASE_ANCHOR("rtw89_ap: link freq=%d\n", freq);
	CASE_ANCHOR("rtw89_ap: PASS A1\n");

	/* --- A2: the Pi's association, read from mac80211's table --------- */
	CASE_ANCHOR("rtw89_ap: waiting station ms=%u\n", AP_STA_WAIT_MS);
	rc = ap_sta_wait(dev, peer_mac, &sta_n, &hostap_n);
	CASE_ANCHOR("rtw89_ap: sta n=%u hostap=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    sta_n, hostap_n, peer_mac[0], peer_mac[1], peer_mac[2],
		    peer_mac[3], peer_mac[4], peer_mac[5]);
	if (rc != 0) {
		CASE_ANCHOR("rtw89_ap: FAIL no station after %u ms\n",
			    AP_STA_WAIT_MS);
		return 11;
	}
	CASE_ANCHOR("rtw89_ap: PASS A2\n");

	/* The peer's ping window starts here, after the station exists: the
	 * first 8189 AP boot pinged a BSS the Pi had not joined yet. */
	CASE_ANCHOR("rtw89_ap: peer window ms=%u\n", AP_PEER_WINDOW_MS);
	osDelay(AP_PEER_WINDOW_MS);
	osDelay(AP_SETTLE_MS);

	/* --- A3: both directions, then the receive account ----------------- */
	(void)memset(&d0, 0, sizeof(d0));
	lkpi_net_diag(&d0);
	if (ap_ping("small", AP_PING_SMALL) != 0)
		return 12;
	if (ap_ping("large", AP_PING_LARGE) != 0)
		return 13;
	(void)memset(&d1, 0, sizeof(d1));
	lkpi_net_diag(&d1);
	CASE_ANCHOR("rtw89_ap: link window rx_frames=%lu rx_foreign=%lu "
		    "rx_no_netif=%lu rx_input_fail=%lu rx_bytes=%lu "
		    "tx_frames=%lu tx_fail=%lu\n",
		    d1.rx_frames - d0.rx_frames,
		    d1.rx_foreign - d0.rx_foreign,
		    d1.rx_no_netif - d0.rx_no_netif,
		    d1.rx_input_fail - d0.rx_input_fail,
		    d1.rx_bytes - d0.rx_bytes,
		    d1.tx_frames - d0.tx_frames, d1.tx_fail - d0.tx_fail);
	CASE_ANCHOR("rtw89_ap: PASS A3\n");

	if (!AP_TP) {
		CASE_ANCHOR("rtw89_ap: PASS AP\n");
		return 0;
	}

#if AP_TP
	rc = lkpi_net_iperf_server_start(AP_TCP_PORT);
	if (rc == 0)
		rc = lkpi_net_udp_sink_start(AP_UDP_PORT);
	CASE_ANCHOR("rtw89_ap: server tcp=%u udp=%u ok=%d\n",
		    AP_TCP_PORT, AP_UDP_PORT, (rc == 0) ? 1 : 0);
	if (rc != 0) {
		CASE_ANCHOR("rtw89_ap: FAIL listener rc=%d\n", rc);
		return 14;
	}

	{
		unsigned int i;
		unsigned int ok = 0u;

		for (i = 0u; i < (unsigned int)AP_PLAN_LEN; i++) {
			rc = ap_run_phase(i + 1u, &ap_plan[i]);
			if (rc == 0)
				ok++;
		}
		if (ok != AP_TRAFFIC_PHASES) {
			CASE_ANCHOR("rtw89_ap: FAIL traffic phases %u/%u\n",
				    ok, AP_TRAFFIC_PHASES);
			return 15;
		}
	}
	CASE_ANCHOR("rtw89_ap: PASS APT\n");
	return 0;
#else
	return 0;
#endif /* AP_TP */
}
