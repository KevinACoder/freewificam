/*
 * @file   wlan_ledger.c
 * @brief  The USB transfer ledger's one-line body (see wlan_ledger.h).
 *
 * @design bring-up diagnostic - the same shape the skb pool's ledger uses
 *         (skbuff_pool.h's stats record): a fixed, plain-scalar snapshot of the
 *         mechanism's own counters, printed once per interesting moment.  It
 *         reads two scalar records and prints them; it owns no state.
 */

#include "wlan_ledger.h"

#include "log.h"

#include "linuxkpi_usb.h"
#include "ports/cherryusb/lkpi_usb_cherryusb.h"

void
wlan_ledger_usb(const char *tag)
{
	struct lkpi_usb_engine_stats es;
	struct lkpi_usb_chusb_stats cs;

	lkpi_usb_engine_stats_get(&es);
	lkpi_usb_cherryusb_stats(&cs);
	/*
	 * engine: enq/kick/comp/park tell the receive path's story - submits with
	 * no completions is a path that left and never returned, parks climbing is
	 * one the controller refused, enq flat is one that never submitted.
	 * adapter: sub/comp/stg/ovf/aex say what the controller did with them; ovf
	 * climbing means a request larger than one stage slot was refused for
	 * good, which is an image sized for the wrong driver.
	 */
	board_log("wlan: %s usb ep=%u enq=%u kick=%u comp=%u park=%u/%u qlen=%u "
	    "inf=%u pkd=%u | chu sub=%u comp=%u stg=%u ovf=%u sex=%u aex=%u "
	    "sz=%u\n",
	    tag, es.eps, es.enqueued, es.kicks, es.completions, es.parks,
	    es.resumes, es.qlen, es.in_flight, es.parked_now,
	    cs.submits, cs.completed, cs.stage_staged, cs.stage_overflow,
	    cs.stage_exhausted, cs.slots_exhausted, cs.stage_size);
}
