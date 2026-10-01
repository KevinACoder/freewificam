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
/* >= TCP_SND_QUEUELEN, per lwIP's own sizing rule. */
#define MEMP_NUM_TCP_SEG                128
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_SYS_TIMEOUT            16
#define MEMP_NUM_FRAG_PBUF              16
/* D54: 16 dropped 1799 messages over a 600s downlink once a driver worker
 * outranked tcpip; 64 is the number the prior workspace converged on. */
#define MEMP_NUM_TCPIP_MSG_API          16
#define MEMP_NUM_TCPIP_MSG_INPKT        64

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

/* --- threads and mailboxes (D54 ladder; sys_arch band_to_cmsis: band*8) ------- */

#define TCPIP_THREAD_NAME               "tcpip"
#define TCPIP_THREAD_STACKSIZE          8192
/* Band 4 (osPriorityAboveNormal): above its feeders, below the shell. */
#define TCPIP_THREAD_PRIO               4
#define TCPIP_MBOX_SIZE                 64
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
