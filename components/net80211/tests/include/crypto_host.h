/**
 * @file
 * @brief Load host libc before the NetBSD port's kernel compatibility headers.
 * @author zhugengyu
 * @date 09.09.2026
 */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#undef setbit
#undef clrbit
#undef isset
#undef isclr
#include "../../compat/netbsd/sys/cdefs.h"
#include "../../compat/netbsd/sys/endian.h"
#include "../../port/port_config.h"
static inline void panic(const char *fmt, ...) {
	abort();
}
