/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/notifier.h
 * @brief   The callback-block record and result codes of the kernel's notifier chains,
 *          which a driver's address-family hooks are registered as.
 *
 * @design declaration-only compatibility face, grown per consumer. The interface
 *         (include/linux/notifier.h) defines the four notifier-chain heads, their
 *         register/unregister wrappers and the whole result-code set; what the vendored
 *         bytes reach is the plain callback record it embeds by value (so the shape is
 *         transcribed field for field, including the __rcu marker on the chain link,
 *         which this project's contract header spells as nothing) and one result code.
 *         The chains themselves have no first-party body here: registering a callback
 *         against "all network interfaces" only means something to a stack that has
 *         interfaces, and which stack callback API the port offers is the port's
 *         decision - the declarations are deliberately omitted rather than given an inert
 *         body that would swallow a registration. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_NOTIFIER_H_
#define _LINUXKPI_LINUX_NOTIFIER_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/notifier.h: notifier_fn_t is the callback signature every notifier
 * block carries: the block itself, the event and its payload. */
struct notifier_block;

typedef int (*notifier_fn_t)(struct notifier_block *nb,
			     unsigned long action, void *data);

/* [LNX] include/linux/notifier.h:54-58, transcribed field for field. */
struct notifier_block {
	notifier_fn_t		notifier_call;
	struct notifier_block __rcu *next;
	int			priority;
};

/* [LNX] include/linux/notifier.h:185-193: the result codes. NOTIFY_DONE is the one the
 * compiled set returns ("nothing to do with this event"). */
#define	NOTIFY_DONE		0x0000		/* Don't care */
#define	NOTIFY_OK		0x0001		/* Suits me */
#define	NOTIFY_STOP_MASK	0x8000		/* Don't call further */
#define	NOTIFY_BAD		(NOTIFY_STOP_MASK | 0x0002)
#define	NOTIFY_STOP		(NOTIFY_OK | NOTIFY_STOP_MASK)

#endif /* _LINUXKPI_LINUX_NOTIFIER_H_ */
