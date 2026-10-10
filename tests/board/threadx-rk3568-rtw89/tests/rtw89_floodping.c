/**
 * @file    rtw89_floodping.c
 * @brief   The floodping case: a sustained board-to-peer ICMP stream, gated in
 *          a clean interval and then held open for the host's own load.
 * @author  zhugengyu
 * @date    11.10.2026
 * @design  same pipeline as the dataplane case (tests/board/threadx-rk3568-rtw89/
 *          tests/rtw89_dataplane.c): the netif the dataplane case bound is
 *          handed over through rtw89_dataplane_netif(), and the contrib ping
 *          (third-party/lwip/contrib/apps/ping) runs in its socket mode.  The
 *          image compiles that translation unit with PING_DELAY=50 and wires
 *          its PING_RESULT hook to this case's counters, so every cycle's
 *          outcome is counted here rather than left to the transcript.
 *
 * The case runs two phases over the same peer:
 *
 *   1. a clean stream, no host load, which is what it judges itself by.  Its
 *      bar is functional, not a quality target: the stream must run long
 *      enough to be a stream and must not be dead or one-way.  Link quality
 *      is deliberately not gated here (the round's exit criterion is that the
 *      forward path works, and the plan leaves the rate to "no pathology");
 *      the measured loss and rate are anchors, and the link's quality is
 *      tracked in its own issue.
 *   2. a loaded window: the host reads the open marker, drives its own load
 *      (a flood ping and the TCP measurement) inside the window, and the
 *      board keeps streaming.  Its totals are anchors, not a gate - the point
 *      of the phase is to exercise both directions at once and to record what
 *      the board's stream looks like while the host is loading the link.
 *
 * The peer is the measurement host on the LAN, a property of the bench rather
 * than of the product, so it lives here next to the case that names it.
 */

#include <stdio.h>

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "lwip/ip_addr.h"
#include "lwip/netif.h"

#include "ping.h"

#include "board_suite.h"

/* the dataplane case's bound interface (NULL until DHCP has filled it) */
struct netif *rtw89_dataplane_netif(void);

/* The peer: the measurement host's LAN address (the same host the runner
 * names as the TFTP server). */
#define RTW89_FLOOD_PEER	"192.168.0.18"

/* Phase lengths.  The clean phase is the gate; the loaded phase is the window
 * the host drives its own load inside.  At the image's 50 ms ping delay and a
 * quiet channel these are hundreds of cycles each. */
#define RTW89_FLOOD_CLEAN_MS	20000U
#define RTW89_FLOOD_LOADED_MS	45000U

/* The clean stream's bar.  The case's job this round is to prove the board
 * sustains a board-to-peer stream at all - the round's exit criterion is that
 * the forward path works, and the link's rate is deliberately not a hard
 * threshold (only "no pathology", the plan's own wording).  So the bar is a
 * floor long enough to be a stream, and a loss ceiling that catches a dead or
 * one-way path without gating on link quality that is known poor and tracked
 * separately (the rtw89 link-quality issue: measured 2% clean / 23-42% under
 * load, seconds of latency).  The exact totals are anchors either way. */
#define RTW89_FLOOD_MIN_CYCLES	100U
#define RTW89_FLOOD_MAX_LOSS_PCT	50U

static int g_flood_phase;		/* 0 = clean, 1 = loaded */
static unsigned int g_clean_cycles, g_clean_replies;
static unsigned int g_loaded_cycles, g_loaded_replies;

/* The contrib ping's per-cycle hook (wired through PING_RESULT in this image's
 * Makefile): one call per send cycle, ok = the cycle's echo came back. */
void rtw89_flood_ping_result(int ok)
{

	if (g_flood_phase == 0) {
		g_clean_cycles++;
		if (ok) {
			g_clean_replies++;
		}
	} else {
		g_loaded_cycles++;
		if (ok) {
			g_loaded_replies++;
		}
	}
}

static unsigned int
flood_loss_pct(unsigned int cycles, unsigned int replies)
{

	return cycles == 0U ? 0U : (100U * (cycles - replies)) / cycles;
}

/* One stream phase: run the module for the given interval and report the
 * totals on the module's own counters. */
static void
flood_stream(const ip_addr_t *peer, int phase, unsigned int ms)
{
	unsigned int waited;

	g_flood_phase = phase;
	if (phase == 0) {
		g_clean_cycles = 0;
		g_clean_replies = 0;
	} else {
		g_loaded_cycles = 0;
		g_loaded_replies = 0;
	}

	ping_init(peer);
	for (waited = 0; waited < ms; waited += 100U) {
		osDelay(100U);
	}
	ping_stop();
}

void rtw89_floodping_chain(void)
{
	struct netif *nif = rtw89_dataplane_netif();
	ip_addr_t peer;
	unsigned int loss;

	if (nif == NULL) {
		board_case_fail("the dataplane netif is not up (the floodping "
		    "case must run after rtw89-dataplane)");
		return;
	}
	if (!ipaddr_aton(RTW89_FLOOD_PEER, &peer)) {
		board_case_fail("the peer address did not parse");
		return;
	}

	/* phase 1: the clean stream the case judges itself by */
	board_log("wlan: flood board ping start peer=%s secs=%u\n",
	    RTW89_FLOOD_PEER, (unsigned)(RTW89_FLOOD_CLEAN_MS / 1000U));
	flood_stream(&peer, 0, RTW89_FLOOD_CLEAN_MS);
	loss = flood_loss_pct(g_clean_cycles, g_clean_replies);
	board_log("wlan: flood board ping cycles=%u replies=%u loss=%u%%\n",
	    g_clean_cycles, g_clean_replies, loss);

	if (g_clean_cycles < RTW89_FLOOD_MIN_CYCLES) {
		board_case_fail("the clean stream ran too few cycles (the ping "
		    "thread did not keep up)");
		return;
	}
	if (loss > RTW89_FLOOD_MAX_LOSS_PCT) {
		board_case_fail("the clean stream lost too many echo replies");
		return;
	}

	/* phase 2: the window the host loads the link inside.  The open marker
	 * is the host's start signal; the totals are recorded, not judged. */
	board_log("wlan: flood host window open peer=%s secs=%u\n",
	    RTW89_FLOOD_PEER, (unsigned)(RTW89_FLOOD_LOADED_MS / 1000U));
	flood_stream(&peer, 1, RTW89_FLOOD_LOADED_MS);
	board_log("wlan: flood board ping loaded cycles=%u replies=%u loss=%u%%\n",
	    g_loaded_cycles, g_loaded_replies,
	    flood_loss_pct(g_loaded_cycles, g_loaded_replies));
	board_log("wlan: flood host window close\n");
}

BOARD_TEST("rtw89-floodping", rtw89_floodping_chain);
