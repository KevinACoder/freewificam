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
/* 256 buffers: the RX ring depth (128) plus an in-flight TCP burst must fit
 * without pool exhaustion - a 40-deep pool dropped 13.5% of ACKs in the
 * earlier gigabit baseline. */
#define PBUF_POOL_SIZE                  256
/* One full 1518-byte frame plus headroom in a single pool buffer, so a
 * maximum-frame receive never needs a chain. */
#define PBUF_POOL_BUFSIZE               1600
/* One pbuf per transmitted frame: the driver's transmit path is written
 * against a single-buffer frame. */
#define LWIP_NETIF_TX_SINGLE_PBUF       1

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
#define DEFAULT_THREAD_STACKSIZE        2048
#define DEFAULT_THREAD_PRIO             3

/* --- core locking ----------------------------------------------------------- */

#define LWIP_TCPIP_CORE_LOCKING         1
/* LWIP_TCPIP_CORE_LOCKING_INPUT stays at its default (off): input runs on
 * the TCP/IP thread, fed through the mailbox by the driver. */

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
 * which fits the 16-bit header field; the receive window itself is 256 KB,
 * i.e. >1 Gbit/s of headroom at the measured RTT. */
#define LWIP_WND_SCALE                  1
#define TCP_RCV_SCALE                   3
#define TCP_WND                         262144
#define TCP_SND_BUF                     262144
/* Floor is 4 * TCP_SND_BUF / TCP_MSS = 719. */
#define TCP_SND_QUEUELEN                768
/* The opt.h default (TCP_SND_BUF / 2) trips the u16 guard sanity check;
 * 32 KB keeps the writer awake far from it. */
#define TCP_SNDLOWAT                    32768
#define TCP_QUEUE_OOSEQ                 1
#define MEMP_NUM_TCP_SEG                768
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

/* --- debugging -------------------------------------------------------------- */

/* Stays off: every diagnostic line would go out the 115200 console, which
 * drags the network path to a halt. Enable per file while debugging. */
#define LWIP_DEBUG                      0
/* Protocol counters on: they are the "did the frame leave / did a reply come
 * back" evidence the wired-line bring-up reads out (gmac net), and cost one
 * increment per event. */
#define LWIP_STATS                      1
#define LWIP_STATS_DISPLAY              0

#endif /* FREEWEB_LWIPOPTS_H */
