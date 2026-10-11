/*
 * @file   wlan_ledger.c
 * @brief  The USB transfer ledger's one-line body (see wlan_ledger.h).
 *
 * @design bring-up diagnostic - the same shape the skb pool's ledger uses
 *         (skbuff_pool.h's stats record): a fixed, plain-scalar snapshot of the
 *         mechanism's own counters, printed once per interesting moment.  It
 *         reads scalar records and prints them; it owns no state.
 */

#include "wlan_ledger.h"

#include "log.h"

#include "linuxkpi_80211.h"
#include "linuxkpi_usb.h"
#include "lkpi_rxtrace.h"
#include "ports/cherryusb/lkpi_usb_cherryusb.h"

void
wlan_ledger_usb(const char *tag)
{
	struct lkpi_usb_engine_stats es;
	struct lkpi_usb_chusb_stats cs;
	u32 rx_frames, rx_fc;
	int rx_fc_seen;
	unsigned int i;

	lkpi_usb_engine_stats_get(&es);
	lkpi_usb_cherryusb_stats(&cs);
	lkpi80211_rx_counters(&rx_frames, &rx_fc, &rx_fc_seen);
	/*
	 * engine: enq/kick/comp/park tell the receive path's story - submits with
	 * no completions is a path that left and never returned, parks climbing is
	 * one the controller refused, enq flat is one that never submitted.
	 * adapter: sub/comp/stg/ovf/aex say what the controller did with them; ovf
	 * climbing means a request larger than one stage slot was refused for
	 * good, which is an image sized for the wrong driver.  rx is what reached
	 * mac80211's receive entry, and the first frame's control byte says whether
	 * what arrived is even a frame.
	 */
	LOG_DEBUG("wlan: %s usb ep=%u enq=%u kick=%u comp=%u park=%u/%u qlen=%u "
	    "inf=%u pkd=%u err=%u | chu sub=%u comp=%u stg=%u ovf=%u sex=%u aex=%u "
	    "sz=%u | mac80211 rx=%u fc=%s%02x\n",
	    tag, es.eps, es.enqueued, es.kicks, es.completions, es.parks,
	    es.resumes, es.qlen, es.in_flight, es.parked_now, es.errors,
	    cs.submits, cs.completed, cs.stage_staged, cs.stage_overflow,
	    cs.stage_exhausted, cs.slots_exhausted, cs.stage_size,
	    rx_frames, rx_fc_seen ? "0x" : "(none)", rx_fc_seen ? rx_fc : 0u);
	/*
	 * The receive boundary trace's counters: completions against deliveries
	 * is the transport-to-stack funnel's shape, anomalies names how many
	 * frames the trace itself flagged, dumps how many snapshots were taken
	 * (bounded to three; a fault before any dump means the anomaly classes
	 * never saw it).
	 */
	{
		u32 rt_c, rt_d, rt_a, rt_s, rt_dm;

		lkpi_rxtrace_stats(&rt_c, &rt_d, &rt_a, &rt_s, &rt_dm);
		LOG_DEBUG("wlan: %s rxtrace comp=%u deliv=%u anom=%u short=%u "
		    "dumps=%u\n", tag, rt_c, rt_d, rt_a, rt_s, rt_dm);
		if (rt_a != 0) {
			u32 a_len, a_ptr;
			u8 a_head[16];
			int a_st;

			if (lkpi_rxtrace_first_anom(&a_len, &a_ptr, &a_st,
			    a_head) != 0)
				LOG_DEBUG("wlan: %s anom1 len=%u ptr=%08x fcs=%d "
				    "head=%02x%02x%02x%02x %02x%02x%02x%02x "
				    "%02x%02x%02x%02x %02x%02x%02x%02x\n",
				    tag, a_len, a_ptr, a_st,
				    a_head[0], a_head[1], a_head[2], a_head[3],
				    a_head[4], a_head[5], a_head[6], a_head[7],
				    a_head[8], a_head[9], a_head[10], a_head[11],
				    a_head[12], a_head[13], a_head[14],
				    a_head[15]);
		}
	}
	/*
	 * Per endpoint, and only the ones that carried traffic: the totals hide
	 * which endpoint is stuck - a receive endpoint that never completes looks
	 * like a busy control endpoint once they are summed.  The address names it
	 * (the receive pipe is the 0x8x one).
	 */
	for (i = 0; i < lkpi_usb_ep_count(); i++) {
		struct lkpi_usb_ep_stats s;

		if (lkpi_usb_ep_stats_get(i, &s) != 0)
			continue;
		if (s.enqueued == 0)
			continue;
		LOG_DEBUG("wlan: %s ep[%u] addr=0x%02x qlen=%u inf=%u pkd=%u "
		    "enq=%u kick=%u comp=%u err=%u park=%u/%u last_in=%u:"
		    "%08x %08x %08x %08x %08x %08x\n",
		    tag, s.index, s.ep_addr, s.qlen, s.in_flight, s.parked,
		    s.enqueued, s.kicks, s.completions, s.errors, s.parks,
		    s.resumes, s.last_in_len, s.last_in_words[0], s.last_in_words[1],
		    s.last_in_words[2], s.last_in_words[3], s.last_in_words[4],
		    s.last_in_words[5]);
	}
	/*
	 * The last few vendor control transfers: the register address the caller
	 * asked for and the bytes that came back.  A register read that returns a
	 * constant is indistinguishable from a working one unless its value is
	 * written down, and the driver above it only reports the symptom (a status
	 * bit that never appears).
	 */
	{
		struct lkpi_usb_ctrl_ev ev[12];
		unsigned int n = lkpi_usb_ctrl_trace(ev, 12);

		for (i = 0; i < n; i++)
			LOG_DEBUG("wlan: %s ctrl[%u] %s addr=0x%05x len=%u "
			    "data=0x%08x\n", tag, i, ev[i].is_in ? "in " : "out",
			    (ev[i].index << 16) | ev[i].value, ev[i].len,
			    ev[i].data);
		/* the device-facing bytes of the last control IN, for the same
		 * comparison from the other side */
		LOG_DEBUG("wlan: %s ctrl-stage last_in=%08x/%u\n", tag,
		    cs.last_in_stage_word, cs.last_in_stage_len);
	}
}
