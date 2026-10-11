/*
 * @file   arch_asm.h
 * @brief  Host spelling of the arch primitives the console outlet uses.
 *
 * The outlet suite compiles os/baremetal/log.c to exercise the line assembly,
 * the stamp and the repeat gate off the board. The only arch primitives that
 * file reaches for are the DAIF mask/restore pair around the print lock; on a
 * single-threaded host they are no-ops. The board's real header is
 * arch/aarch64/arch_asm.h and is not in this build's include path.
 */

#ifndef ULOG_LEVEL_TEST_ARCH_ASM_H
#define ULOG_LEVEL_TEST_ARCH_ASM_H

#define	ARCH_DAIF_GET(v)	do { (v) = 0u; } while (0)
#define	ARCH_DAIF_PUT(v)	do { (void)(v); } while (0)
#define	ARCH_IRQ_MASK()		do { } while (0)

#endif /* ULOG_LEVEL_TEST_ARCH_ASM_H */
