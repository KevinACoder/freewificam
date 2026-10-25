/*
 * @file    sys_mbox_seam.h
 * @brief   The mailbox traffic account of the lwIP port's sys_arch.
 *
 * The census prints `perf census: C lwip_msg`/`C lwip_sys`, but neither is a
 * post counter: the memp figure is a pool gauge and lwIP's sys-stats mbox
 * fields are only ever incremented by ports that call SYS_STATS_INC_USED on
 * mailbox creation, which this port does not. So the number that decides a
 * per-frame handoff question - how many times a frame was actually posted to
 * the TCP/IP mailbox versus handed to the input function in place - had no
 * probe at all, and a mode flip read as "no change" would have been the
 * instrument's silence, not the mechanism's.
 *
 * These are the real counts: sys_arch.c increments them in the mailbox entry
 * points themselves, so every caller (the stack's own includes) is covered.
 */

#ifndef SYS_MBOX_SEAM_H
#define SYS_MBOX_SEAM_H

struct sys_mbox_seam {
	unsigned long	post_ok;	/* trypost accepted a message */
	unsigned long	post_full;	/* trypost found the queue full */
	unsigned long	fetch_ok;	/* tryfetch/blocking fetch got one */
	unsigned long	fetch_empty;	/* tryfetch found none */
	unsigned long long fetch_wait_ms; /* total time fetch spent waiting */
};

void	sys_mbox_seam_get(struct sys_mbox_seam *out);

#endif /* SYS_MBOX_SEAM_H */
