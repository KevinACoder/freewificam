/*
 * @file   cmsis2_diag.h
 * @brief  Read-only kernel-state snapshot for the N7-stab fast probe.
 *
 * The 2026-10-02 sustained-TX freeze round needed per-thread liveness at a
 * cadence the 1 Hz heartbeat cannot provide: every sample reports each
 * CMSIS thread slot's run counter (ThreadX bumps it on every schedule-in),
 * so "which thread stopped being scheduled, and when" is directly readable
 * from the probe log - no symbols, no debugger. Stack high-water is the
 * other half: a wedged/overflowing thread shows up as a flat run counter
 * or a near-limit watermark in the same line.
 */

#ifndef FREEWEBCAM_CMSIS2_DIAG_H
#define FREEWEBCAM_CMSIS2_DIAG_H

typedef struct {
	const char *name;	/* NULL when the slot is free */
	unsigned int tx_prio;	/* mapped ThreadX priority */
	unsigned int state;	/* 0 free / 1 pending / 2 live */
	unsigned long runs;	/* tx_thread_run_count (0 when not created) */
	unsigned long stack_used;	/* bytes from the high-water mark */
	unsigned long stack_size;
} cmsis2_thread_snap_t;

/* Fill up to `max` entries (thread-slot order); returns the count filled. */
unsigned int cmsis2_thread_snapshot(cmsis2_thread_snap_t *out, unsigned int max);

/* Name of the thread holding the CPU at the call, "none" when idle. */
const char *cmsis2_current_thread_name(void);

#endif /* FREEWEBCAM_CMSIS2_DIAG_H */
