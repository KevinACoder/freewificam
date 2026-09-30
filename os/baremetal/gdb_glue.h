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

/* Synchronous-exception entry, called from start.S's boot_sync_entry with
 * a full trapframe. No dbgport yet -> polled fatal park (the non-gdb
 * semantics); otherwise the stub owns the CPU until continue/detach. */
struct gdb_trapframe;
void gdb_sync_entry(struct gdb_trapframe *tf);

#endif /* FREEWEBCAM_GDB_GLUE_H */
