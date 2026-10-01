/*
 * @file   lwip_diag.c
 * @brief  lwIP platform hooks: diagnostics, assertions, sys_now.
 *
 * The three entry points lwIP reaches outside its own tree in the NO_SYS=1
 * world (specs/lwip-adapt §3.1 条 5 / §3.2.2):
 *
 *  - lwip_arch_diag / lwip_arch_assert - the cc.h LWIP_PLATFORM_* hooks,
 *    routed into the stamped board_log sink (one line, one stamp; lwIP
 *    diagnostics never write the console directly),
 *
 *  - sys_now - the NO_SYS timebase timeouts.c runs on. Milliseconds off the
 *    OS tick count; the tick runs at 1000 Hz in the lwip-lan image (spec
 *    §3.1 条 5), but the conversion is written as a formula so a tick-rate
 *    change cannot silently bend every lwIP timeout.
 *
 * @author zhugengyu
 * @date   02.10.2026
 */

#include <stdarg.h>

#include "lwip/opt.h"
#include "lwip/sys.h"

#include "log.h"
#include "minilibc.h"
#include "tick.h"

void lwip_arch_diag(const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	board_log("%s", buf);
}

void lwip_arch_assert(const char *message, const char *file, int line)
{
	/* Bring-up stance: a failed assertion prints and parks. Compiled-out
	 * assertions are unfindable; a parked core is a gdb target. */
	board_log("lwip: ASSERT '%s' at %s:%d\n", message, file, line);
	__asm__ __volatile__("msr daifset, #0xf" ::: "memory");
	for (;;) {
		__asm__ __volatile__("wfi" ::: "memory");
	}
}

#if NO_SYS
/* NO_SYS=1 timebase (timeouts.c polls it from the main loop). Under
 * NO_SYS=0 sys_now comes from cmsis/sys_arch.c (osKernelGetTickCount) and
 * this definition must not exist next to it. */
u32_t sys_now(void)
{
	return (u32_t)((uint64_t)os_tick_get_count32() * 1000U /
		       os_tick_freq_hz());
}
#else
/* lwip/errno.h with LWIP_PROVIDE_ERRNO=1 declares `extern int errno` after
 * defining the constants: this is the one definition (the prior workspace's
 * adapter shape). The api/ world (sockets) is the only reader. */
int errno;
#endif /* NO_SYS */
