/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net.h
 * @brief  The network-egress seam: how a frame leaves the component for the
 *         network stack, and how the stack hands one back.
 *
 * Downward (stack -> device): the egress module resolves the interface's
 * net_device and calls lkpi_netdev_xmit(), which allocates the skb, copies the
 * frame and enters the device's own ndo_start_xmit (mac80211's, once the stack
 * lands).  This is the same hand-off the in-tree lwIP port performs today
 * (third-party/lwip/ports/lkpinetif.c: lkpinetif_tx_frame).
 *
 * Upward (device -> stack): the egress module defines lkpi_netdev_rx().  The
 * component's netif_rx() calls it (the definition is a weak symbol, so an
 * image without an egress consumes the frame and says nothing).  The callee
 * owns the skb and must release it.
 *
 * Replaceability is link-time, not runtime: one image has exactly one
 * definition of lkpi_netdev_rx, the same discipline the bus seam's backends
 * follow.  lwIP is the intended implementation; the null egress is the
 * reference second implementation the seam's test links.
 *
 * STATUS: draft (FW_NET_API_READY 0).  The seam is frozen when the real
 * mac80211 net-device path compiles against it; until then this header states
 * the contract the existing code already satisfies and nothing more.
 *
 * Relation to the larger egress design: the frame path above is the narrow
 * half.  The full stack-facing contract - a stack_ops table with tx/rx, link
 * up/down and set_mac, carrying the stack's own interface pointer - is
 * proposed in the project's docs/EGRESS-DESIGN.md (section 3.3), where the
 * management plane (netdev lifecycle, notifiers, the 38-API surface) is
 * measured and the two-layer split argued.  That proposal is not adopted here
 * yet, and deliberately so: it is unproven, and this header only freezes what
 * code already satisfies.  When mac80211's net_device path compiles, this
 * header grows into that contract or is replaced by it, in one commit.
 */

#ifndef LINUX80211_NET_H
#define LINUX80211_NET_H

#include <stddef.h>

/* 0: the shape may still change when mac80211's net_device path lands. */
#define FW_NET_API_READY 0

struct sk_buff;
struct net_device;

/*
 * Provided by the egress module.  Called from netif_rx() with ownership of the
 * skb; must release it exactly once.  A weak declaration in the component lets
 * an image without an egress link.
 */
void lkpi_netdev_rx(struct sk_buff *skb);

/*
 * Provided by the component.  Allocates an skb, copies `len` bytes of `frame`
 * and enters the device's ndo_start_xmit; on success the device owns the skb.
 * Returns 0, -ENODEV when the device has no transmit entry, or -ENOMEM.
 */
int lkpi_netdev_xmit(struct net_device *dev, const void *frame, size_t len);

/*
 * The lwIP egress' case face (C6).
 *
 * The board case is compiled in the stack's world, so it cannot include lwIP's
 * headers (the two worlds disagree about htons/ntohs and the buffer types).
 * These three calls are the whole contract: plain C types only, implemented in
 * the lwIP world by third-party/lwip/ports/lkpinetif_case.c.
 */

/* Assemble the lwIP interface over the wireless net device ("wlan0"), set the
 * static address and bring it up.  No DHCP.  Returns 0 on success, negative on
 * a bad argument, a missing interface or a failed netif_add. */
int lkpi_net_wlan_up(const char *ip, const char *mask, const char *gw,
		     char mac[6]);

/* Send `count` ICMP echoes of `payload_len` bytes to `dst`, one at a time,
 * waiting at most `timeout_ms` for each reply.  Returns 0 when every echo was
 * answered, -1 otherwise.  `rx` is the answered count; the RTT outputs are in
 * milliseconds (0 when no reply carried a usable stamp). */
int lkpi_net_ping(const char *dst, unsigned count, unsigned payload_len,
		  unsigned timeout_ms, unsigned *rx, unsigned *rtt_min_ms,
		  unsigned *rtt_avg_ms, unsigned *rtt_max_ms);

/* The adapter's own counters, copied out so the case can print them. */
struct lkpi_net_diag {
	unsigned long rx_frames;
	unsigned long rx_foreign;
	unsigned long rx_eapol;
	unsigned long rx_no_netif;
	unsigned long rx_input_fail;
	unsigned long tx_frames;
	unsigned long tx_fail;
	unsigned long rx_bytes;
	unsigned long tx_bytes;
	unsigned long rx_deliver_ns;
	unsigned long rx_deliver_max_ns;
	unsigned long tx_ndo_ns;
	unsigned long tx_ndo_max_ns;
};

void lkpi_net_diag(struct lkpi_net_diag *out);

/* --- the throughput phases (C6T) -----------------------------------------
 *
 * The case drives the same seven phases on both arms; each call below is one
 * phase and fills what the phase's *receiver* measured.  The case prints,
 * the harness compares; no call applies a pass threshold.
 *
 * `kbit` is lwIP's own report unit (kbit/s); the UDP marker computes it from
 * the same bytes/ms the sink counted, so the two directions read alike.
 */
struct lkpi_net_phase {
	int done;		/* the phase's completion actually arrived */
	unsigned bytes;		/* payload bytes counted at the receiver */
	unsigned ms;		/* the phase's own measured duration */
	unsigned kbit;		/* kbit/s (payload, not line rate) */
	unsigned pkts;		/* UDP datagrams (0 for TCP phases) */
};

/* Start the boot's one lwiperf TCP server on `port`.  Returns 0 when it is
 * listening (a second call with the same port is a no-op), -1 otherwise. */
int lkpi_net_iperf_server_start(unsigned port);

/* 1 when the one-shot stack bring-up completed and the wireless netif was
 * added; 0 before that (the C5 case owns both). */
int lkpi_net_ready(void);

/* A downlink session the caller itself paces (the stability case's long run):
 * begin arms the server's report slot, poll returns the current state without
 * waiting (`done` stays 0 until the peer closes), end releases the slot. */
int lkpi_net_iperf_down_begin(void);
int lkpi_net_iperf_down_poll(struct lkpi_net_phase *out);
void lkpi_net_iperf_down_end(void);

/* Serve one TCP session for at most `window_ms` (the peer connects into the
 * board).  Fills the server-side report.  Returns 0 when a session completed
 * and moved bytes, -1 on a missing peer or an empty session. */
int lkpi_net_iperf_down(unsigned port, unsigned window_ms,
			struct lkpi_net_phase *out);

/* Initiate one TCP session to `peer:port` (the board is the sender).  Waits
 * at most `wait_ms`; fills the client-side report.  Returns 0 when the
 * session completed and moved bytes. */
int lkpi_net_iperf_up(const char *peer, unsigned port, unsigned wait_ms,
		      struct lkpi_net_phase *out);

/* Bind the UDP counting sink on `port` once per boot. */
int lkpi_net_udp_sink_start(unsigned port);

/* One sink window: counters are reset at entry, so a phase is one number.
 * `bytes`/`pkts` are what arrived; `kbit` is derived from the window. */
int lkpi_net_udp_sink_window(unsigned window_ms, struct lkpi_net_phase *out);

/* Blast 1470-byte datagrams at `peer:port` for `window_ms`, paced at
 * `rate_kbps` (0 = unpaced, the wired net case's capacity shape).  On Wi-Fi an
 * unpaced blast saturates the skb pool and the transmit entry starts refusing
 * frames, so the delivered rate stops being a capacity read; 40000 matches the
 * peer arm's `iperf -u -b 40M`.  The counts are what was offered; the Pi's
 * counting sink is the receiver's witness. */
int lkpi_net_udp_blast(const char *peer, unsigned port, unsigned window_ms,
		       unsigned rate_kbps, struct lkpi_net_phase *out);

/* Board-local TCP session (127.0.0.1:port) against the running server.  This
 * is an attribution instrument, never a pass criterion: both endpoints share
 * one TCP/IP thread. */
int lkpi_net_loopback(unsigned port, unsigned window_ms,
		      struct lkpi_net_phase *out);

/* The lwIP stack's own counters, copied out for the phase census.  Deltas are
 * the caller's job. */
struct lkpi_net_stack_diag {
	unsigned long tcp_recv;
	unsigned long tcp_xmit;
	unsigned long tcp_drop;
	unsigned long tcp_memerr;
	unsigned long tcp_err;
	unsigned long pbuf_avail;
	unsigned long pbuf_err;
	unsigned long tcpseg_avail;
	unsigned long tcpseg_err;
	unsigned long mbox_post_ok;
	unsigned long mbox_post_full;
	unsigned long mbox_fetch_ok;
	unsigned long mbox_fetch_empty;
	unsigned long mbox_fetch_wait_ms;
};

void lkpi_net_stack_diag(struct lkpi_net_stack_diag *out);

#endif /* LINUX80211_NET_H */
