/*
 * @file   lwipopts.h
 * @brief  lwIP configuration for the freewebcam carrier (phase 1, NO_SYS=1).
 *
 * This file is the whole of the stack's configuration: lwIP reaches it from
 * lwip/opt.h in every translation unit (it must sit ahead of the vendored
 * tree on the include path - Makefile LWIP_CFLAGS). The phase-1 values follow
 * specs/lwip-adapt/SPEC.md §3.2.2 (阶段一 column), carried over from the
 * prior workspace's converged numbers:
 *
 *  - NO_SYS=1: phase 1 runs the polled model on the bare-metal base (S1):
 *    the app main loop feeds the stack (RX drain + sys_check_timeouts) and
 *    no lwIP thread exists. The NO_SYS=0 + sys_arch switch is P7-2a, a
 *    single-variable change (lwipopts rows + api/ compile set + input
 *    function pointer), spec §3.1.
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

#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0
/* The sequential API lands with P7-2a: lwipopts rows and the api/ compile
 * set move together (spec N1/FR-2 - one without the other does not link). */
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0

/* --- memory ---------------------------------------------------------------- */

#define MEM_LIBC_MALLOC                 0
#define MEMP_MEM_MALLOC                 0
#define MEM_SIZE                        131072
/* 8, not lwIP's 4-byte default: aarch64 pointers are 8 bytes and mem_malloc
 * must hand back 8-aligned blocks or every pbuf's pointer fields are asked to
 * be read unaligned. */
#define MEM_ALIGNMENT                   8
#define MEMP_OVERFLOW_CHECK             0
#define MEMP_SANITY_CHECK               0
/* One 1518-byte frame per pool buffer with headroom: a max-frame single-pbuf
 * receive must never need a chain. */
#define PBUF_POOL_SIZE                  40
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
#define LWIP_DNS                        0
#define LWIP_DHCP                       1
#define LWIP_AUTOIP                     0
#define LWIP_UDP                        1
#define LWIP_TCP                        1

/* --- pool sizes (prior-work converged values) ------------------------------- */

#define MEMP_NUM_TCP_PCB                16
#define MEMP_NUM_TCP_PCB_LISTEN         4
/* >= TCP_SND_QUEUELEN, per lwIP's own sizing rule. */
#define MEMP_NUM_TCP_SEG                128
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_SYS_TIMEOUT            16
#define MEMP_NUM_FRAG_PBUF              16

/* --- TCP (prior-work lesson values, D52) ------------------------------------- */

#define TCP_MSS                         1460
/* 8*MSS, not the full 32*MSS window: the wlan line burst-past-FIFO lesson;
 * the sustained-throughput line revisits one variable at a time. */
#define TCP_WND                         (8 * TCP_MSS)
#define TCP_SND_BUF                     (32 * TCP_MSS)
/* Must be >= 4 * TCP_SND_BUF / TCP_MSS: 4 * 46720 / 1460 = 128. */
#define TCP_SND_QUEUELEN                128
#define TCP_QUEUE_OOSEQ                 0
#define LWIP_WND_SCALE                  0

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
