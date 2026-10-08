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
#define MEM_SIZE                        (256 * 1024)
/* AArch64 pointers are 8 bytes: aligned blocks keep every pointer field of
 * every pbuf naturally aligned. */
#define MEM_ALIGNMENT                   8
#define PBUF_POOL_SIZE                  128
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

/* --- debugging -------------------------------------------------------------- */

/* Stays off: every diagnostic line would go out the 115200 console, which
 * drags the network path to a halt. Enable per file while debugging. */
#define LWIP_DEBUG                      0
#define LWIP_STATS                      0
#define LWIP_STATS_DISPLAY              0

#endif /* FREEWEB_LWIPOPTS_H */
