/* SPDX-License-Identifier: GPL-2.0 */
/*
 * net/arp.h - include-by-name face.
 *
 * cfg80211's scan.c includes this header, and names no symbol from it: the
 * port runs no ARP/neighbouring layer, and the neighbour record the pinned
 * header reaches (neighbour.h, hash.h, if_arp.h) has no counterpart here.
 * Keeping the include resolvable is the whole contract; the first compiled
 * consumer that names a declaration gets it added here - not the pinned tree
 * wholesale.
 */
#ifndef _ARP_H
#define _ARP_H

#endif /* _ARP_H */
