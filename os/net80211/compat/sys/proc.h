/*
 * @file
 * @brief process context shell: single context.
 */

#ifndef _COMPAT_SYS_PROC_H_
#define _COMPAT_SYS_PROC_H_

#include <sys/cdefs.h>
#include <sys/kthread.h>
#include <sys/kauth.h>
#include <sys/select.h>
#include "cmsis_os2.h"

struct vmspace {
	int vs_dummy;
};

struct proc {
	struct vmspace *p_vmspace;
	pid_t p_pid;
};

struct lwp {
	struct proc *l_proc;
	/* the port's lwp identity is the CMSIS thread handle; the audio(4)
	 * diagnostics print this as a thread id */
	int l_lid;
};

typedef struct proc proc_t;

extern struct proc proc0_holder;

/* the proc lock: one global no-op (single context carrier) */
struct kmutex;
struct kmutex *proc_lock(struct proc *p);
/* One process exists (the shell): "current" is that held proc, so
 * audio(4)'s async path has a real p_pid to record. */
#define curproc (&proc0_holder)

/* the "lwp" identity is the CMSIS thread handle: the kthread backend
 * (osal) hands out osThread handles, so identity comparisons between
 * curlwp and a created worker actually work */
#define curlwp ((lwp_t) osThreadGetId())

/* Bind the current lwp to the CPU while a lock-free lookup runs; with
 * one execution context there is nothing to pin - the calls bracket the
 * softc lookup in audio(4) (audio_sc_acquire_fromfile) */
#define curlwp_bind() ((int) 0)
#define curlwp_bindx(bound) ((void) (bound))

/* audio(4)'s async path resolves a pid to a proc before psignal(); there
 * is one process (the shell) here, so the lookup yields the held one */
struct proc *proc_find(pid_t);

/* tsleep priority: the value NetBSD's proc.h carries; the osal tsleep
 * ignores it */
#define PZERO 22

#endif /* _COMPAT_SYS_PROC_H_ */
