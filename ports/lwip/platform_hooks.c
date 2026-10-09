/*
 * @file    platform_hooks.c
 * @brief   lwIP's platform exit points: diagnostics, assertions, errno.
 *
 * The hooks cc.h points at, plus the one errno definition the socket world
 * needs. Kept apart from sys_arch.c on purpose: sys_arch.c is the kernel
 * adapter (the only file allowed to name CMSIS-RTOS2), while this file is
 * the platform edge - log sink, panic lander, libc stand-in.
 *
 *  - lwip_platform_diag: one lwIP diagnostic line through the stamped board
 *    log sink. Callers exist only when a translation unit is compiled with
 *    LWIP_DEBUG, which the configuration keeps off: the console runs at
 *    115200 and a debug flood drags the network path to a halt.
 *
 *  - lwip_platform_assert: a failed lwIP assertion prints and parks. A
 *    printed line is findable on the serial transcript; a compiled-out
 *    assertion is not. The print goes through the fault-path sink - an
 *    assertion can fire inside an ISR or with the console lock already held,
 *    where the regular sink would deadlock on its own lock - and the machine
 *    then parks, the serial line being the whole report.
 *
 *  - errno: with LWIP_PROVIDE_ERRNO the stack defines the errno constants
 *    itself (lwip/errno.h) and leaves the variable to the port. The image is
 *    freestanding, so this is its definition. The api/ world (sockets) is the
 *    only reader.
 *
 * @design kernel-services shim: the platform stand-in layer of the port,
 * companion of ports/lwip/include/arch/cc.h. Print and park disciplines
 * follow the log contract declared in os/baremetal/log.h:1-43 (fault-path
 * prints never take the console lock) and the existing panic lander shape of
 * os/threadx/board/rk3568-itx/heap.c:135-147 (emergency line, then park).
 */

#include <stdarg.h>

#include "log.h"
#include "minilibc.h"
#include "arch/aarch64/arch_asm.h"

#define LWIP_HOOKS_LINE_MAX 256u

void lwip_platform_diag(const char *fmt, ...)
{
	char buf[LWIP_HOOKS_LINE_MAX];
	va_list ap;
	size_t n;

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	/* The line is terminated here: the stack's own format strings do not all
	 * end with a newline, and the console splits a line only at its length
	 * cap - an unterminated print glues onto whatever is written next, and
	 * the split then cuts a line (a judged anchor included) in half. */
	n = strlen(buf);
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
		buf[--n] = '\0';
	board_log("%s\n", buf);
}

void lwip_platform_assert(const char *message, const char *file, int line)
{
	board_log_emergency("lwip: assert '%s' at %s:%d", message, file, line);
	for (;;) {
		ARCH_WFE();
	}
}

/* The one errno definition (lwip/errno.h declares it extern under
 * LWIP_PROVIDE_ERRNO). */
int errno;
