/*
 * @file   gdb_arch.c
 * @brief  AArch64 (EL1) debug-state side of the serial GDB stub.
 *
 * Register-file layout and the ESR decode follow the prior image's stub
 * (itself from FreeBSD sys/arm64/arm64/gdb_machdep.c, BSD-2). Z0 patches
 * the reserved BRK #GDB_BRK_IMM into RAM text - the ONLY breakpoint class
 * this boot chain (BL31 + OP-TEE + U-Boot) delivers: the debug registers
 * hold every value written to them, but self-hosted debug EVENTS other
 * than BRK never fire (breakpoint EC 0x31, software step EC 0x32,
 * watchpoint EC 0x35 are all silently suppressed above EL1; the BRK
 * instruction, EC 0x3c, is not a routed debug event and always traps).
 * The Z1/Z2-4 hardware paths are therefore not built at all here.
 *
 * FP/SIMD context does not exist in this image (-mgeneral-regs-only): the
 * target description served to the host (gdb_main.c's target_xml) declares
 * only the 34 core registers.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "cache.h"
#include "arch_asm.h"
#include "log.h"
#include "minilibc.h"
#include "uart.h"
#include "gdb.h"
#include "gdb_arch.h"
#include "gdb_int.h"

/* MDSCR_EL1 bits: software step / kernel debug enable / monitor debug
 * enable. This build arms none of them; setup clears them so boot state
 * cannot leak a live resource into the first session. */
#define MDSCR_SS	(1UL << 0)
#define MDSCR_KDE	(1UL << 13)
#define MDSCR_MDE	(1UL << 15)

#define MAX_BP		8U

static unsigned long bp_addr[MAX_BP];
static unsigned long bp_orig[MAX_BP];	/* patched-away words */
static unsigned int bp_enabled;
static int arch_ready;

/* The trapframe being debugged; gdb_trap_loop's register commands read and
 * write through it. Owned by gdb_debug_interrupt below. */
static struct gdb_trapframe *frame_current;

static unsigned long mdscr_read(void)
{
	unsigned long v;

	ARCH_MRS(mdscr_el1, v);
	return v;
}

static void mdscr_write(unsigned long v)
{
	__asm__ __volatile__("msr mdscr_el1, %0" :: "r"(v));
}

void gdb_arch_setup(void)
{
	unsigned long daif;
	unsigned long auth;
	unsigned long oslock;
	unsigned long probe = 0xdeadbeefUL;

	ARCH_MRS(daif, daif);
	ARCH_MRS(dbgauthstatus_el1, auth);
	ARCH_MRS(oslsr_el1, oslock);
	board_log("gdb: DAIF=%lx MDSCR=%lx\n", daif, mdscr_read());
	board_log("gdb: DBGAUTHSTATUS=%lx OSLSR=%lx\n", auth, oslock);

	/* Clear the OS lock (a bootloader/EL3 may leave it set; a set lock
	 * makes every debug register access trap). */
	__asm__ __volatile__("msr oslar_el1, %0" :: "r"(0UL));
	__asm__ __volatile__("isb" ::: "memory");
	ARCH_MRS(oslsr_el1, oslock);
	board_log("gdb: OSLSR after unlock=%lx\n", oslock);

	/* Boot evidence that EL1 HOLDS debug-register state: DBGBCR/BVR
	 * bits[1:0] are RAZ, so 0xdeadbeef reads back as deadbeec when the
	 * slot retains the write. */
	__asm__ __volatile__("msr dbgbvr0_el1, %0" :: "r"(probe));
	ARCH_MRS(dbgbvr0_el1, probe);
	board_log("gdb: dbgbvr0 readback=%lx (expect deadbeec)\n", probe);

	/* Reset the monitor debug bits, so boot state cannot leak a live
	 * resource into the first session. */
	mdscr_write(mdscr_read() & ~(MDSCR_SS | MDSCR_KDE | MDSCR_MDE));
	__asm__ __volatile__("isb" ::: "memory");

	bp_enabled = 0U;
	arch_ready = 1;
}

/* --- exception classification --------------------------------------------- */

int gdb_cpu_signal(const struct gdb_trapframe *tf)
{
	unsigned long ec = (tf->esr >> 26) & 0x3fUL;

	switch (ec) {
	/* breakpoint (lower/current EL), catch, software step,
	 * watchpoint, BRK */
	case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
	case 0x3c:
		return 5;	/* SIGTRAP */
	/* instruction abort, data abort */
	case 0x20: case 0x21: case 0x24: case 0x25:
		return 11;	/* SIGSEGV */
	/* PC alignment, SP alignment, SError */
	case 0x22: case 0x26: case 0x2f:
		return 7;	/* SIGBUS */
	default:
		return 4;	/* SIGILL - unknown/unallocated EC among them */
	}
}

static const char *stop_reason(const struct gdb_trapframe *tf)
{
	unsigned long ec = (tf->esr >> 26) & 0x3fUL;

	/* Our own compiled-in BRK and every Z0 patch report "swbreak": with
	 * that reason GDB knows the trap was a software breakpoint and runs
	 * its remove-patch/continue cycle instead of assuming a hardware
	 * stop. */
	if (ec == 0x3cUL) {
		return "swbreak;";
	}
	return 0;
}

/* The one sync-exception sink on the gdb build: EVERY synchronous
 * exception comes here - debug events become plain stops, real faults
 * become stops too (signal SEGV/BUS/ILL by EC class), which replaces the
 * fatal park: the host inspects the live scene instead of reading a
 * one-shot dump. Continue on a fault re-executes the faulting instruction
 * (the frame's elr is untouched), so the fault simply comes back into the
 * stub - under the host's control, which is the point. */
void gdb_debug_interrupt(struct gdb_trapframe *tf)
{
	unsigned long ec = (tf->esr >> 26) & 0x3fUL;
	unsigned long iss = tf->esr & 0x1ffffffUL;
	int signal;

	if (!gdb_dbgport_ready()) {
		/* No transport registered yet (fault before gdb_glue_init):
		 * nothing to talk to - report through the polled UART and
		 * park, exactly like the non-gdb build's fatal path. */
		uart_early_panic("fatal: synchronous exception (gdb dbgport"
				 " not ready)\n");
		for (;;) {
			__asm__ __volatile__("wfe");
		}
	}

	if (arch_ready == 0) {
		gdb_arch_setup();
	}

	/* NO uart prints on this path: every byte here lands in gdb's RSP
	 * stream, and the host-side reader does not survive stray text
	 * around the handshake (board-measured 2026-09-30). Stop evidence
	 * travels in the stop reply itself (signal + swbreak); the MCP ring
	 * shows the session's side effects via the resumed tick lines. */

	/* The compiled-in gate BRK (gdb_break / Ctrl-C) has no original
	 * instruction to execute: skip it. A Z0 patch does: leave its PC at
	 * the patched address so GDB can remove the patch, continue through
	 * the original instruction, then reinsert. */
	if (ec == 0x3cUL && iss == (unsigned long)GDB_BRK_IMM) {
		unsigned int i;

		for (i = 0U; i < bp_enabled; i++) {
			if (bp_addr[i] == tf->elr) {
				break;
			}
		}
		if (i == bp_enabled) {
			tf->elr += 4UL;
		}
	}

	signal = gdb_cpu_signal(tf);

	frame_current = tf;
	gdb_session_active = 1;
	gdb_dbgport_intr_ctrl(1);

	gdb_trap_loop(tf, signal, stop_reason(tf));

	/* There is deliberately NO blanket cleanup here: the command
	 * handlers own the debug state on exit ('c'/'D'/'k' keep or drop
	 * patches explicitly). */
	gdb_dbgport_intr_ctrl(0);
	gdb_session_active = 0;
}

/* --- register file --------------------------------------------------------- */

static unsigned long tf_reg(const struct gdb_trapframe *tf, int regnum)
{
	if (regnum <= GDB_REG_LR) {
		if (regnum == GDB_REG_LR) {
			return tf->lr;
		}
		return tf->x[regnum];
	}
	switch (regnum) {
	case GDB_REG_SP:
		return tf->sp;
	case GDB_REG_PC:
		return tf->elr;
	case GDB_REG_CPSR:
		return tf->spsr & 0xffffffffUL;
	default:
		return 0UL;
	}
}

static int tf_set_reg(struct gdb_trapframe *tf, int regnum, unsigned long v)
{
	if (regnum <= GDB_REG_LR) {
		if (regnum == GDB_REG_LR) {
			tf->lr = v;
		} else {
			tf->x[regnum] = v;
		}
		return 0;
	}
	switch (regnum) {
	case GDB_REG_SP:
		tf->sp = v;
		return 0;
	case GDB_REG_PC:
		tf->elr = v;
		return 0;
	case GDB_REG_CPSR:
		tf->spsr = (tf->spsr & ~0xffffffffUL) | (v & 0xffffffffUL);
		return 0;
	default:
		return -1;
	}
}

unsigned long gdb_cpu_regval(int regnum)
{
	if (regnum < 0 || regnum >= GDB_NREGS) {
		return 0UL;
	}
	return tf_reg(frame_current, regnum);
}

int gdb_cpu_setregval(int regnum, unsigned long v)
{
	if (regnum < 0 || regnum >= GDB_NREGS) {
		return -1;
	}
	return tf_set_reg(frame_current, regnum, v);
}

unsigned int gdb_cpu_getregs(char *buf, unsigned int bufsz)
{
	unsigned int o = 0U;
	unsigned char raw[8];
	int r;

	if (bufsz < 128U) {
		return 0U;
	}
	for (r = 0; r < GDB_NREGS; r++) {
		unsigned long v = tf_reg(frame_current, r);
		unsigned int width = (r == GDB_REG_CPSR) ? 4U : 8U;

		raw[0] = (unsigned char)(v);
		raw[1] = (unsigned char)(v >> 8);
		raw[2] = (unsigned char)(v >> 16);
		raw[3] = (unsigned char)(v >> 24);
		raw[4] = (unsigned char)(v >> 32);
		raw[5] = (unsigned char)(v >> 40);
		raw[6] = (unsigned char)(v >> 48);
		raw[7] = (unsigned char)(v >> 56);
		mem2hex(raw, &buf[o], width);
		o += width * 2U;
	}
	buf[o] = '\0';
	return o;
}

unsigned int gdb_cpu_setregs(const char *hex)
{
	unsigned char raw[8];
	int r;

	for (r = 0; r < GDB_NREGS; r++) {
		unsigned int width = (r == GDB_REG_CPSR) ? 4U : 8U;
		unsigned long v;

		if (hex2mem(hex, raw, width) != width) {
			return 1U;
		}
		v = (unsigned long)raw[0] |
		    ((unsigned long)raw[1] << 8) |
		    ((unsigned long)raw[2] << 16) |
		    ((unsigned long)raw[3] << 24) |
		    ((unsigned long)raw[4] << 32) |
		    ((unsigned long)raw[5] << 40) |
		    ((unsigned long)raw[6] << 48) |
		    ((unsigned long)raw[7] << 56);
		(void)tf_set_reg(frame_current, r, v);
		hex += width * 2U;
	}
	return 0U;
}

/* --- breakpoints ------------------------------------------------------------ *
 *
 * Z0 arms a SOFTWARE breakpoint - the reserved BRK #GDB_BRK_IMM patched
 * into RAM text, made visible to fetch by the cache sync recipe R1
 * delivered (cache_icache_sync_range: dsb ish, dc cvau over the range,
 * ic ialluis, dsb ish, isb). See the file header for why no other class
 * is built. */

#define BRK_INSN(imm16)	(0xD4200000UL | ((unsigned long)(imm16) << 5))

int gdb_cpu_set_hwbp(unsigned long addr)
{
	unsigned int i;

	if ((addr & 0x3UL) != 0UL) {
		return -1;	/* instructions are word-aligned here */
	}
	/* Replace if already present. */
	for (i = 0U; i < bp_enabled; i++) {
		if (bp_addr[i] == addr) {
			return 0;
		}
	}
	if (bp_enabled >= MAX_BP) {
		return -1;
	}
	i = bp_enabled++;
	bp_addr[i] = addr;
	bp_orig[i] = *(volatile unsigned int *)addr;
	*(volatile unsigned int *)addr =
		(unsigned int)BRK_INSN(GDB_BRK_IMM);
	cache_icache_sync_range(addr, 4UL);
	return 0;
}

/* Detach must leave the target running exactly as it was before the
 * session: every patched BRK restored - a surviving patch would trap the
 * next fetch through the old address with no host listening. */
void gdb_cpu_breakpoints_disarm(void)
{
	unsigned int i;

	for (i = 0U; i < bp_enabled; i++) {
		*(volatile unsigned int *)bp_addr[i] = bp_orig[i];
		cache_icache_sync_range(bp_addr[i], 4UL);
	}
	bp_enabled = 0U;
}

int gdb_cpu_clr_hwbp(unsigned long addr)
{
	unsigned int i;
	unsigned int j;

	for (i = 0U; i < bp_enabled; i++) {
		if (bp_addr[i] == addr) {
			*(volatile unsigned int *)bp_addr[i] = bp_orig[i];
			cache_icache_sync_range(bp_addr[i], 4UL);
			for (j = i; j + 1U < bp_enabled; j++) {
				bp_addr[j] = bp_addr[j + 1U];
				bp_orig[j] = bp_orig[j + 1U];
			}
			bp_enabled--;
			return 0;
		}
	}
	return -1;
}
