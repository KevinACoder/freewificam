/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/signal.h
 * @brief   The signal numbers, the handler sentinels and the kernel-thread signal
 *          hooks a driver's thread-start path names.
 *
 * @design declaration-only compatibility face, grown per consumer. The interface
 *         (include/linux/signal.h with the uapi constant set) describes signal
 *         delivery, which is one of the few mechanisms this project's kernels do not
 *         have: a kernel thread here has no signal queue and no handler table. What the
 *         vendored bytes name is therefore only the numeric constants and the two
 *         "kernel thread handles this itself" markers, and those are transcribed with
 *         the interface's exact values. allow_signal()/disallow_signal() are transcribed
 *         as the interface's own static inlines - they only record an intent in the
 *         signal-action table - and the one function they call (kernel_sigaction) is
 *         declared and deliberately left undefined: the port that gains signal delivery
 *         has to provide it, and until then the linker is where the missing piece
 *         shows, never a silently dropped intent. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_SIGNAL_H_
#define _LINUXKPI_LINUX_SIGNAL_H_

#include "../../linuxkpi_core.h"

#include <linux/sched.h>

/* [LNX] include/uapi/asm-generic/signal-defs.h:82-83 */
typedef void			__signalfn_t(int);
typedef __signalfn_t __user	*__sighandler_t;

/* [LNX] include/uapi/asm-generic/signal-defs.h:88-90 */
#define	SIG_DFL		((__force __sighandler_t)0)	/* default signal handling */
#define	SIG_IGN		((__force __sighandler_t)1)	/* ignore signal */
#define	SIG_ERR		((__force __sighandler_t)-1)	/* error return from signal */

/* The signal numbers the vendored bytes name ([LNX] include/uapi/asm-generic/signal.h,
 * the asm-generic numbering). */
#define	SIGINT		2
#define	SIGKILL		9
#define	SIGUSR1		10
#define	SIGUSR2		12
#define	SIGALRM		14
#define	SIGTERM		15

/* [LNX] include/linux/signal.h:299-300: the sentinel handlers marking a signal a kernel
 * thread handles itself. */
#define	SIG_KTHREAD		((__force __sighandler_t)2)
#define	SIG_KTHREAD_KERNEL	((__force __sighandler_t)3)

/*
 * [LNX] include/linux/signal.h:298: extern void kernel_sigaction(int, __sighandler_t);
 *
 * Deliberately undefined: installing a signal action needs the per-task action table,
 * which belongs to the port that grows signal delivery. Declaring it keeps the two
 * inlines below an exact transcription; a caller that reaches them asks the linker for
 * the real implementation instead of getting an inert no-op.
 */
extern void kernel_sigaction(int, __sighandler_t);

/* [LNX] include/linux/signal.h:301-325: a kernel thread says "I handle my own signals"
 * by installing the SIG_KTHREAD marker as the handler, which is what stops the signal
 * layer from turning an unhandled signal into SIGKILL or dropping it. The bodies are
 * the interface's, so the meaning of the call site is preserved exactly. */
static inline void allow_signal(int sig)
{
	kernel_sigaction(sig, SIG_KTHREAD);
}

static inline void allow_kernel_signal(int sig)
{
	kernel_sigaction(sig, SIG_KTHREAD_KERNEL);
}

static inline void disallow_signal(int sig)
{
	kernel_sigaction(sig, SIG_IGN);
}

#endif /* _LINUXKPI_LINUX_SIGNAL_H_ */
