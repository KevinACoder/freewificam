/*
 * @file
 * @brief cpu_info shell: single CPU.
 */

#ifndef _COMPAT_SYS_CPU_H_
#define _COMPAT_SYS_CPU_H_

#include <sys/cdefs.h>

struct cpu_info;
#define curcpu() ((struct cpu_info *)NULL)
#define cpu_number() 0

void spl_raise_ipi(void);

#endif /* _COMPAT_SYS_CPU_H_ */
