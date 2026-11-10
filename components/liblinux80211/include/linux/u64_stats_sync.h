/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/u64_stats_sync.h - the include-by-name face for the 64-bit statistics
 * word.
 *
 * The pinned header wraps the word in a seqcount on 32-bit targets and is a
 * no-op on 64-bit ones.  Both targets of this project are LP64, so the no-op
 * arm is the port's truth and the definitions already live once in the contract
 * header (u64_stats_t, struct u64_stats_sync, u64_stats_update_begin/end,
 * u64_stats_fetch_begin/retry, u64_stats_add/inc/read).  A second statement here
 * would be the duplicate mechanism the project forbids; this header exists
 * because the imported stack includes it by name.
 */
#ifndef _LINUXKPI_LINUX_U64_STATS_SYNC_H_
#define _LINUXKPI_LINUX_U64_STATS_SYNC_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_U64_STATS_SYNC_H_ */
