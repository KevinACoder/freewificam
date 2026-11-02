/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net_loop.c
 * @brief  Case net_loop (rk3568-itx): the lwIP stack's own per-packet cost,
 *         measured with the driver removed from the path.
 *
 * Why this case exists: the GMAC round attributes 11.4 us/frame to the feed
 * segment (pbuf allocation + the stack's ethernet/ip/udp layers) after the
 * mailbox hand-off, the per-frame lock and the software checksum were taken
 * out. Before calling that "the stack" the driver, the DMA rings and the
 * staging copy have to leave the equation. lwIP 2.2 creates its loopback
 * interface inside netif_init (src/core/netif.c, netif_loopif_init), so a
 * packet sent to 127.0.0.1 walks pbuf allocation, ip4 output, netif_loop_output,
 * the tcpip thread, ethernet-less ip4 input, udp demux and the socket receive
 * mailbox - the same upper layers, with no GMAC, no descriptors and no DMA.
 *
 * The shape is a ping-pong on one thread: send one datagram to the loopback
 * address, then receive it, timing the whole round trip. That deliberately
 * serialises the two halves, so the number is an UPPER bound: it contains the
 * two thread switches the loopback path costs (app -> tcpip -> app), which a
 * pipelined receiver does not pay per frame. Its use is therefore one-sided
 * and stated in the run ledger: an upper bound below the GMAC feed figure
 * excludes "the stack alone explains it"; an upper bound above it proves
 * nothing about the driver.
 *
 * The frame content is checked (a 32-bit sequence at offset 0), so a run that
 * silently drops or reorders datagrams reports it instead of measuring a
 * shorter path.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <errno.h>

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"

#include "generic_timer.h"
#include "log.h"

#define LOOP_ANCHOR(...)	log_emit(ULOG_ALWAYS_LEVEL, __VA_ARGS__)

#define LOOP_PKTS		20000u	/* measured round trips */
#define LOOP_WARMUP		1000u
#define LOOP_BLK		1440u
#define LOOP_PORT		5002u
#define LOOP_RX_TIMEOUT_MS	2000
#define LOOP_BUDGET_MS		120000u

static uint8_t loop_tx[LOOP_BLK] __attribute__((aligned(8)));
static uint8_t loop_rx[LOOP_BLK] __attribute__((aligned(8)));

static uint32_t loop_freq_hz;

static uint64_t ticks_to_us(uint64_t ticks)
{
	return (ticks * 1000000ull) / (uint64_t)loop_freq_hz;
}

static int loop_bind_udp(uint32_t port)
{
	struct sockaddr_in addr;
	int fd;
	struct timeval tv;

	fd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = lwip_htons((uint16_t)port);
	{
		ip4_addr_t lo;

		IP4_ADDR(&lo, 127, 0, 0, 1);
		addr.sin_addr.s_addr = lo.addr;
	}
	if (lwip_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		lwip_close(fd);
		return -1;
	}

	tv.tv_sec = LOOP_RX_TIMEOUT_MS / 1000;
	tv.tv_usec = (LOOP_RX_TIMEOUT_MS % 1000) * 1000;
	(void)lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	return fd;
}

struct loop_stats {
	uint64_t total_rt;	/* round trip, ticks */
	uint64_t total_tx;	/* sendto leg, ticks */
	uint64_t total_rx;	/* recv leg, ticks */
	uint64_t best_rt;
	uint32_t done;
	uint32_t bad;
};

/*
 * One round trip at `size`: send the sequence word, receive it back. The two
 * legs are timed separately as well as together, so the ledger can say whether
 * the cost sits on the output side (udp/ip output, loop queue) or on the input
 * side (tcpip thread, udp demux, mailbox wakeup).
 */
static void loop_round_trip(int tx, int rx, struct sockaddr_in *dst,
			    uint32_t seq, uint16_t size, struct loop_stats *st)
{
	uint64_t t0, t1, t2;
	int n;

	memcpy(loop_tx, &seq, sizeof(seq));
	t0 = gtimer_count_get();
	if (lwip_sendto(tx, loop_tx, size, 0,
			(struct sockaddr *)dst, sizeof(*dst)) != (int)size) {
		st->bad++;
		return;
	}
	t1 = gtimer_count_get();
	n = lwip_recv(rx, loop_rx, sizeof(loop_rx), 0);
	t2 = gtimer_count_get();
	if (n != (int)size) {
		st->bad++;
		return;
	}
	if (memcmp(loop_rx, &seq, sizeof(seq)) != 0) {
		st->bad++;
		return;
	}

	st->done++;
	st->total_tx += t1 - t0;
	st->total_rx += t2 - t1;
	st->total_rt += t2 - t0;
	if (t2 - t0 < st->best_rt) {
		st->best_rt = t2 - t0;
	}
}

static uint64_t us_x10(uint64_t ticks, uint64_t count)
{
	if (count == 0ull) {
		return 0ull;
	}
	return (ticks * 10ull * 1000000ull) /
	       ((uint64_t)loop_freq_hz * count);
}

/*
 * One block-size sweep: LOOP_WARMUP warm-up round trips, then LOOP_PKTS
 * measured ones. Everything at one size, so the caller can difference two
 * sweeps into a per-byte slope and a per-packet intercept.
 */
static int loop_sweep(int tx, int rx, struct sockaddr_in *dst, uint16_t size)
{
	struct loop_stats st;
	uint64_t t_start;
	uint32_t i;

	memset(&st, 0, sizeof(st));
	st.best_rt = ~0ull;

	for (i = 0; i < LOOP_WARMUP; i++) {
		loop_round_trip(tx, rx, dst, i, size, &st);
	}

	st.done = 0;
	st.bad = 0;
	st.total_rt = 0;
	st.total_tx = 0;
	st.total_rx = 0;
	st.best_rt = ~0ull;

	t_start = gtimer_count_get();
	for (i = 0; i < LOOP_PKTS; i++) {
		uint64_t elapsed_ms;

		loop_round_trip(tx, rx, dst, i, size, &st);

		elapsed_ms = ticks_to_us(gtimer_count_get() - t_start) / 1000u;
		if (elapsed_ms > LOOP_BUDGET_MS) {
			break;
		}
	}

	LOOP_ANCHOR("net: loop cell blk=%u done=%lu bad=%lu wall_ms=%lu "
		    "rt_us_x10=%lu tx_us_x10=%lu rx_us_x10=%lu "
		    "rt_us_min_x10=%lu\n",
		    (unsigned)size,
		    (unsigned long)st.done, (unsigned long)st.bad,
		    (unsigned long)(ticks_to_us(gtimer_count_get() - t_start) /
				    1000ull),
		    (unsigned long)us_x10(st.total_rt, st.done),
		    (unsigned long)us_x10(st.total_tx, st.done),
		    (unsigned long)us_x10(st.total_rx, st.done),
		    (unsigned long)(st.best_rt == ~0ull ? 0ull :
				    (st.best_rt * 10ull * 1000000ull) /
				    (uint64_t)loop_freq_hz));

	return st.done ? 0 : 1;
}

int case_net_loop_run(void)
{
	struct sockaddr_in dst;
	int tx, rx, rc;

	loop_freq_hz = gtimer_freq_get();

	/* The loopback interface is created by netif_init inside tcpip_init;
	 * binding to 127.0.0.1 before that fails with ERR_VAL. */
	if (case_net_stack_up() != 0) {
		LOOP_ANCHOR("net: loop FAIL stack_up\n");
		return 1;
	}

	tx = loop_bind_udp(0u);
	rx = loop_bind_udp(LOOP_PORT);
	if (tx < 0 || rx < 0) {
		LOOP_ANCHOR("net: loop FAIL bind tx=%d rx=%d errno=%d\n",
			    tx, rx, errno);
		return 1;
	}

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_port = lwip_htons(LOOP_PORT);
	{
		ip4_addr_t lo;

		IP4_ADDR(&lo, 127, 0, 0, 1);
		dst.sin_addr.s_addr = lo.addr;
	}

	LOOP_ANCHOR("net: loop ready pkts=%lu warmup=%lu hz=%lu\n",
		    (unsigned long)LOOP_PKTS, (unsigned long)LOOP_WARMUP,
		    (unsigned long)loop_freq_hz);

	rc = loop_sweep(tx, rx, &dst, 64u);
	if (rc == 0) {
		rc = loop_sweep(tx, rx, &dst, LOOP_BLK);
	}

	LOOP_ANCHOR("net: loop done rc=%d\n", rc);

	lwip_close(tx);
	lwip_close(rx);
	return rc;
}
