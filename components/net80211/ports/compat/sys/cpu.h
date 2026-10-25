/*
 * @file
 * @brief cpu_info shell: one CPU.
 *
 * The imported stack touches this only to name the current CPU in diagnostics;
 * there is one CPU as far as the port is concerned.
 *
 * @design kernel-services shim: same declaration face as the component's compat
 * header, reduced to what a uniprocessor port can answer.
 */

#ifndef _WLAN_SYS_CPU_H_
#define _WLAN_SYS_CPU_H_

#include <sys/types.h>

struct cpu_info;

#define curcpu() ((struct cpu_info *) NULL)
#define cpu_number() 0

void spl_raise_ipi(void);

#endif /* _WLAN_SYS_CPU_H_ */
