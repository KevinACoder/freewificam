/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_util.c
 * @brief   The small library bodies the compiled set names: character classes,
 *          gcd, the big-endian CRC, the scatterlist initialiser and the page
 *          projection.
 *
 * @design these are the kernel's own library entries, not part of the wireless
 *         models.  Each body is the interface's behaviour; where this port's
 *         model makes the answer trivial (one page model, no scatter-gather
 *         hardware) the note says so.
 * @date    10.11.2026
 */

#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/ctype.h>
#include <linux/crc32.h>
#include <linux/gcd.h>
#include <linux/mm.h>
#include <linux/hrtimer_types.h>
#include <linux/hrtimer.h>
#include <linux/scatterlist.h>
#include <net/inet_ecn.h>

/* [LNX] lib/ctype.c: the classification table.  The masks are the face's; the
 * table is built from the C locale's character classes. */
const unsigned char _ctype[256] = {
	/* 0x00 */ _C, _C, _C, _C, _C, _C, _C, _C,
	_C, _C | _S, _C | _S, _C | _S, _C | _S, _C | _S, _C, _C,
	/* 0x10 */ _C, _C, _C, _C, _C, _C, _C, _C,
	_C, _C, _C, _C, _C, _C, _C, _C,
	/* 0x20 */ _S | _SP, _P, _P, _P, _P, _P, _P, _P,
	_P, _P, _P, _P, _P, _P, _P, _P,
	/* 0x30 */ _D | _X, _D | _X, _D | _X, _D | _X, _D | _X, _D | _X, _D | _X, _D | _X,
	_D | _X, _D | _X, _P, _P, _P, _P, _P, _P,
	/* 0x40 */ _P, _U | _X, _U | _X, _U | _X, _U | _X, _U | _X, _U | _X, _U,
	_U, _U, _U, _U, _U, _U, _U, _U,
	/* 0x50 */ _U, _U, _U, _U, _U, _U, _U, _U,
	_U, _U, _U, _P, _P, _P, _P, _P,
	/* 0x60 */ _P, _L | _X, _L | _X, _L | _X, _L | _X, _L | _X, _L | _X, _L,
	_L, _L, _L, _L, _L, _L, _L, _L,
	/* 0x70 */ _L, _L, _L, _L, _L, _L, _L, _L,
	_L, _L, _L, _P, _P, _P, _P, _C,
	/* 0x80..0xff: not ASCII */ 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
};

/* [LNX] lib/gcd.c: Euclid. */
unsigned long
gcd(unsigned long a, unsigned long b)
{
	while (b != 0) {
		unsigned long t = a % b;

		a = b;
		b = t;
	}
	return a;
}

/* [LNX] lib/crc32.c: the big-endian (reflected=false) CRC-32, bitwise.  The
 * imported set uses it for the firmware TLV checksum, once per image. */
u32
crc32_be(u32 crc, const void *buf, size_t len)
{
	const unsigned char *p = buf;
	size_t i;
	int bit;

	for (i = 0; i < len; i++) {
		crc ^= (u32)p[i] << 24;
		for (bit = 0; bit < 8; bit++)
			crc = (crc & 0x80000000U) ? (crc << 1) ^ 0x04c11db7U
						  : (crc << 1);
	}
	return crc;
}

/* [LNX] include/linux/mm.h: this port's page handle IS the buffer address
 * (include/linux/mm.h's note), so the head-page projection is the cast. */
struct page *
virt_to_head_page(const void *x)
{
	return (struct page *)(uintptr_t)x;
}

/* [LNX] include/net/inet_ecn.h: mark the packet CE.  This port's skb has no
 * DS field to rewrite (skb_get_dsfield is the constant-0 answer recorded in
 * docs/LINUXKPI-GAP.md §2.2), so the honest answer is "not set", which is what
 * the interface returns when the packet cannot carry the mark. */
bool
INET_ECN_set_ce(struct sk_buff *skb)
{
	(void)skb;
	return false;
}

/* ------------------------------------------------------------------ */
/* the hrtimer face, in its no-clock arm                               */
/* ------------------------------------------------------------------ */

/* [LNX] kernel/time/hrtimer.c: this port has no high-resolution timer wheel.
 * A timer is set up (storage is opaque and caller-owned) but never fires; the
 * interface's callers on the compiled paths (cfg80211's delayed work helpers)
 * do not arm one before C4.  A caller that arms one and waits for it is the
 * signal that this gap has become load-bearing. */
void
hrtimer_setup(struct hrtimer *timer,
	      enum hrtimer_restart (*function)(struct hrtimer *),
	      clockid_t clock_id, enum hrtimer_mode mode)
{
	(void)timer;
	(void)function;
	(void)clock_id;
	(void)mode;
}

void
hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim, u64 delta_ns,
		       const enum hrtimer_mode mode)
{
	(void)timer;
	(void)tim;
	(void)delta_ns;
	(void)mode;
}

int
hrtimer_cancel(struct hrtimer *timer)
{
	(void)timer;
	return 0;
}

bool
hrtimer_is_queued(struct hrtimer *timer)
{
	(void)timer;
	return false;
}

/* [LNX] lib/scatterlist.c: start an empty list with its last entry marked the
 * end.  This lives here rather than in the PCI engine because the stack's
 * crypto paths name it in images that carry no PCI engine (C2). */
void
sg_init_table(struct scatterlist *sgl, unsigned int nents)
{
	if (sgl == NULL || nents == 0)
		return;
	memset(sgl, 0, sizeof(*sgl) * nents);
	sg_mark_end(&sgl[nents - 1]);
}
