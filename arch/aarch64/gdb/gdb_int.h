/*
 * @file   gdb_int.h
 * @brief  Internal contract between the stub's packet layer and command
 *         layer (the gdb_int.h role from the FreeBSD original).
 */

#ifndef FREEWEBCAM_GDB_INT_H
#define FREEWEBCAM_GDB_INT_H

#include <stdint.h>

/* packet layer (gdb_packet.c) */
void gdb_send_packet(const char *data, unsigned int len);
void gdb_reply_str(const char *s);
void gdb_reply_ok(void);
void gdb_reply_err(unsigned char err);
int gdb_get_packet(char *buf, unsigned int bufsz);
int hexval(char c);
unsigned int hex2mem(const char *hex, unsigned char *mem, unsigned int nbytes);
char *mem2hex(const unsigned char *mem, char *hex, unsigned int nbytes);

/* arch layer (gdb_arch.c) */
struct gdb_trapframe;
unsigned int gdb_cpu_getregs(char *buf, unsigned int bufsz);
unsigned int gdb_cpu_setregs(const char *hex);
int gdb_cpu_signal(const struct gdb_trapframe *tf);
int gdb_cpu_set_hwbp(unsigned long addr);	/* Z0: BRK patch (works here) */
int gdb_cpu_clr_hwbp(unsigned long addr);
void gdb_cpu_breakpoints_disarm(void);
unsigned long gdb_cpu_regval(int regnum);
int gdb_cpu_setregval(int regnum, unsigned long val);

/* command layer entry (gdb_main.c) */
void gdb_trap_loop(struct gdb_trapframe *tf, int signal, const char *reason);

#endif /* FREEWEBCAM_GDB_INT_H */
