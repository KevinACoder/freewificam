/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   rtl8189fs_ap.c
 * @brief  Case rtl8189fs_ap: the board's SDIO 8189FTV becomes an AP (A1), the
 *         Pi's MT7921AU joins it (A2), the data plane is proven (A3), then the
 *         AP phase table runs (A4).
 *
 * The attach is rtl8189fs_sta's (SDIO backend, module_init, engine start, the
 * vendor probe).  What changes is the role and the owner of the MLME:
 *
 *   - The vendor stack's own AP MLME (autoconf.h defines CONFIG_AP_MODE and
 *     CONFIG_NATIVEAP_MLME, so CONFIG_HOSTAPD_MLME never applies) answers
 *     probe/auth/assoc and sends the beacons.  8188F has no firmware beaconing
 *     (CONFIG_SWTIMER_BASED_TXBCN), so `start_bss_network` arms
 *     `dvobj->txbcn_timer` and `tx_beacon_handlder` re-arms it and calls
 *     `send_beacon()` from the FreeRTOS timer daemon task - a TX every 100 ms
 *     from that context, which is this role's platform risk and the reason the
 *     A1 anchors report the BSS state and the timer task's stack headroom.
 *   - hostapd (the imported NXP port) supplies the beacon's IE set: its
 *     set_ap builds the 36-byte head + IE tail and NL80211_CMD_START_AP runs
 *     nl80211's real handler, which calls the vendor's `.start_ap`.  The port
 *     has no .set_beacon op, so hostapd never beacons on its own: one beacon
 *     source, the driver's.
 *   - Because there is no netlink socket in this image (ADR
 *     2026-11-10-nl80211-scope D1), the vendor's cfg80211_new_sta()
 *     indication is a multicast into nothing: hostapd never learns of the
 *     station.  A2 therefore reads the vendor's own asoc_list through the
 *     first-party port (ports/rtos/obs.c), not hostapd's table.
 *
 * The channel is not carried by nl80211: `lkpi_set_freq` is a no-op in this
 * port, and the vendor derives the operating channel from the beacon's DS/HT
 * IEs (`rtw_check_beacon_data` -> `rtw_ies_get_chbw`).  hostapd's
 * channel=5/freq=2432 is therefore the authority, and the monitor's radiotap
 * channel field is the only honest check that it took.
 *
 * Direction words are the round's (docs/HANDOFF-8189FTV-RTOS-AP.md): air=down
 * is AP->STA (this board sends; the Pi's server/sink receives), air=up is
 * STA->AP (the Pi sends; this board's lwIP receives) - the opposite of the C6
 * case's board roles with the same phase names.
 *
 * Console discipline (the GT AP round's 110 ms lesson): the vendor's log level
 * stays at INFO for the bring-up so the AP path's milestone lines are emitted,
 * and this case's sink prints only that whitelist; the level drops to errors
 * before the traffic phases.  `lkpi_vlog` formats each line but the sink owns
 * the console, so an unprinted line costs microseconds, not 13 ms.
 *
 * Anchors:
 *   rtl8189fs_ap: core ready
 *   rtl8189fs_ap: initcalls run
 *   rtl8189fs_ap: wlan0 present=1 ifindex=<n> mac=<mac> iftype=<n>
 *   rtl8189fs_ap: start rc=<n>
 *   rtl8189fs_ap: chan freq=<mhz> present=<0|1> flags=0x<hex> can_beacon=<0|1>
 *   rtl8189fs_ap: set_iface rc=<n> type=<n> order=<up_first|down_first>
 *   rtl8189fs_ap: netif up name=wlan0 ip=<ip>/24 mac=<mac>
 *   rtl8189fs_ap: security wpa2=<0|1> ssid=<ssid> channel=<n> bcn_int=<n> dtim_req=<n>
 *   rtl8189fs_ap: hostap start rc=<n> ready=<0|1>
 *   rtl8189fs_ap: mlme valid=<0|1> fwstate=0x<hex> ap_state=<0|1> bstart=<0|1> ch=<n> bcn_int=<n> nr_ap_if=<n>
 *   rtl8189fs_ap: resources heap_free=<n> heap_min=<n> total=<n>
 *   rtl8189fs_ap: drv-<start|assoc> t=<ms> <driver line>   (buffered, printed after the gate)
 *   rtl8189fs_ap: PASS A1
 *   rtl8189fs_ap: waiting station ms=<n>
 *   rtl8189fs_ap: peer window ms=<n>   (after A2: the peer's ping window)
 *   rtl8189fs_ap: link traffic rx_frames=<n> tx_frames=<n> rx_bytes=<n> tx_bytes=<n> tx_fail=<n>
 *   rtl8189fs_ap: sta n=<n> mac=<mac> aid=<n> state=0x<hex> flags=0x<hex> rate_id=<n> cur_rate=<n> bw=<n> sgi=<n> rx_pkts=<n> tx_pkts=<n> tx_bytes=<n>
 *   rtl8189fs_ap: PASS A2
 *   rtl8189fs_ap: ping payload=<n> tx=<n> rx=<n> dup=<n> rtt_ms=<min>/<avg>/<max>
 *   rtl8189fs_ap: link window rx_frames=<n> rx_foreign=<n> rx_no_netif=<n> rx_input_fail=<n> rx_bytes=<n> tx_frames=<n> tx_fail=<n>
 *   rtl8189fs_ap: PASS A3
 *   rtl8189fs_ap: log level 4->2
 *   rtl8189fs_ap: server tcp=<n> udp=<n> ok=<0|1>
 *   rtl8189fs_ap: phase <n> begin proto=<p> role=<r> air=<down|up|local> port=<n> window_ms=<n> name=<n> peer_ms=<n> rate_kbps=<n>
 *   rtl8189fs_ap: phase <n> report bytes=<n> ms=<n> kbit=<n> pkts=<n>
 *   rtl8189fs_ap: phase <n> stack tcp_recv=<n> tcp_xmit=<n> tcp_drop=<n> tcp_memerr=<n> tcp_err=<n> pbuf_err=<n> tcpseg_err=<n>
 *   rtl8189fs_ap: phase <n> ap tx_pkts=<n> rx_pkts=<n> bstart=<n> ch=<n> nr_ap_if=<n>
 *   rtl8189fs_ap: phase <n> done ok=<0|1>
 *   rtl8189fs_ap: PASS AP          (A1-A3 only)
 *   rtl8189fs_ap: PASS APW         (WPA2; stops after A3)
 *   rtl8189fs_ap: PASS APT         (A1-A3 plus all A4 phases)
 *   rtl8189fs_ap: FAIL <reason>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "linuxkpi_core.h"
#include "linux80211/linux80211.h"
#include "linux80211/lkpi_sdio_backend.h"
#include "linux80211/nl80211_probe.h"
#include "linux80211/hostap_ap.h"
#include "linux80211/hostap_drv.h"	/* the hw_mode values, plain C */
#include "linux80211/net.h"
#include "linuxkpi_sdio.h"
#include "rtl8189fs_diag.h"

#include <linux/etherdevice.h>
#include <linux/mmc/sdio_func.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>
#include <net/net_namespace.h>

/* --- this AP ---------------------------------------------------------------
 *
 * The SSID must not collide with the bench AP's (labwifi-ar-ht20s): the Pi's
 * STA scans for this one, and a monitor filter on the same string would count
 * the wrong BSS if the bench were ever left up.
 */
#ifndef RTL8189FS_AP_SSID
#define RTL8189FS_AP_SSID	"rtos-8189-ap"
#endif
#ifndef RTL8189FS_AP_FREQ_MHZ
#define RTL8189FS_AP_FREQ_MHZ	2432u	/* channel 5, the GT AP arm's channel */
#endif
#ifndef RTL8189FS_AP_CHANNEL
#define RTL8189FS_AP_CHANNEL	5u
#endif
#ifndef RTL8189FS_AP_BOARD_IP
#define RTL8189FS_AP_BOARD_IP	"192.168.99.1"
#endif
#ifndef RTL8189FS_AP_BOARD_MASK
#define RTL8189FS_AP_BOARD_MASK	"255.255.255.0"
#endif
#ifndef RTL8189FS_AP_PEER_IP
#define RTL8189FS_AP_PEER_IP	"192.168.99.2"
#endif

#define AP_BEACON_INT		100u
#define AP_DTIM			2u
#define AP_IFNAME		"wlan0"

/* The WPA2 variant: one image-level switch, set by
 * rk3568-rtl8189fs-ap-wpa2.defconfig.  The passphrase is a bench credential,
 * not a secret; the Pi helper carries the same constant. */
#if defined(CONFIG_RTL8189FS_AP_WPA2) && CONFIG_RTL8189FS_AP_WPA2
#define AP_WPA2			1
#ifndef CONFIG_RTL8189FS_AP_PSK
#define CONFIG_RTL8189FS_AP_PSK	"rtos-8189-wpa2-psk"
#endif
#define AP_PASSPHRASE		CONFIG_RTL8189FS_AP_PSK
#else
#define AP_WPA2			0
#define AP_PASSPHRASE		NULL
#endif

/* The traffic table is a separate image (the C6T arm's rule: a throughput
 * verdict and a functional verdict must not share an image). */
#if defined(CONFIG_RTL8189FS_AP_TP) && CONFIG_RTL8189FS_AP_TP
#define AP_TP			1
#else
#define AP_TP			0
#endif

#define AP_WAIT_MS		40000U
#define AP_POLL_MS		100U
#define AP_SETTLE_MS		3000U
#define AP_STA_WAIT_MS		45000U
#define AP_PEER_WINDOW_MS	12000U
#define AP_PING_COUNT		20U
#define AP_PING_TIMEOUT_MS	300U
#define AP_PING_SMALL		56U
#define AP_PING_LARGE		1400U
#define AP_TCP_PORT		5001u
#define AP_UDP_PORT		5002u
#define AP_LOG_MAX		160U

/* The phase windows: the lwIP client is time-limited by lwiperf (10 s), so the
 * board-sent TCP phases use 12 s, the same shape the 7260 AP arm and the GT
 * arm's -t 10 use.  The board-received TCP phase can run longer. */
#define AP_LOOP_MS		12000u
#define AP_TCPDN_MS		12000u
#define AP_UDPDN_MS		12000u
#define AP_TCPUP_MS		30000u
#define AP_UDPUP_MS		20000u
/* The peer's receiver is started by ssh + sudo on the harness side; the
 * throughput arm needs more slack than the functional one because a phase that
 * loses its listener reports 0 bytes and costs a boot. */
#if AP_TP
#define AP_PEER_MS		3000u
#else
#define AP_PEER_MS		1500u
#endif

#define AP_UDP_RATE_15		15000u
#define AP_UDP_RATE_25		25000u
#define AP_UDP_RATE_35		35000u

/* The vendor's log levels (rtw_debug.h): 4 = _DRV_INFO_, 2 = _DRV_ERR_. */
#define RTW_LOG_ERR		2
#define RTW_LOG_INFO		4
#define PS_MODE_ACTIVE		0
#define LPS_OFF			0

void lkpi_sdio_fsl_install(void);
void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));

extern int rtw_power_mgnt;
extern int rtw_lps_level;
extern unsigned int rtw_drv_log_level;

/*
 * The AP path's milestone lines are *buffered*, not printed, and dumped after
 * the association has completed.  This is the GT round's 110 ms lesson applied
 * at the source: on this 115200 console one line costs 10-13 ms, and the peer's
 * association budget is 25 ms - the two lines that matter (OnAssocReq, then
 * issue_asocrsp) would eat the whole budget if printed from inside it.  A
 * timestamp taken here is what makes the dump usable as the timing evidence
 * that GT read from dmesg.
 */
struct ap_log_line {
	unsigned int	ms;
	char		text[176];
};

static struct ap_log_line ap_log_buf[AP_LOG_MAX];
static unsigned int ap_log_lines;

static unsigned int
ap_now_ms(void)
{
	return (unsigned int)(osKernelGetTickCount() * 1000u /
			      (unsigned int)osKernelGetTickFreq());
}

static void
ap_sink(ulog_level_t level, const char *line)
{
	(void)level;
	if (line == NULL || ap_log_lines >= AP_LOG_MAX)
		return;
	if (strstr(line, "start_bss_network") != NULL ||
	    strstr(line, "SW BCN TIMER") != NULL ||
	    strstr(line, "OnAssocReq") != NULL ||
	    strstr(line, "issue_asocrsp") != NULL ||
	    strstr(line, "OnAssocRsp") != NULL ||
	    strstr(line, "indicate_sta_assoc") != NULL ||
	    strstr(line, "start_ap") != NULL ||
	    strstr(line, "add bcn") != NULL ||
	    strstr(line, "lkpinetif:") != NULL ||
	    strstr(line, "ERROR") != NULL ||
	    strstr(line, "WARN") != NULL) {
		struct ap_log_line *slot = &ap_log_buf[ap_log_lines];

		slot->ms = ap_now_ms();
		(void)snprintf(slot->text, sizeof(slot->text), "%s", line);
		ap_log_lines++;
	}
}

/* Dump what the sink kept, with the millisecond stamp the sink took.  Called
 * only where printing is off the association's critical path. */
static void
ap_dump_logs(const char *tag)
{
	unsigned int i;

	for (i = 0u; i < ap_log_lines; i++)
		CASE_ANCHOR("rtl8189fs_ap: drv-%s t=%u %s", tag,
			    ap_log_buf[i].ms, ap_log_buf[i].text);
	ap_log_lines = 0u;
}

/* The A2 witness: the vendor's own associated-station list.  hostapd's table
 * stays empty in this image and is not a witness (see the header). */
static void
ap_print_station(const char *tag)
{
	struct rtl8189f_ap_diag d;

	(void)memset(&d, 0, sizeof(d));
	rtl8189f_ap_diag_get(&d);
	if (!d.valid) {
		CASE_ANCHOR("rtl8189fs_ap: diag %s valid=0\n", tag);
		return;
	}
	CASE_ANCHOR("rtl8189fs_ap: diag %s valid=1 fwstate=0x%08x ap_state=%u "
		    "bstart=%u ch=%u bcn_int=%u nr_ap_if=%u nr_sta=%u "
		    "tx_pkts=%u rx_pkts=%u\n",
		    tag, d.fwstate, d.ap_state, d.bstart_bss, d.cur_channel,
		    d.bcn_interval, d.nr_ap_if, d.nr_sta, d.tx_pkts, d.rx_pkts);
}

static int
ap_wait_station(struct rtl8189f_ap_diag *out, unsigned int *waited_ms)
{
	unsigned int waited = 0U;

	while (waited < AP_STA_WAIT_MS) {
		(void)memset(out, 0, sizeof(*out));
		rtl8189f_ap_diag_get(out);
		if (out->valid && out->nr_sta != 0u) {
			*waited_ms = waited;
			return 0;
		}
		osDelay(AP_POLL_MS);
		waited += AP_POLL_MS;
	}
	*waited_ms = waited;
	return -1;
}

/* --- the A4 table (CONFIG_RTL8189FS_AP_TP) -------------------------------- */

#if AP_TP
enum ap_phase_kind {
	AP_LOOPBACK,
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
 * the AP's roles: down = this board sends.  The 15/25 rungs are the ones the
 * GT AP arm measured clean; 35 is a reference rung only (GT's 35/45 were
 * truncated by peer phase racing). */
static const struct ap_phase ap_plan[] = {
	{ "loopback", "tcp", "client", "local", AP_TCP_PORT, AP_LOOP_MS, 0u,
	  AP_LOOPBACK },
	{ "tcpdn",    "tcp", "client", "down",  AP_TCP_PORT, AP_TCPDN_MS, 0u,
	  AP_TCP_DOWN },
	{ "udpdn15",  "udp", "client", "down",  AP_UDP_PORT, AP_UDPDN_MS,
	  AP_UDP_RATE_15, AP_UDP_DOWN },
	{ "udpdn25",  "udp", "client", "down",  AP_UDP_PORT, AP_UDPDN_MS,
	  AP_UDP_RATE_25, AP_UDP_DOWN },
	{ "tcpdn2",   "tcp", "client", "down",  AP_TCP_PORT, AP_TCPDN_MS, 0u,
	  AP_TCP_DOWN },
	{ "udpdn35",  "udp", "client", "down",  AP_UDP_PORT, AP_UDPDN_MS,
	  AP_UDP_RATE_35, AP_UDP_DOWN },
	{ "tcpup",    "tcp", "server", "up",    AP_TCP_PORT, AP_TCPUP_MS, 0u,
	  AP_TCP_UP },
	{ "udpup",    "udp", "server", "up",    AP_UDP_PORT, AP_UDPUP_MS, 0u,
	  AP_UDP_UP },
};

#define AP_PLAN_LEN	(sizeof(ap_plan) / sizeof(ap_plan[0]))
#define AP_TRAFFIC_PHASES	7u

static int
ap_run_phase(unsigned int n, const struct ap_phase *ph)
{
	struct rtl8189f_ap_diag d;
	struct lkpi_net_phase r;
	struct lkpi_net_stack_diag s0, s1;
	int rc;

	(void)memset(&r, 0, sizeof(r));
	(void)memset(&s0, 0, sizeof(s0));
	(void)memset(&s1, 0, sizeof(s1));
	lkpi_net_stack_diag(&s0);
	CASE_ANCHOR("rtl8189fs_ap: phase %u begin proto=%s role=%s air=%s "
		    "port=%u window_ms=%u name=%s peer_ms=%u rate_kbps=%u\n",
		    n, ph->proto, ph->role, ph->air, ph->port, ph->window_ms,
		    ph->name, AP_PEER_MS, ph->rate_kbps);

	/* The harness starts the peer's receiver/sender on this line; the
	 * margin is bench latency and sits outside the phase's window. */
	if (ph->kind != AP_LOOPBACK)
		osDelay(AP_PEER_MS);

	switch (ph->kind) {
	case AP_LOOPBACK:
		rc = lkpi_net_loopback(ph->port, ph->window_ms, &r);
		break;
	case AP_TCP_DOWN:
		rc = lkpi_net_iperf_up(RTL8189FS_AP_PEER_IP, ph->port,
				       ph->window_ms, &r);
		break;
	case AP_UDP_DOWN:
		rc = lkpi_net_udp_blast(RTL8189FS_AP_PEER_IP, ph->port,
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

	CASE_ANCHOR("rtl8189fs_ap: phase %u report bytes=%u ms=%u kbit=%u "
		    "pkts=%u\n", n, r.bytes, r.ms, r.kbit, r.pkts);
	/* The lwIP side of the same window: whether a phase that moved fewer
	 * bytes than the air could carry did so because the stack refused a
	 * segment (memerr/pbuf_err/tcpseg_err) or because it never sent
	 * (tcp_xmit), or because the peer's segments were dropped (tcp_recv vs
	 * tcp_drop).  This is the first A4 round's missing observation surface. */
	lkpi_net_stack_diag(&s1);
	CASE_ANCHOR("rtl8189fs_ap: phase %u stack tcp_recv=%lu tcp_xmit=%lu "
		    "tcp_drop=%lu tcp_memerr=%lu tcp_err=%lu pbuf_err=%lu "
		    "tcpseg_err=%lu\n",
		    n, s1.tcp_recv - s0.tcp_recv, s1.tcp_xmit - s0.tcp_xmit,
		    s1.tcp_drop - s0.tcp_drop, s1.tcp_memerr - s0.tcp_memerr,
		    s1.tcp_err - s0.tcp_err, s1.pbuf_err - s0.pbuf_err,
		    s1.tcpseg_err - s0.tcpseg_err);
	(void)memset(&d, 0, sizeof(d));
	rtl8189f_ap_diag_get(&d);
	CASE_ANCHOR("rtl8189fs_ap: phase %u ap tx_pkts=%u rx_pkts=%u "
		    "bstart=%u ch=%u nr_ap_if=%u\n",
		    n, d.tx_pkts, d.rx_pkts, d.bstart_bss, d.cur_channel,
		    d.nr_ap_if);
	CASE_ANCHOR("rtl8189fs_ap: phase %u done ok=%d\n", n,
		    (rc == 0) ? 1 : 0);
	return rc;
}
#endif /* AP_TP */

int
case_rtl8189fs_ap_run(void)
{
	struct net_device *dev = NULL;
	struct wireless_dev *wdev;
	struct ieee80211_channel *chan;
	struct cfg80211_chan_def chandef;
	struct lkpi_hostap_cfg ap_cfg;
	struct rtl8189f_ap_diag ad;
	unsigned char mac[6];
	char netif_mac[6];
	char order[16];
	unsigned int waited = 0U;
	unsigned int sta_ms = 0U;
	unsigned int ping_rx = 0U;
	unsigned int rtt_min = 0U, rtt_avg = 0U, rtt_max = 0U;
	struct lkpi_net_diag d0, d1;
	int can_beacon;
	int rc;

	lkpi_core_init();
	CASE_ANCHOR("rtl8189fs_ap: core ready\n");
	lkpi_log_sink_register(ap_sink);

	/* The bring-up runs at INFO so the AP path's milestones are emitted;
	 * the sink above keeps them off the console. */
	rtw_power_mgnt = PS_MODE_ACTIVE;
	rtw_lps_level = LPS_OFF;
	rtw_drv_log_level = RTW_LOG_INFO;

	lkpi_sdio_fsl_install();
	lkpi_module_init_run();
	CASE_ANCHOR("rtl8189fs_ap: initcalls run\n");

	lkpi_sdio_start();

	for (waited = 0U; waited < AP_WAIT_MS; waited += AP_POLL_MS) {
		dev = dev_get_by_name(&init_net, AP_IFNAME);
		if (dev != NULL)
			break;
		osDelay(AP_POLL_MS);
	}
	if (dev == NULL) {
		CASE_ANCHOR("rtl8189fs_ap: FAIL no wlan0 after %u ms\n", waited);
		return 1;
	}
	wdev = dev->ieee80211_ptr;
	if (wdev == NULL || wdev->wiphy == NULL) {
		CASE_ANCHOR("rtl8189fs_ap: FAIL no wireless_dev\n");
		return 2;
	}
	(void)memcpy(mac, dev->dev_addr, 6);
	CASE_ANCHOR("rtl8189fs_ap: wlan0 present=1 ifindex=%d "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x iftype=%d\n",
		    dev->ifindex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		    (int)wdev->iftype);

	/* The netdev comes up first: that is the order the GT AP arm ran (the
	 * driver was already up as a managed interface when hostapd changed its
	 * type).  The mac80211-documented down->set->up order is the recorded
	 * fallback, not the first attempt - and whichever order produced rc=0
	 * is printed. */
	if (!netif_running(dev)) {
		rc = dev_open(dev);
		CASE_ANCHOR("rtl8189fs_ap: start rc=%d\n", rc);
		if (rc != 0) {
			CASE_ANCHOR("rtl8189fs_ap: FAIL dev_open rc=%d\n", rc);
			return 3;
		}
	} else {
		CASE_ANCHOR("rtl8189fs_ap: start rc=0 (already up)\n");
	}

	chan = ieee80211_get_channel(wdev->wiphy, (int)RTL8189FS_AP_FREQ_MHZ);
	can_beacon = 0;
	if (chan != NULL) {
		cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_HT20);
		can_beacon = cfg80211_reg_can_beacon(wdev->wiphy, &chandef,
						     NL80211_IFTYPE_AP) ? 1 : 0;
	}
	CASE_ANCHOR("rtl8189fs_ap: chan freq=%u present=%d flags=0x%08x "
		    "can_beacon=%d\n",
		    RTL8189FS_AP_FREQ_MHZ, chan != NULL ? 1 : 0,
		    chan != NULL ? (unsigned)chan->flags : 0u, can_beacon);

	rc = lkpi_nl80211_set_interface(dev, NL80211_IFTYPE_AP);
	(void)strcpy(order, "up_first");
	if (rc != 0 || lkpi_nl80211_iftype(dev) != NL80211_IFTYPE_AP) {
		/* The fallback order, printed as such: both results land in
		 * the same anchor so a later boot can tell which one worked. */
		CASE_ANCHOR("rtl8189fs_ap: set_iface rc=%d type=%d order=up_first\n",
			    rc, lkpi_nl80211_iftype(dev));
		(void)dev_close(dev);
		rc = lkpi_nl80211_set_interface(dev, NL80211_IFTYPE_AP);
		(void)strcpy(order, "down_first");
		(void)dev_open(dev);
	}
	CASE_ANCHOR("rtl8189fs_ap: set_iface rc=%d type=%d order=%s\n", rc,
		    lkpi_nl80211_iftype(dev), order);
	if (rc != 0 || lkpi_nl80211_iftype(dev) != NL80211_IFTYPE_AP) {
		CASE_ANCHOR("rtl8189fs_ap: FAIL set_iface rc=%d type=%d\n", rc,
			    lkpi_nl80211_iftype(dev));
		return 4;
	}

	/* The AP is the peer's on-link gateway; a default route is wrong here
	 * (net.h documents gw == NULL for this role). */
	rc = lkpi_net_wlan_up(RTL8189FS_AP_BOARD_IP, RTL8189FS_AP_BOARD_MASK,
			      NULL, netif_mac);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_ap: FAIL netif up rc=%d\n", rc);
		return 5;
	}
	CASE_ANCHOR("rtl8189fs_ap: netif up name=wlan0 ip=%s/24 "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    RTL8189FS_AP_BOARD_IP, (unsigned char)netif_mac[0],
		    (unsigned char)netif_mac[1], (unsigned char)netif_mac[2],
		    (unsigned char)netif_mac[3], (unsigned char)netif_mac[4],
		    (unsigned char)netif_mac[5]);

	/* --- A1: hostapd supplies the beacon, the vendor's MLME sends it --- */
	(void)memset(&ap_cfg, 0, sizeof(ap_cfg));
	ap_cfg.ifname = AP_IFNAME;
	ap_cfg.ssid = RTL8189FS_AP_SSID;
	ap_cfg.channel = RTL8189FS_AP_CHANNEL;
	ap_cfg.beacon_int = AP_BEACON_INT;
	ap_cfg.dtim = AP_DTIM;
	ap_cfg.passphrase = AP_PASSPHRASE;
	ap_cfg.freq_mhz = RTL8189FS_AP_FREQ_MHZ;
	ap_cfg.hw_mode = LKPI_HOSTAP_MODE_11G;
	/* dtim_req is what hostapd is asked for; the on-air TIM's DTIM is the
	 * vendor's own constant (rtw_ap.c:153 writes period 1 unconditionally),
	 * which the monitor confirms.  Printed apart so no one reads the
	 * request as the air fact. */
	CASE_ANCHOR("rtl8189fs_ap: security wpa2=%d ssid=%s channel=%u "
		    "bcn_int=%u dtim_req=%u\n",
		    AP_WPA2, RTL8189FS_AP_SSID, RTL8189FS_AP_CHANNEL,
		    AP_BEACON_INT, AP_DTIM);

	rc = lkpi_hostap_start(&ap_cfg);
	CASE_ANCHOR("rtl8189fs_ap: hostap start rc=%d ready=%d\n", rc,
		    lkpi_hostap_ready());
	if (rc != 0 || !lkpi_hostap_ready()) {
		ap_print_station("hostap-fail");
		CASE_ANCHOR("rtl8189fs_ap: FAIL hostap start rc=%d ready=%d\n",
			    rc, lkpi_hostap_ready());
		return 6;
	}

	/* The BSS state, the channel the vendor derived from the beacon's DS IE,
	 * and the two resource numbers A1's failure mode needs: the beacon TX
	 * runs in the FreeRTOS timer daemon task, and a starved heap turns into
	 * "no beacons" with no other symptom. */
	ap_print_station("post-start");
	ap_dump_logs("start");
	{
		struct osMemoryStats heap;

		osMemoryGetStats(&heap);
		CASE_ANCHOR("rtl8189fs_ap: resources heap_free=%u heap_min=%u "
			    "total=%u\n",
			    (unsigned)heap.free_bytes,
			    (unsigned)heap.min_free_bytes,
			    (unsigned)heap.total_bytes);
	}
	CASE_ANCHOR("rtl8189fs_ap: PASS A1\n");

	/* --- A2: the peer joins; the witness is the vendor's station list --- */
	CASE_ANCHOR("rtl8189fs_ap: waiting station ms=%u\n", AP_STA_WAIT_MS);
	lkpi_net_diag(&d0);
	rc = ap_wait_station(&ad, &sta_ms);
	if (rc != 0) {
		ap_print_station("no-sta");
		CASE_ANCHOR("rtl8189fs_ap: FAIL no station after %u ms "
			    "ready=%d\n", sta_ms, lkpi_hostap_ready());
		return 7;
	}
	lkpi_net_diag(&d1);
	CASE_ANCHOR("rtl8189fs_ap: link traffic rx_frames=%lu tx_frames=%lu "
		    "rx_bytes=%lu tx_bytes=%lu tx_fail=%lu\n",
		    d1.rx_frames - d0.rx_frames, d1.tx_frames - d0.tx_frames,
		    d1.rx_bytes - d0.rx_bytes, d1.tx_bytes - d0.tx_bytes,
		    d1.tx_fail - d0.tx_fail);
	CASE_ANCHOR("rtl8189fs_ap: sta n=%u mac=%02x:%02x:%02x:%02x:%02x:%02x "
		    "aid=%u state=0x%x flags=0x%x rate_id=%u cur_rate=%u "
		    "bw=%u sgi=%u rx_pkts=%u tx_pkts=%u tx_bytes=%u\n",
		    ad.nr_sta, ad.sta[0].mac[0], ad.sta[0].mac[1],
		    ad.sta[0].mac[2], ad.sta[0].mac[3], ad.sta[0].mac[4],
		    ad.sta[0].mac[5], ad.sta[0].aid, ad.sta[0].state,
		    ad.sta[0].flags, ad.sta[0].rate_id, ad.sta[0].cur_rate,
		    ad.sta[0].bw_mode, ad.sta[0].sgi, ad.sta[0].rx_data_pkts,
		    ad.sta[0].tx_pkts, ad.sta[0].tx_bytes);
	CASE_ANCHOR("rtl8189fs_ap: PASS A2\n");
	ap_dump_logs("assoc");

	/* --- A3: this board pings the peer -------------------------------
	 * The peer->AP direction is the harness's (`pi-ap.sh ping` runs its own
	 * 20/20 and the runner records it).  Its window is announced here, after
	 * the association exists: the first AP boot announced it before the
	 * station was up, and the peer pinged a BSS it had not joined yet. */
	CASE_ANCHOR("rtl8189fs_ap: peer window ms=%u\n", AP_PEER_WINDOW_MS);
	osDelay(AP_PEER_WINDOW_MS);
	osDelay(AP_SETTLE_MS);
	rc = lkpi_net_ping(RTL8189FS_AP_PEER_IP, AP_PING_COUNT, AP_PING_SMALL,
			   AP_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("rtl8189fs_ap: ping payload=%u tx=%u rx=%u "
		    "dup=%u rtt_ms=%u/%u/%u\n",
		    AP_PING_SMALL, AP_PING_COUNT, ping_rx,
		    ping_rx > AP_PING_COUNT ? ping_rx - AP_PING_COUNT : 0u,
		    rtt_min, rtt_avg, rtt_max);
	/* The gate is "every request was answered".  A reply count above the
	 * request count is a duplicate delivery, not a failure - it is printed
	 * as dup= and judged separately (the 8189's first AP boot read 21/20
	 * with the adapter reporting rc=-1 for exactly that reason). */
	if (ping_rx < AP_PING_COUNT) {
		CASE_ANCHOR("rtl8189fs_ap: FAIL ping small rc=%d rx=%u\n", rc,
			    ping_rx);
		return 8;
	}
	rc = lkpi_net_ping(RTL8189FS_AP_PEER_IP, AP_PING_COUNT, AP_PING_LARGE,
			   AP_PING_TIMEOUT_MS, &ping_rx, &rtt_min, &rtt_avg,
			   &rtt_max);
	CASE_ANCHOR("rtl8189fs_ap: ping payload=%u tx=%u rx=%u "
		    "dup=%u rtt_ms=%u/%u/%u\n",
		    AP_PING_LARGE, AP_PING_COUNT, ping_rx,
		    ping_rx > AP_PING_COUNT ? ping_rx - AP_PING_COUNT : 0u,
		    rtt_min, rtt_avg, rtt_max);
	if (ping_rx < AP_PING_COUNT) {
		CASE_ANCHOR("rtl8189fs_ap: FAIL ping large rc=%d rx=%u\n", rc,
			    ping_rx);
		return 9;
	}
	/* The whole A3 window's receive account: the peer's own ping/ARP walk
	 * happened inside it, and these five counters separate "the driver never
	 * delivered the frame" (rx_frames flat) from "the adapter refused it"
	 * (rx_foreign/rx_no_netif/input_fail moving) from "it never left the
	 * air" (none of them move). */
	lkpi_net_diag(&d1);
	CASE_ANCHOR("rtl8189fs_ap: link window rx_frames=%lu rx_foreign=%lu "
		    "rx_no_netif=%lu rx_input_fail=%lu rx_bytes=%lu "
		    "tx_frames=%lu tx_fail=%lu\n",
		    d1.rx_frames - d0.rx_frames,
		    d1.rx_foreign - d0.rx_foreign,
		    d1.rx_no_netif - d0.rx_no_netif,
		    d1.rx_input_fail - d0.rx_input_fail,
		    d1.rx_bytes - d0.rx_bytes,
		    d1.tx_frames - d0.tx_frames, d1.tx_fail - d0.tx_fail);
	ap_dump_logs("a3");
	CASE_ANCHOR("rtl8189fs_ap: PASS A3\n");

	if (AP_WPA2) {
		/* The 4-way is a later arm of this round; the encryption gate
		 * stops here once it exists. */
		CASE_ANCHOR("rtl8189fs_ap: PASS APW\n");
		return 0;
	}
	if (!AP_TP) {
		CASE_ANCHOR("rtl8189fs_ap: PASS AP\n");
		return 0;
	}

#if AP_TP
	CASE_ANCHOR("rtl8189fs_ap: log level 4->2\n");
	rtw_drv_log_level = RTW_LOG_ERR;

	rc = lkpi_net_iperf_server_start(AP_TCP_PORT);
	if (rc == 0)
		rc = lkpi_net_udp_sink_start(AP_UDP_PORT);
	CASE_ANCHOR("rtl8189fs_ap: server tcp=%u udp=%u ok=%d\n",
		    AP_TCP_PORT, AP_UDP_PORT, (rc == 0) ? 1 : 0);
	if (rc != 0) {
		CASE_ANCHOR("rtl8189fs_ap: FAIL listener rc=%d\n", rc);
		return 10;
	}

	{
		unsigned int i;
		unsigned int ok = 0u;

		for (i = 0u; i < (unsigned int)AP_PLAN_LEN; i++) {
			rc = ap_run_phase(i + 1u, &ap_plan[i]);
			if (rc == 0) {
				if (ap_plan[i].kind != AP_LOOPBACK)
					ok++;
			} else if (ap_plan[i].kind == AP_LOOPBACK) {
				CASE_ANCHOR("rtl8189fs_ap: note loopback failed "
					    "(attribution only)\n");
			}
		}
		if (ok != AP_TRAFFIC_PHASES) {
			CASE_ANCHOR("rtl8189fs_ap: FAIL traffic phases %u/%u\n",
				    ok, AP_TRAFFIC_PHASES);
			return 11;
		}
	}
	CASE_ANCHOR("rtl8189fs_ap: PASS APT\n");
	return 0;
#else
	return 0;
#endif /* AP_TP */
}
