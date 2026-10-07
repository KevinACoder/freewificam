/*
 * @file    main.c
 * @brief   Host contract smoke: the net80211 port layer on the CMSIS-RTOS2 API.
 *
 * One run exercises, with independent counter lines as evidence:
 *   - the osal: the recursive serializer (including a suspend/resume round trip)
 *     and the allocation seam;
 *   - the mbuf shell: MGET/MGETHDR allocation, prepend, tail trim, pullup and
 *     the free of a chain;
 *   - the ifnet shell: interface init, address attach, and the single receive
 *     seam routing a data frame and an EAPOL frame to their registered sinks,
 *     plus one event through rt_ieee80211msg;
 *   - the seam: the stub driver serving six vtable callbacks.
 *
 * The run is a contract, not a network test: it asserts shapes and counts, and
 * says nothing about 802.11 semantics (the frame bytes here are arbitrary
 * payloads; the mock suites own protocol truth).
 *
 * Anchor lines on stdout, machine-checked by the Makefile and the run log:
 *   "net80211-port-smoke: osal=ok ... " and the final "PASS".
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include <sys/mbuf.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/route.h>

#include <port/port.h>

#include "osal/wlan_osal.h"
#include "seam_stub.h"

#define TAG "net80211-port-smoke: "
#define THREAD_STACK_SIZE 4096U

static unsigned rx_data_frames;
static unsigned rx_eapol_frames;
static unsigned port_events;

static void fail(const char *what)
{
	fflush(stdout);
	fprintf(stderr, TAG "FAIL %s\n", what);
	fflush(stderr);
	abort();
}

static void on_data(const uint8_t *frame, size_t len, void *arg)
{
	(void) arg;
	if (frame == NULL || len == 0) {
		fail("data sink got an empty frame");
	}
	rx_data_frames++;
}

static void on_eapol(const uint8_t src[6], const uint8_t *buf, size_t len,
    void *arg)
{
	(void) src;
	(void) arg;
	if (buf == NULL || len == 0) {
		fail("eapol sink got an empty payload");
	}
	rx_eapol_frames++;
}

static void on_event(enum wlan_port_event event, const uint8_t *addr, void *arg)
{
	(void) event;
	(void) addr;
	(void) arg;
	port_events++;
}

static struct mbuf *frame_mbuf(const uint8_t *dst, const uint8_t *src,
    uint16_t ethertype, const uint8_t *payload, size_t payload_len)
{
	struct mbuf *m = NULL;
	uint8_t *p;

	MGET(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		return NULL;
	}
	p = mtod(m, uint8_t *);
	memcpy(p, dst, ETHER_ADDR_LEN);
	p += ETHER_ADDR_LEN;
	memcpy(p, src, ETHER_ADDR_LEN);
	p += ETHER_ADDR_LEN;
	p[0] = (uint8_t) (ethertype >> 8);
	p[1] = (uint8_t) (ethertype & 0xff);
	p += 2;
	memcpy(p, payload, payload_len);
	m->m_len = (int) (2 * ETHER_ADDR_LEN + 2 + payload_len);
	return m;
}

static void mbuf_round_trip(void)
{
	struct mbuf *m = NULL;
	struct mbuf *h = NULL;

	/* A header mbuf carries its data behind the reserved area, so the prepend
	 * grows it in place; a plain mbuf has no leading space and is only used
	 * for the pullup leg (there a prepend would have to chain a new head). */
	MGETHDR(h, M_NOWAIT, MT_HEADER);
	if (h == NULL) {
		fail("header mbuf alloc");
	}
	h->m_len = 40;
	if (m_pullup(h, 40) != h) {
		fail("m_pullup on a contiguous buffer must return the same mbuf");
	}
	M_PREPEND(h, 8, M_NOWAIT);
	if (h == NULL || h->m_len != 48) {
		fail("m_prepend did not grow the header mbuf in place");
	}
	m_adj(h, 8);
	if (h->m_len != 40) {
		fail("m_adj did not trim the head");
	}
	m_adj(h, -16);
	if (h->m_len != 24) {
		fail("m_adj did not trim the tail");
	}
	wlan_m_freem(h);

	MGET(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		fail("mbuf alloc");
	}
	m->m_len = 40;
	if (m_pullup(m, 40) != m) {
		fail("m_pullup on a plain mbuf must return the same mbuf");
	}
	wlan_m_freem(m);
}

static void ifnet_round_trip(void)
{
	static struct ifnet ifp;
	static const uint8_t mac[ETHER_ADDR_LEN] = { 0x02, 0, 0, 0, 0, 0x11 };
	static const uint8_t bcast[ETHER_ADDR_LEN] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};
	static const uint8_t payload[16] = { 0xaa };

	memset(&ifp, 0, sizeof(ifp));
	if (if_initialize(&ifp) != 0) {
		fail("if_initialize");
	}
	(void) if_percpuq_create(&ifp);
	ether_ifattach(&ifp, mac);

	wlan_port_set_data_rx(on_data, NULL);
	wlan_port_set_eapol_rx(on_eapol, NULL);
	wlan_port_set_event_handler(on_event, NULL);

	if_percpuq_enqueue(ifp.if_percpuq,
	    frame_mbuf(bcast, mac, 0x0800, payload, sizeof(payload)));
	if_percpuq_enqueue(ifp.if_percpuq,
	    frame_mbuf(bcast, mac, 0x888e, payload, sizeof(payload)));
	if (rx_data_frames != 1 || rx_eapol_frames != 1) {
		fail("the receive seam did not route one data and one eapol frame");
	}

	rt_ieee80211msg(&ifp, RTM_IEEE80211_SCAN, mac, ETHER_ADDR_LEN);
	if (port_events != 1) {
		fail("the event seam did not deliver the scan event");
	}
}

static void port_smoke(void *argument)
{
	unsigned long blocks0, bytes0, blocks1, bytes1;

	(void) argument;
	wlan_osal_init();

	/* The serializer is recursive and survives a suspend/resume round trip. */
	wlan_port_serializer_lock();
	wlan_port_serializer_lock();
	wlan_port_serializer_suspend();
	wlan_port_serializer_resume();
	wlan_port_serializer_unlock();
	wlan_port_serializer_unlock();

	blocks0 = wlan_osal_live_blocks();
	bytes0 = wlan_osal_live_bytes();

	mbuf_round_trip();
	ifnet_round_trip();
	wlan_seam_stub_run();

	blocks1 = wlan_osal_live_blocks();
	bytes1 = wlan_osal_live_bytes();
	if (blocks0 != blocks1 || bytes0 != bytes1) {
		fail("allocation seam not paired");
	}
	if (wlan_seam_stub_calls() < 6) {
		fail("the seam vtable callbacks did not all run");
	}

	printf(TAG "osal=ok serializer=ok mbuf=ok ifnet=ok seam=ok\n");
	printf(TAG "counters rx_data=%u rx_eapol=%u events=%u seam_calls=%u\n",
	    rx_data_frames, rx_eapol_frames, port_events,
	    wlan_seam_stub_calls());
	printf(TAG "alloc blocks %lu->%lu bytes %lu->%lu\n", blocks0, blocks1,
	    bytes0, bytes1);
	printf(TAG "PASS\n");
	fflush(stdout);

	cmsis2_host_end_scheduler();
}

int main(void)
{
	static const osThreadAttr_t smoke_attr = {
		.name = "port-smoke",
		.priority = osPriorityNormal,
		.stack_size = THREAD_STACK_SIZE,
	};

	if (osKernelInitialize() != osOK) {
		fail("kernel init");
	}
	if (osThreadNew(port_smoke, NULL, &smoke_attr) == NULL) {
		fail("smoke thread create");
	}
	if (osKernelStart() != osOK) {
		fail("kernel start");
	}
	return 0;
}
