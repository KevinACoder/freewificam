/*
 * @file
 * @brief Common constants and arithmetic helpers.
 */

#ifndef _SYS_PARAM_H_
#define _SYS_PARAM_H_

#include <sys/types.h>
#include "endian.h"
#include <errno.h>
#include <limits.h>

/* the arm cacheline this port runs on */
#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif
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
#define rounddown(x, y) (((x) / (y)) * (y))
#define powerof2(x) ((((x) - 1) & (x)) == 0)

#define Hz 100

/* the drivers only need it as a size constant */
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#define PAGE_SHIFT 12
#define PAGE_MASK (PAGE_SIZE - 1)
#ifndef atop
#define atop(x)		((paddr_t) (x) >> PAGE_SHIFT)
#define ptoa(x)		((paddr_t) (x) << PAGE_SHIFT)
#endif

/* the pinned tree is netbsd-11 (11.0_STABLE) */
#ifndef __NetBSD_Version__
#define __NetBSD_Version__ 1100000000
#endif

/* hz/mstohz live in kernel.h too; the imported drivers include only
 * param.h and call mstohz - identical definitions, so including both
 * headers stays benign */
extern int hz;
#ifndef mstohz
#define mstohz(ms) ((ms) * hz / 1000)
#endif

#endif /* _SYS_PARAM_H_ */
