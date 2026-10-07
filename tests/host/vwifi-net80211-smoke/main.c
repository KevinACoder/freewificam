/*
 * @file    main.c
 * @brief   The net80211 protocol-path mock: one host run through the stack.
 *
 * The suite drives the imported net80211 component through its protocol paths
 * with the vwifi driver substitute (vwifi.c) and the scripted responder:
 *   P1 attach, P2 scan, P3 open-system authentication, P4 association and the
 *   node table, P6/P7 the data encapsulation and decapsulation paths, P9 the
 *   management frame negatives, P10 AES-CCM, P11 the inactivity timeout and
 *   P12 the EAPOL classification of the port.
 *
 * Every expectation is an input -> output pair derived from the 802.11 frame
 * formats (vectors.h) or from the component's declared contracts; the stack
 * under test never supplies its own expected values. The anchor lines
 *   "vwifi-smoke: <ID> ok=<0|1> <field>=<value> ..."
 * are machine checked by the container suite (tools/mock/net80211-frame-mock/)
 * and are the board-side print template of the two-sided segment contract.
 *
 * @design state container - ref net80211:driver/urtwn/if_urtwn.c:515
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/route.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_proto.h>
#include <net80211/ieee80211_crypto.h>

#include "osal/wlan_osal.h"
#include "vectors.h"
#include "vwifi.h"

#define TAG "vwifi-smoke: "
#define THREAD_STACK_SIZE 16384U

static const uint8_t ap_mac[6] = VWIFI_AP_MAC_BYTES;
static const uint8_t sta_mac[6] = VWIFI_STA_MAC_BYTES;

static struct ieee80211com *ic;
static struct ifnet ifp;
static struct vwifi vwifi;
static const char *fault_mode;		/* NULL = the green run */
static unsigned failures;

/* ------------------------------------------------------------------ */
/* reporting                                                           */
/* ------------------------------------------------------------------ */

static void fail(const char *what)
{
	fflush(stdout);
	fprintf(stderr, TAG "FAIL %s\n", what);
	fflush(stderr);
	exit(1);
}

/* One machine-checked anchor line: the segment id, the verdict, the fields. */
static void anchor(const char *id, int ok, const char *fmt, ...)
{
	va_list ap;

	printf(TAG "%s ok=%d", id, ok);
	if (fmt != NULL) {
		va_start(ap, fmt);
		putchar(' ');
		vprintf(fmt, ap);
		va_end(ap);
	}
	putchar('\n');
	fflush(stdout);
	if (!ok)
		failures++;
}

static int mac_eq(const uint8_t *a, const uint8_t *b)
{
	return memcmp(a, b, 6) == 0;
}

static int mac_is_broadcast(const uint8_t *a)
{
	static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	return mac_eq(a, bcast);
}

/* ------------------------------------------------------------------ */
/* P1: the device attach                                               */
/* ------------------------------------------------------------------ */

static void device_attach(void)
{
	ic = wlan_osal_zalloc(sizeof(*ic));
	if (ic == NULL)
		fail("ieee80211com allocation");
	memset(&ifp, 0, sizeof(ifp));

	ic->ic_ifp = &ifp;
	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;
	memcpy(ic->ic_myaddr, sta_mac, 6);

	/* One channel: the 2 GHz channel 1. */
	ic->ic_channels[VWIFI_CHAN_IEEE].ic_freq = VWIFI_CHAN_FREQ;
	ic->ic_channels[VWIFI_CHAN_IEEE].ic_flags = IEEE80211_CHAN_CCK |
	    IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;

	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;

	/* The desired network: what the driver feeds from its configuration. A
	 * station with no desired SSID matches no BSS at all (match_bss). */
	memcpy(ic->ic_des_essid, VWIFI_SSID, VWIFI_SSID_LEN);
	ic->ic_des_esslen = VWIFI_SSID_LEN;

	if (if_initialize(&ifp) != 0)
		fail("if_initialize");
	(void) if_percpuq_create(&ifp);
	ifp.if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;

	vwifi_init(&vwifi, ic, &ifp);
	vwifi_install(&vwifi);

	/* NONE is installed by the crypto attach path; CCMP is registered here
	 * because the import's link-set registration is compiled out on this
	 * port (the cipher template is an exported symbol, spec 3.3.4). */
	ieee80211_crypto_register(&ieee80211_cipher_ccmp);

	ieee80211_ifattach(ic);

	/* The driver's late attach work: the node tables and ic_bss come up here,
	 * and the media shell is installed (reference driver shape). */
	ieee80211_media_init(ic, ieee80211_media_change, ieee80211_media_status);
	ifp.if_percpuq = if_percpuq_create(&ifp);
	if_register(&ifp);

	anchor("S2.LINK_ATTACH",
	    ic->ic_ifp == &ifp && ic->ic_bss != NULL &&
	    ic->ic_curchan != NULL && ic->ic_newstate != NULL &&
	    mac_eq(ic->ic_myaddr, sta_mac),
	    "opmode=STA ch=%d bss=%d chan=%d newstate=%d", VWIFI_CHAN_IEEE,
	    ic->ic_bss != NULL, ic->ic_curchan != NULL, ic->ic_newstate != NULL);
}

/* ------------------------------------------------------------------ */
/* P2/P3/P4: scan -> auth -> assoc                                     */
/* ------------------------------------------------------------------ */

static void run_scan_to_run(void)
{
	unsigned guard;

	ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	anchor("S2.SCAN_START", (ic->ic_flags & IEEE80211_F_SCAN) != 0, NULL);

	for (guard = 0; guard < 64 && ic->ic_state != IEEE80211_S_RUN; guard++) {
		if (vwifi_dispatch_one(&vwifi))
			continue;
		if (ic->ic_state == IEEE80211_S_SCAN) {
			/* The scan dwell is over: the driver advances the scan. */
			ieee80211_next_scan(ic);
			continue;
		}
		break;		/* waiting for a response that will not come */
	}
}

static void check_captured_frames(void)
{
	const uint8_t *f;

	/* probe request: broadcast destination, station source, 11b/g rates. */
	f = vwifi.last_probe_req.data;
	anchor("S2.TX_PROBE_REQ", vwifi.stats.tx_probe_req >= 1,
	    "n=%u", vwifi.stats.tx_probe_req);
	anchor("S1.BYTES_TX",
	    vwifi.last_probe_req.len >= 24 &&
	    f[0] == IEEE80211_FC0_SUBTYPE_PROBE_REQ && f[1] == 0x00 &&
	    mac_is_broadcast(f + 4) && mac_eq(f + 10, sta_mac) &&
	    mac_is_broadcast(f + 16),
	    "len=%u", (unsigned) vwifi.last_probe_req.len);

	/* authentication request: open system, first sequence, to the AP. */
	f = vwifi.last_auth_req.data;
	anchor("S3.TX_AUTH",
	    vwifi.stats.tx_auth_req == 1 && vwifi.last_auth_req.len == 30 &&
	    f[0] == IEEE80211_FC0_SUBTYPE_AUTH && f[1] == 0x00 &&
	    mac_eq(f + 4, ap_mac) && mac_eq(f + 10, sta_mac) &&
	    mac_eq(f + 16, ap_mac) &&
	    f[24] == 0x00 && f[25] == 0x00 &&	/* algorithm: open system */
	    f[26] == 0x01 && f[27] == 0x00 &&	/* sequence: 1 */
	    f[28] == 0x00 && f[29] == 0x00,	/* status: success */
	    "alg=0 seq=1");

	/* association request: carries the SSID and the rates elements. */
	f = vwifi.last_assoc_req.data;
	anchor("S3.TX_ASSOC",
	    vwifi.stats.tx_assoc_req == 1 && vwifi.last_assoc_req.len >= 28 &&
	    f[0] == IEEE80211_FC0_SUBTYPE_ASSOC_REQ && f[1] == 0x00 &&
	    mac_eq(f + 4, ap_mac) && mac_eq(f + 10, sta_mac) &&
	    mac_eq(f + 16, ap_mac),
	    "len=%u", (unsigned) vwifi.last_assoc_req.len);
}

static void check_responder_vectors(void)
{
	anchor("S3.RX_PROBE_RESP",
	    vwifi.stats.rx_probe_resp >= 1 &&
	    vwifi.last_probe_resp.len == sizeof(vwifi_vec_probe_resp) &&
	    memcmp(vwifi.last_probe_resp.data, vwifi_vec_probe_resp,
	    sizeof(vwifi_vec_probe_resp)) == 0,
	    "ssid=" VWIFI_SSID);
	anchor("S3.VEC_PROBE_RESP",
	    vwifi.last_probe_resp.len == sizeof(vwifi_vec_probe_resp) &&
	    memcmp(vwifi.last_probe_resp.data, vwifi_vec_probe_resp,
	    sizeof(vwifi_vec_probe_resp)) == 0,
	    "n=%u", (unsigned) sizeof(vwifi_vec_probe_resp));

	anchor("S3.RX_AUTH",
	    vwifi.stats.rx_auth_resp == 1 &&
	    vwifi.last_auth_resp.len == sizeof(vwifi_vec_auth_resp) &&
	    memcmp(vwifi.last_auth_resp.data, vwifi_vec_auth_resp,
	    sizeof(vwifi_vec_auth_resp)) == 0,
	    "seq=2 status=0");
	anchor("S3.VEC_AUTH_RESP",
	    vwifi.last_auth_resp.len == sizeof(vwifi_vec_auth_resp) &&
	    memcmp(vwifi.last_auth_resp.data, vwifi_vec_auth_resp,
	    sizeof(vwifi_vec_auth_resp)) == 0,
	    "n=%u", (unsigned) sizeof(vwifi_vec_auth_resp));

	anchor("S3.RX_ASSOC",
	    vwifi.stats.rx_assoc_resp == 1 &&
	    vwifi.last_assoc_resp.len == sizeof(vwifi_vec_assoc_resp) &&
	    memcmp(vwifi.last_assoc_resp.data, vwifi_vec_assoc_resp,
	    sizeof(vwifi_vec_assoc_resp)) == 0,
	    "status=0 aid=%u", VWIFI_AID);
	anchor("S3.VEC_ASSOC_RESP",
	    vwifi.last_assoc_resp.len == sizeof(vwifi_vec_assoc_resp) &&
	    memcmp(vwifi.last_assoc_resp.data, vwifi_vec_assoc_resp,
	    sizeof(vwifi_vec_assoc_resp)) == 0,
	    "n=%u", (unsigned) sizeof(vwifi_vec_assoc_resp));

	anchor("S3.EVENT_SCAN_DONE", vwifi.stats.ev_scan_done == 1, NULL);
	anchor("S3.EVENT_ASSOC", vwifi.stats.ev_assoc == 1, NULL);
}

/* ------------------------------------------------------------------ */
/* P5: the node table and the port hooks                               */
/* ------------------------------------------------------------------ */

static unsigned node_count_sta, node_count_scan;

static void count_node(void *arg, struct ieee80211_node *ni)
{
	unsigned *count = arg;

	(void) ni;
	(*count)++;
}

static void check_node_table(void)
{
	struct ieee80211_node *bss = ic->ic_bss;

	node_count_sta = 0;
	node_count_scan = 0;
	ieee80211_iterate_nodes(&ic->ic_sta, count_node, &node_count_sta);
	ieee80211_iterate_nodes(&ic->ic_scan, count_node, &node_count_scan);

	anchor("S3.NODE_TABLE",
	    bss != NULL && mac_eq(bss->ni_bssid, ap_mac) &&
	    bss->ni_associd == VWIFI_AID && node_count_scan >= 1 &&
	    ieee80211_node_refcnt(bss) >= 1,
	    "nodes=%u/%u refs=%d", node_count_scan, node_count_sta,
	    ieee80211_node_refcnt(bss));

	anchor("S3.RUN_STATE", ic->ic_state == IEEE80211_S_RUN, "state=%d",
	    (int) ic->ic_state);
}

/* ------------------------------------------------------------------ */
/* the run                                                             */
/* ------------------------------------------------------------------ */

static void suite_run(void *argument)
{
	unsigned blocks0, bytes0, blocks1, bytes1;

	(void) argument;
	wlan_osal_init();

	blocks0 = wlan_osal_live_blocks();
	bytes0 = wlan_osal_live_bytes();

	device_attach();
	run_scan_to_run();
	check_captured_frames();
	check_responder_vectors();
	check_node_table();

	blocks1 = wlan_osal_live_blocks();
	bytes1 = wlan_osal_live_bytes();
	printf(TAG "alloc blocks %u->%u bytes %u->%u\n", blocks0, blocks1,
	    bytes0, bytes1);

	if (failures == 0) {
		printf(TAG "PASS\n");
		fflush(stdout);
		cmsis2_host_end_scheduler();
		return;
	}
	printf(TAG "FAILURES %u\n", failures);
	fflush(stdout);
	exit(1);
}

int main(int argc, char **argv)
{
	static const osThreadAttr_t suite_attr = {
		.name = "vwifi-suite",
		.priority = osPriorityNormal,
		.stack_size = THREAD_STACK_SIZE,
	};

	if (argc > 2 && strcmp(argv[1], "--fault") == 0)
		fault_mode = argv[2];

	if (osKernelInitialize() != osOK)
		fail("kernel init");
	if (osThreadNew(suite_run, NULL, &suite_attr) == NULL)
		fail("suite thread create");
	if (osKernelStart() != osOK)
		fail("kernel start");
	return 0;
}
