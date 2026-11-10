/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   host_stubs.c
 * @brief  The two symbols the host tier owes that a target image answers from
 *         its own layers: the CMSIS OS-tick API (the board implements it over
 *         the SoC counter; the host uses CLOCK_MONOTONIC) and mac80211's
 *         ieee80211_hdrlen() (the stack owns the body; the bench never calls it,
 *         but skbuff_pool.c references it from the network-header derivation).
 */
#include <stdint.h>

#include "os/cmsis2/cmsis2.h"
#include "clock.h"

uint32_t OS_Tick_GetCount(void)
{
	return (uint32_t)(rtos_clock_us() / 1000u);
}

uint32_t OS_Tick_GetOverflow(void)
{
	return 1u;
}

uint32_t OS_Tick_GetInterval(void)
{
	return 1u;
}

/* [LNX] include/net/cfg80211.h:7677: the interface's frame-control -> header
 * length mapping.  The stack carries the real body; the bench needs the symbol
 * only because the sk_buff face's network-header derivation lives in the same
 * object.  This is the interface's arithmetic for the three frame classes. */
unsigned int ieee80211_hdrlen(uint16_t fc)
{
	unsigned int len = 24u;	/* 802.11 header base */
	unsigned int type = (fc >> 2) & 0x3u;
	unsigned int subtype = (fc >> 4) & 0xfu;

	if (type == 2u) {			/* data */
		if ((fc & 0x0300u) == 0x0300u)	/* ToDS + FromDS */
			len += 6u;		/* addr4 */
		if (subtype & 0x8u)		/* QoS */
			len += 2u;
		if ((fc & 0x8000u) != 0u)	/* order: HT control */
			len += 4u;
	} else if (type == 0u && (subtype == 8u || subtype == 9u)) {
		len = 24u;			/* beacon/probe response */
	}
	return len;
}
