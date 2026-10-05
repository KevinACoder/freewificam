/*
 * @file
 * @brief interface types: empty by design.
 *
 * The imported wifi drivers include <net/if_types.h> by name, so the
 * shadow has to exist. None of this port's compiled units uses the
 * IFT_* vocabulary (the constants are for interface-type reporting,
 * which the scan-only scope does not do), so nothing is defined here -
 * and the names stay out of the tree, where the cleanroom scan's FT_
 * pattern would have to be argued about for no benefit.
 */

#ifndef _COMPAT_NET_IF_TYPES_H_
#define _COMPAT_NET_IF_TYPES_H_

#endif /* _COMPAT_NET_IF_TYPES_H_ */