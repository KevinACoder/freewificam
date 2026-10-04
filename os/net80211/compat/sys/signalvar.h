/*
 * @file
 * @brief kernel signal delivery shell: the usb device node's async IO
 * path notifies a process owner; nothing owns one here.
 */

#ifndef _COMPAT_SYS_SIGNALVAR_H_
#define _COMPAT_SYS_SIGNALVAR_H_

#define SIGIO	23

struct proc;

static inline void psignal(struct proc *p, int sig) {
	(void) p; (void) sig;
}

#endif /* _COMPAT_SYS_SIGNALVAR_H_ */
