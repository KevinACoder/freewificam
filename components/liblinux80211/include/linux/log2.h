/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/log2.h
 * @brief   The include Linux code names for ilog2 and the power-of-two
 *          helpers; the bodies are the contract header's.
 *
 * @design declaration-only compatibility face.  The interface's log2.h owns
 *         ilog2(), is_power_of_2(), roundup_pow_of_two() and their unsigned
 *         variants.  This component's contract header already states the exact
 *         pair its compiled consumers use (linuxkpi_core.h, the bit arithmetic
 *         section), including the constant-folding arm the interface selects
 *         with __builtin_constant_p.  The real net/mac80211.h and
 *         net/wireless.h include this header by name; restating the helpers
 *         here made a redefinition, so the contract header answers and this
 *         header is the include-path spelling.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_LOG2_H_
#define _LINUXKPI_LINUX_LOG2_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_LOG2_H_ */
