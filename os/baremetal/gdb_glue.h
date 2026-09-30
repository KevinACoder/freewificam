/*
 * @file   gdb_glue.h
 * @brief  gdb stub integration surface (os interface; CONFIG_GDB_STUB builds).
 */

#ifndef FREEWEBCAM_GDB_GLUE_H
#define FREEWEBCAM_GDB_GLUE_H

/* Register the UART dbgport, run gdb_arch_setup, print the ready banner.
 * Call once from main, before the poll loop. */
void gdb_glue_init(void);

/* Main-loop watcher: breaks into a session on Ctrl-C (0x03, via a BRK
 * whose trap freezes real context) or on the first byte of an incoming
 * RSP frame ('$', host attached while running - enters a session with a
 * synthetic poll-site stop). Call every loop iteration. */
void gdb_poll(void);

/* gdb_poll's '$' branch, callable from any context that sees the first
 * byte of a host frame - with an interrupt-driven console that is the
 * shell's RX interrupt, which has already pulled `rest_len` bytes of the
 * host's first frame out of the FIFO and hands them back here (they ride
 * a stub-internal pushback so the session's first packet stays intact).
 * Runs the session to completion; returns after detach/continue. */
void gdb_isr_attach(int first_byte, const unsigned char *rest,
		    unsigned int rest_len);

/* Synchronous-exception entry, called from start.S's boot_sync_entry with
 * a full trapframe. No dbgport yet -> polled fatal park (the non-gdb
 * semantics); otherwise the stub owns the CPU until continue/detach. */
struct gdb_trapframe;
void gdb_sync_entry(struct gdb_trapframe *tf);

#endif /* FREEWEBCAM_GDB_GLUE_H */
