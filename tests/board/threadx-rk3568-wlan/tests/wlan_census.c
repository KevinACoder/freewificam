/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan_census.c
 * @brief   The image's census face: the census clock's board-side
 *          implementation and the snapshot the dataplane case and the console
 *          command call.
 * @author  zhugengyu
 * @date    19.10.2026
 * @design  the same split the RTL8189FTV image draws
 *          (tests/board/threadx-rk3568-rtl8189/tests/wlan8189_census.c:50-58):
 *          the timestamps come from the architectural counter, and the portable
 *          linuxkpi layers must not touch an aarch64 register, so
 *          lkpi_census_now_ns() is implemented here, with the image that
 *          defines CONFIG_LKPI_CENSUS.
 *
 *          The clock is generic_timer.c's own entry point, which brackets the
 *          counter read with ISB on both sides (arch/aarch64/generic_timer.c:27)
 *          - a bare `mrs cntvct_el0` can be reordered against the work it is
 *          supposed to bracket, and an instrument that mis-times its own
 *          subjects answers the wrong question.
 *
 *          The snapshot is gated on the same macro the instruments are: with it
 *          off the image still builds and runs, it just has no census to print
 *          (and the suite's census anchors then go red, which is the honest
 *          outcome - no instrument, no evidence).
 *
 *          The lwIP-side lines are differenced here rather than in the stack:
 *          those counters are cumulative for the boot, and a phase's number is
 *          the difference from the phase's own opening snapshot.
 */

#include <stdint.h>

#include "generic_timer.h"

#include "linuxkpi_core.h"
#include "lkpi_census.h"
#include "lkpi_tcp_trace.h"

#include "ports/lwip/sys_mbox_seam.h"

#include "wlan_census.h"

/* The lwIP port's counters, declared here rather than through wlannetif.h:
 * that header belongs to the lwIP world and this file must not pull it in. */
void	wlannetif_get_rx_diag(unsigned long *rx_frames, unsigned long *alloc_fail,
	    unsigned long *take_fail, unsigned long *input_fail);
void	wlan_lwip_diag(unsigned long *inpkt_err, unsigned long *tcpin,
	    unsigned long *tcpinerr, unsigned long *tcpretrans,
	    unsigned long *tcpdrop);

/* the phase's opening snapshot of the cumulative lwIP counters */
static struct {
	unsigned long	rx_frames;
	unsigned long	alloc_fail;
	unsigned long	take_fail;
	unsigned long	input_fail;
	unsigned long	inpkt_err;
	unsigned long	tcpin;
	unsigned long	tcpinerr;
	unsigned long	tcpretrans;
	unsigned long	tcpdrop;
	unsigned long	mbox_post_ok;
	unsigned long	mbox_post_full;
} lwip_base;

uint64_t
lkpi_census_now_ns(void)
{
	uint64_t freq = (uint64_t)gtimer_freq_get();

	if (freq == 0u)
		return 0u;
	return (gtimer_count_get() * 1000000000ULL) / freq;
}

static void
wlan_lwip_base_snapshot(void)
{
	struct sys_mbox_seam m;

	wlannetif_get_rx_diag(&lwip_base.rx_frames, &lwip_base.alloc_fail,
	    &lwip_base.take_fail, &lwip_base.input_fail);
	wlan_lwip_diag(&lwip_base.inpkt_err, &lwip_base.tcpin,
	    &lwip_base.tcpinerr, &lwip_base.tcpretrans, &lwip_base.tcpdrop);
	sys_mbox_seam_get(&m);
	lwip_base.mbox_post_ok = m.post_ok;
	lwip_base.mbox_post_full = m.post_full;
}

static void
wlan_lwip_print(void)
{
	struct sys_mbox_seam m;
	unsigned long rx, alloc_fail, take_fail, input_fail;
	unsigned long inpkt_err, tcpin, tcpinerr, tcpretrans, tcpdrop;

	wlannetif_get_rx_diag(&rx, &alloc_fail, &take_fail, &input_fail);
	wlan_lwip_diag(&inpkt_err, &tcpin, &tcpinerr, &tcpretrans, &tcpdrop);
	sys_mbox_seam_get(&m);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: C lwip rx frames=%lu alloc_fail=%lu take_fail=%lu "
	    "input_fail=%lu inpkt_err=%lu mbox_full=%lu\n",
	    rx - lwip_base.rx_frames, alloc_fail - lwip_base.alloc_fail,
	    take_fail - lwip_base.take_fail,
	    input_fail - lwip_base.input_fail,
	    inpkt_err - lwip_base.inpkt_err,
	    m.post_full - lwip_base.mbox_post_full);
	lkpi_printf(ULOG_INFO_LEVEL,
	    "perf census: C lwip tcp in=%lu inerr=%lu retrans=%lu drop=%lu "
	    "mbox_ok=%lu\n",
	    tcpin - lwip_base.tcpin, tcpinerr - lwip_base.tcpinerr,
	    tcpretrans - lwip_base.tcpretrans, tcpdrop - lwip_base.tcpdrop,
	    m.post_ok - lwip_base.mbox_post_ok);
}

void
wlan_census_reset(void)
{
#ifdef CONFIG_LKPI_CENSUS
	lkpi_usb_census_reset();
	lkpi_usb_cherryusb_census_reset();
	lkpi_80211_census_reset();
	wlan_seam_tx_census_reset();
	lkpi_tcp_trace_reset();
	wlan_lwip_base_snapshot();
#endif
}

void
wlan_census_print(void)
{
#ifdef CONFIG_LKPI_CENSUS
	lkpi_usb_census_print();
	lkpi_usb_cherryusb_census_print();
	lkpi_80211_census_print();
	wlan_seam_tx_census_print();
	lkpi_tcp_trace_print();
	wlan_lwip_print();
#endif
}
