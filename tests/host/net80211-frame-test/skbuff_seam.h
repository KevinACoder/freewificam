/*
 * @file    skbuff_seam.h
 * @brief   The driver-side sk_buff seam of the mock: a host backend for the
 *          in-repo contract header components/linuxkpi/skbuff/skbuff_pool.h.
 *
 * The seam is where the memory subsystem will be measured: the driver
 * substitute allocates one sk_buff per received frame and one per captured
 * transmit frame, and the flow asserts the two pools pair out. The header of
 * the seam is the repository's contract header (the Linux-shaped shim
 * contract), so the assertions here keep their meaning when M1 replaces this
 * backend with the real pool.
 *
 * @design adapter - ref linuxkpi:skbuff/skbuff_pool.h:102
 */

#ifndef VWIFI_SKBUFF_SEAM_H
#define VWIFI_SKBUFF_SEAM_H

#include <stddef.h>
#include <stdint.h>

struct sk_buff;
struct lkpi_skb_pool_stats;

/* One allocation of size bytes with NET_SKB_PAD headroom; NULL on failure
 * (the named alloc_fail counter moves). */
struct sk_buff *wlan_seam_skb_alloc(unsigned int size);

/* Release one allocation. A second release is named and skipped, and so is a
 * pointer that never came from this seam. */
void wlan_seam_skb_free(struct sk_buff *skb);

/* The seam's counters, copied out. */
void wlan_seam_stats(struct lkpi_skb_pool_stats *out);

/* The misuse probe: a double free and a write through a released slot are
 * named by the counters and do not corrupt anything. Returns 0 when both
 * behaved as declared. */
int wlan_seam_misuse_probe(void);

/* Reset the counters (used between the flow and the misuse probe). */
void wlan_seam_reset_stats(void);

/* The data area of an allocation, and its usable length. */
uint8_t *wlan_seam_data(struct sk_buff *skb);
unsigned int wlan_seam_capacity(void);

#endif /* VWIFI_SKBUFF_SEAM_H */
