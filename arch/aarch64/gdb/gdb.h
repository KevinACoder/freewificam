/*
 * @file   gdb.h
 * @brief  Serial GDB stub core - transport contract and shared globals.
 *
 * The stub keeps the prior image's wire behaviour (derived from FreeBSD
 * sys/gdb, BSD-2, 2004 Marcel Moolenaar) but is wired through a tiny
 * dbgport ops table instead of any kernel service: the glue
 * (os/baremetal/gdb_glue.c) registers console-UART access at boot, the
 * core never reaches for a driver.
 */

#ifndef FREEWEBCAM_GDB_H
#define FREEWEBCAM_GDB_H

#include <stdint.h>

/* Wire buffer: one RSP packet + framing slack. Advertised to the host as
 * PacketSize=400 - conservative on purpose: 115200 8N1 with no flow control
 * and small frames survive best. */
#define GDB_BUFSZ	1024U

struct gdb_dbgport {
	void	(*putc)(char c);
	int	(*getc)(void);		/* -1 when the FIFO is empty */
	void	(*intr_ctrl)(int on);	/* mask (on) / unmask (off) console INTID */
};

/* Registered once by the glue before anything can trap. */
void gdb_dbgport_register(const struct gdb_dbgport *ops);

/* 1 once the dbgport is in - the sync-exception entry needs to know whether
 * a fault can be turned into a host stop or has to fall back to a raw
 * polled fatal. */
int gdb_dbgport_ready(void);

/* Session-boundary hook: mask (on=1) / unmask (on=0) the console INTID
 * while the stub owns the CPU. No-op when there is no dbgport. */
void gdb_dbgport_intr_ctrl(int on);

/* 1 while the stub owns the CPU (between a trap and its continue). The
 * log layer's stamped sink gates on this (weak board_console_muted) so
 * console output cannot corrupt the RSP stream mid-session. */
extern volatile int gdb_session_active;

/* Compiled-in entry: dsb + BRK #0x401 (the stub's reserved immediate). */
void gdb_break(void);

/* Called once by the glue, before anything can trap: clears the OS lock,
 * reports the debug architectural state, no register side effects beyond
 * OSLAR/MDSCR cleanup. */
void gdb_arch_setup(void);

#endif /* FREEWEBCAM_GDB_H */
