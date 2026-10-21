/*
 * @file
 * @brief Common constants and arithmetic helpers.
 */

#ifndef _SYS_PARAM_H_
#define _SYS_PARAM_H_

#include <sys/types.h>
#include "endian.h"
#include <errno.h>
#include <string.h>

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define MAXBSIZE 4096
#define MAXPATHLEN 1024
#define MAXHOSTNAMELEN 256

#define UPAGES 1
#define ALIGNBYTES 7
#define ALIGN(p) (((uintptr_t)(p) + ALIGNBYTES) & ~ALIGNBYTES)

#define howmany(n, d) ((((n) % (d)) == 0) ? ((n) / (d)) : (((n) / (d)) + 1))
#define roundup2(x, y) (((x)+((y)-1)) & ~((y)-1))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define powerof2(x) ((((x) - 1) & (x)) == 0)

#define Hz 100

#endif /* _SYS_PARAM_H_ */

/* the drivers only need it as a size constant */
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
