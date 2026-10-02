/*
 * @file   lwipopts.h
 * @brief  lwIP configuration for the freewebcam carrier (phase 2, NO_SYS=0).
 *
 * This file is the whole of the stack's configuration: lwIP reaches it from
 * lwip/opt.h in every translation unit (it must sit ahead of the vendored
 * tree on the include path - Makefile LWIP_CFLAGS). The phase-1 values follow
 * specs/lwip-adapt/SPEC.md §3.2.2 (阶段一 column), carried over from the
 * prior workspace's converged numbers:
 *
 *  - NO_SYS=0 (P7-2a, spec §3.2.2 阶段二列): lwIP runs its own tcpip thread
 *    on the cmsis2_threadx shim (sys_arch over osSemaphore/osMessageQueue -
 *    the shim's S3 §3.5.2 扩面义务, lwip is the first consumer). The netif
 *    bridge shape is unchanged; input is tcpip_input and the driver feeds
 *    it from a dedicated RX thread (ISR -> semaphore -> ethnetif_poll).
 *
 *  - MEM_LIBC_MALLOC=0: lwIP keeps its own static heap (MEM_SIZE) and its
 *    own static pools. This image is freestanding (-nostdlib, no libc
 *    malloc) and a static pool makes the stack's memory use visible at link
 *    time instead of showing up as runtime heap exhaustion.
 *
 *  - IPv6, IGMP, AUTOIP and DNS are off; the sequential API (netconn +
 *    sockets) does not exist in phase 1 (api/ files are not compiled - the
 *    P7-1a acceptance nm-checks for their absence). The minimal smoke set
 *    is IPv4 + ARP + ICMP + UDP + TCP; DHCP stays compilable (LWIP_DHCP=1)
 *    but nothing calls dhcp_start in phase 1 (static IP is the acceptance
 *    path).
 *
 *  - LWIP_DEBUG stays off: every diag call would go out the console at
 *    115200 baud, which drags the network path to a halt (prior-work G18).
 *    Enable per-file with LWIP_DBG_ON while debugging, not here.
 *
 * @author zhugengyu
 * @date   02.10.2026
 */

#ifndef FREEWEBCAM_LWIPOPTS_H
#define FREEWEBCAM_LWIPOPTS_H

/* --- OS integration (spec §3.2.2, phase-1 column) -------------------------- */

#define NO_SYS                          0
#define SYS_LIGHTWEIGHT_PROT            1
/* The sequential API: netconn is the layer sockets sits on - the lwipopts
 * rows and the api/ compile set move together (P7-2a). */
#define LWIP_NETCONN                    1
#define LWIP_SOCKET                     1
#define LWIP_NETIF_API                  1
/* S7b/netutils 修订义务 (specs/netutils-adapt §3.5 表, N1 判据 4): the raw
 * API carries the tools' SOCK_RAW/ICMP ping (netconn RAW path is gated on
 * LWIP_RAW, opt.h default 0); SO_RCVTIMEO covers ping/tftp timeouts. raw.c
 * and netdb.c/netbuf.c are already in the compile set. */
#define LWIP_RAW                        1
#define LWIP_SO_RCVTIMEO                1
/* One netconn per socket user: ping + tftp client/server + iperf3 并发余量
 * (the prior workspace's converged value). */
#define MEMP_NUM_NETCONN                10
/* err.c's err-to-errno table (api/ world) needs constants, and the errno
 * variable itself is defined once in the adapter (lwip_diag.c). */
#define LWIP_PROVIDE_ERRNO              1
/* struct timeval comes from the toolchain's <sys/time.h> (arch/cc.h pulls
 * it for every lwIP TU); lwIP must not define its own private copy or the
 * two clash. The socket tools (netutils line) include the header directly. */
#define LWIP_TIMEVAL_PRIVATE            0

/* --- memory ---------------------------------------------------------------- */

#define MEM_LIBC_MALLOC                 0
#define MEMP_MEM_MALLOC                 0
/* N7 gigabit tier (specs/lwip-adapt + runs/20261002-lwip-gigabit): the
 * reference config that measures 921/841 Mbit/s single-core (Phytium
 * FreeRTOS SDK lwip_iperf) carries a 1 MB stack heap; 128 KB exhausted the
 * ram_heap under a 128-deep pbuf burst. */
#define MEM_SIZE                        (1024 * 1024)
/* 8, not lwIP's 4-byte default: aarch64 pointers are 8 bytes and mem_malloc
 * must hand back 8-aligned blocks or every pbuf's pointer fields are asked to
 * be read unaligned. */
#define MEM_ALIGNMENT                   8
#define MEMP_OVERFLOW_CHECK             0
#define MEMP_SANITY_CHECK               0
/* One 1518-byte frame per pool buffer with headroom: a max-frame single-pbuf
 * receive must never need a chain. 256 buffers ≈ 400 KB: the RX ring depth
 * (128, N7) plus an in-flight TCP burst must fit without pool exhaustion -
 * the R0 baseline dropped 13.5% of ACKs on a 40-deep pool. */
#define PBUF_POOL_SIZE                  256
#define PBUF_POOL_BUFSIZE               1600
#define LWIP_NETIF_TX_SINGLE_PBUF       1

/* --- protocols -------------------------------------------------------------- */

#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ETHERNET                   1
#define LWIP_ARP                        1
#define LWIP_ICMP                       1
/* igmp.c / autoip.c are not compiled (the pool rows below would be dead
 * weight); acd.c IS compiled - LWIP_ACD follows LWIP_DHCP by default and
 * etharp.c then calls into it. */
#define LWIP_IGMP                       0
/* dns.c rides in with the api/ set (netdb.c is compiled inside
 * LWIP_DNS && LWIP_SOCKET); no DNS server is configured - the static-IP
 * smoke never resolves a name. */
#define LWIP_DNS                        1
#define LWIP_DHCP                       1
#define LWIP_AUTOIP                     0
#define LWIP_UDP                        1
#define LWIP_TCP                        1

/* --- pool sizes (prior-work converged values) ------------------------------- */

#define MEMP_NUM_TCP_PCB                16
#define MEMP_NUM_TCP_PCB_LISTEN         4
/* >= TCP_SND_QUEUELEN, per lwIP's own sizing rule (768 now). */
#define MEMP_NUM_TCP_SEG                768
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_SYS_TIMEOUT            16
#define MEMP_NUM_FRAG_PBUF              16
/* D54: 16 dropped 1799 messages over a 600s downlink once a driver worker
 * outranked tcpip; 64 is the number the prior workspace converged on. */
#define MEMP_NUM_TCPIP_MSG_API          16
/* Matches TCPIP_MBOX_SIZE: an mbox slot without its msg-pool element drops
 * the frame anyway, so sizing only the mbox is dead weight. */
#define MEMP_NUM_TCPIP_MSG_INPKT        128

/* --- TCP (N7 gigabit tier; reference = Phytium SDK 60000/60000, 921 Mbit/s) --- */

#define TCP_MSS                         1460
/* N7 R6 (2026-10-02): window scaling on. Every historical number is
 * buffer/RTT arithmetic with the old 60000-byte buffers - R4 413M
 * reverse (60000B/1.16ms), 496M forward (60000B/0.97ms), R5b 330-341M
 * (60000B/~1.4ms) - so the buffers were the ceiling, not the path: a
 * UDP flood of 630 Mbit/s was served at ~42k fps/508 Mbit/s (with MTL
 * drops and 41k ICMP replies/s on top). Real window 256 KB (announced
 * value = TCP_WND >> TCP_RCV_SCALE = 32768, fits the 16-bit header):
 * >1.4 Gbit/s of window headroom at the measured RTT, so the remaining
 * limit becomes the per-frame path itself. */
#define TCP_WND                         262144
#define TCP_SND_BUF                     262144
/* opt.h floor is 4 * TCP_SND_BUF / TCP_MSS: 4 * 262144 / 1460 = 719. */
#define TCP_SND_QUEUELEN                768
/* The opt.h default (TCP_SND_BUF/2 = 128K) trips lwIP's u16-guard sanity
 * check (must stay >= 4*MSS below 0xFFFF); 32K keeps the writer awake
 * without sitting anywhere near the guard. */
#define TCP_SNDLOWAT                    32768
/* OoSeq reassembly: the raised window admits more out-of-order segments;
 * the TCP_SEG pool above (256 rows) covers them. */
#define TCP_QUEUE_OOSEQ                 1
#define LWIP_WND_SCALE                  1
/* Announce shift for TCP_WND: 262144 >> 3 = 32768. */
#define TCP_RCV_SCALE                   3
/* Explicit (opt.h default is already 1, but the api-call-into-core-direct
 * shape is a load-bearing assumption of the N7 throughput design: socket
 * sends must not pay a mailbox round trip each). */
#define LWIP_TCPIP_CORE_LOCKING         1
/* LWIP_TCPIP_CORE_LOCKING_INPUT stays 0 (board-measured N7 R4 A/B: 351M vs
 * 406M reverse with it on - running the input processing inline in the
 * band-3 gmacrx thread loses to posting to the band-5 tcpip thread, which
 * then drains the mbox while gmacrx keeps dequeuing; Phytium runs it off
 * too). */

/* struct pbuf_custom must exist for the zero-copy RX wrappers (its opt.h
 * default only turns on when fragmentation needs it; IP_FRAG is on but
 * TX_SINGLE_PBUF=1 masks that path). */
#define LWIP_SUPPORT_CUSTOM_PBUF        1

/* --- RX checksum offload (N7 R3; must flip with the driver, never alone) -------
 *
 * With CONFIG_GMAC_RX_HWCSUM the GMAC IPC block validates the L3/L4
 * checksum of every IP frame and dwc_eqos drops RDES1-flagged bad frames
 * before they reach the stack - the software verification below is then a
 * redundant full-payload pass per packet (the dominant per-frame CPU cost
 * the A55 pays that the Phytium reference core simply brute-forced). ICMP
 * keeps its software check: rare traffic, and its checksum has no pseudo
 * header (the IPC payload check still flags bad ICMP frames). */
#if defined(CONFIG_GMAC_RX_HWCSUM) && CONFIG_GMAC_RX_HWCSUM
#define CHECKSUM_CHECK_IP               0
#define CHECKSUM_CHECK_TCP              0
#define CHECKSUM_CHECK_UDP              0
#endif

/* --- threads and mailboxes (D54 ladder; sys_arch band_to_cmsis: band*8) ------- */

#define TCPIP_THREAD_NAME               "tcpip"
#define TCPIP_THREAD_STACKSIZE          8192
/* Band 5 (osPriorityHigh): N7 experiment - with band 4 the whole RX path
 * capped at ~1250 frames/s (0.8 ms/frame) regardless of protocol (TCP
 * up/down, UDP down, lwiperf all pinned at the same rate), which smells like
 * the tcpip thread being starved rather than doing real work. Shell stays
 * above at the same ladder step in theory but it parks forever. */
#define TCPIP_THREAD_PRIO               5
#define TCPIP_MBOX_SIZE                 128
#define DEFAULT_THREAD_STACKSIZE        2048
#define DEFAULT_THREAD_PRIO             3
#define DEFAULT_RAW_RECVMBOX_SIZE       8
#define DEFAULT_UDP_RECVMBOX_SIZE       8
#define DEFAULT_TCP_RECVMBOX_SIZE       8
#define DEFAULT_ACCEPTMBOX_SIZE         4

/* --- netif -------------------------------------------------------------------- */

#define LWIP_SINGLE_NETIF               0
#define LWIP_HAVE_LOOPIF                0
#define LWIP_NETIF_LOOPBACK             0
/* the bridge (P7-1c) installs a status callback per netif: the first netif
 * that gains an address becomes the default route */
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        0

/* Platform facts (byte order, rand, diag hooks, which libc headers exist)
 * live in arch/cc.h, which is where lwIP looks for them. */

/* --- debugging ------------------------------------------------------------------ */

#define LWIP_DEBUG                      0
#define LWIP_STATS                      0
#define LWIP_STATS_DISPLAY              0

#endif /* FREEWEBCAM_LWIPOPTS_H */
