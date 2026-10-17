/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    wlan_tcp_trace.c
 * @brief   The lwIP half of the acknowledgement-path account: turn the TCP
 *          event hook's pcb into the plain numbers the census face records.
 *
 * @design  the hook itself is patches/lwip/0005-tcp-event-hook.patch, a
 *          build-time extension point that expands to nothing unless a build
 *          points it somewhere. This file is that somewhere for the WLAN
 *          image; the accounting lives in components/linuxkpi/lkpi_tcp_trace.c
 *          so that this translation unit stays inside the lwIP world and never
 *          pulls a linuxkpi header in.
 *
 *          The hook runs on the TCP/IP context, which already holds the core
 *          lock, so walking the pcb's queues here is safe and costs nothing
 *          when the instrument is off.
 *
 * @date    17.10.2026
 */

#include "lwip/opt.h"
#include "lwip/tcp.h"
#include "lwip/priv/tcp_priv.h"

#include "components/linuxkpi/lkpi_tcp_trace.h"

#if LKPI_TCP_TRACE_ENABLED

/* The queue depth is the answer to "did the window collapse to one segment",
 * so it is counted rather than inferred from the byte counters. Bounded: a
 * corrupt link would otherwise walk forever. */
static uint32_t
wlan_tcp_qlen(struct tcp_seg *seg)
{
	uint32_t	n = 0u;

	while (seg != NULL && n < 64u) {
		n++;
		seg = seg->next;
	}
	return n;
}

/*
 * Called from tcp_in.c / tcp_out.c / tcp.c at the sites enumerated in
 * lwip/priv/tcp_priv.h. `pcb` arrives as void* because lwipopts.h cannot name
 * lwIP types yet when it defines the macro.
 */
void
wlan_tcp_event(unsigned int site, void *pcb_arg, unsigned long ackno,
    unsigned long seqno, unsigned long len)
{
	struct tcp_pcb *pcb = (struct tcp_pcb *)pcb_arg;

	if (pcb == NULL)
		return;
	switch (site) {
	case LWIP_TCP_EV_INPUT:
		lkpi_tcp_trace_tcp_in((uint32_t)ackno, (uint32_t)seqno,
		    (uint32_t)len);
		break;
	case LWIP_TCP_EV_ACK_ADV:
		lkpi_tcp_trace_ack_adv((uint32_t)ackno, (uint32_t)len);
		break;
	case LWIP_TCP_EV_SENT:
		lkpi_tcp_trace_sent((uint32_t)ackno, (uint32_t)pcb->cwnd,
		    (uint32_t)pcb->ssthresh, (uint32_t)pcb->mss,
		    (uint32_t)pcb->nrtx, wlan_tcp_qlen(pcb->unacked),
		    wlan_tcp_qlen(pcb->unsent), (uint32_t)pcb->snd_wnd,
		    (uint32_t)pcb->snd_buf, (int32_t)pcb->rto);
		break;
	case LWIP_TCP_EV_SEG_TX:
		lkpi_tcp_trace_seg_tx((uint32_t)seqno, (uint32_t)len);
		break;
	case LWIP_TCP_EV_RTO:
		lkpi_tcp_trace_rto((uint32_t)ackno, (uint32_t)seqno,
		    (uint32_t)pcb->cwnd, (uint32_t)pcb->ssthresh,
		    (uint32_t)pcb->mss, (uint32_t)pcb->nrtx,
		    wlan_tcp_qlen(pcb->unacked), wlan_tcp_qlen(pcb->unsent),
		    (int32_t)pcb->rto);
		break;
	default:
		break;
	}
}

#endif /* LKPI_TCP_TRACE_ENABLED */
