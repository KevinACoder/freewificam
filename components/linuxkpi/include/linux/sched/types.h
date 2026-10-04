#ifndef _LINUXKPI_LINUX_SCHED_TYPES_H_
#define _LINUXKPI_LINUX_SCHED_TYPES_H_
/* [S5b-W1] rtl8851bu funnel header (osdep_service.h:45 resolves it whenever
 * PLATFORM_LINUX && LINUX_VERSION_CODE >= 4.11).  The RTOS build has no
 * scheduler-tuning face; struct sched_param exists only so the
 * CONFIG_RECV_THREAD_MODE include set parses (that mode is not compiled). */
struct sched_param {
	int sched_priority;
};
#endif
