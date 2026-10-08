/*
 * @file    net/if.h
 * @brief   Minimal net/if.h for the wpa world: the toolchain libc has none and
 *          a real one would drag the BSD socket world in with it.
 *
 * @design shadow header (DESIGN.md section 3.2, include order): the shim
 *         directory comes first on the wpa world's include path, so this
 *         definition fills the name the vendored includes.h expects.
 */

#ifndef _WPA_SHIM_NET_IF_H_
#define _WPA_SHIM_NET_IF_H_

#define IFNAMSIZ 16

#endif /* _WPA_SHIM_NET_IF_H_ */
