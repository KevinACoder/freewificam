/*
 * @file
 * @brief PRNG shell.
 *
 * get_random_bytes() is provided by net80211/ieee80211_netbsd.c on top
 * of cprng_fast(); the port implements cprng_fast().
 */

#ifndef _COMPAT_SYS_CPRNG_H_
#define _COMPAT_SYS_CPRNG_H_

#include <sys/types.h>

typedef struct { int unused; } cprng_strong_t;

void cprng_fast(void *buf, size_t len);

#endif /* _COMPAT_SYS_CPRNG_H_ */
