/*
 * @file
 * @brief process context shell: single context.
 */

#ifndef _COMPAT_SYS_PROC_H_
#define _COMPAT_SYS_PROC_H_

#include <sys/cdefs.h>

struct proc;
extern struct proc proc0_holder;
#define curproc ((struct proc *)NULL)

#endif /* _COMPAT_SYS_PROC_H_ */
