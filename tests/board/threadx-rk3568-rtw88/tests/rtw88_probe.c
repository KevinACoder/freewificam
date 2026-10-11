/**
 * @file    rtw88_probe.c
 * @brief   Case rtw88-probe: the dongle is claimed, probed, firmware-loaded and
 *          registered - the board half of the acceptance chain.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h); the
 *          USB bring-up shape follows the mass-storage cases
 *          (tests/board/common/cases/usb_msc.c:74-92), and the wait-for-the-
 *          claim shape follows their instance wait (:54-65). The two-bus attempt
 *          mirrors the EHCI case's "whichever root answers" reading (:14-24).
 *
 * The chain this case witnesses is the real one, no shortcuts: the USB host
 * stack enumerates the dongle (in its CD-ROM persona the modeswitch hook flips
 * it, and the device re-enumerates as its network personality), the linuxkpi
 * vendor-specific class hook builds the Linux view of it, the driver's own
 * id_table matches (0bda:c820 with the ff/ff/ff interface triple), the driver's
 * probe runs against the real chip (register reads, efuse, firmware download),
 * and at the end the driver registers its hw - which is the moment the bridge
 * attaches and the net80211 side exists.
 *
 * Anchors (the judge checks each on PASS):
 *   wlan: usb bus=N hcd=... started         - the controller came up
 *   usbh: modeswitch 0bda:1a2b ... ejecting - the CD-ROM persona was flipped
 *   usbh: lkpi claim attempt 0bda:c820      - the class hook saw the network
 *                                             personality
 *   wlan: probe done attached=1 ...         - the driver's probe completed
 *   lkpi: lkpiu: Firmware version 24.11.0, H2C version 12
 *                                           - the firmware the driver loaded
 *                                             (the dev_info line routed to
 *                                             this log)
 *
 * A missing dongle is not hidden: the case names what it waited for and fails,
 * and the RUN ledger registers it as blocked-by-hardware (the standing rule
 * for this milestone).
 */

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"
#include "log.h"

#include "usb_glue.h"

/* net80211-world header of the bridge: scalars only, safe from this world */
#include "wlan_bridge.h"
/* the linuxkpi pool's ledger (scalars only, safe from this world) */
#include "skbuff/skbuff_pool.h"
/* the USB transfer ledger's one-line print (engine + adapter counters) */
#include "wlan_ledger.h"

#define WLAN_PROBE_WAIT_MS	60000U
#define WLAN_PROBE_POLL_MS	100U

static int wlan_attached(void)
{
	struct wlan_bridge_counters c;

	wlan_bridge_stats(&c);
	return c.attached != 0;
}

/* Bring one controller up and wait for the driver to register its hw. Returns
 * 1 when attached, 0 when this controller produced no claim in time. */
static int wlan_try_bus(unsigned busid, const char *hcd)
{
	unsigned spins = WLAN_PROBE_WAIT_MS / WLAN_PROBE_POLL_MS;

	if (usb_board_init(busid) != 0) {
		board_log("wlan: bus%u board layer bring-up failed\n", busid);
		return 0;
	}
	board_log("wlan: usb bus=%u hcd=%s base=0x%lx started\n", busid, hcd,
		  (unsigned long)usb_board_reg_base(busid));

	if (usbh_initialize(busid, usb_board_reg_base(busid),
	    usb_board_event_handler) != 0) {
		board_log("wlan: bus%u usbh_initialize failed\n", busid);
		return 0;
	}
	usb_board_after_start(busid);

	for (unsigned spin = 0; spin < spins; spin++) {
		if (wlan_attached()) {
			return 1;
		}
		osDelay(WLAN_PROBE_POLL_MS);
	}
	return 0;
}

void rtw88_probe_chain(void)
{
	struct wlan_bridge_counters c;

	/*
	 * The card's port is the user's to choose, and the case takes whichever
	 * root answers.  This image's dongle sits on the xHCI0 root (busid 2, the
	 * lower USB3 socket), so xHCI is tried first: the other order costs a full
	 * 60 s timeout on the wrong controller before the right one is reached,
	 * and a bus that is merely slow to enumerate would look like a failed
	 * probe.  EHCI stays as the fallback so the card can be moved without
	 * rebuilding the image.
	 */
	if (!wlan_try_bus(2U, "xhci") && !wlan_try_bus(1U, "ehci")) {
		board_case_fail("the driver never registered its hw within 60 s "
		    "on either controller (no dongle claimed, or probe failed - "
		    "see the lines above)");
		return;
	}

	/*
	 * The driver's operational start. net80211's side exists from attach, but
	 * the chip only begins receiving after the driver's own start runs (it
	 * submits the RX urbs there) - the host suite takes the same step after
	 * its attach, and without it a sweep transmits but never hears a beacon.
	 */
	if (wlan_bridge_up() != 0) {
		board_case_fail("the driver's start chain refused");
		return;
	}

	wlan_bridge_stats(&c);
	board_log("wlan: probe done attached=%d rx=%u mgt_tx=%u scan=%u/%u\n",
		  c.attached, c.rx_frames, c.mgt_tx, c.scan_starts,
		  c.scan_completes);
	/*
	 * The allocation ledger, on the case's own line: this driver's receive
	 * path holds four 32 KB buffers and its firmware download allocates a
	 * packet per transfer, so whether the image can carry it is a memory
	 * question - and one the case should answer with numbers rather than
	 * with a later failure's symptom.  dyalloc is how many of those buffers
	 * came from the heap, alloc_fail how many requests the pool could not
	 * serve at all.
	 */
	{
		struct lkpi_skb_pool_stats st;

		lkpi_skb_pool_stats(&st);
		board_log("wlan: pool skb=%u/%u data=%u/%u heap_live=%u ovf=%u/%u "
		    "fail=%u alloc=%u free=%u\n",
		    st.skb_used, st.skb_high, st.data_used, st.data_high,
		    st.dyalloc_live, st.slot_overflow, st.data_overflow,
		    st.alloc_fail, st.n_alloc, st.n_free);
	}
	/*
	 * The USB transfer ledger: whether the receive path is submitting, whether
	 * the controller is accepting what it submits, and whether anything is
	 * coming back.  A path that submits but never completes shows enq climbing
	 * with comp flat; one the backend refuses shows parks climbing; one that
	 * never submits shows enq flat.  The adapter's stage counters say why: a
	 * request larger than one stage slot is refused for good (ovf climbing),
	 * which separates a driver bug from an image sized for another driver's
	 * receive buffer.
	 */
	wlan_ledger_usb("probe");
}

BOARD_TEST("rtw88-probe", rtw88_probe_chain);
