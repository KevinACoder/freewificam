/*
 * @file    lwipopts.h
 * @brief   The stack configuration of this project's lwIP port.
 *
 * lwIP reaches this file from lwip/opt.h in every translation unit; it must
 * sit ahead of the vendored tree on the include path (the port's include
 * directory is the first -I). Every option the stack offers is set here or
 * left at its upstream default - no other file carries configuration.
 *
 * Tiering: this is the bring-up tier (upstream defaults plus minimal
 * coverage). The measurement tier - window scaling, the large TCP windows,
 * the raised pool counts - is the converged set of the earlier gigabit work
 * and is applied when the data-plane milestone measures throughput. Keeping
 * the two apart is deliberate: a bring-up image is judged on being alive and
 * correct, not on numbers, and default windows keep the first measurements
 * free of tuning artifacts.
 *
 * Memory: the static-pool plan. The image is freestanding (-nostdlib, no
 * libc malloc), so the stack keeps its own heap (MEM_SIZE) and its own
 * pools; their sizes are visible at link time instead of appearing as a
 * runtime heap exhaustion. The custom-allocator interface this release adds
 * is the recorded switch-over path for the later unified allocator, and
 * switching is a change of three option macros.
 *
 * @design configuration facade: the single point of stack configuration,
 * reached by the stack's own include machinery (the port's include directory
 * shadows src/include/), overriding the "#if !defined X" default table of
 * third-party/lwip/src/include/lwip/opt.h:88-90 rather than editing it.
 */

#ifndef FREEWEB_LWIPOPTS_H
#define FREEWEB_LWIPOPTS_H

/* --- OS integration --------------------------------------------------------- */

#define NO_SYS                          0
#define SYS_LIGHTWEIGHT_PROT            1

/* The sequential and socket APIs: netconn is the layer sockets sits on. The
 * api/ compile set and these rows move together. */
#define LWIP_NETCONN                    1
#define LWIP_SOCKET                     1
#define LWIP_NETIF_API                  1
/* The raw API carries the ICMP ping tool's socket; SO_RCVTIMEO covers its
 * receive timeout. */
#define LWIP_RAW                        1
#define LWIP_SO_RCVTIMEO                1
#define MEMP_NUM_NETCONN                10
/* The errno constants are the stack's own; the variable is defined once in
 * ports/lwip/platform_hooks.c. */
#define LWIP_PROVIDE_ERRNO              1

/* --- memory ----------------------------------------------------------------- */

#define MEM_LIBC_MALLOC                 0
/* The pbuf pools stay static: a bounded, deterministic data path is the
 * point of the static plan on a small target. */
#define MEMP_MEM_MALLOC                 0
/* 1 MB: the converged measurement-tier heap. 128 KB exhausted the static
 * heap under a 128-deep pbuf burst in the earlier gigabit work, and the
 * reference single-core gigabit SDK this tier follows carries 1 MB too. */
#define MEM_SIZE                        (1024 * 1024)
/* AArch64 pointers are 8 bytes: aligned blocks keep every pointer field of
 * every pbuf naturally aligned. */
#define MEM_ALIGNMENT                   8
/* 448 buffers: the RX ring depth (128) plus an in-flight TCP burst must fit
 * without pool exhaustion - a 40-deep pool dropped 13.5% of ACKs in the
 * earlier gigabit baseline. Raised from 256 with the 512 KB window below,
 * because the pool has to be able to hold a whole window of received data
 * (512 KB / 1600 = 328 buffers) on top of the ring and the burst: a pool that
 * cannot hold the window makes the stack advertise one it cannot back. */
#define PBUF_POOL_SIZE                  448
/* One full 1518-byte frame plus headroom in a single pool buffer, so a
 * maximum-frame receive never needs a chain. */
#define PBUF_POOL_BUFSIZE               1600
/* One pbuf per transmitted frame: the driver's transmit path is written
 * against a single-buffer frame. The #ifndef wrapper lets a measurement
 * build flip it to 0 (lever PBREF=1): with the app writing by reference
 * (lwiperf's payload path), tcp_write then takes a PBUF_ROM reference to
 * the caller's data instead of a PBUF_RAM allocation plus copy - the
 * copy and the heap walk leave the per-callback write path. The driver
 * still copies into its own transmit buffers, so the reference lifetime
 * is one call deep. */
#ifndef LWIP_NETIF_TX_SINGLE_PBUF
#define LWIP_NETIF_TX_SINGLE_PBUF       1
#endif

/* --- protocols -------------------------------------------------------------- */

#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ETHERNET                   1
#define LWIP_ARP                        1
#define LWIP_ICMP                       1
#define LWIP_IGMP                       0
#define LWIP_DNS                        0
#define LWIP_DHCP                       1
#define LWIP_AUTOIP                     0
#define LWIP_UDP                        1
#define LWIP_TCP                        1

/* --- threads and mailboxes -------------------------------------------------- */

#define TCPIP_THREAD_NAME               "tcpip"
#define TCPIP_THREAD_STACKSIZE          8192
/* Priority band 5 of 7 (sys_arch maps bands onto the CMSIS-RTOS2 priority
 * ladder): the TCP/IP thread outranks its feeders, nothing starves it. */
#define TCPIP_THREAD_PRIO               5
#define TCPIP_MBOX_SIZE                 128
/* Any other lwIP thread (the contrib apps ask for this constant). It has to
 * carry the same transmit chain the TCP/IP thread does - raw API down through
 * the netif, the bridge's encapsulation and CCMP, and the USB submit - so it
 * gets the same budget: measured, a 2 KiB stack was exhausted by the ping
 * thread's first echo request. */
#define DEFAULT_THREAD_STACKSIZE        8192
#define DEFAULT_THREAD_PRIO             3

/* --- core locking ----------------------------------------------------------- */

/* The #ifndef wrappers are what make these two overridable from a build: every
 * other option in this file is a hard #define, so a -D would lose to it. */
#ifndef LWIP_TCPIP_CORE_LOCKING
#define LWIP_TCPIP_CORE_LOCKING         1
#endif
/* Input on the CALLER's thread under the core lock (2026-10-17 lever): the
 * driver's RX thread runs the stack's input path itself instead of posting one
 * mailbox message per frame and switching to the TCP/IP thread. Off by default:
 * it moves which thread executes the input path, so it is measured, not
 * assumed. */
#ifndef LWIP_TCPIP_CORE_LOCKING_INPUT
#define LWIP_TCPIP_CORE_LOCKING_INPUT   0
#endif

/* --- loopback --------------------------------------------------------------- */

/* The survive-smoke anchor uses the upstream loopback netif (127.0.0.1)
 * before any real network interface exists. */
#define LWIP_NETIF_LOOPBACK             1
#define LWIP_NETIF_LOOPBACK_MULTITHREADING 1
/* 0 = no queue limit; the queue itself is what the loopback path needs. */
#define LWIP_LOOPBACK_MAX_PBUFS         0

/* --- measurement tier: TCP windows and pools (converged values) --------------
 *
 * These are the numbers the earlier gigabit work measured at: the upstream
 * defaults (4 * MSS) put a ~6 KB ceiling on in-flight data, which caps a
 * single TCP stream at ~45 Mbit/s on this path regardless of the CPU. The
 * tier is a bundle on purpose - it is the configuration the 643/496 Mbit/s
 * board evidence was taken with, so it moves as one. Setting the block to 0
 * restores the upstream defaults (the bring-up tier). */
#define LWIP_PERF_TIER                  1

#if LWIP_PERF_TIER
#define TCP_MSS                         1460
/* Window scaling: announced window = TCP_WND >> TCP_RCV_SCALE = 32768,
 * which fits the 16-bit header field; the receive window itself is 512 KB.
 * The scale moved from 3 to 4 with the window, so the announced field stays
 * at 32768: lwIP refuses a TCP_WND above 0xFFFF << TCP_RCV_SCALE, and 512 KB
 * is just past what scale 3 allows (524280).
 *
 * The window is sized against the RTT measured while the link is loaded, not
 * the idle one. The loaded RTT is ~70-100 ms (the sender's smoothed round trip
 * and the ping running alongside a bulk window agree), because the card holds
 * received frames to aggregate them, and at that RTT a 256 KB window caps a
 * single stream at ~20 Mbit/s - which is where the port sat: a UDP blast
 * reached 63 Mbit/s on the same boot and channel while TCP held 18.4, so the
 * ceiling was the window and not the receive path. 512 KB puts the same
 * arithmetic at ~40-58 Mbit/s, above the target. */
#define LWIP_WND_SCALE                  1
#define TCP_RCV_SCALE                   4
#define TCP_WND                         524288
/* Guarded so the SNDBUF= make lever can win (the queue-walk experiment);
 * unset = the converged default. TCP_SND_QUEUELEN's floor is
 * 4 * TCP_SND_BUF / TCP_MSS. */
#ifndef TCP_SND_BUF
#define TCP_SND_BUF                     524288
#endif
/* Floor is 4 * TCP_SND_BUF / TCP_MSS = 1436 at the default size. */
#ifndef TCP_SND_QUEUELEN
#define TCP_SND_QUEUELEN                1536
#endif
/* The opt.h default (TCP_SND_BUF / 2) trips the u16 guard sanity check;
 * 32 KB keeps the writer awake far from it. */
#define TCP_SNDLOWAT                    32768
#define TCP_QUEUE_OOSEQ                 1
#define MEMP_NUM_TCP_SEG                1536
/* Matches TCPIP_MBOX_SIZE: an mbox slot without its msg-pool element drops
 * the frame anyway, so sizing only the mbox is dead weight. */
#define MEMP_NUM_TCPIP_MSG_INPKT        128
#endif /* LWIP_PERF_TIER */

/* --- RX checksum offload (flips with the driver, never alone) ----------------
 *
 * With CONFIG_GMAC_RX_HWCSUM the GMAC's IPC block validates the L3/L4
 * checksum of every IP frame and the driver drops RDES1-flagged bad frames
 * before they reach the stack, so the software verification below is a
 * redundant full-payload pass per packet. ICMP keeps its software check (its
 * checksum has no pseudo header; the IPC payload check still flags bad ICMP
 * frames). The two switches must move together. */
#if defined(CONFIG_GMAC_RX_HWCSUM) && CONFIG_GMAC_RX_HWCSUM
#define CHECKSUM_CHECK_IP               0
#define CHECKSUM_CHECK_TCP              0
#define CHECKSUM_CHECK_UDP              0
#endif

/* --- TX checksum offload (the other half of the same lockstep) ---------------
 *
 * With CONFIG_GMAC_TX_CSUM the driver writes the IPv4/TCP or IPv4/UDP
 * pseudo-header sum into the checksum field and tags the descriptor CIC=3, so
 * the MAC computes and inserts both checksums (dwc_eqos.c eqos_tx_csum_prepare
 * carries the full contract, TRM Part2 ch.20 TDES3). The stack side must stop
 * generating those checksums on this netif or the MAC would checksum over an
 * already-correct value; lwIP expresses that with the per-netif control block
 * (the GEN switches stop being global and become netif flags), which is why
 * the whole option only exists in builds that also enable the driver side.
 * ICMP keeps its software generation - no pseudo-header in the protocol - so
 * its two bits stay set in the netif flags (ports/lwip/ethnetif.c). */
#if defined(CONFIG_GMAC_TX_CSUM) && CONFIG_GMAC_TX_CSUM
#define LWIP_CHECKSUM_CTRL_PER_NETIF    1
#endif

/* --- debugging -------------------------------------------------------------- */

/* Stays off: every diagnostic line would go out the 115200 console, which
 * drags the network path to a halt. Enable per file while debugging. */
#define LWIP_DEBUG                      0
/* Protocol counters on: they are the "did the frame leave / did a reply come
 * back" evidence the wired-line bring-up reads out (gmac net), and cost one
 * increment per event. */
#define LWIP_STATS                      1
#define LWIP_STATS_DISPLAY              0

/* TCP-gap probe tier (2026-10-19): MIB2's TCP segment counters - out/in and,
 * crucially, fast retransmits (tcp_out.c tcp_rexmit) - are the direct read on
 * whether the 548 Mbit/s ceiling carries a retransmission load. Compiled in
 * only under the instrument lever (CONFIG_GMAC_TCP_PROBE); the increments are
 * one u32 add per segment, but the default image stays byte-for-byte on the
 * shipped profile unless the lever says otherwise (the suite Makefile's
 * TCPPROBE=1). A full RTO retransmit is NOT in MIB2; it rides the probe
 * counter in patches/lwip.
 *
 * LWIP_STATS_LARGE is load-bearing with it: the default STAT_COUNTER is u16,
 * and at 500 Mbit/s the segment counters wrap several times inside one 10 s
 * phase - a wrapped differential is silently wrong (it reads as a small
 * positive delta). u32 counters make the phase diffs honest. */
#if defined(CONFIG_GMAC_TCP_PROBE) && CONFIG_GMAC_TCP_PROBE
#define MIB2_STATS                      1
#define LWIP_STATS_LARGE                1
#endif

/* Fine-grained TCP input-path probe (2026-10-19 round 3, FINE=1 lever):
 * patch 0002 adds LWIP_HOOK_TCP_PROBE at fixed points of tcp_input /
 * tcp_output; here the hook is pointed at the bridge's tick-based pair
 * accumulator (ports/lwip/ethnetif.c), which `gmac tcpfine` reads out. The
 * hook adds a counter read and a dispatch per site (~8 sites per input
 * entry, a few hundred ns each) - measurement-only, the default profile
 * never defines it. */
#if defined(CONFIG_GMAC_TCP_FINE) && CONFIG_GMAC_TCP_FINE
void ethnetif_tcp_fine_mark(unsigned int site);
#define LWIP_HOOK_TCP_PROBE(site) ethnetif_tcp_fine_mark((unsigned int)(site))
#endif

#endif /* FREEWEB_LWIPOPTS_H */
