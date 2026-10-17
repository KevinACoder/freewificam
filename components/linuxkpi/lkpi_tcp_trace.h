/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    lkpi_tcp_trace.h
 * @brief   The received-TCP-acknowledgement path's per-boundary account.
 *
 * A TCP acknowledgement from the peer is an 802.11 data frame the board
 * receives; by the time lwIP acts on it, it has crossed the USB engine, the
 * driver's aggregate parser, the irqsafe queue, the receive worker, the
 * net80211 input path and the TCP/IP mailbox. A throughput number cannot say
 * which of those boundaries owns the delay, and a stall that comes from an
 * acknowledgement arriving late is indistinguishable from one that comes from
 * a slow transmit path.
 *
 * This face stamps one record per received frame at each boundary it crosses
 * and reduces the deltas into fixed-width histograms, so a phase reports the
 * latency budget of the acknowledgement path instead of one aggregate. It
 * also carries the pcb's congestion state at the two moments that matter - the
 * acknowledgement that advanced the window and the retransmission timer that
 * collapsed it - because "the window collapsed" and "the transmit path is
 * slow" produce the same frame interval.
 *
 * The interface is plain integers on purpose: the net80211 bridge, the
 * linuxkpi USB engine and the lwIP port all feed it, and none of them may pull
 * another world's headers in to do so.
 *
 * @date    17.10.2026
 */

#ifndef LKPI_TCP_TRACE_H
#define LKPI_TCP_TRACE_H

#include <stdint.h>
#include <stddef.h>

/* The points a received frame crosses, in order. The enumerations stay
 * outside the instrument guard: a call site names a boundary whether or not
 * the instrument is compiled in, and the disabled branch must still resolve
 * the name. */
enum {
	LKPI_TCP_B_USB = 0,	/* the bulk-in transfer completed */
	LKPI_TCP_B_RXQ_IN,	/* ieee80211_rx_irqsafe entry */
	LKPI_TCP_B_RXQ_OUT,	/* the receive worker took it */
	LKPI_TCP_B_BRIDGE,	/* the bridge's receive entry */
	LKPI_TCP_B_NETIF,	/* the lwIP netif's input */
	LKPI_TCP_B_TCPIN,	/* tcp_input matched the segment to a pcb */
	LKPI_TCP_B_ACK_ADV,	/* the segment acknowledged new data */
	LKPI_TCP_B_N
};

/* The deltas accounted, each between two consecutive boundaries above. */
enum {
	LKPI_TCP_D_USB_RXQ = 0,	/* completion -> irqsafe: the driver's parse */
	LKPI_TCP_D_RXQ,		/* irqsafe -> worker: the queue's latency */
	LKPI_TCP_D_RXQ_BRIDGE,	/* worker -> bridge entry */
	LKPI_TCP_D_BRIDGE_NETIF,/* net80211 input, decap, pbuf copy */
	LKPI_TCP_D_NETIF_TCPIN,	/* the TCP/IP mailbox and thread wakeup */
	LKPI_TCP_D_TCPIN_ADV,	/* tcp_input -> lastack moved */
	LKPI_TCP_D_ADV_TX,	/* lastack moved -> segment handed to netif */
	LKPI_TCP_D_ADV_SUBMIT,	/* lastack moved -> the transfer submitted */
	LKPI_TCP_D_TX_LINKOUT,	/* ... -> the netif's linkoutput */
	LKPI_TCP_D_TX_BRIDGE,	/* ... -> the bridge's raw transmit entry */
	LKPI_TCP_D_TX_SEAM,	/* ... -> the seam handing it to the driver */
	LKPI_TCP_D_N
};

/* the transmit path's intermediate boundaries, measured from the same origin
 * as LKPI_TCP_D_ADV_* */
enum {
	LKPI_TCP_TX_LINKOUT = 0,
	LKPI_TCP_TX_BRIDGE,
	LKPI_TCP_TX_SEAM,
	LKPI_TCP_TX_N
};

#if defined(CONFIG_WLAN_TCP_TRACE) && defined(CONFIG_LKPI_CENSUS)

#define LKPI_TCP_TRACE_ENABLED	1

/* the USB completion time the driver's callback runs under */
void		lkpi_tcp_trace_usb_done(uint64_t t_ns);

/* the driver's own share of the path, settled on the thread that ran it: the
 * receive worker runs later and would race the next completion's stamp. Called
 * for received data frames only; in the uplink phase those are the peer's
 * acknowledgements. */
void		lkpi_tcp_trace_usb_to_rxq(uint64_t t_ns);

/* the receive worker opens one record per frame and closes it when the
 * bridge call returns */
void		lkpi_tcp_trace_frame_begin(uint64_t t_rxq_in, uint64_t t_rxq_out);
void		lkpi_tcp_trace_mark(unsigned int boundary);
void		lkpi_tcp_trace_frame_end(void);

/* the netif input: classify the frame and, when it is a pure acknowledgement,
 * account the boundaries it has crossed so far and hand it to the TCP/IP
 * thread's side of the account */
void		lkpi_tcp_trace_classify(const uint8_t *frame, size_t len);

/* the pcb's own moments, fed from the lwIP event hook */
void		lkpi_tcp_trace_tcp_in(uint32_t ackno, uint32_t seqno, uint32_t len);
void		lkpi_tcp_trace_ack_adv(uint32_t ackno, uint32_t acked);
void		lkpi_tcp_trace_sent(uint32_t ackno, uint32_t cwnd,
		    uint32_t ssthresh, uint32_t mss, uint32_t nrtx,
		    uint32_t unacked, uint32_t unsent, uint32_t snd_wnd,
		    uint32_t snd_buf, int32_t rto_ticks);
void		lkpi_tcp_trace_seg_tx(uint32_t seqno, uint32_t len);
void		lkpi_tcp_trace_tx_submit(uint64_t t_ns);
/* one intermediate point of the transmit path, measured from the same
 * acknowledgement advance that produced the segment */
void		lkpi_tcp_trace_tx_mark(unsigned int boundary);
void		lkpi_tcp_trace_rto(uint32_t lastack, uint32_t snd_nxt,
		    uint32_t cwnd, uint32_t ssthresh, uint32_t mss,
		    uint32_t nrtx, uint32_t unacked, uint32_t unsent,
		    int32_t rto_ticks);

void		lkpi_tcp_trace_reset(void);
void		lkpi_tcp_trace_print(void);

#else /* no instrument */

#define LKPI_TCP_TRACE_ENABLED	0

#define lkpi_tcp_trace_usb_done(t)		((void)(t))
#define lkpi_tcp_trace_usb_to_rxq(t)		((void)(t))
#define lkpi_tcp_trace_frame_begin(a, b)	((void)(a), (void)(b))
#define lkpi_tcp_trace_mark(b)			((void)(b))
#define lkpi_tcp_trace_frame_end()		((void)0)
#define lkpi_tcp_trace_classify(f, l)		((void)(f), (void)(l))
#define lkpi_tcp_trace_tcp_in(a, s, l)		((void)(a), (void)(s), (void)(l))
#define lkpi_tcp_trace_ack_adv(a, k)		((void)(a), (void)(k))
#define lkpi_tcp_trace_sent(a, c, s, m, n, u, q, w, b, r) \
	((void)(a), (void)(c), (void)(s), (void)(m), (void)(n), (void)(u), \
	 (void)(q), (void)(w), (void)(b), (void)(r))
#define lkpi_tcp_trace_seg_tx(s, l)		((void)(s), (void)(l))
#define lkpi_tcp_trace_tx_submit(t)		((void)(t))
#define lkpi_tcp_trace_tx_mark(b)		((void)(b))
#define lkpi_tcp_trace_rto(a, s, c, t, m, n, u, q, r) \
	((void)(a), (void)(s), (void)(c), (void)(t), (void)(m), (void)(n), \
	 (void)(u), (void)(q), (void)(r))
#define lkpi_tcp_trace_reset()			((void)0)
#define lkpi_tcp_trace_print()			((void)0)

#endif /* CONFIG_WLAN_TCP_TRACE && CONFIG_LKPI_CENSUS */

#endif /* LKPI_TCP_TRACE_H */
