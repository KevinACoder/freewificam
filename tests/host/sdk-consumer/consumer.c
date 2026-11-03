/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    consumer.c
 * @brief   The smallest L1 consumer: configuration macros plus one log call.
 *
 * This file is compiled by check.sh with exactly one include path, the
 * published `dist/<profile>/include`, and linked with exactly one library
 * path, `dist/<profile>/lib`, through the generated freewificam.pc.  It may
 * not include an internal header or rely on a -D configuration macro.
 *
 * The compile-time assertions prove the generated configuration reached the
 * consumer (the "config" half of the checkpoint); `log_level_get()` proves the
 * archives close over a real log symbol (the "link" half).  If a future change
 * breaks either, this consumer stops building or linking.
 */

#include <freewificam/fw_config.h>
#include <freewificam/fw_log.h>

#ifndef CONFIG_PROFILE_NAME
#error "sdk-consumer: fw_config.h did not provide CONFIG_PROFILE_NAME"
#endif
#ifndef CONFIG_BOARD_NAME
#error "sdk-consumer: fw_config.h did not provide CONFIG_BOARD_NAME"
#endif

_Static_assert(sizeof(CONFIG_PROFILE_NAME) > 1,
	       "sdk-consumer: the profile name is empty");
_Static_assert(sizeof(CONFIG_BOARD_NAME) > 1,
	       "sdk-consumer: the board name is empty");
_Static_assert(CONFIG_HEAP_SIZE > 0,
	       "sdk-consumer: HEAP_SIZE did not reach the consumer");

int main(void)
{
	LOG_ANCHOR("sdk-consumer: board=%s profile=%s heap=%dMiB\n",
		   CONFIG_BOARD_NAME, CONFIG_PROFILE_NAME, CONFIG_HEAP_SIZE);

	/* One public log symbol, resolved from the archives, not from libc. */
	(void)log_level_get();
	return 0;
}
