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
 * static address and bring it up.  No DHCP.  `gw` may be NULL: the AP role has
 * no default route and its peer is on-link.  Returns 0 on success, negative on
 * a bad argument, a missing interface or a failed netif_add. */
int lkpi_net_wlan_up(const char *ip, const char *mask, const char *gw,
		     char mac[6]);

/* The DHCP form used by G2's external AP.  Build the netif (the caller already
 * used lkpi_net_wlan_up with 0.0.0.0), then start the client after association
 * and wait for the lease.  Returns 0 when the client started; the wait call
 * returns 0 when a lease arrived and fills the plain-C strings. */
struct lkpi_net_lease {
	char ip[16];
	char mask[16];
	char gw[16];
};

int lkpi_net_dhcp_start(void);
int lkpi_net_dhcp_wait(struct lkpi_net_lease *lease, unsigned int timeout_ms);

/* Change the live netif's address after a failed DHCP attempt.  The caller
 * uses this only as a labelled bench fallback. */
int lkpi_net_set_static(const char *ip, const char *mask, const char *gw);

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
	/* The frame classes of the frames the egress owns (see lkpinetif_seam.h):
	 * an AP's broadcast ARP request and the board's reply are what a
	 * "the AP cannot reach the station" verdict is about. */
	unsigned long rx_arp_req;
	unsigned long rx_arp_rep;
	unsigned long rx_ip;
	unsigned long rx_other;
	unsigned long rx_other_n;
	unsigned long rx_other_len[4];
	unsigned long rx_other_proto[4];
	unsigned char rx_other_head[4][16];
	unsigned long rx_no_netif;
	unsigned long rx_input_fail;
	unsigned long rx_alloc_fail;
	unsigned long rx_post_fail;
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
	unsigned offered;	/* sender-side bytes the phase tried to send */
	unsigned tx_err;	/* UDP sends the stack refused */
	unsigned rx_drop;	/* UDP datagrams the sink refused */
	unsigned first_ms;	/* first/last datagram inside the window */
	unsigned last_ms;
	unsigned long beat;	/* tcpip mailbox heartbeats at the end */
	unsigned stall_ms;	/* longest span with the heartbeat static */
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
	/* The UDP/IP drops that are otherwise silent: a frame counted by the
	 * adapter but never delivered to a UDP pcb (checksum/length) shows up
	 * in udp_in_err, not in any netif counter. */
	unsigned long udp_in;
	unsigned long udp_in_err;
	unsigned long udp_noport;
	unsigned long udp_out;
	unsigned long ip_drop;
	unsigned long ip_chkerr;
	unsigned long link_drop;
};

void lkpi_net_stack_diag(struct lkpi_net_stack_diag *out);

/* The stack's core-lock witness, for the phase that stalls: a bounded take
 * can say who held the lock and for how long, and the mailbox heartbeat says
 * whether the TCP/IP thread is still turning. */
int lkpi_net_server_ready(void);
const char *lkpi_net_lock_owner(void);
unsigned long lkpi_net_lock_held_ms(void);
unsigned long lkpi_net_tcpip_beat(void);

/* --- the monitor sink (2026-11-12, docs/HANDOFF-AC7260-MONITOR.md P3) -----
 *
 * mac80211 hands a monitor interface's frames (radiotap + 802.11, FCS already
 * stripped by ieee80211_rx_monitor()) to the same seam as data: netif_rx().
 * For a data interface that is lwIP; for a monitor it is not, because the
 * frame has no ethernet header and no stack consumer.  The sink is therefore
 * a branch netif_rx() takes *before* the egress seam, keyed on the interface
 * type mac80211 sets for a monitor (ARPHRD_IEEE80211_RADIOTAP), and it owns
 * the skb: the frame never reaches lkpi_netdev_rx().
 *
 * The sink is a compile-in registry, not a Kconfig symbol: it costs a branch
 * and a 384-byte ring, and an image without a monitor case leaves it disabled
 * (enable defaults to 0), so the ordinary data path is unchanged.
 *
 * The keep ring is the first frames' evidence: len, radiotap length, the
 * 802.11 frame-control word and the first 64 bytes, which is what tells the
 * case whether the radiotap it built is the one GT built.
 */
#define LKPI_NET_MON_KEEP		4
#define LKPI_NET_MON_FREQS		4
#define LKPI_NET_MON_KEEP_BYTES		64

struct lkpi_net_mon_frame {
	unsigned int	len;		/* skb length as delivered */
	unsigned int	it_len;		/* radiotap header length */
	unsigned int	fc;		/* 802.11 frame control (LE u16) */
	unsigned char	head[LKPI_NET_MON_KEEP_BYTES];
};

struct lkpi_net_mon_diag {
	unsigned long	frames;		/* frames taken by the sink */
	unsigned long	bytes;
	unsigned long	beacons;
	unsigned long	probes;		/* probe requests + responses */
	unsigned long	probe_resp;
	unsigned long	auth;
	unsigned long	assoc;		/* assoc/reassoc request+response */
	unsigned long	data;
	unsigned long	ctrl;
	unsigned long	other;
	unsigned long	bad_radiotap;	/* it_len < 8 or > skb->len */
	unsigned long	too_short;	/* no room for a frame-control word */
	unsigned int	it_len_min;
	unsigned int	it_len_max;
	/* The channel each frame's radiotap names, as a small histogram: a
	 * monitor opened before its channel is set receives on the wiphy's
	 * default channel for a few frames (the 2026-11-12 arm saw 2412 before
	 * the switch to 2432), so "the frequency" is the dominant one and the
	 * minority is evidence, not noise to hide. */
	struct {
		unsigned int	freq;
		unsigned long	count;
	} freq[LKPI_NET_MON_FREQS];
	unsigned int	freq_mhz;	/* the dominant frequency */
	unsigned int	kept;		/* entries filled in keep[] */
	struct lkpi_net_mon_frame keep[LKPI_NET_MON_KEEP];
};

/* Zero the counters and the ring.  Called before the interface is opened. */
void lkpi_net_monitor_reset(void);

/* 1 takes monitor-typed frames at netif_rx(); 0 leaves them to the egress
 * seam exactly as before.  Returns the previous state. */
int lkpi_net_monitor_enable(int enable);

int lkpi_net_monitor_enabled(void);

void lkpi_net_monitor_diag(struct lkpi_net_mon_diag *out);

#endif /* LINUX80211_NET_H */
