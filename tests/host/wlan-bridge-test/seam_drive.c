/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    seam_drive.c
 * @brief   The port's own receive/event delivery, driven from the test.
 *
 * Two calls the port already owns are the way a frame and an event reach the
 * presentation layer in production: `if_percpuq_enqueue` classifies the frame
 * (EAPOL to the supplicant hook, everything else to the data hook) and
 * `rt_ieee80211msg` maps the stack's routing message onto the port's event
 * numbering. Driving them here means the layer under test is exercised through
 * the production seam rather than through a test-local copy of it - and it
 * pulls the port's classification into the same case.
 *
 * @design two-worlds boundary, harness side: this file is compiled in the
 *         net80211 world (BSD flags), so it must not include a linuxkpi-world
 *         header; its face (seam_drive.h) carries scalars and byte arrays only.
 * @date    08.10.2026
 */

#include <string.h>

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/route.h>

#include "seam_drive.h"

/* the port functions this driver reaches (the production delivery paths) */
void if_percpuq_enqueue(void *pq, struct mbuf *m);
void rt_ieee80211msg(struct ifnet *ifp, int what, const void *data,
    size_t len);

int
seam_drive_frame(const uint8_t *frame, size_t len)
{
	struct mbuf *m;

	if (frame == NULL || len == 0 || len > MCLBYTES)
		return -1;
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return -1;
	memcpy(mtod(m, void *), frame, len);
	m->m_len = (int)len;
	m->m_pkthdr.len = (int)len;
	/* the port's own classification and delivery run inside this call */
	if_percpuq_enqueue(NULL, m);
	return 0;
}

int
seam_drive_event(int event, const uint8_t addr[6])
{
	int what;

	switch (event) {
	case SEAM_DRIVE_EV_SCAN_DONE:
		what = RTM_IEEE80211_SCAN;
		break;
	case SEAM_DRIVE_EV_ASSOC:
		what = RTM_IEEE80211_ASSOC;
		break;
	case SEAM_DRIVE_EV_DISASSOC:
		what = RTM_IEEE80211_DISASSOC;
		break;
	default:
		return -1;
	}
	rt_ieee80211msg(NULL, what, addr, 6);
	return 0;
}
