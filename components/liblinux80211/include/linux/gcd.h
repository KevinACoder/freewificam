/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/gcd.h - the greatest-common-divisor helper, used-surface face.
 *
 * The pinned header also declares the efficient-ffs static key, which drags the
 * jump-label layer in.  The key has no consumer in this tree (the only mention
 * was this declaration), and gcd() itself is a plain library function, so the
 * face keeps the function and drops the key.
 */
#ifndef _GCD_H
#define _GCD_H

#include "../../linuxkpi_core.h"

unsigned long gcd(unsigned long a, unsigned long b) __attribute_const__;

#endif /* _GCD_H */
