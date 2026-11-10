/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/bitmap.h - the include-by-name face for the bitmap vocabulary.
 *
 * The pinned header (and the find.h it pulls) is one of the layers this port
 * already carries in the contract header: bitmap_zero/fill/copy/weight/or/
 * empty/subset, find_first_bit/find_next_bit/find_first_zero_bit/
 * find_next_zero_bit/find_last_bit, for_each_set_bit and BITS_TO_LONGS are all
 * stated once in components/liblinux80211/linuxkpi_core.h.  This face exists
 * because the imported stack includes it by name; a second implementation here
 * would be the duplicate mechanism the project forbids.
 */
#ifndef __LINUX_BITMAP_H
#define __LINUX_BITMAP_H

#include "../../linuxkpi_core.h"

#endif /* __LINUX_BITMAP_H */
