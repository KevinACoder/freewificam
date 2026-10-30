/*
 * @file   ethnetif.h
 * @brief  lwIP netif bridge over the GMAC driver: the adapter surface.
 *
 * The bridge is the only consumer of the driver's receive path and the only
 * producer of its transmit path; the stack sees it as one netif. The driver
 * side of the seam is dwc_eqos_read_frame/send_frame (board/rk3568-itx/drivers),
 * the stack side is tcpip_input (the RX thread hands frames to the TCP/IP
 * mailbox).
 */

#ifndef FREEWEB_LWIP_ETHNETIF_H
#define FREEWEB_LWIP_ETHNETIF_H

#include "lwip/ip_addr.h"
#include "lwip/netif.h"

/* Create the netif for one GMAC port and register it with the stack. Runs in
 * a thread context (the call comes under the core lock from the caller). */
void ethnetif_add(struct netif *netif, unsigned int port,
		  const ip4_addr_t *ip, const ip4_addr_t *mask,
		  const ip4_addr_t *gw);

/* Driver-side receive seam: drain the received frames into the stack
 * (netif->input = tcpip_input, or ethernet_input under the batch lever).
 * Returns the number of frames handed over. */
unsigned int ethnetif_poll(struct netif *netif);

#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
/* Arm the zero-copy receive path: installs the driver's lend-out hook, after
 * which completed frames are wrapped in custom pbufs over the driver's own
 * buffers instead of being copied. Must run BEFORE the receiver is started -
 * frames that complete with no hook are dropped by the driver, by design, so
 * arming late shows up as receive loss rather than as a hang. Idempotent. */
void ethnetif_zc_arm(void);
#endif

/* Loss counters for the heartbeat: poll calls, pbuf-pool failures, input
 * failures, frames fed in total. */
void ethnetif_get_diag(unsigned long *poll_calls, unsigned long *pool_fail,
		       unsigned long *input_fail, unsigned long *fed_total);

/* Service-time split of the receive bridge (2026-10-18): prep = pbuf_alloc +
 * pbuf_take, input = the netif->input call (ethernet_input on the caller's
 * thread under CORE_LOCKING_INPUT, else just a mailbox post). Ticks are the
 * 24 MHz system counter. The UDP-vs-TCP cut needs to know which half the
 * protocol-dependent cost sits in. */
void ethnetif_get_stack_prof(uint64_t *prep_ticks, uint64_t *input_ticks,
			     unsigned long *input_calls);

/* The pbuf-copy share of prep_ticks (subset, not an addition): pbuf_take in
 * the copying shapes, the driver's staged->pbuf read in the DIRECT shape. It
 * exists so a cost decomposition can name the copy instead of hiding it in
 * "prep". */
uint64_t ethnetif_get_prep_copy_ticks(void);

/* Traffic-shape classification (2026-10-19): every IP/TCP frame crossing the
 * bridge, split by what it is. The TCP-gap round needs the shape mix - is the
 * flow data segments with a few ACKs, or a wall of tiny pure ACKs? - because
 * "which end is short" cannot be read from byte counters alone. */
struct ethnetif_shape {
	unsigned long frames;
	unsigned long data;		/* TCP with payload */
	unsigned long pure_ack;		/* TCP header only, ACK set */
	unsigned long ack_dup;		/* pure ACK identical to the previous
					 * one in (seq, ack) AND window: the true
					 * duplicate-ACK signature */
	unsigned long ack_wupd;		/* same (seq, ack), different window:
					 * a window update, not a hole report */
	unsigned long syn;
	unsigned long fin;
	unsigned long rst;
	unsigned long other;		/* non-IPv4, non-TCP, malformed */
};

void ethnetif_get_shape(struct ethnetif_shape *rx, struct ethnetif_shape *tx);
void ethnetif_reset_shape(void);

/* Frame classes for the input-cost split (2026-10-19 round 2). The shape
 * counters say how many of each kind crossed; these index the split arrays
 * below so the same kinds can be priced individually - "the input bucket is
 * 6.36 us/frame" is not actionable until it is known WHICH frame class pays
 * it. Disjoint by construction: every classified frame lands in exactly one. */
#define ETHNETIF_CLASS_DATA	0	/* TCP with payload */
#define ETHNETIF_CLASS_ACK	1	/* pure ACK, new (seq, ack) */
#define ETHNETIF_CLASS_ACKDUP	2	/* pure ACK repeating (seq, ack, win) */
#define ETHNETIF_CLASS_ACKWUPD	3	/* pure ACK, same (seq, ack), new win */
#define ETHNETIF_CLASS_UDP	4	/* IPv4/UDP (the control protocol) */
#define ETHNETIF_CLASS_OTHER	5	/* SYN/FIN/RST/malformed/anything else */
#define ETHNETIF_CLASSES	6

/* The receive input call, split (INPUTSPLIT=1 builds). wait = wall time
 * blocked acquiring the core lock; proc = the ethernet_input body itself
 * (ARP/IP/TCP state machine, including any transmission it triggers). Both
 * are ticks of the 24 MHz system counter and both are indexed by frame class,
 * so `wait[ACK]` answers "how much of the uplink's per-ACK cost is lock
 * handoff" directly. calls[] is the denominator. Without the lever the
 * arrays stay zero and `gmac split` says so. */
void ethnetif_get_input_split(uint64_t *wait_ticks, uint64_t *proc_ticks,
			      unsigned long *calls);
void ethnetif_reset_input_split(void);

/* TX inter-arrival rhythm (INPUTSPLIT=1 builds): log2 histogram of the gap
 * between consecutive eth_linkoutput entries, same bucket helper as the RX
 * gap instrument. The uplink's send cadence - ACKs in, data segments out -
 * lives here; a paced flow shows a fat tail, a back-to-back burst piles into
 * the low buckets. */
void ethnetif_get_tx_gap(uint64_t *hist, uint64_t *samples);
void ethnetif_reset_tx_gap(void);

/* Fine-grained TCP input-path probe (FINE=1 builds; patch 0002): the lwIP
 * TCP layer calls ethnetif_tcp_fine_mark() through LWIP_HOOK_TCP_PROBE at
 * fixed points of the input path, and this side buckets the 24 MHz tick
 * deltas by the site PAIR they span. The measured ~30 us per pure ACK is one
 * tcp_input entry that internally does the ACK's state-machine work, the
 * application sent callback (lwiperf writes the next segment) and the
 * deferred tcp_output that hands that segment to the wire - the pair table
 * is what splits those apart. Pair order is ABI for `gmac tcpfine`. */
#define ETHNETIF_TCP_FINE_PAIRS 11

/* Names for the pair deltas, in slot order; defined next to the table. */
extern const char *const ethnetif_tcp_fine_names[ETHNETIF_TCP_FINE_PAIRS];

/* The lwIP-side hook target. `site` is the LWIP_HOOK_TCP_PROBE site number:
 * 0 tcp_input entry, 1 before TCP_EVENT_SENT, 2 after it, 3 before the
 * deferred tcp_output, 4 tcp_output entry, 5 after the unacked walk, 6 one
 * data segment sent, 7 one control segment sent. Runs on the TCP/IP context
 * (single-threaded under the core lock); must not change any state. */
void ethnetif_tcp_fine_mark(unsigned int site);

/* Read the buckets: per-pair tick totals and call counts, the "other" pile
 * (deltas between sites that are not a tabled pair - e.g. a timer-context
 * tcp_output landing after a send; the tail/inter-frame delta left by an
 * entry's LAST site is dropped at the next entry, by design), and the
 * denominator counters: entries (site 0 seen), data handoffs (site 6),
 * control handoffs (site 7). */
void ethnetif_get_tcp_fine(uint64_t *pair_ticks, unsigned long *pair_calls,
			   uint64_t *other_ticks, unsigned long *entries,
			   unsigned long *data_handoffs,
			   unsigned long *ctrl_handoffs);
void ethnetif_reset_tcp_fine(void);

#endif /* FREEWEB_LWIP_ETHNETIF_H */
