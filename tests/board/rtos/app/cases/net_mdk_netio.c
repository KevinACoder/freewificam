/* SPDX-License-Identifier: Apache-2.0 */
/*
 * @file   net_mdk_netio.c
 * @brief  Case net_mdk_netio (rk3568-itx): NETIO benchmark server over lwIP,
 *         ported from Arm's MDK-Middleware netio test
 *         (Test/Network/netio/netio.c, MIT, Copyright (c) 2004-2024 Arm Ltd).
 *
 * Why this one and not upstream: both speak the *same* wire protocol (TCP
 * 18767 control, UDP 18768 data, CMD_QUIT/C2S/S2C/RES), so the unmodified
 * upstream client on the Pi drives either. This one is the version that has a
 * UDP side that is known to run on an RTOS: Arm ships it as an RTX5 test with
 * its own socket layer, and the shape it uses - a control thread per protocol,
 * and a receive thread spawned only after the client asks for a C2S run - is
 * the part worth copying. Upstream netio starts UDP_Receiver unconditionally
 * from main and on this board that takes the kernel down before it prints.
 *
 * What was changed from Arm's file (and nothing else):
 *   - rl_net.h / RTE_Components.h -> lwIP sockets.h. The MDK names map
 *     one-to-one: SOCKADDR->struct sockaddr, SOCKADDR_IN->struct sockaddr_in,
 *     closesocket->lwip_close, BSD_ERROR->-1, BSD_EMSGSIZE->EMSGSIZE.
 *   - netInitialize() is the board's GMAC bring-up (same routine net.c uses).
 *   - The 4 KiB test buffer stays static (Arm's shape), so no heap is touched
 *     at all - the whole reason it is the safe reference for a second thread.
 *   - printf goes to log_emit: the board console only carries log anchors.
 *   - Arm compiles this with -mgeneral-regs-only here, so nothing needs the
 *     FPU; the timer that ends a 6 s window is the CMSIS-RTOS2 one-shot tick,
 *     as upstream.
 *
 * The measurement: the client sends C2S (it transmits, the board receives into
 * the buffer and counts), then S2C (the board transmits for 6 s, the client
 * receives). The board reports packet counts; the client reports rates. Both
 * ends are printed, and both are stored in the run directory.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"

#include "log.h"

#include "dwc_eqos.h"
#include "eqos_types.h"
#include "eqos_rk3568.h"
#include "rtl8211f.h"
#include "ethnetif.h"

#include "lwip/stats.h"
#include "lwip/memp.h"

#include "generic_timer.h"
#include "net_prof.h"

/* ---------------------------------------------------------------------------
 * The ported file (Arm's netio.c, with the MDK seams filled by lwIP).
 * ------------------------------------------------------------------------- */

#define BUF_SIZE        16384

#define NETIO_PORT      18767
#define NETIO_AUXPORT   18768

/* The bench's wired port: gmac1 (fe010000). gmac0 has no cable on this board. */
#define MN_GMAC		1u
#define MN_IP3		200u
#define MN_GW3		1u

#define CMD_QUIT        0
#define CMD_C2S         1
#define CMD_S2C         2
#define CMD_RES         3

#define BSD_ERROR       (-1)
#define BSD_EMSGSIZE    (EMSGSIZE)

#define MDK_ANCHOR(...) log_emit(ULOG_ALWAYS_LEVEL, __VA_ARGS__)

typedef struct {
	uint32_t cmd;
	uint32_t data;
} CONTROL;

/* The board's own IPv4 address in network order; the UDP data socket binds to
 * it so data datagrams cannot be matched by the control connection's stream. */
static uint32_t g_local_ip;

static struct {
	int32_t command;
	uint32_t pkt_size;
	osTimerId_t timer;
	struct sockaddr_in *client;
	uint8_t *buf;
	uint32_t buf_size;
	int32_t udp_sock;
	uint32_t pkt_count;
	uint32_t rx_bytes;
	uint32_t tx_bytes;
	bool timeover;
	int32_t err_line;
	int32_t err_bsd;
} io;

static void cb_timer(void *arg) { (void)arg; io.timeover = true; }

static uint32_t rand32(void)
{
	static uint32_t rnd_state;

	rnd_state *= 1103515245;
	rnd_state += 12345;
	return rnd_state;
}

/* ---------------------------------------------------------------------------
 * Per-cell layered accounting (K2-01 W1.1).
 *
 * One line per completed measurement cell, cumulative since boot, so the
 * sweep's report diffs consecutive lines and each layer of the receive path
 * gets its own column. The point is that a lost datagram has to show up in
 * exactly one of these, and the one it shows up in names the layer:
 *
 *   mmc_ovf      the MAC's FIFO/DMA ring was full  -> ring depth / drain rate
 *   rx_frames    completed descriptors               (driver saw it)
 *   fed          frames handed to the stack          (bridge saw it)
 *   pool_fail    pbuf refused                        (lwIP pool)
 *   input_fail   netif->input said no                (stack/mbox)
 *   udp_in/drop  reached UDP, or was discarded       (transport)
 *
 * Nothing here sits on the data path: it is called at cell boundaries only.
 * ------------------------------------------------------------------------- */
static void emit_layer_counters(const char *tag)
{
	struct dwc_eqos_stats ds;
	struct dwc_eqos_mmc_stats mmc;
	struct dwc_eqos_diag dg;
	unsigned long polls = 0, pool_fail = 0, input_fail = 0, fed = 0;
	unsigned long pbuf_err = 0, mem_err = 0, udp_in = 0, udp_inerr = 0;
	unsigned long ip_drop = 0, tcp_retrans = 0;

	memset(&ds, 0, sizeof(ds));
	memset(&mmc, 0, sizeof(mmc));
	memset(&dg, 0, sizeof(dg));
	dwc_eqos_get_stats(MN_GMAC, &ds);
	dwc_eqos_get_mmc_stats(MN_GMAC, &mmc);
	dwc_eqos_get_diag(MN_GMAC, &dg);
	ethnetif_get_diag(&polls, &pool_fail, &input_fail, &fed);

#if LWIP_STATS
	mem_err = (unsigned long)lwip_stats.mem.err;
#endif
#if LWIP_STATS && MEMP_STATS
	if (lwip_stats.memp[MEMP_PBUF] != NULL)
		pbuf_err = (unsigned long)lwip_stats.memp[MEMP_PBUF]->err;
#endif
#if MIB2_STATS
	udp_in = (unsigned long)lwip_stats.mib2.udpindatagrams;
	udp_inerr = (unsigned long)lwip_stats.mib2.udpinerrors;
	ip_drop = (unsigned long)lwip_stats.mib2.ipindiscards;
	tcp_retrans = (unsigned long)lwip_stats.mib2.tcpretranssegs;
#endif

	MDK_ANCHOR("netio: layers tag=%s mmc_ovf=%lu mmc_crc=%lu rx_ru=%lu rx_frames=%lu rx_drop=%lu "
		   "rx_irq=%lu csum_drop=%lu rx_err=%lu dma_err=%lu tx_frames=%lu "
		   "tx_drop=%lu ring_ready=%lu ring_free=%lu bridge_poll=%lu fed=%lu "
		   "pool_fail=%lu input_fail=%lu mem_err=%lu pbuf_err=%lu "
		   "udp_in=%lu udp_inerr=%lu ip_drop=%lu tcp_retrans=%lu\n",
		   tag,
		   (unsigned long)mmc.rx_fifo_overflow,
		   (unsigned long)mmc.rx_crc_error,
		   (unsigned long)ds.rx_buf_unavail,
		   (unsigned long)ds.rx_frames,
		   (unsigned long)ds.rx_dropped,
		   (unsigned long)ds.rx_irqs,
		   (unsigned long)ds.rx_csum_drops,
		   (unsigned long)ds.rx_errors,
		   (unsigned long)ds.dma_errors,
		   (unsigned long)ds.tx_frames,
		   (unsigned long)ds.tx_dropped,
		   (unsigned long)dg.rx_ready_count,
		   (unsigned long)dg.rx_free_count,
		   polls, fed, pool_fail, input_fail,
		   mem_err, pbuf_err, udp_in, udp_inerr, ip_drop, tcp_retrans);
}

static void init_buffer(uint8_t *buf, int32_t size)
{
	uint32_t i;
	uint8_t ch;

	buf[0] = 0;
	for (i = 1; i < size; i++) {
		do
			ch = (uint8_t)rand32();
		while (ch == 0);
		buf[i] = ch;
	}
}

static int32_t recv_data(int32_t sock, void *buf, int32_t size)
{
	int32_t rc = recv(sock, buf, size, 0);

	if (rc < 0)
		return rc;
	if (rc != size)
		return BSD_ERROR;
	return 0;
}

static int32_t send_data(int32_t sock, const void *buf, int32_t size)
{
	int32_t rc = send(sock, buf, size, 0);

	if (rc < 0)
		return rc;
	if (rc != size)
		return BSD_ERROR;
	return 0;
}

/* Arm's thread_exit: record where a thread died, print it, end the thread.
 * On this board the console sentence is the evidence. */
static void thread_exit(int32_t line, int32_t bsd_error)
{
	io.err_line = line;
	io.err_bsd = bsd_error;
	MDK_ANCHOR("netio: error line=%ld code=%ld\n",
		   (long)line, (long)bsd_error);
	osThreadExit();
}

#define __THREAD_EXIT(err) thread_exit(__LINE__, (err))

/* Thread attributes are written out rather than passed as NULL: CMSIS-RTOS2
 * then takes defaults, and on this image a default-priority thread was what
 * stopped the scheduler. Explicit stack and priority, always. */
/* Above the control threads, not below them: at 1440-byte datagrams the
 * receive path is a continuous interrupt storm, and a below-normal reader was
 * never scheduled once in six seconds - the mailbox filled to its 16-slot
 * ceiling and every further datagram was dropped before the reader saw one.
 * Measured (2026-10-29): iters=0 over the whole C2S window with
 * osPriorityBelowNormal. */
/* Receive-thread priority. A lever, not a preference: the cost instrument
 * measures the producer's feed span with the wall clock, so a consumer that
 * preempts it (the UDP receiver runs at AboveNormal) has its own processing
 * charged to the producer's feed bucket. Raising the producer above the
 * consumer is the discriminating experiment for that contamination - and,
 * if it also raises throughput, a fix in its own right. */
#ifndef MN_RX_PRIO
#define MN_RX_PRIO osPriorityNormal
#endif

static const osThreadAttr_t udp_rx_attr = {
	.name = "netio-udp-rx",
	.priority = osPriorityAboveNormal,
	.stack_size = 4096U,
};

/* UDP receiver thread: drains the UDP data socket into the shared buffer.
 * Spawned only after the client asked for a C2S run (Arm's shape).
 *
 * Polled, not blocked-on-socket: an lwIP socket close with a thread parked in
 * its receive mbox takes the kernel down. lwIP's sys_mbox_free deletes the
 * queue unconditionally and FreeRTOS asserts on a queue that still has tasks
 * waiting on it (queue.c:2265, configASSERT(listLIST_IS_EMPTY(
 * &pxQueue->xTasksWaitingToReceive))). So this thread is always *not*
 * blocked in lwip_recv when the control thread decides to stop it: it polls a
 * short receive timeout, and the control thread clears io_rx_stop first and
 * waits one poll interval before touching the socket. */
static volatile int io_rx_stop;

/* Datagrams this receiver has consumed since boot. The cost decomposition
 * needs a cumulative denominator: a cell is two snapshots apart, exactly like
 * the layered loss counters, so a phase cannot inherit the previous phase's
 * frames. */
static uint32_t io_rx_total;
static uint32_t io_empty_polls;
#define APP_SAMPLE_PERIOD 4096u
static uint32_t io_app_blocked;
static uint32_t io_app_charged;

static void UDP_Receiver(void *arg)
{
	int32_t rc;

	(void)arg;
	MDK_ANCHOR("netio: udp recv start\n");

	while (!io_rx_stop) {
		uint64_t t_recv = gtimer_count_get();

		/* Two-step receive, and the reason matters more than the code.
		 *
		 * The application bucket must be CPU cost, so it must not
		 * include time spent waiting for the next datagram. But the
		 * waiting itself is part of the vehicle: a pure polling loop
		 * with a one-tick sleep changed the measured system (throughput
		 * fell from 53 to 22 MB/s), which disqualifies the numbers.
		 *
		 * So: ask the socket whether a datagram is already queued.
		 * If yes, the receive cannot block and is charged. If not,
		 * block exactly as the original code did - that call is
		 * counted but charged zero, because its duration is the wait. */
		{
			int avail = 0;

			(void)lwip_ioctl(io.udp_sock, FIONREAD, &avail);
			if (avail > 0) {
				/* A datagram is already queued, so this receive
				 * cannot block: its whole duration is work and
				 * belongs to the application bucket. */
				rc = recv(io.udp_sock, (char *)io.buf,
					  io.pkt_size, 0);
				if (rc > 0) {
					net_prof_app_charge(gtimer_count_get() -
							    t_recv);
					io_app_charged++;
				}
			} else {
				rc = recv(io.udp_sock, (char *)io.buf,
					  io.pkt_size, 0);
				if (rc > 0) {
					/* Woke up with data: the duration is
					 * the wait, not the work. Counted, not
					 * charged. */
					io_app_blocked++;
				} else {
					io_empty_polls++;
					osDelay(1u);
					continue;
				}
			}
		}
		if (rc <= 0)
			continue;
		io.rx_bytes += (uint32_t)rc;
		io.pkt_count++;
		io_rx_total++;

		/* Sample window for the application bucket. In steady state the
		 * consumer is faster than the producer, so every receive finds
		 * an empty queue, blocks, and its duration is the wait rather
		 * than the work - the bucket would read zero. Pausing for two
		 * ticks every few thousand frames lets a small backlog form, so
		 * the next receives find data already queued and their (purely
		 * working) duration can be charged. The perturbation is ~1% of
		 * the cell and is identical in every arm, which is what makes
		 * the number comparable; the app_empty count still reports how
		 * often the queue really was empty. */
		if ((io_rx_total % APP_SAMPLE_PERIOD) == 0u)
			osDelay(2u);
	}
	MDK_ANCHOR("netio: udp recv end pkts=%lu\n", (unsigned long)io.pkt_count);
	osThreadExit();
}

/* Ask the receiver to stop and let it leave lwip_recv on its own. */
static void udp_rx_quiesce(void)
{
	io_rx_stop = 1;
	osDelay(5u);
	io_rx_stop = 0;
}

/* One measurement cell: the C2S window opens when the receiver thread is
 * created and closes when the client's RES arrives (after the receiver has
 * been quiesced), so the wall clock and every bucket cover exactly the same
 * frames. */
static struct net_prof_snap prof_a;
static uint64_t prof_t0;

static void UDP_Server(void *arg)
{
	struct sockaddr_in sa;
	CONTROL ctl;
	int32_t sock, csock, rc;
	socklen_t sa_len;
	int32_t nb;
	struct timeval tv;
	fd_set fds;

	(void)arg;
	MDK_ANCHOR("netio: udp server start\n");

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		__THREAD_EXIT(sock);

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(NETIO_AUXPORT);
	sa.sin_addr.s_addr = INADDR_ANY;
	rc = bind(sock, (struct sockaddr *)&sa, sizeof(sa));
	if (rc < 0)
		__THREAD_EXIT(rc);

	rc = listen(sock, 1);
	if (rc < 0)
		__THREAD_EXIT(rc);

	MDK_ANCHOR("netio: udp listening port=%d\n", NETIO_AUXPORT);

	for (;;) {
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		tv.tv_sec = 120;
		tv.tv_usec = 0;

		rc = select(sock + 1, &fds, NULL, NULL, &tv);
		if (rc < 0)
			__THREAD_EXIT(rc);
		if ((rc == 0) || (FD_ISSET(sock, &fds) == 0))
			continue;

			sa_len = sizeof(*io.client);
		csock = accept(sock, (struct sockaddr *)io.client, &sa_len);
		if (csock < 0)
			continue;

		MDK_ANCHOR("netio: udp client connected\n");

		for (;;) {
			rc = recv_data(csock, &ctl, sizeof(ctl));
			if (rc < 0)
				__THREAD_EXIT(rc);

			io.command = ntohl(ctl.cmd);

			if (io.command == CMD_C2S) {
				io.pkt_size = ntohl(ctl.data);
				io.pkt_count = 0;
				io.rx_bytes = 0;
				MDK_ANCHOR("netio: udp c2s pkt=%lu\n",
					   (unsigned long)io.pkt_size);

				if ((int32_t)io.pkt_size > (int32_t)io.buf_size)
					__THREAD_EXIT(BSD_EMSGSIZE);

				rc = socket(PF_INET, SOCK_DGRAM, 0);
				if (rc < 0)
					__THREAD_EXIT(rc);
				io.udp_sock = rc;

				/* Bound to the board's own address, not ANY: with
				 * ANY, lwIP matched the client's 1440-byte data
				 * datagrams against this unconnected socket and
				 * delivered them ahead of the control connection's
				 * data stream, so both streams interleaved and the
				 * C2S window counted zero packets. */
				memset(&sa, 0, sizeof(sa));
				sa.sin_family = AF_INET;
				sa.sin_port = htons(NETIO_AUXPORT);
				sa.sin_addr.s_addr = g_local_ip;
				rc = bind(io.udp_sock, (struct sockaddr *)&sa,
					  sizeof(sa));
				if (rc < 0)
					__THREAD_EXIT(rc);
				{
					struct timeval rtv;
					rtv.tv_sec = 0;
					rtv.tv_usec = 50000;
					(void)setsockopt(io.udp_sock, SOL_SOCKET,
							 SO_RCVTIMEO, &rtv,
							 sizeof(rtv));
				}

				/* Cell opens here. The application bucket is
				 * zeroed first so the snapshot that starts
				 * the cell carries a zero for it and the delta
				 * at the far end is the cell's own cost. */
				net_prof_bind(MN_GMAC);
				net_prof_app_reset();
				net_prof_take(&prof_a);
				prof_a.frames = io_rx_total;
				prof_a.app_empty = io_empty_polls + io_app_blocked;
				prof_a.app_charged = io_app_charged;
				prof_t0 = gtimer_count_get();

				io_rx_stop = 0;
				if (osThreadNew(UDP_Receiver, NULL,
						&udp_rx_attr) == NULL)
					__THREAD_EXIT(BSD_ERROR);

				/* The RES the client waits for: Arm sends the echoed
				 * control block back after the receiver is up. */
				rc = send_data(csock, (char *)&ctl, sizeof(ctl));
				if (rc < 0)
					__THREAD_EXIT(rc);
			} else if (io.command == CMD_RES) {
				struct net_prof_snap prof_b;
				uint64_t wall_ticks;

				udp_rx_quiesce();
				MDK_ANCHOR("netio: udp rx done pkts=%lu bytes=%lu\n",
					   (unsigned long)io.pkt_count,
					   (unsigned long)io.rx_bytes);

				/* Cell closes here, after the receiver is out of
				 * its receive call, so no bucket is read
				 * mid-flight. */
				wall_ticks = gtimer_count_get() - prof_t0;
				net_prof_take(&prof_b);
				prof_b.frames = io_rx_total;
				prof_b.app_empty = io_empty_polls + io_app_blocked;
				prof_b.app_charged = io_app_charged;
				net_prof_report("udp-c2s", &prof_a, &prof_b,
						wall_ticks / 24u);
				ctl.cmd = htonl(CMD_RES);
				ctl.data = htonl(io.pkt_count);
				rc = send_data(csock, (char *)&ctl, sizeof(ctl));
				if (rc < 0)
					__THREAD_EXIT(rc);
			} else if (io.command == CMD_S2C) {
				udp_rx_quiesce();
				io.pkt_size = ntohl(ctl.data);
				io.tx_bytes = 0;
				io.pkt_count = 0;
				if ((int32_t)io.pkt_size > (int32_t)io.buf_size)
					__THREAD_EXIT(BSD_EMSGSIZE);

				MDK_ANCHOR("netio: udp s2c pkt=%lu\n",
					   (unsigned long)io.pkt_size);

				io_rx_stop = 1;
				osDelay(5u);
				io.client->sin_port = htons(NETIO_AUXPORT);
				io.timeover = false;
				init_buffer(io.buf, io.buf_size);
				if (io.timer != NULL)
					osTimerStart(io.timer, 6000U);

				io.buf[0] = 0;
				while (!io.timeover) {
					for (nb = 0; nb < (int32_t)io.pkt_size;) {
						rc = sendto(io.udp_sock,
							    (char *)&io.buf[nb],
							    io.pkt_size - nb, 0,
							    (struct sockaddr *)io.client,
							    sizeof(*io.client));
						if (rc < 0) {
							while (!io.timeover)
								osDelay(2);
							goto done_send;
						}
						nb += rc;
					}
					io.tx_bytes += io.pkt_size;
					io.pkt_count++;
				}
done_send:
				lwip_close(io.udp_sock);
				MDK_ANCHOR("netio: udp tx done pkts=%lu bytes=%lu\n",
					   (unsigned long)io.pkt_count,
					   (unsigned long)io.tx_bytes);

				io.udp_sock = -1;
				ctl.cmd = htonl(CMD_RES);
				ctl.data = htonl(io.pkt_count);
				rc = send_data(csock, (char *)&ctl, sizeof(ctl));
				if (rc < 0)
					__THREAD_EXIT(rc);
			} else {
				break;
			}
		}
		emit_layer_counters("udp-end");
		lwip_close(csock);
	}
	lwip_close(sock);
}

static void TCP_Server(void *arg)
{
	struct sockaddr_in sa;
	CONTROL ctl;
	int32_t sock, csock, rc;
	socklen_t sa_len;
	int32_t nb;
	struct timeval tv;
	fd_set fds;

	(void)arg;
	MDK_ANCHOR("netio: tcp server start\n");

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		__THREAD_EXIT(sock);

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(NETIO_PORT);
	sa.sin_addr.s_addr = INADDR_ANY;
	rc = bind(sock, (struct sockaddr *)&sa, sizeof(sa));
	if (rc < 0)
		__THREAD_EXIT(rc);

	rc = listen(sock, 1);
	if (rc < 0)
		__THREAD_EXIT(rc);

	MDK_ANCHOR("netio: tcp listening port=%d\n", NETIO_PORT);

	for (;;) {
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		tv.tv_sec = 120;
		tv.tv_usec = 0;

		rc = select(sock + 1, &fds, NULL, NULL, &tv);
		if (rc < 0)
			__THREAD_EXIT(rc);
		if ((rc == 0) || (FD_ISSET(sock, &fds) == 0))
			continue;

		sa_len = sizeof(*io.client);
		csock = accept(sock, (struct sockaddr *)io.client, &sa_len);
		if (csock < 0)
			continue;

		MDK_ANCHOR("netio: tcp client connected\n");

		for (;;) {
			rc = recv_data(csock, &ctl, sizeof(ctl));
			if (rc < 0)
				__THREAD_EXIT(rc);

			io.command = ntohl(ctl.cmd);

			if (io.command == CMD_C2S) {
				io.pkt_size = ntohl(ctl.data);
				io.pkt_count = 0;
				io.rx_bytes = 0;
				MDK_ANCHOR("netio: tcp c2s pkt=%lu\n",
					   (unsigned long)io.pkt_size);
				if ((int32_t)io.pkt_size > (int32_t)io.buf_size)
					__THREAD_EXIT(BSD_EMSGSIZE);

				do {
					for (nb = 0; nb < (int32_t)io.pkt_size;) {
						rc = recv(csock,
							  (char *)&io.buf[nb],
							  io.pkt_size - nb, 0);
						if (rc < 0)
							break;
						nb += rc;
					}
					io.rx_bytes += io.pkt_size;
					io.pkt_count++;
				} while (io.buf[0] == 0 && rc > 0);

				MDK_ANCHOR("netio: tcp rx done pkts=%lu bytes=%lu\n",
					   (unsigned long)io.pkt_count,
					   (unsigned long)io.rx_bytes);
			} else if (io.command == CMD_S2C) {
				io.pkt_size = ntohl(ctl.data);
				io.pkt_count = 0;
				io.tx_bytes = 0;
				if (io.pkt_size > io.buf_size)
					__THREAD_EXIT(BSD_EMSGSIZE);
				MDK_ANCHOR("netio: tcp s2c pkt=%lu\n",
					   (unsigned long)io.pkt_size);

				init_buffer(io.buf, io.buf_size);
				io.timeover = false;
				if (io.timer != NULL)
					osTimerStart(io.timer, 6000U);

				io.buf[0] = 0;
				while (!io.timeover) {
					for (nb = 0; nb < (int32_t)io.pkt_size;) {
						rc = send(csock,
							  (char *)&io.buf[nb],
							  io.pkt_size - nb, 0);
						if (rc < 0)
							break;
						nb += rc;
					}
					io.tx_bytes += io.pkt_size;
					io.pkt_count++;
				}
				io.buf[0] = 1;
				rc = send_data(csock, io.buf, io.pkt_size);
				if (rc < 0)
					__THREAD_EXIT(rc);

				MDK_ANCHOR("netio: tcp tx done pkts=%lu bytes=%lu\n",
					   (unsigned long)io.pkt_count,
					   (unsigned long)io.tx_bytes);
			} else {
				break;
			}
		}
		emit_layer_counters("tcp-end");
		lwip_close(csock);
	}
	lwip_close(sock);
}

/* ---------------------------------------------------------------------------
 * Board glue: the GMAC line and the lwIP stack, the same sequence net.c runs.
 * ------------------------------------------------------------------------- */

static struct netif g_netif;
static osSemaphoreId_t g_rx_sem;
static int g_line_up;

static void rx_notify(unsigned int index)
{
	(void)index;
	if (g_rx_sem != NULL)
		(void)osSemaphoreRelease(g_rx_sem);
}

/* With CONFIG_GMAC_RX_BH the ISR only signals: somebody has to run the
 * completion sweep. Both the interrupt thread and a 100 ms service thread do
 * it, exactly as net.c has done since 2026-10-24. */
static void rx_thread(void *arg)
{
	(void)arg;
	for (;;) {
		(void)osSemaphoreAcquire(g_rx_sem, osWaitForever);
		dwc_eqos_watchdog(MN_GMAC);
		(void)ethnetif_poll(&g_netif);
	}
}

static void service_thread(void *arg)
{
	(void)arg;
	for (;;) {
		osDelay(100u);
		dwc_eqos_watchdog(MN_GMAC);
	}
}

static int line_up(void)
{
	const struct dwc_eqos_plat *plat;
	eqos_link_info_t link;
	uint32_t configure;
	ip4_addr_t ip, mask, gw;
	uint8_t mac[6];

	if (g_line_up)
		return 0;

	plat = eqos_board_plat(MN_GMAC);
	if (plat == NULL || dwc_eqos_initialize(MN_GMAC, plat) != 0) {
		CASE_ANCHOR("net: netio FAIL eqos init\n");
		return -1;
	}
	if (dwc_eqos_power_control(MN_GMAC, EQOS_POWER_FULL) != EQOS_OK) {
		CASE_ANCHOR("net: netio FAIL gmac power\n");
		return -1;
	}
	if (rtl8211f_initialize(MN_GMAC, (void *)(uintptr_t)MN_GMAC,
				eqos_board_mdio_read,
				eqos_board_mdio_write) != EQOS_OK) {
		CASE_ANCHOR("net: netio FAIL phy init\n");
		return -1;
	}
	if (rtl8211f_power_control(MN_GMAC, EQOS_POWER_FULL) != EQOS_OK) {
		CASE_ANCHOR("net: netio FAIL phy power\n");
		return -1;
	}
	(void)rtl8211f_set_interface(MN_GMAC, EQOS_INTERFACE_RGMII);
	if (rtl8211f_set_mode(MN_GMAC,
			      EQOS_PHY_AUTO_NEGOTIATE |
			      EQOS_PHY_SPEED_1G) != EQOS_OK) {
		CASE_ANCHOR("net: netio FAIL phy autoneg\n");
		return -1;
	}
	{
		unsigned int waited = 0u;

		for (;;) {
			link = rtl8211f_get_link_info(MN_GMAC);
			if (link.speed != 0u)
				break;
			if (waited >= 15000u) {
				CASE_ANCHOR("net: netio FAIL link timeout\n");
				return -1;
			}
			(void)osDelay(100u);
			waited += 100u;
		}
	}
	/* EQOS_DUPLEX_FULL is 0 in this driver's mask encoding: only HALF sets
	 * a bit, so OR-ing the duplex value would program half duplex. */
	configure = link.speed | (link.duplex == EQOS_DUPLEX_HALF ?
				  EQOS_DUPLEX_Msk : 0u);
	(void)dwc_eqos_control(MN_GMAC, DWC_EQOS_CONFIGURE, configure);
	(void)dwc_eqos_control(MN_GMAC, DWC_EQOS_CONTROL_TX, 1);
	(void)dwc_eqos_control(MN_GMAC, DWC_EQOS_CONTROL_RX, 1);

	CASE_ANCHOR("net: netio link speed=%lu duplex=%lu\n",
		    (unsigned long)link.speed, (unsigned long)link.duplex);

	g_rx_sem = osSemaphoreNew(0xFFFFu, 0u, NULL);
	if (g_rx_sem == NULL) {
		CASE_ANCHOR("net: netio FAIL rx sem\n");
		return -1;
	}
	dwc_eqos_set_rx_notify(rx_notify);

	if (osThreadNew(rx_thread, NULL,
			&(const osThreadAttr_t){ .name = "netio-rx",
						 .priority = MN_RX_PRIO,
						 .stack_size = 4096U }) == NULL ||
	    osThreadNew(service_thread, NULL,
			&(const osThreadAttr_t){ .name = "netio-svc",
						 .priority = osPriorityLow,
						 .stack_size = 2048U }) == NULL) {
		CASE_ANCHOR("net: netio FAIL threads\n");
		return -1;
	}

	if (case_net_stack_up() != 0) {
		CASE_ANCHOR("net: netio FAIL stack\n");
		return -1;
	}

	IP4_ADDR(&ip, 192, 168, 0, MN_IP3);
	g_local_ip = ip.addr;
	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gw, 192, 168, 0, MN_GW3);

	LOCK_TCPIP_CORE();
	ethnetif_add(&g_netif, MN_GMAC, &ip, &mask, &gw);
	UNLOCK_TCPIP_CORE();

	(void)dwc_eqos_get_mac_address(MN_GMAC, mac);
	CASE_ANCHOR("net: netio addr 192.168.0.%u/24 "
		    "mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    MN_IP3, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	g_line_up = 1;
	return 0;
}

/* Arm's app_main_thread: static buffer, one-shot timer, then one control
 * thread per protocol. */
static uint8_t g_io_buf[BUF_SIZE];
static struct sockaddr_in g_sa_client;

static const osThreadAttr_t tcp_srv_attr = {
	.name = "netio-tcp",
	.priority = osPriorityNormal,
	.stack_size = 4096U,
};

static const osThreadAttr_t udp_srv_attr = {
	.name = "netio-udp",
	.priority = osPriorityNormal,
	.stack_size = 4096U,
};

int case_net_mdk_netio_run(void)
{
	CASE_ANCHOR("rtos: case net_mdk_netio start\n");

	if (line_up() != 0) {
		CASE_ANCHOR("net: netio FAIL line-up\n");
		return 1;
	}

	io.buf = &g_io_buf[0];
	io.buf_size = sizeof(g_io_buf);
	io.client = &g_sa_client;
	io.udp_sock = -1;
	io.timer = osTimerNew(cb_timer, osTimerOnce, NULL, NULL);
	if (io.timer == NULL) {
		CASE_ANCHOR("net: netio FAIL timer\n");
		return 1;
	}

	/* Sequential on purpose: TCP first, and UDP only after it is parked in
	 * select(). A kernel that cannot host the second thread says so with a
	 * console line instead of taking the whole board down silently. */
	if (osThreadNew(TCP_Server, NULL, &tcp_srv_attr) == NULL) {
		CASE_ANCHOR("net: netio FAIL tcp thread\n");
		return 1;
	}
	osDelay(500u);
	if (osThreadNew(UDP_Server, NULL, &udp_srv_attr) == NULL) {
		CASE_ANCHOR("net: netio FAIL udp thread\n");
		return 1;
	}

	emit_layer_counters("boot");
	CASE_ANCHOR("net: netio ready port=%d aux=%d buf=%u\n",
		    NETIO_PORT, NETIO_AUXPORT, (unsigned)BUF_SIZE);

	for (;;)
		osDelay(1000u);
	return 0;
}